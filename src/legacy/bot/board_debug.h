#pragma once

#include "board_analysis.h"
#include <string>

// ============================================================================
// Board Debug Utilities - Console visualization with ANSI colors
// ============================================================================

namespace BoardDebug {

/**
 * Print the current board state to console with colored gems.
 * Uses ANSI escape codes for terminal colors.
 *
 * @param board The board state to visualize
 * @param title Optional title to display above the board
 */
void PrintBoard(const BoardState &board, const std::string &title = "");

/**
 * Print a single swap simulation: before, after swap, after clearing, after
 * cascades.
 *
 * @param board The initial board state
 * @param swap The swap to simulate
 */
void PrintSwapSimulation(const BoardState &board, const MatchableSwap &swap);

/**
 * Print all scored swaps from analysis, sorted by score.
 * Shows top N swaps with their scores and basic info.
 *
 * @param analysis The board analysis result
 * @param topN How many top swaps to show (0 = all)
 */
void PrintScoredSwaps(const BoardAnalysis &analysis, int topN = 5);

/**
 * Print detailed simulation of the best swap from analysis.
 * Shows step-by-step board evolution.
 *
 * @param analysis The board analysis result
 */
void PrintBestSwap(const BoardAnalysis &analysis);

/**
 * Print full analysis summary: board state + all swaps + best move.
 *
 * @param analysis The board analysis result
 */
void PrintFullAnalysis(const BoardAnalysis &analysis);

} // namespace BoardDebug
