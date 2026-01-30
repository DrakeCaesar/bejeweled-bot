#pragma once

#include <sstream>
#include <iomanip>
#include <algorithm>

// ============================================================================
// Timing Structure
// ============================================================================

struct TimingInfo
{
  // Rolling average over 100 frames
  static const int HISTORY_SIZE = 100;
  double captureHistory[HISTORY_SIZE] = {0};
  double detectHistory[HISTORY_SIZE] = {0};
  double drawHistory[HISTORY_SIZE] = {0};
  double displayHistory[HISTORY_SIZE] = {0};
  double fpsHistory[HISTORY_SIZE] = {0};
  int historyIndex = 0;
  int validFrameCount = 0; // Track how many frames we've actually collected

  // Current frame timings (for updating history)
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
  int maxFpsWidth = 5;     // "100.0" = 5 chars
  int maxCaptureWidth = 4; // "0.00" = 4 chars
  int maxDetectWidth = 4;
  int maxDrawWidth = 4;
  int maxDisplayWidth = 4;

  void updateAverages()
  {
    captureHistory[historyIndex] = captureMs;
    detectHistory[historyIndex] = detectMs;
    drawHistory[historyIndex] = drawMs;
    displayHistory[historyIndex] = displayMs;
    fpsHistory[historyIndex] = currentFps;

    historyIndex = (historyIndex + 1) % HISTORY_SIZE;
    if (validFrameCount < HISTORY_SIZE)
      validFrameCount++;

    // Calculate averages over valid frames only
    int count = validFrameCount;
    avgCaptureMs = 0;
    avgDetectMs = 0;
    avgDrawMs = 0;
    avgDisplayMs = 0;
    avgFps = 0;

    for (int i = 0; i < count; i++)
    {
      avgCaptureMs += captureHistory[i];
      avgDetectMs += detectHistory[i];
      avgDrawMs += drawHistory[i];
      avgDisplayMs += displayHistory[i];
      avgFps += fpsHistory[i];
    }

    avgCaptureMs /= count;
    avgDetectMs /= count;
    avgDrawMs /= count;
    avgDisplayMs /= count;
    avgFps /= count;

    // Update max widths
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << avgFps;
    maxFpsWidth = std::max(maxFpsWidth, (int)oss.str().length());

    oss.str("");
    oss << std::setprecision(2) << avgCaptureMs;
    maxCaptureWidth = std::max(maxCaptureWidth, (int)oss.str().length());

    oss.str("");
    oss << avgDetectMs;
    maxDetectWidth = std::max(maxDetectWidth, (int)oss.str().length());

    oss.str("");
    oss << avgDrawMs;
    maxDrawWidth = std::max(maxDrawWidth, (int)oss.str().length());

    oss.str("");
    oss << avgDisplayMs;
    maxDisplayWidth = std::max(maxDisplayWidth, (int)oss.str().length());
  }
};
