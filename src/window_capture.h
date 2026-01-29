#pragma once

#define NOMINMAX
#include <Windows.h>
#include <opencv2/opencv.hpp>
#include <string>

// ============================================================================
// Window Capture Functions
// ============================================================================

/**
 * Find a game window by title (exact or partial match)
 * @param windowTitle The window title to search for
 * @return Window handle (HWND) or NULL if not found
 */
HWND FindGameWindow(const std::string &windowTitle);

/**
 * Capture the contents of a window
 * @param hwnd Window handle to capture
 * @return OpenCV Mat containing the window contents (BGR format)
 */
cv::Mat CaptureWindow(HWND hwnd);

/**
 * Capture the contents of a window into a pre-allocated buffer
 * @param hwnd Window handle to capture
 * @param outputBuffer Pre-allocated Mat to write into (must be correct size)
 * @return true if successful, false otherwise
 */
bool CaptureWindowInto(HWND hwnd, cv::Mat &outputBuffer);

/**
 * Crop the game board from the full window capture
 * @param fullWindow The full window capture
 * @return Cropped game board region
 */
cv::Mat CropGameBoard(const cv::Mat &fullWindow);
