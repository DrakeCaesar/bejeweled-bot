#include "frame_processing.h"
#include <opencv2/opencv.hpp>

namespace FrameProcessing {

namespace TexelOffsetConfig {
    int GetInterpolationFlag(int method) {
        switch (method) {
            case 0: return cv::INTER_NEAREST;
            case 1: return cv::INTER_LINEAR;
            case 2: return cv::INTER_CUBIC;
            case 3: return cv::INTER_AREA;
            default: return cv::INTER_NEAREST;
        }
    }
}


cv::Mat ApplyHalfTexelOffset(const cv::Mat& image, int interpolation) {
    if (image.empty()) {
        return cv::Mat();
    }

    cv::Mat shifted;
    cv::Mat transform = (cv::Mat_<double>(2, 3) <<
        1.0, 0.0, TexelOffsetConfig::OFFSET_X,
        0.0, 1.0, TexelOffsetConfig::OFFSET_Y);
    cv::warpAffine(image, shifted, transform, image.size(), interpolation,
                   cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
    return shifted;
}

cv::Mat BuildRingMask(const cv::Mat& alphaMask) {
    if (alphaMask.empty()) {
        return cv::Mat();
    }

    cv::Mat outer;
    cv::Mat outerKernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(13, 13));
    cv::dilate(alphaMask, outer, outerKernel);
    cv::threshold(outer, outer, 127, 255, cv::THRESH_BINARY);

    cv::Mat inner;
    cv::Mat innerKernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(9, 9));
    cv::dilate(alphaMask, inner, innerKernel);
    cv::threshold(inner, inner, 127, 255, cv::THRESH_BINARY);

    cv::Mat innerInverted;
    cv::bitwise_not(inner, innerInverted);

    cv::Mat ring;
    cv::bitwise_and(outer, innerInverted, ring);

    return ring;
}

void ProcessSingleFrame(
    const cv::Mat& colorRoi,
    const cv::Mat& maskRoi,
    const FrameProcessingParams& params,
    cv::Mat& outColor,
    cv::Mat& outMask,
    cv::Mat& outRingMask,
    cv::Mat& outLab)
{
    cv::Mat colorFrame = colorRoi.clone();
    cv::Mat maskFrame = maskRoi.clone();

    // Apply constant thresholdValue = 254
    cv::threshold(maskFrame, maskFrame, 254, 255, cv::THRESH_BINARY);

    // Apply kernel erosion if kernel size > 0
    if (params.kernelSize.width > 0 && params.kernelSize.height > 0) {
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, params.kernelSize);
        cv::erode(maskFrame, maskFrame, kernel, cv::Point(-1, -1), 1);
    }

    cv::Mat ringMask = BuildRingMask(maskFrame);

    // Apply half-texel offset based on params
    if (params.applyHalfTexelOffset) {
        colorFrame = ApplyHalfTexelOffset(colorFrame,
            TexelOffsetConfig::GetInterpolationFlag(TexelOffsetConfig::COLOR_INTERPOLATION));
        maskFrame = ApplyHalfTexelOffset(maskFrame,
            TexelOffsetConfig::GetInterpolationFlag(TexelOffsetConfig::ALPHA_INTERPOLATION));
        ringMask = BuildRingMask(maskFrame);
    }

    for (int y = 0; y < colorFrame.rows; y++) {
        cv::Vec3b* colorRow = colorFrame.ptr<cv::Vec3b>(y);
        const uchar* maskRow = maskFrame.ptr<uchar>(y);
        for (int x = 0; x < colorFrame.cols; x++) {
            if (maskRow[x] == 0) {
                colorRow[x] = cv::Vec3b(0, 0, 0);
            }
        }
    }

    cv::Mat labFrame;
    cv::cvtColor(colorFrame, labFrame, cv::COLOR_BGR2Lab);

    outColor = colorFrame;
    outMask = maskFrame;
    outRingMask = ringMask;
    outLab = labFrame;
}

} // namespace FrameProcessing
