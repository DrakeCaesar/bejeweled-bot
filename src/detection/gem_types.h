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
    HYPERCUBE,
    UNKNOWN
};

enum class GemState {
    NORMAL,
    FLAME, // For future: flame gem detection
    LIGHTNING, // For future: lightning gem detection
    STAR // For future: star gem detection
};

struct Gem {
    GemType type;
    GemType closestType; // Closest hue match (for debug when no confident match)
    GemState state;
    bool matchedThreshold;
    cv::Point gridPos; // Grid position (0-7, 0-7)
    cv::Rect roi; // Region of interest for color classification (smaller)
    cv::Rect shapeRoi; // Region of interest for shape classification (larger)
    cv::Scalar avgColor; // Average color for debugging
    float bestMatch; // Best match ratio [0..1] for debug
    int bestMatchFrame; // Best matching frame index (0-19) for shape mode
    cv::Mat
    shapeMask; // Binary mask for shape classification (white gem on black)
    int ringValue; // HSV V channel from ring around gem (debug)
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