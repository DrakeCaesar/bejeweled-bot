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
      cells[row][col].pending = false;
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

GemType BoardState::activeType(int row, int col,
                               const MatchableSwap *swap) const {
  if (swap) {
    if (row == swap->a.y && col == swap->a.x) {
      const BoardCell &src = cells[swap->b.y][swap->b.x];
      return src.pending ? GemType::UNKNOWN : src.type;
    }
    if (row == swap->b.y && col == swap->b.x) {
      const BoardCell &src = cells[swap->a.y][swap->a.x];
      return src.pending ? GemType::UNKNOWN : src.type;
    }
  }
  // Locked gems break runs — they belong to an already-committed match.
  if (cells[row][col].pending)
    return GemType::UNKNOWN;
  return cells[row][col].type;
}

void BoardState::markPendingMatches() {
  clearPendingMatches();

  // Horizontal runs of 3+
  for (int row = 0; row < SIZE; ++row) {
    int col = 0;
    while (col < SIZE) {
      GemType type = cells[row][col].type;
      if (!IsMatchableType(type)) {
        ++col;
        continue;
      }
      int count = 1;
      while (col + count < SIZE && cells[row][col + count].type == type)
        ++count;
      if (count >= 3)
        for (int k = 0; k < count; ++k)
          cells[row][col + k].pending = true;
      col += count;
    }
  }

  // Vertical runs of 3+
  for (int col = 0; col < SIZE; ++col) {
    int row = 0;
    while (row < SIZE) {
      GemType type = cells[row][col].type;
      if (!IsMatchableType(type)) {
        ++row;
        continue;
      }
      int count = 1;
      while (row + count < SIZE && cells[row + count][col].type == type)
        ++count;
      if (count >= 3)
        for (int k = 0; k < count; ++k)
          cells[row + k][col].pending = true;
      row += count;
    }
  }
}

void BoardState::clearPendingMatches() {
  for (int row = 0; row < SIZE; ++row)
    for (int col = 0; col < SIZE; ++col)
      cells[row][col].pending = false;
}

bool BoardState::wouldFormMatchAt(int row, int col,
                                  const MatchableSwap &swap) const {
  GemType type = activeType(row, col, &swap);
  if (!IsMatchableType(type))
    return false;

  // Horizontal
  int count = 1;
  for (int x = col - 1; x >= 0; --x) {
    if (activeType(row, x, &swap) != type)
      break;
    ++count;
  }
  for (int x = col + 1; x < SIZE; ++x) {
    if (activeType(row, x, &swap) != type)
      break;
    ++count;
  }
  if (count >= 3)
    return true;

  // Vertical
  count = 1;
  for (int y = row - 1; y >= 0; --y) {
    if (activeType(y, col, &swap) != type)
      break;
    ++count;
  }
  for (int y = row + 1; y < SIZE; ++y) {
    if (activeType(y, col, &swap) != type)
      break;
    ++count;
  }
  return count >= 3;
}

bool BoardState::swapCreatesMatch(const MatchableSwap &swap) const {
  // A gem that is already part of a committed match cannot be moved or used to
  // form a new match.
  if (cells[swap.a.y][swap.a.x].pending ||
      cells[swap.b.y][swap.b.x].pending)
    return false;

  GemType typeA = cells[swap.a.y][swap.a.x].type;
  GemType typeB = cells[swap.b.y][swap.b.x].type;

  // Hypercube swaps are always valid (swap with any other gem)
  if (typeA == GemType::HYPERCUBE || typeB == GemType::HYPERCUBE) {
    // Must swap with a matchable type (not both hypercubes, not empty/unknown)
    if (typeA == GemType::HYPERCUBE && IsMatchableType(typeB))
      return true;
    if (typeB == GemType::HYPERCUBE && IsMatchableType(typeA))
      return true;
    return false;
  }

  // Standard swap logic for non-hypercube gems
  if (!IsMatchableType(typeA) || !IsMatchableType(typeB))
    return false;
  if (typeA == typeB)
    return false;

  return wouldFormMatchAt(swap.a.y, swap.a.x, swap) ||
         wouldFormMatchAt(swap.b.y, swap.b.x, swap);
}

