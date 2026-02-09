#include "board_analysis.h"
#include "config.h"
#include <algorithm>

// ============================================================================
// BoardState Implementation
// ============================================================================

namespace {
bool IsMatchableType(GemType type) {
  switch (type) {
  case GemType::RED:
  case GemType::ORANGE:
  case GemType::YELLOW:
  case GemType::GREEN:
  case GemType::BLUE:
  case GemType::PURPLE:
  case GemType::WHITE:
    return true;
  default:
    return false;
  }
}
} // namespace

void BoardState::initFromGrid(const std::vector<std::vector<Gem>> &grid) {
  for (int row = 0; row < SIZE; ++row) {
    for (int col = 0; col < SIZE; ++col) {
      cells[row][col].type = grid[row][col].type;
      cells[row][col].state = grid[row][col].state;
    }
  }
}

GemType BoardState::getType(int row, int col, const MatchableSwap *swap) const {
  if (swap) {
    if (row == swap->a.y && col == swap->a.x)
      return cells[swap->b.y][swap->b.x].type;
    if (row == swap->b.y && col == swap->b.x)
      return cells[swap->a.y][swap->a.x].type;
  }
  return cells[row][col].type;
}

bool BoardState::wouldFormMatchAt(int row, int col,
                                  const MatchableSwap &swap) const {
  GemType type = getType(row, col, &swap);
  if (!IsMatchableType(type))
    return false;

  // Horizontal
  int count = 1;
  for (int x = col - 1; x >= 0; --x) {
    if (getType(row, x, &swap) != type)
      break;
    ++count;
  }
  for (int x = col + 1; x < SIZE; ++x) {
    if (getType(row, x, &swap) != type)
      break;
    ++count;
  }
  if (count >= 3)
    return true;

  // Vertical
  count = 1;
  for (int y = row - 1; y >= 0; --y) {
    if (getType(y, col, &swap) != type)
      break;
    ++count;
  }
  for (int y = row + 1; y < SIZE; ++y) {
    if (getType(y, col, &swap) != type)
      break;
    ++count;
  }
  return count >= 3;
}

bool BoardState::swapCreatesMatch(const MatchableSwap &swap) const {
  GemType typeA = cells[swap.a.y][swap.a.x].type;
  GemType typeB = cells[swap.b.y][swap.b.x].type;

  if (!IsMatchableType(typeA) || !IsMatchableType(typeB))
    return false;
  if (typeA == typeB)
    return false;

  return wouldFormMatchAt(swap.a.y, swap.a.x, swap) ||
         wouldFormMatchAt(swap.b.y, swap.b.x, swap);
}

std::vector<MatchableSwap> BoardState::findValidSwaps() const {
  std::vector<MatchableSwap> swaps;

  for (int row = 0; row < SIZE; ++row) {
    for (int col = 0; col < SIZE; ++col) {
      // Horizontal swap
      if (col + 1 < SIZE) {
        MatchableSwap swap{cv::Point(col, row), cv::Point(col + 1, row)};
        if (swapCreatesMatch(swap)) {
          swaps.push_back(swap);
        }
      }
      // Vertical swap
      if (row + 1 < SIZE) {
        MatchableSwap swap{cv::Point(col, row), cv::Point(col, row + 1)};
        if (swapCreatesMatch(swap)) {
          swaps.push_back(swap);
        }
      }
    }
  }

  return swaps;
}

void BoardState::applySwap(const MatchableSwap &swap) {
  std::swap(cells[swap.a.y][swap.a.x], cells[swap.b.y][swap.b.x]);
}

int BoardState::clearMatches() {
  bool toClear[SIZE][SIZE] = {};
  int cleared = 0;

  // Horizontal matches
  for (int row = 0; row < SIZE; ++row) {
    int col = 0;
    while (col < SIZE - 2) {
      GemType type = cells[row][col].type;
      if (!IsMatchableType(type)) {
        ++col;
        continue;
      }

      int count = 1;
      while (col + count < SIZE && cells[row][col + count].type == type) {
        ++count;
      }
      if (count >= 3) {
        for (int i = 0; i < count; ++i) {
          toClear[row][col + i] = true;
        }
      }
      col += count;
    }
  }

  // Vertical matches
  for (int col = 0; col < SIZE; ++col) {
    int row = 0;
    while (row < SIZE - 2) {
      GemType type = cells[row][col].type;
      if (!IsMatchableType(type)) {
        ++row;
        continue;
      }

      int count = 1;
      while (row + count < SIZE && cells[row + count][col].type == type) {
        ++count;
      }
      if (count >= 3) {
        for (int i = 0; i < count; ++i) {
          toClear[row + i][col] = true;
        }
      }
      row += count;
    }
  }

  // Clear marked cells
  for (int row = 0; row < SIZE; ++row) {
    for (int col = 0; col < SIZE; ++col) {
      if (toClear[row][col]) {
        cells[row][col].type = GemType::EMPTY;
        cells[row][col].state = GemState::NORMAL;
        ++cleared;
      }
    }
  }

  return cleared;
}

