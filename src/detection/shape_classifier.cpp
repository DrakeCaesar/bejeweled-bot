#include "shape_classifier.h"
#include "assets/loader.h"
#include "color_similarity.h"
#include "config.h"
#include <cmath>
#include <opencv2/opencv.hpp>

// ============================================================================
// Trackbar Creation for Real-time Parameter Adjustment
// ============================================================================

namespace ShapeParams {
// Trackbar backing variables (OpenCV requires int pointers)
static int debugStageTrack = debugStage.load();
static int matchThresholdTrack = matchThreshold.load();
static int lCompressionCeilingTrack = lCompressionCeiling.load();
static int lCompressionTrack = lCompression.load();

static void OnDebugStageChange(int value, void *) { debugStage.store(value); }

static void OnMatchThresholdChange(int value, void *) {
  int v = value;
  if (v < 1)
    v = 1;
  if (v > 100)
    v = 100;
  matchThreshold.store(v);
}

static void OnLCompressionCeilingChange(int value, void *) {
  int v = value;
  if (v < 0)
    v = 0;
  if (v > 254)
    v = 254;
  lCompressionCeiling.store(v);
}

static void OnLCompressionChange(int value, void *) {
  int v = value;
  if (v < 0)
    v = 0;
  if (v > 100)
    v = 100;
  lCompression.store(v);
}

void CreateTrackbars(const std::string &windowName) {
  cv::createTrackbar("Debug Stage", windowName, &debugStageTrack, 5,
                     OnDebugStageChange);
  cv::createTrackbar("Match Thresh %", windowName, &matchThresholdTrack, 100,
                     OnMatchThresholdChange);
  cv::createTrackbar("L Ceiling", windowName, &lCompressionCeilingTrack, 254,
                     OnLCompressionCeilingChange);
  cv::createTrackbar("L Compress %", windowName, &lCompressionTrack, 100,
                     OnLCompressionChange);
}
} // namespace ShapeParams

// ============================================================================
// Shape Processing - Direct Color Image Comparison
// ============================================================================

// Thread-local storage for debug visualizations
namespace {
thread_local cv::Mat g_debugBestInput;
thread_local cv::Mat g_debugBestRef;
thread_local cv::Mat g_debugBestDiffMap;

// Per-position temporal coherence cache (8x8 grid)
struct GemMatchCache {
  GemType type = GemType::UNKNOWN;
  int frame = -1;
};
static std::array<std::array<GemMatchCache, 8>, 8> s_lastMatches;

// Planar mat pool for pre-allocated planar LAB mats (one per 8x8 grid position)
struct PlanarMatPool {
  cv::Mat mats[64]; // One per 8x8 grid position
  bool initialized = false;
  int tileWidth = 0;
  int tileHeight = 0;

  void Initialize(int width, int height) {
    if (initialized)
      return;
    int pixelsPerTile = width * height;
    for (int i = 0; i < 64; i++) {
      mats[i] = cv::Mat(3, pixelsPerTile, CV_8UC1);
    }
    tileWidth = width;
    tileHeight = height;
    initialized = true;
  }
};
static thread_local PlanarMatPool g_planarMatPool;

// Thread-local temporary mat for LAB conversion pipeline
static thread_local cv::Mat g_tempLabImage;
} // namespace

// Helper: Iterate over all pixels (no longer crop-bounded - using bitmasks for
// optimization)
template <typename Callback>
static void IterateAllPixels(const cv::Mat &img1Lab, const cv::Mat &img2Lab,
                             Callback cb) {
  // Background color constants
  const uchar bgL = 0;
  const uchar bgA = 128;
  const uchar bgB = 128;

  // Process all pixels
  for (int y = 0; y < img1Lab.rows; y++) {
    const cv::Vec3b *row1 = img1Lab.ptr<cv::Vec3b>(y);
    const cv::Vec3b *row2 = img2Lab.ptr<cv::Vec3b>(y);

    for (int x = 0; x < img1Lab.cols; x++) {
      if (row2[x][0] == bgL && row2[x][1] == bgA && row2[x][2] == bgB)
        continue;

      int dL = static_cast<int>(row1[x][0]) - static_cast<int>(row2[x][0]);
      int da = static_cast<int>(row1[x][1]) - static_cast<int>(row2[x][1]);
      int db = static_cast<int>(row1[x][2]) - static_cast<int>(row2[x][2]);

      cb(x, y, dL, da, db);
    }
  }
}

