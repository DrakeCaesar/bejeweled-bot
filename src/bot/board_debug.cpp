#include "board_debug.h"
#include "detection/gem_types.h"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

// ============================================================================
// ANSI Color Codes for Terminal
// ============================================================================

namespace {
// ANSI escape codes
const char *RESET = "\033[0m";
const char *BOLD = "\033[1m";
const char *DIM = "\033[2m";

// Gem colors (bright versions for better visibility)
const char *COLOR_RED = "\033[91m";          // Bright red
const char *COLOR_ORANGE = "\033[38;5;208m"; // Orange (256-color)
const char *COLOR_YELLOW = "\033[93m";       // Bright yellow
const char *COLOR_GREEN = "\033[92m";        // Bright green
const char *COLOR_BLUE = "\033[94m";         // Bright blue
const char *COLOR_PURPLE = "\033[95m";       // Bright magenta
const char *COLOR_WHITE = "\033[97m";        // Bright white
const char *COLOR_HYPERCUBE = "\033[96m";    // Bright cyan
const char *COLOR_EMPTY = "\033[90m";        // Dark gray
const char *COLOR_UNKNOWN = "\033[33m";      // Yellow

// State indicators
const char *FLAME_MARKER = "🔥"; // or use "*" for ASCII
const char *STAR_MARKER = "⭐";  // or use "+" for ASCII

// Get color code for gem type
const char *GetGemColor(GemType type) {
  switch (type) {
  case GemType::RED:
    return COLOR_RED;
  case GemType::ORANGE:
    return COLOR_ORANGE;
  case GemType::YELLOW:
    return COLOR_YELLOW;
  case GemType::GREEN:
    return COLOR_GREEN;
  case GemType::BLUE:
    return COLOR_BLUE;
  case GemType::PURPLE:
    return COLOR_PURPLE;
  case GemType::WHITE:
    return COLOR_WHITE;
  case GemType::HYPERCUBE:
    return COLOR_HYPERCUBE;
  case GemType::EMPTY:
    return COLOR_EMPTY;
  default:
    return COLOR_UNKNOWN;
  }
}

// Get single-character representation
char GetGemChar(GemType type) {
  switch (type) {
  case GemType::RED:
    return 'R';
  case GemType::ORANGE:
    return 'O';
  case GemType::YELLOW:
    return 'Y';
  case GemType::GREEN:
    return 'G';
  case GemType::BLUE:
    return 'B';
  case GemType::PURPLE:
    return 'P';
  case GemType::WHITE:
    return 'W';
  case GemType::HYPERCUBE:
    return 'H';
  case GemType::EMPTY:
    return '.';
  default:
    return '?';
  }
}

// Get state marker
std::string GetStateMarker(GemState state) {
  switch (state) {
  case GemState::FLAME:
    return "*"; // ASCII fallback for flame
  case GemState::STAR:
    return "+"; // ASCII fallback for star
  default:
    return " ";
  }
}

} // namespace

// ============================================================================
// Public Debug Functions
// ============================================================================

