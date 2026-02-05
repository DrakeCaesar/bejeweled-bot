#include "shape_classifier.h"
#include "mask_loader.h"
#include "config.h"
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
        cv::createTrackbar("Debug Stage", windowName, &debugStageTrack, 5, OnDebugStageChange);
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

    if (stage == 0) return cv::Mat();  // Show nothing for maximum throughput
    if (stage == 1) return gemRegion.clone();
    if (stage == 2 && !g_debugBestRef.empty()) return g_debugBestRef.clone();
    if (stage == 3 && !g_debugBestDiffMap.empty()) return g_debugBestDiffMap.clone();
    if (stage == 4 && !g_debugBestInput.empty() && !g_debugBestDiffMap.empty()) {
        cv::Mat overlay;
        cv::addWeighted(g_debugBestInput, 0.5, g_debugBestDiffMap, 0.5, 0, overlay);
        return overlay;
    }

    return gemRegion.clone();
}

// ============================================================================
// Color Similarity Calculation
// ============================================================================

// Fast integer square root using Newton's method (portable, no floating point)
__forceinline static uint64_t isqrt(uint64_t x) {
    if (x == 0) return 0;
    if (x == 1) return 1;
    
    // Initial guess: bit_length / 2
    uint64_t result = x;
    uint64_t y = (result + 1) >> 1;
    
    while (y < result) {
        result = y;
        y = (result + x / result) >> 1;
    }
    
    return result;
}

__declspec(noinline)
static int CalculateColorSimilarity(const cv::Mat&img1Lab,
                                  const cv::Mat&img2Lab,
                                  GemType type,
                                  int frame) {
if (img1Lab.empty() || img2Lab.empty()) return 0;

// Compare LAB images - Reference already has mask applied (background = (0,128,128))
// Returns similarity score on integer scale 0-10000 (representing 0.00% to 100.00%)

// Get pre-computed crop bounds to skip padding pixels
const MaskLoader::CropBounds& bounds = MaskLoader::GetCropBounds(type, frame);

uint64_t totalDistanceSq = 0;
int pixelCount = 0;

// Use pre-computed bounds to skip leading/trailing black rows and columns
for (int y = bounds.minRow; y <= bounds.maxRow; y++) {
    const cv::Vec3b* row1 = img1Lab.ptr<cv::Vec3b>(y);
    const cv::Vec3b* row2 = img2Lab.ptr<cv::Vec3b>(y);

    for (int x = bounds.minCol; x <= bounds.maxCol; x++) {
        // Skip background pixels in reference image (already masked to black in LAB)
        if (row2[x][0] == 0 && row2[x][1] == 128 && row2[x][2] == 128) continue;

        int dL = static_cast<int>(row1[x][0]) - static_cast<int>(row2[x][0]);
        int da = static_cast<int>(row1[x][1]) - static_cast<int>(row2[x][1]);
        int db = static_cast<int>(row1[x][2]) - static_cast<int>(row2[x][2]);

        totalDistanceSq += dL * dL + da * da + db * db;
        pixelCount++;
    }
}

if (pixelCount == 0) return 0;

// Calculate similarity based on configured method
if (Config::USE_SQRT) {
    // Fast integer sqrt: similarity = 1.0 - sqrt(avgDistSq)/100.0, scale 0-10000
    uint64_t avgDistSq = totalDistanceSq / pixelCount;
    uint64_t sqrtAvg = isqrt(avgDistSq);
    return static_cast<int>(10000ULL - std::min(sqrtAvg * 100ULL, 10000ULL));
} else {
    // Squared distance without sqrt (monotonic): similarity = 1.0 - avgDistSq/10000.0, scale 0-10000
    if (totalDistanceSq >= 10000ULL * pixelCount) return 0;
    return static_cast<int>(10000ULL - (totalDistanceSq * 10000ULL) / (10000ULL * pixelCount));
}
}

