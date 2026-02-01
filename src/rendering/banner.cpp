#include "banner.h"
#include <iomanip>
#include <opencv2/freetype.hpp>
#include <sstream>
#include <vector>

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
            static const char* fontPaths[] = {
                "C:/Windows/Fonts/FiraCodeNerdFont-Regular.ttf",
                "C:/Windows/Fonts/FiraCodeNerdFontMono-Regular.ttf",
                "C:/Windows/Fonts/consola.ttf", // Consolas
                "C:/Windows/Fonts/cour.ttf", // Courier New
            };

            for (const auto&path: fontPaths) {
                try {
                    ft2->loadFontData(path, 0);
                    freetypeInitialized = true;
                    return;
                }
                catch (...) {
                    continue;
                }
            }
        }
        catch (...) {
            // FreeType not available, will use built-in font
        }
    }

// ============================================================================
// HSV Range Formatting Helpers
// ============================================================================

    std::string FormatRangeLine(const std::string&label, const cv::Scalar&minHSV,
                                const cv::Scalar&maxHSV) {
        std::ostringstream oss;

        constexpr int LABEL_WIDTH = 14;
        constexpr int VAL_WIDTH = 3;

        oss << std::left << std::setw(LABEL_WIDTH) << label << std::right << " H["
                << std::setw(VAL_WIDTH) << static_cast<int>(minHSV[0]) << "-"
                << std::setw(VAL_WIDTH) << static_cast<int>(maxHSV[0]) << "]"
                << " S[" << std::setw(VAL_WIDTH) << static_cast<int>(minHSV[1]) << "-"
                << std::setw(VAL_WIDTH) << static_cast<int>(maxHSV[1]) << "]"
                << " V[" << std::setw(VAL_WIDTH) << static_cast<int>(minHSV[2]) << "-"
                << std::setw(VAL_WIDTH) << static_cast<int>(maxHSV[2]) << "]";

        return oss.str();
    }

    std::string FormatRedRangeLine() {
        std::ostringstream oss;

        constexpr int LABEL_WIDTH = 6;
        constexpr int VAL_WIDTH = 3;

        oss << std::left << std::setw(LABEL_WIDTH) << "RED" << std::right << " H["
                << std::setw(VAL_WIDTH) << static_cast<int>(Config::RED_GEM_HSV_MIN_1[0])
                << "-" << std::setw(VAL_WIDTH)
                << static_cast<int>(Config::RED_GEM_HSV_MAX_1[0]) << ","
                << std::setw(VAL_WIDTH) << static_cast<int>(Config::RED_GEM_HSV_MIN_2[0])
                << "-" << std::setw(VAL_WIDTH)
                << static_cast<int>(Config::RED_GEM_HSV_MAX_2[0]) << "]"
                << " S[" << std::setw(VAL_WIDTH)
                << static_cast<int>(Config::RED_GEM_HSV_MIN_1[1]) << "-"
                << std::setw(VAL_WIDTH) << static_cast<int>(Config::RED_GEM_HSV_MAX_1[1])
                << "]"
                << " V[" << std::setw(VAL_WIDTH)
                << static_cast<int>(Config::RED_GEM_HSV_MIN_1[2]) << "-"
                << std::setw(VAL_WIDTH) << static_cast<int>(Config::RED_GEM_HSV_MAX_1[2])
                << "]";

        return oss.str();
    }
} // namespace

// ============================================================================
// Banner Drawing
// ============================================================================

void DrawBanner(cv::Mat&image, float fps, double captureMs, double detectMs,
                double drawMs, double displayMs,
                Config::ClassificationMode classificationMode, int fpsWidth,
                int captureWidth, int detectWidth, int drawWidth,
                int displayWidth) {
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
            << " | Display: " << std::setw(displayWidth) << displayMs << "ms"
            << " | Mode: "
            << (classificationMode == Config::ClassificationMode::COLOR
                    ? "COLOR (c)"
                    : "SHAPE (c)");
    static thread_local std::string bannerText;
    bannerText = oss.str();

    // HSV range lines - computed once at startup, values are config constants
    static const std::vector<std::string> colorHsvLines = {
        FormatRedRangeLine(),
        FormatRangeLine("ORANGE", Config::ORANGE_GEM_HSV_MIN,
                        Config::ORANGE_GEM_HSV_MAX),
        FormatRangeLine("YELLOW", Config::YELLOW_GEM_HSV_MIN,
                        Config::YELLOW_GEM_HSV_MAX),
        FormatRangeLine("GREEN", Config::GREEN_GEM_HSV_MIN,
                        Config::GREEN_GEM_HSV_MAX),
        FormatRangeLine("BLUE", Config::BLUE_GEM_HSV_MIN,
                        Config::BLUE_GEM_HSV_MAX),
        FormatRangeLine("PURPLE", Config::PURPLE_GEM_HSV_MIN,
                        Config::PURPLE_GEM_HSV_MAX),
        FormatRangeLine("WHITE", Config::WHITE_GEM_HSV_MIN,
                        Config::WHITE_GEM_HSV_MAX),
    };
    static const std::vector<std::string> shapeHsvLines = {
        "SHAPE MODE"
    };
    const std::vector<std::string>&hsvLines =
            (classificationMode == Config::ClassificationMode::COLOR)
                ? colorHsvLines
                : shapeHsvLines;

    // Draw text with monospace font
    if (freetypeInitialized && ft2) {
        // Use FreeType2 with monospace font
        ft2->putText(image, bannerText, cv::Point(10, 0),
                     18, // Font height
                     Config::BANNER_TEXT_COLOR,
                     -1, // Thickness (filled)
                     cv::LINE_AA,
                     false); // Bottom-left origin
    }
    else {
        // Fallback to built-in font (not truly monospace but best we have)
        int fontFace = cv::FONT_HERSHEY_PLAIN;
        double fontScale = 1.0;
        int thickness = 1;

        cv::putText(image, bannerText, cv::Point(10, 20), fontFace, fontScale,
                    Config::BANNER_TEXT_COLOR, thickness);
    }

    // Draw HSV ranges
    int lineHeight = 16;
    int startY = 40;

    for (size_t i = 0; i < hsvLines.size(); ++i) {
        cv::Point pos(10, startY + static_cast<int>(i) * (lineHeight + 5));

        if (freetypeInitialized && ft2) {
            // Use FreeType2 with monospace font
            ft2->putText(image, hsvLines[i], pos,
                         14, // Font height
                         cv::Scalar(180, 180, 180),
                         -1, // Thickness (filled)
                         cv::LINE_AA,
                         false); // Bottom-left origin
        }
        else {
            // Fallback to built-in font
            int fontFace = cv::FONT_HERSHEY_PLAIN;
            double fontScale = 1.0;
            int thickness = 1;
            cv::putText(image, hsvLines[i], pos, fontFace, fontScale,
                        cv::Scalar(180, 180, 180), thickness, cv::LINE_AA);
        }
    }

    // Draw separator line below banner
    cv::line(image, cv::Point(0, Config::BANNER_HEIGHT),
             cv::Point(image.cols, Config::BANNER_HEIGHT),
             cv::Scalar(100, 100, 100), 1);
}