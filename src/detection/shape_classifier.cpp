#include "shape_classifier.h"
#include "mask_loader.h"
#include <opencv2/opencv.hpp>

// ============================================================================
// Trackbar Creation for Real-time Parameter Adjustment
// ============================================================================

namespace ShapeParams {

// Trackbar backing variables (OpenCV requires int pointers)
static int blurSizeTrack = blurSize.load();
static int blockSizeTrack = blockSize.load();
static int thresholdCTrack =
    thresholdC.load() + 20; // Offset to make range 0-40
static int morphSizeTrack = morphSize.load();
static int erodeSizeTrack = erodeSize.load();
static int debugStageTrack = debugStage.load();
static int matchThresholdTrack = matchThreshold.load();
static int maskOffsetXTrack = maskOffsetX.load();
static int maskOffsetYTrack = maskOffsetY.load();

static void OnBlurChange(int value, void *) {
  // Ensure odd value, minimum 1
  int v = value | 1;
  if (v < 1)
    v = 1;
  blurSize.store(v);
}

static void OnBlockSizeChange(int value, void *) {
  // Ensure odd value, minimum 3
  int v = value | 1;
  if (v < 3)
    v = 3;
  blockSize.store(v);
}

static void OnThresholdCChange(int value, void *) {
  // Convert from 0-40 range to -20 to +20
  thresholdC.store(value - 20);
}

static void OnMorphSizeChange(int value, void *) {
  int v = value;
  if (v < 1)
    v = 1;
  morphSize.store(v);
}

static void OnErodeSizeChange(int value, void *) { erodeSize.store(value); }

static void OnDebugStageChange(int value, void *) { debugStage.store(value); }

static void OnMatchThresholdChange(int value, void *) {
  // Clamp to 1-100 range
  int v = value;
  if (v < 1)
    v = 1;
  if (v > 100)
    v = 100;
  matchThreshold.store(v);
}

static void OnMaskOffsetXChange(int value, void *) { maskOffsetX.store(value); }

static void OnMaskOffsetYChange(int value, void *) { maskOffsetY.store(value); }

void CreateTrackbars(const std::string &windowName) {
  cv::createTrackbar("Blur Size", windowName, &blurSizeTrack, 15, OnBlurChange);
  cv::createTrackbar("Block Size", windowName, &blockSizeTrack, 51,
                     OnBlockSizeChange);
  cv::createTrackbar("Threshold C", windowName, &thresholdCTrack, 40,
                     OnThresholdCChange);
  cv::createTrackbar("Morph Size", windowName, &morphSizeTrack, 9,
                     OnMorphSizeChange);
  cv::createTrackbar("Erode Size", windowName, &erodeSizeTrack, 10,
                     OnErodeSizeChange);
  cv::createTrackbar("Debug Stage", windowName, &debugStageTrack, 10,
                     OnDebugStageChange);
  cv::createTrackbar("Match Thresh %", windowName, &matchThresholdTrack, 100,
                     OnMatchThresholdChange);
  cv::createTrackbar("Mask Offset X", windowName, &maskOffsetXTrack, 10,
                     OnMaskOffsetXChange);
  cv::createTrackbar("Mask Offset Y", windowName, &maskOffsetYTrack, 10,
                     OnMaskOffsetYChange);
}

} // namespace ShapeParams

// ============================================================================
// Shape Processing for Mask Comparison
// ============================================================================

