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
  bool g_recordingEnabled = false;
  int g_recordCol = 2;
  int g_recordRow = 1;
  int g_maxFrames = 1000;
  int g_recordedFrames = 0;
  std::string g_outputDir = "recorded_frames";

  // Frame comparison for duplicate detection
  cv::Mat g_previousFrame;
  cv::Mat g_firstFrame;
  int g_totalFramesChecked = 0;
  std::vector<cv::Mat> g_rawCaptureBuffer;
  std::vector<cv::Mat> g_uniqueFramesBuffer;
}

void StartRecording(int col, int row, int maxFrames, const std::string&outputDir) {
  g_recordCol = col;
  g_recordRow = row;
  g_maxFrames = maxFrames;
  g_recordedFrames = 0;
  g_totalFramesChecked = 0;
  g_outputDir = outputDir;

  // Clear previous comparison frames and memory buffers
  g_previousFrame = cv::Mat();
  g_firstFrame = cv::Mat();
  g_rawCaptureBuffer.clear();
  g_rawCaptureBuffer.reserve(g_maxFrames);
  g_uniqueFramesBuffer.clear();

  // Create the output directory and clear existing PNGs
  if (std::filesystem::exists(g_outputDir)) {
    for (const auto&entry: std::filesystem::directory_iterator(g_outputDir)) {
      if (entry.path().extension() == ".png") {
        std::filesystem::remove(entry.path());
      }
    }
  }
  else {
    std::filesystem::create_directories(g_outputDir);
  }

  g_recordingEnabled = true;

  std::cout << "Started recording (capture only): col=" << col << " row=" << row
      << " maxFrames=" << g_maxFrames << " output=" << outputDir << std::endl;
  std::cout << "Capturing raw frames to memory. Processing will happen after stop." << std::endl;
}

