#include "capture_thread.h"
#include "../config.h"
#include "../detection/gem_detector.h"
#include "window_capture.h"
#include <chrono>
#include <iostream>
#include <thread>

// ============================================================================
// Capture Thread Implementation
// ============================================================================

void CaptureThreadFunc(HWND gameWindow, SharedFrameData &frameData,
                       TimingInfo &timingInfo) {
  const int ACTIVATION_INTERVAL_MS = 1000;
  auto lastActivation = std::chrono::high_resolution_clock::now();

  const auto captureInterval =
      std::chrono::milliseconds(Config::CAPTURE_INTERVAL_MS);
  auto nextCaptureTime = std::chrono::steady_clock::now();

  // Use Desktop Duplication if available, fallback to BitBlt
  bool useDuplication = (frameData.dupContext != nullptr);

  const auto spinThreshold = std::chrono::microseconds(300);

  while (true) {
    // Skip frame pacing when recording for maximum capture speed
    if (!IsRecording()) {
      // Pace capture to fixed interval (e.g., every 10 ms)
      auto nowSteady = std::chrono::steady_clock::now();
      if (nextCaptureTime < nowSteady) {
        nextCaptureTime = nowSteady;
      }

      auto timeToWait = nextCaptureTime - nowSteady;
      auto sleepUntil = (timeToWait > spinThreshold)
                            ? (nextCaptureTime - spinThreshold)
                            : nextCaptureTime;
      if (sleepUntil > nowSteady) {
        std::this_thread::sleep_until(sleepUntil);
      }

      while (std::chrono::steady_clock::now() < nextCaptureTime) {
        std::this_thread::yield();
      }

      nextCaptureTime += captureInterval;
    }

    // Periodically keep game active
    auto now = std::chrono::high_resolution_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                              lastActivation)
            .count() >= ACTIVATION_INTERVAL_MS) {
      KeepGameActive(gameWindow);
      lastActivation = now;
    }

    // Find a FREE buffer to capture into
    int bufferIndex = -1;
    {
      std::unique_lock<std::mutex> lock(frameData.mutex);

      // Wait for a FREE buffer or exit signal
      frameData.cv.wait(lock, [&] {
        if (frameData.shouldExit)
          return true;
        bufferIndex = frameData.findBuffer(BufferState::FREE);
        return bufferIndex >= 0;
      });

      if (frameData.shouldExit)
        break;
    }

    // Capture into the buffer (outside mutex for performance)
    auto captureStart = std::chrono::high_resolution_clock::now();

    // Get ROI for game board region (below banner)
    cv::Mat gameBoardROI = frameData.frameBuffer[bufferIndex](cv::Rect(
        0, Config::BANNER_HEIGHT, Config::BOARD_WIDTH, Config::BOARD_HEIGHT));

    bool captureSuccess = false;

    if (useDuplication) {
      // Convert window-relative coordinates to screen-absolute coordinates
      RECT windowRect;
      GetWindowRect(gameWindow, &windowRect);
      POINT clientOffset = {0, 0};
      ClientToScreen(gameWindow, &clientOffset);

      int screenX = clientOffset.x + Config::BOARD_X;
      int screenY = clientOffset.y + Config::BOARD_Y;

      // Desktop Duplication - blocks until new frame arrives (perfect sync!)
      captureSuccess = CaptureRegionFromDuplication(
          frameData.dupContext, screenX, screenY, Config::BOARD_WIDTH,
          Config::BOARD_HEIGHT, gameBoardROI,
          16 // 16ms timeout = ~60 FPS minimum
      );
    } else {
      // Fallback to BitBlt - capture into ROI
      captureSuccess = CaptureWindowRegionInto(
          gameWindow, Config::BOARD_X, Config::BOARD_Y, Config::BOARD_WIDTH,
          Config::BOARD_HEIGHT, gameBoardROI);
    }

    auto captureEnd = std::chrono::high_resolution_clock::now();

    if (!captureSuccess) {
      if (!useDuplication) {
        std::cerr << "Warning: Failed to capture window region" << std::endl;
      }
      // Don't change state on failure, buffer stays FREE
      continue;
    }

    // Mark buffer as CAPTURED (ready for processing)
    {
      std::lock_guard<std::mutex> lock(frameData.mutex);
      if (frameData.shouldExit)
        break;

      timingInfo.captureMs =
          std::chrono::duration<double, std::milli>(captureEnd - captureStart)
              .count();
      frameData.bufferState[bufferIndex] = BufferState::CAPTURED;
    }
    frameData.cv.notify_all();
  }
}
