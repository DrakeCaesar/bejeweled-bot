#include "overlay.h"
#include "../detection/mask_loader.h"
#include "../detection/shape_classifier.h"
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
        // Check if the mask is grayscale or color
        bool isColorMask = (gem.shapeMask.channels() == 3);
        
        cv::Mat maskColored;
        
        if (isColorMask) {
          // New color-based system: shapeMask is already a BGR image
          // Apply alpha mask to captured gem for visualization
          cv::Mat capturedGem = gem.shapeMask.clone();
          
          // Apply mask to captured gem if we have a match
          if (isKnown && gem.bestMatchFrame >= 0 && MaskLoader::AreMasksLoaded()) {
            const cv::Mat &alphaMask = MaskLoader::GetAlphaMask(gem.type, gem.bestMatchFrame);
            
            if (!alphaMask.empty()) {
              // Resize mask to match captured gem
              cv::Mat mask = alphaMask;
              if (mask.size() != capturedGem.size()) {
                cv::resize(alphaMask, mask, capturedGem.size(), 0, 0, cv::INTER_NEAREST);
              }
              
              // Apply mask: set background pixels to black
              for (int y = 0; y < capturedGem.rows; y++) {
                cv::Vec3b *gemRow = capturedGem.ptr<cv::Vec3b>(y);
                const uchar *maskRow = mask.ptr<uchar>(y);
                
                for (int x = 0; x < capturedGem.cols; x++) {
                  if (maskRow[x] == 0) {
                    // Background pixel - set to black
                    gemRow[x] = cv::Vec3b(0, 0, 0);
                  }
                }
              }
            }
          }
          
          // Show comparison with reference if available
          int stage = ShapeParams::debugStage.load();
          bool showComparison = (stage == 0);
          
          if (showComparison && isKnown && gem.bestMatchFrame >= 0 &&
              MaskLoader::AreMasksLoaded()) {
            // Get the best matching reference image
            cv::Mat refImage = MaskLoader::GetMask(gem.type, gem.bestMatchFrame);
            
            if (!refImage.empty()) {
              // Resize reference to match gem size
              if (refImage.size() != capturedGem.size()) {
                cv::resize(refImage, refImage, capturedGem.size(), 0, 0,
                           cv::INTER_LINEAR);
              }
              
              // Create side-by-side comparison
              int w = capturedGem.cols;
              int h = capturedGem.rows;
              maskColored = cv::Mat::zeros(h, w * 2 + 4, CV_8UC3);
              
              // Left: captured gem (with mask applied)
              capturedGem.copyTo(maskColored(cv::Rect(0, 0, w, h)));
              
              // Middle: divider line
              cv::line(maskColored, cv::Point(w + 2, 0), cv::Point(w + 2, h),
                       cv::Scalar(255, 255, 0), 2);
              
              // Right: reference image (already has mask applied)
              refImage.copyTo(maskColored(cv::Rect(w + 4, 0, w, h)));
              
              // Add labels
              cv::putText(maskColored, "Captured", cv::Point(2, 12),
                          cv::FONT_HERSHEY_SIMPLEX, 0.35, cv::Scalar(0, 255, 255),
                          1, cv::LINE_AA);
              cv::putText(maskColored, "Reference", cv::Point(w + 6, 12),
                          cv::FONT_HERSHEY_SIMPLEX, 0.35, cv::Scalar(0, 255, 255),
                          1, cv::LINE_AA);
            } else {
              maskColored = capturedGem.clone();
            }
          } else {
            // Just show the captured gem (with mask applied)
            maskColored = capturedGem.clone();
          }
        } else {
          // Old binary mask system (for backward compatibility)
          maskColored = cv::Mat::zeros(gem.shapeMask.size(), CV_8UC3);

          // Only show reference mask comparison when debugStage is 0 (final) or 10
          int stage = ShapeParams::debugStage.load();
          bool showComparison = (stage == 0 || stage == 10);

          // Get reference mask if available and comparison is enabled
          cv::Mat refMask;
          if (showComparison && isKnown && gem.bestMatchFrame >= 0 &&
              MaskLoader::AreMasksLoaded()) {
            refMask = MaskLoader::GetMask(gem.type, gem.bestMatchFrame);
            // Resize to match computed mask size
            if (!refMask.empty() && refMask.size() != gem.shapeMask.size()) {
              cv::resize(refMask, refMask, gem.shapeMask.size(), 0, 0,
                         cv::INTER_NEAREST);
            }
          }

          for (int y = 0; y < gem.shapeMask.rows; y++) {
            for (int x = 0; x < gem.shapeMask.cols; x++) {
              bool inComputed = gem.shapeMask.at<uchar>(y, x) > 127;

              if (showComparison) {
                bool inRef = !refMask.empty() && refMask.at<uchar>(y, x) > 127;

                if (inComputed && inRef) {
                  // Overlap (good match)
                  maskColored.at<cv::Vec3b>(y, x) = cv::Vec3b(0, 0, 255);
                } else if (inComputed) {
                  // Computed only (extra pixels)
                  maskColored.at<cv::Vec3b>(y, x) = cv::Vec3b(0, 255, 0);
                } else if (inRef) {
                  // Magenta - reference only (missing pixels)
                  maskColored.at<cv::Vec3b>(y, x) = cv::Vec3b(255, 0, 255);
                }
              } else {
                // Debug mode - just show the mask in white
                if (inComputed) {
                  maskColored.at<cv::Vec3b>(y, x) = cv::Vec3b(255, 255, 255);
                }
              }
            }
          }
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
}
