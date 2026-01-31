#pragma once

#include "../config.h"
#include "../detection/gem_types.h"
#include <opencv2/opencv.hpp>
#include <vector>

// ============================================================================
// Debug Overlay - Grid and gem information display
// ============================================================================

/**
 * Draw debug overlay on the image showing grid and detected gems.
 * Includes grid lines, gem ROIs, and classification info.
 * Used for COLOR classification mode.
 *
 * @param image The image to draw on (will be modified)
 * @param grid The detected gem grid
 */
void DrawDebugOverlay(cv::Mat &image,
                      const std::vector<std::vector<Gem>> &grid);

/**
 * Draw shape mode overlay showing processed binary masks for each gem.
 * Displays the white-on-black gem silhouettes used for shape matching.
 * Used for SHAPE classification mode.
 *
 * @param image The image to draw on (will be modified)
 * @param grid The detected gem grid with shapeMask populated
 */
void DrawShapeOverlay(cv::Mat &image,
                      const std::vector<std::vector<Gem>> &grid);
