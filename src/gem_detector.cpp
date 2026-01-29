#include "gem_detector.h"
#include "config.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <thread>
#include <vector>

std::string GemTypeToString(GemType type)
{
  switch (type)
  {
  case GemType::RED:
    return "RED";
  case GemType::ORANGE:
    return "ORANGE";
  case GemType::YELLOW:
    return "YELLOW";
  case GemType::GREEN:
    return "GREEN";
  case GemType::BLUE:
    return "BLUE";
  case GemType::PURPLE:
    return "PURPLE";
  case GemType::WHITE:
    return "WHITE";
  case GemType::EMPTY:
    return "EMPTY";
  default:
    return "UNKNOWN";
  }
}

cv::Scalar GemTypeToColor(GemType type)
{
  switch (type)
  {
  case GemType::RED:
    return cv::Scalar(0, 0, 255);
  case GemType::ORANGE:
    return cv::Scalar(0, 165, 255);
  case GemType::YELLOW:
    return cv::Scalar(0, 255, 255);
  case GemType::GREEN:
    return cv::Scalar(0, 255, 0);
  case GemType::BLUE:
    return cv::Scalar(255, 0, 0);
  case GemType::PURPLE:
    return cv::Scalar(255, 0, 255);
  case GemType::WHITE:
    return cv::Scalar(255, 255, 255);
  default:
    return cv::Scalar(128, 128, 128);
  }
}

GemType ClassifyGemByColor(const cv::Mat &gemRegion)
{
  if (gemRegion.empty())
  {
    return GemType::UNKNOWN;
  }

  // Convert to HSV for better color classification
  cv::Mat hsv;
  cv::cvtColor(gemRegion, hsv, cv::COLOR_BGR2HSV);

  // Calculate the percentage of pixels matching each color range
  auto calculateColorMatch = [&hsv](const cv::Scalar &minHSV, const cv::Scalar &maxHSV) -> float
  {
    cv::Mat mask;
    cv::inRange(hsv, minHSV, maxHSV, mask);
    int matchingPixels = cv::countNonZero(mask);
    return (float)matchingPixels / (hsv.rows * hsv.cols);
  };

  // Check each gem color
  float redMatch = calculateColorMatch(Config::RED_GEM_HSV_MIN, Config::RED_GEM_HSV_MAX);
  float orangeMatch = calculateColorMatch(Config::ORANGE_GEM_HSV_MIN, Config::ORANGE_GEM_HSV_MAX);
  float yellowMatch = calculateColorMatch(Config::YELLOW_GEM_HSV_MIN, Config::YELLOW_GEM_HSV_MAX);
  float greenMatch = calculateColorMatch(Config::GREEN_GEM_HSV_MIN, Config::GREEN_GEM_HSV_MAX);
  float blueMatch = calculateColorMatch(Config::BLUE_GEM_HSV_MIN, Config::BLUE_GEM_HSV_MAX);
  float purpleMatch = calculateColorMatch(Config::PURPLE_GEM_HSV_MIN, Config::PURPLE_GEM_HSV_MAX);
  float whiteMatch = calculateColorMatch(Config::WHITE_GEM_HSV_MIN, Config::WHITE_GEM_HSV_MAX);

  // Find the best match
  float maxMatch = 0.1f; // Minimum threshold
  GemType bestType = GemType::UNKNOWN;

  if (redMatch > maxMatch)
  {
    maxMatch = redMatch;
    bestType = GemType::RED;
  }
  if (orangeMatch > maxMatch)
  {
    maxMatch = orangeMatch;
    bestType = GemType::ORANGE;
  }
  if (yellowMatch > maxMatch)
  {
    maxMatch = yellowMatch;
    bestType = GemType::YELLOW;
  }
  if (greenMatch > maxMatch)
  {
    maxMatch = greenMatch;
    bestType = GemType::GREEN;
  }
  if (blueMatch > maxMatch)
  {
    maxMatch = blueMatch;
    bestType = GemType::BLUE;
  }
  if (purpleMatch > maxMatch)
  {
    maxMatch = purpleMatch;
    bestType = GemType::PURPLE;
  }
  if (whiteMatch > maxMatch)
  {
    maxMatch = whiteMatch;
    bestType = GemType::WHITE;
  }

  return bestType;
}

