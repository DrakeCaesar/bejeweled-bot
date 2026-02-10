#include "control_panel.h"
#include "detection/shape_classifier.h"
#include <algorithm>
#include <array>
#include <sstream>

namespace {
struct SliderDef {
  const char *label;
  int minVal;
  int maxVal;
  std::atomic<int> *value;
};

constexpr int kPanelX = 10;
constexpr int kPanelY = 25;
constexpr int kPanelW = 380;
constexpr int kPanelH = 210; // Increased height for additional sliders

constexpr int kSliderX = kPanelX + 140;
constexpr int kSliderW = 200;
constexpr int kSliderH = 8;
constexpr int kSliderSpacing = 28;
constexpr int kFirstSliderY = kPanelY + 24;

int g_activeSlider = -1;

const std::array<SliderDef, 4> kSliders = {
    SliderDef{"Debug", 0, 5, &ShapeParams::debugStage},
    SliderDef{"Match%", 1, 100, &ShapeParams::matchThreshold},
    SliderDef{"LCeil", 0, 254, &ShapeParams::lCompressionCeiling},
    SliderDef{"LComp%", 0, 100, &ShapeParams::lCompression}};

cv::Rect GetSliderTrackRect(int index) {
  int y = kFirstSliderY + index * kSliderSpacing;
  return cv::Rect(kSliderX, y, kSliderW, kSliderH);
}

int ValueFromX(int index, int x) {
  const auto &def = kSliders[index];
  cv::Rect track = GetSliderTrackRect(index);
  float t = (x - track.x) / static_cast<float>(track.width);
  t = std::clamp(t, 0.0f, 1.0f);
  int v = static_cast<int>(def.minVal + t * (def.maxVal - def.minVal));
  return v;
}

int KnobXFromValue(int index, int value) {
  const auto &def = kSliders[index];
  cv::Rect track = GetSliderTrackRect(index);
  float t = (value - def.minVal) / static_cast<float>(def.maxVal - def.minVal);
  t = std::clamp(t, 0.0f, 1.0f);
  return track.x + static_cast<int>(t * track.width);
}
} // namespace

void DrawControlPanel(cv::Mat &image) {
  // Panel background (dark)
  cv::rectangle(image, cv::Rect(kPanelX, kPanelY, kPanelW, kPanelH),
                cv::Scalar(15, 15, 15), -1);
  cv::rectangle(image, cv::Rect(kPanelX, kPanelY, kPanelW, kPanelH),
                cv::Scalar(60, 60, 60), 1);

  for (size_t i = 0; i < kSliders.size(); ++i) {
    const auto &def = kSliders[i];
    int value = def.value->load();

    cv::Rect track = GetSliderTrackRect(static_cast<int>(i));
    int knobX = KnobXFromValue(static_cast<int>(i), value);

    // Labels
    std::ostringstream oss;
    oss << def.label << ": " << value;
    cv::putText(image, oss.str(), cv::Point(kPanelX + 10, track.y + 8),
                cv::FONT_HERSHEY_SIMPLEX, 0.45, cv::Scalar(220, 220, 220), 1,
                cv::LINE_AA);

    // Track
    cv::rectangle(image, track, cv::Scalar(40, 40, 40), -1);
    cv::rectangle(image, track, cv::Scalar(80, 80, 80), 1);

    // Fill
    int fillW = knobX - track.x;
    if (fillW > 0) {
      cv::rectangle(image, cv::Rect(track.x, track.y, fillW, track.height),
                    cv::Scalar(120, 120, 120), -1);
    }

    // Knob
    cv::circle(image, cv::Point(knobX, track.y + track.height / 2), 6,
               cv::Scalar(220, 220, 220), -1, cv::LINE_AA);
    cv::circle(image, cv::Point(knobX, track.y + track.height / 2), 6,
               cv::Scalar(30, 30, 30), 1, cv::LINE_AA);
  }

  bool HandleControlPanelMouse(int event, int x, int y, int flags) {
    // Check if within panel area
    if (x < kPanelX || x > kPanelX + kPanelW || y < kPanelY ||
        y > kPanelY + kPanelH) {
      if (event == cv::EVENT_LBUTTONUP) {
        g_activeSlider = -1;
      }
      return false;
    }

    if (event == cv::EVENT_LBUTTONDOWN) {
      // Find slider under cursor
      for (size_t i = 0; i < kSliders.size(); ++i) {
        cv::Rect track = GetSliderTrackRect(static_cast<int>(i));
        cv::Rect hit(track.x - 6, track.y - 6, track.width + 12,
                     track.height + 12);
        if (hit.contains(cv::Point(x, y))) {
          g_activeSlider = static_cast<int>(i);
          int v = ValueFromX(g_activeSlider, x);
          kSliders[g_activeSlider].value->store(v);
          return true;
        }
      }
    } else if (event == cv::EVENT_MOUSEMOVE &&
               (flags & cv::EVENT_FLAG_LBUTTON)) {
      if (g_activeSlider >= 0) {
        int v = ValueFromX(g_activeSlider, x);
        kSliders[g_activeSlider].value->store(v);
        return true;
      }
    } else if (event == cv::EVENT_LBUTTONUP) {
      g_activeSlider = -1;
      return true;
    }

    return true;
  }
