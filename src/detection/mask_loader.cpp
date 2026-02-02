#include "mask_loader.h"
#include <iostream>

namespace MaskLoader {
// Static storage for masks
    static MaskStorage s_masks;
    static AlphaMaskStorage s_alphaMasks;
    static RingMaskStorage s_ringMasks;
    static HypercubeMaskStorage s_hypercubeMasks;
    static HypercubeAlphaMaskStorage s_hypercubeAlphaMasks;
    static HypercubeRingMaskStorage s_hypercubeRingMasks;
    static bool s_loaded = false;
    static cv::Mat s_emptyMask;

// Static color reference images (first frame replacements for Phase 1)
    static MaskStorage s_staticColorMasks;
    static bool s_staticColorsLoaded = false;

    static cv::Mat BuildRingMask(const cv::Mat&alphaMask) {
        if (alphaMask.empty()) {
            return cv::Mat();
        }

        constexpr double kRingScale = 1.15;
        cv::Mat scaled;
        cv::resize(alphaMask, scaled, cv::Size(), kRingScale, kRingScale, cv::INTER_NEAREST);

        // Ensure binary
        cv::threshold(scaled, scaled, 127, 255, cv::THRESH_BINARY);

        // Scale the mask by 105% to remove edge pixels
        constexpr double kInnerScale = 1.05;
        cv::Mat innerMask;
        cv::resize(alphaMask, innerMask, cv::Size(), kInnerScale, kInnerScale, cv::INTER_NEAREST);
        cv::threshold(innerMask, innerMask, 127, 255, cv::THRESH_BINARY);

        // Center the 95%-scaled mask inside the scaled mask and invert it
        cv::Mat centered = cv::Mat::zeros(scaled.size(), scaled.type());
        int offsetX = (scaled.cols - innerMask.cols) / 2;
        int offsetY = (scaled.rows - innerMask.rows) / 2;
        if (offsetX >= 0 && offsetY >= 0 &&
            offsetX + innerMask.cols <= scaled.cols &&
            offsetY + innerMask.rows <= scaled.rows) {
            innerMask.copyTo(centered(cv::Rect(offsetX, offsetY, innerMask.cols, innerMask.rows)));
        }

        cv::Mat centeredInverted;
        cv::bitwise_not(centered, centeredInverted);

        // Apply inverted center to scaled mask to keep only the ring
        cv::Mat ringScaled;
        cv::bitwise_and(scaled, centeredInverted, ringScaled);

        // Crop back to original size (centered)
        cv::Mat ring;
        if (ringScaled.cols >= alphaMask.cols && ringScaled.rows >= alphaMask.rows) {
            int startX = (ringScaled.cols - alphaMask.cols) / 2;
            int startY = (ringScaled.rows - alphaMask.rows) / 2;
            ring = ringScaled(cv::Rect(startX, startY, alphaMask.cols, alphaMask.rows)).clone();
        }
        else {
            ring = cv::Mat::zeros(alphaMask.size(), alphaMask.type());
            int dstX = (alphaMask.cols - ringScaled.cols) / 2;
            int dstY = (alphaMask.rows - ringScaled.rows) / 2;
            ringScaled.copyTo(ring(cv::Rect(dstX, dstY, ringScaled.cols, ringScaled.rows)));
        }

        return ring;
    }

// Map GemType to filename
    static const char* GemTypeToFilename(GemType type) {
        switch (type) {
            case GemType::RED:
                return "red.png";
            case GemType::ORANGE:
                return "orange.png";
            case GemType::YELLOW:
                return "yellow.png";
            case GemType::GREEN:
                return "green.png";
            case GemType::BLUE:
                return "blue.png";
            case GemType::PURPLE:
                return "purple.png";
            case GemType::WHITE:
                return "white.png";
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

        const char* filename = GemTypeToFilename(type);
        if (!filename)
            return false;

        int gemIndex = GemTypeToIndex(type);
        if (gemIndex < 0)
            return false;

        // Load the color sprite sheet
        std::string colorPath = assetsPath + "/color/" + filename;
        cv::Mat spriteSheet = cv::imread(colorPath, cv::IMREAD_COLOR);
        if (spriteSheet.empty()) {
            std::cerr << "Failed to load reference image: " << colorPath << std::endl;
            return false;
        }

        // Load the alpha mask sprite sheet
        std::string maskPath = assetsPath + "/mask/" + filename;
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

                // Extract and clone the color frame
                cv::Mat colorFrame = spriteSheet(roi).clone();

                // Extract and clone the mask frame
                cv::Mat maskFrame = maskSheet(roi).clone();

                // Threshold mask to ensure binary (white = foreground, black = background)
                cv::threshold(maskFrame, maskFrame, 254, 255, cv::THRESH_BINARY);

                // Erode mask by 1 pixel to clean up edges for rotating frames
                cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
                cv::erode(maskFrame, maskFrame, kernel, cv::Point(-1, -1), 1);

                // Build ring mask (scaled alpha minus original)
                cv::Mat ringMask = BuildRingMask(maskFrame);

                // Apply mask to color frame (set background to black)
                for (int y = 0; y < colorFrame.rows; y++) {
                    cv::Vec3b* colorRow = colorFrame.ptr<cv::Vec3b>(y);
                    const uchar* maskRow = maskFrame.ptr<uchar>(y);

                    for (int x = 0; x < colorFrame.cols; x++) {
                        if (maskRow[x] == 0) {
                            // Background pixel - set to black
                            colorRow[x] = cv::Vec3b(0, 0, 0);
                        }
                    }
                }

                s_masks[gemIndex][frameIndex] = colorFrame;
                s_alphaMasks[gemIndex][frameIndex] = maskFrame;
                s_ringMasks[gemIndex][frameIndex] = ringMask;
                frameIndex++;
            }
        }