bool BoardState::applyGravity() {
  bool moved = false;

  for (int col = 0; col < SIZE; ++col) {
    // Compact non-empty cells downward
    int writeRow = SIZE - 1;
    for (int readRow = SIZE - 1; readRow >= 0; --readRow) {
      if (cells[readRow][col].type != GemType::EMPTY) {
        if (readRow != writeRow) {
          cells[writeRow][col] = cells[readRow][col];
          cells[readRow][col] = {GemType::EMPTY, GemState::NORMAL};
          moved = true;
        }
        --writeRow;
      }
    }
  }

  return moved;
}

int BoardState::resolveBoard() {
  int totalCleared = 0;

  while (true) {
    int cleared = clearMatches();
    if (cleared == 0)
      break;
    totalCleared += cleared;
    applyGravity();
    // Note: In the real game, new gems fall from above to fill empties.
    // For DFS simulation we can't predict those, so cascading stops
    // when no more matches exist among the remaining gems.
  }

  return totalCleared;
}

bool BoardState::isFullyIdentified() const {
  for (int row = 0; row < SIZE; ++row) {
    for (int col = 0; col < SIZE; ++col) {
      if (cells[row][col].type == GemType::UNKNOWN)
        return false;
    }
  }
  return true;
}

// ============================================================================
// Match Length Computation
// ============================================================================

namespace {
/// Compute the longest match at (row, col) after a virtual swap
int ComputeMatchLength(const BoardState &board, int row, int col,
                       const MatchableSwap &swap) {
  GemType type = board.getType(row, col, &swap);
  if (!IsMatchableType(type))
    return 0;

  int maxLen = 0;

  // Horizontal
  int count = 1;
  for (int x = col - 1; x >= 0; --x) {
    if (board.getType(row, x, &swap) != type)
      break;
    ++count;
  }
  for (int x = col + 1; x < BoardState::SIZE; ++x) {
    if (board.getType(row, x, &swap) != type)
      break;
    ++count;
  }
  if (count >= 3)
    maxLen = std::max(maxLen, count);

  // Vertical
  count = 1;
  for (int y = row - 1; y >= 0; --y) {
    if (board.getType(y, col, &swap) != type)
      break;
    ++count;
  }
  for (int y = row + 1; y < BoardState::SIZE; ++y) {
    if (board.getType(y, col, &swap) != type)
      break;
    ++count;
  }
  if (count >= 3)
    maxLen = std::max(maxLen, count);

  return maxLen;
}
} // namespace

// ============================================================================
// Board Analysis
// ============================================================================

void BoardAnalysis::analyze(const std::vector<std::vector<Gem>> &grid) {
  // Snapshot the board
  board.initFromGrid(grid);
  boardValid = board.isFullyIdentified();

  // Find all valid swaps
  std::vector<MatchableSwap> validSwaps = board.findValidSwaps();

  // Score each swap
  scoredSwaps.clear();
  scoredSwaps.reserve(validSwaps.size());
  bestSwapIndex = -1;
  int bestScore = -1;

  for (size_t i = 0; i < validSwaps.size(); ++i) {
    const MatchableSwap &swap = validSwaps[i];
    ScoredSwap scored;
    scored.swap = swap;

    // Compute match length at both swap positions
    int lenA = ComputeMatchLength(board, swap.a.y, swap.a.x, swap);
    int lenB = ComputeMatchLength(board, swap.b.y, swap.b.x, swap);
    scored.matchLength = std::max(lenA, lenB);
    scored.createsSpecial = (scored.matchLength >= 4);

    // Simulate the swap to count clears and cascades
    BoardState simBoard = board;
    simBoard.applySwap(swap);
    scored.immediateClears = simBoard.clearMatches();
    simBoard.applyGravity();

    // Continue cascading
    scored.cascadeClears = scored.immediateClears;
    while (true) {
      int more = simBoard.clearMatches();
      if (more == 0)
        break;
      scored.cascadeClears += more;
      simBoard.applyGravity();
    }

    // Weighted score: prioritize cascades > specials > immediate clears
    scored.totalScore = scored.cascadeClears * 10 +
                        (scored.createsSpecial ? 50 : 0) +
                        scored.matchLength * 5;

    scoredSwaps.push_back(scored);

    if (scored.totalScore > bestScore) {
      bestScore = scored.totalScore;
      bestSwapIndex = static_cast<int>(i);
    }
  }
}
