#include "shape_classifier.h"
#include "mask_loader.h"

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

  // Apply Gaussian blur to reduce noise
  cv::GaussianBlur(grayscale, blurred, cv::Size(blur, blur), 0);

  // Apply adaptive thresholding to create binary mask
  // This handles varying lighting conditions better than fixed threshold
  cv::adaptiveThreshold(blurred, binary, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C,
                        cv::THRESH_BINARY, block, threshC);

  // Apply morphological operations to clean up the edges
  cv::Mat kernel =
      cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(morph, morph));

  // Close small gaps in the edge lines
  cv::morphologyEx(binary, binary, cv::MORPH_CLOSE, kernel);

  // Remove small noise spots
  cv::morphologyEx(binary, binary, cv::MORPH_OPEN, kernel);

  // Find contours in the edge image
  // The edges form black lines on white background, so we invert first
  thread_local cv::Mat inverted;
  cv::bitwise_not(binary, inverted);

  thread_local std::vector<std::vector<cv::Point>> contours;
  contours.clear();
  cv::findContours(inverted, contours, cv::RETR_EXTERNAL,
                   cv::CHAIN_APPROX_SIMPLE);

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

      // Apply morphological opening to remove thin protrusions ("sticks")
      // Use a larger kernel for more aggressive cleanup
      cv::Mat cleanupKernel =
          cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));
      cv::morphologyEx(result, result, cv::MORPH_OPEN, cleanupKernel);

      // Apply closing to fill any small holes created
      cv::morphologyEx(result, result, cv::MORPH_CLOSE, cleanupKernel);

      // Apply user-controlled erosion to shrink detected shape
      int erode = ShapeParams::erodeSize.load();
      if (erode > 0) {
        cv::Mat erodeKernel = cv::getStructuringElement(
            cv::MORPH_ELLIPSE, cv::Size(erode * 2 + 1, erode * 2 + 1));
        cv::erode(result, result, erodeKernel);
      }
    }
  }

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

  // Calculate intersection and union
  cv::Mat intersection, unionMat;
  cv::bitwise_and(m1, m2, intersection);
  cv::bitwise_or(m1, m2, unionMat);

  int intersectCount = cv::countNonZero(intersection);
  int unionCount = cv::countNonZero(unionMat);

  if (unionCount == 0)
    return 0.0f;

  return static_cast<float>(intersectCount) / static_cast<float>(unionCount);
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

  GemType bestType = GemType::UNKNOWN;
  float bestScore = 0.0f;
  int bestFrame = -1;

  // Compare against all masks for all gem types
  for (GemType type : gemTypes) {
    for (int frame = 0; frame < MaskLoader::FRAMES_PER_GEM; frame++) {
      const cv::Mat &mask = MaskLoader::GetMask(type, frame);
      if (mask.empty())
        continue;

      float score = CalculateIoU(processedMask, mask);

      if (score > bestScore) {
        bestScore = score;
        bestType = type;
        bestFrame = frame;
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