namespace BoardDebug {

void PrintBoard(const BoardState &board, const std::string &title) {
  if (!title.empty()) {
    std::cout << BOLD << title << RESET << "\n";
  }

  // Column headers
  std::cout << "   ";
  for (int col = 0; col < BoardState::SIZE; ++col) {
    std::cout << " " << col << " ";
  }
  std::cout << "\n";

  // Top border
  std::cout << "  +";
  for (int col = 0; col < BoardState::SIZE; ++col) {
    std::cout << "---";
  }
  std::cout << "+\n";

  // Board rows
  for (int row = 0; row < BoardState::SIZE; ++row) {
    std::cout << row << " |";
    for (int col = 0; col < BoardState::SIZE; ++col) {
      const BoardCell &cell = board.cells[row][col];
      const char *color = GetGemColor(cell.type);
      char gemChar = GetGemChar(cell.type);
      std::string stateMarker = GetStateMarker(cell.state);

      // Print: color + gem + state marker + reset
      std::cout << color << " " << gemChar << stateMarker << RESET;
    }
    std::cout << "| " << row << "\n";
  }

  // Bottom border
  std::cout << "  +";
  for (int col = 0; col < BoardState::SIZE; ++col) {
    std::cout << "---";
  }
  std::cout << "+\n";

  // Column headers (bottom)
  std::cout << "   ";
  for (int col = 0; col < BoardState::SIZE; ++col) {
    std::cout << " " << col << " ";
  }
  std::cout << "\n";

  // Legend
  std::cout << DIM << "  * = Flame, + = Star, H = Hypercube" << RESET << "\n\n";
}

void PrintSwapSimulation(const BoardState &board, const MatchableSwap &swap) {
  std::cout << BOLD << "=== Swap Simulation ===" << RESET << "\n";
  std::cout << "Swapping (" << swap.a.x << "," << swap.a.y << ") <-> ("
            << swap.b.x << "," << swap.b.y << ")\n\n";

  // Step 1: Initial board
  PrintBoard(board, "1. Initial Board");

  // Step 2: After swap
  BoardState afterSwap = board;
  afterSwap.applySwap(swap);
  PrintBoard(afterSwap, "2. After Swap");

  // Step 3: After first clear
  BoardState afterClear = afterSwap;
  int firstClears = afterClear.clearMatches(&swap);
  std::cout << BOLD << "     Cleared " << firstClears << " gems\n" << RESET;
  PrintBoard(afterClear, "3. After First Clear");

  // Step 4: After gravity
  BoardState afterGravity = afterClear;
  afterGravity.applyGravity();
  PrintBoard(afterGravity, "4. After Gravity");

  // Step 5+: Cascades
  int cascadeNum = 1;
  int totalCleared = firstClears;
  BoardState cascading = afterGravity;

  while (true) {
    int cleared = cascading.clearMatches();
    if (cleared == 0)
      break;

    totalCleared += cleared;
    std::cout << BOLD << "     Cascade " << cascadeNum << ": Cleared "
              << cleared << " gems\n"
              << RESET;

    std::ostringstream titleClear;
    titleClear << "5." << cascadeNum << "a After Cascade " << cascadeNum
               << " Clear";
    PrintBoard(cascading, titleClear.str());

    cascading.applyGravity();

    std::ostringstream titleGravity;
    titleGravity << "5." << cascadeNum << "b After Gravity";
    PrintBoard(cascading, titleGravity.str());

    cascadeNum++;
  }

  std::cout << BOLD << "\n=== Total Gems Cleared: " << totalCleared << " ===\n"
            << RESET << "\n";
}

void PrintScoredSwaps(const BoardAnalysis &analysis, int topN) {
  if (!analysis.boardValid) {
    std::cout << COLOR_UNKNOWN
              << "Board not fully identified - no analysis available" << RESET
              << "\n";
    return;
  }

  if (analysis.scoredSwaps.empty()) {
    std::cout << DIM << "No valid swaps found" << RESET << "\n";
    return;
  }

  // Sort swaps by score (descending)
  std::vector<size_t> indices(analysis.scoredSwaps.size());
  for (size_t i = 0; i < indices.size(); ++i) {
    indices[i] = i;
  }
  std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
    return analysis.scoredSwaps[a].totalScore >
           analysis.scoredSwaps[b].totalScore;
  });

  int count = (topN <= 0) ? static_cast<int>(indices.size())
                          : std::min(topN, static_cast<int>(indices.size()));

  std::cout << BOLD << "=== Top " << count << " Swap" << (count != 1 ? "s" : "")
            << " ===" << RESET << "\n\n";

  std::cout
      << " # | Swap     | Score | Match | Immediate | Cascade | Special\n";
  std::cout
      << "---+----------+-------+-------+-----------+---------+--------\n";

  for (int i = 0; i < count; ++i) {
    size_t idx = indices[i];
    const ScoredSwap &scored = analysis.scoredSwaps[idx];

    // Highlight the best swap
    const char *highlight =
        (static_cast<int>(idx) == analysis.bestSwapIndex) ? BOLD : "";
    const char *endHighlight =
        (static_cast<int>(idx) == analysis.bestSwapIndex) ? RESET : "";

    std::cout << highlight << std::setw(2) << (i + 1) << " | ";

    // Swap positions
    std::cout << "(" << scored.swap.a.x << "," << scored.swap.a.y << ")<->("
              << scored.swap.b.x << "," << scored.swap.b.y << ") | ";

    // Score
    std::cout << std::setw(5) << scored.totalScore << " | ";

    // Match length
    std::cout << std::setw(5) << scored.matchLength << " | ";

    // Immediate clears
    std::cout << std::setw(9) << scored.immediateClears << " | ";

    // Cascade clears
    std::cout << std::setw(7) << scored.cascadeClears << " | ";

    // Special indicator
    std::cout << (scored.createsSpecial ? "  YES" : "   NO");

    std::cout << endHighlight << "\n";
  }

  std::cout << "\n";
}

void PrintBestSwap(const BoardAnalysis &analysis) {
  if (!analysis.boardValid) {
    std::cout << COLOR_UNKNOWN
              << "Board not fully identified - analysis skipped" << RESET
              << "\n";
    return;
  }

  if (analysis.bestSwapIndex < 0 || analysis.scoredSwaps.empty()) {
    std::cout << DIM << "No valid swaps available" << RESET << "\n";
    return;
  }

  const ScoredSwap &best = analysis.scoredSwaps[analysis.bestSwapIndex];

  std::cout << BOLD << "\n+======================================+\n";
  std::cout << "|       BEST SWAP SIMULATION          |\n";
  std::cout << "+======================================+" << RESET << "\n\n";

  std::cout << "Swap: (" << best.swap.a.x << "," << best.swap.a.y << ") <-> ("
            << best.swap.b.x << "," << best.swap.b.y << ")\n";
  std::cout << "Score: " << BOLD << best.totalScore << RESET << "\n";
  std::cout << "Match Length: " << best.matchLength << "\n";
  std::cout << "Immediate Clears: " << best.immediateClears << "\n";
  std::cout << "Total Cascade Clears: " << best.cascadeClears << "\n";
  std::cout << "Creates Special: " << (best.createsSpecial ? "YES" : "NO")
            << "\n\n";

  PrintSwapSimulation(analysis.board, best.swap);
}

void PrintFullAnalysis(const BoardAnalysis &analysis) {
  std::cout << BOLD << "\n+======================================+\n";
  std::cout << "|      FULL BOARD ANALYSIS            |\n";
  std::cout << "+======================================+" << RESET << "\n\n";

  // Current board state
  PrintBoard(analysis.board, "Current Board State");

  if (!analysis.boardValid) {
    std::cout << COLOR_UNKNOWN
              << "! Board contains UNKNOWN gems - skipping analysis" << RESET
              << "\n\n";
    return;
  }

  // All scored swaps
  PrintScoredSwaps(analysis, 0); // Show all swaps

  // Best swap simulation
  if (analysis.bestSwapIndex >= 0) {
    PrintBestSwap(analysis);
  }
}

} // namespace BoardDebug
