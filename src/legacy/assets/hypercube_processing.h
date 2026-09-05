#pragma once

#include "frame_processing.h"
#include <opencv2/opencv.hpp>

namespace HypercubeProcessing {

// Apply glow overlay additively (simple game engine approach)
// This matches Bejeweled 3's rendering - strength fixed at 100%
void ApplyGlowOverlay(cv::Mat &colorFrame, const cv::Mat &maskFrame,
                      const cv::Mat &glowSheet, int row, int col,
                      int sourceCellWidth, int sourceCellHeight, int frameWidth,
                      int frameHeight, int glowCellWidth, int glowCellHeight);

// Process a hypercube frame with centered positioning and glow effects
void ProcessHypercubeFrame(const cv::Mat &colorSheet, const cv::Mat &maskSheet,
                           const cv::Mat &glowSheet, int row, int col,
                           int frameIndex,
                           const FrameProcessing::FrameProcessingParams &params,
                           int sourceCellWidth, int sourceCellHeight,
                           int frameWidth, int frameHeight, int glowCellWidth,
                           int glowCellHeight, cv::Mat &outColor,
                           cv::Mat &outMask, cv::Mat &outLab);

} // namespace HypercubeProcessing
