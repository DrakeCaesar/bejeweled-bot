#include "bitmasks.h"
#include <iostream>

namespace MaskLoader {

// Static storage for bitmasks
static BitmaskStorage s_bitmasks;
static HypercubeBitmaskStorage s_hypercubeBitmasks;
static BitmaskFrame s_emptyBitmask(256, 0ULL); // 256 masks for 128x128 frame

// Storage for mask pixel counts (for score normalization)
static MaskPixelCountStorage s_maskPixelCounts;
static HypercubeMaskPixelCountStorage s_hypercubeMaskPixelCounts;

// Storage accessors
BitmaskStorage &GetBitmaskStorage() { return s_bitmasks; }
HypercubeBitmaskStorage &GetHypercubeBitmaskStorage() {
  return s_hypercubeBitmasks;
}

MaskPixelCountStorage &GetMaskPixelCountStorage() { return s_maskPixelCounts; }
HypercubeMaskPixelCountStorage &GetHypercubeMaskPixelCountStorage() {
  return s_hypercubeMaskPixelCounts;
}

// Get bitmask for SIMD gem matching optimization
const BitmaskFrame &GetBitmask(GemType type, int frameIndex) {
  if (type == GemType::HYPERCUBE) {
    if (frameIndex >= 0 && frameIndex < HYPERCUBE_FRAMES) {
      return s_hypercubeBitmasks[frameIndex];
    }
  } else {
    // Map GemType to index
    int gemIndex = -1;
    switch (type) {
    case GemType::RED:
      gemIndex = 0;
      break;
    case GemType::ORANGE:
      gemIndex = 1;
      break;
    case GemType::YELLOW:
      gemIndex = 2;
      break;
    case GemType::GREEN:
      gemIndex = 3;
      break;
    case GemType::BLUE:
      gemIndex = 4;
      break;
    case GemType::PURPLE:
      gemIndex = 5;
      break;
    case GemType::WHITE:
      gemIndex = 6;
      break;
    default:
      return s_emptyBitmask;
    }

    if (gemIndex >= 0 && gemIndex < NUM_GEM_TYPES && frameIndex >= 0 &&
        frameIndex < FRAMES_PER_GEM) {
      return s_bitmasks[gemIndex][frameIndex];
    }
  }

  return s_emptyBitmask;
}

// Get mask pixel count for normalization
int GetMaskPixelCount(GemType type, int frameIndex) {
  if (type == GemType::HYPERCUBE) {
    if (frameIndex >= 0 && frameIndex < HYPERCUBE_FRAMES) {
      return s_hypercubeMaskPixelCounts[frameIndex];
    }
  } else {
    // Map GemType to index
    int gemIndex = -1;
    switch (type) {
    case GemType::RED:
      gemIndex = 0;
      break;
    case GemType::ORANGE:
      gemIndex = 1;
      break;
    case GemType::YELLOW:
      gemIndex = 2;
      break;
    case GemType::GREEN:
      gemIndex = 3;
      break;
    case GemType::BLUE:
      gemIndex = 4;
      break;
    case GemType::PURPLE:
      gemIndex = 5;
      break;
    case GemType::WHITE:
      gemIndex = 6;
      break;
    default:
      return 0;
    }

    if (gemIndex >= 0 && gemIndex < NUM_GEM_TYPES && frameIndex >= 0 &&
        frameIndex < FRAMES_PER_GEM) {
      return s_maskPixelCounts[gemIndex][frameIndex];
    }
  }

  return 0;
}

// Compute bitmask for SIMD optimization from LAB mask
// Works for both standard gems and hypercubes since they have same dimensions
// Returns pair of (bitmask, active_pixel_count)
std::pair<BitmaskFrame, int> ComputeBitmaskFromLabMask(const cv::Mat &labMask) {
  BitmaskFrame bitmask;
  int activePixelCount = 0;
  const int totalPixels = labMask.cols * labMask.rows;
  const int masksPerFrame = totalPixels / PIXELS_PER_MASK;
  bitmask.reserve(masksPerFrame);

  for (int y = 0; y < labMask.rows; y += 8) {
    for (int x = 0; x < labMask.cols; x += 8) {
      uint64_t mask = 0;

      for (int dy = 0; dy < 8; dy++) {
        int rowIdx = y + dy;
        if (rowIdx >= labMask.rows)
          break;

        const cv::Vec3b *row = labMask.ptr<cv::Vec3b>(rowIdx);

        for (int dx = 0; dx < 8; dx++) {
          int colIdx = x + dx;
          if (colIdx >= labMask.cols)
            break;

          if (row[colIdx][0] != LAB_BG_L || row[colIdx][1] != LAB_BG_A ||
              row[colIdx][2] != LAB_BG_B) {
            mask |= (1ULL << (dy * 8 + dx));
            activePixelCount++; // Count this active pixel
          }
        }
      }

      bitmask.push_back(mask);
    }
  }

  return {bitmask, activePixelCount};
}

} // namespace MaskLoader
