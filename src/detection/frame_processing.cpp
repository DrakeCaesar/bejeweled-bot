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

// Default parameters for different gem types
const FrameProcessingParams kStandardGemParams = {
    254,                          // thresholdValue
    cv::MORPH_ELLIPSE,            // kernelShape
    cv::Size(3, 3),               // kernelSize
    false,                        // applyHalfTexelOffset
    TexelOffsetConfig::COLOR_INTERPOLATION,
    TexelOffsetConfig::ALPHA_INTERPOLATION
};

const FrameProcessingParams kStaticColorParams = {
    254,                          // thresholdValue
    cv::MORPH_ELLIPSE,            // kernelShape
    cv::Size(7, 7),               // kernelSize
    true,                         // applyHalfTexelOffset
    TexelOffsetConfig::COLOR_INTERPOLATION,
    TexelOffsetConfig::ALPHA_INTERPOLATION
};

const FrameProcessingParams kHypercubeParams = {
    254,                          // thresholdValue
    cv::MORPH_ELLIPSE,            // kernelShape (not used)
    cv::Size(0, 0),               // kernelSize (no erosion)
    false,                        // applyHalfTexelOffset
    TexelOffsetConfig::COLOR_INTERPOLATION,
    TexelOffsetConfig::ALPHA_INTERPOLATION
};

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

CropBounds CalculateCropBounds(const cv::Mat& labImage) {
    CropBounds bounds = {0, labImage.rows - 1, 0, labImage.cols - 1};

    if (labImage.empty()) {
        return bounds;
    }

    int minRow = labImage.rows;
    int maxRow = -1;

    for (int y = 0; y < labImage.rows; y++) {
        const cv::Vec3b* row = labImage.ptr<cv::Vec3b>(y);
        bool hasNonBlack = false;

        for (int x = 0; x < labImage.cols; x++) {
            if (!(row[x][0] == 0 && row[x][1] == 128 && row[x][2] == 128)) {
                hasNonBlack = true;
                break;
            }
        }

        if (hasNonBlack) {
            if (y < minRow) minRow = y;
            if (y > maxRow) maxRow = y;
        }
    }

    int minCol = labImage.cols;
    int maxCol = -1;

    for (int x = 0; x < labImage.cols; x++) {
        bool hasNonBlack = false;

        for (int y = 0; y < labImage.rows; y++) {
            const cv::Vec3b* row = labImage.ptr<cv::Vec3b>(y);
            if (!(row[x][0] == 0 && row[x][1] == 128 && row[x][2] == 128)) {
                hasNonBlack = true;
                break;
            }
        }

        if (hasNonBlack) {
            if (x < minCol) minCol = x;
            if (x > maxCol) maxCol = x;
        }
    }

    bounds.minRow = (minRow == labImage.rows) ? 0 : minRow;
    bounds.maxRow = (maxRow == -1) ? labImage.rows - 1 : maxRow;
    bounds.minCol = (minCol == labImage.cols) ? 0 : minCol;
    bounds.maxCol = (maxCol == -1) ? labImage.cols - 1 : maxCol;

    return bounds;
}

void ProcessSingleFrame(
    const cv::Mat& colorRoi,
    const cv::Mat& maskRoi,
    const FrameProcessingParams& params,
    cv::Mat& outColor,
    cv::Mat& outMask,
    cv::Mat& outRingMask,
    cv::Mat& outLab,
    CropBounds& outCropBounds)
{
    cv::Mat colorFrame = colorRoi.clone();
    cv::Mat maskFrame = maskRoi.clone();

    cv::threshold(maskFrame, maskFrame, params.thresholdValue, 255, cv::THRESH_BINARY);

    if (params.kernelSize.width > 0 && params.kernelSize.height > 0) {
        cv::Mat kernel = cv::getStructuringElement(params.kernelShape, params.kernelSize);
        cv::erode(maskFrame, maskFrame, kernel, cv::Point(-1, -1), 1);
    }

    cv::Mat ringMask = BuildRingMask(maskFrame);

    if (params.applyHalfTexelOffset) {
        colorFrame = ApplyHalfTexelOffset(colorFrame,
            TexelOffsetConfig::GetInterpolationFlag(params.colorInterpolation));
        maskFrame = ApplyHalfTexelOffset(maskFrame,
            TexelOffsetConfig::GetInterpolationFlag(params.alphaInterpolation));
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

    outCropBounds = CalculateCropBounds(labFrame);

    outColor = colorFrame;
    outMask = maskFrame;
    outRingMask = ringMask;
    outLab = labFrame;
}

} // namespace FrameProcessing
