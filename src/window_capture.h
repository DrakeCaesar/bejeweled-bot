#pragma once

#define NOMINMAX
#include <Windows.h>
#include <opencv2/opencv.hpp>
#include <string>
#include <d3d11.h>
#include <dxgi1_2.h>

struct DuplicationContext
{
  ID3D11Device *device = nullptr;
  ID3D11DeviceContext *context = nullptr;
  IDXGIOutputDuplication *duplication = nullptr;
  ID3D11Texture2D *stagingTexture = nullptr;
  int width = 0;
  int height = 0;
};

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
 * Capture a specific region of a window directly into a pre-allocated buffer
 * @param hwnd Window handle to capture
 * @param x X offset from window client area
 * @param y Y offset from window client area
 * @param width Width of region to capture
 * @param height Height of region to capture
 * @param outputBuffer Pre-allocated Mat to write into (must match width x height x CV_8UC3)
 * @return true if successful, false otherwise
 */
bool CaptureWindowRegionInto(HWND hwnd, int x, int y, int width, int height, cv::Mat &outputBuffer);

/**
 * Initialize Desktop Duplication for a specific monitor
 * @param monitorIndex Monitor index (0 = primary)
 * @return Duplication context or nullptr on failure
 */
DuplicationContext *InitializeDesktopDuplication(int monitorIndex = 0);

/**
 * Capture next frame using Desktop Duplication (blocks until new frame)
 * @param ctx Duplication context
 * @param outputBuffer Pre-allocated buffer to write into
 * @param timeoutMs Timeout in milliseconds (0 = no timeout)
 * @return true if new frame captured, false on timeout/error
 */
bool CaptureNextDuplicationFrame(DuplicationContext *ctx, cv::Mat &outputBuffer, int timeoutMs = 16);

/**
 * Capture a region from the duplicated frame
 * @param ctx Duplication context
 * @param x X offset
 * @param y Y offset
 * @param width Region width
 * @param height Region height
 * @param outputBuffer Pre-allocated buffer
 * @param timeoutMs Timeout in milliseconds
 * @return true if successful
 */
bool CaptureRegionFromDuplication(DuplicationContext *ctx, int x, int y, int width, int height, cv::Mat &outputBuffer, int timeoutMs = 16);

/**
 * Cleanup Desktop Duplication resources
 * @param ctx Duplication context to release
 */
void CleanupDesktopDuplication(DuplicationContext *ctx);

/**
 * Crop the game board from the full window capture
 * @param fullWindow The full window capture
 * @return Cropped game board region
 */
cv::Mat CropGameBoard(const cv::Mat &fullWindow);
