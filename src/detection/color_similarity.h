#pragma once

#include <cstdint>
#include <opencv2/opencv.hpp>
#include <vector>

int CalculateColorSimilarity(const cv::Mat &capturedPlanarLab,
                             const cv::Mat &referencePlanarLab,
                             const std::vector<uint64_t> &bitmask,
                             int imageWidth, int imageHeight);

void InitializeColorSimilarityLut();
