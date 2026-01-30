#pragma once

#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include "config.h"

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
  GemType closestType; // Closest hue match (for debug when no confident match)
  GemState state;
  bool isMoving;
  bool matchedThreshold;
  cv::Point gridPos;   // Grid position (0-7, 0-7)
  cv::Rect roi;        // Region of interest in image
  cv::Scalar avgColor; // Average color for debugging
  float bestMatch;     // Best match ratio [0..1] for debug
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
 * Classify a gem based on the specified classification mode
 * For COLOR mode: classifies by color using HSV thresholds
 * For SHAPE mode: returns UNKNOWN (placeholder for future shape classification)
 * Outputs best match ratio and closest hue match for debug display
 */
GemType ClassifyGemByColor(const cv::Mat &gemRegion, float *outBestMatch, GemType *outClosestType, Config::ClassificationMode mode = Config::ClassificationMode::COLOR);

/**
 * Check if a gem is moving by comparing current and previous frames
 */
bool IsGemMoving(const cv::Mat &prevFrame, const cv::Mat &currFrame, const cv::Rect &roi);

/**
 * Detect all gems in the game board grid
 * Returns a 2D vector representing the 8x8 grid
 */
std::vector<std::vector<Gem>> DetectGems(const cv::Mat &gameBoard, const cv::Mat &prevBoard, Config::ClassificationMode mode = Config::ClassificationMode::COLOR);

/**
 * Draw debug overlay on the image showing grid and detected gems
 */
void DrawDebugOverlay(cv::Mat &image, const std::vector<std::vector<Gem>> &grid);

/**
 * Draw information banner at the top of the image
 * @param image The image to draw on (will be modified)
 * @param fps Current frames per second
 * @param captureMs Capture time in milliseconds
 * @param detectMs Detection time in milliseconds
 * @param drawMs Draw time in milliseconds
 * @param displayMs Display time in milliseconds
 * @param classificationMode Current classification mode (COLOR or SHAPE)
 * @param fpsWidth Width for FPS padding
 * @param captureWidth Width for capture padding
 * @param detectWidth Width for detect padding
 * @param drawWidth Width for draw padding
 * @param displayWidth Width for display padding
 */
void DrawBanner(cv::Mat &image, float fps, double captureMs = 0, double detectMs = 0,
                double drawMs = 0, double displayMs = 0, Config::ClassificationMode classificationMode = Config::ClassificationMode::COLOR,
                int fpsWidth = 5, int captureWidth = 4, int detectWidth = 4,
                int drawWidth = 4, int displayWidth = 4);

/**
 * Print the gem grid to console for debugging
 */
void PrintGemGrid(const std::vector<std::vector<Gem>> &grid);
