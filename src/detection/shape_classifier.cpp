#include "shape_classifier.h"
#include "mask_loader.h"
#include <cmath>
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
static int refScaleTrack = refScale.load();

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

static void OnRefScaleChange(int value, void *) {
  // Clamp to 980-1020 range (98.0% - 102.0%)
  int v = value;
  if (v < 980)
    v = 980;
  if (v > 1020)
    v = 1020;
  refScale.store(v);
}

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
  cv::createTrackbar("Debug Stage", windowName, &debugStageTrack, 5,
                     OnDebugStageChange);
  cv::createTrackbar("Match Thresh %", windowName, &matchThresholdTrack, 100,
                     OnMatchThresholdChange);
  cv::createTrackbar("Mask Offset X", windowName, &maskOffsetXTrack, 10,
                     OnMaskOffsetXChange);
  cv::createTrackbar("Mask Offset Y", windowName, &maskOffsetYTrack, 10,
                     OnMaskOffsetYChange);
  cv::createTrackbar("Ref Scale %%", windowName, &refScaleTrack, 1020,
                     OnRefScaleChange);
  cv::setTrackbarMin("Ref Scale %%", windowName, 980);
}

} // namespace ShapeParams

// ============================================================================
// Shape Processing - Direct Color Image Comparison
// ============================================================================

// Thread-local storage for debug visualizations
namespace {
  thread_local cv::Mat g_debugRefImage;
  thread_local cv::Mat g_debugDiffMap;
  thread_local cv::Mat g_debugMaskedInput;
  thread_local cv::Mat g_debugMaskedRef;
  thread_local cv::Mat g_debugBestInput;
  thread_local cv::Mat g_debugBestRef;
  thread_local cv::Mat g_debugBestDiffMap;
}

cv::Mat ProcessGemForShapeMatching(const cv::Mat &gemRegion) {
  if (gemRegion.empty()) {
    return cv::Mat();
  }

  int blur = ShapeParams::blurSize.load();
  blur = (blur < 1) ? 1 : (blur | 1);
  
  // Apply blur if needed
  if (blur > 1) {
    cv::Mat blurred;
    cv::GaussianBlur(gemRegion, blurred, cv::Size(blur, blur), 0);
    return blurred;
  }
  
  return gemRegion.clone();
}

cv::Mat GetDebugVisualization(const cv::Mat &gemRegion) {
  int stage = ShapeParams::debugStage.load();
  
  // Stage 0: original image
  if (stage == 0)
    return gemRegion.clone();
  
  // Stage 1: preprocessed (blurred) image
  if (stage == 1)
    return ProcessGemForShapeMatching(gemRegion);
  
  // Stage 2: show best matched reference image
  if (stage == 2) {
    if (!g_debugBestRef.empty())
      return g_debugBestRef.clone();
  }
  
  // Stage 3: show side-by-side comparison (input | reference)
  if (stage == 3) {
    if (!g_debugBestInput.empty() && !g_debugBestRef.empty()) {
      cv::Mat sideBySide;
      cv::hconcat(g_debugBestInput, g_debugBestRef, sideBySide);
      return sideBySide;
    }
  }
  
  // Stage 4: show difference heat map
  if (stage == 4) {
    if (!g_debugBestDiffMap.empty())
      return g_debugBestDiffMap.clone();
  }
  
  // Stage 5: show difference heat map overlaid on input
  if (stage == 5) {
    if (!g_debugBestInput.empty() && !g_debugBestDiffMap.empty()) {
      cv::Mat overlay;
      cv::addWeighted(g_debugBestInput, 0.5, g_debugBestDiffMap, 0.5, 0, overlay);
      return overlay;
    }
  }
  
  return gemRegion.clone();
}

// ============================================================================
// Shape Classification - Direct Color Image Comparison
// ============================================================================

