#pragma once

#include "gem_types.h"
#include <atomic>
#include <opencv2/opencv.hpp>

// ============================================================================
// Shape Classification - Color image comparison for gem detection
// ============================================================================

// Adjustable parameters for shape processing (thread-safe for trackbar updates)
namespace ShapeParams {
// Debug stage to visualize:
//   0 = original image
//   1 = preprocessed
//   2 = best matched reference image
//   3 = side-by-side (input | reference)
//   4 = difference heat map
//   5 = heat map overlaid on input
    inline std::atomic<int> debugStage{5};

// Match threshold percentage (0-100) - if a match exceeds this, stop searching
    inline std::atomic<int> matchThreshold{80};

// Reference asset scale (900-1100 maps to 0.900x - 1.100x)
// Used to compensate for size differences between assets and game rendering
    inline std::atomic<int> refScale{1080};

// Reference asset blur (1-15, odd values only)
// Used to soften reference images to match game's rendering
    inline std::atomic<int> refBlur{3};

/**
 * Create trackbars for shape parameters in the given window.
 */
    void CreateTrackbars(const std::string&windowName);
} // namespace ShapeParams

/**
 * Process a gem region for shape matching.
 */
cv::Mat ProcessGemForShapeMatching(const cv::Mat&gemRegion);

/**
 * Get debug visualization for a gem region.
 * Must be called AFTER ClassifyGemByShape to get the best match data.
 */
cv::Mat GetDebugVisualization(const cv::Mat&gemRegion);

/**
 * Classify a gem by comparing its color to reference images.
 */
GemType ClassifyGemByShape(const cv::Mat&processedImage,
                           float* outBestMatch = nullptr,
                           int* outBestFrame = nullptr);