        std::cout << "Loaded " << (frameIndex - 1) << " animated frames for "
                << GemTypeToString(type) << " with alpha masks" << std::endl;
        return true;
    }

    bool LoadStaticColorMasks(const std::string&assetsPath) {
        bool success = true;

        static const GemType gemTypes[] = {
            GemType::RED, GemType::ORANGE, GemType::YELLOW, GemType::GREEN,
            GemType::BLUE, GemType::PURPLE, GemType::WHITE
        };

        for (GemType type: gemTypes) {
            const char* filename = GemTypeToFilename(type);
            if (!filename) {
                success = false;
                continue;
            }

            int gemIndex = GemTypeToIndex(type);
            if (gemIndex < 0) {
                success = false;
                continue;
            }

            // Load static color image from static_color folder
            std::string staticPath = assetsPath + "/static_color/" + filename;
            cv::Mat staticImage = cv::imread(staticPath, cv::IMREAD_COLOR);
            if (staticImage.empty()) {
                std::cerr << "Failed to load static color image: " << staticPath << std::endl;
                success = false;
                continue;
            }

            // Resize to standard FRAME_WIDTH x FRAME_HEIGHT if needed
            if (staticImage.cols != FRAME_WIDTH || staticImage.rows != FRAME_HEIGHT) {
                cv::Mat resized;
                cv::resize(staticImage, resized, cv::Size(FRAME_WIDTH, FRAME_HEIGHT), 0, 0, cv::INTER_LANCZOS4);
                staticImage = resized;
            }

            // Generate alpha mask from black background
            cv::Mat alphaMask = cv::Mat::zeros(staticImage.size(), CV_8UC1);
            for (int y = 0; y < staticImage.rows; y++) {
                const cv::Vec3b* colorRow = staticImage.ptr<cv::Vec3b>(y);
                uchar* maskRow = alphaMask.ptr<uchar>(y);
                for (int x = 0; x < staticImage.cols; x++) {
                    if (colorRow[x][0] > 0 || colorRow[x][1] > 0 || colorRow[x][2] > 0) {
                        maskRow[x] = 255;
                    }
                }
            }

            // Store as frame 0 in main storage
            s_masks[gemIndex][0] = staticImage;
            s_alphaMasks[gemIndex][0] = alphaMask;
            s_ringMasks[gemIndex][0] = BuildRingMask(alphaMask);

            std::cout << "Loaded static color reference for " << GemTypeToString(type) << std::endl;
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

                cv::Mat colorFrame = spriteSheet(roi).clone();
                cv::Mat maskFrame = maskSheet(roi).clone();

                cv::threshold(maskFrame, maskFrame, 127, 255, cv::THRESH_BINARY);

                cv::Mat ringMask = BuildRingMask(maskFrame);

                // Apply mask to color
                for (int y = 0; y < colorFrame.rows; y++) {
                    cv::Vec3b* colorRow = colorFrame.ptr<cv::Vec3b>(y);
                    const uchar* maskRow = maskFrame.ptr<uchar>(y);
                    for (int x = 0; x < colorFrame.cols; x++) {
                        if (maskRow[x] == 0) {
                            colorRow[x] = cv::Vec3b(0, 0, 0);
                        }
                    }
                }

                s_hypercubeMasks[frameIndex] = colorFrame;
                s_hypercubeAlphaMasks[frameIndex] = maskFrame;
                s_hypercubeRingMasks[frameIndex] = ringMask;
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

    int GetFrameCount(GemType type) {
        if (type == GemType::HYPERCUBE) {
            return HYPERCUBE_FRAMES;
        }
        return FRAMES_PER_GEM;
    }

    bool AreStaticColorsLoaded() { return s_staticColorsLoaded; }
} // namespace MaskLoader