__declspec(noinline)
static void GenerateDebugVisualization(const cv::Mat&img1, const cv::Mat&img1Lab,
                                       const cv::Mat&img2Lab,
                                       const cv::Mat&alphaMask, const cv::Mat&img2Bgr,
                                       GemType type, int frame) {
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

    // Reference LAB image already has mask applied (background = (0,128,128))
    const cv::Mat& lab2 = img2Lab;

    // Generate diff map
    g_debugBestDiffMap = cv::Mat::zeros(img1Lab.rows, img1Lab.cols, CV_8UC3);

    // Get pre-computed crop bounds to skip padding pixels
    const MaskLoader::CropBounds& bounds = MaskLoader::GetCropBounds(type, frame);

    for (int y = bounds.minRow; y <= bounds.maxRow; y++) {
        const cv::Vec3b* row1 = img1Lab.ptr<cv::Vec3b>(y);
        const cv::Vec3b* row2 = lab2.ptr<cv::Vec3b>(y);
        cv::Vec3b* diffRow = g_debugBestDiffMap.ptr<cv::Vec3b>(y);

        for (int x = bounds.minCol; x <= bounds.maxCol; x++) {
            // Skip background pixels in reference image (already masked to black in LAB)
            if (row2[x][0] == 0 && row2[x][1] == 128 && row2[x][2] == 128) continue;

            int dL = static_cast<int>(row1[x][0]) - static_cast<int>(row2[x][0]);
            int da = static_cast<int>(row1[x][1]) - static_cast<int>(row2[x][1]);
            int db = static_cast<int>(row1[x][2]) - static_cast<int>(row2[x][2]);

            float dist = std::sqrt(static_cast<float>(dL * dL + da * da + db * db));

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

// ============================================================================
// Gem Classification Helper Functions
// ============================================================================

// Check a specific type/frame with caching to avoid redundant comparisons
static int CheckMatchScore(
    GemType type, 
    int frame,
    const cv::Mat& processedImageLab,
    int* scoreCache,
    bool* checkedFlags,
    int maxCacheSize,
    int maxFramesPerType)
{
    int idx = static_cast<int>(type) * maxFramesPerType + frame;
    
    // Return cached score if already checked
    if (idx >= 0 && idx < maxCacheSize && checkedFlags[idx]) {
        return scoreCache[idx];
    }
    
    // Compute score
    const cv::Mat& refImageLab = MaskLoader::GetLabMask(type, frame);
    int score = 0;
    if (!refImageLab.empty()) {
        score = CalculateColorSimilarity(processedImageLab, refImageLab, type, frame);
    }
    
    // Cache result
    if (idx >= 0 && idx < maxCacheSize) {
        scoreCache[idx] = score;
        checkedFlags[idx] = true;
    }
    
    return score;
}

// Finalize a match by updating cache and output parameters
static GemType FinalizeMatch(
    GemType type,
    int frame,
    int score,
    GemMatchCache& cache,
    float* outBestMatch,
    int* outBestFrame,
    bool needDebugMap,
    const cv::Mat& processedImage,
    const cv::Mat& processedImageLab)
{
    cache.type = type;
    cache.frame = frame;
    if (outBestMatch) *outBestMatch = score / 10000.0f;  // Convert int (0-10000) to float (0.0-1.0)
    if (outBestFrame) *outBestFrame = frame;
    if (needDebugMap) {
        const cv::Mat& refImageBgr = MaskLoader::GetMask(type, frame);
        GenerateDebugVisualization(processedImage, processedImageLab,
            MaskLoader::GetLabMask(type, frame),
            MaskLoader::GetAlphaMask(type, frame), refImageBgr, type, frame);
    }
    return type;
}

// Try matching a specific type/frame, updating best score and returning if threshold met
static bool TryMatch(
    GemType type,
    int frame,
    const cv::Mat& processedImageLab,
    int* scoreCache,
    bool* checkedFlags,
    int maxCacheSize,
    int maxFramesPerType,
    int threshold,
    GemType& bestType,
    int& bestScore,
    int& bestFrame)
{
    int score = CheckMatchScore(type, frame, processedImageLab, 
                                   scoreCache, checkedFlags, 
                                   maxCacheSize, maxFramesPerType);
    if (score > bestScore) {
        bestScore = score;
        bestType = type;
        bestFrame = frame;
    }
    return score >= threshold;
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

// All gem types including hypercube for unified iteration
static const GemType allGemTypes[] = {
    GemType::RED, GemType::ORANGE, GemType::YELLOW, GemType::GREEN,
    GemType::BLUE, GemType::PURPLE, GemType::WHITE, GemType::HYPERCUBE
};
static const int numAllGemTypes = 8;

// Use integer threshold (0-10000 scale representing 0.00%-100.00%)
int threshold = ShapeParams::matchThreshold.load() * 100;
GemType bestType = GemType::UNKNOWN;
int bestScore = 0;
int bestFrame = -1;

// Get last match for this specific gem position
auto& cache = s_lastMatches[gemRow][gemCol];
GemType lastType = cache.type;
int lastFrame = cache.frame;

// Track checked frames to avoid redundant comparisons (max 8 types * ~30 frames = 240 entries)
// Use array indexed by (type * MAX_FRAMES + frame) for fast lookup
constexpr int MAX_FRAMES_PER_TYPE = 32;  // Conservative upper bound
constexpr int MAX_CACHE_SIZE = 8 * MAX_FRAMES_PER_TYPE;
thread_local int scoreCache[MAX_CACHE_SIZE];
thread_local bool checkedFlags[MAX_CACHE_SIZE];
std::fill(std::begin(checkedFlags), std::end(checkedFlags), false);

// Phase 0: Temporal coherence - check cached match and next animation frame
if (lastType != GemType::UNKNOWN && lastFrame >= 0) {
    // Check the exact cached position first
    if (TryMatch(lastType, lastFrame, processedImageLab, 
                 scoreCache, checkedFlags, MAX_CACHE_SIZE, MAX_FRAMES_PER_TYPE,
                 threshold, bestType, bestScore, bestFrame)) {
        return FinalizeMatch(lastType, lastFrame, bestScore, cache,
                            outBestMatch, outBestFrame, needDebugMap,
                            processedImage, processedImageLab);
    }
    
    // If we were in an animation, check the next frame
    if (lastFrame > 0) {
        int frameCount = MaskLoader::GetFrameCount(lastType);
        int nextFrame = (lastFrame + 1) % frameCount;
        if (nextFrame != 0 && TryMatch(lastType, nextFrame, processedImageLab,
                                       scoreCache, checkedFlags, MAX_CACHE_SIZE, MAX_FRAMES_PER_TYPE,
                                       threshold, bestType, bestScore, bestFrame)) {
            return FinalizeMatch(lastType, nextFrame, bestScore, cache,
                                outBestMatch, outBestFrame, needDebugMap,
                                processedImage, processedImageLab);
        }
    }
}

// Phase 1: Check frame 0 of all gem types (cache prevents redundant computation)
struct FirstFrameResult {
    GemType type;
    int score;
};
FirstFrameResult firstFrameScores[8];

for (int i = 0; i < numAllGemTypes; i++) {
    int score = CheckMatchScore(allGemTypes[i], 0, processedImageLab,
                                   scoreCache, checkedFlags,
                                   MAX_CACHE_SIZE, MAX_FRAMES_PER_TYPE);
    firstFrameScores[i] = {allGemTypes[i], score};
    
    if (score > bestScore) {
        bestScore = score;
        bestType = allGemTypes[i];
        bestFrame = 0;
    }
    
    if (score >= threshold) {
        return FinalizeMatch(allGemTypes[i], 0, score, cache,
                            outBestMatch, outBestFrame, needDebugMap,
                            processedImage, processedImageLab);
    }
}

// Sort by first-frame score to prioritize likely matches in Phase 2
for (int i = 1; i < numAllGemTypes; i++) {
    FirstFrameResult key = firstFrameScores[i];
    int j = i - 1;
    while (j >= 0 && firstFrameScores[j].score < key.score) {
        firstFrameScores[j + 1] = firstFrameScores[j];
        j--;
    }
    firstFrameScores[j + 1] = key;
}

// Phase 2: Check remaining frames, prioritizing types with better first-frame scores
for (int i = 0; i < numAllGemTypes; i++) {
    GemType type = firstFrameScores[i].type;
    int frameCount = MaskLoader::GetFrameCount(type);

    for (int frame = 1; frame < frameCount; frame++) {
        if (TryMatch(type, frame, processedImageLab,
                     scoreCache, checkedFlags, MAX_CACHE_SIZE, MAX_FRAMES_PER_TYPE,
                     threshold, bestType, bestScore, bestFrame)) {
            return FinalizeMatch(type, frame, bestScore, cache,
                                outBestMatch, outBestFrame, needDebugMap,
                                processedImage, processedImageLab);
        }
    }
}

// Update cache with best match (reached when no match >= threshold was found)
cache.type = bestType;
cache.frame = bestFrame;

if (outBestMatch) *outBestMatch = bestScore / 10000.0f;  // Convert int (0-10000) to float (0.0-1.0)
if (outBestFrame) *outBestFrame = bestFrame;

// Generate debug visualization for best match if needed
if (needDebugMap && bestType != GemType::UNKNOWN && bestFrame >= 0) {
    const cv::Mat&refImageLab = MaskLoader::GetLabMask(bestType, bestFrame);
    const cv::Mat&refImageBgr = MaskLoader::GetMask(bestType, bestFrame);
    const cv::Mat&alphaMask = MaskLoader::GetAlphaMask(bestType, bestFrame);
    if (!refImageLab.empty()) {
        GenerateDebugVisualization(processedImage, processedImageLab, refImageLab, alphaMask, refImageBgr, bestType, bestFrame);
    }
}

// Integer comparison: bestScore is 0-10000, threshold is 0-10000
if (bestScore < Config::GEM_MATCH_FINAL_THRESHOLD_PERCENT * 100) return GemType::UNKNOWN;

return bestType;
}