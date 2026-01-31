#pragma once

#include "gem_types.h"
#include <atomic>
#include <opencv2/opencv.hpp>

// ============================================================================
// Shape Classification - Mask-based gem shape detection
// ============================================================================

// Adjustable parameters for shape processing (thread-safe for trackbar updates)
namespace ShapeParams {
// Gaussian blur kernel size (must be odd, 1-15)
inline std::atomic<int> blurSize{2};

// Adaptive threshold block size (must be odd, 3-51)
inline std::atomic<int> blockSize{5};

// Adaptive threshold C constant (-20 to 20, controls threshold sensitivity)
inline std::atomic<int> thresholdC{2};

// Morphological kernel size (1-9)
inline std::atomic<int> morphSize{1};

// Erosion size for shrinking detected shapes (0-10, 0 = no erosion)
inline std::atomic<int> erodeSize{6};

// Debug stage to visualize (0=final, 1=grayscale, 2=blurred, 3=threshold,
// 4=morphed, 5=inverted, 6=contours, 7=filled, 8=cleaned, 9=eroded, 10=vs
// reference)
inline std::atomic<int> debugStage{0};

// Match threshold percentage (0-100) - if a match exceeds this, stop searching
// Early exit optimization: check first frame of each set, then explore in order
inline std::atomic<int> matchThreshold{98};

// Mask offset for alignment fine-tuning (-5 to +5 pixels)
// Stored as 0-10 range, interpreted as -5 to +5
inline std::atomic<int> maskOffsetX{6}; // 5 = center (0 offset)
inline std::atomic<int> maskOffsetY{6}; // 5 = center (0 offset)

/**
 * Create trackbars for shape parameters in the given window.
 * Call this once after creating the display window.
 */
void CreateTrackbars(const std::string &windowName);
} // namespace ShapeParams

/**
 * Process a gem region to create a binary mask suitable for shape matching.
 * The result is a white-on-black representation of the gem silhouette.
 *
 * @param gemRegion The BGR image region containing the gem
 * @return Binary mask (CV_8UC1) with white gem silhouette on black background
 */
cv::Mat ProcessGemForShapeMatching(const cv::Mat &gemRegion);

/**
 * Classify a gem by comparing its processed shape to reference masks.
 *
 * @param processedMask The binary mask from ProcessGemForShapeMatching
 * @param outBestMatch Optional output for the best match score [0..1]
 * @param outBestFrame Optional output for the best matching frame index (0-19)
 * @return Detected GemType, or UNKNOWN if no confident match
 */
GemType ClassifyGemByShape(const cv::Mat &processedMask,
                           float *outBestMatch = nullptr,
                           int *outBestFrame = nullptr);
