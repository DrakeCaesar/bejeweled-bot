#include "capture_thread.h"
#include "config.h"
#include "detection/gem_detector.h"
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

      if (IsRecording()) {
        // When recording, try to find a FREE buffer but don't wait long.
        // If no FREE buffer, use the oldest DISPLAYING or PROCESSED one to keep
        // recording moving, or just use a temporary buffer.
        bufferIndex = frameData.findBuffer(BufferState::FREE);

        if (bufferIndex < 0) {
          // If we are recording and no FREE buffer, we might be blocked by the
          // display thread. Let's wait a tiny bit then try again, or just wait
          // normally. To ensure we don't miss frames, we SHOULD wait for a
          // buffer, but the processing thread is now much faster (skips work),
          // so FREE buffers should be available quickly.
          frameData.cv.wait_for(lock, std::chrono::milliseconds(5), [&] {
            if (frameData.shouldExit)
              return true;
            bufferIndex = frameData.findBuffer(BufferState::FREE);
            return bufferIndex >= 0;
          });
        }
      } else {
        // Normal mode: wait indefinitely for a FREE buffer
        frameData.cv.wait(lock, [&] {
          if (frameData.shouldExit)
            return true;
          bufferIndex = frameData.findBuffer(BufferState::FREE);
          return bufferIndex >= 0;
        });
      }

      if (frameData.shouldExit || bufferIndex < 0) {
        if (frameData.shouldExit)
          break;
        // If we still have no buffer and we are recording,
        // we can still capture into a temporary Mat just for recording.
      }
    }

    // Capture logic
    auto captureStart = std::chrono::high_resolution_clock::now();
    bool captureSuccess = false;
    cv::Mat tempBuffer;
    cv::Mat *targetMat = nullptr;

    if (bufferIndex >= 0) {
      targetMat = &frameData.frameBuffer[bufferIndex];
    } else {
      // Emergency buffer for recording if all shared buffers are busy
      // Use CV_8UC3 as established in window_capture.cpp
      tempBuffer = cv::Mat(Config::BOARD_HEIGHT + Config::BANNER_HEIGHT,
                           Config::BOARD_WIDTH, CV_8UC3);
      targetMat = &tempBuffer;
    }

    // Get ROI for game board region (below banner)
    cv::Mat gameBoardROI = (*targetMat)(cv::Rect(
        0, Config::BANNER_HEIGHT, Config::BOARD_WIDTH, Config::BOARD_HEIGHT));

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
    if (bufferIndex >= 0) {
      std::lock_guard<std::mutex> lock(frameData.mutex);
      if (frameData.shouldExit)
        break;

      timingInfo.captureMs =
          std::chrono::duration<double, std::milli>(captureEnd - captureStart)
              .count();
      frameData.bufferState[bufferIndex] = BufferState::CAPTURED;
    }

    // Process recording if enabled (outside mutex, syncs with capture)
    if (IsRecording()) {
      UpdateRecording(gameBoardROI);
    }

    if (bufferIndex >= 0) {
      frameData.cv.notify_all();
    }
  }
}
