#pragma once

#include "bot/board_analysis.h"
#include <chrono>
#include <string>
#include <vector>
#include <windows.h>

// ============================================================================
// Application - Main application lifecycle (memory-hook mode)
//
// The board is read directly from game memory via the memory hook; there is no
// capture/detection/classification pipeline. Auto-play runs the move logic on
// the memory board and forwards clicks to the game window.
// ============================================================================

class Application {
public:
  Application() = default;

  ~Application();

  /**
   * Find the game window, attach the memory hook, and determine resolution
   * (for click coordinate translation).
   */
  bool Initialize();

  /**
   * Run the main loop: read the board, drive auto-play, render the live grid,
   * and handle keyboard input.
   */
  void Run();

  /**
   * Cleanup and shutdown.
   */
  void Shutdown();

private:
  bool FindAndSetupGameWindow();

  void RunHookMode();

  void HandleKeyboardInput(int key);

  // Auto-play
  void ExecuteBestSwap();
  void UpdateAutoPlay();

  // Cascade validation harness
  void PrintCascadeComparison();

  // State
  HWND gameWindow_ = nullptr;
  bool running_ = false;

  // Auto-play state
  bool autoPlayEnabled_ = false;
  std::chrono::high_resolution_clock::time_point lastClickTime_;
  int clicksInProgress_ = 0; // 0 = idle, 2 = waiting after a click sequence
  std::chrono::steady_clock::time_point settleUntil_{}; // don't act until then

  // Cascade validation harness state
  bool observingCascade_ = false;
  int predictedClears_ = 0;
  std::string predictedBoard_;          // simulation's final board (toString)
  std::vector<std::string> observedStates_; // distinct live boards since move

  // Current board analysis (produced by the hook loop, consumed by auto-play)
  BoardAnalysis analysis_;
};