bool IsGemMoving(const cv::Mat &prevFrame, const cv::Mat &currFrame, const cv::Rect &roi)
{
  if (prevFrame.empty() || currFrame.empty())
  {
    return false;
  }

  // Ensure ROI is within bounds
  cv::Rect safeRoi = roi & cv::Rect(0, 0, currFrame.cols, currFrame.rows);
  if (safeRoi.width <= 0 || safeRoi.height <= 0)
  {
    return false;
  }

  cv::Mat prevRegion = prevFrame(safeRoi);
  cv::Mat currRegion = currFrame(safeRoi);

  // Calculate absolute difference
  cv::Mat diff;
  cv::absdiff(prevRegion, currRegion, diff);

  // Convert to grayscale and calculate mean difference
  cv::Mat grayDiff;
  cv::cvtColor(diff, grayDiff, cv::COLOR_BGR2GRAY);

  cv::Scalar meanDiff = cv::mean(grayDiff);

  return meanDiff[0] > Config::MOTION_THRESHOLD;
}

std::vector<std::vector<Gem>> DetectGems(const cv::Mat &gameBoard, const cv::Mat &prevBoard)
{
  std::vector<std::vector<Gem>> grid(Config::GRID_SIZE, std::vector<Gem>(Config::GRID_SIZE));

  // Lambda to process a chunk of tiles
  auto processTiles = [&](int startIdx, int endIdx)
  {
    for (int idx = startIdx; idx < endIdx; idx++)
    {
      int row = idx / Config::GRID_SIZE;
      int col = idx % Config::GRID_SIZE;

      Gem &gem = grid[row][col];
      gem.gridPos = cv::Point(col, row);
      gem.state = GemState::NORMAL;

      // Calculate cell center
      int cellX = col * Config::CELL_SIZE;
      int cellY = row * Config::CELL_SIZE;
      int centerX = cellX + (Config::CELL_SIZE - Config::GEM_CENTER_SIZE) / 2;
      int centerY = cellY + (Config::CELL_SIZE - Config::GEM_CENTER_SIZE) / 2;

      // Define ROI for gem center
      gem.roi = cv::Rect(centerX, centerY, Config::GEM_CENTER_SIZE, Config::GEM_CENTER_SIZE);

      // Check if ROI is valid
      if (gem.roi.x + gem.roi.width > gameBoard.cols ||
          gem.roi.y + gem.roi.height > gameBoard.rows)
      {
        gem.type = GemType::UNKNOWN;
        gem.isMoving = false;
        continue;
      }

      // Check for motion
      gem.isMoving = IsGemMoving(prevBoard, gameBoard, gem.roi);

      // Only classify if gem is not moving
      if (!gem.isMoving)
      {
        cv::Mat gemRegion = gameBoard(gem.roi);
        gem.type = ClassifyGemByColor(gemRegion);
        gem.avgColor = cv::mean(gemRegion);
      }
      else
      {
        gem.type = GemType::UNKNOWN;
      }
    }
  };

  // Use 8 threads to process 8 tiles each (64 total tiles)
  const int NUM_THREADS = 4;
  const int TOTAL_TILES = Config::GRID_SIZE * Config::GRID_SIZE;
  const int TILES_PER_THREAD = TOTAL_TILES / NUM_THREADS;

  std::vector<std::thread> threads;
  threads.reserve(NUM_THREADS);

  for (int t = 0; t < NUM_THREADS; t++)
  {
    int startIdx = t * TILES_PER_THREAD;
    int endIdx = (t == NUM_THREADS - 1) ? TOTAL_TILES : (t + 1) * TILES_PER_THREAD;
    threads.emplace_back(processTiles, startIdx, endIdx);
  }

  // Wait for all threads to complete
  for (auto &thread : threads)
  {
    thread.join();
  }

  return grid;
}

