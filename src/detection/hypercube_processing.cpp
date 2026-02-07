#include "hypercube_processing.h"
#include <opencv2/opencv.hpp>

namespace HypercubeProcessing {

void ApplyGlowOverlay(
    cv::Mat& colorFrame,
    const cv::Mat& maskFrame,
    const cv::Mat& glowSheet,
    int row, int col,
    int sourceCellWidth,
    int sourceCellHeight,
    int frameWidth,
    int frameHeight,
    int glowCellWidth,
    int glowCellHeight)
{
    cv::Rect glowRoi(col * glowCellWidth,
                   row * glowCellHeight,
                   glowCellWidth,
                   glowCellHeight);

    cv::Mat glowCell = glowSheet(glowRoi);

    if (glowCell.depth() != CV_8U) {
        glowCell.convertTo(glowCell, CV_8U, 255.0);
    }

    int cropOffset = (glowCellWidth - frameWidth) / 2;
    cv::Rect cropRect(cropOffset, cropOffset, frameWidth, frameHeight);
    cv::Mat glowCropped = glowCell(cropRect);

    for (int y = 0; y < frameHeight; y++) {
        cv::Vec3b* colorRow = colorFrame.ptr<cv::Vec3b>(y);
        const cv::Vec3b* glowRow = glowCropped.ptr<cv::Vec3b>(y);
        const uchar* maskRow = maskFrame.ptr<uchar>(y);

        for (int x = 0; x < frameWidth; x++) {
            if (maskRow[x] > 0) {
                int b = colorRow[x][0] + glowRow[x][0];
                int g = colorRow[x][1] + glowRow[x][1];
                int r = colorRow[x][2] + glowRow[x][2];

                colorRow[x][0] = cv::saturate_cast<uchar>(b);
                colorRow[x][1] = cv::saturate_cast<uchar>(g);
                colorRow[x][2] = cv::saturate_cast<uchar>(r);
            }
        }
    }
}

void ProcessHypercubeFrame(
    const cv::Mat& colorSheet,
    const cv::Mat& maskSheet,
    const cv::Mat& glowSheet,
    int row, int col,
    int frameIndex,
    const FrameProcessing::FrameProcessingParams& params,
    int sourceCellWidth,
    int sourceCellHeight,
    int frameWidth,
    int frameHeight,
    int glowCellWidth,
    int glowCellHeight,
    cv::Mat& outColor,
    cv::Mat& outMask,
    cv::Mat& outLab,
    FrameProcessing::CropBounds& outCropBounds)
{
    cv::Rect colorRoi(col * sourceCellWidth,
                     row * sourceCellHeight,
                     sourceCellWidth,
                     sourceCellHeight);

    cv::Mat colorFrame = cv::Mat::zeros(frameHeight, frameWidth, CV_8UC3);
    cv::Mat maskFrame = cv::Mat::zeros(frameHeight, frameWidth, CV_8UC1);

    int xOffset = (frameWidth - sourceCellWidth) / 2;
    int yOffset = (frameHeight - sourceCellHeight) / 2;

    colorSheet(colorRoi).copyTo(colorFrame(cv::Rect(xOffset, yOffset, sourceCellWidth, sourceCellHeight)));
    maskSheet(colorRoi).copyTo(maskFrame(cv::Rect(xOffset, yOffset, sourceCellWidth, sourceCellHeight)));

    cv::threshold(maskFrame, maskFrame, 254, 255, cv::THRESH_BINARY);

    for (int y = 0; y < colorFrame.rows; y++) {
        cv::Vec3b* colorRow = colorFrame.ptr<cv::Vec3b>(y);
        const uchar* maskRow = maskFrame.ptr<uchar>(y);
        for (int x = 0; x < colorFrame.cols; x++) {
            if (maskRow[x] == 0) {
                colorRow[x] = cv::Vec3b(0, 0, 0);
            }
        }
    }

    if (!glowSheet.empty()) {
        ApplyGlowOverlay(colorFrame, maskFrame, glowSheet, row, col,
            sourceCellWidth, sourceCellHeight, frameWidth, frameHeight,
            glowCellWidth, glowCellHeight);
    }

    cv::Mat processedMask = maskFrame.clone();
    cv::threshold(processedMask, processedMask, 254, 255, cv::THRESH_BINARY);

    cv::Mat processedColor = colorFrame.clone();
    if (params.applyHalfTexelOffset) {
        processedColor = FrameProcessing::ApplyHalfTexelOffset(processedColor,
            FrameProcessing::TexelOffsetConfig::GetInterpolationFlag(FrameProcessing::TexelOffsetConfig::COLOR_INTERPOLATION));
        processedMask = FrameProcessing::ApplyHalfTexelOffset(processedMask,
            FrameProcessing::TexelOffsetConfig::GetInterpolationFlag(FrameProcessing::TexelOffsetConfig::ALPHA_INTERPOLATION));
    }

    cv::cvtColor(processedColor, outLab, cv::COLOR_BGR2Lab);
    outCropBounds = FrameProcessing::CalculateCropBounds(outLab);

    outColor = processedColor;
    outMask = processedMask;
}


} // namespace HypercubeProcessing
