#include "gem_detector.h"
#include "mask_loader.h"
#include "shape_classifier.h"
#include "thread_pool.h"
#include <iostream>

// ============================================================================
// Grid Detection
// ============================================================================

std::vector<std::vector<Gem>> &DetectGems(const cv::Mat &gameBoard,
                                          Config::ClassificationMode) {
  // Static allocation - grid is reused across calls
  static std::vector<std::vector<Gem>> grid(
      Config::GRID_SIZE, std::vector<Gem>(Config::GRID_SIZE));

  static constexpr int TOTAL_TILES = Config::GRID_SIZE * Config::GRID_SIZE;

  // Ensure scaled cache is ready BEFORE parallel processing
  if (MaskLoader::AreMasksLoaded()) {
    int scaleInt = ShapeParams::refScale.load();
    int blurSize = ShapeParams::refBlur.load();
    cv::Size targetSize(Config::GEM_SHAPE_SIZE, Config::GEM_SHAPE_SIZE);
    MaskLoader::EnsureScaledCache(targetSize, scaleInt, blurSize);
  }

  // Get pointer to static grid for lambda capture
  auto *gridPtr = &grid;

  // Use persistent thread pool - no thread creation per frame
  StaticThreadPool::Instance().ParallelFor(
      [&gameBoard, gridPtr](int threadIndex, int numThreads) {
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

          // Define ROI for shape classification
          int shapeCenterX =
              cellX + (Config::CELL_SIZE - Config::GEM_SHAPE_SIZE) / 2;
          int shapeCenterY =
              cellY + (Config::CELL_SIZE - Config::GEM_SHAPE_SIZE) / 2;
          gem.shapeRoi =
              cv::Rect(shapeCenterX, shapeCenterY, Config::GEM_SHAPE_SIZE,
                       Config::GEM_SHAPE_SIZE);
          gem.roi = gem.shapeRoi;

          // Check if ROI is valid
          bool roiValid =
              gem.shapeRoi.x >= 0 && gem.shapeRoi.y >= 0 &&
              gem.shapeRoi.x + gem.shapeRoi.width <= gameBoard.cols &&
              gem.shapeRoi.y + gem.shapeRoi.height <= gameBoard.rows;

          if (!roiValid) {
            gem.type = GemType::UNKNOWN;
            gem.closestType = GemType::UNKNOWN;
            gem.bestMatch = 0.0f;
            gem.matchedThreshold = false;
            continue;
          }

          // Get gem region
          cv::Mat shapeRegion = gameBoard(gem.shapeRoi);
          gem.avgColor = cv::mean(shapeRegion);

          // Classify gem using shape matching
          cv::Mat processed = ProcessGemForShapeMatching(shapeRegion);
          gem.type = ClassifyGemByShape(processed, &gem.bestMatch,
                                        &gem.bestMatchFrame);
          gem.shapeMask = GetDebugVisualization(shapeRegion);
          gem.closestType = GemType::UNKNOWN;
          gem.matchedThreshold = (gem.type != GemType::UNKNOWN);
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
