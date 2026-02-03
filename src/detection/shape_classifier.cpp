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

static float CalculateColorSimilarity(const cv::Mat&img1Lab,
                                  const cv::Mat&img2Lab,
                                  const cv::Mat&alphaMask) {
if (img1Lab.empty() || img2Lab.empty()) return 0.0f;

// Apply mask to LAB image for comparison
cv::Mat lab1 = img1Lab.clone();
if (!alphaMask.empty()) {
    for (int y = 0; y < lab1.rows; y++) {
        cv::Vec3b* labRow = lab1.ptr<cv::Vec3b>(y);
        const uchar* maskRow = alphaMask.ptr<uchar>(y);
        for (int x = 0; x < lab1.cols; x++) {
            if (maskRow[x] == 0) {
                labRow[x] = cv::Vec3b(0, 128, 128);  // Black in LAB space
            }
        }
    }
}

const cv::Mat& lab2 = img2Lab;

double totalDistance = 0.0;
int pixelCount = 0;

for (int y = 0; y < lab1.rows; y++) {
    const cv::Vec3b* row1 = lab1.ptr<cv::Vec3b>(y);
    const cv::Vec3b* row2 = lab2.ptr<cv::Vec3b>(y);

    for (int x = 0; x < lab1.cols; x++) {
        if (row1[x][0] == 0 && row1[x][1] == 128 && row1[x][2] == 128) continue;

        float dL = static_cast<float>(row1[x][0]) - static_cast<float>(row2[x][0]);
        float da = static_cast<float>(row1[x][1]) - static_cast<float>(row2[x][1]);
        float db = static_cast<float>(row1[x][2]) - static_cast<float>(row2[x][2]);

        float dist = std::sqrt(dL * dL + da * da + db * db);
        totalDistance += dist;
        pixelCount++;
    }
}

if (pixelCount == 0) return 0.0f;

double avgDistance = totalDistance / pixelCount;
double similarity = 1.0 - std::min(avgDistance / 100.0, 1.0);

return static_cast<float>(similarity);
}

