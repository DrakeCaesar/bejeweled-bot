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
    constexpr int MASK_COLS = 5;
    constexpr int MASK_ROWS = 4;
    constexpr int FRAMES_PER_GEM = 20;
    constexpr int SPRITE_WIDTH = 640;
    constexpr int SPRITE_HEIGHT = 512;
    constexpr int FRAME_WIDTH = 128;
    constexpr int FRAME_HEIGHT = 128;

// Hypercube has different dimensions
    constexpr int HYPERCUBE_COLS = 10;
    constexpr int HYPERCUBE_ROWS = 6;
    constexpr int HYPERCUBE_FRAMES = 60;
    constexpr int HYPERCUBE_SPRITE_WIDTH = 1280;
    constexpr int HYPERCUBE_SPRITE_HEIGHT = 768;
    constexpr int HYPERCUBE_FRAME_WIDTH = 128;
    constexpr int HYPERCUBE_FRAME_HEIGHT = 128;

// Number of gem types (excluding EMPTY, HYPERCUBE and UNKNOWN)
    constexpr int NUM_GEM_TYPES = 7; // RED, ORANGE, YELLOW, GREEN, BLUE, PURPLE, WHITE

/**
 * Check if a gem type uses fixed (pre-scaled) sprites that don't need scaling.
 * @param type The gem type to check
 * @return True if the gem uses fixed sprites at the correct scale
 */
    inline bool IsGemFixed(GemType type) {
        switch (type) {
            case GemType::HYPERCUBE:
                return true; // Hypercube sprites are already at correct scale
            default:
                return false; // Other gems still need scaling
        }
    }

// Storage for all reference images: [gem_type][frame_index] -> color image
// Indexed by (GemType - 1) since EMPTY=0 is not used
    using MaskStorage =
    std::array<std::array<cv::Mat, FRAMES_PER_GEM>, NUM_GEM_TYPES>;

// Storage for binary alpha masks: [gem_type][frame_index] -> grayscale mask
    using AlphaMaskStorage =
    std::array<std::array<cv::Mat, FRAMES_PER_GEM>, NUM_GEM_TYPES>;

// Storage for hypercube (separate due to different frame count)
    using HypercubeMaskStorage = std::array<cv::Mat, HYPERCUBE_FRAMES>;
    using HypercubeAlphaMaskStorage = std::array<cv::Mat, HYPERCUBE_FRAMES>;

/**
 * Load all gem reference images from the assets folder.
 * Images are stored as BGR color images.
 *
 * @param assetsPath Path to the assets folder (e.g., "../assets")
 * @return True if all reference images loaded successfully
 */
    bool LoadAllMasks(const std::string&assetsPath);

/**
 * Get the loaded masks storage.
 * Call LoadAllMasks first.
 *
 * @return Reference to the mask storage array
 */
    const MaskStorage& GetMasks();

/**
 * Check if masks have been loaded.
 */
    bool AreMasksLoaded();

/**
 * Get a specific mask for a gem type and frame.
 *
 * @param type The gem type (RED, ORANGE, YELLOW, GREEN, BLUE, PURPLE, WHITE, HYPERCUBE)
 * @param frameIndex Frame index 0-19 (or 0-29 for HYPERCUBE)
 * @return The mask image, or empty Mat if not found
 */
    const cv::Mat& GetMask(GemType type, int frameIndex);

/**
 * Get a specific alpha mask for a gem type and frame.
 * Alpha masks are used to isolate the gem from background.
 *
 * @param type The gem type (RED, ORANGE, YELLOW, GREEN, BLUE, PURPLE, WHITE, HYPERCUBE)
 * @param frameIndex Frame index 0-19 (or 0-29 for HYPERCUBE)
 * @return The alpha mask (grayscale, 0=background, 255=foreground), or empty Mat if not found
 */
    const cv::Mat& GetAlphaMask(GemType type, int frameIndex);

/**
 * Get the number of frames for a given gem type.
 */
    int GetFrameCount(GemType type);

/**
 * Check if static color masks have been loaded.
 */
    bool AreStaticColorsLoaded();

/**
 * Ensure scaled cache is built for the given parameters.
 * Call ONCE before parallel processing begins (main thread only).
 * Only scales color images - alpha masks are used as-is via GetAlphaMask.
 *
 * @param targetSize Target size for scaled color images
 * @param scaleInt Scale as integer for color images (1000 = 100%)
 * @param blurSize Gaussian blur kernel size to apply to reference images (1 = no blur)
 */
    void EnsureScaledCache(cv::Size targetSize, int scaleInt, int blurSize = 1);

/**
 * Get a pre-scaled reference color image. Must call EnsureScaledCache first.
 */
    const cv::Mat& GetScaledMask(GemType type, int frameIndex);
} // namespace MaskLoader