#include "shape_classifier.h"
#include "mask_loader.h"
#include <cmath>
#include <opencv2/opencv.hpp>

// ============================================================================
// Trackbar Creation for Real-time Parameter Adjustment
// ============================================================================

namespace ShapeParams {
// Trackbar backing variables (OpenCV requires int pointers)
    static int debugStageTrack = debugStage.load();
    static int matchThresholdTrack = matchThreshold.load();

    static void OnDebugStageChange(int value, void*) { debugStage.store(value); }

    static void OnMatchThresholdChange(int value, void*) {
        int v = value;
        if (v < 1) v = 1;
        if (v > 100) v = 100;
        matchThreshold.store(v);
    }

    void CreateTrackbars(const std::string&windowName) {
        cv::createTrackbar("Debug Stage", windowName, &debugStageTrack, 4, OnDebugStageChange);
        cv::createTrackbar("Match Thresh %", windowName, &matchThresholdTrack, 100, OnMatchThresholdChange);
    }
} // namespace ShapeParams

// ============================================================================
// Shape Processing - Direct Color Image Comparison
// ============================================================================

// Thread-local storage for debug visualizations
namespace {
    thread_local cv::Mat g_debugMaskedInput;
    thread_local cv::Mat g_debugMaskedRef;
    thread_local cv::Mat g_debugDiffMap;
    thread_local cv::Mat g_debugBestInput;
    thread_local cv::Mat g_debugBestRef;
    thread_local cv::Mat g_debugBestDiffMap;
}

cv::Mat ProcessGemForShapeMatching(const cv::Mat&gemRegion) {
    if (gemRegion.empty()) {
        return cv::Mat();
    }
    return gemRegion.clone();
}

cv::Mat GetDebugVisualization(const cv::Mat&gemRegion) {
    int stage = ShapeParams::debugStage.load();

    if (stage == 0) return gemRegion.clone();
    if (stage == 1 && !g_debugBestRef.empty()) return g_debugBestRef.clone();
    if (stage == 2 && !g_debugBestDiffMap.empty()) return g_debugBestDiffMap.clone();
    if (stage == 3 && !g_debugBestInput.empty() && !g_debugBestDiffMap.empty()) {
        cv::Mat overlay;
        cv::addWeighted(g_debugBestInput, 0.5, g_debugBestDiffMap, 0.5, 0, overlay);
        return overlay;
    }

    return gemRegion.clone();
}

// ============================================================================
// Color Similarity Calculation
// ============================================================================

static float CalculateColorSimilarity(const cv::Mat&img1, const cv::Mat&img2,
                                      const cv::Mat&alphaMask = cv::Mat()) {
    if (img1.empty() || img2.empty()) return 0.0f;

    cv::Mat i1 = img1.clone();
    cv::Mat i2 = img2.clone();
    cv::Mat mask = alphaMask.empty() ? cv::Mat() : alphaMask.clone();

    // Resize reference to match input if needed (scaling already applied in cache)
    if (i2.size() != i1.size()) {
        cv::resize(i2, i2, i1.size(), 0, 0, cv::INTER_LANCZOS4);
    }

    // Resize mask to match input if needed
    if (!mask.empty() && mask.size() != i1.size()) {
        cv::resize(mask, mask, i1.size(), 0, 0, cv::INTER_NEAREST);
    }

    // Apply mask to BOTH images
    if (!mask.empty()) {
        for (int y = 0; y < i1.rows; y++) {
            cv::Vec3b* row1 = i1.ptr<cv::Vec3b>(y);
            cv::Vec3b* row2 = i2.ptr<cv::Vec3b>(y);
            const uchar* maskRow = mask.ptr<uchar>(y);
            for (int x = 0; x < i1.cols; x++) {
                if (maskRow[x] == 0) {
                    row1[x] = cv::Vec3b(0, 0, 0);
                    row2[x] = cv::Vec3b(0, 0, 0);
                }
            }
        }
    }

    g_debugMaskedInput = i1.clone();
    g_debugMaskedRef = i2.clone();

    thread_local cv::Mat lab1, lab2;
    cv::cvtColor(i1, lab1, cv::COLOR_BGR2Lab);
    cv::cvtColor(i2, lab2, cv::COLOR_BGR2Lab);

    g_debugDiffMap = cv::Mat::zeros(lab1.rows, lab1.cols, CV_8UC3);

    double totalDistance = 0.0;
    int pixelCount = 0;

    for (int y = 0; y < lab1.rows; y++) {
        const cv::Vec3b* row1 = lab1.ptr<cv::Vec3b>(y);
        const cv::Vec3b* row2 = lab2.ptr<cv::Vec3b>(y);
        cv::Vec3b* diffRow = g_debugDiffMap.ptr<cv::Vec3b>(y);

        for (int x = 0; x < lab1.cols; x++) {
            if (row1[x][0] == 0 && row1[x][1] == 128 && row1[x][2] == 128) continue;

            float dL = static_cast<float>(row1[x][0]) - static_cast<float>(row2[x][0]);
            float da = static_cast<float>(row1[x][1]) - static_cast<float>(row2[x][1]);
            float db = static_cast<float>(row1[x][2]) - static_cast<float>(row2[x][2]);

            float dist = std::sqrt(dL * dL + da * da + db * db);
            totalDistance += dist;
            pixelCount++;

            if (dist < 10.0f) {
                diffRow[x] = cv::Vec3b(0, 0, 0);
            }
            else {
                float normalized = std::min((dist - 10.0f) / 90.0f, 1.0f);
                uchar intensity = static_cast<uchar>(normalized * 255.0f);
                if (normalized < 0.5f) {
                    diffRow[x] = cv::Vec3b(0, intensity * 2, intensity * 2);
                }
                else {
                    diffRow[x] = cv::Vec3b(0, static_cast<uchar>((1.0f - normalized) * 510.0f), 255);
                }
            }
        }
    }

    if (pixelCount == 0) return 0.0f;

    double avgDistance = totalDistance / pixelCount;
    double similarity = 1.0 - std::min(avgDistance / 100.0, 1.0);

    return static_cast<float>(similarity);
}

