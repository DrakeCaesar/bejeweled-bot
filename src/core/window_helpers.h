#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <string>

// ============================================================================
// Window helpers used by the active (memory-hook) path.
// ============================================================================

/// Find a game window by title (exact or partial match).
HWND FindGameWindow(const std::string &windowTitle);

/// Keep the game window "active" (not paused) even when it loses focus, by
/// sending activation messages.
void KeepGameActive(HWND gameWindow);

/// Set the window to always-on-top (or not).
void SetWindowAlwaysOnTop(HWND hwnd, bool onTop);
