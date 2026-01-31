#include "mask_loader.h"
#include <iostream>

namespace MaskLoader {

// Static storage for masks
static MaskStorage s_masks;
static bool s_loaded = false;
static cv::Mat s_emptyMask;

// Map GemType to filename
static const char *GemTypeToFilename(GemType type) {
  switch (type) {
  case GemType::RED:
    return "Red_.png";
  case GemType::ORANGE:
    return "Orange_.png";
  case GemType::YELLOW:
    return "Yellow_.png";
  case GemType::GREEN:
    return "Green_.png";
  case GemType::BLUE:
    return "Blue_.png";
  case GemType::PURPLE:
    return "Purple_.png";
  case GemType::WHITE:
    return "White_.png";
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

  std::string filepath = assetsPath + "/Gems/" + filename;

  // Load the sprite sheet
  cv::Mat spriteSheet = cv::imread(filepath, cv::IMREAD_GRAYSCALE);
  if (spriteSheet.empty()) {
    std::cerr << "Failed to load mask: " << filepath << std::endl;
    return false;
  }

  // Verify dimensions
  if (spriteSheet.cols != SPRITE_WIDTH || spriteSheet.rows != SPRITE_HEIGHT) {
    std::cerr << "Unexpected sprite sheet size for " << filepath << ": "
              << spriteSheet.cols << "x" << spriteSheet.rows << " (expected "
              << SPRITE_WIDTH << "x" << SPRITE_HEIGHT << ")" << std::endl;
    return false;
  }

  // Extract each frame
  int frameIndex = 0;
  for (int row = 0; row < MASK_ROWS; row++) {
    for (int col = 0; col < MASK_COLS; col++) {
      // Calculate ROI for this frame
      cv::Rect roi(col * FRAME_WIDTH, row * FRAME_HEIGHT, FRAME_WIDTH,
                   FRAME_HEIGHT);

      // Extract and clone the frame
      cv::Mat frame = spriteSheet(roi).clone();

      // Ensure white on black: if the image is black on white, invert it
      // Check by looking at corners (background should be black)
      double cornerSum = frame.at<uchar>(0, 0) +
                         frame.at<uchar>(0, FRAME_WIDTH - 1) +
                         frame.at<uchar>(FRAME_HEIGHT - 1, 0) +
                         frame.at<uchar>(FRAME_HEIGHT - 1, FRAME_WIDTH - 1);
      if (cornerSum > 512) {
        // Corners are mostly white, so background is white - invert
        cv::bitwise_not(frame, frame);
      }

      // Threshold to ensure binary (in case of anti-aliasing in GIF)
      cv::threshold(frame, frame, 127, 255, cv::THRESH_BINARY);

      s_masks[gemIndex][frameIndex] = frame;
      frameIndex++;
    }
  }

  std::cout << "Loaded " << frameIndex << " masks for " << GemTypeToString(type)
            << std::endl;
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
    std::cout << "Loaded " << (NUM_GEM_TYPES * FRAMES_PER_GEM) << " gem masks"
              << std::endl;
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

} // namespace MaskLoader
