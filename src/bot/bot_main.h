#pragma once

#include "board_analysis.h"
#include <opencv2/opencv.hpp>
#include <vector>

std::vector<MatchableSwap>
FindMatchableSwaps(const std::vector<std::vector<Gem>> &grid);

/// Draw match lines from pre-computed board analysis (preferred)
void DrawMatchableSwaps(cv::Mat &image, const BoardAnalysis &analysis);

/// Draw match lines by computing swaps on the fly (legacy)
void DrawMatchableSwaps(cv::Mat &image,
                        const std::vector<std::vector<Gem>> &grid);
