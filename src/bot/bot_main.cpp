#include "bot_main.h"
#include "config.h"

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

GemType GetTypeWithSwap(const std::vector<std::vector<Gem>> &grid, int row,
                        int col, const MatchableSwap &swap) {
  if (row == swap.a.y && col == swap.a.x) {
    return grid[swap.b.y][swap.b.x].type;
  }
  if (row == swap.b.y && col == swap.b.x) {
    return grid[swap.a.y][swap.a.x].type;
  }
  return grid[row][col].type;
}

bool WouldFormMatchAt(const std::vector<std::vector<Gem>> &grid, int row,
                      int col, const MatchableSwap &swap) {
  const int rows = static_cast<int>(grid.size());
  if (rows == 0)
    return false;
  const int cols = static_cast<int>(grid[0].size());

  GemType type = GetTypeWithSwap(grid, row, col, swap);
  if (!IsMatchableType(type))
    return false;

  int count = 1;
  for (int x = col - 1; x >= 0; --x) {
    if (GetTypeWithSwap(grid, row, x, swap) != type)
      break;
    ++count;
  }
  for (int x = col + 1; x < cols; ++x) {
    if (GetTypeWithSwap(grid, row, x, swap) != type)
      break;
    ++count;
  }
  if (count >= 3)
    return true;

  count = 1;
  for (int y = row - 1; y >= 0; --y) {
    if (GetTypeWithSwap(grid, y, col, swap) != type)
      break;
    ++count;
  }
  for (int y = row + 1; y < rows; ++y) {
    if (GetTypeWithSwap(grid, y, col, swap) != type)
      break;
    ++count;
  }
  return count >= 3;
}

bool SwapCreatesMatch(const std::vector<std::vector<Gem>> &grid,
                      const MatchableSwap &swap) {
  GemType typeA = grid[swap.a.y][swap.a.x].type;
  GemType typeB = grid[swap.b.y][swap.b.x].type;

  if (!IsMatchableType(typeA) || !IsMatchableType(typeB))
    return false;
  if (typeA == typeB)
    return false;

  return WouldFormMatchAt(grid, swap.a.y, swap.a.x, swap) ||
         WouldFormMatchAt(grid, swap.b.y, swap.b.x, swap);
}

cv::Point CellCenter(int row, int col) {
  int x = col * Config::CELL_SIZE + Config::CELL_SIZE / 2;
  int y =
      Config::BANNER_HEIGHT + row * Config::CELL_SIZE + Config::CELL_SIZE / 2;
  return cv::Point(x, y);
}
} // namespace

std::vector<MatchableSwap>
FindMatchableSwaps(const std::vector<std::vector<Gem>> &grid) {
  std::vector<MatchableSwap> swaps;

  const int rows = static_cast<int>(grid.size());
  if (rows == 0)
    return swaps;
  const int cols = static_cast<int>(grid[0].size());

  for (int row = 0; row < rows; ++row) {
    for (int col = 0; col < cols; ++col) {
      if (col + 1 < cols) {
        MatchableSwap swap{cv::Point(col, row), cv::Point(col + 1, row)};
        if (SwapCreatesMatch(grid, swap)) {
          swaps.push_back(swap);
        }
      }
      if (row + 1 < rows) {
        MatchableSwap swap{cv::Point(col, row), cv::Point(col, row + 1)};
        if (SwapCreatesMatch(grid, swap)) {
          swaps.push_back(swap);
        }
      }
    }
  }

  return swaps;
}

void DrawMatchableSwaps(cv::Mat &image,
                        const std::vector<std::vector<Gem>> &grid) {
  if (image.empty())
    return;

  const std::vector<MatchableSwap> swaps = FindMatchableSwaps(grid);

  for (const auto &swap : swaps) {
    cv::Point a = CellCenter(swap.a.y, swap.a.x);
    cv::Point b = CellCenter(swap.b.y, swap.b.x);
    cv::line(image, a, b, Config::BOT_MATCH_LINE_COLOR,
             Config::BOT_MATCH_LINE_THICKNESS, cv::LINE_AA);
  }
}
