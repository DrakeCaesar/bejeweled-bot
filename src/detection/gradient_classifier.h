#pragma once

#include "config.h"
#include "gem_types.h"
#include <opencv2/opencv.hpp>

// ============================================================================
// Gradient Classification - Gradient-based gem matching for star overlay gems
// ============================================================================

/**
 * Classify a gem by comparing its gradient magnitude to reference images.
 * Used as a fallback when color-based matching fails (e.g., star overlay effect).
 *
 * @param processedImage The gem image to classify (BGR)
 * @param gemRow Row position in grid (for temporal coherence cache)
 * @param gemCol Column position in grid (for temporal coherence cache)
 * @param outBestMatch Optional score output (0.0-1.0)
 * @param outBestFrame Optional frame index output
 * @return The classified gem type, or GemType::UNKNOWN if no confident match
 */
GemType ClassifyGemByGradient(const cv::Mat &processedImage, int gemRow,
                              int gemCol, float *outBestMatch = nullptr,
                              int *outBestFrame = nullptr);

/**
 * Convert a BGR image to gradient magnitude in planar format.
 * Used by both the classifier and for generating reference gradients.
 *
 * @param bgrImage Input BGR image
 * @return Planar gradient magnitude image (GGG... format)
 */
cv::Mat ConvertToPlanarGradient(const cv::Mat &bgrImage);
