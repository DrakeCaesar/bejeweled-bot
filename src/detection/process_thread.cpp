#include "process_thread.h"
#include "../config.h"
#include "../rendering/banner.h"
#include "../rendering/overlay.h"
#include "../rendering/control_panel.h"
#include "gem_detector.h"
#include <chrono>
#include <iomanip>
#include <iostream>

// ============================================================================
// Process Thread Implementation
// ============================================================================

void ProcessThreadFunc(
    SharedFrameData&frameData, TimingInfo&timingInfo, float&currentFps,
    int&frameCount,
    std::chrono::high_resolution_clock::time_point&lastFpsUpdateTime) {
    while (true) {
        // Find a CAPTURED buffer to process
        int bufferIndex = -1;
        Config::ClassificationMode mode; {
            std::unique_lock<std::mutex> lock(frameData.mutex);

            // Wait for a CAPTURED buffer or exit signal
            frameData.cv.wait(lock, [&] {
                if (frameData.shouldExit)
                    return true;
                bufferIndex = frameData.findBuffer(BufferState::CAPTURED);
                return bufferIndex >= 0;
            });

            if (frameData.shouldExit)
                break;

            mode = frameData.classificationMode;
        }

        // Process the buffer (detection outside mutex for performance)
        cv::Mat currentFrame = frameData.frameBuffer[bufferIndex](cv::Rect(
            0, Config::BANNER_HEIGHT, Config::BOARD_WIDTH, Config::BOARD_HEIGHT));

        if (!currentFrame.empty()) {
            bool recording = IsRecording();

            if (!recording) {
                // Detect gems
                auto detectStart = std::chrono::high_resolution_clock::now();
                std::vector<std::vector<Gem>>&gemGrid = DetectGems(currentFrame, mode);
                auto detectEnd = std::chrono::high_resolution_clock::now();
                timingInfo.detectMs =
                        std::chrono::duration<double, std::milli>(detectEnd - detectStart)
                        .count();

                // Draw overlays directly onto the buffer (in-place, zero-copy!)
                auto drawStart = std::chrono::high_resolution_clock::now();

                cv::Mat&buffer = frameData.frameBuffer[bufferIndex];

                // Clear banner area
                buffer(cv::Rect(0, 0, buffer.cols, Config::BANNER_HEIGHT))
                        .setTo(cv::Scalar(20, 20, 20));

                // Draw banner and overlay directly on buffer
                DrawBanner(buffer, timingInfo.avgFps, timingInfo.avgCaptureMs,
                           timingInfo.avgDetectMs, timingInfo.avgDrawMs,
                           timingInfo.avgDisplayMs, mode, timingInfo.maxFpsWidth,
                           timingInfo.maxCaptureWidth, timingInfo.maxDetectWidth,
                           timingInfo.maxDrawWidth, timingInfo.maxDisplayWidth);

                if (Config::SHOW_DEBUG_OVERLAY) {
                    if (mode == Config::ClassificationMode::SHAPE) {
                        DrawShapeOverlay(buffer, gemGrid);
                    }
                    else {
                        DrawDebugOverlay(buffer, gemGrid);
                    }
                }

                // Draw custom control panel on top
                DrawControlPanel(buffer);

                auto drawEnd = std::chrono::high_resolution_clock::now();
                timingInfo.drawMs =
                        std::chrono::duration<double, std::milli>(drawEnd - drawStart)
                        .count();
            }
            else {
                // When recording, skip processing but still update the banner to show status
                cv::Mat&buffer = frameData.frameBuffer[bufferIndex];
                buffer(cv::Rect(0, 0, buffer.cols, Config::BANNER_HEIGHT))
                        .setTo(cv::Scalar(20, 50, 20)); // Slightly green background for recording

                // Simple status message on banner
                std::string status = "RECORDING: " + std::to_string(GetRecordedFrameCount()) + " frames";
                cv::putText(buffer, status, cv::Point(20, 35), cv::FONT_HERSHEY_SIMPLEX, 0.8,
                            cv::Scalar(255, 255, 255), 2);

                // Draw custom control panel on top
                DrawControlPanel(buffer);

                timingInfo.detectMs = 0;
                timingInfo.drawMs = 0;
            }
        }

        // Mark buffer as PROCESSED (ready for display)
        {
            std::lock_guard<std::mutex> lock(frameData.mutex);
            frameData.bufferState[bufferIndex] = BufferState::PROCESSED;
        }
        frameData.cv.notify_all();

        // Update rolling averages every frame
        frameCount++;
        auto now = std::chrono::high_resolution_clock::now();
        auto fpsElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - lastFpsUpdateTime)
                .count();

        // Calculate instantaneous FPS for this frame
        timingInfo.currentFps = (frameCount * 1000.0f) / fpsElapsed;
        timingInfo.updateAverages();

        // Print to console every 500ms
        if (fpsElapsed >= 500) {
            currentFps = timingInfo.avgFps; // Use smoothed FPS
            frameCount = 0;
            lastFpsUpdateTime = now;

            // Disabled for debugging
            // std::cout << "FPS: " << std::fixed << std::setprecision(1)
            //           << std::setw(timingInfo.maxFpsWidth) << timingInfo.avgFps
            //           << " | Capture: " << std::setprecision(2)
            //           << std::setw(timingInfo.maxCaptureWidth)
            //           << timingInfo.avgCaptureMs << "ms"
            //           << " | Detect: " << std::setw(timingInfo.maxDetectWidth)
            //           << timingInfo.avgDetectMs << "ms"
            //           << " | Draw: " << std::setw(timingInfo.maxDrawWidth)
            //           << timingInfo.avgDrawMs << "ms"
            //           << " | Display: " << std::setw(timingInfo.maxDisplayWidth)
            //           << timingInfo.avgDisplayMs << "ms" << std::endl;
        }
    }
}