// Calculate color similarity between two images using LAB color space
// Applies alpha mask to both images to focus comparison on gem pixels only
static float CalculateColorSimilarity(const cv::Mat &img1, const cv::Mat &img2,
                                     const cv::Mat &alphaMask = cv::Mat()) {
if (img1.empty() || img2.empty())
  return 0.0f;

// Clone input so we can modify
cv::Mat i1 = img1.clone();
cv::Mat i2 = img2;
cv::Mat mask = alphaMask;

// Apply scale to reference image and mask (high precision: 980-1020 = 0.980x-1.020x)
float scale = ShapeParams::refScale.load() / 1000.0f;
if (std::abs(scale - 1.0f) > 0.0001f) {
  // Scale the reference image using high-quality Lanczos interpolation
  int scaledW = static_cast<int>(std::round(i2.cols * scale));
  int scaledH = static_cast<int>(std::round(i2.rows * scale));
  cv::Mat scaledRef;
  cv::resize(i2, scaledRef, cv::Size(scaledW, scaledH), 0, 0, cv::INTER_LINEAR);
    
  // Center crop or pad to match input size
  cv::Mat finalRef = cv::Mat::zeros(i1.size(), i2.type());
  int offsetX = (scaledW - i1.cols) / 2;
  int offsetY = (scaledH - i1.rows) / 2;
    
  // Calculate source and destination ROIs
  int srcX = std::max(0, offsetX);
  int srcY = std::max(0, offsetY);
  int dstX = std::max(0, -offsetX);
  int dstY = std::max(0, -offsetY);
  int copyW = std::min(scaledW - srcX, i1.cols - dstX);
  int copyH = std::min(scaledH - srcY, i1.rows - dstY);
    
  if (copyW > 0 && copyH > 0) {
    cv::Rect srcRoi(srcX, srcY, copyW, copyH);
    cv::Rect dstRoi(dstX, dstY, copyW, copyH);
    scaledRef(srcRoi).copyTo(finalRef(dstRoi));
  }
  i2 = finalRef;
    
  // Also scale the mask using nearest neighbor (preserve binary values)
  if (!alphaMask.empty()) {
    cv::Mat scaledMask;
    cv::resize(alphaMask, scaledMask, cv::Size(scaledW, scaledH), 0, 0, cv::INTER_NEAREST);
    cv::Mat finalMask = cv::Mat::zeros(i1.size(), alphaMask.type());
      
    if (copyW > 0 && copyH > 0) {
      cv::Rect srcRoi(srcX, srcY, copyW, copyH);
      cv::Rect dstRoi(dstX, dstY, copyW, copyH);
      scaledMask(srcRoi).copyTo(finalMask(dstRoi));
    }
    mask = finalMask;
  }
} else {
  // No scaling, just resize if sizes don't match
  if (img1.size() != img2.size()) {
    cv::resize(img2, i2, img1.size(), 0, 0, cv::INTER_LINEAR);
  }
    
  // Prepare mask - resize to match if provided
  if (!alphaMask.empty()) {
    if (alphaMask.size() != i1.size()) {
      cv::resize(alphaMask, mask, i1.size(), 0, 0, cv::INTER_NEAREST);
    } else {
      mask = alphaMask.clone();
    }
  }
}

  // Apply mask offset (slider range 0-10 maps to -5 to +5 pixels)
  int offsetX = ShapeParams::maskOffsetX.load() - 5;
  int offsetY = ShapeParams::maskOffsetY.load() - 5;

  if (offsetX != 0 || offsetY != 0) {
    // Create translation matrix and shift the reference image
    cv::Mat shiftedImg = cv::Mat::zeros(i2.size(), i2.type());

    // Calculate source and destination ROIs for the shift
    int srcX = std::max(0, -offsetX);
    int srcY = std::max(0, -offsetY);
    int dstX = std::max(0, offsetX);
    int dstY = std::max(0, offsetY);
    int width = i2.cols - std::abs(offsetX);
    int height = i2.rows - std::abs(offsetY);

    if (width > 0 && height > 0) {
      cv::Rect srcRoi(srcX, srcY, width, height);
      cv::Rect dstRoi(dstX, dstY, width, height);
      i2(srcRoi).copyTo(shiftedImg(dstRoi));
    }
    i2 = shiftedImg;
    
    // Also shift the mask
    if (!mask.empty()) {
      cv::Mat shiftedMask = cv::Mat::zeros(mask.size(), mask.type());
      if (width > 0 && height > 0) {
        cv::Rect srcRoi(srcX, srcY, width, height);
        cv::Rect dstRoi(dstX, dstY, width, height);
        mask(srcRoi).copyTo(shiftedMask(dstRoi));
      }
      mask = shiftedMask;
    }
  }

  // Apply mask to BOTH images (set background pixels to black)
  // This ensures we're comparing the exact same masked region
  if (!mask.empty()) {
    for (int y = 0; y < i1.rows; y++) {
      cv::Vec3b *row1 = i1.ptr<cv::Vec3b>(y);
      cv::Vec3b *row2 = i2.ptr<cv::Vec3b>(y);
      const uchar *maskRow = mask.ptr<uchar>(y);
      
      for (int x = 0; x < i1.cols; x++) {
        if (maskRow[x] == 0) {
          // Background pixel - set to black in both images
          row1[x] = cv::Vec3b(0, 0, 0);
          row2[x] = cv::Vec3b(0, 0, 0);
        }
      }
    }
  }

  // Store masked images for debug visualization
  g_debugMaskedInput = i1.clone();
  g_debugMaskedRef = i2.clone();
  
  // Convert both images to LAB color space for perceptual comparison
  thread_local cv::Mat lab1, lab2;
  cv::cvtColor(i1, lab1, cv::COLOR_BGR2Lab);
  cv::cvtColor(i2, lab2, cv::COLOR_BGR2Lab);

  // Create difference map for visualization
  g_debugDiffMap = cv::Mat::zeros(lab1.rows, lab1.cols, CV_8UC3);

  // Calculate per-pixel color distance (only on non-black pixels)
  double totalDistance = 0.0;
  int pixelCount = 0;

  for (int y = 0; y < lab1.rows; y++) {
    const cv::Vec3b *row1 = lab1.ptr<cv::Vec3b>(y);
    const cv::Vec3b *row2 = lab2.ptr<cv::Vec3b>(y);
    cv::Vec3b *diffRow = g_debugDiffMap.ptr<cv::Vec3b>(y);

    for (int x = 0; x < lab1.cols; x++) {
      // Skip black pixels (background)
      if (row1[x][0] == 0 && row1[x][1] == 128 && row1[x][2] == 128) {
        // This is black in LAB space (L=0, a=128, b=128)
        continue;
      }

      float dL = static_cast<float>(row1[x][0]) - static_cast<float>(row2[x][0]);
      float da = static_cast<float>(row1[x][1]) - static_cast<float>(row2[x][1]);
      float db = static_cast<float>(row1[x][2]) - static_cast<float>(row2[x][2]);

      // LAB color distance
      float dist = std::sqrt(dL * dL + da * da + db * db);
      totalDistance += dist;
      pixelCount++;
      
      // Visualize difference as heat map (black = good match, bright = difference)
      // Only show pixels with significant differences
      if (dist < 10.0f) {
        // Good match - show as dark/black
        diffRow[x] = cv::Vec3b(0, 0, 0);
      } else {
        // Scale difference: 10-100 distance maps to visible colors
        float normalized = std::min((dist - 10.0f) / 90.0f, 1.0f);
        uchar intensity = static_cast<uchar>(normalized * 255.0f);
        
        if (normalized < 0.5f) {
          // Yellow for moderate differences
          diffRow[x] = cv::Vec3b(0, intensity * 2, intensity * 2);
        } else {
          // Red for large differences
          diffRow[x] = cv::Vec3b(0, static_cast<uchar>((1.0f - normalized) * 510.0f), 255);
        }
      }
    }
  }

  if (pixelCount == 0)
    return 0.0f;

  // Average distance per pixel
  double avgDistance = totalDistance / pixelCount;

  // Convert to similarity score (0.0 to 1.0)
  // Max LAB distance is ~184 for pure black to pure white
  // Typical gem-to-gem distance is much smaller (< 50)
  // Normalize and invert: similarity = 1 - (distance / maxExpected)
  const double maxExpectedDistance = 100.0; // Tunable threshold
  double similarity = 1.0 - std::min(avgDistance / maxExpectedDistance, 1.0);

  return static_cast<float>(similarity);
}

