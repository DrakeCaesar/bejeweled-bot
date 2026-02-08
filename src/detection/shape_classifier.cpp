#include "shape_classifier.h"
#include "loader.h"
#include "config.h"
#include <cmath>
#include <opencv2/opencv.hpp>

#ifdef _MSC_VER
#include <immintrin.h>
#endif

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

// Helper: Iterate over all pixels (no longer crop-bounded - using bitmasks for optimization)
template<typename Callback>
static void IterateAllPixels(const cv::Mat& img1Lab, const cv::Mat& img2Lab, Callback cb) {
    // Background color constants
    const uchar bgL = 0;
    const uchar bgA = 128;
    const uchar bgB = 128;

    // Process all pixels
    for (int y = 0; y < img1Lab.rows; y++) {
        const cv::Vec3b* row1 = img1Lab.ptr<cv::Vec3b>(y);
        const cv::Vec3b* row2 = img2Lab.ptr<cv::Vec3b>(y);

        for (int x = 0; x < img1Lab.cols; x++) {
            if (row2[x][0] == bgL && row2[x][1] == bgA && row2[x][2] == bgB) continue;

            int dL = static_cast<int>(row1[x][0]) - static_cast<int>(row2[x][0]);
            int da = static_cast<int>(row1[x][1]) - static_cast<int>(row2[x][1]);
            int db = static_cast<int>(row1[x][2]) - static_cast<int>(row2[x][2]);

            cb(x, y, dL, da, db);
        }
    }
}

cv::Mat ProcessGemForShapeMatching(const cv::Mat&gemRegion) {
    // No-op wrapper maintained for API compatibility
    // Returns a clone of the region (or empty if input is empty)
    return gemRegion.empty() ? cv::Mat() : gemRegion.clone();
}

