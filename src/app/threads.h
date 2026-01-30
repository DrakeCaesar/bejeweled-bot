#pragma once

#include <windows.h>
#include <chrono>
#include "types/timing.h"
#include "types/frame_data.h"

// ============================================================================
// Thread Functions
// ============================================================================

// Capture thread - continuously captures game window at fixed intervals
void CaptureThreadFunc(HWND gameWindow, SharedFrameData &frameData, TimingInfo &timingInfo);

// Process/Display thread - detects gems and draws overlays
void ProcessThreadFunc(SharedFrameData &frameData, TimingInfo &timingInfo, float &currentFps, int &frameCount,
                       std::chrono::high_resolution_clock::time_point &lastFpsUpdateTime);
