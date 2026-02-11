#pragma once

#include "config.h"
#include "gem_types.h"
#include <atomic>
#include <opencv2/opencv.hpp>

// ============================================================================
// Shape Classification - Color image comparison for gem detection
// ============================================================================

// Adjustable parameters for shape processing (thread-safe for trackbar updates)
namespace ShapeParams {
// Debug stage: 0=nothing, 1=input, 2=best ref, 3=diff map, 4=overlay,
// 5=ring/HSV
inline std::atomic<int> debugStage{Config::DEBUG_STAGE_DEFAULT};
// Match threshold percentage (0-100) - controls early exit during search
// User adjusts this value, which gets converted to internal threshold during
// comparison
inline std::atomic<int> matchThreshold{
    Config::GEM_MATCH_SEARCH_THRESHOLD_PERCENT};

void CreateTrackbars(const std::string &windowName);
} // namespace ShapeParams

/**
 * Process a gem region for shape matching (returns clone).
 */
cv::Mat ProcessGemForShapeMatching(const cv::Mat &gemRegion);

/**
 * Get debug visualization for the most recent gem classification.
 * Must be called after ClassifyGemByShape.
 */
cv::Mat GetDebugVisualization(const cv::Mat &gemRegion);

/**
 * Classify a gem by comparing its color to reference images.
 *
 * @param processedImage The gem image to classify (BGR)
 * @param processedImageLab LAB version of processedImage (pre-converted for
 * performance)
 * @param gemRow Row position in grid (for temporal coherence cache)
 * @param gemCol Column position in grid (for temporal coherence cache)
 * @param outBestMatch Optional score output (0.0-1.0)
 * @param outBestFrame Optional frame index output
 * @param needDebugMap Whether to compute debug visualization
 */
GemType ClassifyGemByShape(const cv::Mat &processedImage,
                           const cv::Mat &processedImageLab, int gemRow,
                           int gemCol, float *outBestMatch = nullptr,
                           int *outBestFrame = nullptr,
                           bool needDebugMap = false);