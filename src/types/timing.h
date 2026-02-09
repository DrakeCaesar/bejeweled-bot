#pragma once

#include "config.h"
#include <algorithm>
#include <iomanip>
#include <sstream>

// ============================================================================
// Timing Structure - Performance tracking with rolling averages
// ============================================================================

struct TimingInfo {
  static const int HISTORY_SIZE = Config::STATISICS_HISTORY_SIZE;
  double captureHistory[HISTORY_SIZE] = {0};
  double detectHistory[HISTORY_SIZE] = {0};
  double drawHistory[HISTORY_SIZE] = {0};
  double displayHistory[HISTORY_SIZE] = {0};
  double fpsHistory[HISTORY_SIZE] = {0};
  int historyIndex = 0;
  int validFrameCount = 0;

  // Current frame timings
  double captureMs = 0;
  double detectMs = 0;
  double drawMs = 0;
  double displayMs = 0;
  float currentFps = 0;

  // Computed averages
  double avgCaptureMs = 0;
  double avgDetectMs = 0;
  double avgDrawMs = 0;
  double avgDisplayMs = 0;
  float avgFps = 0;

  // Max widths for padding
  int maxFpsWidth = 5;
  int maxCaptureWidth = 4;
  int maxDetectWidth = 4;
  int maxDrawWidth = 4;
  int maxDisplayWidth = 4;

  void updateAverages() {
    captureHistory[historyIndex] = captureMs;
    detectHistory[historyIndex] = detectMs;
    drawHistory[historyIndex] = drawMs;
    displayHistory[historyIndex] = displayMs;
    fpsHistory[historyIndex] = currentFps;

    historyIndex = (historyIndex + 1) % HISTORY_SIZE;
    if (validFrameCount < HISTORY_SIZE) {
      validFrameCount++;
    }

    // Calculate averages using only valid frames
    double sumCapture = 0, sumDetect = 0, sumDraw = 0, sumDisplay = 0,
           sumFps = 0;
    for (int i = 0; i < validFrameCount; i++) {
      sumCapture += captureHistory[i];
      sumDetect += detectHistory[i];
      sumDraw += drawHistory[i];
      sumDisplay += displayHistory[i];
      sumFps += fpsHistory[i];
    }

    avgCaptureMs = sumCapture / validFrameCount;
    avgDetectMs = sumDetect / validFrameCount;
    avgDrawMs = sumDraw / validFrameCount;
    avgDisplayMs = sumDisplay / validFrameCount;
    avgFps = static_cast<float>(sumFps / validFrameCount);

    // Update max widths for consistent formatting
    auto updateWidth = [](int &width, double value, int precision) {
      std::ostringstream oss;
      oss << std::fixed << std::setprecision(precision) << value;
      width = std::max(width, static_cast<int>(oss.str().length()));
    };

    updateWidth(maxFpsWidth, avgFps, 1);
    updateWidth(maxCaptureWidth, avgCaptureMs, 2);
    updateWidth(maxDetectWidth, avgDetectMs, 2);
    updateWidth(maxDrawWidth, avgDrawMs, 2);
    updateWidth(maxDisplayWidth, avgDisplayMs, 2);
  }
};