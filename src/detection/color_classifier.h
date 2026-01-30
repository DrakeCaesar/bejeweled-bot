#pragma once

#include "../config.h"
#include "gem_types.h"
#include <opencv2/opencv.hpp>

// ============================================================================
// Color Classification - HSV-based gem color detection
// ============================================================================

/**
 * Classify a gem region by its dominant color using HSV thresholds.
 *
 * @param gemRegion The image region containing the gem
 * @param outBestMatch Optional output for the best match ratio [0..1]
 * @param outClosestType Optional output for the closest hue match (for debug)
 * @return Detected GemType, or UNKNOWN if no confident match
 */
GemType ClassifyGemByColor(const cv::Mat &gemRegion,
                           float *outBestMatch = nullptr,
                           GemType *outClosestType = nullptr);

/**
 * Calculate what percentage of pixels in an HSV image fall within a color
 * range.
 *
 * @param hsv The HSV image to analyze
 * @param minHSV Minimum HSV values
 * @param maxHSV Maximum HSV values
 * @return Ratio of matching pixels [0..1]
 */
float CalculateColorMatch(const cv::Mat &hsv, const cv::Scalar &minHSV,
                          const cv::Scalar &maxHSV);

/**
 * Calculate color match for a range that wraps around (like red hue).
 *
 * @param hsv The HSV image to analyze
 * @param minHSV1 First range minimum
 * @param maxHSV1 First range maximum
 * @param minHSV2 Second range minimum
 * @param maxHSV2 Second range maximum
 * @return Ratio of matching pixels [0..1]
 */
float CalculateColorMatchUnion(const cv::Mat &hsv, const cv::Scalar &minHSV1,
                               const cv::Scalar &maxHSV1,
                               const cv::Scalar &minHSV2,
                               const cv::Scalar &maxHSV2);

/**
 * Find the closest gem type by hue distance when no threshold is met.
 *
 * @param meanH Mean hue value
 * @param meanS Mean saturation value
 * @param meanV Mean value (brightness)
 * @return Closest GemType by hue
 */
GemType FindClosestGemByHue(float meanH, float meanS, float meanV);
