#include "gem_types.h"

std::string GemTypeToString(GemType type) {
  switch (type) {
  case GemType::RED:
    return "RED";
  case GemType::ORANGE:
    return "ORANGE";
  case GemType::YELLOW:
    return "YELLOW";
  case GemType::GREEN:
    return "GREEN";
  case GemType::BLUE:
    return "BLUE";
  case GemType::PURPLE:
    return "PURPLE";
  case GemType::WHITE:
    return "WHITE";
  case GemType::HYPERCUBE:
    return "HYPERCUBE";
  case GemType::EMPTY:
    return "EMPTY";
  default:
    return "UNKNOWN";
  }
}

cv::Scalar GemTypeToColor(GemType type) {
  switch (type) {
  case GemType::RED:
    return cv::Scalar(0, 0, 255);
  case GemType::ORANGE:
    return cv::Scalar(0, 165, 255);
  case GemType::YELLOW:
    return cv::Scalar(0, 255, 255);
  case GemType::GREEN:
    return cv::Scalar(0, 255, 0);
  case GemType::BLUE:
    return cv::Scalar(255, 0, 0);
  case GemType::PURPLE:
    return cv::Scalar(255, 0, 255);
  case GemType::WHITE:
    return cv::Scalar(255, 255, 255);
  case GemType::HYPERCUBE:
    return cv::Scalar(200, 200, 200); // Silver/gray
  default:
    return cv::Scalar(128, 128, 128);
  }
}