#include "gradient_classifier.h"
#include "assets/loader.h"
#include "color_similarity.h"
#include "config.h"
#include "gradient_similarity.h"
#include <cmath>
#include <iomanip>
#include <opencv2/opencv.hpp>

// ============================================================================
// Trackbar Creation for Edge Threshold Adjustment
// ============================================================================

namespace GradientParams {
// Trackbar backing variable
static int edgeThresholdTrack = edgeThreshold.load();

static void OnEdgeThresholdChange(int value, void*) {
  int v = value;
  if (v < 0) v = 0;
  if (v > 255) v = 255;
  edgeThreshold.store(v);
  SetEdgeThreshold(v);
  std::cout << "[GRADIENT] Edge threshold: " << v << "/255" << std::endl;
}

void CreateTrackbars(const std::string& windowName) {
  cv::createTrackbar("GradEdgeThresh", windowName, &edgeThresholdTrack, 255,
                     OnEdgeThresholdChange);
}
} // namespace GradientParams

// ============================================================================
// Gradient Classification - Gradient-based gem matching
// ============================================================================

// Temporal coherence cache for gradient matching (separate from color cache)
namespace {
struct GradientMatchCache {
  GemType type = GemType::UNKNOWN;
  int frame = -1;
};
static std::array<std::array<GradientMatchCache, 8>, 8> s_gradientCache;

// Thread-local temporary mat for gradient conversion
static thread_local cv::Mat g_tempGrayImage;
static thread_local cv::Mat g_tempGradX;
static thread_local cv::Mat g_tempGradY;
} // namespace

cv::Mat ConvertToPlanarGradient(const cv::Mat &bgrImage) {
  if (bgrImage.empty()) {
    return cv::Mat();
  }

  // Convert to grayscale
  cv::cvtColor(bgrImage, g_tempGrayImage, cv::COLOR_BGR2GRAY);

  // Apply Gaussian blur to reduce noise (same as reference generation)
  cv::GaussianBlur(g_tempGrayImage, g_tempGrayImage, cv::Size(3, 3), 0);

  // Compute Sobel gradients in x and y directions
  cv::Sobel(g_tempGrayImage, g_tempGradX, CV_32F, 1, 0, 3);
  cv::Sobel(g_tempGrayImage, g_tempGradY, CV_32F, 0, 1, 3);

  // Compute gradient magnitude: sqrt(dx^2 + dy^2)
  cv::Mat magnitude;
  cv::magnitude(g_tempGradX, g_tempGradY, magnitude);

  // Normalize to 0-255 range
  cv::normalize(magnitude, magnitude, 0, 255, cv::NORM_MINMAX);
  magnitude.convertTo(magnitude, CV_8U);

  // Return as single-channel planar format (GGG... only one plane)
  // No need to duplicate across 3 planes for gradients
  return magnitude.reshape(0, 1);
}

namespace {
// Initialize LUT on first use
struct GradientLutInit {
  GradientLutInit() {
    InitializeGradientSimilarityLut();
    // Initialize edge threshold from parameter
    SetEdgeThreshold(GradientParams::edgeThreshold.load());
  }
};
static GradientLutInit g_gradientLutInit;
} // namespace

