#include "mask_loader.h"
#include <filesystem>
#include <iostream>
#include <opencv2/opencv.hpp>

namespace MaskLoader {
// Static storage for masks
    static MaskStorage s_masks;
    static LabMaskStorage s_labMasks;
    static AlphaMaskStorage s_alphaMasks;
    static RingMaskStorage s_ringMasks;
    static HypercubeMaskStorage s_hypercubeMasks;
    static HypercubeLabMaskStorage s_hypercubeLabMasks;
    static HypercubeAlphaMaskStorage s_hypercubeAlphaMasks;
    static HypercubeRingMaskStorage s_hypercubeRingMasks;
    static bool s_loaded = false;
    static cv::Mat s_emptyMask;

// Crop bounds for non-black regions (optimization to skip padding during comparison)
    static CropBoundsStorage s_cropBounds;
    static HypercubeCropBoundsStorage s_hypercubeCropBounds;

// Static color reference images (first frame replacements for Phase 1)
    static MaskStorage s_staticColorMasks;
    static bool s_staticColorsLoaded = false;

    // Half-texel offset configuration
    namespace TexelOffsetConfig {
        // Offset values (in pixels)
        constexpr double OFFSET_X = 0.5;  // Options: -0.5, 0.0, 0.5
        constexpr double OFFSET_Y = 0.5;  // Options: -0.5, 0.0, 0.5
        
        // Interpolation method for color image
        // 0 = INTER_NEAREST, 1 = INTER_LINEAR, 2 = INTER_CUBIC, 3 = INTER_AREA
        constexpr int COLOR_INTERPOLATION = 1;  // 0=Nearest, 1=Bilinear, 2=Bicubic, 3=Area
        
        // Interpolation method for alpha mask
        constexpr int ALPHA_INTERPOLATION = 0;  // Usually keep at 0 (Nearest)
        
        inline int GetInterpolationFlag(int method) {
            switch (method) {
                case 0: return cv::INTER_NEAREST;
                case 1: return cv::INTER_LINEAR;
                case 2: return cv::INTER_CUBIC;
                case 3: return cv::INTER_AREA;
                default: return cv::INTER_NEAREST;
            }
        }
    }
    // namespace TexelOffsetConfig

    // Frame processing parameters
    struct FrameProcessingParams {
        int thresholdValue;           // 127 or 254
        int kernelShape;              // cv::MorphShapes enum value
        cv::Size kernelSize;          // e.g., Size(3,3), Size(7,7)
        bool applyHalfTexelOffset;    // whether to apply offset
        int colorInterpolation;       // interpolation for color offset
        int alphaInterpolation;       // interpolation for alpha offset
    };

    // Default parameters for different gem types
    static const FrameProcessingParams kStandardGemParams = {
        254,                          // thresholdValue
        cv::MORPH_RECT,               // kernelShape
        cv::Size(3, 3),               // kernelSize
        false,                        // applyHalfTexelOffset
        TexelOffsetConfig::COLOR_INTERPOLATION,
        TexelOffsetConfig::ALPHA_INTERPOLATION
    };

    static const FrameProcessingParams kStaticColorParams = {
        254,                          // thresholdValue
        cv::MORPH_ELLIPSE,            // kernelShape
        cv::Size(7, 7),               // kernelSize
        true,                         // applyHalfTexelOffset
        TexelOffsetConfig::COLOR_INTERPOLATION,
        TexelOffsetConfig::ALPHA_INTERPOLATION
    };

    static const FrameProcessingParams kHypercubeParams = {
        127,                          // thresholdValue
        cv::MORPH_RECT,               // kernelShape (not used)
        cv::Size(0, 0),               // kernelSize (no erosion)
        false,                        // applyHalfTexelOffset
        TexelOffsetConfig::COLOR_INTERPOLATION,
        TexelOffsetConfig::ALPHA_INTERPOLATION
    };

