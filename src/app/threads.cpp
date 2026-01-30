#include "threads.h"
#include "helpers.h"
#include "config.h"
#include "window_capture.h"
#include "gem_detector.h"
#include <thread>
#include <iostream>

// ============================================================================
// Capture Thread
// ============================================================================

void CaptureThreadFunc(HWND gameWindow, SharedFrameData &frameData, TimingInfo &timingInfo)
{
  const int ACTIVATION_INTERVAL_MS = 1000;
  auto lastActivation = std::chrono::high_resolution_clock::now();

  const auto captureInterval = std::chrono::milliseconds(Config::CAPTURE_INTERVAL_MS);
  auto nextCaptureTime = std::chrono::steady_clock::now();

  // Use Desktop Duplication if available, fallback to BitBlt
  bool useDuplication = (frameData.dupContext != nullptr);

  const auto spinThreshold = std::chrono::microseconds(300);

  while (true)
  {
    // Pace capture to fixed interval (e.g., every 10 ms)
    auto nowSteady = std::chrono::steady_clock::now();
    if (nextCaptureTime < nowSteady)
    {
      nextCaptureTime = nowSteady;
    }

    auto timeToWait = nextCaptureTime - nowSteady;
    auto sleepUntil = (timeToWait > spinThreshold) ? (nextCaptureTime - spinThreshold) : nextCaptureTime;
    if (sleepUntil > nowSteady)
    {
      std::this_thread::sleep_until(sleepUntil);
    }

    while (std::chrono::steady_clock::now() < nextCaptureTime)
    {
      std::this_thread::yield();
    }

    nextCaptureTime += captureInterval;

    auto frameStart = std::chrono::high_resolution_clock::now();

    // Periodically keep game active
    auto now = std::chrono::high_resolution_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastActivation).count() >= ACTIVATION_INTERVAL_MS)
    {
      KeepGameActive(gameWindow);
      lastActivation = now;
    }

    bool captureSuccess = false;
    auto captureStart = std::chrono::high_resolution_clock::now();

    int localCaptureIndex = 0;
    {
      std::unique_lock<std::mutex> lock(frameData.mutex);
      frameData.cv.wait(lock, [&]
                        { return frameData.shouldExit || !frameData.captureDone; });
      if (frameData.shouldExit)
        break;
      localCaptureIndex = frameData.captureIndex;
    }

    // Get ROI for game board region (below banner)
    cv::Mat gameBoardROI = frameData.rawBoardBuffer[localCaptureIndex](
        cv::Rect(0, Config::BANNER_HEIGHT, Config::BOARD_WIDTH, Config::BOARD_HEIGHT));

    if (useDuplication)
    {
      // Convert window-relative coordinates to screen-absolute coordinates
      RECT windowRect;
      GetWindowRect(gameWindow, &windowRect);
      POINT clientOffset = {0, 0};
      ClientToScreen(gameWindow, &clientOffset);

      int screenX = clientOffset.x + Config::BOARD_X;
      int screenY = clientOffset.y + Config::BOARD_Y;

      // Desktop Duplication - blocks until new frame arrives (perfect sync!)
      captureSuccess = CaptureRegionFromDuplication(
          frameData.dupContext,
          screenX, screenY,
          Config::BOARD_WIDTH, Config::BOARD_HEIGHT,
          gameBoardROI,
          16 // 16ms timeout = ~60 FPS minimum
      );
    }
    else
    {
      // Fallback to BitBlt - capture into ROI
      captureSuccess = CaptureWindowRegionInto(
          gameWindow,
          Config::BOARD_X, Config::BOARD_Y,
          Config::BOARD_WIDTH, Config::BOARD_HEIGHT,
          gameBoardROI);
    }

    auto captureEnd = std::chrono::high_resolution_clock::now();

    if (!captureSuccess)
    {
      if (!useDuplication)
      {
        std::cerr << "Warning: Failed to capture window region" << std::endl;
      }
      // For duplication, timeout is normal, just continue
      continue;
    }

    {
      std::lock_guard<std::mutex> lock(frameData.mutex);

      if (frameData.shouldExit)
        break;

      // Update timing
      timingInfo.captureMs = std::chrono::duration<double, std::milli>(captureEnd - captureStart).count();

      frameData.captureDone = true;
      TryRotateBuffers(frameData);
    }
    frameData.cv.notify_all();
  }
}

// ============================================================================
// Process/Display Thread
// ============================================================================

