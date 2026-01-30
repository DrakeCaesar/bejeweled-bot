#pragma once

#include <windows.h>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

// ============================================================================
// Helper Functions
// ============================================================================

// Keep the game window active by sending activation messages
void KeepGameActive(HWND gameWindow);

// Set window to always on top
void SetWindowAlwaysOnTop(HWND hwnd, bool onTop);

// RAII wrapper for high-resolution timer
struct TimerResolution
{
  explicit TimerResolution(UINT periodMs);
  ~TimerResolution();

  UINT period;
};
