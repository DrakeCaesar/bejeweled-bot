#include "capture/window_capture.h"
#include "config.h"
#include "core/application.h"
#include "hook/memory_hook.h"
#include <cstring>
#include <iostream>
#include <mmsystem.h>
#include <opencv2/opencv.hpp>

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
// Diagnostic mode: find the game window, attach the memory hook, dump the
// board state, and exit. No capture / classifiers / window setup involved.
// ============================================================================

static int RunDiagnostic() {
  HWND hwnd = FindGameWindow(Config::WINDOW_TITLE);
  if (!hwnd) {
    std::cerr << "Error: could not find game window \"" << Config::WINDOW_TITLE
              << "\".\n";
    return -1;
  }
  if (!MemoryHook::Attach(hwnd)) {
    std::cerr << "Error: memory hook attach failed.\n";
    return -1;
  }
  std::cout << "Attached. Module base 0x" << std::hex
            << MemoryHook::ModuleBase() << std::dec << "\n";
  MemoryHook::DumpBoardState();
  MemoryHook::Detach();
  return 0;
}

// ============================================================================
// Main Entry Point
// ============================================================================

int main(int argc, char **argv) {
  if (argc > 1 && std::strcmp(argv[1], "--dump") == 0) {
    return RunDiagnostic();
  }
  if (argc > 1 && std::strcmp(argv[1], "--find") == 0) {
    // Known board state (7 colors: 0=RED 1=ORANGE 2=YELLOW 3=GREEN 4=BLUE
    // 5=PURPLE 6=WHITE), row-major. Update this to match the live board.
    static const int board[64] = {
        4, 0, 1, 2, 6, 0, 3, 4, //
        2, 2, 0, 4, 6, 2, 4, 0, //
        5, 3, 5, 1, 1, 2, 3, 3, //
        3, 2, 5, 2, 3, 1, 4, 4, //
        6, 0, 6, 4, 2, 6, 5, 0, //
        1, 5, 5, 1, 6, 5, 0, 5, //
        4, 3, 6, 3, 4, 5, 0, 5, //
        0, 0, 2, 2, 5, 1, 6, 3, //
    };
    HWND hwnd = FindGameWindow(Config::WINDOW_TITLE);
    if (!hwnd) {
      std::cerr << "Error: could not find game window.\n";
      return -1;
    }
    if (!MemoryHook::Attach(hwnd)) {
      std::cerr << "Error: attach failed.\n";
      return -1;
    }
    MemoryHook::FindGemGridByPattern(board);
    MemoryHook::Detach();
    return 0;
  }
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
  if (argc > 1 && std::strcmp(argv[1], "--watch") == 0) {
    HWND hwnd = FindGameWindow(Config::WINDOW_TITLE);
    if (!hwnd) {
      std::cerr << "Error: could not find game window.\n";
      return -1;
    }
    if (!MemoryHook::Attach(hwnd)) {
      std::cerr << "Error: attach failed.\n";
      return -1;
    }
    MemoryHook::WatchBoardForMove();
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