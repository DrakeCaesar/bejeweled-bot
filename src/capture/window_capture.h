#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <opencv2/opencv.hpp>
#include <string>

// ============================================================================
// Desktop Duplication Context
// ============================================================================

struct DuplicationContext {
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
 */
HWND FindGameWindow(const std::string &windowTitle);

/**
 * Capture the contents of a window
 */
cv::Mat CaptureWindow(HWND hwnd);

/**
 * Capture the contents of a window into a pre-allocated buffer
 */
bool CaptureWindowInto(HWND hwnd, cv::Mat &outputBuffer);

/**
 * Capture a specific region of a window directly into a pre-allocated buffer
 */
bool CaptureWindowRegionInto(HWND hwnd, int x, int y, int width, int height,
                             cv::Mat &outputBuffer);

/**
 * Crop the game board from the full window capture
 */
cv::Mat CropGameBoard(const cv::Mat &fullWindow);

// ============================================================================
// Desktop Duplication Functions
// ============================================================================

/**
 * Initialize Desktop Duplication for a specific monitor
 */
DuplicationContext *InitializeDesktopDuplication(int monitorIndex = 0);

/**
 * Capture next frame using Desktop Duplication
 */
bool CaptureNextDuplicationFrame(DuplicationContext *ctx, cv::Mat &outputBuffer,
                                 int timeoutMs = 16);

/**
 * Capture a region from the duplicated frame
 */
bool CaptureRegionFromDuplication(DuplicationContext *ctx, int x, int y,
                                  int width, int height, cv::Mat &outputBuffer,
                                  int timeoutMs = 16);

/**
 * Cleanup Desktop Duplication resources
 */
void CleanupDesktopDuplication(DuplicationContext *ctx);

// ============================================================================
// Window Helpers
// ============================================================================

/**
 * Keep the game window active by sending activation messages
 */
void KeepGameActive(HWND gameWindow);

/**
 * Set window to always on top
 */
void SetWindowAlwaysOnTop(HWND hwnd, bool onTop);
