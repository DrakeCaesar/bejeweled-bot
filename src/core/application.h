#pragma once

#include "bot/board_analysis.h"
#include <array>
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

  /**
   * Self-serve board/gem trace: enable auto-play immediately and log the full
   * per-slot gem state (pointer, raw dwords at +0x08..+0x24, type, special
   * state) to board_trace.bin, then quit after `ms`.
   */
  void SetBoardTrace(int ms) { boardTraceMs_ = ms; }

  /**
   * Board editor: paint gems onto the overlay, then flush them to the game.
   * Click a palette swatch to pick a colour/state, left-click a board cell to
   * stage it as pending, right-click to clear a staged cell. 'Enter' writes all
   * staged cells to the game, 'Backspace' clears them.
   */
  static void OnMouse(int event, int x, int y, int flags, void *userdata);
  void HandleEditorClick(int event, int x, int y);
  void FlushPendingBoard();
  void ClearPendingBoard();

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
  int boardTraceMs_ = 0;   // >0 => self-serve board/gem trace, auto-quit

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

  // Board editor state
  int brushColorIndex_ = 0; // index into the colour row (0..6)
  int brushStateIndex_ = 0; // index into the state row (0..4)
  std::array<uint8_t, 64> pendingMask_{}; // 1 = staged, not yet written
  std::array<GemType, 64> pendingType_{};
  std::array<GemState, 64> pendingState_{};
  int displayCols_ = 640;      // overlay width in pixels (set when created)
  int displayCellPx_ = 80;     // board cell size in pixels
  int paletteHeightPx_ = 92;   // top palette strip height

  // Current board analysis (produced by the hook loop, consumed by auto-play)
  BoardAnalysis analysis_;
};