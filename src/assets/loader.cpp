#include "loader.h"
#include "bitmasks.h"
#include "frame_processing.h"
#include "hypercube_processing.h"
#include <iostream>
#include <opencv2/opencv.hpp>

namespace MaskLoader {
// Static storage for masks
static FrameProcessing::MaskStorage s_masks;
static FrameProcessing::LabMaskStorage s_labMasks; // Legacy interleaved format
static PlanarLabMaskStorage
    s_planarLabMasks; // Planar format: LLL...AAA...BBB...
static FrameProcessing::AlphaMaskStorage s_alphaMasks;
static FrameProcessing::RingMaskStorage s_ringMasks;
static FrameProcessing::HypercubeMaskStorage s_hypercubeMasks;
static FrameProcessing::HypercubeLabMaskStorage
    s_hypercubeLabMasks;                                        // Legacy format
static HypercubePlanarLabMaskStorage s_hypercubePlanarLabMasks; // Planar format
static FrameProcessing::HypercubeAlphaMaskStorage s_hypercubeAlphaMasks;
static bool s_loaded = false;
static cv::Mat s_emptyMask;

// Static color reference images
static FrameProcessing::MaskStorage s_staticColorMasks;
static bool s_staticColorsLoaded = false;

// Map GemType to filename
static const char *GemTypeToImageFilename(GemType type) {
  switch (type) {
  case GemType::RED:
    return "Red.jp2";
  case GemType::ORANGE:
    return "Orange.jp2";
  case GemType::YELLOW:
    return "Yellow.jp2";
  case GemType::GREEN:
    return "Green.jp2";
  case GemType::BLUE:
    return "Blue.jp2";
  case GemType::PURPLE:
    return "Purple.jp2";
  case GemType::WHITE:
    return "White.jp2";
  case GemType::HYPERCUBE:
    return "hypercube.png";
  default:
    return nullptr;
  }
}

static const char *GemTypeToMaskFilename(GemType type) {
  switch (type) {
  case GemType::RED:
    return "Red_.gif";
  case GemType::ORANGE:
    return "Orange_.gif";
  case GemType::YELLOW:
    return "Yellow_.gif";
  case GemType::GREEN:
    return "Green_.gif";
  case GemType::BLUE:
    return "Blue_.gif";
  case GemType::PURPLE:
    return "Purple_.gif";
  case GemType::WHITE:
    return "White_.gif";
  case GemType::HYPERCUBE:
    return "hypercube.png";
  default:
    return nullptr;
  }
}

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

// Helper: Convert interleaved LAB image to planar format (LLL...AAA...BBB...)
static cv::Mat ConvertToPlanarLab(const cv::Mat &interleavedLab) {
  if (interleavedLab.empty() || interleavedLab.type() != CV_8UC3) {
    return cv::Mat();
  }

  // Create planar format: 3x128x128 single-channel image
  // Layout: [L channel (128x128), A channel (128x128), B channel (128x128)]
  cv::Mat planarLab(3, interleavedLab.rows * interleavedLab.cols, CV_8UC1);

  // Extract each channel separately
  std::vector<cv::Mat> channels(3);
  cv::split(interleavedLab, channels);

  // Copy each channel to its plane
  // L plane at row 0
  channels[0].reshape(0, 1).copyTo(planarLab.row(0));
  // A plane at row 1
  channels[1].reshape(0, 1).copyTo(planarLab.row(1));
  // B plane at row 2
  channels[2].reshape(0, 1).copyTo(planarLab.row(2));

  return planarLab;
}


bool LoadGem(const std::string &assetsPath) {
  bool success = true;

  static const GemType gemOrder[] = {
      GemType::RED,    GemType::WHITE,  GemType::GREEN, GemType::YELLOW,
      GemType::PURPLE, GemType::ORANGE, GemType::BLUE};

  std::string staticPath = assetsPath + "/gems_shadowed.jp2";
  cv::Mat spriteSheet = cv::imread(staticPath, cv::IMREAD_COLOR);
  if (spriteSheet.empty()) {
    std::cerr << "Failed to load static color image: " << staticPath
              << std::endl;
    return false;
  }

  std::string maskPath = assetsPath + "/gems_shadowed_.gif";
  cv::Mat maskSheet = cv::imread(maskPath, cv::IMREAD_UNCHANGED);
  if (maskSheet.empty()) {
    std::cerr << "Failed to load static mask image: " << maskPath << std::endl;
    return false;
  }

  if (maskSheet.channels() == 4) {
    cv::Mat channels[4];
    cv::split(maskSheet, channels);
    maskSheet = channels[0];
  } else if (maskSheet.channels() == 3) {
    cv::cvtColor(maskSheet, maskSheet, cv::COLOR_BGR2GRAY);
  }

  int frameWidth = spriteSheet.cols / NUM_GEM_TYPES;
  int frameHeight = spriteSheet.rows;

  for (int i = 0; i < NUM_GEM_TYPES; i++) {
    GemType type = gemOrder[i];
    int gemIndex = GemTypeToIndex(type);
    if (gemIndex < 0) {
      success = false;
      continue;
    }

    cv::Rect roi(i * frameWidth, 0, frameWidth, frameHeight);
    cv::Mat colorRoi = spriteSheet(roi);
    cv::Mat maskRoi = maskSheet(roi);

    FrameProcessing::ProcessSingleFrame(
        colorRoi, maskRoi, FrameProcessing::GetStaticColorParams(),
        s_masks[gemIndex][0], s_alphaMasks[gemIndex][0],
        s_ringMasks[gemIndex][0], s_labMasks[gemIndex][0]);

    // Convert to planar format for SIMD optimization
    s_planarLabMasks[gemIndex][0] = ConvertToPlanarLab(s_labMasks[gemIndex][0]);

    // Compute bitmask for SIMD optimization
    GetBitmaskStorage()[gemIndex][0] =
        ComputeBitmaskFromLabMask(s_labMasks[gemIndex][0]);
  }

  s_staticColorsLoaded = success;
  return success;
}

bool LoadGemAnimation(const std::string &assetsPath, GemType type) {
  if (type == GemType::HYPERCUBE)
    return true;

  const char *colorFilename = GemTypeToImageFilename(type);
  const char *maskFilename = GemTypeToMaskFilename(type);
  if (!colorFilename || !maskFilename)
    return false;

  int gemIndex = GemTypeToIndex(type);
  if (gemIndex < 0)
    return false;

  // Load the color sprite sheet
  std::string colorPath = assetsPath + "/Gems/" + colorFilename;
  cv::Mat spriteSheet = cv::imread(colorPath, cv::IMREAD_COLOR);
  if (spriteSheet.empty()) {
    std::cerr << "Failed to load reference image: " << colorPath << std::endl;
    return false;
  }

  // Load the alpha mask sprite sheet
  std::string maskPath = assetsPath + "/Gems/" + maskFilename;
  cv::Mat maskSheet = cv::imread(maskPath, cv::IMREAD_UNCHANGED);
  if (maskSheet.empty()) {
    std::cerr << "Failed to load alpha mask: " << maskPath << std::endl;
    maskSheet = cv::Mat::ones(spriteSheet.size(), CV_8UC1) * 255;
  } else {
    // Convert to grayscale if needed
    if (maskSheet.channels() == 4) {
      cv::Mat channels[4];
      cv::split(maskSheet, channels);
      maskSheet = channels[0];
    } else if (maskSheet.channels() == 3) {
      cv::cvtColor(maskSheet, maskSheet, cv::COLOR_BGR2GRAY);
    }
  }

  // Extract each frame from sprite sheet (20 frames go into indices 1-20)
  // Frame 0 is reserved for static image loaded separately
  int frameIndex = 1;
  int frameWidth = spriteSheet.cols / MASK_COLS;
  int frameHeight = spriteSheet.rows / MASK_ROWS;

  for (int row = 0; row < MASK_ROWS; row++) {
    for (int col = 0; col < MASK_COLS; col++) {
      cv::Rect roi(col * frameWidth, row * frameHeight, frameWidth,
                   frameHeight);

      cv::Mat colorRoi = spriteSheet(roi);
      cv::Mat maskRoi = maskSheet(roi);

      // Process the frame using standard gem parameters
      FrameProcessing::ProcessSingleFrame(
          colorRoi, maskRoi, FrameProcessing::GetStandardGemParams(),
          s_masks[gemIndex][frameIndex], s_alphaMasks[gemIndex][frameIndex],
          s_ringMasks[gemIndex][frameIndex], s_labMasks[gemIndex][frameIndex]);

      // Convert to planar format for SIMD optimization
      s_planarLabMasks[gemIndex][frameIndex] =
          ConvertToPlanarLab(s_labMasks[gemIndex][frameIndex]);

      // Compute bitmask for SIMD optimization
      GetBitmaskStorage()[gemIndex][frameIndex] =
          ComputeBitmaskFromLabMask(s_labMasks[gemIndex][frameIndex]);

      frameIndex++;
    }
  }

  std::cout << "Loaded " << (frameIndex - 1) << " animated frames for "
            << GemTypeToString(type) << " with alpha masks" << std::endl;
  return true;
}

bool LoadHypercube(const std::string &assetsPath) {
  const std::string hypercubePath = assetsPath + "/hypercube/";

  // Load color sprite sheet
  std::string colorPath = hypercubePath + "frame.jp2";
  cv::Mat colorSheet = cv::imread(colorPath, cv::IMREAD_COLOR);
  if (colorSheet.empty()) {
    std::cerr << "Failed to load hypercube color image: " << colorPath
              << std::endl;
    return false;
  }

  // Load alpha mask
  std::string maskPath = hypercubePath + "frame_.gif";
  cv::Mat maskSheet = cv::imread(maskPath, cv::IMREAD_UNCHANGED);
  if (maskSheet.empty()) {
    std::cerr << "Failed to load hypercube mask: " << maskPath << std::endl;
    maskSheet = cv::Mat::ones(colorSheet.rows, colorSheet.cols, CV_8UC1) * 255;
  } else {
    if (maskSheet.channels() == 4) {
      cv::Mat channels[4];
      cv::split(maskSheet, channels);
      maskSheet = channels[0];
    } else if (maskSheet.channels() == 3) {
      cv::cvtColor(maskSheet, maskSheet, cv::COLOR_BGR2GRAY);
    }
  }

  // Load glow overlay
  std::string glowPath = hypercubePath + "colorglow.jp2";
  cv::Mat glowSheet = cv::imread(glowPath, cv::IMREAD_COLOR);
  if (glowSheet.empty()) {
    std::cerr << "Warning: Failed to load hypercube glow image: " << glowPath
              << std::endl;
  }

  // Extract each frame from the grid
  int frameIndex = 0;
  int sourceCellWidth = colorSheet.cols / HYPERCUBE_COLS;
  int sourceCellHeight = colorSheet.rows / HYPERCUBE_ROWS;
  int glowCellWidth = glowSheet.empty() ? 0 : glowSheet.cols / HYPERCUBE_COLS;
  int glowCellHeight = glowSheet.empty() ? 0 : glowSheet.rows / HYPERCUBE_ROWS;

  for (int row = 0; row < HYPERCUBE_ROWS; row++) {
    for (int col = 0; col < HYPERCUBE_COLS; col++) {
      HypercubeProcessing::ProcessHypercubeFrame(
          colorSheet, maskSheet, glowSheet, row, col, frameIndex,
          FrameProcessing::GetHypercubeParams(), sourceCellWidth,
          sourceCellHeight, HYPERCUBE_FRAME_WIDTH, HYPERCUBE_FRAME_HEIGHT,
          glowCellWidth, glowCellHeight, s_hypercubeMasks[frameIndex],
          s_hypercubeAlphaMasks[frameIndex], s_hypercubeLabMasks[frameIndex]);

      // Convert to planar format for SIMD optimization
      s_hypercubePlanarLabMasks[frameIndex] =
          ConvertToPlanarLab(s_hypercubeLabMasks[frameIndex]);

      // Compute bitmask for SIMD optimization
      GetHypercubeBitmaskStorage()[frameIndex] =
          ComputeBitmaskFromLabMask(s_hypercubeLabMasks[frameIndex]);

      frameIndex++;
    }
  }

  std::cout << "Loaded " << frameIndex
            << " hypercube frames with centered positioning and glow effects"
            << std::endl;
  return true;
}

bool LoadAllImages(const std::string &assetsPath, int resolution) {
  bool success = true;

  // Build resolution-specific path
  std::string resAssetsPath = assetsPath + "\\" + std::to_string(resolution);

  static const GemType gemTypes[] = {
      GemType::RED,  GemType::ORANGE, GemType::YELLOW, GemType::GREEN,
      GemType::BLUE, GemType::PURPLE, GemType::WHITE};

  if (!LoadGem(resAssetsPath)) {
    success = false;
  }

  for (GemType type : gemTypes) {
    if (!LoadGemAnimation(resAssetsPath, type)) {
      success = false;
    }
  }

  if (!LoadHypercube(resAssetsPath)) {
    success = false;
  }

  s_loaded = success;

  if (success) {
    int totalRefImages = NUM_GEM_TYPES * FRAMES_PER_GEM + HYPERCUBE_FRAMES;
    std::cout << "Loaded " << totalRefImages << " gem reference images from "
              << resAssetsPath << std::endl;

    std::cout << "Precomputed " << totalRefImages
              << " bitmasks for SIMD optimization" << std::endl;
  }

  return success;
}

const MaskStorage &GetMasks() { return s_masks; }

bool AreMasksLoaded() { return s_loaded; }

const cv::Mat &GetMask(GemType type, int frameIndex) {
  if (type == GemType::HYPERCUBE) {
    if (frameIndex < 0 || frameIndex >= HYPERCUBE_FRAMES) {
      return s_emptyMask;
    }
    return s_hypercubeMasks[frameIndex];
  }

  int gemIndex = GemTypeToIndex(type);
  if (gemIndex < 0 || frameIndex < 0 || frameIndex >= FRAMES_PER_GEM) {
    return s_emptyMask;
  }

  return s_masks[gemIndex][frameIndex];
}

const cv::Mat &GetAlphaMask(GemType type, int frameIndex) {
  if (type == GemType::HYPERCUBE) {
    if (frameIndex < 0 || frameIndex >= HYPERCUBE_FRAMES) {
      return s_emptyMask;
    }
    return s_hypercubeAlphaMasks[frameIndex];
  }

  int gemIndex = GemTypeToIndex(type);
  if (gemIndex < 0 || frameIndex < 0 || frameIndex >= FRAMES_PER_GEM) {
    return s_emptyMask;
  }
  return s_alphaMasks[gemIndex][frameIndex];
}

const cv::Mat &GetRingMask(GemType type, int frameIndex) {
  int gemIndex = GemTypeToIndex(type);
  if (gemIndex < 0 || frameIndex < 0 || frameIndex >= FRAMES_PER_GEM) {
    return s_emptyMask;
  }
  return s_ringMasks[gemIndex][frameIndex];
}

const cv::Mat &GetLabMask(GemType type, int frameIndex) {
  if (type == GemType::HYPERCUBE) {
    if (frameIndex < 0 || frameIndex >= HYPERCUBE_FRAMES) {
      return s_emptyMask;
    }
    return s_hypercubeLabMasks[frameIndex];
  }

  int gemIndex = GemTypeToIndex(type);
  if (gemIndex < 0 || frameIndex < 0 || frameIndex >= FRAMES_PER_GEM) {
    return s_emptyMask;
  }
  return s_labMasks[gemIndex][frameIndex];
}

const cv::Mat &GetPlanarLabMask(GemType type, int frameIndex) {
  if (type == GemType::HYPERCUBE) {
    if (frameIndex < 0 || frameIndex >= HYPERCUBE_FRAMES) {
      return s_emptyMask;
    }
    return s_hypercubePlanarLabMasks[frameIndex];
  }

  int gemIndex = GemTypeToIndex(type);
  if (gemIndex < 0 || frameIndex < 0 || frameIndex >= FRAMES_PER_GEM) {
    return s_emptyMask;
  }
  return s_planarLabMasks[gemIndex][frameIndex];
}

int GetFrameCount(GemType type) {
  if (type == GemType::HYPERCUBE) {
    return HYPERCUBE_FRAMES;
  }
  return FRAMES_PER_GEM;
}

} // namespace MaskLoader
