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
 *
 * @param image The image to draw on (will be modified)
 * @param grid The detected gem grid
 */
void DrawDebugOverlay(cv::Mat &image,
                      const std::vector<std::vector<Gem>> &grid);
