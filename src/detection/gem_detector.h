#pragma once

#include "../config.h"
#include "gem_types.h"
#include <opencv2/opencv.hpp>
#include <vector>
#include <string>

// ============================================================================
// Gem Detection - Grid-based gem detection
// ============================================================================

/**
 * Detect all gems in the game board grid.
 * Returns a reference to a static grid that is reused across calls.
 *
 * @param gameBoard The game board image (resolution-dependent size)
 * @param needDebugMap Whether debug visualization is needed (to avoid atomic loads)
 * @return Reference to internal 2D vector representing the 8x8 grid of detected
 * gems
 */
std::vector<std::vector<Gem>>&
DetectGems(const cv::Mat&gameBoard, bool needDebugMap = false);

/**
 * Print the gem grid to console for debugging
 */
void PrintGemGrid(const std::vector<std::vector<Gem>>&grid);

// ============================================================================
// Frame Recording - Capture raw gem images for asset creation
// ============================================================================

/**
 * Start recording frames from a specific grid position.
 * Frames are saved as PNG files in the output directory.
 *
 * @param col Grid column (0-7)
 * @param row Grid row (0-7)
 * @param maxFrames Maximum number of frames to record
 * @param outputDir Directory to save frames (will be created if needed)
 */
void StartRecording(int col, int row, int maxFrames,
                    const std::string&outputDir = "recorded_frames");

/**
 * Stop recording frames.
 */
void StopRecording();

/**
 * Check if currently recording.
 */
bool IsRecording();

/**
 * Get the number of frames recorded so far.
 */
int GetRecordedFrameCount();

/**
 * Get the total number of frames checked (including duplicates).
 */
int GetTotalFramesChecked();

/**
 * Update recording logic with a new frame.
 * Should be called once per capture cycle when recording is enabled.
 * 
 * @param gameBoard The full game board image
 */
void UpdateRecording(const cv::Mat&gameBoard);