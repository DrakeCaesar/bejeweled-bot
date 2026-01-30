#include "gem_detector.h"
#include "config.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <thread>
#include <vector>
#include <opencv2/freetype.hpp>

// Static freetype font instance
static cv::Ptr<cv::freetype::FreeType2> ft2;
static bool freetypeInitialized = false;

static void InitializeFreeType()
{
  if (freetypeInitialized)
    return;

  try
  {
    ft2 = cv::freetype::createFreeType2();

    // Try Fira Code Nerd Font first, then common monospace fallbacks
    std::vector<std::string> fontPaths = {
        "C:/Windows/Fonts/FiraCodeNerdFont-Regular.ttf",
        "C:/Windows/Fonts/FiraCodeNerdFontMono-Regular.ttf",
        "C:/Windows/Fonts/consola.ttf", // Consolas
        "C:/Windows/Fonts/cour.ttf",    // Courier New
    };

    for (const auto &path : fontPaths)
    {
      try
      {
        ft2->loadFontData(path, 0);
        freetypeInitialized = true;
        return;
      }
      catch (...)
      {
        continue;
      }
    }
  }
  catch (...)
  {
    // FreeType not available, will use built-in font
  }
}

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

namespace
{
  float HueDistance(float h1, float h2)
  {
    float diff = std::fabs(h1 - h2);
    return std::min(diff, 180.0f - diff);
  }

  std::string FormatRangeLine(const std::string &label, const cv::Scalar &minHSV, const cv::Scalar &maxHSV)
  {
    std::ostringstream oss;
    oss << label
        << " H[" << (int)minHSV[0] << "-" << (int)maxHSV[0] << "]"
        << " S[" << (int)minHSV[1] << "-" << (int)maxHSV[1] << "]"
        << " V[" << (int)minHSV[2] << "-" << (int)maxHSV[2] << "]";
    return oss.str();
  }

  std::string FormatRedRangeLine()
  {
    std::ostringstream oss;
    oss << "RED H[" << (int)Config::RED_GEM_HSV_MIN_1[0] << "-" << (int)Config::RED_GEM_HSV_MAX_1[0]
        << "," << (int)Config::RED_GEM_HSV_MIN_2[0] << "-" << (int)Config::RED_GEM_HSV_MAX_2[0] << "]"
        << " S[" << (int)Config::RED_GEM_HSV_MIN_1[1] << "-" << (int)Config::RED_GEM_HSV_MAX_1[1] << "]"
        << " V[" << (int)Config::RED_GEM_HSV_MIN_1[2] << "-" << (int)Config::RED_GEM_HSV_MAX_1[2] << "]";
    return oss.str();
  }
}

