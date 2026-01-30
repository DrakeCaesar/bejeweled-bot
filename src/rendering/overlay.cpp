#include "overlay.h"
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

        // Get HSV value for this gem
        cv::Scalar meanBGR = gem.avgColor;
        cv::Mat tmpBGR(1, 1, CV_8UC3, meanBGR);
        cv::Mat tmpHSV;
        cv::cvtColor(tmpBGR, tmpHSV, cv::COLOR_BGR2HSV);
        cv::Vec3b hsvVal = tmpHSV.at<cv::Vec3b>(0, 0);

        // Prepare text lines
        std::vector<std::string> lines;
        cv::Scalar textColor;

        if (gem.matchedThreshold && gem.type != GemType::UNKNOWN) {
          lines.push_back(GemTypeToString(gem.type));
          std::ostringstream line2;
          line2 << std::fixed << std::setprecision(0)
                << (gem.bestMatch * 100.0f) << "%";
          lines.push_back(line2.str());
          textColor = GemTypeToColor(gem.type);
        } else {
          lines.push_back("CLOSE:");
          lines.push_back(GemTypeToString(gem.closestType));
          std::ostringstream line3;
          line3 << std::fixed << std::setprecision(0)
                << (gem.bestMatch * 100.0f) << "%";
          lines.push_back(line3.str());
          textColor = (gem.closestType != GemType::UNKNOWN)
                          ? GemTypeToColor(gem.closestType)
                          : cv::Scalar(160, 160, 160);
        }

        std::ostringstream hsvLine;
        hsvLine << "H" << (int)hsvVal[0] << " S" << (int)hsvVal[1];
        lines.push_back(hsvLine.str());
        std::ostringstream vLine;
        vLine << "V" << (int)hsvVal[2];
        lines.push_back(vLine.str());

        // Calculate background size
        int maxWidth = 0;
        int fontFace = cv::FONT_HERSHEY_SIMPLEX;
        double fontScale = 0.45;
        int thickness = 1;
        int baseline = 0;

        for (const auto &line : lines) {
          cv::Size textSize =
              cv::getTextSize(line, fontFace, fontScale, thickness, &baseline);
          maxWidth = std::max(maxWidth, textSize.width);
        }

        int bgPadding = 3;
        cv::Rect bgRect(textX - bgPadding, textY - 12, maxWidth + bgPadding * 2,
                        static_cast<int>(lines.size()) * lineHeight +
                            bgPadding * 2);
        cv::rectangle(image, bgRect, cv::Scalar(20, 20, 20), -1);
        cv::rectangle(image, bgRect, cv::Scalar(60, 60, 60), 1);

        // Draw text lines with anti-aliasing
        for (size_t i = 0; i < lines.size(); ++i) {
          cv::Point pos(textX, textY + static_cast<int>(i) * lineHeight);
          cv::putText(image, lines[i], pos, fontFace, fontScale, textColor,
                      thickness, cv::LINE_AA);
        }
      }
    }
  }
}