cv::Mat ProcessGemForShapeMatching(const cv::Mat &gemRegion) {
  if (gemRegion.empty()) {
    return cv::Mat();
  }

  // Load current parameters
  int blur = ShapeParams::blurSize.load();
  int block = ShapeParams::blockSize.load();
  int threshC = ShapeParams::thresholdC.load();
  int morph = ShapeParams::morphSize.load();
  int stage = ShapeParams::debugStage.load();

  // Ensure odd values for blur and block size
  blur = (blur < 1) ? 1 : (blur | 1);
  block = (block < 3) ? 3 : (block | 1);
  morph = (morph < 1) ? 1 : morph;

  // Reuse thread-local buffers to avoid repeated allocations
  thread_local cv::Mat grayscale;
  thread_local cv::Mat blurred;
  thread_local cv::Mat binary;

  // Convert to grayscale
  cv::cvtColor(gemRegion, grayscale, cv::COLOR_BGR2GRAY);
  if (stage == 1)
    return grayscale.clone();

  // Apply Gaussian blur to reduce noise
  cv::GaussianBlur(grayscale, blurred, cv::Size(blur, blur), 0);
  if (stage == 2)
    return blurred.clone();

  // Apply adaptive thresholding to create binary mask
  // This handles varying lighting conditions better than fixed threshold
  cv::adaptiveThreshold(blurred, binary, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C,
                        cv::THRESH_BINARY, block, threshC);
  if (stage == 3)
    return binary.clone();

  // Apply morphological operations to clean up the edges
  cv::Mat kernel =
      cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(morph, morph));

  // Close small gaps in the edge lines
  cv::morphologyEx(binary, binary, cv::MORPH_CLOSE, kernel);

  // Remove small noise spots
  cv::morphologyEx(binary, binary, cv::MORPH_OPEN, kernel);
  if (stage == 4)
    return binary.clone();

  // Find contours in the edge image
  // The edges form black lines on white background, so we invert first
  thread_local cv::Mat inverted;
  cv::bitwise_not(binary, inverted);
  if (stage == 5)
    return inverted.clone();

  // Close gaps in contours using morphological closing
  cv::Mat closeKernel =
      cv::getStructuringElement(cv::MORPH_CROSS, cv::Size(4, 4));
  cv::morphologyEx(inverted, inverted, cv::MORPH_CLOSE, closeKernel);
  if (stage == 6)
    return inverted.clone();

  thread_local std::vector<std::vector<cv::Point>> contours;
  contours.clear();
  cv::findContours(inverted.clone(), contours, cv::RETR_EXTERNAL,
                   cv::CHAIN_APPROX_SIMPLE);

  // Stage 7: Show all contours drawn
  if (stage == 7) {
    cv::Mat contourVis = inverted.clone();
    cv::drawContours(contourVis, contours, -1, cv::Scalar(128), 1);
    return contourVis;
  }

  // Create output mask (white gem on black background)
  cv::Mat result = cv::Mat::zeros(binary.size(), CV_8UC1);

  if (!contours.empty()) {
    // Find the best contour: largest area that's reasonably centered
    int centerX = binary.cols / 2;
    int centerY = binary.rows / 2;
    int bestIdx = -1;
    double bestScore = -1;

    for (size_t i = 0; i < contours.size(); i++) {
      double area = cv::contourArea(contours[i]);
      if (area < 100)
        continue; // Skip tiny contours

      // Get contour center
      cv::Moments m = cv::moments(contours[i]);
      if (m.m00 == 0)
        continue;
      int cx = static_cast<int>(m.m10 / m.m00);
      int cy = static_cast<int>(m.m01 / m.m00);

      // Distance from image center
      double dist = std::sqrt((cx - centerX) * (cx - centerX) +
                              (cy - centerY) * (cy - centerY));

      // Score: prefer large area and central position
      // Normalize distance (max ~60 pixels from center in 120x120 image)
      double distScore = 1.0 - std::min(dist / 60.0, 1.0);
      double score = area * (0.5 + 0.5 * distScore);

      if (score > bestScore) {
        bestScore = score;
        bestIdx = static_cast<int>(i);
      }
    }

    // Fill the best contour to create a solid silhouette
    if (bestIdx >= 0) {
      cv::drawContours(result, contours, bestIdx, cv::Scalar(255), cv::FILLED);
      if (stage == 8)
        return result.clone();

      // Apply morphological opening to remove thin protrusions ("sticks")
      // Use a larger kernel for more aggressive cleanup
      cv::Mat cleanupKernel =
          cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));
      cv::morphologyEx(result, result, cv::MORPH_OPEN, cleanupKernel);

      // Apply closing to fill any small holes created
      cv::morphologyEx(result, result, cv::MORPH_CLOSE, cleanupKernel);
      if (stage == 9)
        return result.clone();

      // Apply user-controlled erosion to shrink detected shape
      int erode = ShapeParams::erodeSize.load();
      if (erode > 0) {
        cv::Mat erodeKernel = cv::getStructuringElement(
            cv::MORPH_ELLIPSE, cv::Size(erode * 2 + 1, erode * 2 + 1));
        cv::erode(result, result, erodeKernel);
      }
      if (stage == 10)
        return result.clone();
    }
  }

  // Stage 11: comparison visualization is handled in overlay.cpp
  return result;
}

// ============================================================================
// Shape Classification - Compare against loaded masks
// ============================================================================

