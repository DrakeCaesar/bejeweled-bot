#pragma once

#include "detection/gem_types.h"
#include <array>
#include <cstdint>
#include <opencv2/opencv.hpp>
#include <vector>

namespace MaskLoader {

// Constants - define here to avoid circular dependency with loader.h
constexpr int FRAMES_PER_GEM = 21; // 1 static + 20 animated
constexpr int HYPERCUBE_FRAMES = 60; // Total frames (10x6)
constexpr int NUM_GEM_TYPES = 7; // RED, ORANGE, YELLOW, GREEN, BLUE, PURPLE, WHITE

// Bitmask storage for SIMD optimization (64-bit masks, 1=gem pixel,
// 0=background)
constexpr int PIXELS_PER_MASK = 64;

// Type definitions
using BitmaskFrame = std::vector<uint64_t>;
using BitmaskStorage = std::array<std::array<BitmaskFrame, FRAMES_PER_GEM>, NUM_GEM_TYPES>;
using HypercubeBitmaskStorage = std::array<BitmaskFrame, HYPERCUBE_FRAMES>;

// Background color in LAB color space
constexpr uchar LAB_BG_L = 0;
constexpr uchar LAB_BG_A = 128;
constexpr uchar LAB_BG_B = 128;

// Storage accessors (defined in bitmasks.cpp)
extern BitmaskStorage &GetBitmaskStorage();
extern HypercubeBitmaskStorage &GetHypercubeBitmaskStorage();

/**
 * Compute bitmask for SIMD optimization from LAB mask
 * Each uint64_t represents 64 pixels (8x8 block) where 1=gem pixel,
 * 0=background Works for both standard gems and hypercubes
 *
 * @param labMask The LAB color space image
 * @return Vector of 64-bit masks
 */
BitmaskFrame ComputeBitmaskFromLabMask(const cv::Mat &labMask);

/**
 * Get bitmask for SIMD gem matching optimization.
 * Each uint64_t represents 64 pixels (1=gem pixel, 0=background in LAB space).
 *
 * @param type The gem type
 * @param frameIndex Frame index
 * @return Reference to the bitmask vector
 */
const BitmaskFrame &GetBitmask(GemType type, int frameIndex);

} // namespace MaskLoader