// SIMD-optimized version of CalculateColorSimilarity using AVX2 intrinsics
// Processes 8 pixels simultaneously to eliminate per-pixel branching
static int CalculateColorSimilarity(const cv::Mat& capturedLab,
                                        const cv::Mat& referenceLab,
                                        const std::vector<uint64_t>& bitmask) {
    __m256i totalDistSq = _mm256_setzero_si256();
    int pixelCount = 0;

    // Generate mask lookup table on first call - converts 8-bit mask to 8 integer masks
    // Each entry: if bit i is set, value is -1 (all 1s), else 0
    alignas(32) static int32_t maskTable[256][8];
    static bool maskTableInitialized = false;
    if (!maskTableInitialized) {
        for (int mask = 0; mask < 256; mask++) {
            for (int i = 0; i < 8; i++) {
                maskTable[mask][i] = (mask & (1 << i)) ? 0xFFFFFFFF : 0;
            }
        }
        maskTableInitialized = true;
    }

    // Process the image in 8x8 blocks (aligned with the mask for cache efficiency)
    // 128x128 image = 16 rows of blocks × 16 columns of blocks = 256 blocks total
    for (int blockRow = 0; blockRow < 16; blockRow++) {
        for (int blockCol = 0; blockCol < 16; blockCol++) {
            int maskIdx = blockRow * 16 + blockCol;
            uint64_t mask = bitmask[maskIdx];

            // Skip entire 8x8 block if all pixels are background
            if (mask == 0) continue;

            // Calculate pixel coordinates for this block
            int startY = blockRow * 8;
            int startX = blockCol * 8;

            // Extract 8-bit row masks from 64-bit block mask
            // Row 0 uses bits 0-7, Row 1 uses bits 8-15, etc.
            alignas(32) uint8_t rowMasks[8];
            for (int i = 0; i < 8; i++) {
                rowMasks[i] = static_cast<uint8_t>((mask >> (i * 8)) & 0xFF);
            }

            // Process each row in the 8x8 block
            for (int dy = 0; dy < 8; dy++) {
                uint8_t rowMask = rowMasks[dy];
                if (rowMask == 0) continue;  // Skip empty rows

                int y = startY + dy;
                const uchar* capturedBase = capturedLab.ptr<uchar>(y);
                const uchar* referenceBase = referenceLab.ptr<uchar>(y);

                // Process all 8 pixels in this row using SIMD for each channel
                // Pixel at (x,y) has byte offset = x * 3 + channel
                alignas(32) int capturedVals[8];
                alignas(32) int referenceVals[8];

                // Process L, A, B channels
                for (int channel = 0; channel < 3; channel++) {
                    // Load 8 pixels for this channel
                    // Calculate byte offsets: (startX + i) * 3 + channel
                    int channelOffset = channel;
                    for (int i = 0; i < 8; i++) {
                        int x = startX + i;
                        int offset = x * 3 + channelOffset;
                        capturedVals[i] = capturedBase[offset];
                        referenceVals[i] = referenceBase[offset];
                    }

                    // Load into 256-bit vector registers (8 x 32-bit integers)
                    __m256i captured = _mm256_load_si256((__m256i*)capturedVals);
                    __m256i reference = _mm256_load_si256((__m256i*)referenceVals);

                    // Compute differences (captured - reference)
                    __m256i diff = _mm256_sub_epi32(captured, reference);

                    // Square the differences (d²)
                    __m256i diffSq = _mm256_mullo_epi32(diff, diff);

                    // Apply mask: zero out background pixels
                    // Load mask vector from lookup table and AND with diffSq
                    __m256i maskVec = _mm256_load_si256((__m256i*)maskTable[rowMask]);
                    __m256i maskedDiffSq = _mm256_and_si256(diffSq, maskVec);

                    // Accumulate to total
                    totalDistSq = _mm256_add_epi32(totalDistSq, maskedDiffSq);
                }

                // Count active pixels using builtin popcount
                pixelCount += _popcnt32(rowMask);
            }
        }
    }

    // Reduce 8 32-bit values to single sum
    alignas(32) int temp[8];
    _mm256_store_si256((__m256i*)temp, totalDistSq);

    uint64_t totalDistanceSq = 0;
    for (int i = 0; i < 8; i++) {
        totalDistanceSq += temp[i];
    }

    if (pixelCount == 0) return 0;
    if (totalDistanceSq >= 10000ULL * pixelCount) return 0;
    return static_cast<int>(10000ULL - (totalDistanceSq * 10000ULL) / (10000ULL * pixelCount));
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

// Helper: Convert integer similarity score (0-10000) to percentage (0.0-1.0)
// Applies sqrt conversion for better distance-to-similarity mapping
static float ConvertScoreToPercentage(int score) {
    // Apply sqrt for accurate distance-to-similarity conversion
    // score = 10000 - avgDistSq, so avgDistSq = 10000 - score
    // final_score = 10000 - sqrt(avgDistSq) * 100
    float avgDistSq = 10000.0f - static_cast<float>(score);
    avgDistSq = std::max(avgDistSq, 0.0f);
    float sqrtDist = std::sqrt(avgDistSq);
    float finalScore = 10000.0f - sqrtDist * 100.0f;
    finalScore = std::max(finalScore, 0.0f);
    return finalScore / 10000.0f;
}

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

    // Generate diff map
    g_debugBestDiffMap = cv::Mat::zeros(img1Lab.rows, img1Lab.cols, CV_8UC3);

    IterateAllPixels(img1Lab, img2Lab,
        [&](int x, int y, int dL, int da, int db) {
            cv::Vec3b* diffRow = g_debugBestDiffMap.ptr<cv::Vec3b>(y);
            float dist = std::sqrt(static_cast<float>(dL * dL + da * da + db * db));

            if (dist < 10.0f) {
                diffRow[x] = cv::Vec3b(0, 0, 0);
            } else {
                float normalized = std::min((dist - 10.0f) / 90.0f, 1.0f);
                uchar intensity = static_cast<uchar>(normalized * 255.0f);
                if (normalized < 0.5f) {
                    diffRow[x] = cv::Vec3b(0, intensity * 2, intensity * 2);
                } else {
                    diffRow[x] = cv::Vec3b(0, static_cast<uchar>((1.0f - normalized) * 510.0f), 255);
                }
            }
        });
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
        const auto& bitmask = MaskLoader::GetBitmask(type, frame);
        score = CalculateColorSimilarity(processedImageLab, refImageLab, bitmask);
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

    // Convert score to percentage (0.0-1.0)
    if (outBestMatch) {
        *outBestMatch = ConvertScoreToPercentage(score);
    }
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

// Convert user's desired final percentage to internal threshold for comparison
// Derived from: internalThreshold = finalPercentage * (200 - finalPercentage)
int finalThresholdPercent = ShapeParams::matchThreshold.load();  // User's desired final percentage (0-100)
int threshold = finalThresholdPercent * (200 - finalThresholdPercent);
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

// Convert score to percentage (0.0-1.0)
if (outBestMatch) {
    *outBestMatch = ConvertScoreToPercentage(bestScore);
}
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

// Integer comparison: bestScore is 0-10000, using internal threshold that accounts for sqrt scaling
if (bestScore < Config::GEM_MATCH_FINAL_THRESHOLD_INTERNAL) return GemType::UNKNOWN;

return bestType;
}
