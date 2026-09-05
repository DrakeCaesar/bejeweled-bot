#include "config.h"
#include "core/application.h"
#include "core/window_helpers.h"
#include "hook/memory_hook.h"
#include <cstring>
#include <iostream>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

// ============================================================================
// RAII wrapper for high-resolution timer
// ============================================================================

struct TimerResolution {
  explicit TimerResolution(UINT periodMs) : period(periodMs) {
    timeBeginPeriod(period);
  }

  ~TimerResolution() { timeEndPeriod(period); }
  UINT period;
};

// ============================================================================
// Main Entry Point
// ============================================================================

int main(int argc, char **argv) {
  // Standalone board dump (no window / auto-play).
  if (argc > 1 && std::strcmp(argv[1], "--dumpgrid") == 0) {
    HWND hwnd = FindGameWindow(Config::WINDOW_TITLE);
    if (!hwnd) {
      std::cerr << "Error: could not find game window.\n";
      return -1;
    }
    if (!MemoryHook::Attach(hwnd)) {
      std::cerr << "Error: attach failed.\n";
      return -1;
    }
    MemoryHook::DumpGrid();
    MemoryHook::Detach();
    return 0;
  }

  // Enable high-resolution timing
  TimerResolution timerResolution(1);

  // Create and run application
  Application app;

  if (!app.Initialize()) {
    return -1;
  }

  app.Run();

  return 0;
}
