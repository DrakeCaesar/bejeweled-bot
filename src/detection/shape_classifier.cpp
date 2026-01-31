#include "shape_classifier.h"

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

void CreateTrackbars(const std::string &windowName) {
  cv::createTrackbar("Blur Size", windowName, &blurSizeTrack, 15, OnBlurChange);
  cv::createTrackbar("Block Size", windowName, &blockSizeTrack, 51,
                     OnBlockSizeChange);
  cv::createTrackbar("Threshold C", windowName, &thresholdCTrack, 40,
                     OnThresholdCChange);
  cv::createTrackbar("Morph Size", windowName, &morphSizeTrack, 9,
                     OnMorphSizeChange);
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

  // Apply morphological operations to clean up the mask
  cv::Mat kernel =
      cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(morph, morph));

  // Close small gaps in the gem shape
  cv::morphologyEx(binary, binary, cv::MORPH_CLOSE, kernel);

  // Remove small noise spots
  cv::morphologyEx(binary, binary, cv::MORPH_OPEN, kernel);

  // Return a clone since binary uses thread-local storage
  return binary.clone();
}

// ============================================================================
// Shape Classification (Placeholder for mask comparison)
// ============================================================================

GemType ClassifyGemByShape(const cv::Mat &processedMask, float *outBestMatch) {
  // Placeholder implementation
  // This will be replaced with actual mask comparison logic when masks are
  // loaded

  if (outBestMatch) {
    *outBestMatch = 0.0f;
  }

  // For now, return UNKNOWN - actual classification will compare against
  // loaded mask templates for each gem type (7 types x 20 animation frames)
  return GemType::UNKNOWN;
}
