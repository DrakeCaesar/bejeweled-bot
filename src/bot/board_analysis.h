#pragma once

#include "detection/gem_types.h"
#include <array>
#include <opencv2/core/types.hpp>
#include <vector>

// ============================================================================
// Board Representation for Analysis and Simulation
// ============================================================================

/// A swap between two adjacent cells, stored as (col, row) points
struct MatchableSwap {
  cv::Point a; // (col, row)
  cv::Point b; // (col, row)
};

/// Lightweight cell for board simulation (no OpenCV matrices)
struct BoardCell {
  GemType type = GemType::EMPTY;
  GemState state = GemState::NORMAL;
  // True when this cell is part of a match that has ALREADY formed and is
  // being cleared by the game (a lingering / pending match). Such gems are no
  // longer active: they cannot be swapped and they cannot be used to extend or
  // form a new match — the match they belong to is already committed.
  // Derived from `type`, so it is not part of the board's identity.
  bool pending = false;
};

/// Copyable 8x8 board state for simulation and DFS exploration.
/// Contains only the data needed for move evaluation — no image data.
struct BoardState {
  static constexpr int SIZE = 8;

  std::array<std::array<BoardCell, SIZE>, SIZE> cells{};

  /// Initialize from the full detection grid
  void initFromGrid(const std::vector<std::vector<Gem>> &grid);

  /// Get cell type, with optional swap applied virtually
  GemType getType(int row, int col, const MatchableSwap *swap = nullptr) const;

  /// Like getType, but returns UNKNOWN for cells that are `pending` (already
  /// part of a committed match). Used for match detection so a locked match
  /// breaks a run instead of being counted as part of a new one.
  GemType activeType(int row, int col,
                     const MatchableSwap *swap = nullptr) const;

  /// Flag every cell that participates in an existing 3+ match as `pending`
  /// (locked / clearing). Derived state — call after initFromGrid().
  void markPendingMatches();

  /// Clear all `pending` flags (used before simulating a future resolution).
  void clearPendingMatches();

  /// Check if a specific position would form a match (3+) after a swap
  bool wouldFormMatchAt(int row, int col, const MatchableSwap &swap) const;

  /// Check if a swap creates any match
  bool swapCreatesMatch(const MatchableSwap &swap) const;

  /// Check if the board already contains a 3+ match (i.e. it is mid-cascade /
  /// not yet settled). A playable board never has an existing match.
  bool hasExistingMatch() const;

  /// Find all valid swaps that create at least one match
  /// @param excludeHypercubes If true, exclude swaps involving hypercube gems
  std::vector<MatchableSwap>
  findValidSwaps(bool excludeHypercubes = false) const;

  /// Apply a swap in-place (mutates the board)
  void applySwap(const MatchableSwap &swap);

  /// Find and clear all 3+ matches, including special detonations.
  /// If a swap is provided, special creation prefers swap positions.
  /// Returns number of gems cleared and sets cleared cells to EMPTY.
  int clearMatches(const MatchableSwap *swap = nullptr,
                   bool allowSpecialCreation = true);

  /// Apply gravity: drop gems down to fill empty spaces.
  /// Returns true if any gems moved.
  bool applyGravity();

  /// Full resolution: clear matches + gravity, repeat until stable.
  /// Returns total gems cleared across all cascades.
  /// Note: After resolution, empty cells at top represent unknown incoming
  /// gems.
  int resolveBoard();

  /// Check if all cells have a known gem type (board is fully identified)
  bool isFullyIdentified() const;

  /// Compare two boards (type + state)
  bool equals(const BoardState &other) const;

  /// Simulate a move to completion (swap + full cascade) and write the final
  /// settled board into `finalBoard`. New falling gems are NOT modeled (known
  /// gap). Returns the total number of gems cleared.
  int simulateMove(const MatchableSwap &swap, BoardState &finalBoard) const;

  /// Copy any gem from `src` into cells of *this that are EMPTY (same grid
  /// position). Used to seed the refill with the gems the game actually
  /// spawned, so the simulation continues instead of stopping at the refill.
  void fillEmptiesFrom(const BoardState &src);

  /// Like simulateMove, but after each clear+gravity the emptied top cells are
  /// seeded from `refill` (the live board) so the cascade continues through the
  /// game's real refill instead of stopping. Returns total gems cleared.
  int simulateMoveSeeded(const MatchableSwap &swap, const BoardState &refill,
                         BoardState &finalBoard) const;

  /// Compact 8-line rendering for diffing/prediction: r,o,y,g,b,p,w = colors,
  /// H = hypercube, '.' = empty, '?' = unknown.
  std::string toString() const;
};

// ============================================================================
// Move Evaluation
// ============================================================================

/// A swap with its evaluated score
struct ScoredSwap {
  MatchableSwap swap;
  int immediateClears = 0;     // Gems cleared by the initial match
  int cascadeClears = 0;       // Total gems cleared after all cascades
  int matchLength = 0;         // Length of the longest match created
  int totalScore = 0;          // Combined weighted score
  bool createsSpecial = false; // Whether it creates a 4+ match (flame/star)
  GemState specialType = GemState::NORMAL; // Type of special gem created
};

// ============================================================================
// Board Analysis - Computed once per frame in the process thread
// ============================================================================

struct BoardAnalysis {
  BoardState board;                    // Snapshot of current board state
  std::vector<ScoredSwap> scoredSwaps; // All valid swaps with scores
  int bestSwapIndex = -1;              // Index of highest-scored swap
  bool boardValid = false;             // Board is fully identified
  bool settling = false; // Board currently has a pending match (mid-cascade)
  int stableFrameCount = 0; // Consecutive frames with same valid board

  /// Run full analysis: snapshot board, find swaps, score them
  void analyze(const std::vector<std::vector<Gem>> &grid);

private:
  /// Find and score every valid swap for the current `board` snapshot.
  /// Respects the `pending` (locked) flags on the board.
  void computeScoredSwaps();
};
