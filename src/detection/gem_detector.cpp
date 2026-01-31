#include "gem_detector.h"
#include "color_classifier.h"
#include "shape_classifier.h"
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

          // Calculate cell position
          int cellX = col * Config::CELL_SIZE;
          int cellY = row * Config::CELL_SIZE;

          // Define ROI for color classification (smaller center region)
          int colorCenterX =
              cellX + (Config::CELL_SIZE - Config::GEM_CENTER_SIZE) / 2;
          int colorCenterY =
              cellY + (Config::CELL_SIZE - Config::GEM_CENTER_SIZE) / 2;
          gem.roi = cv::Rect(colorCenterX, colorCenterY,
                             Config::GEM_CENTER_SIZE, Config::GEM_CENTER_SIZE);

          // Define ROI for shape classification (larger to capture full gem)
          int shapeCenterX =
              cellX + (Config::CELL_SIZE - Config::GEM_SHAPE_SIZE) / 2;
          int shapeCenterY =
              cellY + (Config::CELL_SIZE - Config::GEM_SHAPE_SIZE) / 2;
          gem.shapeRoi =
              cv::Rect(shapeCenterX, shapeCenterY, Config::GEM_SHAPE_SIZE,
                       Config::GEM_SHAPE_SIZE);

          // Check if ROIs are valid
          bool colorRoiValid = gem.roi.x >= 0 && gem.roi.y >= 0 &&
                               gem.roi.x + gem.roi.width <= gameBoard.cols &&
                               gem.roi.y + gem.roi.height <= gameBoard.rows;
          bool shapeRoiValid =
              gem.shapeRoi.x >= 0 && gem.shapeRoi.y >= 0 &&
              gem.shapeRoi.x + gem.shapeRoi.width <= gameBoard.cols &&
              gem.shapeRoi.y + gem.shapeRoi.height <= gameBoard.rows;

          if (!colorRoiValid) {
            gem.type = GemType::UNKNOWN;
            gem.closestType = GemType::UNKNOWN;
            gem.bestMatch = 0.0f;
            gem.matchedThreshold = false;
            continue;
          }

          // Get gem region for color classification
          cv::Mat gemRegion = gameBoard(gem.roi);
          gem.avgColor = cv::mean(gemRegion);

          // Classify gem based on mode
          if (mode == Config::ClassificationMode::SHAPE && shapeRoiValid) {
            // Shape mode - use larger ROI for shape matching
            cv::Mat shapeRegion = gameBoard(gem.shapeRoi);
            cv::Mat processed = ProcessGemForShapeMatching(shapeRegion);
            gem.type = ClassifyGemByShape(processed, &gem.bestMatch,
                                          &gem.bestMatchFrame);
            // Get debug visualization AFTER classification (uses best match data)
            gem.shapeMask = GetDebugVisualization(shapeRegion);
            gem.closestType = GemType::UNKNOWN;
            gem.matchedThreshold = (gem.type != GemType::UNKNOWN);
          } else {
            // Color classification
            gem.shapeMask = cv::Mat(); // Clear shape mask in color mode
            gem.bestMatchFrame = -1;
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
