#include "banner.h"
#include "config.h"
#include <iomanip>
#include <opencv2/freetype.hpp>
#include <sstream>

// ============================================================================
// FreeType Font Support
// ============================================================================

namespace {
cv::Ptr<cv::freetype::FreeType2> ft2;
bool freetypeInitialized = false;

void InitializeFreeType() {
  if (freetypeInitialized)
    return;

  try {
    ft2 = cv::freetype::createFreeType2();

    // Try Fira Code Nerd Font first, then common monospace fallbacks
    static const char *fontPaths[] = {
        "C:/Windows/Fonts/FiraCodeNerdFont-Regular.ttf",
        "C:/Windows/Fonts/FiraCodeNerdFontMono-Regular.ttf",
        "C:/Windows/Fonts/consola.ttf", // Consolas
        "C:/Windows/Fonts/cour.ttf",    // Courier New
    };

    for (const auto &path : fontPaths) {
      try {
        ft2->loadFontData(path, 0);
        freetypeInitialized = true;
        return;
      } catch (...) {
        continue;
      }
    }
  } catch (...) {
    // FreeType not available, will use built-in font
  }
}
} // namespace

// ============================================================================
// Banner Drawing
// ============================================================================

void DrawBanner(cv::Mat &image, float fps, double captureMs, double detectMs,
                double drawMs, double displayMs, int fpsWidth, int captureWidth,
                int detectWidth, int drawWidth, int displayWidth) {
  // Initialize FreeType on first call
  InitializeFreeType();

  // Draw banner background
  cv::rectangle(image, cv::Rect(0, 0, image.cols, Config::BANNER_HEIGHT),
                Config::BANNER_BG_COLOR, -1);

  // Create timing text with padding - static allocation
  static thread_local std::ostringstream oss;
  oss.str("");
  oss << "FPS: " << std::fixed << std::setprecision(1) << std::setw(fpsWidth)
      << fps << " | Capture: " << std::setprecision(2)
      << std::setw(captureWidth) << captureMs << "ms"
      << " | Detect: " << std::setw(detectWidth) << detectMs << "ms"
      << " | Draw: " << std::setw(drawWidth) << drawMs << "ms"
      << " | Display: " << std::setw(displayWidth) << displayMs << "ms";
  static thread_local std::string bannerText;
  bannerText = oss.str();

  // Draw text with monospace font
  if (freetypeInitialized && ft2) {
    // Use FreeType2 with monospace font
    ft2->putText(image, bannerText, cv::Point(10, 0),
                 18, // Font height
                 Config::BANNER_TEXT_COLOR,
                 -1, // Thickness (filled)
                 cv::LINE_AA,
                 false); // Bottom-left origin
  } else {
    // Fallback to built-in font (not truly monospace but best we have)
    int fontFace = cv::FONT_HERSHEY_PLAIN;
    double fontScale = 1.0;
    int thickness = 1;

    cv::putText(image, bannerText, cv::Point(10, 20), fontFace, fontScale,
                Config::BANNER_TEXT_COLOR, thickness);
  }

  // Draw separator line below banner
  cv::line(image, cv::Point(0, Config::BANNER_HEIGHT),
           cv::Point(image.cols, Config::BANNER_HEIGHT),
           cv::Scalar(100, 100, 100), 1);
}