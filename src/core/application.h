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

  /**
   * Self-serve RNG data collection: enable auto-play immediately and log the
   * MT19937 state/index + gem board to rng_log.txt, then quit after `ms`.
   */
  void SetRngAutoTrace(int ms) { rngAutoTraceMs_ = ms; }

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

  // Auto-play state (event-driven: the bot waits for the board in memory to
  // change rather than using fixed time delays).
  bool autoPlayEnabled_ = false;
  bool awaitingBoardChange_ = false; // true while a dispatched move is in flight
  std::chrono::steady_clock::time_point moveDispatchedAt_{}; // stuck-move guard
  int rngAutoTraceMs_ = 0; // >0 => self-serve RNG trace, auto-quit after this

  // Cascade validation harness state
  bool observingCascade_ = false;
  int predictedClears_ = 0;
  std::string predictedBoard_;          // simulation's final board (toString)
  std::vector<std::string> observedStates_; // distinct live boards since move
  std::vector<BoardState> observedBoards_;  // same states as BoardState

  // Seeded prediction (option B): the pre-move board + swap, so we can re-run
  // the simulation with the live refill injected once the cascade settles.
  bool havePreMove_ = false;
  BoardState preMoveBoard_;
  MatchableSwap lastSwap_;

  // Current board analysis (produced by the hook loop, consumed by auto-play)
  BoardAnalysis analysis_;
};