#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include "config_resolution.h"

// ============================================================================
// CONFIGURATION CONSTANTS - Fill these out based on your setup
// ============================================================================

namespace Config {
// Window title of the Bejeweled 3 game (or part of it)
    const std::string WINDOW_TITLE = "Bejeweled 3";

// Grid configuration (constant across all resolutions)
    const int GRID_SIZE = 8; // 8x8 grid

// Display settings
    const std::string DISPLAY_WINDOW_NAME = "Bejeweled 3 - Game Board";
    const bool SHOW_DEBUG_OVERLAY = true; // Show grid and detection overlay
    const int DEBUG_STAGE_DEFAULT = 0; // Default debug stage to show (0-5)

// Bot debug settings
    const bool SHOW_BOT_MATCH_LINES = true; // Draw swap lines that create a match
    const cv::Scalar BOT_MATCH_LINE_COLOR = cv::Scalar(0, 255, 255);
    const int BOT_MATCH_LINE_THICKNESS = 2;

// Capture interval (ms)
    const int CAPTURE_INTERVAL_MS = 10; // Capture every 10 ms

// Capture method
    const bool USE_DESKTOP_DUPLICATION =
            false; // Use DXGI Desktop Duplication API (synced to display refresh) vs
    // BitBlt

// Thread priority
    const bool BOOST_THREAD_PRIORITY =
            true; // Reduce FPS drop when window loses focus

// Gem classification settings
// Note: SEARCH threshold controls early exit during matching phases (can be adjusted via UI)
// Note: FINAL threshold is hard-coded minimum before returning UNKNOWN type
    const int GEM_MATCH_SEARCH_THRESHOLD_PERCENT = 85;  // Default search threshold (0-100) - shown in UI
    const int GEM_MATCH_FINAL_THRESHOLD_PERCENT = 50;   // Hard minimum threshold (0-100) - classification threshold

// Internal thresholds for squared-distance comparison (pre-sqrt)
// These ensure the final sqrt-converted percentages match the config values above
// Derived from: score = 10000 - (100 - finalPercentage)^2 = finalPercentage * (200 - finalPercentage)
// For GEM_MATCH_SEARCH_THRESHOLD_PERCENT=90: 90 * (200 - 90) = 9900
// For GEM_MATCH_FINAL_THRESHOLD_PERCENT=80: 80 * (200 - 80) = 9600
    constexpr int GEM_MATCH_SEARCH_THRESHOLD_INTERNAL = GEM_MATCH_SEARCH_THRESHOLD_PERCENT * (200 - GEM_MATCH_SEARCH_THRESHOLD_PERCENT);
    constexpr int GEM_MATCH_FINAL_THRESHOLD_INTERNAL = GEM_MATCH_FINAL_THRESHOLD_PERCENT * (200 - GEM_MATCH_FINAL_THRESHOLD_PERCENT);

// Resolution-dependent values that will be set at runtime
inline int BOARD_X = 523;           // Will be set by application based on resolution
inline int BOARD_Y = 75;            // Will be set by application based on resolution
inline int BOARD_WIDTH = 1024;      // Will be set by application based on resolution
inline int BOARD_HEIGHT = 1024;     // Will be set by application based on resolution
inline int CELL_SIZE = 128;         // Will be set by application based on resolution
inline int BANNER_HEIGHT = 200;     // Will be set by application based on resolution
inline int FRAME_DIFF_REGION_WIDTH = 25; // Will be set by application based on resolution
inline int FRAME_DIFF_REGION_HEIGHT = 45; // Will be set by application based on resolution

// Banner colors (constant)
    const cv::Scalar BANNER_BG_COLOR = cv::Scalar(40, 40, 40); // Dark gray background
    const cv::Scalar BANNER_TEXT_COLOR = cv::Scalar(0, 255, 0); // Green text

} // namespace Config