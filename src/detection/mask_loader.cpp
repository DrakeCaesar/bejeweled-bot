#include "mask_loader.h"
#include <iostream>

namespace MaskLoader {

// Static storage for masks
static MaskStorage s_masks;
static AlphaMaskStorage s_alphaMasks;
static bool s_loaded = false;
static cv::Mat s_emptyMask;

// Map GemType to filename
static const char *GemTypeToFilename(GemType type) {
  switch (type) {
  case GemType::RED:
    return "Red.png";
  case GemType::ORANGE:
    return "Orange.png";
  case GemType::YELLOW:
    return "Yellow.png";
  case GemType::GREEN:
    return "Green.png";
  case GemType::BLUE:
    return "Blue.png";
  case GemType::PURPLE:
    return "Purple.png";
  case GemType::WHITE:
    return "White.png";
  default:
    return nullptr;
  }
}

// Convert GemType to storage index (0-6)
static int GemTypeToIndex(GemType type) {
  switch (type) {
  case GemType::RED:
    return 0;
  case GemType::ORANGE:
    return 1;
  case GemType::YELLOW:
    return 2;
  case GemType::GREEN:
    return 3;
  case GemType::BLUE:
    return 4;
  case GemType::PURPLE:
    return 5;
  case GemType::WHITE:
    return 6;
  default:
    return -1;
  }
}

bool LoadMasksForGem(const std::string &assetsPath, GemType type) {
  const char *filename = GemTypeToFilename(type);
  if (!filename)
    return false;

  int gemIndex = GemTypeToIndex(type);
  if (gemIndex < 0)
    return false;

  // Load the color sprite sheet
  std::string colorPath = assetsPath + "/color/" + filename;
  cv::Mat spriteSheet = cv::imread(colorPath, cv::IMREAD_COLOR);
  if (spriteSheet.empty()) {
    std::cerr << "Failed to load reference image: " << colorPath << std::endl;
    return false;
  }

  // Load the alpha mask sprite sheet
  std::string maskPath = assetsPath + "/mask/" + filename;
  cv::Mat maskSheet = cv::imread(maskPath, cv::IMREAD_UNCHANGED);
  if (maskSheet.empty()) {
    std::cerr << "Failed to load alpha mask: " << maskPath << std::endl;
    // Continue without masks - use full images
    maskSheet = cv::Mat::ones(spriteSheet.size(), CV_8UC1) * 255;
  } else {
    // Convert to grayscale if needed
    if (maskSheet.channels() == 4) {
      // RGBA image - extract first channel (they're all the same for B&W)
      cv::Mat channels[4];
      cv::split(maskSheet, channels);
      maskSheet = channels[0]; // Use any channel (R, G, or B - they're identical for B&W)
    } else if (maskSheet.channels() == 3) {
      // BGR image - convert to grayscale
      cv::cvtColor(maskSheet, maskSheet, cv::COLOR_BGR2GRAY);
    }
    // If already 1 channel, use as-is
  }

  // Verify dimensions
  if (spriteSheet.cols != SPRITE_WIDTH || spriteSheet.rows != SPRITE_HEIGHT) {
    std::cerr << "Unexpected sprite sheet size for " << colorPath << ": "
              << spriteSheet.cols << "x" << spriteSheet.rows << " (expected "
              << SPRITE_WIDTH << "x" << SPRITE_HEIGHT << ")" << std::endl;
    return false;
  }

  if (maskSheet.cols != SPRITE_WIDTH || maskSheet.rows != SPRITE_HEIGHT) {
    std::cerr << "Unexpected mask sheet size for " << maskPath << ": "
              << maskSheet.cols << "x" << maskSheet.rows << " (expected "
              << SPRITE_WIDTH << "x" << SPRITE_HEIGHT << ")" << std::endl;
    // Resize mask to match color sheet
    cv::resize(maskSheet, maskSheet, cv::Size(SPRITE_WIDTH, SPRITE_HEIGHT), 0, 0, cv::INTER_NEAREST);
  }

  // Extract each frame
  int frameIndex = 0;
  for (int row = 0; row < MASK_ROWS; row++) {
    for (int col = 0; col < MASK_COLS; col++) {
      // Calculate ROI for this frame
      cv::Rect roi(col * FRAME_WIDTH, row * FRAME_HEIGHT, FRAME_WIDTH,
                   FRAME_HEIGHT);

      // Extract and clone the color frame
      cv::Mat colorFrame = spriteSheet(roi).clone();
      
      // Extract and clone the mask frame
      cv::Mat maskFrame = maskSheet(roi).clone();
      
      // Threshold mask to ensure binary (white = foreground, black = background)
      cv::threshold(maskFrame, maskFrame, 127, 255, cv::THRESH_BINARY);
      
      // Apply mask to color frame (set background to black)
      for (int y = 0; y < colorFrame.rows; y++) {
        cv::Vec3b *colorRow = colorFrame.ptr<cv::Vec3b>(y);
        const uchar *maskRow = maskFrame.ptr<uchar>(y);
        
        for (int x = 0; x < colorFrame.cols; x++) {
          if (maskRow[x] == 0) {
            // Background pixel - set to black
            colorRow[x] = cv::Vec3b(0, 0, 0);
          }
        }
      }

      s_masks[gemIndex][frameIndex] = colorFrame;
      s_alphaMasks[gemIndex][frameIndex] = maskFrame;
      frameIndex++;
    }
  }

  std::cout << "Loaded " << frameIndex << " reference images for "
            << GemTypeToString(type) << " with alpha masks" << std::endl;
  return true;
}

bool LoadAllMasks(const std::string &assetsPath) {
  bool success = true;

  // Load masks for each gem type
  static const GemType gemTypes[] = {
      GemType::RED,  GemType::ORANGE, GemType::YELLOW, GemType::GREEN,
      GemType::BLUE, GemType::PURPLE, GemType::WHITE};

  for (GemType type : gemTypes) {
    if (!LoadMasksForGem(assetsPath, type)) {
      success = false;
    }
  }

  s_loaded = success;

  if (success) {
    std::cout << "Loaded " << (NUM_GEM_TYPES * FRAMES_PER_GEM)
              << " gem reference images" << std::endl;
  }

  return success;
}

const MaskStorage &GetMasks() { return s_masks; }

bool AreMasksLoaded() { return s_loaded; }

const cv::Mat &GetMask(GemType type, int frameIndex) {
  int gemIndex = GemTypeToIndex(type);
  if (gemIndex < 0 || frameIndex < 0 || frameIndex >= FRAMES_PER_GEM) {
    return s_emptyMask;
  }
  return s_masks[gemIndex][frameIndex];
}

const cv::Mat &GetAlphaMask(GemType type, int frameIndex) {
  int gemIndex = GemTypeToIndex(type);
  if (gemIndex < 0 || frameIndex < 0 || frameIndex >= FRAMES_PER_GEM) {
    return s_emptyMask;
  }
  return s_alphaMasks[gemIndex][frameIndex];
}

// ============================================================================
// Scaled Color Image Cache - Pre-scaled color images for current settings
// ============================================================================

static MaskStorage s_scaledMasks;
static int s_cachedScaleInt = 0;  // 0 = not initialized
static int s_cachedBlurSize = 0;
static cv::Size s_cachedTargetSize{0, 0};

void EnsureScaledCache(cv::Size targetSize, int scaleInt, int blurSize) {
  // Already valid and matches?
  if (scaleInt == s_cachedScaleInt && targetSize == s_cachedTargetSize && 
      blurSize == s_cachedBlurSize) {
    return;
  }
  
  if (!s_loaded) return;
  
  s_cachedScaleInt = scaleInt;
  s_cachedTargetSize = targetSize;
  s_cachedBlurSize = blurSize;
  
  float scale = scaleInt / 1000.0f;
  float centerX = targetSize.width / 2.0f;
  float centerY = targetSize.height / 2.0f;
  cv::Mat colorTransform = cv::getRotationMatrix2D(cv::Point2f(centerX, centerY), 0, scale);
  
  // Ensure blur size is odd and >= 1
  int blur = (blurSize < 1) ? 1 : (blurSize | 1);
  
  for (int gemIdx = 0; gemIdx < NUM_GEM_TYPES; gemIdx++) {
    for (int frame = 0; frame < FRAMES_PER_GEM; frame++) {
      const cv::Mat &srcMask = s_masks[gemIdx][frame];
      
      if (srcMask.empty()) continue;
      
      // Resize color image to target size first
      cv::Mat resized;
      if (srcMask.size() != targetSize) {
        cv::resize(srcMask, resized, targetSize, 0, 0, cv::INTER_LANCZOS4);
      } else {
        resized = srcMask.clone();
      }
      
      // Apply Gaussian blur to match game's softer appearance
      if (blur > 1) {
        cv::GaussianBlur(resized, resized, cv::Size(blur, blur), 0);
      }
      
      // Apply scale transformation to color image
      cv::warpAffine(resized, s_scaledMasks[gemIdx][frame], colorTransform, targetSize,
                     cv::INTER_LANCZOS4, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
    }
  }
  
  std::cout << "Rebuilt color cache: " << targetSize.width << "x" << targetSize.height 
            << " @ " << (scale * 100.0f) << "%, blur=" << blur << std::endl;
}

const cv::Mat &GetScaledMask(GemType type, int frameIndex) {
  int gemIndex = GemTypeToIndex(type);
  if (gemIndex < 0 || frameIndex < 0 || frameIndex >= FRAMES_PER_GEM) {
    return s_emptyMask;
  }
  const cv::Mat &scaled = s_scaledMasks[gemIndex][frameIndex];
  return scaled.empty() ? s_masks[gemIndex][frameIndex] : scaled;
}

} // namespace MaskLoader
