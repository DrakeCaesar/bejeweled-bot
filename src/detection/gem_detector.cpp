#include "gem_detector.h"
#include "assets/loader.h"
#include "shape_classifier.h"
#include "thread_pool.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

// ============================================================================
// Frame Recording for Asset Collection
// ============================================================================

namespace {
// Recording configuration
bool g_recordingEnabled = false;
int g_recordCol = 0;
int g_recordRow = 0;
int g_maxFrames = 0;
int g_recordedFrames = 0;
std::string g_outputDir = "recorded_frames";

// Frame comparison for duplicate detection
cv::Mat g_previousFrame;
cv::Mat g_firstFrame;
int g_totalFramesChecked = 0;
std::vector<cv::Mat> g_rawCaptureBuffer;
std::vector<cv::Mat> g_uniqueFramesBuffer;
std::vector<std::chrono::steady_clock::time_point> g_frameTimestamps;
} // namespace

void StartRecording(int col, int row, int maxFrames,
                    const std::string &outputDir) {
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
  g_frameTimestamps.clear();
  g_frameTimestamps.reserve(g_maxFrames);

  // Create the output directory and clear existing PNGs
  if (std::filesystem::exists(g_outputDir)) {
    for (const auto &entry : std::filesystem::directory_iterator(g_outputDir)) {
      if (entry.path().extension() == ".png") {
        std::filesystem::remove(entry.path());
      }
    }
  } else {
    std::filesystem::create_directories(g_outputDir);
  }

  g_recordingEnabled = true;

  std::cout << "Started recording (capture only): col=" << col << " row=" << row
            << " maxFrames=" << g_maxFrames << " output=" << outputDir
            << std::endl;
  std::cout
      << "Capturing raw frames to memory. Processing will happen after stop."
      << std::endl;
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

    const int blackThreshold = 20;

    size_t animationStart = g_rawCaptureBuffer.size();
    size_t animationEnd = g_rawCaptureBuffer.size();
    bool sawBlack = false;
    int nonBlackCount = 0;

    std::vector<size_t> nonBlackFrames;

    // Find all non-black corner frames
    for (size_t i = 0; i < g_rawCaptureBuffer.size(); ++i) {
      cv::Mat &frame = g_rawCaptureBuffer[i];
      cv::Vec3b cornerPixel = frame.at<cv::Vec3b>(0, 0);
      int brightness = (cornerPixel[0] + cornerPixel[1] + cornerPixel[2]) / 3;
      if (brightness >= blackThreshold) {
        nonBlackFrames.push_back(i);
      }
    }

    std::cout << "Found " << nonBlackFrames.size()
              << " non-black corner frames: ";
    for (size_t idx : nonBlackFrames) {
      std::cout << idx << " ";
    }
    std::cout << std::endl;

    // Start at first non-black corner, end at fourth non-black corner (3 full
    // loops)
    if (!nonBlackFrames.empty()) {
      animationStart = nonBlackFrames[0];

      // Look for fourth non-black corner after seeing black frames (3 full
      // loops)
      for (size_t i = animationStart + 1; i < g_rawCaptureBuffer.size(); ++i) {
        cv::Mat &frame = g_rawCaptureBuffer[i];
        cv::Vec3b cornerPixel = frame.at<cv::Vec3b>(0, 0);
        int brightness = (cornerPixel[0] + cornerPixel[1] + cornerPixel[2]) / 3;
        if (brightness < blackThreshold) {
          sawBlack = true;
        } else if (sawBlack) {
          nonBlackCount++;
          sawBlack = false;
          if (nonBlackCount ==
              3) { // Fourth non-black overall (first was start, 3rd after black
                   // = 4th total = 3 full loops)
            animationEnd = i;
            break;
          }
        }
      }
    }

    if (animationStart >= g_rawCaptureBuffer.size()) {
      std::cout
          << "No non-black corner frames detected. Falling back to full buffer."
          << std::endl;
      animationStart = 0;
      animationEnd = g_rawCaptureBuffer.size();
    } else if (animationEnd >= g_rawCaptureBuffer.size()) {
      std::cout
          << "No end marker detected after black segment. Using frames from "
          << animationStart << " to end." << std::endl;
      animationEnd = g_rawCaptureBuffer.size();
    } else {
      std::cout << "Animation extracted from frame " << animationStart << " to "
                << animationEnd << " (" << (animationEnd - animationStart - 1)
                << " frames, excluding end)" << std::endl;
    }

    // Calculate animation duration and required framerate
    if (animationEnd > animationStart &&
        animationEnd - 1 < g_frameTimestamps.size()) {
      size_t capturedFrameCount =
          animationEnd - animationStart - 1; // Exclude last frame

      // Calculate actual duration from timestamps
      auto startTime = g_frameTimestamps[animationStart];
      auto endTime = g_frameTimestamps[animationEnd - 1];
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          endTime - startTime);
      float animationDurationMs = static_cast<float>(duration.count());

      float requiredFramerate = 20000.0f / animationDurationMs;

      std::cout << "Animation duration: " << std::fixed << std::setprecision(1)
                << animationDurationMs << " ms (" << capturedFrameCount
                << " captured frames)" << std::endl;
      std::cout << "Required game FPS to display 20 frames in this duration: "
                << std::fixed << std::setprecision(2) << requiredFramerate
                << " FPS" << std::endl;
    }

    // Process extracted range to find unique frames
    g_uniqueFramesBuffer.clear();
    cv::Mat previousFrame;
    int threshold = 0;

    for (size_t i = animationStart; i < animationEnd; ++i) {
      cv::Mat &currentFrame = g_rawCaptureBuffer[i];

      if (previousFrame.empty()) {
        previousFrame = currentFrame;
        g_uniqueFramesBuffer.push_back(currentFrame);
      } else {
        // Check difference with previous unique frame (using whole image)
        cv::Mat xorResult;
        cv::bitwise_xor(currentFrame, previousFrame, xorResult);
        int nonZeroPixels = cv::countNonZero(xorResult.reshape(1));

        if (nonZeroPixels > threshold) {
          g_uniqueFramesBuffer.push_back(currentFrame);
          previousFrame = currentFrame;
        }
      }
    }

    std::cout << "Found " << g_uniqueFramesBuffer.size() << " unique frames."
              << std::endl;

    // Remove all duplicate base frames (from loop restarts) BEFORE subsampling
    if (g_uniqueFramesBuffer.size() > 1) {
      cv::Mat &firstFrame = g_uniqueFramesBuffer[0];
      for (size_t i = g_uniqueFramesBuffer.size() - 1; i >= 1; --i) {
        cv::Mat diff;
        cv::absdiff(g_uniqueFramesBuffer[i], firstFrame, diff);
        int nonZeroPixels = cv::countNonZero(diff.reshape(1));

        if (nonZeroPixels == 0) {
          // Found duplicate base frame, remove it
          g_uniqueFramesBuffer.erase(g_uniqueFramesBuffer.begin() + i);
          std::cout << "Removed duplicate base frame at position " << i
                    << std::endl;
        }
      }
      std::cout << g_uniqueFramesBuffer.size()
                << " frames remaining after removing loop markers."
                << std::endl;
    }

    // Keep only every 5th frame (0, 5, 10, 15, ...)
    if (g_uniqueFramesBuffer.size() > 1) {
      std::vector<cv::Mat> subsampledFrames;
      for (size_t i = 0; i < g_uniqueFramesBuffer.size(); i += 5) {
        subsampledFrames.push_back(g_uniqueFramesBuffer[i]);
      }
      std::cout << "After keeping every 5th frame: " << subsampledFrames.size()
                << " frames." << std::endl;
      g_uniqueFramesBuffer = std::move(subsampledFrames);
    }

    // Remove the first frame (base frame) after subsampling
    if (!g_uniqueFramesBuffer.empty()) {
      g_uniqueFramesBuffer.erase(g_uniqueFramesBuffer.begin());
      std::cout << "Removed first frame (base frame), "
                << g_uniqueFramesBuffer.size() << " frames remaining."
                << std::endl;
    }

    // Second pass: remove duplicate frames across the entire set
    if (g_uniqueFramesBuffer.size() > 1) {
      std::vector<cv::Mat> trulyUniqueFrames;
      trulyUniqueFrames.push_back(g_uniqueFramesBuffer[0]);

      for (size_t i = 1; i < g_uniqueFramesBuffer.size(); ++i) {
        bool isDuplicate = false;

        for (size_t j = 0; j < trulyUniqueFrames.size(); ++j) {
          cv::Mat diff;
          cv::absdiff(g_uniqueFramesBuffer[i], trulyUniqueFrames[j], diff);
          int nonZeroPixels = cv::countNonZero(diff.reshape(1));

          if (nonZeroPixels == 0) {
            isDuplicate = true;
            break;
          }
        }

        if (!isDuplicate) {
          trulyUniqueFrames.push_back(g_uniqueFramesBuffer[i]);
        }
      }

      std::cout << "After removing duplicates: " << trulyUniqueFrames.size()
                << " truly unique frames." << std::endl;
      g_uniqueFramesBuffer = std::move(trulyUniqueFrames);
    }

    if (!g_uniqueFramesBuffer.empty()) {
      std::cout << "Writing unique frames to disk..." << std::endl;

      for (size_t i = 0; i < g_uniqueFramesBuffer.size(); ++i) {
        // Save raw frame
        std::ostringstream filename;
        filename << g_outputDir << "/frame_" << std::setw(4)
                 << std::setfill('0') << i << ".png";
        cv::imwrite(filename.str(), g_uniqueFramesBuffer[i]);

        if ((i + 1) % 10 == 0 || i == g_uniqueFramesBuffer.size() - 1) {
          std::cout << "\rProgress: " << (i + 1) << "/"
                    << g_uniqueFramesBuffer.size() << std::flush;
        }
      }
      std::cout << std::endl;
    }
  }

  if (wasRecording) {
    g_recordedFrames = (int)g_uniqueFramesBuffer.size();
    std::cout << "Finished! Saved " << g_recordedFrames << " frames."
              << std::endl;
  }

  // Clear buffers to free memory
  g_rawCaptureBuffer.clear();
  g_rawCaptureBuffer.shrink_to_fit();
  g_uniqueFramesBuffer.clear();
  g_uniqueFramesBuffer.shrink_to_fit();
  g_firstFrame = cv::Mat();
  g_previousFrame = cv::Mat();
}

