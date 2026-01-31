#include "overlay.h"
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
