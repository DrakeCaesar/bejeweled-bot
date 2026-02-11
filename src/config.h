#pragma once

#include <opencv2/opencv.hpp>
#include <string>

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
const int DEBUG_STAGE_DEFAULT = 4;    // Default debug stage to show (0-5)

// Bot debug settings
const bool SHOW_BOT_MATCH_LINES = true; // Draw swap lines that create a match
const cv::Scalar BOT_MATCH_LINE_COLOR =
    cv::Scalar(255, 255, 0); // Cyan - best match
const cv::Scalar BOT_OTHER_MATCH_LINE_COLOR =
    cv::Scalar(0, 0, 255); // Red - other matches
const int BOT_MATCH_LINE_THICKNESS = 2;

// Auto-play settings
const bool BOT_STOP_AFTER_SUPERNOVA_FOUND =
    true; // Stop bot when supernova move is detected
const bool BOT_PREFER_NON_HYPERCUBE_MOVES =
    true; // Only match hypercubes if no other moves
const int BOT_STABLE_FRAMES_REQUIRED =
    20; // Number of stable frames required before making a move
const int BOT_CLICK_DELAY_MS =
    500; // Milliseconds to wait between clicks in auto-play

const int STATISICS_HISTORY_SIZE =
    100; // Number of frames to keep in rolling average for statistics

// Capture interval (ms)
const int CAPTURE_INTERVAL_MS = 10; // Capture every 10 ms

// Capture method
const bool USE_DESKTOP_DUPLICATION =
    false; // Use DXGI Desktop Duplication API (synced to display refresh) vs
// BitBlt

// Thread priority
const bool BOOST_THREAD_PRIORITY =
    true; // Reduce FPS drop when window loses focus

// Asset extraction
const bool WRITE_EXTRACTED_ASSETS_TO_DISK =
    false; // Keep in memory always; optionally write to disk

// Gem classification settings
// Note: SEARCH threshold controls early exit during matching phases (can be
// adjusted via UI) Note: FINAL threshold is hard-coded minimum before returning
// UNKNOWN type
const int GEM_MATCH_SEARCH_THRESHOLD_PERCENT =
    90; // Default search threshold (0-100) - shown in UI

// Per-color final thresholds (0-100) - classification threshold
const int GEM_MATCH_FINAL_THRESHOLD_PERCENT_BLUE = 60;
const int GEM_MATCH_FINAL_THRESHOLD_PERCENT_GREEN = 60;
const int GEM_MATCH_FINAL_THRESHOLD_PERCENT_ORANGE = 60;
const int GEM_MATCH_FINAL_THRESHOLD_PERCENT_PURPLE = 60;
const int GEM_MATCH_FINAL_THRESHOLD_PERCENT_RED = 60;
const int GEM_MATCH_FINAL_THRESHOLD_PERCENT_WHITE = 60;
const int GEM_MATCH_FINAL_THRESHOLD_PERCENT_YELLOW = 60;

// Star gem final thresholds (0-100) - used for special gems (STAR, SUPERNOVA)
const int GEM_MATCH_FINAL_THRESHOLD_PERCENT_STAR_BLUE = 60;
const int GEM_MATCH_FINAL_THRESHOLD_PERCENT_STAR_GREEN = 60;
const int GEM_MATCH_FINAL_THRESHOLD_PERCENT_STAR_ORANGE = 60;
const int GEM_MATCH_FINAL_THRESHOLD_PERCENT_STAR_PURPLE = 60;
const int GEM_MATCH_FINAL_THRESHOLD_PERCENT_STAR_RED = 60;
const int GEM_MATCH_FINAL_THRESHOLD_PERCENT_STAR_WHITE = 60;
const int GEM_MATCH_FINAL_THRESHOLD_PERCENT_STAR_YELLOW = 60;

// Internal thresholds for squared-distance comparison (pre-sqrt)
// These ensure the final sqrt-converted percentages match the config values
// above Derived from: score = 10000 - (100 - finalPercentage)^2 =
// finalPercentage * (200 - finalPercentage) For
// GEM_MATCH_SEARCH_THRESHOLD_PERCENT=90: 90 * (200 - 90) = 9900 For
// GEM_MATCH_FINAL_THRESHOLD_PERCENT=80: 80 * (200 - 80) = 9600
constexpr int GEM_MATCH_SEARCH_THRESHOLD_INTERNAL =
    GEM_MATCH_SEARCH_THRESHOLD_PERCENT *
    (200 - GEM_MATCH_SEARCH_THRESHOLD_PERCENT);

// Resolution-dependent values that will be set at runtime
inline int BOARD_X = 523;      // Will be set by application based on resolution
inline int BOARD_Y = 75;       // Will be set by application based on resolution
inline int BOARD_WIDTH = 1024; // Will be set by application based on resolution
inline int BOARD_HEIGHT =
    1024;                   // Will be set by application based on resolution
inline int CELL_SIZE = 128; // Will be set by application based on resolution
inline int BANNER_HEIGHT =
    200; // Will be set by application based on resolution
inline int FRAME_DIFF_REGION_WIDTH =
    25; // Will be set by application based on resolution
inline int FRAME_DIFF_REGION_HEIGHT =
    45; // Will be set by application based on resolution

// Banner colors (constant)
const cv::Scalar BANNER_BG_COLOR =
    cv::Scalar(40, 40, 40); // Dark gray background
const cv::Scalar BANNER_TEXT_COLOR = cv::Scalar(0, 255, 0); // Green text

// Resolution-specific configuration
struct ResolutionConfig {
  int boardX;                // X offset from left edge of window
  int boardY;                // Y offset from top edge of window
  int cellSize;              // Grid cell size in pixels
  int bannerHeight;          // Height of info banner
  int frameDiffRegionWidth;  // Frame diff region width
  int frameDiffRegionHeight; // Frame diff region height
};

// Resolution configurations for supported resolutions
inline ResolutionConfig GetResolutionConfig(int height) {
  switch (height) {
  case 600:
    return {
        261, // boardX
        37,  // boardY
        64,  // cellSize (8x8 grid of 64x64 cells)
        150, // bannerHeight (scaled down)
        25,  // frameDiffRegionWidth
        45   // frameDiffRegionHeight
    };
  case 768:
    return {
        334, // boardX
        47,  // boardY
        82,  // cellSize (8x8 grid of 82x82 cells)
        175, // bannerHeight (scaled down)
        25,  // frameDiffRegionWidth
        45   // frameDiffRegionHeight
    };
  case 1200:
  default:
    return {
        523, // boardX
        75,  // boardY
        128, // cellSize (8x8 grid of 128x128 cells)
        200, // bannerHeight
        25,  // frameDiffRegionWidth
        45   // frameDiffRegionHeight
    };
  }
}

} // namespace Config