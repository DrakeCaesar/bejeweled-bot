#include "overlay.h"
#include "../detection/mask_loader.h"
#include <array>
#include <iomanip>
#include <sstream>

// ============================================================================
// Debug Overlay Drawing
// ============================================================================

void DrawDebugOverlay(cv::Mat &image,
                      const std::vector<std::vector<Gem>> &grid) {
  // Draw grid lines with offset for banner
  for (int i = 0; i <= Config::GRID_SIZE; i++) {
    int pos = i * Config::CELL_SIZE;
    cv::line(image, cv::Point(pos, Config::BANNER_HEIGHT),
             cv::Point(pos, image.rows), cv::Scalar(100, 100, 100), 1);
    cv::line(image, cv::Point(0, Config::BANNER_HEIGHT + pos),
             cv::Point(image.cols, Config::BANNER_HEIGHT + pos),
             cv::Scalar(100, 100, 100), 1);
  }

  // Draw gem information with offset for banner
  for (int row = 0; row < Config::GRID_SIZE; row++) {
    for (int col = 0; col < Config::GRID_SIZE; col++) {
      const Gem &gem = grid[row][col];

      // Draw center ROI with offset
      cv::Rect offsetRoi = gem.roi;
      offsetRoi.y += Config::BANNER_HEIGHT;
      cv::rectangle(image, offsetRoi, cv::Scalar(0, 255, 0), 2);

      // Draw gem type label with offset
      {
        int textX = col * Config::CELL_SIZE + 5;
        int textY = Config::BANNER_HEIGHT + row * Config::CELL_SIZE + 18;
        int lineHeight = 16;

        // Get HSV value for this gem - reuse static buffers
        cv::Scalar meanBGR = gem.avgColor;
        static thread_local cv::Mat tmpBGR(1, 1, CV_8UC3);
        static thread_local cv::Mat tmpHSV(1, 1, CV_8UC3);
        tmpBGR.at<cv::Vec3b>(0, 0) = cv::Vec3b(static_cast<uchar>(meanBGR[0]),
                                               static_cast<uchar>(meanBGR[1]),
                                               static_cast<uchar>(meanBGR[2]));
        cv::cvtColor(tmpBGR, tmpHSV, cv::COLOR_BGR2HSV);
        cv::Vec3b hsvVal = tmpHSV.at<cv::Vec3b>(0, 0);

        // Prepare text lines - static allocation with fixed-size array
        static thread_local std::array<std::string, 5> lines;
        static thread_local std::ostringstream oss;
        int lineCount = 0;
        cv::Scalar textColor;

        if (gem.matchedThreshold && gem.type != GemType::UNKNOWN) {
          lines[lineCount++] = GemTypeToString(gem.type);
          oss.str("");
          oss << std::fixed << std::setprecision(0) << (gem.bestMatch * 100.0f)
              << "%";
          lines[lineCount++] = oss.str();
          textColor = GemTypeToColor(gem.type);
        } else {
          lines[lineCount++] = "CLOSE:";
          lines[lineCount++] = GemTypeToString(gem.closestType);
          oss.str("");
          oss << std::fixed << std::setprecision(0) << (gem.bestMatch * 100.0f)
              << "%";
          lines[lineCount++] = oss.str();
          textColor = (gem.closestType != GemType::UNKNOWN)
                          ? GemTypeToColor(gem.closestType)
                          : cv::Scalar(160, 160, 160);
        }

        oss.str("");
        oss << "H" << (int)hsvVal[0] << " S" << (int)hsvVal[1];
        lines[lineCount++] = oss.str();
        oss.str("");
        oss << "V" << (int)hsvVal[2];
        lines[lineCount++] = oss.str();

        // Calculate background size
        int maxWidth = 0;
        int fontFace = cv::FONT_HERSHEY_SIMPLEX;
        double fontScale = 0.45;
        int thickness = 1;
        int baseline = 0;

        for (int i = 0; i < lineCount; i++) {
          cv::Size textSize = cv::getTextSize(lines[i], fontFace, fontScale,
                                              thickness, &baseline);
          maxWidth = std::max(maxWidth, textSize.width);
        }

        int bgPadding = 3;
        cv::Rect bgRect(textX - bgPadding, textY - 12, maxWidth + bgPadding * 2,
                        lineCount * lineHeight + bgPadding * 2);
        cv::rectangle(image, bgRect, cv::Scalar(20, 20, 20), -1);
        cv::rectangle(image, bgRect, cv::Scalar(60, 60, 60), 1);

        // Draw text lines with anti-aliasing
        for (int i = 0; i < lineCount; i++) {
          cv::Point pos(textX, textY + i * lineHeight);
          cv::putText(image, lines[i], pos, fontFace, fontScale, textColor,
                      thickness, cv::LINE_AA);
        }
      }
    }
  }
}