void StopRecording() {
  if (!g_recordingEnabled && g_rawCaptureBuffer.empty()) {
    return;
  }

  bool wasRecording = g_recordingEnabled;
  g_recordingEnabled = false;

  if (!g_rawCaptureBuffer.empty()) {
    std::cout << "Recording stopped. Processing " << g_rawCaptureBuffer.size()
        << " captured frames..." << std::endl;

    // Process captured frames to find unique ones and detect loops
    g_uniqueFramesBuffer.clear();
    cv::Mat firstFrame;
    cv::Mat previousFrame;
    int threshold = 30000;
    bool loopDetected = false;

    for (size_t i = 0; i < g_rawCaptureBuffer.size(); ++i) {
      cv::Mat&currentFrame = g_rawCaptureBuffer[i];

      if (firstFrame.empty()) {
        firstFrame = currentFrame;
        previousFrame = currentFrame;
        g_uniqueFramesBuffer.push_back(currentFrame);
      }
      else {
        // Check difference with previous unique frame
        cv::Mat xorResult;
        cv::bitwise_xor(currentFrame, previousFrame, xorResult);
        int nonZeroPixels = cv::countNonZero(xorResult.reshape(1));

        if (nonZeroPixels > threshold) {
          // Check for loop back to the first frame
          cv::Mat xorFirst;
          cv::bitwise_xor(currentFrame, firstFrame, xorFirst);
          int nonZeroFirst = cv::countNonZero(xorFirst.reshape(1));

          if (nonZeroFirst < threshold) {
            std::cout << "Animation loop detected at frame " << i << "!" << std::endl;
            loopDetected = true;
            break;
          }

          g_uniqueFramesBuffer.push_back(currentFrame);
          previousFrame = currentFrame;
        }
      }
    }

    if (loopDetected) {
      std::cout << "Captured complete animation cycle: " << g_uniqueFramesBuffer.size() << " unique frames." <<
          std::endl;
    }
    else {
      std::cout << "No loop detected. Found " << g_uniqueFramesBuffer.size() << " unique frames." << std::endl;
    }

    if (!g_uniqueFramesBuffer.empty()) {
      std::cout << "Writing unique frames and masks to disk..." << std::endl;
      
      for (size_t i = 0; i < g_uniqueFramesBuffer.size(); ++i) {
        // Use GrabCut for mask generation
        cv::Mat frame = g_uniqueFramesBuffer[i];
        cv::Mat mask = cv::Mat::zeros(frame.size(), CV_8UC1);
        cv::Mat bgModel, fgModel;

        // Initialize with a rectangle (slightly smaller than frame to assume border is background)
        cv::Rect rect(1, 1, frame.cols - 2, frame.rows - 2);
        
        // Run GrabCut (1 iteration is usually enough for a starting mask)
        try {
          cv::grabCut(frame, mask, rect, bgModel, fgModel, 10, cv::GC_INIT_WITH_RECT);
        } catch (const cv::Exception& e) {
          std::cerr << "\nGrabCut error on frame " << i << ": " << e.what() << std::endl;
          mask = cv::Mat::zeros(frame.size(), CV_8UC1);
        }

        // Convert GrabCut mask to binary mask (foreground and probably foreground)
        cv::Mat binaryMask;
        binaryMask = (mask == cv::GC_FGD) | (mask == cv::GC_PR_FGD);
        binaryMask.convertTo(binaryMask, CV_8UC1, 255.0);

        // Enforce convexity using the largest contour's convex hull
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(binaryMask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        if (!contours.empty()) {
          size_t largestIdx = 0;
          double largestArea = 0.0;
          for (size_t c = 0; c < contours.size(); ++c) {
            double area = cv::contourArea(contours[c]);
            if (area > largestArea) {
              largestArea = area;
              largestIdx = c;
            }
          }

          std::vector<cv::Point> hull;
          cv::convexHull(contours[largestIdx], hull);
          binaryMask = cv::Mat::zeros(binaryMask.size(), CV_8UC1);
          std::vector<std::vector<cv::Point>> hulls = { hull };
          cv::drawContours(binaryMask, hulls, 0, cv::Scalar(255), cv::FILLED);
        }
        
        // Save original frame
        std::ostringstream filename;
        filename << g_outputDir << "/frame_" << std::setw(4) << std::setfill('0')
            << i << ".png";
        cv::imwrite(filename.str(), frame);

        // Save mask frame
        std::ostringstream maskFilename;
        maskFilename << g_outputDir << "/mask_" << std::setw(4) << std::setfill('0')
            << i << ".png";
        cv::imwrite(maskFilename.str(), binaryMask);

        if ((i + 1) % 10 == 0 || i == g_uniqueFramesBuffer.size() - 1) {
          std::cout << "\rProgress: " << (i + 1) << "/" << g_uniqueFramesBuffer.size() << std::flush;
        }
      }
      std::cout << std::endl;
    }
  }

  if (wasRecording) {
    g_recordedFrames = (int)g_uniqueFramesBuffer.size();
    std::cout << "Finished! Saved " << g_recordedFrames << " frames." << std::endl;
  }

  // Clear buffers to free memory
  g_rawCaptureBuffer.clear();
  g_rawCaptureBuffer.shrink_to_fit();
  g_uniqueFramesBuffer.clear();
  g_uniqueFramesBuffer.shrink_to_fit();
  g_firstFrame = cv::Mat();
  g_previousFrame = cv::Mat();
}

bool IsRecording() {
  return g_recordingEnabled;
}

int GetRecordedFrameCount() {
  if (g_recordingEnabled) {
    return (int)g_rawCaptureBuffer.size();
  }
  return g_recordedFrames;
}

int GetTotalFramesChecked() {
  return (int)g_rawCaptureBuffer.size();
}

void UpdateRecording(const cv::Mat&gameBoard) {
  if (!g_recordingEnabled || gameBoard.empty()) {
    return;
  }

  // Calculate cell position
  int cellX = g_recordCol * Config::CELL_SIZE;
  int cellY = g_recordRow * Config::CELL_SIZE;

  // Capture full cell
  cv::Rect shapeRoi(cellX, cellY, Config::CELL_SIZE, Config::CELL_SIZE);

  // Check if ROI is valid
  bool roiValid = shapeRoi.x >= 0 && shapeRoi.y >= 0 &&
                  shapeRoi.x + shapeRoi.width <= gameBoard.cols &&
                  shapeRoi.y + shapeRoi.height <= gameBoard.rows;

  if (!roiValid) {
    return;
  }

  // Just capture the frame, no processing
  if (g_rawCaptureBuffer.size() < (size_t)g_maxFrames) {
    g_rawCaptureBuffer.push_back(gameBoard(shapeRoi).clone());

    if (g_rawCaptureBuffer.size() >= (size_t)g_maxFrames) {
      std::cout << "Capture buffer full (" << g_maxFrames << " frames). Stopping capture and starting processing..." <<
          std::endl;
      StopRecording();
    }
  }
}

// ============================================================================
// Grid Detection
// ============================================================================

std::vector<std::vector<Gem>>& DetectGems(const cv::Mat&gameBoard,
                                          Config::ClassificationMode) {
// Static allocation - grid is reused across calls
  static std::vector<std::vector<Gem>> grid(
    Config::GRID_SIZE, std::vector<Gem>(Config::GRID_SIZE));

  static constexpr int TOTAL_TILES = Config::GRID_SIZE * Config::GRID_SIZE;

// Skip cache building when recording for max performance
  if (!g_recordingEnabled && MaskLoader::AreMasksLoaded()) {
    int scaleInt = ShapeParams::refScale.load();
    int blurSize = ShapeParams::refBlur.load();
    cv::Size targetSize(Config::CELL_SIZE, Config::CELL_SIZE);
    MaskLoader::EnsureScaledCache(targetSize, scaleInt, blurSize);
  }

  // Get pointer to static grid for lambda capture
  auto* gridPtr = &grid;

  // Use a persistent thread pool - no thread creation per frame
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

        Gem&gem = (*gridPtr)[row][col];
        gem.gridPos = cv::Point(col, row);
        gem.state = GemState::NORMAL;

        // Calculate cell position
        int cellX = col * Config::CELL_SIZE;
        int cellY = row * Config::CELL_SIZE;

        // Use full cell
        gem.shapeRoi = cv::Rect(cellX, cellY, Config::CELL_SIZE,
                     Config::CELL_SIZE);
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

void PrintGemGrid(const std::vector<std::vector<Gem>>&grid) {
  std::cout << "\nCurrent Gem Grid:" << std::endl;
  for (int row = 0; row < Config::GRID_SIZE; row++) {
    for (int col = 0; col < Config::GRID_SIZE; col++) {
      const Gem&gem = grid[row][col];
      std::cout << "  [" << GemTypeToString(gem.type).substr(0, 1) << "]";
    }
    std::cout << std::endl;
  }
  std::cout << std::endl;
}