bool IsRecording() { return g_recordingEnabled; }

int GetRecordedFrameCount() {
  if (g_recordingEnabled) {
    return (int)g_rawCaptureBuffer.size();
  }
  return g_recordedFrames;
}

int GetTotalFramesChecked() { return (int)g_rawCaptureBuffer.size(); }

void UpdateRecording(const cv::Mat &gameBoard) {
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
    g_frameTimestamps.push_back(std::chrono::steady_clock::now());

    if (g_rawCaptureBuffer.size() >= (size_t)g_maxFrames) {
      std::cout << "Capture buffer full (" << g_maxFrames
                << " frames). Stopping capture and starting processing..."
                << std::endl;
      StopRecording();
    }
  }
}

// ============================================================================
// Grid Detection
// ============================================================================

std::vector<std::vector<Gem>> &DetectGems(const cv::Mat &gameBoard,
                                          bool needDebugMap) {
  // Static allocation - grid is reused across calls
  static std::vector<std::vector<Gem>> grid(
      Config::GRID_SIZE, std::vector<Gem>(Config::GRID_SIZE));

  static constexpr int TOTAL_TILES = Config::GRID_SIZE * Config::GRID_SIZE;

  // Get pointer to static grid for lambda capture
  auto *gridPtr = &grid;

  // Atomic counters for dynamic tile distribution and ring stat accumulation
  std::atomic<int> nextTile(0);
  std::atomic<int> atomicRingVSum(0);
  std::atomic<int> atomicNormalCount(0);

  struct GemDetectWork {
    const cv::Mat &gameBoard;
    std::vector<std::vector<Gem>> *gridPtr;
    std::atomic<int> &nextTile;
    std::atomic<int> &ringVSum;
    std::atomic<int> &normalCount;
    bool needDebugMap;

    void operator()(int threadIndex, int numThreads) {
      (void)threadIndex;
      (void)numThreads;

      int idx;
      while ((idx = nextTile.fetch_add(1)) < TOTAL_TILES) {
        int row = idx / Config::GRID_SIZE;
        int col = idx % Config::GRID_SIZE;

        Gem &gem = (*gridPtr)[row][col];
        gem.gridPos = cv::Point(col, row);
        gem.state = GemState::NORMAL;
        gem.ringValue = -1;
        gem.ringHSV = cv::Vec3i(-1, -1, -1);

        // Calculate cell position
        int cellX = col * Config::CELL_SIZE;
        int cellY = row * Config::CELL_SIZE;

        // Use full cell
        gem.shapeRoi =
            cv::Rect(cellX, cellY, Config::CELL_SIZE, Config::CELL_SIZE);
        gem.roi = gem.shapeRoi;

        // Check if ROI is valid
        bool roiValid = gem.shapeRoi.x >= 0 && gem.shapeRoi.y >= 0 &&
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

        // Classify gem
        cv::Mat processed = ProcessGemForShapeMatching(shapeRegion);
        cv::Mat processedLab;
        cv::cvtColor(processed, processedLab, cv::COLOR_BGR2Lab);
        gem.type = ClassifyGemByShape(processed, processedLab, row, col,
                                      &gem.bestMatch, &gem.bestMatchFrame,
                                      needDebugMap);
        gem.shapeMask = GetDebugVisualization(shapeRegion);
        gem.closestType = GemType::UNKNOWN;
        gem.matchedThreshold = (gem.type != GemType::UNKNOWN);

        // Compute ring HSV V value for flame/lightning state debug
        if (gem.type != GemType::UNKNOWN && gem.type != GemType::EMPTY &&
            MaskLoader::AreMasksLoaded()) {
          int frameIndex = (gem.bestMatchFrame >= 0) ? gem.bestMatchFrame : 0;
          const cv::Mat &ringMask =
              MaskLoader::GetRingMask(gem.type, frameIndex);

          if (!ringMask.empty()) {
            cv::Mat mask = ringMask;
            if (mask.size() != shapeRegion.size()) {
              cv::resize(ringMask, mask, shapeRegion.size(), 0, 0,
                         cv::INTER_NEAREST);
            }

            if (cv::countNonZero(mask) > 0) {
              cv::Scalar meanBgr = cv::mean(shapeRegion, mask);
              cv::Mat tmpBgr(1, 1, CV_8UC3);
              tmpBgr.at<cv::Vec3b>(0, 0) =
                  cv::Vec3b(cv::saturate_cast<uchar>(meanBgr[0]),
                            cv::saturate_cast<uchar>(meanBgr[1]),
                            cv::saturate_cast<uchar>(meanBgr[2]));
              cv::Mat tmpHsv;
              cv::cvtColor(tmpBgr, tmpHsv, cv::COLOR_BGR2HSV);
              cv::Vec3b hsvVal = tmpHsv.at<cv::Vec3b>(0, 0);
              gem.ringValue = hsvVal[2];
              gem.ringHSV = cv::Vec3i(hsvVal[0], hsvVal[1], hsvVal[2]);
            }
          }
        }

        // Accumulate ring stats atomically for avgRingV computation
        if (gem.type != GemType::UNKNOWN && gem.type != GemType::EMPTY &&
            gem.type != GemType::HYPERCUBE && gem.ringValue >= 0) {
          ringVSum.fetch_add(gem.ringValue, std::memory_order_relaxed);
          normalCount.fetch_add(1, std::memory_order_relaxed);
        }
      }
    }
  };

  // Use a persistent thread pool with dynamic work distribution
  GemDetectWork work{gameBoard,      gridPtr,           nextTile,
                     atomicRingVSum, atomicNormalCount, needDebugMap};
  StaticThreadPool::Instance().ParallelFor(work);

  // Compute avgRingV from atomically-accumulated stats (no sequential scan)
  int normalGemCount = atomicNormalCount.load(std::memory_order_relaxed);
  int avgRingV =
      (normalGemCount > 0)
          ? (atomicRingVSum.load(std::memory_order_relaxed) / normalGemCount)
          : 0;

  // Detect flame and star gems
  for (int row = 0; row < Config::GRID_SIZE; row++) {
    for (int col = 0; col < Config::GRID_SIZE; col++) {
      Gem &gem = grid[row][col];
      if (gem.type != GemType::UNKNOWN && gem.type != GemType::EMPTY &&
          gem.type != GemType::HYPERCUBE && gem.ringHSV[0] >= 0) {
        int H = gem.ringHSV[0];
        int V = gem.ringHSV[2];
        int minVThreshold = avgRingV * 2;

        // Check for flame: H < 50 and V > 190 and V >= 2 * avgRingV
        if (H < 50 && V > 190 && V >= minVThreshold) {
          gem.state = GemState::FLAME;
        }
        // Check for star: H > 70 and V > 190 and V >= 2 * avgRingV
        else if (H > 70 && V > 190 && V >= minVThreshold) {
          gem.state = GemState::STAR;
        } else {
          gem.state = GemState::NORMAL;
        }
      }
    }
  }

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