// Helper to save current comparison as the best match for debug visualization
static void SaveAsBestMatch() {
  g_debugBestInput = g_debugMaskedInput.clone();
  g_debugBestRef = g_debugMaskedRef.clone();
  g_debugBestDiffMap = g_debugDiffMap.clone();
}

GemType ClassifyGemByShape(const cv::Mat &processedImage, float *outBestMatch,
                           int *outBestFrame) {
  if (processedImage.empty() || !MaskLoader::AreMasksLoaded()) {
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
    const cv::Mat &refImage = MaskLoader::GetMask(type, 0);
    const cv::Mat &alphaMask = MaskLoader::GetAlphaMask(type, 0);
    float score = 0.0f;

    if (!refImage.empty()) {
      score = CalculateColorSimilarity(processedImage, refImage, alphaMask);
    }

    firstFrameScores[i] = {type, score};

    // Track best score so far
    if (score > bestScore) {
      bestScore = score;
      bestType = type;
      bestFrame = 0;
      SaveAsBestMatch();
    }

    // Early exit: if first frame exceeds threshold, we're done
    if (score >= threshold) {
      if (outBestMatch)
        *outBestMatch = score;
      if (outBestFrame)
        *outBestFrame = 0;
      return type;
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
      const cv::Mat &refImage = MaskLoader::GetMask(type, frame);
      const cv::Mat &alphaMask = MaskLoader::GetAlphaMask(type, frame);
      if (refImage.empty())
        continue;

      float score = CalculateColorSimilarity(processedImage, refImage, alphaMask);

      if (score > bestScore) {
        bestScore = score;
        bestType = type;
        bestFrame = frame;
        SaveAsBestMatch();
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
  if (bestScore < 0.4f) {
    return GemType::UNKNOWN;
  }

  return bestType;
}
