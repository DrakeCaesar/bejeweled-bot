#pragma once

#include <opencv2/opencv.hpp>

// ============================================================================
// Banner Rendering - Info banner at top of display
// ============================================================================

/**
 * Draw information banner at the top of the image.
 *
 * @param image The image to draw on (will be modified)
 * @param fps Current frames per second
 * @param captureMs Capture time in milliseconds
 * @param detectMs Detection time in milliseconds
 * @param drawMs Draw time in milliseconds
 * @param displayMs Display time in milliseconds
 * @param fpsWidth Width for FPS padding
 * @param captureWidth Width for capture padding
 * @param detectWidth Width for detect padding
 * @param drawWidth Width for draw padding
 * @param displayWidth Width for display padding
 */
void DrawBanner(cv::Mat &image, float fps, double captureMs = 0,
                double detectMs = 0, double drawMs = 0, double displayMs = 0,
                int fpsWidth = 5, int captureWidth = 4, int detectWidth = 4,
                int drawWidth = 4, int displayWidth = 4, int stableFrames = 0);