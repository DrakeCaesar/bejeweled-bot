#pragma once

#include <cstdint>
#include <opencv2/opencv.hpp>
#include <vector>

/**
 * Calculate similarity between two gradient magnitude images.
 * Uses bidirectional subtraction: counts both missing AND extra edges.
 * Score = (total_edges - missing_edges - extra_edges) / total_edges * 10000
 *
 * For gradient images (white=edges, black=flat):
 * - missing_edges = max(0, reference - captured)  // Edges we should have but don't
 * - extra_edges = max(0, captured - reference)    // Edges we have but shouldn't
 *
 * Both missing and extra edges are bad for matching!
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
 * Set edge detection threshold (0-255, higher = stricter edge definition).
 * Pixels >= threshold in reference count as expected edges.
 */
void SetEdgeThreshold(int threshold);

/**
 * Get current edge detection threshold.
 */
int GetEdgeThreshold();

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

/**
 * Create visual debug image showing edge matching.
 * Shows:
 * - Black: masked areas
 * - Gray: correct match
 * - Yellow: missing edge (should have but doesn't)
 * - Red: extra edge (shouldn't have but does)
 *
 * @param capturedGradient Single-channel planar gradient (GGG... format)
 * @param referenceGradient Single-channel planar reference gradient
 * @param bitmask 64-bit masks for 8x8 pixel blocks
 * @param imageWidth Width of the gradient images
 * @param imageHeight Height of the gradient images
 * @param threshold Threshold for what counts as an edge
 * @return 3-channel BGR debug visualization
 */
cv::Mat CreateEdgeMatchVisualization(
    const cv::Mat &capturedGradient, const cv::Mat &referenceGradient,
    const std::vector<uint64_t> &bitmask, int imageWidth, int imageHeight,
    int threshold);
