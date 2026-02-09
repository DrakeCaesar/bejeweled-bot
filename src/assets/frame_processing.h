#pragma once

#include <array>
#include <opencv2/opencv.hpp>

namespace FrameProcessing {

// Grid dimensions (constant across all resolutions)
constexpr int MASK_COLS = 5;
constexpr int MASK_ROWS = 4;
constexpr int FRAMES_PER_GEM = 21; // 1 static + 20 animated

// Hypercube grid dimensions
constexpr int HYPERCUBE_COLS = 10;
constexpr int HYPERCUBE_ROWS = 6;
constexpr int HYPERCUBE_FRAMES = 60;

constexpr int NUM_GEM_TYPES = 7;

// Storage type definitions
using MaskStorage =
    std::array<std::array<cv::Mat, FRAMES_PER_GEM>, NUM_GEM_TYPES>;
using LabMaskStorage =
    std::array<std::array<cv::Mat, FRAMES_PER_GEM>, NUM_GEM_TYPES>;
using AlphaMaskStorage =
    std::array<std::array<cv::Mat, FRAMES_PER_GEM>, NUM_GEM_TYPES>;
using RingMaskStorage =
    std::array<std::array<cv::Mat, FRAMES_PER_GEM>, NUM_GEM_TYPES>;
using HypercubeMaskStorage = std::array<cv::Mat, HYPERCUBE_FRAMES>;
using HypercubeLabMaskStorage = std::array<cv::Mat, HYPERCUBE_FRAMES>;
using HypercubeAlphaMaskStorage = std::array<cv::Mat, HYPERCUBE_FRAMES>;
using HypercubeRingMaskStorage = std::array<cv::Mat, HYPERCUBE_FRAMES>;

// Resolution-dependent kernel sizes (set by application)
// Can be adjusted per resolution if needed
inline cv::Size kStandardGemKernelSize = cv::Size(3, 3);
inline cv::Size kStaticColorKernelSize = cv::Size(7, 7);
inline cv::Size kHypercubeKernelSize = cv::Size(0, 0);

// Common frame processing parameters (only varying fields are exposed)
struct FrameProcessingParams {
  cv::Size kernelSize;       // e.g., Size(3,3), Size(7,7), Size(0,0)
  bool applyHalfTexelOffset; // whether to apply half-texel offset
};

// Get parameter sets with the appropriate kernel size
// Other values are constant: threshold=254, kernelShape=ELLIPSE,
// colorInterp=LINEAR, alphaInterp=NEAREST
inline FrameProcessingParams GetStandardGemParams() {
  return {kStandardGemKernelSize, true}; // applyHalfTexelOffset controlled via
                                         // TexelOffsetConfig::OFFSET_X/Y
}

inline FrameProcessingParams GetStaticColorParams() {
  return {kStaticColorKernelSize, true};
}

inline FrameProcessingParams GetHypercubeParams() {
  return {kHypercubeKernelSize, false};
}

namespace TexelOffsetConfig {
// Half texel offset values (resolution-dependent, set by application)
// For 600p/1200p: 0.5, 0.5 | For 768p: -0.5, -0.5
inline double OFFSET_X = 0.5;
inline double OFFSET_Y = 0.5;
constexpr int COLOR_INTERPOLATION = 1;
constexpr int ALPHA_INTERPOLATION = 0;

int GetInterpolationFlag(int method);
} // namespace TexelOffsetConfig

// Apply half-texel offset to an image
cv::Mat ApplyHalfTexelOffset(const cv::Mat &image, int interpolation);

// Build a ring mask from an alpha mask (scaled-up minus original)
cv::Mat BuildRingMask(const cv::Mat &alphaMask);

// Process a single frame with the given parameters
void ProcessSingleFrame(const cv::Mat &colorRoi, const cv::Mat &maskRoi,
                        const FrameProcessingParams &params, cv::Mat &outColor,
                        cv::Mat &outMask, cv::Mat &outRingMask,
                        cv::Mat &outLab);

} // namespace FrameProcessing