void ProcessThreadFunc(SharedFrameData &frameData, TimingInfo &timingInfo, float &currentFps, int &frameCount,
                       std::chrono::high_resolution_clock::time_point &lastFpsUpdateTime)
{
  bool firstFrame = true;

  while (true)
  {
    int localProcessIndex = 0;
    int localPrevIndex = 0;
    {
      std::unique_lock<std::mutex> lock(frameData.mutex);
      frameData.cv.wait(lock, [&]
                        { return frameData.shouldExit || (!frameData.processDone && (frameData.hasProcessedFrame || frameData.captureDone)); });

      if (frameData.shouldExit)
        break;

      if (!frameData.hasProcessedFrame)
      {
        // First frame: process the freshly captured buffer
        frameData.processIndex = frameData.captureIndex;
        localProcessIndex = frameData.processIndex;
        localPrevIndex = frameData.processIndex;
      }
      else
      {
        localProcessIndex = frameData.processIndex;
        localPrevIndex = frameData.prevIndex;
      }
    }

    // Get ROI references for game board region (excluding banner)
    cv::Mat currentFrame = frameData.rawBoardBuffer[localProcessIndex](
        cv::Rect(0, Config::BANNER_HEIGHT, Config::BOARD_WIDTH, Config::BOARD_HEIGHT));

    if (!currentFrame.empty())
    {
      // Detect gems
      auto detectStart = std::chrono::high_resolution_clock::now();
      std::vector<std::vector<Gem>> gemGrid = DetectGems(currentFrame, frameData.classificationMode);
      auto detectEnd = std::chrono::high_resolution_clock::now();
      timingInfo.detectMs = std::chrono::duration<double, std::milli>(detectEnd - detectStart).count();

      // Draw banner and overlay directly onto the buffer (zero-copy!)
      auto drawStart = std::chrono::high_resolution_clock::now();

      {
        std::lock_guard<std::mutex> displayLock(frameData.mutex);

        cv::Mat &fullBuffer = frameData.gameBoardBuffer[localProcessIndex];
        cv::Mat &rawFullBuffer = frameData.rawBoardBuffer[localProcessIndex];

        // Copy latest raw capture into the display buffer
        rawFullBuffer(cv::Rect(0, Config::BANNER_HEIGHT, Config::BOARD_WIDTH, Config::BOARD_HEIGHT))
            .copyTo(fullBuffer(cv::Rect(0, Config::BANNER_HEIGHT, Config::BOARD_WIDTH, Config::BOARD_HEIGHT)));

        // Clear banner area
        fullBuffer(cv::Rect(0, 0, fullBuffer.cols, Config::BANNER_HEIGHT)).setTo(cv::Scalar(20, 20, 20));

        // Draw banner and overlay directly on buffer
        DrawBanner(fullBuffer, timingInfo.avgFps, timingInfo.avgCaptureMs, timingInfo.avgDetectMs,
                   timingInfo.avgDrawMs, timingInfo.avgDisplayMs, frameData.classificationMode,
                   timingInfo.maxFpsWidth, timingInfo.maxCaptureWidth, timingInfo.maxDetectWidth,
                   timingInfo.maxDrawWidth, timingInfo.maxDisplayWidth);

        if (Config::SHOW_DEBUG_OVERLAY)
        {
          DrawDebugOverlay(fullBuffer, gemGrid);
        }
      }

      auto drawEnd = std::chrono::high_resolution_clock::now();
      timingInfo.drawMs = std::chrono::duration<double, std::milli>(drawEnd - drawStart).count();

      // No copy needed! Previous frame is already in gameBoardBuffer[prevIndex]
    }

    {
      std::lock_guard<std::mutex> lock(frameData.mutex);
      frameData.processDone = true;
      frameData.hasProcessedFrame = true;
      TryRotateBuffers(frameData);
    }
    frameData.cv.notify_all();

    // Update rolling averages every frame
    frameCount++;
    auto now = std::chrono::high_resolution_clock::now();
    auto fpsElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFpsUpdateTime).count();

    // Calculate instantaneous FPS for this frame
    timingInfo.currentFps = (frameCount * 1000.0f) / fpsElapsed;
    timingInfo.updateAverages();

    // Print to console every 500ms
    if (fpsElapsed >= 500)
    {
      currentFps = timingInfo.avgFps; // Use smoothed FPS
      frameCount = 0;
      lastFpsUpdateTime = now;

      std::cout << "FPS: " << std::fixed << std::setprecision(1) << std::setw(timingInfo.maxFpsWidth) << timingInfo.avgFps
                << " | Capture: " << std::setprecision(2) << std::setw(timingInfo.maxCaptureWidth) << timingInfo.avgCaptureMs << "ms"
                << " | Detect: " << std::setw(timingInfo.maxDetectWidth) << timingInfo.avgDetectMs << "ms"
                << " | Draw: " << std::setw(timingInfo.maxDrawWidth) << timingInfo.avgDrawMs << "ms"
                << " | Display: " << std::setw(timingInfo.maxDisplayWidth) << timingInfo.avgDisplayMs << "ms"
                << std::endl;
    }
  }
}
