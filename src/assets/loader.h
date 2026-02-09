#pragma once

#include "bitmasks.h"
#include "detection/gem_types.h"
#include <array>
#include <opencv2/opencv.hpp>
#include <string>
#include <unordered_map>
#include <vector>

// ============================================================================
// Reference Image Loading - Load color reference images for gem matching
// ============================================================================

namespace MaskLoader {

constexpr int MASK_COLS = 5;
constexpr int MASK_ROWS = 4;

// Hypercube has different dimensions (game assets)
constexpr int HYPERCUBE_COLS = 10; // Grid columns in source sprite sheet
constexpr int HYPERCUBE_ROWS = 6;  // Grid rows in source sprite sheet

// Output frame dimensions (resolution dependent - equals cell size)
// For 1200p: 128x128, for 768p: 82x82, for 600p: 64x64
inline int FRAME_WIDTH = 128;
inline int FRAME_HEIGHT = 128;
inline int HYPERCUBE_FRAME_WIDTH = 128;
inline int HYPERCUBE_FRAME_HEIGHT = 128;

// Storage for all reference images: [gem_type][frame_index] -> color image
// Indexed by (GemType - 1) since EMPTY=0 is not used
using MaskStorage =
    std::array<std::array<cv::Mat, FRAMES_PER_GEM>, NUM_GEM_TYPES>;

// Storage for Lab color space reference images (pre-converted for performance)
// Layout: Planar format with separate channels LLL...AAA...BBB... (128x128 per
// channel)
using PlanarLabMaskStorage =
    std::array<std::array<cv::Mat, FRAMES_PER_GEM>, NUM_GEM_TYPES>;

// Storage for interleaved Lab color space reference images (legacy format)
// To be removed after planar format is fully implemented
using LabMaskStorage =
    std::array<std::array<cv::Mat, FRAMES_PER_GEM>, NUM_GEM_TYPES>;

// Storage for binary alpha masks: [gem_type][frame_index] -> grayscale mask
using AlphaMaskStorage =
    std::array<std::array<cv::Mat, FRAMES_PER_GEM>, NUM_GEM_TYPES>;

// Storage for ring masks (scaled-up alpha minus original):
// [gem_type][frame_index]
using RingMaskStorage =
    std::array<std::array<cv::Mat, FRAMES_PER_GEM>, NUM_GEM_TYPES>;

// Storage for hypercube (separate due to different frame count)
using HypercubeMaskStorage = std::array<cv::Mat, HYPERCUBE_FRAMES>;
using HypercubeAlphaMaskStorage = std::array<cv::Mat, HYPERCUBE_FRAMES>;
using HypercubeRingMaskStorage = std::array<cv::Mat, HYPERCUBE_FRAMES>;
using HypercubePlanarLabMaskStorage = std::array<cv::Mat, HYPERCUBE_FRAMES>;
using HypercubeLabMaskStorage = std::array<cv::Mat, HYPERCUBE_FRAMES>;

/**
 * Load all gem reference images from the assets folder.
 * Images are stored as BGR color images.
 *
 * @param assetsPath Path to the assets folder (e.g., "../assets")
 * @param resolution Game resolution (600, 768, or 1200)
 * @return True if all reference images loaded successfully
 */
bool LoadAllImages(const std::string &assetsPath, int resolution);

// Provide extracted assets in memory (keyed by "<resolution>/<path>")
void SetInMemoryAssets(
    int resolution,
    std::unordered_map<std::string, std::vector<uint8_t>> assets);

// Clear any in-memory assets and fall back to disk loading
void ClearInMemoryAssets();

/**
 * Get the loaded masks storage.
 * Call LoadAllImages first.
 *
 * @return Reference to the mask storage array
 */
const MaskStorage &GetMasks();

/**
 * Check if masks have been loaded.
 */
bool AreMasksLoaded();

/**
 * Get a specific mask for a gem type and frame.
 *
 * @param type The gem type (RED, ORANGE, YELLOW, GREEN, BLUE, PURPLE, WHITE,
 * HYPERCUBE)
 * @param frameIndex Frame index 0-19 (or 0-29 for HYPERCUBE)
 * @return The mask image, or empty Mat if not found
 */
const cv::Mat &GetMask(GemType type, int frameIndex);

/**
 * Get a specific alpha mask for a gem type and frame.
 * Alpha masks are used to isolate the gem from background.
 *
 * @param type The gem type (RED, ORANGE, YELLOW, GREEN, BLUE, PURPLE, WHITE,
 * HYPERCUBE)
 * @param frameIndex Frame index 0-19 (or 0-29 for HYPERCUBE)
 * @return The alpha mask (grayscale, 0=background, 255=foreground), or empty
 * Mat if not found
 */
const cv::Mat &GetAlphaMask(GemType type, int frameIndex);

/**
 * Get a specific ring mask for a gem type and frame.
 * Ring masks are the scaled-up alpha mask minus the original alpha mask.
 *
 * @param type The gem type (RED, ORANGE, YELLOW, GREEN, BLUE, PURPLE, WHITE,
 * HYPERCUBE)
 * @param frameIndex Frame index 0-19 (or 0-29 for HYPERCUBE)
 * @return The ring mask (grayscale, 0=background, 255=ring), or empty Mat if
 * not found
 */
const cv::Mat &GetRingMask(GemType type, int frameIndex);

/**
 * Get a specific Lab color space reference image for a gem type and frame.
 * Lab images are pre-converted from BGR for performance optimization.
 *
 * @param type The gem type (RED, ORANGE, YELLOW, GREEN, BLUE, PURPLE, WHITE,
 * HYPERCUBE)
 * @param frameIndex Frame index 0-20 (or 0-59 for HYPERCUBE)
 * @return The Lab color space image, or empty Mat if not found
 */
const cv::Mat &GetLabMask(GemType type, int frameIndex);

/**
 * Get a specific planar Lab color space reference image for a gem type and
 * frame. Planar format: LLL...AAA...BBB... (128x128 per channel) for optimal
 * SIMD access.
 *
 * @param type The gem type (RED, ORANGE, YELLOW, GREEN, BLUE, PURPLE, WHITE,
 * HYPERCUBE)
 * @param frameIndex Frame index 0-20 (or 0-59 for HYPERCUBE)
 * @return The planar Lab color space image (3x128x128), or empty Mat if not
 * found
 */
const cv::Mat &GetPlanarLabMask(GemType type, int frameIndex);

/**
 * Get the number of frames for a given gem type.
 */
int GetFrameCount(GemType type);
} // namespace MaskLoader