GemType ClassifyGemByColor(const cv::Mat &gemRegion, float *outBestMatch, GemType *outClosestType, Config::ClassificationMode mode)
{
  if (gemRegion.empty())
  {
    if (outBestMatch)
      *outBestMatch = 0.0f;
    if (outClosestType)
      *outClosestType = GemType::UNKNOWN;
    return GemType::UNKNOWN;
  }

  // If shape mode is enabled, return UNKNOWN (placeholder for future shape classification)
  if (mode == Config::ClassificationMode::SHAPE)
  {
    if (outBestMatch)
      *outBestMatch = 0.0f;
    if (outClosestType)
      *outClosestType = GemType::UNKNOWN;
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

  auto calculateColorMatchUnion = [&hsv](const cv::Scalar &minHSV1, const cv::Scalar &maxHSV1,
                                         const cv::Scalar &minHSV2, const cv::Scalar &maxHSV2) -> float
  {
    cv::Mat mask1, mask2, mask;
    cv::inRange(hsv, minHSV1, maxHSV1, mask1);
    cv::inRange(hsv, minHSV2, maxHSV2, mask2);
    cv::bitwise_or(mask1, mask2, mask);
    int matchingPixels = cv::countNonZero(mask);
    return (float)matchingPixels / (hsv.rows * hsv.cols);
  };

  // Check each gem color
  float redMatch = calculateColorMatchUnion(Config::RED_GEM_HSV_MIN_1, Config::RED_GEM_HSV_MAX_1,
                                            Config::RED_GEM_HSV_MIN_2, Config::RED_GEM_HSV_MAX_2);
  float orangeMatch = calculateColorMatch(Config::ORANGE_GEM_HSV_MIN, Config::ORANGE_GEM_HSV_MAX);
  float yellowMatch = calculateColorMatch(Config::YELLOW_GEM_HSV_MIN, Config::YELLOW_GEM_HSV_MAX);
  float greenMatch = calculateColorMatch(Config::GREEN_GEM_HSV_MIN, Config::GREEN_GEM_HSV_MAX);
  float blueMatch = calculateColorMatch(Config::BLUE_GEM_HSV_MIN, Config::BLUE_GEM_HSV_MAX);
  float purpleMatch = calculateColorMatch(Config::PURPLE_GEM_HSV_MIN, Config::PURPLE_GEM_HSV_MAX);
  float whiteMatch = calculateColorMatch(Config::WHITE_GEM_HSV_MIN, Config::WHITE_GEM_HSV_MAX);

  // Find the best match
  float maxMatch = 0.0f;
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

  if (outBestMatch)
    *outBestMatch = maxMatch;

  struct HueTarget
  {
    GemType type;
    float hue;
  };

  // Determine closest hue match for debug display
  GemType closestType = bestType;
  if (maxMatch < Config::COLOR_MATCH_THRESHOLD)
  {
    cv::Scalar meanHSV = cv::mean(hsv);
    float meanH = static_cast<float>(meanHSV[0]);
    float meanS = static_cast<float>(meanHSV[1]);
    float meanV = static_cast<float>(meanHSV[2]);

    if (meanS <= Config::WHITE_GEM_HSV_MAX[1] && meanV >= Config::WHITE_GEM_HSV_MIN[2])
    {
      closestType = GemType::WHITE;
    }
    else
    {
      std::vector<HueTarget> targets;

      HueTarget red;
      red.type = GemType::RED;
      red.hue = 0.0f;
      targets.push_back(red);

      HueTarget orange;
      orange.type = GemType::ORANGE;
      orange.hue = static_cast<float>((Config::ORANGE_GEM_HSV_MIN[0] + Config::ORANGE_GEM_HSV_MAX[0]) / 2.0);
      targets.push_back(orange);

      HueTarget yellow;
      yellow.type = GemType::YELLOW;
      yellow.hue = static_cast<float>((Config::YELLOW_GEM_HSV_MIN[0] + Config::YELLOW_GEM_HSV_MAX[0]) / 2.0);
      targets.push_back(yellow);

      HueTarget green;
      green.type = GemType::GREEN;
      green.hue = static_cast<float>((Config::GREEN_GEM_HSV_MIN[0] + Config::GREEN_GEM_HSV_MAX[0]) / 2.0);
      targets.push_back(green);

      HueTarget blue;
      blue.type = GemType::BLUE;
      blue.hue = static_cast<float>((Config::BLUE_GEM_HSV_MIN[0] + Config::BLUE_GEM_HSV_MAX[0]) / 2.0);
      targets.push_back(blue);

      HueTarget purple;
      purple.type = GemType::PURPLE;
      purple.hue = static_cast<float>((Config::PURPLE_GEM_HSV_MIN[0] + Config::PURPLE_GEM_HSV_MAX[0]) / 2.0);
      targets.push_back(purple);

      float bestDist = 1e9f;
      for (const auto &t : targets)
      {
        float dist = HueDistance(meanH, t.hue);
        if (dist < bestDist)
        {
          bestDist = dist;
          closestType = t.type;
        }
      }
    }
  }

  if (outClosestType)
    *outClosestType = closestType;

  if (maxMatch < Config::COLOR_MATCH_THRESHOLD)
  {
    return GemType::UNKNOWN;
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

std::vector<std::vector<Gem>> DetectGems(const cv::Mat &gameBoard, const cv::Mat &prevBoard, Config::ClassificationMode mode)
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
        gem.closestType = GemType::UNKNOWN;
        gem.bestMatch = 0.0f;
        gem.matchedThreshold = false;
        gem.isMoving = false;
        continue;
      }

      // Check for motion
      gem.isMoving = IsGemMoving(prevBoard, gameBoard, gem.roi);

      // Only classify if gem is not moving
      if (!gem.isMoving)
      {
        cv::Mat gemRegion = gameBoard(gem.roi);
        gem.type = ClassifyGemByColor(gemRegion, &gem.bestMatch, &gem.closestType, mode);
        gem.matchedThreshold = (gem.type != GemType::UNKNOWN);
        gem.avgColor = cv::mean(gemRegion);
      }
      else
      {
        gem.type = GemType::UNKNOWN;
        gem.closestType = GemType::UNKNOWN;
        gem.bestMatch = 0.0f;
        gem.matchedThreshold = false;
      }
    }
  };

  const int NUM_THREADS = 8;
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
      if (!gem.isMoving)
      {
        int textX = col * Config::CELL_SIZE + 5;
        int textY = Config::BANNER_HEIGHT + row * Config::CELL_SIZE + 18;
        int lineHeight = 16;

        // Get HSV value for this gem
        cv::Scalar meanBGR = gem.avgColor;
        cv::Mat tmpBGR(1, 1, CV_8UC3, meanBGR);
        cv::Mat tmpHSV;
        cv::cvtColor(tmpBGR, tmpHSV, cv::COLOR_BGR2HSV);
        cv::Vec3b hsvVal = tmpHSV.at<cv::Vec3b>(0, 0);

        // Prepare text lines
        std::vector<std::string> lines;
        cv::Scalar textColor;

        if (gem.matchedThreshold && gem.type != GemType::UNKNOWN)
        {
          lines.push_back(GemTypeToString(gem.type));
          std::ostringstream line2;
          line2 << std::fixed << std::setprecision(0) << (gem.bestMatch * 100.0f) << "%";
          lines.push_back(line2.str());
          textColor = GemTypeToColor(gem.type);
        }
        else
        {
          lines.push_back("CLOSE:");
          lines.push_back(GemTypeToString(gem.closestType));
          std::ostringstream line3;
          line3 << std::fixed << std::setprecision(0) << (gem.bestMatch * 100.0f) << "%";
          lines.push_back(line3.str());
          textColor = (gem.closestType != GemType::UNKNOWN) ? GemTypeToColor(gem.closestType) : cv::Scalar(160, 160, 160);
        }

        std::ostringstream hsvLine;
        hsvLine << "H" << (int)hsvVal[0] << " S" << (int)hsvVal[1];
        lines.push_back(hsvLine.str());
        std::ostringstream vLine;
        vLine << "V" << (int)hsvVal[2];
        lines.push_back(vLine.str());

        // Calculate background size
        int maxWidth = 0;
        int fontFace = cv::FONT_HERSHEY_SIMPLEX;
        double fontScale = 0.45;
        int thickness = 1;
        int baseline = 0;

        for (const auto &line : lines)
        {
          cv::Size textSize = cv::getTextSize(line, fontFace, fontScale, thickness, &baseline);
          maxWidth = std::max(maxWidth, textSize.width);
        }

        int bgPadding = 3;
        cv::Rect bgRect(textX - bgPadding, textY - 12, maxWidth + bgPadding * 2,
                        static_cast<int>(lines.size()) * lineHeight + bgPadding * 2);
        cv::rectangle(image, bgRect, cv::Scalar(20, 20, 20), -1);
        cv::rectangle(image, bgRect, cv::Scalar(60, 60, 60), 1);

        // Draw text lines with anti-aliasing
        for (size_t i = 0; i < lines.size(); ++i)
        {
          cv::Point pos(textX, textY + static_cast<int>(i) * lineHeight);
          cv::putText(image, lines[i], pos, fontFace, fontScale, textColor, thickness, cv::LINE_AA);
        }
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

void DrawBanner(cv::Mat &image, float fps, double captureMs, double detectMs, double drawMs, double displayMs,
                Config::ClassificationMode classificationMode,
                int fpsWidth, int captureWidth, int detectWidth, int drawWidth, int displayWidth)
{
  // Initialize FreeType on first call
  InitializeFreeType();

  // Draw banner background
  cv::rectangle(image, cv::Rect(0, 0, image.cols, Config::BANNER_HEIGHT),
                Config::BANNER_BG_COLOR, -1);

  // Create timing text with padding
  std::ostringstream oss;
  oss << "FPS: " << std::fixed << std::setprecision(1) << std::setw(fpsWidth) << fps
      << " | Capture: " << std::setprecision(2) << std::setw(captureWidth) << captureMs << "ms"
      << " | Detect: " << std::setw(detectWidth) << detectMs << "ms"
      << " | Draw: " << std::setw(drawWidth) << drawMs << "ms"
      << " | Display: " << std::setw(displayWidth) << displayMs << "ms"
      << " | Mode: " << (classificationMode == Config::ClassificationMode::COLOR ? "COLOR (c)" : "SHAPE (c)");
  std::string bannerText = oss.str();

  // Draw HSV range legend in banner (only if in COLOR mode)
  std::vector<std::string> hsvLines;
  if (classificationMode == Config::ClassificationMode::COLOR)
  {
    hsvLines.push_back(FormatRedRangeLine());
    hsvLines.push_back(FormatRangeLine("ORANGE", Config::ORANGE_GEM_HSV_MIN, Config::ORANGE_GEM_HSV_MAX));
    hsvLines.push_back(FormatRangeLine("YELLOW", Config::YELLOW_GEM_HSV_MIN, Config::YELLOW_GEM_HSV_MAX));
    hsvLines.push_back(FormatRangeLine("GREEN", Config::GREEN_GEM_HSV_MIN, Config::GREEN_GEM_HSV_MAX));
    hsvLines.push_back(FormatRangeLine("BLUE", Config::BLUE_GEM_HSV_MIN, Config::BLUE_GEM_HSV_MAX));
    hsvLines.push_back(FormatRangeLine("PURPLE", Config::PURPLE_GEM_HSV_MIN, Config::PURPLE_GEM_HSV_MAX));
    hsvLines.push_back(FormatRangeLine("WHITE", Config::WHITE_GEM_HSV_MIN, Config::WHITE_GEM_HSV_MAX));
  }
  else
  {
    hsvLines.push_back("SHAPE MODE - No color thresholds (press 'c' to toggle)");
  }

  // Draw text with monospace font
  if (freetypeInitialized && ft2)
  {
    // Use FreeType2 with monospace font
    ft2->putText(image, bannerText,
                 cv::Point(10, 0),
                 18, // Font height
                 Config::BANNER_TEXT_COLOR,
                 -1, // Thickness (filled)
                 cv::LINE_AA,
                 false); // Bottom-left origin
  }
  else
  {
    // Fallback to built-in font (not truly monospace but best we have)
    int fontFace = cv::FONT_HERSHEY_PLAIN;
    double fontScale = 1.0;
    int thickness = 1;
    int baseline = 0;

    cv::putText(image, bannerText,
                cv::Point(10, 20),
                fontFace, fontScale, Config::BANNER_TEXT_COLOR, thickness);
  }

  // Draw HSV ranges
  int fontFace = cv::FONT_HERSHEY_PLAIN;
  double fontScale = 1.0;
  int thickness = 1;
  int lineHeight = 16;
  int startY = 40;

  for (size_t i = 0; i < hsvLines.size(); ++i)
  {
    cv::Point pos(10, startY + static_cast<int>(i) * (lineHeight + 5));
    cv::putText(image, hsvLines[i], pos, fontFace, fontScale, cv::Scalar(180, 180, 180), thickness, cv::LINE_AA);
  }

  // Draw separator line below banner
  cv::line(image, cv::Point(0, Config::BANNER_HEIGHT),
           cv::Point(image.cols, Config::BANNER_HEIGHT),
           cv::Scalar(100, 100, 100), 1);
}