static void GenerateDebugVisualization(const cv::Mat&img1, const cv::Mat&img1Lab,
                                       const cv::Mat&img2Lab,
                                       const cv::Mat&alphaMask, const cv::Mat&img2Bgr) {
    if (img1.empty() || img2Lab.empty() || img1Lab.empty()) return;

    // Apply mask to BGR for debug visualization
    cv::Mat i1 = img1.clone();
    if (!alphaMask.empty()) {
        for (int y = 0; y < i1.rows; y++) {
            cv::Vec3b* row1 = i1.ptr<cv::Vec3b>(y);
            const uchar* maskRow = alphaMask.ptr<uchar>(y);
            for (int x = 0; x < i1.cols; x++) {
                if (maskRow[x] == 0) {
                    row1[x] = cv::Vec3b(0, 0, 0);
                }
            }
        }
    }

    g_debugBestInput = i1;
    g_debugBestRef = img2Bgr.empty() ? cv::Mat() : img2Bgr.clone();

    // Apply mask to LAB image
    cv::Mat lab1 = img1Lab.clone();
    if (!alphaMask.empty()) {
        for (int y = 0; y < lab1.rows; y++) {
            cv::Vec3b* labRow = lab1.ptr<cv::Vec3b>(y);
            const uchar* maskRow = alphaMask.ptr<uchar>(y);
            for (int x = 0; x < lab1.cols; x++) {
                if (maskRow[x] == 0) {
                    labRow[x] = cv::Vec3b(0, 128, 128);
                }
            }
        }
    }

    const cv::Mat& lab2 = img2Lab;

    // Generate diff map
    g_debugBestDiffMap = cv::Mat::zeros(lab1.rows, lab1.cols, CV_8UC3);

    for (int y = 0; y < lab1.rows; y++) {
        const cv::Vec3b* row1 = lab1.ptr<cv::Vec3b>(y);
        const cv::Vec3b* row2 = lab2.ptr<cv::Vec3b>(y);
        cv::Vec3b* diffRow = g_debugBestDiffMap.ptr<cv::Vec3b>(y);

        for (int x = 0; x < lab1.cols; x++) {
            if (row1[x][0] == 0 && row1[x][1] == 128 && row1[x][2] == 128) continue;

            float dL = static_cast<float>(row1[x][0]) - static_cast<float>(row2[x][0]);
            float da = static_cast<float>(row1[x][1]) - static_cast<float>(row2[x][1]);
            float db = static_cast<float>(row1[x][2]) - static_cast<float>(row2[x][2]);

            float dist = std::sqrt(dL * dL + da * da + db * db);

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
}

GemType ClassifyGemByShape(const cv::Mat&processedImage, const cv::Mat&processedImageLab,
                           float* outBestMatch,
                           int* outBestFrame, bool needDebugMap) {
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
        const cv::Mat&refImageLab = MaskLoader::GetLabMask(lastMatchedType, lastMatchedFrame);
        const cv::Mat&alphaMask = MaskLoader::GetAlphaMask(lastMatchedType, lastMatchedFrame);
        
        if (!refImageLab.empty()) {
            float score = CalculateColorSimilarity(processedImageLab, refImageLab, alphaMask);
            
            if (score > bestScore) {
                bestScore = score;
                bestType = lastMatchedType;
                bestFrame = lastMatchedFrame;
            }

            if (score >= threshold) {
                if (outBestMatch) *outBestMatch = score;
                if (outBestFrame) *outBestFrame = lastMatchedFrame;
                
                if (needDebugMap) {
                    const cv::Mat&refImageBgr = MaskLoader::GetMask(lastMatchedType, lastMatchedFrame);
                    GenerateDebugVisualization(processedImage, processedImageLab, refImageLab, alphaMask, refImageBgr);
                }
                
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

        const cv::Mat&refImageLab = MaskLoader::GetLabMask(type, 0);
        const cv::Mat&alphaMask = MaskLoader::GetAlphaMask(type, 0);
        float score = 0.0f;

        if (!refImageLab.empty()) {
            score = CalculateColorSimilarity(processedImageLab, refImageLab, alphaMask);
        }

        firstFrameScores[i] = {type, score};

        if (score > bestScore) {
            bestScore = score;
            bestType = type;
            bestFrame = 0;
        }

        if (score >= threshold) {
            lastMatchedType = type;
            lastMatchedFrame = 0;
            if (outBestMatch) *outBestMatch = score;
            if (outBestFrame) *outBestFrame = 0;
            
            if (needDebugMap) {
                const cv::Mat&refImageBgr = MaskLoader::GetMask(type, 0);
                GenerateDebugVisualization(processedImage, processedImageLab, refImageLab, alphaMask, refImageBgr);
            }
            
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

            const cv::Mat&refImageLab = MaskLoader::GetLabMask(type, frame);
            const cv::Mat&alphaMask = MaskLoader::GetAlphaMask(type, frame);
            if (refImageLab.empty()) continue;

            float score = CalculateColorSimilarity(processedImageLab, refImageLab, alphaMask);

            if (score > bestScore) {
                bestScore = score;
                bestType = type;
                bestFrame = frame;
            }

            if (score >= threshold) {
                lastMatchedType = bestType;
                lastMatchedFrame = bestFrame;
                if (outBestMatch) *outBestMatch = bestScore;
                if (outBestFrame) *outBestFrame = bestFrame;
                
                if (needDebugMap) {
                    const cv::Mat&refImageBgr = MaskLoader::GetMask(type, frame);
                    GenerateDebugVisualization(processedImage, processedImageLab, refImageLab, alphaMask, refImageBgr);
                }
                
                return bestType;
            }
        }
    }

    // Update last matched even if below threshold (use best match)
    lastMatchedType = bestType;
    lastMatchedFrame = bestFrame;

    if (outBestMatch) *outBestMatch = bestScore;
    if (outBestFrame) *outBestFrame = bestFrame;

    // Generate debug visualization for best match if needed
    if (needDebugMap && bestType != GemType::UNKNOWN && bestFrame >= 0) {
        const cv::Mat&refImageLab = MaskLoader::GetLabMask(bestType, bestFrame);
        const cv::Mat&refImageBgr = MaskLoader::GetMask(bestType, bestFrame);
        const cv::Mat&alphaMask = MaskLoader::GetAlphaMask(bestType, bestFrame);
        if (!refImageLab.empty()) {
            GenerateDebugVisualization(processedImage, processedImageLab, refImageLab, alphaMask, refImageBgr);
        }
    }

    if (bestScore < 0.4f) return GemType::UNKNOWN;

    return bestType;
}