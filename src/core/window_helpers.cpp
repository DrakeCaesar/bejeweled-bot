#include "window_helpers.h"

#include <iostream>

// ============================================================================
// Window Finding
// ============================================================================

HWND FindGameWindow(const std::string &windowTitle) {
  HWND hwnd = FindWindowA(NULL, windowTitle.c_str());

  if (hwnd == NULL) {
    std::cout << "Searching for window containing: " << windowTitle
              << std::endl;
    // If exact match fails, search for partial match
    hwnd = FindWindowA(NULL, NULL);
    while (hwnd != NULL) {
      char title[256];
      GetWindowTextA(hwnd, title, sizeof(title));
      std::string titleStr(title);

      if (titleStr.find(windowTitle) != std::string::npos &&
          IsWindowVisible(hwnd)) {
        std::cout << "Found window: " << titleStr << std::endl;
        return hwnd;
      }
      hwnd = GetWindow(hwnd, GW_HWNDNEXT);
    }
  }

  return hwnd;
}

// ============================================================================
// Window Helpers
// ============================================================================

void KeepGameActive(HWND gameWindow) {
  // Only activate if the window is not already active
  HWND foregroundWindow = GetForegroundWindow();
  if (foregroundWindow != gameWindow) {
    SendMessage(gameWindow, WM_ACTIVATEAPP, TRUE, 0);
    SendMessage(gameWindow, WM_ACTIVATE, WA_ACTIVE, 0);
  }
  // Don't send WM_MOUSEMOVE - this causes the drag issue!
}

void SetWindowAlwaysOnTop(HWND hwnd, bool onTop) {
  SetWindowPos(hwnd, onTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}
