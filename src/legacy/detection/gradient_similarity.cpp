// ============================================================================
// Gradient Similarity - Missing Edge Detection with Threshold
// ============================================================================
//
// For gradient images (white=edges, black=flat):
// leftover = max(0, reference - captured)
//
// This detects MISSING edges - places where reference has gradients
// but captured doesn't. Very white pixels in leftover drastically lower score.
//
// Algorithm:
// 1. Calculate reference edge energy (sum of white pixels in reference)
// 2. Calculate leftover: reference - captured, clipped at 0
// 3. Sum leftover values (how much edge is missing)
// 4. Score = (ref_energy - leftover) / ref_energy * 10000
//
// The more edges missing (bright leftover), the lower the score.
//
// ============================================================================

#include "gradient_similarity.h"
#include "assets/loader.h"
#include <algorithm>
#include <cmath>

// Adjustable threshold for what counts as an edge (0-255)
namespace {
int g_edgeThreshold = 60; // Default: pixels >= 60/255 count as edges
}

void InitializeGradientSimilarityLut() {
  // No LUT needed
}

int CalculateGradientSimilarity(const cv::Mat& capturedGradient,
                               const cv::Mat& referenceGradient,
                               const std::vector<uint64_t>& bitmask,
                               int imageWidth, int imageHeight) {
  int64_t refEnergy = 0;          // Sum of all reference edge pixels
  int64_t missingEdges = 0;       // Sum of leftover (reference - captured)
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
            int refVal = referenceBase[pixelOffset];

            // Sum reference edge energy (white pixels above threshold)
            if (refVal >= g_edgeThreshold) {
              refEnergy += refVal;
            }

            // Calculate leftover: reference - captured, clipped at 0
            int capturedVal = capturedBase[pixelOffset];
            int leftover = refVal - capturedVal;
            if (leftover > 0) {
              // Only count significant missing edges (above threshold in ref)
              if (refVal >= g_edgeThreshold) {
                missingEdges += leftover;
              }
            }

            pixelCount++;
          }
        }
      }
    }
  }

  if (pixelCount == 0 || refEnergy == 0) {
    return 0; // Avoid division by zero or no edges to match
  }

  // Calculate score: matched edges = reference energy minus missing edges
  // The more edges missing, the lower the score
  float matchedEnergy = static_cast<float>(refEnergy - missingEdges);
  float score = (matchedEnergy / static_cast<float>(refEnergy)) * 10000.0f;

  // Clamp score to valid range (0-10000)
  if (score < 0) score = 0;
  if (score > 10000) score = 10000;

  return static_cast<int>(score);
}

// Set edge detection threshold (0-255, higher = stricter edge definition)
void SetEdgeThreshold(int threshold) {
  g_edgeThreshold = std::max(0, std::min(255, threshold));
}

// Get current edge threshold
int GetEdgeThreshold() {
  return g_edgeThreshold;
}