// Helper: Convert interleaved BGR to planar LAB with optional L channel
// compression Stores image dimensions in the matrix for easy access
static void ConvertToPlanarLab(const cv::Mat &bgrImage, cv::Mat &planarLabOut,
                               int lCompressionCeiling = 180,
                               int lCompressionStrength = 0) {
  // Convert BGR to LAB using pre-allocated temp mat
  cv::cvtColor(bgrImage, g_tempLabImage, cv::COLOR_BGR2Lab);

  // Apply non-linear compression to high L channel values only
  if (lCompressionStrength > 0 && lCompressionCeiling < 254) {
    // Extract L channel (first channel in Lab image)
    const int rows = g_tempLabImage.rows;
    const int cols = g_tempLabImage.cols;

    // Define ceiling at which compression begins (from slider)
    const uchar compressionCeiling = static_cast<uchar>(lCompressionCeiling);
    const float compressionRange = 255.0f - compressionCeiling;

    // Calculate compression factor based on strength (0 to 1.0)
    const float compressionFactor =
        static_cast<float>(lCompressionStrength) / 100.0f;

    // Process L channel pixels
    for (int y = 0; y < rows; ++y) {
      cv::Vec3b *row = g_tempLabImage.ptr<cv::Vec3b>(y);
      for (int x = 0; x < cols; ++x) {
        uchar &L = row[x][0]; // L channel

        // Only compress values above the ceiling
        if (L > compressionCeiling) {
          float normalizedValue = (L - compressionCeiling) / compressionRange;

          // Apply non-linear compression curve: x^a where a > 1
          const float exponent =
              1.0f + (compressionFactor * 2.0f); // 1.0 to 3.0
          float compressedNormalized = std::pow(normalizedValue, exponent);

          // Map back to 0-255 range
          L = compressionCeiling +
              static_cast<uchar>(compressedNormalized * compressionRange);
        }
        // Values at or below compressionCeiling remain unchanged
      }
    }
  }

  // Map planar output rows to 2D views and split directly into them
  const int rows = g_tempLabImage.rows;
  const int cols = g_tempLabImage.cols;
  cv::Mat lPlane(rows, cols, CV_8UC1, planarLabOut.ptr<uchar>(0));
  cv::Mat aPlane(rows, cols, CV_8UC1, planarLabOut.ptr<uchar>(1));
  cv::Mat bPlane(rows, cols, CV_8UC1, planarLabOut.ptr<uchar>(2));

  cv::Mat dstPlanes[] = {lPlane, aPlane, bPlane};
  const int fromTo[] = {0, 0, 1, 1, 2, 2};
  cv::mixChannels(&g_tempLabImage, 1, dstPlanes, 3, fromTo, 3);
}

cv::Mat ProcessGemForShapeMatching(const cv::Mat &gemRegion) {
  // No-op wrapper maintained for API compatibility
  // Returns a clone of the region (or empty if input is empty)
  return gemRegion.empty() ? cv::Mat() : gemRegion.clone();
}

cv::Mat GetDebugVisualization(const cv::Mat &gemRegion) {
  int stage = ShapeParams::debugStage.load();

  if (stage == 0)
    return cv::Mat(); // Show nothing for maximum throughput
  if (stage == 1)
    return gemRegion.clone();
  if (stage == 2 && !g_debugBestRef.empty())
    return g_debugBestRef.clone();
  if (stage == 3 && !g_debugBestDiffMap.empty())
    return g_debugBestDiffMap.clone();
  if (stage == 4 && !g_debugBestInput.empty() && !g_debugBestDiffMap.empty()) {
    cv::Mat overlay;
    cv::addWeighted(g_debugBestInput, 0.5, g_debugBestDiffMap, 0.5, 0, overlay);
    return overlay;
  }

  return gemRegion.clone();
}

// ============================================================================
// Color Similarity Calculation
// ============================================================================

// Helper: Convert integer similarity score (0-10000) to percentage (0.0-1.0)
// Applies sqrt conversion for better distance-to-similarity mapping
static float ConvertScoreToPercentage(int score) {
  // Apply sqrt for accurate distance-to-similarity conversion
  // score = 10000 - avgDistSq, so avgDistSq = 10000 - score
  // final_score = 10000 - sqrt(avgDistSq) * 100
  float avgDistSq = 10000.0f - static_cast<float>(score);
  avgDistSq = std::max(avgDistSq, 0.0f);
  float sqrtDist = std::sqrt(avgDistSq);
  float finalScore = 10000.0f - sqrtDist * 100.0f;
  finalScore = std::max(finalScore, 0.0f);
  return finalScore / 10000.0f;
}

