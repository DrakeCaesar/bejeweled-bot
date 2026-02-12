// ============================================================================
// Gradient Similarity - Simple absolute difference matching
// ============================================================================
//
// Simple and effective approach for gradient matching:
// 1. Calculate absolute differences between captured and reference gradients
// 2. Average the differences across all pixels
// 3. Convert to similarity score (lower avg diff = higher score)
//
// This matches the overlay visualization math exactly.
//
// ============================================================================

#include "gradient_similarity.h"
#include "assets/loader.h"
#include <algorithm>
#include <cmath>

void InitializeGradientSimilarityLut() {
  // No LUT needed for simple pixel-by-pixel approach
}

int CalculateGradientSimilarity(const cv::Mat& capturedGradient,
                               const cv::Mat& referenceGradient,
                               const std::vector<uint64_t>& bitmask,
                               int imageWidth, int imageHeight) {
  int64_t totalDiff = 0;
  int pixelCount = 0;

  const uchar* capturedBase = capturedGradient.ptr<uchar>(0);
  const uchar* referenceBase = referenceGradient.ptr<uchar>(0);

  // Process in 8x8 blocks using the bitmask
  for (int blockRow = 0; blockRow < 16; blockRow++) {
    for (int blockCol = 0; blockCol < 16; blockCol++) {
      int maskIdx = blockRow * 16 + blockCol;
      uint64_t mask = bitmask[maskIdx];

      if (mask == 0) {
        continue; // Skip empty blocks
      }

      int startY = blockRow * 8;
      int startX = blockCol * 8;

      // Check each pixel in the 8x8 block
      for (int y = startY; y < startY + 8 && y < imageHeight; y++) {
        for (int x = startX; x < startX + 8 && x < imageWidth; x++) {
          // Check if this pixel is in the mask
          int pixelIdxInBlock = (y - startY) * 8 + (x - startX);
          if (mask & (1ULL << pixelIdxInBlock)) {
            int pixelOffset = y * imageWidth + x;
            int diff = std::abs(static_cast<int>(capturedBase[pixelOffset]) -
                               static_cast<int>(referenceBase[pixelOffset]));
            totalDiff += diff;
            pixelCount++;
          }
        }
      }
    }
  }

  if (pixelCount == 0) {
    return 0; // Avoid division by zero
  }

  // Calculate average difference normalized by mask size
  // This ensures fair comparison between gems of different sizes
  float avgDiffPerPixel = static_cast<float>(totalDiff) / pixelCount;

  // Convert to similarity score (0-10000)
  // Lower avg diff = higher score
  // Formula: score = 10000 * (1 - avgDiff/255)
  float score = 10000.0f * (1.0f - avgDiffPerPixel / 255.0f);

  return static_cast<int>(score);
}
