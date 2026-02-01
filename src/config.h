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

// Color thresholds for gem classification (HSV color space)
// Hue range in OpenCV is 0-179. Red wraps around, so it is split into two
// ranges. These are tuned for Bejeweled 3 gem hues.
    const cv::Scalar RED_GEM_HSV_MIN_1 = cv::Scalar(0, 90, 90);
    const cv::Scalar RED_GEM_HSV_MAX_1 = cv::Scalar(8, 255, 255);
    const cv::Scalar RED_GEM_HSV_MIN_2 = cv::Scalar(172, 90, 90);
    const cv::Scalar RED_GEM_HSV_MAX_2 = cv::Scalar(179, 255, 255);

    const cv::Scalar ORANGE_GEM_HSV_MIN = cv::Scalar(9, 90, 90);
    const cv::Scalar ORANGE_GEM_HSV_MAX = cv::Scalar(20, 255, 255);

    const cv::Scalar YELLOW_GEM_HSV_MIN = cv::Scalar(21, 90, 90);
    const cv::Scalar YELLOW_GEM_HSV_MAX = cv::Scalar(34, 255, 255);

    const cv::Scalar GREEN_GEM_HSV_MIN = cv::Scalar(35, 80, 80);
    const cv::Scalar GREEN_GEM_HSV_MAX = cv::Scalar(85, 255, 255);

    const cv::Scalar BLUE_GEM_HSV_MIN = cv::Scalar(90, 80, 80);
    const cv::Scalar BLUE_GEM_HSV_MAX = cv::Scalar(125, 255, 255);

    const cv::Scalar PURPLE_GEM_HSV_MIN = cv::Scalar(126, 80, 80);
    const cv::Scalar PURPLE_GEM_HSV_MAX = cv::Scalar(155, 255, 255);

    const cv::Scalar WHITE_GEM_HSV_MIN = cv::Scalar(0, 0, 200);
    const cv::Scalar WHITE_GEM_HSV_MAX = cv::Scalar(179, 35, 255);

    const float COLOR_MATCH_THRESHOLD =
            0.10f; // Minimum match ratio for a confident detection

// Classification mode
    enum class ClassificationMode {
        COLOR, // Classify gems by color
        SHAPE // Classify gems by shape (placeholder)
    };

// Display settings
    const std::string DISPLAY_WINDOW_NAME = "Bejeweled 3 - Game Board";
    const int TARGET_FPS = 30; // Target frames per second for capture
    const bool SHOW_DEBUG_OVERLAY = true; // Show grid and detection overlay

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
    const int BANNER_HEIGHT = 30; // Height of info banner at top
    const cv::Scalar BANNER_BG_COLOR =
            cv::Scalar(40, 40, 40); // Dark gray background
    const cv::Scalar BANNER_TEXT_COLOR = cv::Scalar(0, 255, 0); // Green text
} // namespace Config