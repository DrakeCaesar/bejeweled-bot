#pragma once

#include "../config.h"
#include "gem_types.h"
#include <opencv2/opencv.hpp>
#include <vector>

// ============================================================================
// Gem Detection - Grid-based gem detection
// ============================================================================

/**
 * Detect all gems in the game board grid using color classification.
 * Returns a reference to a static grid that is reused across calls.
 *
 * @param gameBoard The game board image (1024x1024)
 * @param mode Classification mode (COLOR or SHAPE)
 * @return Reference to internal 2D vector representing the 8x8 grid of detected
 * gems
 */
std::vector<std::vector<Gem>> &
DetectGems(const cv::Mat &gameBoard,
           Config::ClassificationMode mode = Config::ClassificationMode::COLOR);

/**
 * Print the gem grid to console for debugging
 */
void PrintGemGrid(const std::vector<std::vector<Gem>> &grid);
