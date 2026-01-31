#include "gem_detector.h"
#include "color_classifier.h"
#include "thread_pool.h"
#include <iostream>

// ============================================================================
// Grid Detection
// ============================================================================

std::vector<std::vector<Gem>> &DetectGems(const cv::Mat &gameBoard,
                                          Config::ClassificationMode mode) {
  // Static allocation - grid is reused across calls
  static std::vector<std::vector<Gem>> grid(
      Config::GRID_SIZE, std::vector<Gem>(Config::GRID_SIZE));

  static constexpr int TOTAL_TILES = Config::GRID_SIZE * Config::GRID_SIZE;

  // Get pointer to static grid for lambda capture
  auto *gridPtr = &grid;

  // Use persistent thread pool - no thread creation per frame
  StaticThreadPool::Instance().ParallelFor(
      [&gameBoard, gridPtr, mode](int threadIndex, int numThreads) {
        int tilesPerThread = TOTAL_TILES / numThreads;
        int startIdx = threadIndex * tilesPerThread;
        int endIdx = (threadIndex == numThreads - 1)
                         ? TOTAL_TILES
                         : (threadIndex + 1) * tilesPerThread;

        for (int idx = startIdx; idx < endIdx; idx++) {
          int row = idx / Config::GRID_SIZE;
          int col = idx % Config::GRID_SIZE;

          Gem &gem = (*gridPtr)[row][col];
          gem.gridPos = cv::Point(col, row);
          gem.state = GemState::NORMAL;

          // Calculate cell center
          int cellX = col * Config::CELL_SIZE;
          int cellY = row * Config::CELL_SIZE;
          int centerX =
              cellX + (Config::CELL_SIZE - Config::GEM_CENTER_SIZE) / 2;
          int centerY =
              cellY + (Config::CELL_SIZE - Config::GEM_CENTER_SIZE) / 2;

          // Define ROI for gem center
          gem.roi = cv::Rect(centerX, centerY, Config::GEM_CENTER_SIZE,
                             Config::GEM_CENTER_SIZE);

          // Check if ROI is valid
          if (gem.roi.x + gem.roi.width > gameBoard.cols ||
              gem.roi.y + gem.roi.height > gameBoard.rows) {
            gem.type = GemType::UNKNOWN;
            gem.closestType = GemType::UNKNOWN;
            gem.bestMatch = 0.0f;
            gem.matchedThreshold = false;
            continue;
          }

          // Get gem region
          cv::Mat gemRegion = gameBoard(gem.roi);
          gem.avgColor = cv::mean(gemRegion);

          // Classify gem based on mode
          if (mode == Config::ClassificationMode::SHAPE) {
            // Shape mode placeholder - returns UNKNOWN
            gem.type = GemType::UNKNOWN;
            gem.closestType = GemType::UNKNOWN;
            gem.bestMatch = 0.0f;
            gem.matchedThreshold = false;
          } else {
            // Color classification
            gem.type =
                ClassifyGemByColor(gemRegion, &gem.bestMatch, &gem.closestType);
            gem.matchedThreshold = (gem.type != GemType::UNKNOWN);
          }
        }
      });

  return grid;
}

// ============================================================================
// Debug Output
// ============================================================================

void PrintGemGrid(const std::vector<std::vector<Gem>> &grid) {
  std::cout << "\nCurrent Gem Grid:" << std::endl;
  for (int row = 0; row < Config::GRID_SIZE; row++) {
    for (int col = 0; col < Config::GRID_SIZE; col++) {
      const Gem &gem = grid[row][col];
      std::cout << "  [" << GemTypeToString(gem.type).substr(0, 1) << "]";
    }
    std::cout << std::endl;
  }
  std::cout << std::endl;
}
