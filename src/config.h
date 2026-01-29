#pragma once

#include <opencv2/opencv.hpp>
#include <string>

// ============================================================================
// CONFIGURATION CONSTANTS - Fill these out based on your setup
// ============================================================================

namespace Config
{

  // Window title of the Bejeweled 3 game (or part of it)
  const std::string WINDOW_TITLE = "Bejeweled 3";

  // Game board crop rectangle (relative to the captured window)
  // These values need to be calibrated based on your screen resolution and window size
  const int BOARD_X = 523;       // X offset from left edge of window
  const int BOARD_Y = 75;        // Y offset from top edge of window
  const int BOARD_WIDTH = 1024;  // Width of the game board area (1024x1024)
  const int BOARD_HEIGHT = 1024; // Height of the game board area

  // Grid configuration
  const int GRID_SIZE = 8;   // 8x8 grid
  const int CELL_SIZE = 128; // Each cell is 128x128 pixels

  // Gem detection settings
  const int GEM_CENTER_SIZE = 80;       // Size of center region to sample (80x80 from center of 128x128 cell)
  const float MOTION_THRESHOLD = 50.0f; // Pixel difference threshold to detect motion

  // Color thresholds for gem classification (HSV color space)
  // These need to be calibrated based on actual game colors
  const cv::Scalar RED_GEM_HSV_MIN = cv::Scalar(0, 100, 100);
  const cv::Scalar RED_GEM_HSV_MAX = cv::Scalar(10, 255, 255);

  const cv::Scalar ORANGE_GEM_HSV_MIN = cv::Scalar(10, 100, 100);
  const cv::Scalar ORANGE_GEM_HSV_MAX = cv::Scalar(25, 255, 255);

  const cv::Scalar YELLOW_GEM_HSV_MIN = cv::Scalar(25, 100, 100);
  const cv::Scalar YELLOW_GEM_HSV_MAX = cv::Scalar(35, 255, 255);

  const cv::Scalar GREEN_GEM_HSV_MIN = cv::Scalar(35, 100, 100);
  const cv::Scalar GREEN_GEM_HSV_MAX = cv::Scalar(85, 255, 255);

  const cv::Scalar BLUE_GEM_HSV_MIN = cv::Scalar(85, 100, 100);
  const cv::Scalar BLUE_GEM_HSV_MAX = cv::Scalar(130, 255, 255);

  const cv::Scalar PURPLE_GEM_HSV_MIN = cv::Scalar(130, 100, 100);
  const cv::Scalar PURPLE_GEM_HSV_MAX = cv::Scalar(160, 255, 255);

  const cv::Scalar WHITE_GEM_HSV_MIN = cv::Scalar(0, 0, 200);
  const cv::Scalar WHITE_GEM_HSV_MAX = cv::Scalar(180, 30, 255);

  // Display settings
  const std::string DISPLAY_WINDOW_NAME = "Bejeweled 3 - Game Board";
  const int TARGET_FPS = 30;            // Target frames per second for capture
  const bool SHOW_DEBUG_OVERLAY = true; // Show grid and detection overlay

} // namespace Config
