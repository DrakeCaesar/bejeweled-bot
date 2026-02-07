#pragma once

#include "gem_types.h"
#include "frame_processing.h"
#include <array>
#include <opencv2/opencv.hpp>
#include <string>

// ============================================================================
// Reference Image Loading - Load color reference images for gem matching
// ============================================================================

namespace MaskLoader {
    constexpr int MASK_COLS = 5;
    constexpr int MASK_ROWS = 4;
    constexpr int FRAMES_PER_GEM = 21;  // 1 static + 20 animated

// Hypercube has different dimensions (game assets)
    constexpr int HYPERCUBE_COLS = 10;      // Grid columns in source sprite sheet
    constexpr int HYPERCUBE_ROWS = 6;       // Grid rows in source sprite sheet
    constexpr int HYPERCUBE_FRAMES = 60;    // Total frames (10x6)

    // Output frame dimensions (resolution dependent - equals cell size)
    // For 1200p: 128x128, for 768p: 82x82, for 600p: 64x64
    inline int FRAME_WIDTH = 128;
    inline int FRAME_HEIGHT = 128;
    inline int HYPERCUBE_FRAME_WIDTH = 128;
    inline int HYPERCUBE_FRAME_HEIGHT = 128;

// Number of gem types (excluding EMPTY, HYPERCUBE and UNKNOWN)
    constexpr int NUM_GEM_TYPES = 7; // RED, ORANGE, YELLOW, GREEN, BLUE, PURPLE, WHITE

// Storage for all reference images: [gem_type][frame_index] -> color image
// Indexed by (GemType - 1) since EMPTY=0 is not used
    using MaskStorage =
    std::array<std::array<cv::Mat, FRAMES_PER_GEM>, NUM_GEM_TYPES>;

// Storage for Lab color space reference images (pre-converted for performance)
    using LabMaskStorage =
    std::array<std::array<cv::Mat, FRAMES_PER_GEM>, NUM_GEM_TYPES>;

// Storage for binary alpha masks: [gem_type][frame_index] -> grayscale mask
    using AlphaMaskStorage =
    std::array<std::array<cv::Mat, FRAMES_PER_GEM>, NUM_GEM_TYPES>;

// Storage for ring masks (scaled-up alpha minus original): [gem_type][frame_index]
    using RingMaskStorage =
    std::array<std::array<cv::Mat, FRAMES_PER_GEM>, NUM_GEM_TYPES>;

// Storage for hypercube (separate due to different frame count)
    using HypercubeMaskStorage = std::array<cv::Mat, HYPERCUBE_FRAMES>;
    using HypercubeAlphaMaskStorage = std::array<cv::Mat, HYPERCUBE_FRAMES>;
    using HypercubeRingMaskStorage = std::array<cv::Mat, HYPERCUBE_FRAMES>;
    using HypercubeLabMaskStorage = std::array<cv::Mat, HYPERCUBE_FRAMES>;

// Crop bounds for non-black regions in reference images (optimization to skip padding)
    using CropBounds = FrameProcessing::CropBounds;
    using CropBoundsStorage =
    std::array<std::array<CropBounds, FRAMES_PER_GEM>, NUM_GEM_TYPES>;
    using HypercubeCropBoundsStorage = std::array<CropBounds, HYPERCUBE_FRAMES>;

/**
 * Load all gem reference images from the assets folder.
 * Images are stored as BGR color images.
 *
 * @param assetsPath Path to the assets folder (e.g., "../assets")
 * @param resolution Game resolution (600, 768, or 1200)
 * @return True if all reference images loaded successfully
 */
    bool LoadAllMasks(const std::string&assetsPath, int resolution);

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
 * Get a specific ring mask for a gem type and frame.
 * Ring masks are the scaled-up alpha mask minus the original alpha mask.
 *
 * @param type The gem type (RED, ORANGE, YELLOW, GREEN, BLUE, PURPLE, WHITE, HYPERCUBE)
 * @param frameIndex Frame index 0-19 (or 0-29 for HYPERCUBE)
 * @return The ring mask (grayscale, 0=background, 255=ring), or empty Mat if not found
 */
     const cv::Mat& GetRingMask(GemType type, int frameIndex);

/**
 * Get a specific Lab color space reference image for a gem type and frame.
 * Lab images are pre-converted from BGR for performance optimization.
 *
 * @param type The gem type (RED, ORANGE, YELLOW, GREEN, BLUE, PURPLE, WHITE, HYPERCUBE)
 * @param frameIndex Frame index 0-20 (or 0-59 for HYPERCUBE)
 * @return The Lab color space image, or empty Mat if not found
 */
    const cv::Mat& GetLabMask(GemType type, int frameIndex);

/**
 * Get the number of frames for a given gem type.
 */
    int GetFrameCount(GemType type);

/**
 * Get the crop bounds for a specific mask (non-black region).
 * Used to skip padding pixels during color similarity comparison.
 *
 * @param type The gem type
 * @param frameIndex Frame index
 * @return CropBounds containing minRow, maxRow, minCol, maxCol of non-black region
 */
    const CropBounds& GetCropBounds(GemType type, int frameIndex);

/**
 * Check if static color masks have been loaded.
 */
    bool AreStaticColorsLoaded();
} // namespace MaskLoader