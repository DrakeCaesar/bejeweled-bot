#pragma once

#include "detection/gem_types.h"
#include <opencv2/opencv.hpp>
#include <vector>

struct MatchableSwap {
  cv::Point a; // (col, row)
  cv::Point b; // (col, row)
};

std::vector<MatchableSwap>
FindMatchableSwaps(const std::vector<std::vector<Gem>> &grid);

void DrawMatchableSwaps(cv::Mat &image,
                        const std::vector<std::vector<Gem>> &grid);
