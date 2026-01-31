#pragma once

#include "gem_types.h"
#include <array>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

// ============================================================================
// Reference Image Loading - Load color reference images for gem matching
// ============================================================================

namespace MaskLoader {

// Constants for reference image grid layout
constexpr int MASK_COLS = 5;       // 5 columns in the sprite sheet
constexpr int MASK_ROWS = 4;       // 4 rows in the sprite sheet
constexpr int FRAMES_PER_GEM = 20; // Total frames per gem (5 * 4)
constexpr int SPRITE_WIDTH = 640;  // Total sprite width
constexpr int SPRITE_HEIGHT = 512; // Total sprite height
constexpr int FRAME_WIDTH = 128;   // Width of each frame (640 / 5)
constexpr int FRAME_HEIGHT = 128;  // Height of each frame (512 / 4)

// Number of gem types (excluding EMPTY and UNKNOWN)
constexpr int NUM_GEM_TYPES = 7;

// Storage for all reference images: [gem_type][frame_index] -> color image
// Indexed by (GemType - 1) since EMPTY=0 is not used
using MaskStorage =
    std::array<std::array<cv::Mat, FRAMES_PER_GEM>, NUM_GEM_TYPES>;

// Storage for binary alpha masks: [gem_type][frame_index] -> grayscale mask
using AlphaMaskStorage =
    std::array<std::array<cv::Mat, FRAMES_PER_GEM>, NUM_GEM_TYPES>;

/**
 * Load all gem reference images from the assets folder.
 * Images are stored as BGR color images.
 *
 * @param assetsPath Path to the assets folder (e.g., "../assets")
 * @return True if all reference images loaded successfully
 */
bool LoadAllMasks(const std::string &assetsPath);

/**
 * Get the loaded masks storage.
 * Call LoadAllMasks first.
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
 * @param type The gem type (RED, ORANGE, YELLOW, GREEN, BLUE, PURPLE, WHITE)
 * @param frameIndex Frame index 0-19
 * @return The mask image, or empty Mat if not found
 */
const cv::Mat &GetMask(GemType type, int frameIndex);

/**
 * Get a specific alpha mask for a gem type and frame.
 * Alpha masks are used to isolate the gem from background.
 *
 * @param type The gem type (RED, ORANGE, YELLOW, GREEN, BLUE, PURPLE, WHITE)
 * @param frameIndex Frame index 0-19
 * @return The alpha mask (grayscale, 0=background, 255=foreground), or empty Mat if not found
 */
const cv::Mat &GetAlphaMask(GemType type, int frameIndex);

} // namespace MaskLoader