static void
GenerateDebugVisualization(const cv::Mat &img1, const cv::Mat &img1Lab,
                           const cv::Mat &img2Lab, const cv::Mat &alphaMask,
                           const cv::Mat &img2Bgr, GemType type, int frame) {
  if (img1.empty() || img2Lab.empty() || img1Lab.empty())
    return;

  // Apply mask to BGR for debug visualization
  cv::Mat i1 = img1.clone();
  if (!alphaMask.empty()) {
    for (int y = 0; y < i1.rows; y++) {
      cv::Vec3b *row1 = i1.ptr<cv::Vec3b>(y);
      const uchar *maskRow = alphaMask.ptr<uchar>(y);
      for (int x = 0; x < i1.cols; x++) {
        if (maskRow[x] == 0) {
          row1[x] = cv::Vec3b(0, 0, 0);
        }
      }
    }
  }

  g_debugBestInput = i1;
  g_debugBestRef = img2Bgr.empty() ? cv::Mat() : img2Bgr.clone();

  // Generate diff map
  g_debugBestDiffMap = cv::Mat::zeros(img1Lab.rows, img1Lab.cols, CV_8UC3);

  struct DebugDiffWork {
    cv::Mat &diffMap;

    void operator()(int x, int y, int dL, int da, int db) {
      cv::Vec3b *diffRow = diffMap.ptr<cv::Vec3b>(y);
      float dist = std::sqrt(static_cast<float>(dL * dL + da * da + db * db));

      if (dist < 10.0f) {
        diffRow[x] = cv::Vec3b(0, 0, 0);
      } else {
        float normalized = std::min((dist - 10.0f) / 90.0f, 1.0f);
        uchar intensity = static_cast<uchar>(normalized * 255.0f);
        if (normalized < 0.5f) {
          diffRow[x] = cv::Vec3b(0, intensity * 2, intensity * 2);
        } else {
          diffRow[x] = cv::Vec3b(
              0, static_cast<uchar>((1.0f - normalized) * 510.0f), 255);
        }
      }
    }
  };

  DebugDiffWork work{g_debugBestDiffMap};
  IterateAllPixels(img1Lab, img2Lab, work);
}

// ============================================================================
// Gem Classification Helper Functions
// ============================================================================

// Check a specific type/frame with caching to avoid redundant comparisons
static int CheckMatchScore(GemType type, int frame,
                           const cv::Mat &processedImagePlanarLab,
                           int imageWidth, int imageHeight, int *scoreCache,
                           bool *checkedFlags, int maxCacheSize,
                           int maxFramesPerType) {
  int idx = static_cast<int>(type) * maxFramesPerType + frame;

  // Return cached score if already checked
  if (idx >= 0 && idx < maxCacheSize && checkedFlags[idx]) {
    return scoreCache[idx];
  }

  // Compute score using planar format for optimal SIMD performance
  const cv::Mat &refImagePlanarLab = MaskLoader::GetPlanarLabMask(type, frame);
  int score = 0;
  if (!refImagePlanarLab.empty()) {
    const auto &bitmask = MaskLoader::GetBitmask(type, frame);
    score = CalculateColorSimilarity(processedImagePlanarLab, refImagePlanarLab,
                                     bitmask, imageWidth, imageHeight);
  }

  // Cache result
  if (idx >= 0 && idx < maxCacheSize) {
    scoreCache[idx] = score;
    checkedFlags[idx] = true;
  }

  return score;
}

// Finalize a match by updating cache and output parameters
static GemType FinalizeMatch(GemType type, int frame, int score,
                             GemMatchCache &cache, float *outBestMatch,
                             int *outBestFrame, bool needDebugMap,
                             const cv::Mat &processedImage,
                             const cv::Mat &processedImageLab) {
  cache.type = type;
  cache.frame = frame;

  // Convert score to percentage (0.0-1.0)
  if (outBestMatch) {
    *outBestMatch = ConvertScoreToPercentage(score);
  }
  if (outBestFrame)
    *outBestFrame = frame;
  if (needDebugMap) {
    const cv::Mat &refImageBgr = MaskLoader::GetMask(type, frame);
    GenerateDebugVisualization(
        processedImage, processedImageLab, MaskLoader::GetLabMask(type, frame),
        MaskLoader::GetAlphaMask(type, frame), refImageBgr, type, frame);
  }
  return type;
}

