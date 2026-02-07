#pragma once

#include <opencv2/opencv.hpp>
#include <array>

namespace FrameProcessing {

// Constants for mask storage
constexpr int MASK_COLS = 5;
constexpr int MASK_ROWS = 4;
constexpr int FRAMES_PER_GEM = 21;  // 1 static + 20 animated
constexpr int FRAME_WIDTH = 128;
constexpr int FRAME_HEIGHT = 128;
constexpr int SPRITE_WIDTH = 640;
constexpr int SPRITE_HEIGHT = 512;

// Hypercube constants
constexpr int HYPERCUBE_COLS = 10;
constexpr int HYPERCUBE_ROWS = 6;
constexpr int HYPERCUBE_FRAMES = 60;
constexpr int HYPERCUBE_SOURCE_WIDTH = 1140;
constexpr int HYPERCUBE_SOURCE_HEIGHT = 720;
constexpr int HYPERCUBE_SOURCE_CELL_WIDTH = HYPERCUBE_SOURCE_WIDTH / HYPERCUBE_COLS;
constexpr int HYPERCUBE_SOURCE_CELL_HEIGHT = HYPERCUBE_SOURCE_HEIGHT / HYPERCUBE_ROWS;
constexpr int HYPERCUBE_GLOW_WIDTH = 1600;
constexpr int HYPERCUBE_GLOW_HEIGHT = 960;
constexpr int HYPERCUBE_GLOW_CELL_WIDTH = HYPERCUBE_GLOW_WIDTH / HYPERCUBE_COLS;
constexpr int HYPERCUBE_GLOW_CELL_HEIGHT = HYPERCUBE_GLOW_HEIGHT / HYPERCUBE_ROWS;
constexpr int HYPERCUBE_FRAME_WIDTH = 128;
constexpr int HYPERCUBE_FRAME_HEIGHT = 128;
constexpr int NUM_GEM_TYPES = 7;

// Storage type definitions
using MaskStorage = std::array<std::array<cv::Mat, FRAMES_PER_GEM>, NUM_GEM_TYPES>;
using LabMaskStorage = std::array<std::array<cv::Mat, FRAMES_PER_GEM>, NUM_GEM_TYPES>;
using AlphaMaskStorage = std::array<std::array<cv::Mat, FRAMES_PER_GEM>, NUM_GEM_TYPES>;
using RingMaskStorage = std::array<std::array<cv::Mat, FRAMES_PER_GEM>, NUM_GEM_TYPES>;
using HypercubeMaskStorage = std::array<cv::Mat, HYPERCUBE_FRAMES>;
using HypercubeLabMaskStorage = std::array<cv::Mat, HYPERCUBE_FRAMES>;
using HypercubeAlphaMaskStorage = std::array<cv::Mat, HYPERCUBE_FRAMES>;
using HypercubeRingMaskStorage = std::array<cv::Mat, HYPERCUBE_FRAMES>;

// Crop bounds for non-black region optimization
struct CropBounds {
    int minRow, maxRow, minCol, maxCol;
};

using CropBoundsStorage = std::array<std::array<CropBounds, FRAMES_PER_GEM>, NUM_GEM_TYPES>;
using HypercubeCropBoundsStorage = std::array<CropBounds, HYPERCUBE_FRAMES>;

// Frame processing parameters
struct FrameProcessingParams {
    int thresholdValue;           // 127 or 254
    int kernelShape;              // cv::MorphShapes enum value
    cv::Size kernelSize;          // e.g., Size(3,3), Size(7,7)
    bool applyHalfTexelOffset;    // whether to apply offset
    int colorInterpolation;       // interpolation for color offset
    int alphaInterpolation;       // interpolation for alpha offset
};

// Predefined parameter sets
extern const FrameProcessingParams kStandardGemParams;
extern const FrameProcessingParams kStaticColorParams;
extern const FrameProcessingParams kHypercubeParams;

namespace TexelOffsetConfig {
    constexpr double OFFSET_X = 0.5;
    constexpr double OFFSET_Y = 0.5;
    constexpr int COLOR_INTERPOLATION = 1;
    constexpr int ALPHA_INTERPOLATION = 0;

    int GetInterpolationFlag(int method);
}

// Apply half-texel offset to an image
cv::Mat ApplyHalfTexelOffset(const cv::Mat& image, int interpolation);

// Build a ring mask from an alpha mask (scaled-up minus original)
cv::Mat BuildRingMask(const cv::Mat& alphaMask);

// Calculate crop bounds for non-black region in LAB image
CropBounds CalculateCropBounds(const cv::Mat& labImage);

// Process a single frame with the given parameters
void ProcessSingleFrame(
    const cv::Mat& colorRoi,
    const cv::Mat& maskRoi,
    const FrameProcessingParams& params,
    cv::Mat& outColor,
    cv::Mat& outMask,
    cv::Mat& outRingMask,
    cv::Mat& outLab,
    CropBounds& outCropBounds
);

} // namespace FrameProcessing
