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
    inline std::atomic<int> debugStage{4};

// Match threshold percentage (0-100) - if a match exceeds this, stop searching
    inline std::atomic<int> matchThreshold{80};

/**
 * Create trackbars for shape parameters in the given window.
 */
    void CreateTrackbars(const std::string&windowName);
} // namespace ShapeParams

/**
 * Process a gem region for matching.
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