// Try matching a specific type/frame, updating best score and returning if
// threshold met
static bool TryMatch(GemType type, int frame,
                     const cv::Mat &processedImagePlanarLab, int imageWidth,
                     int imageHeight, int *scoreCache, bool *checkedFlags,
                     int maxCacheSize, int maxFramesPerType, int threshold,
                     GemType &bestType, int &bestScore, int &bestFrame) {
  int score = CheckMatchScore(type, frame, processedImagePlanarLab, imageWidth,
                              imageHeight, scoreCache, checkedFlags,
                              maxCacheSize, maxFramesPerType);
  if (score > bestScore) {
    bestScore = score;
    bestType = type;
    bestFrame = frame;
  }
  return score >= threshold;
}

GemType ClassifyGemByShape(const cv::Mat &processedImage,
                           const cv::Mat &processedImageLab, int gemRow,
                           int gemCol, float *outBestMatch, int *outBestFrame,
                           bool needDebugMap) {
  if (processedImage.empty() || !MaskLoader::AreMasksLoaded()) {
    if (outBestMatch)
      *outBestMatch = 0.0f;
    if (outBestFrame)
      *outBestFrame = -1;
    return GemType::UNKNOWN;
  }

  // Initialize planar mat pool on first use for this thread
  if (!g_planarMatPool.initialized) {
    g_planarMatPool.Initialize(processedImage.cols, processedImage.rows);
  }

  // Convert to planar LAB format for optimal SIMD performance
  // Reuse pre-allocated mat from pool based on tile position
  cv::Mat &processedImagePlanarLab = g_planarMatPool.mats[gemRow * 8 + gemCol];
  ConvertToPlanarLab(processedImage, processedImagePlanarLab,
                     ShapeParams::lCompressionCeiling.load(),
                     ShapeParams::lCompression.load());
  if (processedImagePlanarLab.empty()) {
    if (outBestMatch)
      *outBestMatch = 0.0f;
    if (outBestFrame)
      *outBestFrame = -1;
    return GemType::UNKNOWN;
  }

  // Store image dimensions for SIMD access
  int imageWidth = processedImage.cols;
  int imageHeight = processedImage.rows;

  // All gem types including hypercube for unified iteration
  static const GemType allGemTypes[] = {
      GemType::RED,  GemType::ORANGE, GemType::YELLOW, GemType::GREEN,
      GemType::BLUE, GemType::PURPLE, GemType::WHITE,  GemType::HYPERCUBE};
  static const int numAllGemTypes = 8;

  // Convert user's desired final percentage to internal threshold for
  // comparison Derived from: internalThreshold = finalPercentage * (200 -
  // finalPercentage)
  int finalThresholdPercent =
      ShapeParams::matchThreshold
          .load(); // User's desired final percentage (0-100)
  int threshold = finalThresholdPercent * (200 - finalThresholdPercent);
  GemType bestType = GemType::UNKNOWN;
  int bestScore = 0;
  int bestFrame = -1;

  // Get last match for this specific gem position
  auto &cache = s_lastMatches[gemRow][gemCol];
  GemType lastType = cache.type;
  int lastFrame = cache.frame;

  // Track checked frames to avoid redundant comparisons (max 8 types * ~30
  // frames = 240 entries) Use array indexed by (type * MAX_FRAMES + frame) for
  // fast lookup
  constexpr int MAX_FRAMES_PER_TYPE = 32; // Conservative upper bound
  constexpr int MAX_CACHE_SIZE = 8 * MAX_FRAMES_PER_TYPE;
  thread_local int scoreCache[MAX_CACHE_SIZE];
  thread_local bool checkedFlags[MAX_CACHE_SIZE];
  std::fill(std::begin(checkedFlags), std::end(checkedFlags), false);

  // Phase 0: Temporal coherence - check cached match and next animation frame
  if (lastType != GemType::UNKNOWN && lastFrame >= 0) {
    // Check the exact cached position first
    if (TryMatch(lastType, lastFrame, processedImagePlanarLab, imageWidth,
                 imageHeight, scoreCache, checkedFlags, MAX_CACHE_SIZE,
                 MAX_FRAMES_PER_TYPE, threshold, bestType, bestScore,
                 bestFrame)) {
      return FinalizeMatch(lastType, lastFrame, bestScore, cache, outBestMatch,
                           outBestFrame, needDebugMap, processedImage,
                           processedImageLab);
    }

    // If we were in an animation, check the next frame
    if (lastFrame > 0) {
      int frameCount = MaskLoader::GetFrameCount(lastType);
      int nextFrame = (lastFrame + 1) % frameCount;
      if (nextFrame != 0 &&
          TryMatch(lastType, nextFrame, processedImagePlanarLab, imageWidth,
                   imageHeight, scoreCache, checkedFlags, MAX_CACHE_SIZE,
                   MAX_FRAMES_PER_TYPE, threshold, bestType, bestScore,
                   bestFrame)) {
        return FinalizeMatch(lastType, nextFrame, bestScore, cache,
                             outBestMatch, outBestFrame, needDebugMap,
                             processedImage, processedImageLab);
      }
    }
  }

  // Phase 1: Check frame 0 of all gem types (cache prevents redundant
  // computation)
  struct FirstFrameResult {
    GemType type;
    int score;
  };
  FirstFrameResult firstFrameScores[8];

  for (int i = 0; i < numAllGemTypes; i++) {
    int score = CheckMatchScore(
        allGemTypes[i], 0, processedImagePlanarLab, imageWidth, imageHeight,
        scoreCache, checkedFlags, MAX_CACHE_SIZE, MAX_FRAMES_PER_TYPE);
    firstFrameScores[i] = {allGemTypes[i], score};

    if (score > bestScore) {
      bestScore = score;
      bestType = allGemTypes[i];
      bestFrame = 0;
    }

    if (score >= threshold) {
      return FinalizeMatch(allGemTypes[i], 0, score, cache, outBestMatch,
                           outBestFrame, needDebugMap, processedImage,
                           processedImageLab);
    }
  }

  // Sort by first-frame score to prioritize likely matches in Phase 2
  for (int i = 1; i < numAllGemTypes; i++) {
    FirstFrameResult key = firstFrameScores[i];
    int j = i - 1;
    while (j >= 0 && firstFrameScores[j].score < key.score) {
      firstFrameScores[j + 1] = firstFrameScores[j];
      j--;
    }
    firstFrameScores[j + 1] = key;
  }

  // Phase 2: Check remaining frames, prioritizing types with better first-frame
  // scores
  for (int i = 0; i < numAllGemTypes; i++) {
    GemType type = firstFrameScores[i].type;
    int frameCount = MaskLoader::GetFrameCount(type);

    for (int frame = 1; frame < frameCount; frame++) {
      if (TryMatch(type, frame, processedImagePlanarLab, imageWidth,
                   imageHeight, scoreCache, checkedFlags, MAX_CACHE_SIZE,
                   MAX_FRAMES_PER_TYPE, threshold, bestType, bestScore,
                   bestFrame)) {
        return FinalizeMatch(type, frame, bestScore, cache, outBestMatch,
                             outBestFrame, needDebugMap, processedImage,
                             processedImageLab);
      }
    }
  }

  // Update cache with best match (reached when no match >= threshold was found)
  cache.type = bestType;
  cache.frame = bestFrame;

  // Convert score to percentage (0.0-1.0)
  if (outBestMatch) {
    *outBestMatch = ConvertScoreToPercentage(bestScore);
  }
  if (outBestFrame)
    *outBestFrame = bestFrame;

  // Generate debug visualization for best match if needed
  if (needDebugMap && bestType != GemType::UNKNOWN && bestFrame >= 0) {
    const cv::Mat &refImageLab = MaskLoader::GetLabMask(bestType, bestFrame);
    const cv::Mat &refImageBgr = MaskLoader::GetMask(bestType, bestFrame);
    const cv::Mat &alphaMask = MaskLoader::GetAlphaMask(bestType, bestFrame);
    if (!refImageLab.empty()) {
      GenerateDebugVisualization(processedImage, processedImageLab, refImageLab,
                                 alphaMask, refImageBgr, bestType, bestFrame);
    }
  }

  return bestType;
}
