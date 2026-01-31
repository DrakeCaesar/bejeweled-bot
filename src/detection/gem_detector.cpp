#include "gem_detector.h"
#include "mask_loader.h"
#include "shape_classifier.h"
#include "thread_pool.h"
#include <iostream>
#include <filesystem>
#include <iomanip>
#include <sstream>

// ============================================================================
// Frame Recording for Asset Collection
// ============================================================================



namespace {
  // Recording configuration
  static bool g_recordingEnabled = false;
  static int g_recordCol = 2;
  static int g_recordRow = 1;
  static int g_maxFrames = 100;
  static int g_recordedFrames = 0;
  static std::string g_outputDir = "recorded_frames";
  
  // Frame comparison for duplicate detection
  static cv::Mat g_previousFrame;
  static cv::Mat g_firstFrame;
  static int g_totalFramesChecked = 0;
}

void StartRecording(int col, int row, int maxFrames, const std::string& outputDir) {
  g_recordCol = col;
  g_recordRow = row;
  g_maxFrames = maxFrames;
  g_recordedFrames = 0;
  g_totalFramesChecked = 0;
  g_outputDir = outputDir;
  
  // Clear previous comparison frames
  g_previousFrame = cv::Mat();
  g_firstFrame = cv::Mat();
  
  // Create output directory
  std::filesystem::create_directories(g_outputDir);
  
  g_recordingEnabled = true;
  
  std::cout << "Started recording: col=" << col << " row=" << row 
            << " maxFrames=" << maxFrames << " output=" << outputDir << std::endl;
  std::cout << "Will skip duplicate frames and stop if animation loops back to start." << std::endl;
}

void StopRecording() {
  g_recordingEnabled = false;
  std::cout << "Recording stopped. Captured " << g_recordedFrames 
            << " unique frames from " << g_totalFramesChecked << " total frames checked." << std::endl;
  
  // Clear comparison frames to free memory
  g_previousFrame = cv::Mat();
  g_firstFrame = cv::Mat();
}

bool IsRecording() {
  return g_recordingEnabled;
}

int GetRecordedFrameCount() {
  return g_recordedFrames;
}

int GetTotalFramesChecked() {
  return g_totalFramesChecked;
}

// ============================================================================
// Grid Detection
// ============================================================================

std::vector<std::vector<Gem>> &DetectGems(const cv::Mat &gameBoard,
                                        Config::ClassificationMode) {
// Static allocation - grid is reused across calls
static std::vector<std::vector<Gem>> grid(
    Config::GRID_SIZE, std::vector<Gem>(Config::GRID_SIZE));

static constexpr int TOTAL_TILES = Config::GRID_SIZE * Config::GRID_SIZE;

// Skip cache building when recording for max performance
if (!g_recordingEnabled && MaskLoader::AreMasksLoaded()) {
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

          // Recording mode: skip ALL processing and only record target position
          if (g_recordingEnabled) {
            // Only process frames from the target position
            if (col == g_recordCol && row == g_recordRow) {
              g_totalFramesChecked++;
              
              bool shouldSave = false;
              bool loopDetected = false;
              
              // First frame - always save
              if (g_firstFrame.empty()) {
                g_firstFrame = shapeRegion.clone();
                g_previousFrame = shapeRegion.clone();
                shouldSave = true;
              }
              // Check if frame is different from the last saved frame
              else {
                double diff = cv::norm(shapeRegion, g_previousFrame, cv::NORM_L2);
                double threshold = 100.0; // Adjust if needed - lower = more sensitive
                
                if (diff > threshold) {
                  // Frame is different from previous saved frame
                  // Check if it loops back to first frame
                  double diffFromFirst = cv::norm(shapeRegion, g_firstFrame, cv::NORM_L2);
                  
                  if (diffFromFirst < threshold && g_recordedFrames > 0) {
                    // Frame matches first frame - animation loop detected
                    loopDetected = true;
                    std::cout << "Animation loop detected! Frame " << g_totalFramesChecked 
                             << " matches first frame. Stopping recording." << std::endl;
                    std::cout << "Captured " << g_recordedFrames 
                             << " unique frames from complete animation cycle." << std::endl;
                    g_recordingEnabled = false;
                  } else {
                    // Frame is unique - save it
                    shouldSave = true;
                  }
                }
                // else: frame is same as previous saved frame, skip it
              }
              
              if (shouldSave && g_recordedFrames < g_maxFrames) {
                std::ostringstream filename;
                filename << g_outputDir << "/frame_" 
                         << std::setw(4) << std::setfill('0') << g_recordedFrames << ".png";
                cv::imwrite(filename.str(), shapeRegion);
                g_previousFrame = shapeRegion.clone(); // Update to this saved frame
                g_recordedFrames++;
                
                if (g_recordedFrames >= g_maxFrames) {
                  std::cout << "Recording complete! Saved " << g_maxFrames 
                           << " unique frames (checked " << g_totalFramesChecked 
                           << " total frames) to " << g_outputDir << std::endl;
                  g_recordingEnabled = false;
                }
              }
            }
            // Skip classification for ALL gems while recording
            gem.type = GemType::UNKNOWN;
            gem.closestType = GemType::UNKNOWN;
            gem.bestMatch = 0.0f;
            gem.matchedThreshold = false;
            gem.shapeMask = cv::Mat();
            continue;
          }

          // Normal mode: classify gem using shape matching
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
