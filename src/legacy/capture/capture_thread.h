#pragma once

#include "types/frame_data.h"
#include "types/timing.h"
#include <windows.h>

// ============================================================================
// Capture Thread - Continuously captures game window at fixed intervals
// ============================================================================

/**
 * Capture thread function - finds FREE buffer, captures into it, marks as
 * CAPTURED. Uses Desktop Duplication if available, falls back to BitBlt
 * otherwise.
 *
 * @param gameWindow Handle to the game window
 * @param frameData Shared frame data for buffer management
 * @param timingInfo Timing information for performance tracking
 */
void CaptureThreadFunc(HWND gameWindow, SharedFrameData &frameData,
                       TimingInfo &timingInfo);