static void SaveAsBestMatch() {
    g_debugBestInput = g_debugMaskedInput.clone();
    g_debugBestRef = g_debugMaskedRef.clone();
    g_debugBestDiffMap = g_debugDiffMap.clone();
}

GemType ClassifyGemByShape(const cv::Mat&processedImage, float* outBestMatch,
                           int* outBestFrame) {
    if (processedImage.empty() || !MaskLoader::AreMasksLoaded()) {
        if (outBestMatch) *outBestMatch = 0.0f;
        if (outBestFrame) *outBestFrame = -1;
        return GemType::UNKNOWN;
    }

    static const GemType gemTypes[] = {
        GemType::RED, GemType::ORANGE, GemType::YELLOW, GemType::GREEN,
        GemType::BLUE, GemType::PURPLE, GemType::WHITE, GemType::HYPERCUBE
    };
    static const int numTypes = 8;

    float threshold = ShapeParams::matchThreshold.load() / 100.0f;
    GemType bestType = GemType::UNKNOWN;
    float bestScore = 0.0f;
    int bestFrame = -1;

    // Track last match for temporal coherence optimization
    thread_local GemType lastMatchedType = GemType::UNKNOWN;
    thread_local int lastMatchedFrame = -1;

    // Fast path: Check last matched frame first (temporal coherence)
    if (lastMatchedType != GemType::UNKNOWN && lastMatchedFrame >= 0) {
        const cv::Mat&refImage = MaskLoader::GetMask(lastMatchedType, lastMatchedFrame);
        const cv::Mat&alphaMask = MaskLoader::GetAlphaMask(lastMatchedType, lastMatchedFrame);
        
        if (!refImage.empty()) {
            float score = CalculateColorSimilarity(processedImage, refImage, alphaMask);
            
            if (score > bestScore) {
                bestScore = score;
                bestType = lastMatchedType;
                bestFrame = lastMatchedFrame;
                SaveAsBestMatch();
            }

            if (score >= threshold) {
                if (outBestMatch) *outBestMatch = score;
                if (outBestFrame) *outBestFrame = lastMatchedFrame;
                return lastMatchedType;
            }
        }
    }

    struct FirstFrameResult {
        GemType type;
        float score;
    };
    FirstFrameResult firstFrameScores[8]; // Updated for 8 types

    // Phase 1: Check first frame of each type (skip if already checked above)
    for (int i = 0; i < numTypes; i++) {
        GemType type = gemTypes[i];
        
        // Skip if this was the last matched frame
        if (type == lastMatchedType && lastMatchedFrame == 0) {
            firstFrameScores[i] = {type, bestScore};
            continue;
        }

        const cv::Mat&refImage = MaskLoader::GetMask(type, 0);
        const cv::Mat&alphaMask = MaskLoader::GetAlphaMask(type, 0);
        float score = 0.0f;

        if (!refImage.empty()) {
            score = CalculateColorSimilarity(processedImage, refImage, alphaMask);
        }

        firstFrameScores[i] = {type, score};

        if (score > bestScore) {
            bestScore = score;
            bestType = type;
            bestFrame = 0;
            SaveAsBestMatch();
        }

        if (score >= threshold) {
            lastMatchedType = type;
            lastMatchedFrame = 0;
            if (outBestMatch) *outBestMatch = score;
            if (outBestFrame) *outBestFrame = 0;
            return type;
        }
    }

    // Sort by first-frame score
    for (int i = 1; i < numTypes; i++) {
        FirstFrameResult key = firstFrameScores[i];
        int j = i - 1;
        while (j >= 0 && firstFrameScores[j].score < key.score) {
            firstFrameScores[j + 1] = firstFrameScores[j];
            j--;
        }
        firstFrameScores[j + 1] = key;
    }

    // Phase 2: Check remaining frames
    for (int i = 0; i < numTypes; i++) {
        GemType type = firstFrameScores[i].type;
        int frameCount = MaskLoader::GetFrameCount(type);

        for (int frame = 1; frame < frameCount; frame++) {
            // Skip if this was the last matched frame
            if (type == lastMatchedType && frame == lastMatchedFrame) {
                continue;
            }

            const cv::Mat&refImage = MaskLoader::GetMask(type, frame);
            const cv::Mat&alphaMask = MaskLoader::GetAlphaMask(type, frame);
            if (refImage.empty()) continue;

            float score = CalculateColorSimilarity(processedImage, refImage, alphaMask);

            if (score > bestScore) {
                bestScore = score;
                bestType = type;
                bestFrame = frame;
                SaveAsBestMatch();
            }

            if (score >= threshold) {
                lastMatchedType = bestType;
                lastMatchedFrame = bestFrame;
                if (outBestMatch) *outBestMatch = bestScore;
                if (outBestFrame) *outBestFrame = bestFrame;
                return bestType;
            }
        }
    }

    // Update last matched even if below threshold (use best match)
    lastMatchedType = bestType;
    lastMatchedFrame = bestFrame;

    if (outBestMatch) *outBestMatch = bestScore;
    if (outBestFrame) *outBestFrame = bestFrame;

    if (bestScore < 0.4f) return GemType::UNKNOWN;

    return bestType;
}