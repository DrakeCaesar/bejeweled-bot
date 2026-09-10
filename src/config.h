#pragma once

#include <string>

// ============================================================================
// CONFIGURATION CONSTANTS (active memory-hook path)
// ============================================================================

namespace Config {
// Window title of the Bejeweled 3 game (or part of it)
const std::string WINDOW_TITLE = "Bejeweled 3";

// Grid configuration (constant across all resolutions)
const int GRID_SIZE = 8; // 8x8 grid

// Display settings
const std::string DISPLAY_WINDOW_NAME = "Bejeweled 3 - Game Board";

// Auto-play settings
const bool BOT_STOP_AFTER_SUPERNOVA_FOUND =
    true; // Stop bot when supernova move is detected
const bool BOT_PREFER_NON_HYPERCUBE_MOVES =
    true; // Only match hypercubes if no other moves
// Consecutive identical memory reads required before acting. With the memory
// hook this is only a tiny debounce against a mid-transition read; the real
// "board is settled" signal is a board with no pending match (analysis_.settling).
const int BOT_STABLE_FRAMES_REQUIRED = 2;

// Whether to wait for a cascade to fully settle before the next move.
// true  = normal play (wait for the board to resolve).
// false = chain moves the instant the previous swap registers in memory, so we
//         can act mid-cascade (interception / Gemwipe experiments).
const bool BOT_WAIT_FOR_SETTLE = false;

// Safety net only: if a dispatched move is never reflected in memory (e.g. the
// click was rejected), give up waiting after this long and try again. This is
// NOT pacing — normal moves proceed as soon as memory changes.
const int BOT_MAX_WAIT_FOR_CHANGE_MS = 100;

// Resolution-dependent values that will be set at runtime
inline int BOARD_X = 523;      // Will be set by application based on resolution
inline int BOARD_Y = 75;       // Will be set by application based on resolution
inline int BOARD_WIDTH = 1024; // Will be set by application based on resolution
inline int BOARD_HEIGHT =
    1024;                   // Will be set by application based on resolution
inline int CELL_SIZE = 128; // Will be set by application based on resolution
inline int BANNER_HEIGHT =
    200; // Will be set by application based on resolution

// Resolution-specific configuration
struct ResolutionConfig {
  int boardX;       // X offset from left edge of window
  int boardY;       // Y offset from top edge of window
  int cellSize;     // Grid cell size in pixels
  int bannerHeight; // Height of info banner
};

// Resolution configurations for supported resolutions
inline ResolutionConfig GetResolutionConfig(int height) {
  switch (height) {
  case 600:
    return {261, 37, 64, 150};
  case 768:
    return {334, 47, 82, 175};
  case 1200:
  default:
    return {523, 75, 128, 200};
  }
}

} // namespace Config