// ============================================================================
// Shape Mode Overlay Drawing
// ============================================================================

void DrawShapeOverlay(cv::Mat &image,
                      const std::vector<std::vector<Gem>> &grid) {
  // Draw grid lines with offset for banner
  for (int i = 0; i <= Config::GRID_SIZE; i++) {
    int pos = i * Config::CELL_SIZE;
    cv::line(image, cv::Point(pos, Config::BANNER_HEIGHT),
             cv::Point(pos, image.rows), cv::Scalar(100, 100, 100), 1);
    cv::line(image, cv::Point(0, Config::BANNER_HEIGHT + pos),
             cv::Point(image.cols, Config::BANNER_HEIGHT + pos),
             cv::Scalar(100, 100, 100), 1);
  }

  // Draw processed shape masks on top of each gem
  for (int row = 0; row < Config::GRID_SIZE; row++) {
    for (int col = 0; col < Config::GRID_SIZE; col++) {
      const Gem &gem = grid[row][col];

      // Use shapeRoi (larger) for shape mode display
      cv::Rect offsetRoi = gem.shapeRoi;
      offsetRoi.y += Config::BANNER_HEIGHT;

      // Get the color for this gem type
      cv::Scalar gemColor = GemTypeToColor(gem.type);
      bool isKnown =
          (gem.type != GemType::UNKNOWN && gem.type != GemType::EMPTY);

      // Draw the shape mask if available
      if (!gem.shapeMask.empty()) {
        // Convert grayscale mask to colored version based on detected type
        cv::Mat maskColored;

        if (isKnown) {
          // Create a colored mask using the gem's color
          cv::Mat maskBGR;
          cv::cvtColor(gem.shapeMask, maskBGR, cv::COLOR_GRAY2BGR);

          // Blend the gem color with the white mask
          maskColored = cv::Mat::zeros(maskBGR.size(), CV_8UC3);
          for (int y = 0; y < maskBGR.rows; y++) {
            for (int x = 0; x < maskBGR.cols; x++) {
              if (gem.shapeMask.at<uchar>(y, x) > 127) {
                maskColored.at<cv::Vec3b>(y, x) =
                    cv::Vec3b(static_cast<uchar>(gemColor[0]),
                              static_cast<uchar>(gemColor[1]),
                              static_cast<uchar>(gemColor[2]));
              }
            }
          }
        } else {
          // Keep white mask for unknown gems
          cv::cvtColor(gem.shapeMask, maskColored, cv::COLOR_GRAY2BGR);
        }

        // Resize mask to fit the ROI if needed
        if (maskColored.size() != offsetRoi.size()) {
          cv::resize(maskColored, maskColored, offsetRoi.size(), 0, 0,
                     cv::INTER_NEAREST);
        }

        // Copy the mask onto the image at the gem's position
        maskColored.copyTo(image(offsetRoi));

        // Draw a colored border around the mask
        cv::Scalar borderColor = isKnown ? gemColor : cv::Scalar(100, 100, 100);
        cv::rectangle(image, offsetRoi, borderColor, 2);
      } else {
        // Draw outline even if no mask
        cv::rectangle(image, offsetRoi, cv::Scalar(0, 255, 0), 1);
      }

      // Draw gem type label and match percentage
      {
        int textX = col * Config::CELL_SIZE + 5;
        int textY = Config::BANNER_HEIGHT + row * Config::CELL_SIZE + 16;

        int fontFace = cv::FONT_HERSHEY_SIMPLEX;
        double fontScale = 0.4;
        int thickness = 1;

        // Background for text
        std::string typeName = GemTypeToString(gem.type);
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(0) << (gem.bestMatch * 100.0f)
            << "%";
        std::string matchStr = oss.str();

        cv::Size typeSize =
            cv::getTextSize(typeName, fontFace, fontScale, thickness, nullptr);
        cv::Size matchSize =
            cv::getTextSize(matchStr, fontFace, fontScale, thickness, nullptr);
        int bgWidth = std::max(typeSize.width, matchSize.width) + 6;

        cv::Rect bgRect(textX - 3, textY - 12, bgWidth, 30);
        cv::rectangle(image, bgRect, cv::Scalar(20, 20, 20), -1);
        cv::rectangle(image, bgRect, cv::Scalar(60, 60, 60), 1);

        // Draw text
        cv::Scalar textColor = isKnown ? gemColor : cv::Scalar(160, 160, 160);
        cv::putText(image, typeName, cv::Point(textX, textY), fontFace,
                    fontScale, textColor, thickness, cv::LINE_AA);
        cv::putText(image, matchStr, cv::Point(textX, textY + 14), fontFace,
                    fontScale, textColor, thickness, cv::LINE_AA);
      }
    }
  }

  // DEBUG: Draw the first 64 loaded masks on top of the grid to verify loading
  if (MaskLoader::AreMasksLoaded()) {
    static const GemType gemTypes[] = {
        GemType::RED,  GemType::ORANGE, GemType::YELLOW, GemType::GREEN,
        GemType::BLUE, GemType::PURPLE, GemType::WHITE};

    int maskIdx = 0;
    for (int row = 0; row < Config::GRID_SIZE && maskIdx < 64; row++) {
      for (int col = 0; col < Config::GRID_SIZE && maskIdx < 64; col++) {
        // Calculate which gem type and frame this slot represents
        int gemTypeIdx = maskIdx / MaskLoader::FRAMES_PER_GEM;
        int frameIdx = maskIdx % MaskLoader::FRAMES_PER_GEM;

        if (gemTypeIdx >= 7)
          break;

        const cv::Mat &mask =
            MaskLoader::GetMask(gemTypes[gemTypeIdx], frameIdx);

        if (!mask.empty()) {
          // Draw in top-left corner of each cell
          int cellX = col * Config::CELL_SIZE;
          int cellY = Config::BANNER_HEIGHT + row * Config::CELL_SIZE;

          // Scale mask to 64x64 for display
          cv::Mat smallMask;
          cv::resize(mask, smallMask, cv::Size(64, 64), 0, 0,
                     cv::INTER_NEAREST);

          cv::Mat maskBGR;
          cv::cvtColor(smallMask, maskBGR, cv::COLOR_GRAY2BGR);

          // Tint with gem color
          cv::Scalar color = GemTypeToColor(gemTypes[gemTypeIdx]);
          for (int y = 0; y < maskBGR.rows; y++) {
            for (int x = 0; x < maskBGR.cols; x++) {
              if (smallMask.at<uchar>(y, x) > 127) {
                maskBGR.at<cv::Vec3b>(y, x) = cv::Vec3b(
                    static_cast<uchar>(color[0]), static_cast<uchar>(color[1]),
                    static_cast<uchar>(color[2]));
              }
            }
          }

          // Copy to image
          cv::Rect destRoi(cellX, cellY, 64, 64);
          maskBGR.copyTo(image(destRoi));

          // Draw frame number
          cv::putText(image, std::to_string(frameIdx),
                      cv::Point(cellX + 2, cellY + 12),
                      cv::FONT_HERSHEY_SIMPLEX, 0.35, cv::Scalar(255, 255, 255),
                      1, cv::LINE_AA);
        }
        maskIdx++;
      }
    }
  } else {
    // Draw warning if masks not loaded
    cv::putText(
        image, "MASKS NOT LOADED", cv::Point(10, Config::BANNER_HEIGHT + 30),
        cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 255), 2, cv::LINE_AA);
  }
}
