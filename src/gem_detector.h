#pragma once

#include <opencv2/opencv.hpp>
#include <vector>
#include <string>

// ============================================================================
// Gem Types and States
// ============================================================================

enum class GemType
{
  EMPTY,
  RED,
  ORANGE,
  YELLOW,
  GREEN,
  BLUE,
  PURPLE,
  WHITE,
  UNKNOWN
};

enum class GemState
{
  NORMAL,
  FLAME,     // For future: flame gem detection
  LIGHTNING, // For future: lightning gem detection
  STAR,      // For future: star gem detection
  HYPERCUBE  // For future: hypercube detection
};

struct Gem
{
  GemType type;
  GemState state;
  bool isMoving;
  cv::Point gridPos;   // Grid position (0-7, 0-7)
  cv::Rect roi;        // Region of interest in image
  cv::Scalar avgColor; // Average color for debugging
};

// ============================================================================
// Gem Detection Functions
// ============================================================================

/**
 * Convert gem type to string representation
 */
std::string GemTypeToString(GemType type);

/**
 * Convert gem type to display color (BGR)
 */
cv::Scalar GemTypeToColor(GemType type);

/**
 * Classify a gem based on its color in the provided region
 */
GemType ClassifyGemByColor(const cv::Mat &gemRegion);

/**
 * Check if a gem is moving by comparing current and previous frames
 */
bool IsGemMoving(const cv::Mat &prevFrame, const cv::Mat &currFrame, const cv::Rect &roi);

/**
 * Detect all gems in the game board grid
 * Returns a 2D vector representing the 8x8 grid
 */
std::vector<std::vector<Gem>> DetectGems(const cv::Mat &gameBoard, const cv::Mat &prevBoard);

/**
 * Draw debug overlay on the image showing grid and detected gems
 */
void DrawDebugOverlay(cv::Mat &image, const std::vector<std::vector<Gem>> &grid);

/**
 * Draw information banner at the top of the image
 * @param image The image to draw on (will be modified)
 * @param fps Current frames per second
 */
void DrawBanner(cv::Mat &image, float fps);

/**
 * Print the gem grid to console for debugging
 */
void PrintGemGrid(const std::vector<std::vector<Gem>> &grid);
