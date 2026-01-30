#pragma once

#include "../types/frame_data.h"
#include "../types/timing.h"
#include <chrono>

// ============================================================================
// Process Thread - Detects gems and draws overlays
// ============================================================================

/**
 * Process thread function - finds CAPTURED buffer, processes it, marks as
 * PROCESSED. Detects gems and draws debug overlays.
 *
 * @param frameData Shared frame data for buffer management
 * @param timingInfo Timing information for performance tracking
 * @param currentFps Reference to current FPS value
 * @param frameCount Reference to frame counter
 * @param lastFpsUpdateTime Reference to last FPS update timestamp
 */
void ProcessThreadFunc(
    SharedFrameData &frameData, TimingInfo &timingInfo, float &currentFps,
    int &frameCount,
    std::chrono::high_resolution_clock::time_point &lastFpsUpdateTime);
