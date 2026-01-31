#include "color_classifier.h"
#include <algorithm>
#include <cmath>
#include <vector>

// ============================================================================
// Helper Functions
// ============================================================================

namespace {

float HueDistance(float h1, float h2) {
  float diff = std::fabs(h1 - h2);
  return std::min(diff, 180.0f - diff);
}

} // namespace

// ============================================================================
// Color Match Calculations
// ============================================================================

float CalculateColorMatch(const cv::Mat &hsv, const cv::Scalar &minHSV,
                          const cv::Scalar &maxHSV) {
  // Reuse static buffer to avoid repeated allocations
  thread_local cv::Mat mask;
  cv::inRange(hsv, minHSV, maxHSV, mask);
  int matchingPixels = cv::countNonZero(mask);
  return static_cast<float>(matchingPixels) / (hsv.rows * hsv.cols);
}

float CalculateColorMatchUnion(const cv::Mat &hsv, const cv::Scalar &minHSV1,
                               const cv::Scalar &maxHSV1,
                               const cv::Scalar &minHSV2,
                               const cv::Scalar &maxHSV2) {
  // Reuse static buffers to avoid repeated allocations
  thread_local cv::Mat mask1, mask2, mask;
  cv::inRange(hsv, minHSV1, maxHSV1, mask1);
  cv::inRange(hsv, minHSV2, maxHSV2, mask2);
  cv::bitwise_or(mask1, mask2, mask);
  int matchingPixels = cv::countNonZero(mask);
  return static_cast<float>(matchingPixels) / (hsv.rows * hsv.cols);
}

// ============================================================================
// Closest Hue Detection
// ============================================================================

GemType FindClosestGemByHue(float meanH, float meanS, float meanV) {
  // Check if it's white first (low saturation, high value)
  if (meanS <= Config::WHITE_GEM_HSV_MAX[1] &&
      meanV >= Config::WHITE_GEM_HSV_MIN[2]) {
    return GemType::WHITE;
  }

  // Define hue targets for each gem type - use static array to avoid
  // allocations
  struct HueTarget {
    GemType type;
    float hue;
  };

  static const HueTarget targets[] = {
      {GemType::RED, 0.0f},
      {GemType::ORANGE, static_cast<float>((Config::ORANGE_GEM_HSV_MIN[0] +
                                            Config::ORANGE_GEM_HSV_MAX[0]) /
                                           2.0)},
      {GemType::YELLOW, static_cast<float>((Config::YELLOW_GEM_HSV_MIN[0] +
                                            Config::YELLOW_GEM_HSV_MAX[0]) /
                                           2.0)},
      {GemType::GREEN, static_cast<float>((Config::GREEN_GEM_HSV_MIN[0] +
                                           Config::GREEN_GEM_HSV_MAX[0]) /
                                          2.0)},
      {GemType::BLUE,
       static_cast<float>(
           (Config::BLUE_GEM_HSV_MIN[0] + Config::BLUE_GEM_HSV_MAX[0]) / 2.0)},
      {GemType::PURPLE, static_cast<float>((Config::PURPLE_GEM_HSV_MIN[0] +
                                            Config::PURPLE_GEM_HSV_MAX[0]) /
                                           2.0)},
  };

  GemType closestType = GemType::UNKNOWN;
  float bestDist = 1e9f;

  for (const auto &t : targets) {
    float dist = HueDistance(meanH, t.hue);
    if (dist < bestDist) {
      bestDist = dist;
      closestType = t.type;
    }
  }

  return closestType;
}

// ============================================================================
// Main Classification Function
// ============================================================================

GemType ClassifyGemByColor(const cv::Mat &gemRegion, float *outBestMatch,
                           GemType *outClosestType) {
  if (gemRegion.empty()) {
    if (outBestMatch)
      *outBestMatch = 0.0f;
    if (outClosestType)
      *outClosestType = GemType::UNKNOWN;
    return GemType::UNKNOWN;
  }

  // Reuse static buffer to avoid repeated allocations
  thread_local cv::Mat hsv;
  cv::cvtColor(gemRegion, hsv, cv::COLOR_BGR2HSV);

  // Calculate match percentages for each gem color
  float redMatch = CalculateColorMatchUnion(
      hsv, Config::RED_GEM_HSV_MIN_1, Config::RED_GEM_HSV_MAX_1,
      Config::RED_GEM_HSV_MIN_2, Config::RED_GEM_HSV_MAX_2);
  float orangeMatch = CalculateColorMatch(hsv, Config::ORANGE_GEM_HSV_MIN,
                                          Config::ORANGE_GEM_HSV_MAX);
  float yellowMatch = CalculateColorMatch(hsv, Config::YELLOW_GEM_HSV_MIN,
                                          Config::YELLOW_GEM_HSV_MAX);
  float greenMatch = CalculateColorMatch(hsv, Config::GREEN_GEM_HSV_MIN,
                                         Config::GREEN_GEM_HSV_MAX);
  float blueMatch = CalculateColorMatch(hsv, Config::BLUE_GEM_HSV_MIN,
                                        Config::BLUE_GEM_HSV_MAX);
  float purpleMatch = CalculateColorMatch(hsv, Config::PURPLE_GEM_HSV_MIN,
                                          Config::PURPLE_GEM_HSV_MAX);
  float whiteMatch = CalculateColorMatch(hsv, Config::WHITE_GEM_HSV_MIN,
                                         Config::WHITE_GEM_HSV_MAX);

  // Find the best match
  float maxMatch = 0.0f;
  GemType bestType = GemType::UNKNOWN;

  if (redMatch > maxMatch) {
    maxMatch = redMatch;
    bestType = GemType::RED;
  }
  if (orangeMatch > maxMatch) {
    maxMatch = orangeMatch;
    bestType = GemType::ORANGE;
  }
  if (yellowMatch > maxMatch) {
    maxMatch = yellowMatch;
    bestType = GemType::YELLOW;
  }
  if (greenMatch > maxMatch) {
    maxMatch = greenMatch;
    bestType = GemType::GREEN;
  }
  if (blueMatch > maxMatch) {
    maxMatch = blueMatch;
    bestType = GemType::BLUE;
  }
  if (purpleMatch > maxMatch) {
    maxMatch = purpleMatch;
    bestType = GemType::PURPLE;
  }
  if (whiteMatch > maxMatch) {
    maxMatch = whiteMatch;
    bestType = GemType::WHITE;
  }

  if (outBestMatch)
    *outBestMatch = maxMatch;

  // Determine closest hue match for debug display
  GemType closestType = bestType;
  if (maxMatch < Config::COLOR_MATCH_THRESHOLD) {
    cv::Scalar meanHSV = cv::mean(hsv);
    float meanH = static_cast<float>(meanHSV[0]);
    float meanS = static_cast<float>(meanHSV[1]);
    float meanV = static_cast<float>(meanHSV[2]);
    closestType = FindClosestGemByHue(meanH, meanS, meanV);
  }

  if (outClosestType)
    *outClosestType = closestType;

  if (maxMatch < Config::COLOR_MATCH_THRESHOLD) {
    return GemType::UNKNOWN;
  }

  return bestType;
}
