#pragma once

#include <cstdint>
#include <opencv2/opencv.hpp>
#include <vector>

/**
 * Calculate similarity between two gradient magnitude images.
 * Uses absolute differences with thresholding, more robust for gradients than
 * squared differences.
 *
 * Scoring approach:
 * 1. Calculate absolute differences between captured and reference gradients
 * 2. Only count pixels with differences >= 8 as "significantly different"
 * 3. Score = (ratio_of_similar_pixels * 10000) - penalty_based_on_avg_diff
 *
 * @param capturedGradient Single-channel planar gradient (GGG... format)
 * @param referenceGradient Single-channel planar reference gradient
 * @param bitmask 64-bit masks for 8x8 pixel blocks
 * @param imageWidth Width of the gradient images
 * @param imageHeight Height of the gradient images
 * @return Similarity score (0-10000, higher is better)
 */
int CalculateGradientSimilarity(const cv::Mat &capturedGradient,
                                const cv::Mat &referenceGradient,
                                const std::vector<uint64_t> &bitmask,
                                int imageWidth, int imageHeight);

/**
 * Initialize lookup tables for gradient similarity calculation.
 * Call once before using CalculateGradientSimilarity.
 */
void InitializeGradientSimilarityLut();

/**
 * Calculate gradient difference statistics with visualization.
 * Shared by classifier (for scoring) and overlay (for visualization).
 * Returns both aggregate statistics and a visual diff map.
 *
 * @param capturedGradient Single-channel planar gradient (GGG... format)
 * @param referenceGradient Single-channel planar reference gradient
 * @param bitmask 64-bit masks for 8x8 pixel blocks
 * @param imageWidth Width of the gradient images
 * @param imageHeight Height of the gradient images
 * @param diffThreshold Threshold for considering a difference significant
 * @param outDiffImage Output: 8-bit single channel diff image
 *                     Values: 0=masked/black, 100=similar(gray), 255=significant diff(red)
 * @param outTotalDiff Output: sum of absolute differences for significant pixels
 * @param outPixelCount Output: total number of pixels compared
 * @param outSignificantCount Output: number of pixels with diff >= threshold
 */
void CalculateGradientDiffWithVisualization(
    const cv::Mat &capturedGradient, const cv::Mat &referenceGradient,
    const std::vector<uint64_t> &bitmask, int imageWidth, int imageHeight,
    int diffThreshold, cv::Mat &outDiffImage, int &outTotalDiff,
    int &outPixelCount, int &outSignificantCount);
