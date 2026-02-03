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

    // Per-position temporal coherence cache (8x8 grid)
    struct GemMatchCache {
        GemType type = GemType::UNKNOWN;
        int frame = -1;
    };
    static std::array<std::array<GemMatchCache, 8>, 8> s_lastMatches;
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
                       int gemRow, int gemCol,
                       float* outBestMatch,
                       int* outBestFrame, bool needDebugMap) {
if (processedImage.empty() || !MaskLoader::AreMasksLoaded()) {
    if (outBestMatch) *outBestMatch = 0.0f;
    if (outBestFrame) *outBestFrame = -1;
    return GemType::UNKNOWN;
}

static const GemType regularGems[] = {
    GemType::RED, GemType::ORANGE, GemType::YELLOW, GemType::GREEN,
    GemType::BLUE, GemType::PURPLE, GemType::WHITE
};
static const int numRegularGems = 7;
    
float threshold = ShapeParams::matchThreshold.load() / 100.0f;
GemType bestType = GemType::UNKNOWN;
float bestScore = 0.0f;
int bestFrame = -1;

// Get last match for this specific gem position
auto& cache = s_lastMatches[gemRow][gemCol];
GemType lastType = cache.type;
int lastFrame = cache.frame;

// Helper lambda to check a specific type/frame
auto checkMatch = [&](GemType type, int frame) -> float {
    const cv::Mat&refImageLab = MaskLoader::GetLabMask(type, frame);
    const cv::Mat&alphaMask = MaskLoader::GetAlphaMask(type, frame);
    if (refImageLab.empty()) return 0.0f;
    return CalculateColorSimilarity(processedImageLab, refImageLab, alphaMask);
};

// Phase 0: Temporal coherence - check last matched frame first
if (lastType != GemType::UNKNOWN && lastFrame >= 0) {
    float score = checkMatch(lastType, lastFrame);
    if (score > bestScore) {
        bestScore = score;
        bestType = lastType;
        bestFrame = lastFrame;
    }
        
    if (score >= threshold) {
        cache.type = lastType;
        cache.frame = lastFrame;
        if (outBestMatch) *outBestMatch = score;
        if (outBestFrame) *outBestFrame = lastFrame;
        if (needDebugMap) {
            const cv::Mat&refImageBgr = MaskLoader::GetMask(lastType, lastFrame);
            GenerateDebugVisualization(processedImage, processedImageLab, 
                MaskLoader::GetLabMask(lastType, lastFrame), 
                MaskLoader::GetAlphaMask(lastType, lastFrame), refImageBgr);
        }
        return lastType;
    }

    // No match - intelligent fallback based on last frame context
    bool isRegularGem = (lastType != GemType::HYPERCUBE);
    bool isFrame0 = (lastFrame == 0);
        
    if (isRegularGem && isFrame0) {
        // Was frame 0 of regular gem - check other regular gems' frame 0
        for (int i = 0; i < numRegularGems; i++) {
            if (regularGems[i] == lastType) continue; // Already checked
                
            float score = checkMatch(regularGems[i], 0);
            if (score > bestScore) {
                bestScore = score;
                bestType = regularGems[i];
                bestFrame = 0;
            }
                
            if (score >= threshold) {
                cache.type = regularGems[i];
                cache.frame = 0;
                if (outBestMatch) *outBestMatch = score;
                if (outBestFrame) *outBestFrame = 0;
                if (needDebugMap) {
                    const cv::Mat&refImageBgr = MaskLoader::GetMask(regularGems[i], 0);
                    GenerateDebugVisualization(processedImage, processedImageLab,
                        MaskLoader::GetLabMask(regularGems[i], 0),
                        MaskLoader::GetAlphaMask(regularGems[i], 0), refImageBgr);
                }
                return regularGems[i];
            }
        }
            
        // Check HYPERCUBE frame 0
        float score = checkMatch(GemType::HYPERCUBE, 0);
        if (score > bestScore) {
            bestScore = score;
            bestType = GemType::HYPERCUBE;
            bestFrame = 0;
        }
            
        if (score >= threshold) {
            cache.type = GemType::HYPERCUBE;
            cache.frame = 0;
            if (outBestMatch) *outBestMatch = score;
            if (outBestFrame) *outBestFrame = 0;
            if (needDebugMap) {
                const cv::Mat&refImageBgr = MaskLoader::GetMask(GemType::HYPERCUBE, 0);
                GenerateDebugVisualization(processedImage, processedImageLab,
                    MaskLoader::GetLabMask(GemType::HYPERCUBE, 0),
                    MaskLoader::GetAlphaMask(GemType::HYPERCUBE, 0), refImageBgr);
            }
            return GemType::HYPERCUBE;
        }
    }
    else if (isRegularGem && !isFrame0) {
        // Was non-0 frame of regular gem - check next frame in animation
        int frameCount = MaskLoader::GetFrameCount(lastType);
        int nextFrame = (lastFrame + 1) % frameCount;
            
        float score = checkMatch(lastType, nextFrame);
        if (score > bestScore) {
            bestScore = score;
            bestType = lastType;
            bestFrame = nextFrame;
        }
            
        if (score >= threshold) {
            cache.type = lastType;
            cache.frame = nextFrame;
            if (outBestMatch) *outBestMatch = score;
            if (outBestFrame) *outBestFrame = nextFrame;
            if (needDebugMap) {
                const cv::Mat&refImageBgr = MaskLoader::GetMask(lastType, nextFrame);
                GenerateDebugVisualization(processedImage, processedImageLab,
                    MaskLoader::GetLabMask(lastType, nextFrame),
                    MaskLoader::GetAlphaMask(lastType, nextFrame), refImageBgr);
            }
            return lastType;
        }
    }
    else { // HYPERCUBE
        // Was HYPERCUBE frame - check next frame in hypercube animation
        int frameCount = MaskLoader::GetFrameCount(GemType::HYPERCUBE);
        int nextFrame = (lastFrame + 1) % frameCount;
            
        float score = checkMatch(GemType::HYPERCUBE, nextFrame);
        if (score > bestScore) {
            bestScore = score;
            bestType = GemType::HYPERCUBE;
            bestFrame = nextFrame;
        }
            
        if (score >= threshold) {
            cache.type = GemType::HYPERCUBE;
            cache.frame = nextFrame;
            if (outBestMatch) *outBestMatch = score;
            if (outBestFrame) *outBestFrame = nextFrame;
            if (needDebugMap) {
                const cv::Mat&refImageBgr = MaskLoader::GetMask(GemType::HYPERCUBE, nextFrame);
                GenerateDebugVisualization(processedImage, processedImageLab,
                    MaskLoader::GetLabMask(GemType::HYPERCUBE, nextFrame),
                    MaskLoader::GetAlphaMask(GemType::HYPERCUBE, nextFrame), refImageBgr);
            }
            return GemType::HYPERCUBE;
        }
            
        // No match in HYPERCUBE animation - check all regular gems frame 0
        for (int i = 0; i < numRegularGems; i++) {
            float score = checkMatch(regularGems[i], 0);
            if (score > bestScore) {
                bestScore = score;
                bestType = regularGems[i];
                bestFrame = 0;
            }
                
            if (score >= threshold) {
                cache.type = regularGems[i];
                cache.frame = 0;
                if (outBestMatch) *outBestMatch = score;
                if (outBestFrame) *outBestFrame = 0;
                if (needDebugMap) {
                    const cv::Mat&refImageBgr = MaskLoader::GetMask(regularGems[i], 0);
                    GenerateDebugVisualization(processedImage, processedImageLab,
                        MaskLoader::GetLabMask(regularGems[i], 0),
                        MaskLoader::GetAlphaMask(regularGems[i], 0), refImageBgr);
                }
                return regularGems[i];
            }
        }
            
        // Check HYPERCUBE frame 0
        score = checkMatch(GemType::HYPERCUBE, 0);
        if (score > bestScore) {
            bestScore = score;
            bestType = GemType::HYPERCUBE;
            bestFrame = 0;
        }
            
        if (score >= threshold) {
            cache.type = GemType::HYPERCUBE;
            cache.frame = 0;
            if (outBestMatch) *outBestMatch = score;
            if (outBestFrame) *outBestFrame = 0;
            if (needDebugMap) {
                const cv::Mat&refImageBgr = MaskLoader::GetMask(GemType::HYPERCUBE, 0);
                GenerateDebugVisualization(processedImage, processedImageLab,
                    MaskLoader::GetLabMask(GemType::HYPERCUBE, 0),
                    MaskLoader::GetAlphaMask(GemType::HYPERCUBE, 0), refImageBgr);
            }
            return GemType::HYPERCUBE;
        }
    }
}

// Phase 1: Full search - check first frame of each gem type
struct FirstFrameResult {
    GemType type;
    float score;
};
FirstFrameResult firstFrameScores[8];

    // Check all regular gems frame 0 + hypercube frame 0
    for (int i = 0; i < numRegularGems; i++) {
        float score = checkMatch(regularGems[i], 0);
        firstFrameScores[i] = {regularGems[i], score};

        if (score > bestScore) {
            bestScore = score;
            bestType = regularGems[i];
            bestFrame = 0;
        }

        if (score >= threshold) {
            cache.type = regularGems[i];
            cache.frame = 0;
            if (outBestMatch) *outBestMatch = score;
            if (outBestFrame) *outBestFrame = 0;
            
            if (needDebugMap) {
                const cv::Mat&refImageBgr = MaskLoader::GetMask(regularGems[i], 0);
                GenerateDebugVisualization(processedImage, processedImageLab,
                    MaskLoader::GetLabMask(regularGems[i], 0),
                    MaskLoader::GetAlphaMask(regularGems[i], 0), refImageBgr);
            }
            
            return regularGems[i];
        }
    }
    
    // Check HYPERCUBE frame 0
    float hypercubeScore = checkMatch(GemType::HYPERCUBE, 0);
    firstFrameScores[7] = {GemType::HYPERCUBE, hypercubeScore};
    
    if (hypercubeScore > bestScore) {
        bestScore = hypercubeScore;
        bestType = GemType::HYPERCUBE;
        bestFrame = 0;
    }
    
    if (hypercubeScore >= threshold) {
        cache.type = GemType::HYPERCUBE;
        cache.frame = 0;
        if (outBestMatch) *outBestMatch = hypercubeScore;
        if (outBestFrame) *outBestFrame = 0;
        
        if (needDebugMap) {
            const cv::Mat&refImageBgr = MaskLoader::GetMask(GemType::HYPERCUBE, 0);
            GenerateDebugVisualization(processedImage, processedImageLab,
                MaskLoader::GetLabMask(GemType::HYPERCUBE, 0),
                MaskLoader::GetAlphaMask(GemType::HYPERCUBE, 0), refImageBgr);
        }
        
        return GemType::HYPERCUBE;
    }

    // Sort by first-frame score
    for (int i = 1; i < 8; i++) {
        FirstFrameResult key = firstFrameScores[i];
        int j = i - 1;
        while (j >= 0 && firstFrameScores[j].score < key.score) {
            firstFrameScores[j + 1] = firstFrameScores[j];
            j--;
        }
        firstFrameScores[j + 1] = key;
    }

    // Phase 2: Check remaining frames, prioritizing types with better first-frame scores
    for (int i = 0; i < 8; i++) {
        GemType type = firstFrameScores[i].type;
        int frameCount = MaskLoader::GetFrameCount(type);

        for (int frame = 1; frame < frameCount; frame++) {
            float score = checkMatch(type, frame);

            if (score > bestScore) {
                bestScore = score;
                bestType = type;
                bestFrame = frame;
            }

            if (score >= threshold) {
                cache.type = type;
                cache.frame = frame;
                if (outBestMatch) *outBestMatch = bestScore;
                if (outBestFrame) *outBestFrame = bestFrame;
                
                if (needDebugMap) {
                    const cv::Mat&refImageBgr = MaskLoader::GetMask(type, frame);
                    GenerateDebugVisualization(processedImage, processedImageLab,
                        MaskLoader::GetLabMask(type, frame),
                        MaskLoader::GetAlphaMask(type, frame), refImageBgr);
                }
                
                return type;
            }
        }
    }

    // Update cache with best match
    cache.type = bestType;
    cache.frame = bestFrame;

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