#pragma once

#include <opencv2/opencv.hpp>
#include <string>

// ============================================================================
// Gem Types and States
// ============================================================================

enum class GemType {
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

enum class GemState {
  NORMAL,
  FLAME,     // For future: flame gem detection
  LIGHTNING, // For future: lightning gem detection
  STAR,      // For future: star gem detection
  HYPERCUBE  // For future: hypercube detection
};

struct Gem {
  GemType type;
  GemType closestType; // Closest hue match (for debug when no confident match)
  GemState state;
  bool matchedThreshold;
  cv::Point gridPos;   // Grid position (0-7, 0-7)
  cv::Rect roi;        // Region of interest in image
  cv::Scalar avgColor; // Average color for debugging
  float bestMatch;     // Best match ratio [0..1] for debug
};

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Convert gem type to string representation
 */
std::string GemTypeToString(GemType type);

/**
 * Convert gem type to display color (BGR)
 */
cv::Scalar GemTypeToColor(GemType type);