bool BoardState::hasExistingMatch() const {
  // Horizontal runs of 3+
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
      if (count >= 3)
        return true;
      col += count;
    }
  }

  // Vertical runs of 3+
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
      if (count >= 3)
        return true;
      row += count;
    }
  }

  return false;
}

std::vector<MatchableSwap>
BoardState::findValidSwaps(bool excludeHypercubes) const {
  std::vector<MatchableSwap> swaps;

  for (int row = 0; row < SIZE; ++row) {
    for (int col = 0; col < SIZE; ++col) {
      // Horizontal swap
      if (col + 1 < SIZE) {
        MatchableSwap swap{cv::Point(col, row), cv::Point(col + 1, row)};

        // Skip if excluding hypercubes and either cell is a hypercube
        if (excludeHypercubes &&
            (cells[row][col].type == GemType::HYPERCUBE ||
             cells[row][col + 1].type == GemType::HYPERCUBE)) {
          continue;
        }

        if (swapCreatesMatch(swap)) {
          swaps.push_back(swap);
        }
      }
      // Vertical swap
      if (row + 1 < SIZE) {
        MatchableSwap swap{cv::Point(col, row), cv::Point(col, row + 1)};

        // Skip if excluding hypercubes and either cell is a hypercube
        if (excludeHypercubes &&
            (cells[row][col].type == GemType::HYPERCUBE ||
             cells[row + 1][col].type == GemType::HYPERCUBE)) {
          continue;
        }

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

int BoardState::clearMatches(const MatchableSwap *swap,
                             bool allowSpecialCreation) {
  // Phase 1: Find all straight 3+ matches and mark for clearing
  bool toClear[SIZE][SIZE] = {};
  struct Match {
    int row, col, length;
    bool isHorizontal;
    GemType type; // Store gem type
  };
  std::vector<Match> matches;

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
        matches.push_back({row, col, count, true, type});
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
        matches.push_back({row, col, count, false, type});
        for (int i = 0; i < count; ++i) {
          toClear[row + i][col] = true;
        }
      }
      row += count;
    }
  }

  // Phase 2: Create special gems from 4+ matches (before clearing).
  //
  // NOTE: the game creates MULTIPLE specials when several runs qualify in the
  // same resolve step (verified in-game: a 4+4 intersection yields several
  // flames plus a star), so we plant one per qualifying run rather than
  // collapsing to a single "best" special.
  if (allowSpecialCreation && !matches.empty()) {
    for (const auto &match : matches) {
      int targetRow, targetCol;

      // Determine target position: prefer swap position if in match, otherwise
      // center
      auto getTargetPos = [&]() {
        if (swap) {
          bool swapAInMatch =
              match.isHorizontal
                  ? (match.row == swap->a.y && swap->a.x >= match.col &&
                     swap->a.x < match.col + match.length)
                  : (match.col == swap->a.x && swap->a.y >= match.row &&
                     swap->a.y < match.row + match.length);
          bool swapBInMatch =
              match.isHorizontal
                  ? (match.row == swap->b.y && swap->b.x >= match.col &&
                     swap->b.x < match.col + match.length)
                  : (match.col == swap->b.x && swap->b.y >= match.row &&
                     swap->b.y < match.row + match.length);
          if (swapAInMatch) {
            targetRow = swap->a.y;
            targetCol = swap->a.x;
            return;
          } else if (swapBInMatch) {
            targetRow = swap->b.y;
            targetCol = swap->b.x;
            return;
          }
        }
        // Fallback: center of match
        targetRow =
            match.isHorizontal ? match.row : match.row + match.length / 2;
        targetCol =
            match.isHorizontal ? match.col + match.length / 2 : match.col;
      };

      if (match.length >= 6) {
        // 6+ in a row creates SUPERNOVA gem
        getTargetPos();
        toClear[targetRow][targetCol] = false;
        cells[targetRow][targetCol].state = GemState::SUPERNOVA;
        cells[targetRow][targetCol].type = match.type; // Preserve color
      } else if (match.length == 5) {
        // 5 in a row creates HYPERCUBE
        getTargetPos();
        toClear[targetRow][targetCol] = false;
        cells[targetRow][targetCol].type = GemType::HYPERCUBE;
        cells[targetRow][targetCol].state = GemState::NORMAL;
      } else if (match.length == 4) {
        // 4 in a row creates FLAME gem
        getTargetPos();
        toClear[targetRow][targetCol] = false;
        cells[targetRow][targetCol].state = GemState::FLAME;
        cells[targetRow][targetCol].type = match.type; // Preserve color
      }
    }
  }

  // Phase 2b: Check for T-shape patterns (creates STAR gem)
  if (allowSpecialCreation && swap) {
    // T-shape: 3+ horizontal with 3+ vertical intersecting
    for (const auto &hMatch : matches) {
      if (!hMatch.isHorizontal || hMatch.length < 3)
        continue;

      for (const auto &vMatch : matches) {
        if (vMatch.isHorizontal || vMatch.length < 3)
          continue;

        // Check if they intersect
        for (int hPos = hMatch.col; hPos < hMatch.col + hMatch.length; ++hPos) {
          for (int vPos = vMatch.row; vPos < vMatch.row + vMatch.length;
               ++vPos) {
            if (hMatch.row == vPos && vMatch.col == hPos &&
                hMatch.type == vMatch.type) {
              // Found T-shape intersection - create STAR at intersection
              toClear[vPos][hPos] = false;
              cells[vPos][hPos].state = GemState::STAR;
              cells[vPos][hPos].type = hMatch.type;
              goto t_shape_done; // Exit nested loops
            }
          }
        }
      }
    }
  t_shape_done:;
  }

  // Phase 3: Detonate special gems in the match zone
  std::vector<std::pair<int, int>> flameDetonations;
  std::vector<std::pair<int, int>> starDetonations;
  std::vector<std::pair<int, int>> supernovaDetonations;
  std::vector<std::pair<int, int>> hypercubeDetonations;

  for (int row = 0; row < SIZE; ++row) {
    for (int col = 0; col < SIZE; ++col) {
      if (toClear[row][col]) {
        GemState state = cells[row][col].state;
        GemType type = cells[row][col].type;

        if (state == GemState::FLAME) {
          flameDetonations.push_back({row, col});
        } else if (state == GemState::STAR) {
          starDetonations.push_back({row, col});
        } else if (state == GemState::SUPERNOVA) {
          supernovaDetonations.push_back({row, col});
        } else if (type == GemType::HYPERCUBE) {
          hypercubeDetonations.push_back({row, col});
        }
      }
    }
  }

  // Handle flame detonations: clear 3x3 around each flame
  for (const auto &[fRow, fCol] : flameDetonations) {
    GemType flameColor = cells[fRow][fCol].type;
    for (int dy = -1; dy <= 1; ++dy) {
      for (int dx = -1; dx <= 1; ++dx) {
        int r = fRow + dy;
        int c = fCol + dx;
        if (r >= 0 && r < SIZE && c >= 0 && c < SIZE) {
          // Flame + Hypercube interaction: clear all gems of flame's color
          if (cells[r][c].type == GemType::HYPERCUBE) {
            for (int nr = 0; nr < SIZE; ++nr) {
              for (int nc = 0; nc < SIZE; ++nc) {
                if (cells[nr][nc].type == flameColor) {
                  toClear[nr][nc] = true;
                }
              }
            }
          }
          toClear[r][c] = true;
        }
      }
    }
  }

  // Handle star detonations: clear entire row + column
  for (const auto &[sRow, sCol] : starDetonations) {
    GemType starColor = cells[sRow][sCol].type;
    // Clear row
    for (int c = 0; c < SIZE; ++c) {
      if (cells[sRow][c].type == GemType::HYPERCUBE) {
        // Star + Hypercube: clear all gems of star's color
        for (int nr = 0; nr < SIZE; ++nr) {
          for (int nc = 0; nc < SIZE; ++nc) {
            if (cells[nr][nc].type == starColor) {
              toClear[nr][nc] = true;
            }
          }
        }
      }
      toClear[sRow][c] = true;
    }
    // Clear column
    for (int r = 0; r < SIZE; ++r) {
      if (cells[r][sCol].type == GemType::HYPERCUBE) {
        for (int nr = 0; nr < SIZE; ++nr) {
          for (int nc = 0; nc < SIZE; ++nc) {
            if (cells[nr][nc].type == starColor) {
              toClear[nr][nc] = true;
            }
          }
        }
      }
      toClear[r][sCol] = true;
    }
  }

  // Handle supernova detonations: clear row, column, and 2 adjacent
  // rows/columns
  for (const auto &[snRow, snCol] : supernovaDetonations) {
    GemType supernovaColor = cells[snRow][snCol].type;

    // Clear 3 rows: the supernova's row and one above/below
    for (int rowOffset = -1; rowOffset <= 1; ++rowOffset) {
      int r = snRow + rowOffset;
      if (r < 0 || r >= SIZE)
        continue;

      for (int c = 0; c < SIZE; ++c) {
        if (cells[r][c].type == GemType::HYPERCUBE) {
          // Supernova + Hypercube: clear all gems of supernova's color
          for (int nr = 0; nr < SIZE; ++nr) {
            for (int nc = 0; nc < SIZE; ++nc) {
              if (cells[nr][nc].type == supernovaColor) {
                toClear[nr][nc] = true;
              }
            }
          }
        }
        toClear[r][c] = true;
      }
    }

    // Clear 3 columns: the supernova's column and one left/right
    for (int colOffset = -1; colOffset <= 1; ++colOffset) {
      int c = snCol + colOffset;
      if (c < 0 || c >= SIZE)
        continue;

      for (int r = 0; r < SIZE; ++r) {
        if (cells[r][c].type == GemType::HYPERCUBE) {
          // Supernova + Hypercube: clear all gems of supernova's color
          for (int nr = 0; nr < SIZE; ++nr) {
            for (int nc = 0; nc < SIZE; ++nc) {
              if (cells[nr][nc].type == supernovaColor) {
                toClear[nr][nc] = true;
              }
            }
          }
        }
        toClear[r][c] = true;
      }
    }
  }

  // Handle hypercube detonations (matched directly in a 3+)
  for (const auto &[hRow, hCol] : hypercubeDetonations) {
    // Find matched color (scan neighbors in cleared zone)
    GemType matchedColor = GemType::UNKNOWN;
    for (int dr = -1; dr <= 1 && matchedColor == GemType::UNKNOWN; ++dr) {
      for (int dc = -1; dc <= 1 && matchedColor == GemType::UNKNOWN; ++dc) {
        if (dr == 0 && dc == 0)
          continue;
        int r = hRow + dr;
        int c = hCol + dc;
        if (r >= 0 && r < SIZE && c >= 0 && c < SIZE && toClear[r][c]) {
          GemType nType = cells[r][c].type;
          if (IsMatchableType(nType)) {
            matchedColor = nType;
          }
        }
      }
    }
    // Clear all gems of that color
    if (matchedColor != GemType::UNKNOWN) {
      for (int r = 0; r < SIZE; ++r) {
        for (int c = 0; c < SIZE; ++c) {
          if (cells[r][c].type == matchedColor) {
            toClear[r][c] = true;
          }
        }
      }
    }
  }

  // Phase 4: Actually clear all marked cells
  int cleared = 0;
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

bool BoardState::equals(const BoardState &other) const {
  for (int row = 0; row < SIZE; ++row) {
    for (int col = 0; col < SIZE; ++col) {
      if (cells[row][col].type != other.cells[row][col].type ||
          cells[row][col].state != other.cells[row][col].state) {
        return false;
      }
    }
  }
  return true;
}

int BoardState::simulateMove(const MatchableSwap &swap,
                             BoardState &finalBoard) const {
  finalBoard = *this;
  finalBoard.clearPendingMatches(); // model the future resolution, not locks
  finalBoard.applySwap(swap);

  int total = finalBoard.clearMatches(&swap);
  finalBoard.applyGravity();
  while (true) {
    int more = finalBoard.clearMatches();
    if (more == 0)
      break;
    total += more;
    finalBoard.applyGravity();
  }
  return total;
}

void BoardState::fillEmptiesFrom(const BoardState &src) {
  for (int row = 0; row < SIZE; ++row) {
    for (int col = 0; col < SIZE; ++col) {
      if (cells[row][col].type == GemType::EMPTY &&
          src.cells[row][col].type != GemType::EMPTY) {
        cells[row][col] = src.cells[row][col];
      }
    }
  }
}

int BoardState::simulateMoveSeeded(const MatchableSwap &swap,
                                   const BoardState &refill,
                                   BoardState &finalBoard) const {
  finalBoard = *this;
  finalBoard.clearPendingMatches();
  finalBoard.applySwap(swap);

  int total = 0;
  const MatchableSwap *swapPtr = &swap;
  while (true) {
    int cleared = finalBoard.clearMatches(swapPtr);
    if (cleared == 0)
      break;
    total += cleared;
    finalBoard.applyGravity();
    // Seed the emptied top cells with the gems the game actually spawned there,
    // so the cascade continues through the real refill instead of stopping.
    finalBoard.fillEmptiesFrom(refill);
    swapPtr = nullptr;
  }
  return total;
}

std::string BoardState::toString() const {
  std::string s;
  s.reserve(SIZE * (SIZE + 1));
  for (int row = 0; row < SIZE; ++row) {
    for (int col = 0; col < SIZE; ++col) {
      char ch;
      switch (cells[row][col].type) {
      case GemType::RED:
        ch = 'r';
        break;
      case GemType::ORANGE:
        ch = 'o';
        break;
      case GemType::YELLOW:
        ch = 'y';
        break;
      case GemType::GREEN:
        ch = 'g';
        break;
      case GemType::BLUE:
        ch = 'b';
        break;
      case GemType::PURPLE:
        ch = 'p';
        break;
      case GemType::WHITE:
        ch = 'w';
        break;
      case GemType::HYPERCUBE:
        ch = 'H';
        break;
      case GemType::EMPTY:
        ch = '.';
        break;
      default:
        ch = '?';
        break;
      }
      s += ch;
    }
    if (row + 1 < SIZE)
      s += '\n';
  }
  return s;
}

// ============================================================================
// Match Length Computation
// ============================================================================

namespace {
/// Compute the longest match at (row, col) after a virtual swap
int ComputeMatchLength(const BoardState &board, int row, int col,
                       const MatchableSwap &swap) {
  GemType type = board.activeType(row, col, &swap);
  if (!IsMatchableType(type))
    return 0;

  int maxLen = 0;

  // Horizontal
  int count = 1;
  for (int x = col - 1; x >= 0; --x) {
    if (board.activeType(row, x, &swap) != type)
      break;
    ++count;
  }
  for (int x = col + 1; x < BoardState::SIZE; ++x) {
    if (board.activeType(row, x, &swap) != type)
      break;
    ++count;
  }
  if (count >= 3)
    maxLen = std::max(maxLen, count);

  // Vertical
  count = 1;
  for (int y = row - 1; y >= 0; --y) {
    if (board.activeType(y, col, &swap) != type)
      break;
    ++count;
  }
  for (int y = row + 1; y < BoardState::SIZE; ++y) {
    if (board.activeType(y, col, &swap) != type)
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
  BoardState newBoard;
  newBoard.initFromGrid(grid);

  // Skip analysis if board has any UNKNOWN gems - keep previous analysis
  if (!newBoard.isFullyIdentified()) {
    boardValid = false;
    return;
  }

  boardValid = true;

  // Flag gems that are already part of a formed (lingering) match. They are
  // committed: the bot cannot swap them or add to them, so every move search
  // below treats them as non-matchable.
  newBoard.markPendingMatches();

  // A board with a pre-existing match is mid-cascade. The matched gems are
  // already committed (flagged `pending` above) and will clear on their own.
  if (newBoard.hasExistingMatch()) {
    settling = true;
    stableFrameCount = 0;
    board = newBoard;
    if (Config::BOT_WAIT_FOR_SETTLE) {
      // Normal play: wait for the cascade to finish before acting.
      scoredSwaps.clear();
      bestSwapIndex = -1;
    } else {
      // Mid-cascade mode: keep planning. computeScoredSwaps() ignores the
      // locked (pending) gems, so only matches the bot actually creates count.
      computeScoredSwaps();
    }
    return;
  }
  settling = false;

  // Skip expensive re-analysis if board unchanged
  if (board.equals(newBoard)) {
    stableFrameCount++;
    return;
  }

  // Board changed - reset counter and update
  stableFrameCount = 1;
  board = newBoard;
  computeScoredSwaps();
}

void BoardAnalysis::computeScoredSwaps() {
  // Find all valid swaps - prefer non-hypercube moves if configured
  std::vector<MatchableSwap> validSwaps;
  if (Config::BOT_PREFER_NON_HYPERCUBE_MOVES) {
    // Try to find non-hypercube swaps first
    validSwaps = board.findValidSwaps(true);
    // If no non-hypercube swaps, allow hypercube swaps
    if (validSwaps.empty()) {
      validSwaps = board.findValidSwaps(false);
    }
  } else {
    validSwaps = board.findValidSwaps(false);
  }

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

    // Detect what special gem is created (if any)
    scored.specialType = GemState::NORMAL;
    if (scored.matchLength >= 6) {
      scored.specialType = GemState::SUPERNOVA;
      scored.createsSpecial = true;
    } else if (scored.matchLength == 5) {
      // 5 in a row creates Hypercube (represented as hypercube type, not a
      // state)
      scored.createsSpecial = true;
    } else if (scored.matchLength == 4) {
      scored.specialType = GemState::FLAME;
      scored.createsSpecial = true;
    } else if (scored.matchLength == 3) {
      // Check for T-shape: 3+ horizontal and 3+ vertical intersecting
      // This happens when both swap positions form matches
      int lenA = ComputeMatchLength(board, swap.a.y, swap.a.x, swap);
      int lenB = ComputeMatchLength(board, swap.b.y, swap.b.x, swap);
      if (lenA >= 3 && lenB >= 3) {
        // Both positions have matches - likely a T-shape
        scored.specialType = GemState::STAR;
        scored.createsSpecial = true;
      }
    }

    scored.immediateClears = simBoard.clearMatches(&swap);
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

    // Weighted score: prioritize supernova above all else
    // Special bonuses: Supernova (6) = 10000, Hypercube (5) = 100, Star (T) =
    // 50, Flame (4) = 30
    int specialBonus = 0;
    if (scored.specialType == GemState::SUPERNOVA) {
      specialBonus = 10000; // Supernova - massive bonus
    } else if (scored.matchLength == 5) {
      specialBonus = 100; // Hypercube - 5 in a row
    } else if (scored.specialType == GemState::STAR) {
      specialBonus = 50; // Star - T-shape
    } else if (scored.specialType == GemState::FLAME) {
      specialBonus = 30; // Flame - 4 in a row
    }

    scored.totalScore =
        scored.cascadeClears * 10 + specialBonus + scored.matchLength * 5;

    scoredSwaps.push_back(scored);

    if (scored.totalScore > bestScore) {
      bestScore = scored.totalScore;
      bestSwapIndex = static_cast<int>(i);
    }
  }
}