void DrawDebugOverlay(cv::Mat &image, const std::vector<std::vector<Gem>> &grid)
{
  // Draw grid lines with offset for banner
  for (int i = 0; i <= Config::GRID_SIZE; i++)
  {
    int pos = i * Config::CELL_SIZE;
    cv::line(image, cv::Point(pos, Config::BANNER_HEIGHT), cv::Point(pos, image.rows),
             cv::Scalar(100, 100, 100), 1);
    cv::line(image, cv::Point(0, Config::BANNER_HEIGHT + pos), cv::Point(image.cols, Config::BANNER_HEIGHT + pos),
             cv::Scalar(100, 100, 100), 1);
  }

  // Draw gem information with offset for banner
  for (int row = 0; row < Config::GRID_SIZE; row++)
  {
    for (int col = 0; col < Config::GRID_SIZE; col++)
    {
      const Gem &gem = grid[row][col];

      // Draw center ROI with offset
      cv::Rect offsetRoi = gem.roi;
      offsetRoi.y += Config::BANNER_HEIGHT;
      cv::rectangle(image, offsetRoi,
                    gem.isMoving ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 255, 0),
                    2);

      // Draw gem type label with offset
      if (!gem.isMoving && gem.type != GemType::UNKNOWN)
      {
        cv::Point labelPos(col * Config::CELL_SIZE + 5, Config::BANNER_HEIGHT + row * Config::CELL_SIZE + 20);
        cv::putText(image, GemTypeToString(gem.type), labelPos,
                    cv::FONT_HERSHEY_SIMPLEX, 0.4,
                    GemTypeToColor(gem.type), 1);
      }
      else if (gem.isMoving)
      {
        cv::Point labelPos(col * Config::CELL_SIZE + 5, Config::BANNER_HEIGHT + row * Config::CELL_SIZE + 20);
        cv::putText(image, "MOVING", labelPos,
                    cv::FONT_HERSHEY_SIMPLEX, 0.4,
                    cv::Scalar(0, 0, 255), 1);
      }
    }
  }
}

void PrintGemGrid(const std::vector<std::vector<Gem>> &grid)
{
  std::cout << "\nCurrent Gem Grid:" << std::endl;
  for (int row = 0; row < Config::GRID_SIZE; row++)
  {
    for (int col = 0; col < Config::GRID_SIZE; col++)
    {
      const Gem &gem = grid[row][col];
      if (gem.isMoving)
      {
        std::cout << "  [M]";
      }
      else
      {
        std::cout << "  [" << GemTypeToString(gem.type).substr(0, 1) << "]";
      }
    }
    std::cout << std::endl;
  }
  std::cout << std::endl;
}

void DrawBanner(cv::Mat &image, float fps, double captureMs, double detectMs)
{
  // Draw banner background
  cv::rectangle(image, cv::Rect(0, 0, image.cols, Config::BANNER_HEIGHT),
                Config::BANNER_BG_COLOR, -1);

  // Create timing text
  std::ostringstream oss;
  oss << "FPS: " << std::fixed << std::setprecision(1) << fps
      << " | Capture: " << std::setprecision(2) << captureMs << "ms"
      << " | Detect: " << detectMs << "ms";
  std::string bannerText = oss.str();

  // Get text size for positioning
  int fontFace = cv::FONT_HERSHEY_SIMPLEX;
  double fontScale = 0.5;
  int thickness = 1;
  int baseline = 0;
  cv::Size textSize = cv::getTextSize(bannerText, fontFace, fontScale, thickness, &baseline);

  // Draw text in banner
  cv::putText(image, bannerText,
              cv::Point(10, Config::BANNER_HEIGHT / 2 + textSize.height / 2),
              fontFace, fontScale, Config::BANNER_TEXT_COLOR, thickness);

  // Draw separator line below banner
  cv::line(image, cv::Point(0, Config::BANNER_HEIGHT),
           cv::Point(image.cols, Config::BANNER_HEIGHT),
           cv::Scalar(100, 100, 100), 1);
}