// Calculate Intersection over Union (IoU) between two binary masks
static float CalculateIoU(const cv::Mat &mask1, const cv::Mat &mask2) {
  if (mask1.empty() || mask2.empty())
    return 0.0f;

  // Resize if sizes don't match
  cv::Mat m1 = mask1;
  cv::Mat m2 = mask2;

  if (mask1.size() != mask2.size()) {
    cv::resize(mask2, m2, mask1.size(), 0, 0, cv::INTER_NEAREST);
  }

  // Apply mask offset (slider range 0-10 maps to -5 to +5 pixels)
  int offsetX = ShapeParams::maskOffsetX.load() - 5;
  int offsetY = ShapeParams::maskOffsetY.load() - 5;

  if (offsetX != 0 || offsetY != 0) {
    // Create translation matrix and shift the reference mask
    cv::Mat shiftedMask = cv::Mat::zeros(m2.size(), m2.type());

    // Calculate source and destination ROIs for the shift
    int srcX = std::max(0, -offsetX);
    int srcY = std::max(0, -offsetY);
    int dstX = std::max(0, offsetX);
    int dstY = std::max(0, offsetY);
    int width = m2.cols - std::abs(offsetX);
    int height = m2.rows - std::abs(offsetY);

    if (width > 0 && height > 0) {
      cv::Rect srcRoi(srcX, srcY, width, height);
      cv::Rect dstRoi(dstX, dstY, width, height);
      m2(srcRoi).copyTo(shiftedMask(dstRoi));
    }
    m2 = shiftedMask;
  }

  // Calculate the difference (XOR) and erode it to ignore small edge
  // differences
  cv::Mat diff;
  cv::bitwise_xor(m1, m2, diff);

  // Erode the difference to remove small mismatches along edges
  cv::Mat erodeKernel =
      cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3));
  // cv::erode(diff, diff, erodeKernel);

  // Calculate intersection and union, then subtract eroded diff from union
  cv::Mat intersection, unionMat;
  cv::bitwise_and(m1, m2, intersection);
  cv::bitwise_or(m1, m2, unionMat);

  // Effective intersection = intersection + eroded away differences
  int intersectCount = cv::countNonZero(intersection);
  int diffCount = cv::countNonZero(diff);
  int unionCount = cv::countNonZero(unionMat);

  if (unionCount == 0)
    return 0.0f;

  // Score: intersection / (union - eroded diff) gives better tolerance for edge
  // noise
  int effectiveUnion =
      unionCount -
      (cv::countNonZero(unionMat) - cv::countNonZero(intersection) - diffCount);
  if (effectiveUnion <= 0)
    effectiveUnion = unionCount;

  return static_cast<float>(intersectCount +
                            (unionCount - intersectCount - diffCount)) /
         static_cast<float>(unionCount);
}

GemType ClassifyGemByShape(const cv::Mat &processedMask, float *outBestMatch,
                           int *outBestFrame) {
  if (processedMask.empty() || !MaskLoader::AreMasksLoaded()) {
    if (outBestMatch)
      *outBestMatch = 0.0f;
    if (outBestFrame)
      *outBestFrame = -1;
    return GemType::UNKNOWN;
  }

  // Gem types to check
  static const GemType gemTypes[] = {
      GemType::RED,  GemType::ORANGE, GemType::YELLOW, GemType::GREEN,
      GemType::BLUE, GemType::PURPLE, GemType::WHITE};
  static const int numTypes = sizeof(gemTypes) / sizeof(gemTypes[0]);

  // Get threshold as fraction (0.0 - 1.0)
  float threshold = ShapeParams::matchThreshold.load() / 100.0f;

  GemType bestType = GemType::UNKNOWN;
  float bestScore = 0.0f;
  int bestFrame = -1;

  // ============================================================================
  // PHASE 1: Check only the first frame (frame 0) of each gem type
  // If any exceeds threshold, return immediately
  // ============================================================================
  struct FirstFrameResult {
    GemType type;
    float score;
  };
  FirstFrameResult firstFrameScores[7]; // One per gem type

  for (int i = 0; i < numTypes; i++) {
    GemType type = gemTypes[i];
    const cv::Mat &mask = MaskLoader::GetMask(type, 0);
    float score = 0.0f;

    if (!mask.empty()) {
      score = CalculateIoU(processedMask, mask);
    }

    firstFrameScores[i] = {type, score};

    // Early exit: if first frame exceeds threshold, we're done
    if (score >= threshold) {
      if (outBestMatch)
        *outBestMatch = score;
      if (outBestFrame)
        *outBestFrame = 0;
      return type;
    }

    // Track best score so far
    if (score > bestScore) {
      bestScore = score;
      bestType = type;
      bestFrame = 0;
    }
  }

  // ============================================================================
  // PHASE 2: No first-frame match exceeded threshold
  // Sort gem types by first-frame score (highest to lowest)
  // Explore each set's remaining frames in that order until threshold is met
  // ============================================================================

  // Simple insertion sort (only 7 elements)
  for (int i = 1; i < numTypes; i++) {
    FirstFrameResult key = firstFrameScores[i];
    int j = i - 1;
    while (j >= 0 && firstFrameScores[j].score < key.score) {
      firstFrameScores[j + 1] = firstFrameScores[j];
      j--;
    }
    firstFrameScores[j + 1] = key;
  }

  // Explore remaining frames (1 to FRAMES_PER_GEM-1) for each type in sorted
  // order
  for (int i = 0; i < numTypes; i++) {
    GemType type = firstFrameScores[i].type;

    for (int frame = 1; frame < MaskLoader::FRAMES_PER_GEM; frame++) {
      const cv::Mat &mask = MaskLoader::GetMask(type, frame);
      if (mask.empty())
        continue;

      float score = CalculateIoU(processedMask, mask);

      if (score > bestScore) {
        bestScore = score;
        bestType = type;
        bestFrame = frame;
      }

      // Early exit if we found a match exceeding threshold
      if (score >= threshold) {
        if (outBestMatch)
          *outBestMatch = bestScore;
        if (outBestFrame)
          *outBestFrame = bestFrame;
        return bestType;
      }
    }
  }

  if (outBestMatch)
    *outBestMatch = bestScore;
  if (outBestFrame)
    *outBestFrame = bestFrame;

  // Require a minimum match threshold
  if (bestScore < 0.3f) {
    return GemType::UNKNOWN;
  }

  return bestType;
}