GemType ClassifyGemByGradient(const cv::Mat &processedImage, int gemRow,
                              int gemCol, float *outBestMatch,
                              int *outBestFrame) {
  if (processedImage.empty() || !MaskLoader::AreMasksLoaded()) {
    if (outBestMatch)
      *outBestMatch = 0.0f;
    if (outBestFrame)
      *outBestFrame = -1;
    return GemType::UNKNOWN;
  }

  // Convert input image to gradient magnitude in planar format
  cv::Mat processedImagePlanarGradient =
      ConvertToPlanarGradient(processedImage);
  if (processedImagePlanarGradient.empty()) {
    if (outBestMatch)
      *outBestMatch = 0.0f;
    if (outBestFrame)
      *outBestFrame = -1;
    return GemType::UNKNOWN;
  }

  // Store image dimensions for SIMD access
  int imageWidth = processedImage.cols;
  int imageHeight = processedImage.rows;

  // First 7 static gem types (RED through WHITE), excluding HYPERCUBE
  static const GemType staticGemTypes[] = {
      GemType::RED,  GemType::ORANGE, GemType::YELLOW, GemType::GREEN,
      GemType::BLUE, GemType::PURPLE, GemType::WHITE};
  static const int numStaticGemTypes = 7;

  // Use config threshold for gradient matching (direct percentage to score
  // conversion) Config value is in percent (0-100), convert to 0-10000 range
  int threshold = Config::GEM_MATCH_GRADIENT_THRESHOLD_PERCENT * 100;

  // std::cout << "[DEBUG] Gradient threshold: " << gradientThresholdPercent <<
  // "%, internal: " << threshold << std::endl;

  GemType bestType = GemType::UNKNOWN;
  int bestScore = 0;
  int bestFrame = -1;

  // Get last match for this position
  auto &cache = s_gradientCache[gemRow][gemCol];
  GemType lastType = cache.type;
  int lastFrame = cache.frame;

  // Phase 0: Temporal coherence - check cached match for early exit
  if (lastType != GemType::UNKNOWN && lastFrame >= 0) {
    // Check the exact cached position
    const cv::Mat &refGradient =
        MaskLoader::GetGradientMask(lastType, lastFrame);
    if (!refGradient.empty()) {
      const auto &bitmask = MaskLoader::GetBitmask(lastType, lastFrame);
      int score =
          CalculateGradientSimilarity(processedImagePlanarGradient, refGradient,
                                      bitmask, imageWidth, imageHeight);
      bestScore = score;
      bestType = lastType;
      bestFrame = lastFrame;

      // Check for early exit if above threshold
      if (score >= threshold) {
        cache.type = lastType;
        cache.frame = lastFrame;
        if (outBestMatch) {
          *outBestMatch = static_cast<float>(score) / 10000.0f;
        }
        if (outBestFrame)
          *outBestFrame = lastFrame;
        return lastType;
      }
    }

    // Check next frame in animation
    if (lastFrame > 0) {
      int frameCount = MaskLoader::GetFrameCount(lastType);
      int nextFrame = (lastFrame + 1) % frameCount;
      if (nextFrame != 0) {
        const cv::Mat &refGradientNext =
            MaskLoader::GetGradientMask(lastType, nextFrame);
        if (!refGradientNext.empty()) {
          const auto &bitmaskNext = MaskLoader::GetBitmask(lastType, nextFrame);
          int scoreNext = CalculateGradientSimilarity(
              processedImagePlanarGradient, refGradientNext, bitmaskNext,
              imageWidth, imageHeight);

          if (scoreNext > bestScore) {
            bestScore = scoreNext;
            bestType = lastType;
            bestFrame = nextFrame;
          }

          // Check for early exit if above threshold
          if (scoreNext >= threshold) {
            cache.type = lastType;
            cache.frame = nextFrame;
            if (outBestMatch) {
              *outBestMatch = static_cast<float>(scoreNext) / 10000.0f;
            }
            if (outBestFrame)
              *outBestFrame = nextFrame;
            return lastType;
          }
        }
      }
    }
  }

  // Phase 1: Check static frame 0 and first animated frame 1 for debugging
  // This makes it easier to understand what's happening during debugging
  for (int i = 0; i < numStaticGemTypes; i++) {
    GemType type = staticGemTypes[i];
    const int debugFrames[] = {0, 1};

    for (int frameIndex : debugFrames) {
      const cv::Mat &refGradient =
          MaskLoader::GetGradientMask(type, frameIndex);
      if (refGradient.empty()) {
        continue;
      }

      const auto &bitmask = MaskLoader::GetBitmask(type, frameIndex);
      int score =
          CalculateGradientSimilarity(processedImagePlanarGradient, refGradient,
                                      bitmask, imageWidth, imageHeight);

      if (score > bestScore) {
        bestScore = score;
        bestType = type;
        bestFrame = frameIndex;
      }

      // Check for early exit if above gradient search threshold
      if (score >= threshold) {
        cache.type = type;
        cache.frame = frameIndex;
        if (outBestMatch) {
          *outBestMatch = static_cast<float>(score) / 10000.0f;
        }
        if (outBestFrame)
          *outBestFrame = frameIndex;
        return type;
      }
    }
  }

  // Update cache with best match (even if below threshold)
  cache.type = bestType;
  cache.frame = bestFrame;

  // Convert score to percentage (0.0-1.0)
  if (outBestMatch) {
    *outBestMatch = static_cast<float>(bestScore) / 10000.0f;
  }
  if (outBestFrame)
    *outBestFrame = bestFrame;

  // DEBUG: For debugging, always return the best match among static gems
  // even if the score is low. This helps visualize what's happening.
  return bestType;
}
