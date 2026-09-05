#include "overlay.h"
#include "assets/loader.h"
#include "bot/bot_main.h"
#include "detection/shape_classifier.h"
#include "detection/gradient_classifier.h"
#include "detection/gradient_similarity.h"
#include <array>
#include <iomanip>
#include <sstream>

// ============================================================================
// Debug Overlay Drawing
// ============================================================================

namespace {
int g_selectedCol = -1;
int g_selectedRow = -1;
bool debugSelected = false;
} // namespace

void SetSelectedCell(int col, int row) {
  g_selectedCol = col;
  g_selectedRow = row;
}

void ClearSelectedCell() {
  g_selectedCol = -1;
  g_selectedRow = -1;
}

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
// Overlay Drawing
// ============================================================================

void DrawShapeOverlay(cv::Mat &image, const std::vector<std::vector<Gem>> &grid,
                      const BoardAnalysis *analysis) {
  // Draw grid lines with offset for banner
  for (int i = 0; i <= Config::GRID_SIZE; i++) {
    int pos = i * Config::CELL_SIZE;
    cv::line(image, cv::Point(pos, Config::BANNER_HEIGHT),
             cv::Point(pos, image.rows), cv::Scalar(100, 100, 100), 1);
    cv::line(image, cv::Point(0, Config::BANNER_HEIGHT + pos),
             cv::Point(image.cols, Config::BANNER_HEIGHT + pos),
             cv::Scalar(100, 100, 100), 1);
  }

  // Draw processed masks on top of each gem
  for (int row = 0; row < Config::GRID_SIZE; row++) {
    for (int col = 0; col < Config::GRID_SIZE; col++) {
      const Gem &gem = grid[row][col];

      // Use shapeRoi for display
      cv::Rect offsetRoi = gem.shapeRoi;
      offsetRoi.y += Config::BANNER_HEIGHT;

      // Get the color for this gem type
      cv::Scalar gemColor = GemTypeToColor(gem.type);
      bool isKnown =
          (gem.type != GemType::UNKNOWN && gem.type != GemType::EMPTY);

      int stage = ShapeParams::debugStage.load();

      // Check if we should show gradient debug visualization
      // (stages 2-4 with gradient-only mode or when gradient was used)
      bool showGradientDebug = (stage >= 2 && stage <= 4) &&
                               (ShapeParams::forceGradientOnly ||
                                gem.type == GemType::UNKNOWN);

      // Draw the shape mask or gradient debug
      if (!gem.shapeMask.empty() || showGradientDebug) {
        // Check if the mask is grayscale or color
        bool isColorMask = (gem.shapeMask.channels() == 3);
        int stage = ShapeParams::debugStage.load();

        cv::Mat maskColored;

        // Check if we should show gradient debug visualization
        // (stages 2-4 with gradient-only mode or when gradient was used)
        bool showGradientDebug = (stage >= 2 && stage <= 4) &&
                                 (ShapeParams::forceGradientOnly ||
                                  gem.type == GemType::UNKNOWN);

        if (showGradientDebug) {
          // Generate gradient debug visualization on-demand for this cell
          if (gem.bestMatchFrame >= 0 && gem.type != GemType::UNKNOWN) {
            // Get the matched reference gradient
            const cv::Mat &refGradient = MaskLoader::GetGradientMask(gem.type, gem.bestMatchFrame);
            const cv::Mat &alphaMask = MaskLoader::GetAlphaMask(gem.type, gem.bestMatchFrame);
            const cv::Mat &inputColor = gem.shapeMask; // This is the captured gem

            if (!refGradient.empty() && !inputColor.empty() && !alphaMask.empty()) {
              // Create visualization for this cell based on stage
              switch (stage) {
              case 2: // Show reference gradient
                {
                  cv::Mat refMat = refGradient.row(0).reshape(0, offsetRoi.height);
                  cv::cvtColor(refMat, maskColored, cv::COLOR_GRAY2BGR);
                  if (maskColored.size() != offsetRoi.size()) {
                    cv::resize(maskColored, maskColored, offsetRoi.size(), 0, 0, cv::INTER_NEAREST);
                  }
                }
                break;
              case 3: // Show captured gradient
                {
                  // Apply alpha mask to captured image first
                  cv::Mat maskedInput = inputColor.clone();
                  for (int y = 0; y < maskedInput.rows; y++) {
                    cv::Vec3b* colorRow = maskedInput.ptr<cv::Vec3b>(y);
                    const uchar* maskRow = alphaMask.ptr<uchar>(y);
                    for (int x = 0; x < maskedInput.cols; x++) {
                      if (maskRow[x] == 0) {
                        colorRow[x] = cv::Vec3b(0, 0, 0);
                      }
                    }
                  }

                  // Generate gradient from masked captured image
                  cv::Mat inputGradient = ConvertToPlanarGradient(maskedInput);
                  cv::Mat capturedMat = inputGradient.row(0).reshape(0, offsetRoi.height);

                  // Normalize to match reference gradient range (0-255)
                  cv::normalize(capturedMat, capturedMat, 0, 255, cv::NORM_MINMAX);

                  cv::cvtColor(capturedMat, maskColored, cv::COLOR_GRAY2BGR);
                  if (maskColored.size() != offsetRoi.size()) {
                    cv::resize(maskColored, maskColored, offsetRoi.size(), 0, 0, cv::INTER_NEAREST);
                  }
                }
                break;
              case 4: // Show gradient leftover map (reference - captured)
                {
                  // Generate gradients
                  cv::Mat inputGradient = ConvertToPlanarGradient(inputColor);
                  cv::Mat capturedMat = inputGradient.row(0).reshape(0, offsetRoi.height);
                  cv::Mat refMat = refGradient.row(0).reshape(0, offsetRoi.height);

                  // Create leftover map: reference - captured, clipped at 0
                  cv::Mat leftover = cv::Mat::zeros(offsetRoi.height, offsetRoi.width, CV_8U);
                  int edgeThreshold = GetEdgeThreshold(); // Get current threshold

                  for (int y = 0; y < capturedMat.rows; y++) {
                    const uchar* capRow = capturedMat.ptr<uchar>(y);
                    const uchar* refRow = refMat.ptr<uchar>(y);
                    const uchar* maskRow = alphaMask.empty() ? nullptr : alphaMask.ptr<uchar>(y);
                    uchar* leftoverRow = leftover.ptr<uchar>(y);

                    for (int x = 0; x < capturedMat.cols; x++) {
                      if (maskRow && maskRow[x] == 0) {
                        leftoverRow[x] = 0; // Masked area
                      } else {
                        // Calculate reference - captured, clipped at 0
                        int leftoverVal = int(refRow[x]) - int(capRow[x]);
                        if (leftoverVal < 0) leftoverVal = 0;

                        // Only show pixels above edge threshold in reference
                        if (refRow[x] >= edgeThreshold) {
                          leftoverRow[x] = cv::saturate_cast<uchar>(leftoverVal);
                        } else {
                          leftoverRow[x] = 0; // Below threshold, treat as black
                        }
                      }
                    }
                  }

                  // Convert to color
                  cv::cvtColor(leftover, maskColored, cv::COLOR_GRAY2BGR);
                  if (maskColored.size() != offsetRoi.size()) {
                    cv::resize(maskColored, maskColored, offsetRoi.size(), 0, 0, cv::INTER_NEAREST);
                  }

                  // Add edge threshold text overlay
                  std::ostringstream oss;
                  oss << "Edge Thresh: " << edgeThreshold << "/255";
                  cv::putText(maskColored, oss.str(), cv::Point(5, 20),
                              cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);
                }
                break;
              case 5: // Overlay (similar to color stage 4)
                {
                  cv::Mat refMat = refGradient.row(0).reshape(0, offsetRoi.height);
                  cv::Mat refColor;
                  cv::cvtColor(refMat, refColor, cv::COLOR_GRAY2BGR);

                  cv::Mat input = gem.shapeMask.clone();
                  if (input.size() != offsetRoi.size()) {
                    cv::resize(input, input, offsetRoi.size(), 0, 0, cv::INTER_NEAREST);
                  }
                  if (refColor.size() != offsetRoi.size()) {
                    cv::resize(refColor, refColor, offsetRoi.size(), 0, 0, cv::INTER_NEAREST);
                  }

                  // Blend 50/50 like color debug
                  cv::addWeighted(input, 0.5, refColor, 0.5, 0, maskColored);
                }
                break;
              }
            } else {
              maskColored = cv::Mat::zeros(offsetRoi.size(), CV_8UC3);
            }
          } else {
            maskColored = cv::Mat::zeros(offsetRoi.size(), CV_8UC3);
          }
        } else if (isColorMask) {
          // New color-based system: shapeMask is already a BGR image
          // Apply alpha mask to captured gem for visualization
          cv::Mat capturedGem = gem.shapeMask.clone();

          // Stage 5: Show only ring pixels
          if (stage == 5 && isKnown && gem.bestMatchFrame >= 0 &&
              MaskLoader::AreMasksLoaded()) {
            const cv::Mat &ringMask =
                MaskLoader::GetRingMask(gem.type, gem.bestMatchFrame);

            if (!ringMask.empty()) {
              // Resize ring mask to match captured gem
              cv::Mat mask = ringMask;
              if (mask.size() != capturedGem.size()) {
                cv::resize(ringMask, mask, capturedGem.size(), 0, 0,
                           cv::INTER_NEAREST);
              }

              // Apply ring mask: keep only ring pixels, set rest to black
              for (int y = 0; y < capturedGem.rows; y++) {
                cv::Vec3b *gemRow = capturedGem.ptr<cv::Vec3b>(y);
                const uchar *maskRow = mask.ptr<uchar>(y);

                for (int x = 0; x < capturedGem.cols; x++) {
                  if (maskRow[x] == 0) {
                    // Not in ring - set to black
                    gemRow[x] = cv::Vec3b(0, 0, 0);
                  }
                }
              }
            }
            maskColored = capturedGem.clone();
          } else {
            // Apply regular mask to captured gem if we have a match
            if (isKnown && gem.bestMatchFrame >= 0 &&
                MaskLoader::AreMasksLoaded()) {
              const cv::Mat &alphaMask =
                  MaskLoader::GetAlphaMask(gem.type, gem.bestMatchFrame);

              if (!alphaMask.empty()) {
                // Resize mask to match captured gem
                cv::Mat mask = alphaMask;
                if (mask.size() != capturedGem.size()) {
                  cv::resize(alphaMask, mask, capturedGem.size(), 0, 0,
                             cv::INTER_NEAREST);
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

            // Just show the captured gem (with mask applied)
            maskColored = capturedGem.clone();
          }
        } else {
          // Old binary mask system (for backward compatibility)
          maskColored = cv::Mat::zeros(gem.shapeMask.size(), CV_8UC3);

          // Get reference mask if available
          cv::Mat refMask;
          if (isKnown && gem.bestMatchFrame >= 0 &&
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
            }
          }
        }

        // Resize mask to fit the ROI if needed
        if (maskColored.size() != offsetRoi.size()) {
          cv::resize(maskColored, maskColored, offsetRoi.size(), 0, 0,
                     cv::INTER_NEAREST);
        }

        if (stage == 3 && gem.bestMatchFrame >= 0) {
          std::string frameText = "F" + std::to_string(gem.bestMatchFrame);
          int fontFace = cv::FONT_HERSHEY_SIMPLEX;
          double fontScale = 0.45;
          int thickness = 1;
          cv::Size textSize = cv::getTextSize(frameText, fontFace, fontScale,
                                              thickness, nullptr);
          int textX = (maskColored.cols - textSize.width) / 2;
          int textY = (maskColored.rows + textSize.height) / 2;
          cv::putText(maskColored, frameText, cv::Point(textX, textY), fontFace,
                      fontScale, cv::Scalar(255, 255, 255), thickness,
                      cv::LINE_AA);
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

        int stage = ShapeParams::debugStage.load();

        // Stage 5: Display ring HSV values in center
        if (stage == 5) {
          std::ostringstream oss;
          std::vector<std::string> lines;

          if (gem.ringHSV[0] >= 0) {
            // Add state indicator
            if (gem.state == GemState::FLAME) {
              lines.push_back("FLAME");
            } else if (gem.state == GemState::STAR) {
              lines.push_back("STAR");
            } else if (gem.state == GemState::SUPERNOVA) {
              lines.push_back("SUPERNOVA");
            }

            oss << "H" << gem.ringHSV[0];
            lines.push_back(oss.str());
            // oss.str("");
            // oss << "S" << gem.ringHSV[1];
            // lines.push_back(oss.str());
            oss.str("");
            oss << "V" << gem.ringHSV[2];
            lines.push_back(oss.str());
          } else {
            lines.push_back("---");
          }

          cv::Scalar textColor = (gem.type != GemType::UNKNOWN)
                                     ? GemTypeToColor(gem.type)
                                     : cv::Scalar(160, 160, 160);

          // Center the text in the cell
          int cellCenterX = col * Config::CELL_SIZE + Config::CELL_SIZE / 2;
          int cellCenterY = Config::BANNER_HEIGHT + row * Config::CELL_SIZE +
                            Config::CELL_SIZE / 2;
          int lineHeight = 14;
          int totalHeight = lines.size() * lineHeight;
          int startY = cellCenterY - totalHeight / 2 +
                       lineHeight; // Add lineHeight to account for baseline

          for (size_t i = 0; i < lines.size(); ++i) {
            cv::Size textSize = cv::getTextSize(lines[i], fontFace, fontScale,
                                                thickness, nullptr);
            int textStartX = cellCenterX - textSize.width / 2;
            cv::Point pos(textStartX, startY + i * lineHeight);
            cv::putText(image, lines[i], pos, fontFace, fontScale, textColor,
                        thickness, cv::LINE_AA);
          }
          continue;
        }

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

  // Draw selected cell highlight on top
  if (Config::SHOW_BOT_MATCH_LINES && ShapeParams::debugStage.load() >= 1) {
    // Use analysis if available and has scored swaps, otherwise fall back to
    // grid
    if (analysis && !analysis->scoredSwaps.empty()) {
      DrawMatchableSwaps(image, *analysis);
    } else {
      DrawMatchableSwaps(image, grid);
    }
  }

  if (debugSelected && g_selectedCol >= 0 && g_selectedRow >= 0) {
    cv::Rect selRect(g_selectedCol * Config::CELL_SIZE,
                     Config::BANNER_HEIGHT + g_selectedRow * Config::CELL_SIZE,
                     Config::CELL_SIZE, Config::CELL_SIZE);
    cv::rectangle(image, selRect, cv::Scalar(0, 255, 255), 3);

    // Draw size reference square in center
    int refSquareWidth = Config::FRAME_DIFF_REGION_WIDTH;
    int refSquareHeight = Config::FRAME_DIFF_REGION_HEIGHT;
    int borderThickness = 2;
    int drawWidth =
        refSquareWidth + 2 * borderThickness; // Account for border width
    int drawHeight = refSquareHeight + 2 * borderThickness;
    int centerX = selRect.x + selRect.width / 2;
    int centerY = selRect.y + selRect.height / 2;
    int refX = centerX - drawWidth / 2;
    int refY = centerY - drawHeight / 2;

    cv::rectangle(image, cv::Rect(refX, refY, drawWidth, drawHeight),
                  cv::Scalar(0, 255, 255), borderThickness);
  }
}