    static cv::Mat ApplyHalfTexelOffset(const cv::Mat&image, int interpolation) {
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

    // Process a single frame (color ROI and mask ROI) according to parameters
    // Outputs processed color, mask, ringMask, labImage, and cropBounds

    static cv::Mat BuildRingMask(const cv::Mat&alphaMask) {
        if (alphaMask.empty()) {
            return cv::Mat();
        }

        // Create outer boundary by dilating the mask
        cv::Mat outer;
        cv::Mat outerKernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(13, 13));
        cv::dilate(alphaMask, outer, outerKernel);
        cv::threshold(outer, outer, 127, 255, cv::THRESH_BINARY);

        // Create inner boundary by dilating with a smaller kernel (pushes boundary outward)
        cv::Mat inner;
        cv::Mat innerKernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(9, 9));
        cv::dilate(alphaMask, inner, innerKernel);
        cv::threshold(inner, inner, 127, 255, cv::THRESH_BINARY);

        // Invert the inner mask
        cv::Mat innerInverted;
        cv::bitwise_not(inner, innerInverted);

        // Ring is outer AND (NOT inner)
        cv::Mat ring;
        cv::bitwise_and(outer, innerInverted, ring);

        return ring;
    }



// Calculate crop bounds for non-black region in LAB image (background = 0,128,128)
    static CropBounds CalculateCropBounds(const cv::Mat&labImage) {
        CropBounds bounds = {0, labImage.rows - 1, 0, labImage.cols - 1};
        
        if (labImage.empty()) {
            return bounds;
        }

        // Find first and last non-black rows
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

        // Find first and last non-black columns
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

    static void ProcessSingleFrame(
        const cv::Mat& colorRoi,
        const cv::Mat& maskRoi,
        const FrameProcessingParams& params,
        cv::Mat& outColor,
        cv::Mat& outMask,
        cv::Mat& outRingMask,
        cv::Mat& outLab,
        CropBounds& outCropBounds)
    {
        // Clone inputs
        cv::Mat colorFrame = colorRoi.clone();
        cv::Mat maskFrame = maskRoi.clone();

        // Threshold mask
        cv::threshold(maskFrame, maskFrame, params.thresholdValue, 255, cv::THRESH_BINARY);

        // Erode mask if kernel size is non-zero
        if (params.kernelSize.width > 0 && params.kernelSize.height > 0) {
            cv::Mat kernel = cv::getStructuringElement(params.kernelShape, params.kernelSize);
            cv::erode(maskFrame, maskFrame, kernel, cv::Point(-1, -1), 1);
        }

        // Build ring mask
        cv::Mat ringMask = BuildRingMask(maskFrame);

        // Apply half-texel offset if requested
        if (params.applyHalfTexelOffset) {
            colorFrame = ApplyHalfTexelOffset(colorFrame,
                TexelOffsetConfig::GetInterpolationFlag(params.colorInterpolation));
            maskFrame = ApplyHalfTexelOffset(maskFrame,
                TexelOffsetConfig::GetInterpolationFlag(params.alphaInterpolation));
            // Rebuild ring mask after offset (mask changed)
            ringMask = BuildRingMask(maskFrame);
        }

        // Apply mask to color frame (set background pixels to black)
        for (int y = 0; y < colorFrame.rows; y++) {
            cv::Vec3b* colorRow = colorFrame.ptr<cv::Vec3b>(y);
            const uchar* maskRow = maskFrame.ptr<uchar>(y);
            for (int x = 0; x < colorFrame.cols; x++) {
                if (maskRow[x] == 0) {
                    colorRow[x] = cv::Vec3b(0, 0, 0);
                }
            }
        }

        // Convert to Lab color space
        cv::Mat labFrame;
        cv::cvtColor(colorFrame, labFrame, cv::COLOR_BGR2Lab);

        // Calculate crop bounds
        outCropBounds = CalculateCropBounds(labFrame);

        // Return processed images
        outColor = colorFrame;
        outMask = maskFrame;
        outRingMask = ringMask;
        outLab = labFrame;
    }

// Map GemType to filename
    static const char* GemTypeToColorFilename(GemType type) {
        switch (type) {
            case GemType::RED:
                return "Red.jp2";
            case GemType::ORANGE:
                return "Orange.jp2";
            case GemType::YELLOW:
                return "Yellow.jp2";
            case GemType::GREEN:
                return "Green.jp2";
            case GemType::BLUE:
                return "Blue.jp2";
            case GemType::PURPLE:
                return "Purple.jp2";
            case GemType::WHITE:
                return "White.jp2";
            case GemType::HYPERCUBE:
                return "hypercube.png";
            default:
                return nullptr;
        }
    }

    static const char* GemTypeToMaskFilename(GemType type) {
        switch (type) {
            case GemType::RED:
                return "Red_.gif";
            case GemType::ORANGE:
                return "Orange_.gif";
            case GemType::YELLOW:
                return "Yellow_.gif";
            case GemType::GREEN:
                return "Green_.gif";
            case GemType::BLUE:
                return "Blue_.gif";
            case GemType::PURPLE:
                return "Purple_.gif";
            case GemType::WHITE:
                return "White_.gif";
            case GemType::HYPERCUBE:
                return "hypercube.png";
            default:
                return nullptr;
        }
    }

// Convert GemType to storage index (0-6)
    static int GemTypeToIndex(GemType type) {
        switch (type) {
            case GemType::RED:
                return 0;
            case GemType::ORANGE:
                return 1;
            case GemType::YELLOW:
                return 2;
            case GemType::GREEN:
                return 3;
            case GemType::BLUE:
                return 4;
            case GemType::PURPLE:
                return 5;
            case GemType::WHITE:
                return 6;
            default:
                return -1;
        }
    }

    bool LoadMasksForGem(const std::string&assetsPath, GemType type) {
        // Skip hypercube - it's loaded separately
        if (type == GemType::HYPERCUBE) return true;

        const char* colorFilename = GemTypeToColorFilename(type);
        const char* maskFilename = GemTypeToMaskFilename(type);
        if (!colorFilename || !maskFilename)
            return false;

        int gemIndex = GemTypeToIndex(type);
        if (gemIndex < 0)
            return false;

        // Load the color sprite sheet
        std::string colorPath = assetsPath + "/Gems/" + colorFilename;
        cv::Mat spriteSheet = cv::imread(colorPath, cv::IMREAD_COLOR);
        if (spriteSheet.empty()) {
            std::cerr << "Failed to load reference image: " << colorPath << std::endl;
            return false;
        }

        // Load the alpha mask sprite sheet
        std::string maskPath = assetsPath + "/Gems/" + maskFilename;
        cv::Mat maskSheet = cv::imread(maskPath, cv::IMREAD_UNCHANGED);
        if (maskSheet.empty()) {
            std::cerr << "Failed to load alpha mask: " << maskPath << std::endl;
            // Continue without masks - use full images
            maskSheet = cv::Mat::ones(spriteSheet.size(), CV_8UC1) * 255;
        }
        else {
            // Convert to grayscale if needed
            if (maskSheet.channels() == 4) {
                // RGBA image - extract first channel (they're all the same for B&W)
                cv::Mat channels[4];
                cv::split(maskSheet, channels);
                maskSheet = channels[0]; // Use any channel (R, G, or B - they're identical for B&W)
            }
            else if (maskSheet.channels() == 3) {
                // BGR image - convert to grayscale
                cv::cvtColor(maskSheet, maskSheet, cv::COLOR_BGR2GRAY);
            }
            // If already 1 channel, use as-is
        }

        // Verify dimensions
        if (spriteSheet.cols != SPRITE_WIDTH || spriteSheet.rows != SPRITE_HEIGHT) {
            std::cerr << "Unexpected sprite sheet size for " << colorPath << ": "
                    << spriteSheet.cols << "x" << spriteSheet.rows << " (expected "
                    << SPRITE_WIDTH << "x" << SPRITE_HEIGHT << ")" << std::endl;
            return false;
        }

        if (maskSheet.cols != SPRITE_WIDTH || maskSheet.rows != SPRITE_HEIGHT) {
            std::cerr << "Unexpected mask sheet size for " << maskPath << ": "
                    << maskSheet.cols << "x" << maskSheet.rows << " (expected "
                    << SPRITE_WIDTH << "x" << SPRITE_HEIGHT << ")" << std::endl;
            // Resize mask to match color sheet
            cv::resize(maskSheet, maskSheet, cv::Size(SPRITE_WIDTH, SPRITE_HEIGHT), 0, 0, cv::INTER_NEAREST);
        }

        // Extract each frame from sprite sheet (20 frames go into indices 1-20)
        // Frame 0 is reserved for static image loaded separately
        int frameIndex = 1;
        for (int row = 0; row < MASK_ROWS; row++) {
            for (int col = 0; col < MASK_COLS; col++) {
                // Calculate ROI for this frame
                cv::Rect roi(col * FRAME_WIDTH, row * FRAME_HEIGHT, FRAME_WIDTH,
                             FRAME_HEIGHT);

                // Extract color and mask ROIs
                cv::Mat colorRoi = spriteSheet(roi);
                cv::Mat maskRoi = maskSheet(roi);

                // Process the frame using standard gem parameters
                cv::Mat colorFrame, maskFrame, ringMask, labFrame;
                CropBounds cropBounds;
                ProcessSingleFrame(colorRoi, maskRoi, kStandardGemParams,
                                  colorFrame, maskFrame, ringMask, labFrame, cropBounds);

                // Store processed images and crop bounds
                s_masks[gemIndex][frameIndex] = colorFrame;
                s_labMasks[gemIndex][frameIndex] = labFrame;
                s_alphaMasks[gemIndex][frameIndex] = maskFrame;
                s_ringMasks[gemIndex][frameIndex] = ringMask;
                s_cropBounds[gemIndex][frameIndex] = cropBounds;

                frameIndex++;
            }
        }

        std::cout << "Loaded " << (frameIndex - 1) << " animated frames for "
                << GemTypeToString(type) << " with alpha masks" << std::endl;
        return true;
    }

    bool LoadStaticColorMasks(const std::string&assetsPath) {
        bool success = true;

        // gems_shadowed.jp2 contains 7 frames side-by-side in this order:
        // red, white, green, yellow, purple, orange, blue
        static const GemType gemOrder[] = {
            GemType::RED, GemType::WHITE, GemType::GREEN, GemType::YELLOW,
            GemType::PURPLE, GemType::ORANGE, GemType::BLUE
        };

        std::string staticPath = assetsPath + "/gems_shadowed.jp2";
        cv::Mat spriteSheet = cv::imread(staticPath, cv::IMREAD_COLOR);
        if (spriteSheet.empty()) {
            std::cerr << "Failed to load static color image: " << staticPath << std::endl;
            return false;
        }

        std::string maskPath = assetsPath + "/gems_shadowed_.gif";
        cv::Mat maskSheet = cv::imread(maskPath, cv::IMREAD_UNCHANGED);
        if (maskSheet.empty()) {
            std::cerr << "Failed to load static mask image: " << maskPath << std::endl;
            return false;
        }

        if (maskSheet.channels() == 4) {
            cv::Mat channels[4];
            cv::split(maskSheet, channels);
            maskSheet = channels[0];
        }
        else if (maskSheet.channels() == 3) {
            cv::cvtColor(maskSheet, maskSheet, cv::COLOR_BGR2GRAY);
        }

        std::string debugDir = assetsPath + "/debug";
        std::filesystem::create_directories(debugDir);

        int expectedWidth = FRAME_WIDTH * NUM_GEM_TYPES;
        if (spriteSheet.rows != FRAME_HEIGHT || spriteSheet.cols != expectedWidth) {
            std::cerr << "Unexpected static color sheet size for " << staticPath << ": "
                    << spriteSheet.cols << "x" << spriteSheet.rows << " (expected "
                    << expectedWidth << "x" << FRAME_HEIGHT << ")" << std::endl;
            return false;
        }

        if (maskSheet.rows != FRAME_HEIGHT || maskSheet.cols != expectedWidth) {
            std::cerr << "Unexpected static mask sheet size for " << maskPath << ": "
                << maskSheet.cols << "x" << maskSheet.rows << " (expected "
                << expectedWidth << "x" << FRAME_HEIGHT << "), resizing" << std::endl;
            cv::resize(maskSheet, maskSheet, cv::Size(expectedWidth, FRAME_HEIGHT), 0, 0, cv::INTER_NEAREST);
        }

        for (int i = 0; i < NUM_GEM_TYPES; i++) {
            GemType type = gemOrder[i];
            int gemIndex = GemTypeToIndex(type);
            if (gemIndex < 0) {
                success = false;
                continue;
            }

            cv::Rect roi(i * FRAME_WIDTH, 0, FRAME_WIDTH, FRAME_HEIGHT);
            cv::Mat colorRoi = spriteSheet(roi);
            cv::Mat maskRoi = maskSheet(roi);

            // Debug paths (commented out)
            std::string debugPath = debugDir + "/static_" + std::to_string(i) + ".png";
            // cv::imwrite(debugPath, colorRoi);

            // Process the frame using static color parameters
            cv::Mat colorFrame, maskFrame, ringMask, labImage;
            CropBounds cropBounds;
            ProcessSingleFrame(colorRoi, maskRoi, kStaticColorParams,
                               colorFrame, maskFrame, ringMask, labImage, cropBounds);

            std::string alphaDebugPath = debugDir + "/static_alpha_" + std::to_string(i) + ".png";
            // cv::imwrite(alphaDebugPath, maskFrame);

            // Store as frame 0 in main storage
            s_masks[gemIndex][0] = colorFrame;
            s_labMasks[gemIndex][0] = labImage;
            s_alphaMasks[gemIndex][0] = maskFrame;
            s_ringMasks[gemIndex][0] = ringMask;
            s_cropBounds[gemIndex][0] = cropBounds;
        }

        s_staticColorsLoaded = success;
        return success;
    }

    bool LoadHypercube(const std::string&assetsPath) {
        const char* filename = "hypercube.png";

        // Load color sprite sheet
        std::string colorPath = assetsPath + "/color/" + filename;
        cv::Mat spriteSheet = cv::imread(colorPath, cv::IMREAD_COLOR);
        if (spriteSheet.empty()) {
            std::cerr << "Failed to load hypercube color image: " << colorPath << std::endl;
            return false;
        }

        // Load alpha mask (already 1 channel)
        std::string maskPath = assetsPath + "/mask/" + filename;
        cv::Mat maskSheet = cv::imread(maskPath, cv::IMREAD_GRAYSCALE);
        if (maskSheet.empty()) {
            std::cerr << "Failed to load hypercube mask: " << maskPath << std::endl;
            maskSheet = cv::Mat::ones(spriteSheet.size(), CV_8UC1) * 255;
        }

        // Verify dimensions
        if (spriteSheet.cols != HYPERCUBE_SPRITE_WIDTH || spriteSheet.rows != HYPERCUBE_SPRITE_HEIGHT) {
            std::cerr << "Unexpected hypercube sprite size: " << spriteSheet.cols << "x"
                    << spriteSheet.rows << " (expected " << HYPERCUBE_SPRITE_WIDTH << "x"
                    << HYPERCUBE_SPRITE_HEIGHT << ")" << std::endl;
            return false;
        }

        // Extract each frame (6 columns x 5 rows)
        int frameIndex = 0;
        for (int row = 0; row < HYPERCUBE_ROWS; row++) {
            for (int col = 0; col < HYPERCUBE_COLS; col++) {
                cv::Rect roi(col * HYPERCUBE_FRAME_WIDTH, row * HYPERCUBE_FRAME_HEIGHT,
                             HYPERCUBE_FRAME_WIDTH, HYPERCUBE_FRAME_HEIGHT);

                cv::Mat colorRoi = spriteSheet(roi);
                cv::Mat maskRoi = maskSheet(roi);

                // Process the frame using hypercube parameters
                cv::Mat colorFrame, maskFrame, ringMask, labFrame;
                CropBounds cropBounds;
                ProcessSingleFrame(colorRoi, maskRoi, kHypercubeParams,
                                   colorFrame, maskFrame, ringMask, labFrame, cropBounds);

                s_hypercubeMasks[frameIndex] = colorFrame;
                s_hypercubeLabMasks[frameIndex] = labFrame;
                s_hypercubeAlphaMasks[frameIndex] = maskFrame;
                s_hypercubeRingMasks[frameIndex] = ringMask;
                s_hypercubeCropBounds[frameIndex] = cropBounds;
                frameIndex++;
            }
        }

        std::cout << "Loaded " << frameIndex << " reference images for HYPERCUBE" << std::endl;
        return true;
    }

    bool LoadAllMasks(const std::string&assetsPath) {
        bool success = true;

        // Load masks for each standard gem type
        static const GemType gemTypes[] = {
            GemType::RED, GemType::ORANGE, GemType::YELLOW, GemType::GREEN,
            GemType::BLUE, GemType::PURPLE, GemType::WHITE
        };

        for (GemType type: gemTypes) {
            if (!LoadMasksForGem(assetsPath, type)) {
                success = false;
            }
        }

        // Load static color references (first frame replacements from static_color folder)
        if (!LoadStaticColorMasks(assetsPath)) {
            success = false;
        }

        // Load hypercube separately
        if (!LoadHypercube(assetsPath)) {
            success = false;
        }

        s_loaded = success;

        if (success) {
            std::cout << "Loaded " << (NUM_GEM_TYPES * FRAMES_PER_GEM + HYPERCUBE_FRAMES)
                    << " gem reference images total" << std::endl;
        }

        return success;
    }

    const MaskStorage& GetMasks() { return s_masks; }

    bool AreMasksLoaded() { return s_loaded; }

    const cv::Mat& GetMask(GemType type, int frameIndex) {
        if (type == GemType::HYPERCUBE) {
            if (frameIndex < 0 || frameIndex >= HYPERCUBE_FRAMES) {
                return s_emptyMask;
            }
            return s_hypercubeMasks[frameIndex];
        }

        int gemIndex = GemTypeToIndex(type);
        if (gemIndex < 0 || frameIndex < 0 || frameIndex >= FRAMES_PER_GEM) {
            return s_emptyMask;
        }
        
        // Frame 0 = static image, Frames 1-20 = animated frames
        return s_masks[gemIndex][frameIndex];
    }

    const cv::Mat& GetAlphaMask(GemType type, int frameIndex) {
        if (type == GemType::HYPERCUBE) {
            if (frameIndex < 0 || frameIndex >= HYPERCUBE_FRAMES) {
                return s_emptyMask;
            }
            return s_hypercubeAlphaMasks[frameIndex];
        }

        int gemIndex = GemTypeToIndex(type);
        if (frameIndex < 0 || frameIndex < 0 || frameIndex >= FRAMES_PER_GEM) {
            return s_emptyMask;
        }
        return s_alphaMasks[gemIndex][frameIndex];
    }

    const cv::Mat& GetRingMask(GemType type, int frameIndex) {
        if (type == GemType::HYPERCUBE) {
            if (frameIndex < 0 || frameIndex >= HYPERCUBE_FRAMES) {
                return s_emptyMask;
            }
            return s_hypercubeRingMasks[frameIndex];
        }

        int gemIndex = GemTypeToIndex(type);
        if (gemIndex < 0 || frameIndex < 0 || frameIndex >= FRAMES_PER_GEM) {
            return s_emptyMask;
        }
        return s_ringMasks[gemIndex][frameIndex];
    }

    const cv::Mat& GetLabMask(GemType type, int frameIndex) {
        if (type == GemType::HYPERCUBE) {
            if (frameIndex < 0 || frameIndex >= HYPERCUBE_FRAMES) {
                return s_emptyMask;
            }
            return s_hypercubeLabMasks[frameIndex];
        }

        int gemIndex = GemTypeToIndex(type);
        if (gemIndex < 0 || frameIndex < 0 || frameIndex >= FRAMES_PER_GEM) {
            return s_emptyMask;
        }
        return s_labMasks[gemIndex][frameIndex];
    }

    int GetFrameCount(GemType type) {
        if (type == GemType::HYPERCUBE) {
            return HYPERCUBE_FRAMES;
        }
        return FRAMES_PER_GEM;
    }

    const CropBounds& GetCropBounds(GemType type, int frameIndex) {
        static const CropBounds emptyBounds = {0, 0, 0, 0};
        
        if (type == GemType::HYPERCUBE) {
            if (frameIndex >= 0 && frameIndex < HYPERCUBE_FRAMES) {
                return s_hypercubeCropBounds[frameIndex];
            }
        } else {
            int gemIndex = static_cast<int>(type) - 1;
            if (gemIndex >= 0 && gemIndex < NUM_GEM_TYPES &&
                frameIndex >= 0 && frameIndex < FRAMES_PER_GEM) {
                return s_cropBounds[gemIndex][frameIndex];
            }
        }
        return emptyBounds;
    }

    bool AreStaticColorsLoaded() { return s_staticColorsLoaded; }
}