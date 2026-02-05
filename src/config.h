#pragma once

#include <opencv2/opencv.hpp>
#include <string>

// ============================================================================
// CONFIGURATION CONSTANTS - Fill these out based on your setup
// ============================================================================

namespace Config {
// Window title of the Bejeweled 3 game (or part of it)
    const std::string WINDOW_TITLE = "Bejeweled 3";

// Game board crop rectangle (relative to the captured window)
// These values need to be calibrated based on your screen resolution and window
// size
    const int BOARD_X = 523; // X offset from left edge of window
    const int BOARD_Y = 75; // Y offset from top edge of window
    const int BOARD_WIDTH = 1024; // Width of the game board area (1024x1024)
    const int BOARD_HEIGHT = 1024; // Height of the game board area

// Grid configuration
    const int GRID_SIZE = 8; // 8x8 grid
    const int CELL_SIZE = 128; // Each cell is 128x128 pixels

// Display settings
    const std::string DISPLAY_WINDOW_NAME = "Bejeweled 3 - Game Board";
    const bool SHOW_DEBUG_OVERLAY = true; // Show grid and detection overlay

// Frame deduplication settings
    const int FRAME_DIFF_REGION_WIDTH = 25; // Width of center region (pixels) for frame uniqueness comparison
    const int FRAME_DIFF_REGION_HEIGHT = 45; // Height of center region (pixels) for frame uniqueness comparison

// Capture interval (ms)
    const int CAPTURE_INTERVAL_MS = 10; // Capture every 10 ms

// Capture method
    const bool USE_DESKTOP_DUPLICATION =
            false; // Use DXGI Desktop Duplication API (synced to display refresh) vs
    // BitBlt

// Thread priority
    const bool BOOST_THREAD_PRIORITY =
            true; // Reduce FPS drop when window loses focus

// Banner settings
    const int BANNER_HEIGHT = 200; // Height of info banner at top
    const cv::Scalar BANNER_BG_COLOR =
            cv::Scalar(40, 40, 40); // Dark gray background
    const cv::Scalar BANNER_TEXT_COLOR = cv::Scalar(0, 255, 0); // Green text

// Gem classification settings
    const int GEM_MATCH_SEARCH_THRESHOLD_PERCENT = 80;  // Early exit threshold during search (0-100)
    const int GEM_MATCH_FINAL_THRESHOLD_PERCENT = 60;   // Final fallback threshold for classification (0-100)
    const bool USE_SQRT = false;    // Use fast integer sqrt vs squared distance without sqrt for similarity calculation

} // namespace Config