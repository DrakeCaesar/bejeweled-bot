#pragma once

#include "gem_types.h"
#include <atomic>
#include <opencv2/opencv.hpp>
#include "config.h"

// ============================================================================
// Shape Classification - Color image comparison for gem detection
// ============================================================================

// Adjustable parameters for shape processing (thread-safe for trackbar updates)
namespace ShapeParams {
// Debug stage to visualize:
//   0 = nothing (maximum throughput)
//   1 = original image
//   2 = best matched reference image
//   3 = difference heat map
//   4 = heat map overlaid on input
//   5 = ring pixels and HSV values
    inline std::atomic<int> debugStage{0};

// Match threshold percentage (0-100) - if a match exceeds this, stop searching
    inline std::atomic<int> matchThreshold{Config::GEM_MATCH_FINAL_THRESHOLD_PERCENT};

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
 * 
 * @param processedImage The gem image to classify
 * @param processedImageLab Pre-converted LAB version of processedImage to avoid redundant conversion
 * @param gemRow Row position of the gem (for temporal coherence)
 * @param gemCol Column position of the gem (for temporal coherence)
 * @param outBestMatch Optional pointer to store the best match score
 * @param outBestFrame Optional pointer to store the best matching frame index
 * @param needDebugMap Whether debug visualization is needed (passed from frame level to avoid atomic loads)
 */
GemType ClassifyGemByShape(const cv::Mat&processedImage,
                           const cv::Mat&processedImageLab,
                           int gemRow,
                           int gemCol,
                           float* outBestMatch = nullptr,
                           int* outBestFrame = nullptr,
                           bool needDebugMap = false);