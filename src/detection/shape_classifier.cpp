#include "shape_classifier.h"
#include "loader.h"
#include "config.h"
#include <cmath>
#include <opencv2/opencv.hpp>

#if defined(_MSC_VER) && defined(ENABLE_AVX2)
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

// Helper: Iterate over non-background pixels in crop-bounded region
template<typename Callback>
static void IterateNonBlackPixels(const cv::Mat& img1Lab, const cv::Mat& img2Lab,
                                   const MaskLoader::CropBounds& bounds, Callback cb) {
    // Extract bounds once (avoids repeated struct field access)
    const int rowStart = bounds.minRow;
    const int rowEnd = bounds.maxRow;
    const int colStart = bounds.minCol;
    const int colEnd = bounds.maxCol;

    // Background color constants
    const uchar bgL = 0;
    const uchar bgA = 128;
    const uchar bgB = 128;

    // Process one row at a time for better cache locality
    for (int y = rowStart; y <= rowEnd; y++) {
        const cv::Vec3b* row1 = img1Lab.ptr<cv::Vec3b>(y);
        const cv::Vec3b* row2 = img2Lab.ptr<cv::Vec3b>(y);

        for (int x = colStart; x <= colEnd; x++) {
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

// Keep original implementation accessible for comparison
// __declspec(noinline)
static int CalculateColorSimilarity(const cv::Mat&img1Lab,
                                              const cv::Mat&img2Lab,
                                              const MaskLoader::CropBounds& bounds) {
    uint64_t totalDistanceSq = 0;
    int pixelCount = 0;

    IterateNonBlackPixels(img1Lab, img2Lab, bounds,
        [&](int x, int y, int dL, int da, int db) {
            totalDistanceSq += dL * dL + da * da + db * db;
            pixelCount++;
        });

    if (pixelCount == 0) return 0;
    if (totalDistanceSq >= 10000ULL * pixelCount) return 0;
    return static_cast<int>(10000ULL - (totalDistanceSq * 10000ULL) / (10000ULL * pixelCount));
}

#if defined(_MSC_VER) && defined(ENABLE_AVX2)
#pragma message("Compiling with SIMD optimization enabled for gem matching")
// SIMD-optimized version of CalculateColorSimilarity using AVX2 intrinsics
static int CalculateColorSimilaritySIMD(const cv::Mat& capturedLab,
                                        const cv::Mat& referenceLab,
                                        const std::vector<uint64_t>& bitmask,
                                        const MaskLoader::CropBounds& bounds) {
    // Early exit if bounds are empty
    if (bounds.minRow == 0 && bounds.maxRow == 0 &&
        bounds.minCol == 0 && bounds.maxCol == 0) {
        return 0;
    }

    // Initialize SIMD accumulators
    __m256i totalDistSqVec = _mm256_setzero_si256();
    __m256i pixelCountVec = _mm256_setzero_si256();

    // Background color constants
    const uchar bgL = MaskLoader::LAB_BG_L;
    const uchar bgA = MaskLoader::LAB_BG_A;
    const uchar bgB = MaskLoader::LAB_BG_B;

    // Process each pixel within the bounds
    for (int y = bounds.minRow; y <= bounds.maxRow; y++) {
        const cv::Vec3b* capturedRow = capturedLab.ptr<cv::Vec3b>(y);
        const cv::Vec3b* referenceRow = referenceLab.ptr<cv::Vec3b>(y);

        for (int x = bounds.minCol; x <= bounds.maxCol; x++) {
            // Calculate which 8x8 block this pixel belongs to
            int blockRow = y / 8;  // Which row of 8x8 blocks (0-15 for 128x128)
            int blockCol = x / 8;  // Which column of 8x8 blocks (0-15 for 128x128)
            int maskIdx = blockRow * (MaskLoader::FRAME_WIDTH / 8) + blockCol;

            // Calculate position within the 8x8 block
            int dy = y % 8;  // Row within the block (0-7)
            int dx = x % 8;  // Column within the block (0-7)
            int bitPos = dy * 8 + dx;  // Bit position in the mask (0-63)

            // Check if this pixel should be processed (not background)
            if ((bitmask[maskIdx] & (1ULL << bitPos)) == 0) continue;

            // Pixel is foreground, compute difference
            int dL = static_cast<int>(capturedRow[x][0]) - static_cast<int>(referenceRow[x][0]);
            int da = static_cast<int>(capturedRow[x][1]) - static_cast<int>(referenceRow[x][1]);
            int db = static_cast<int>(capturedRow[x][2]) - static_cast<int>(referenceRow[x][2]);

            int distSq = dL * dL + da * da + db * db;

            // Accumulate using scalar for simplicity (SIMD for individual pixels would be overkill)
            __m256i distSqVec = _mm256_set1_epi32(distSq);
            totalDistSqVec = _mm256_add_epi32(totalDistSqVec, distSqVec);

            __m256i oneVec = _mm256_set1_epi32(1);
            pixelCountVec = _mm256_add_epi32(pixelCountVec, oneVec);
        }
    }

    // Horizontal sum of SIMD vectors to scalars
    alignas(32) int totalDistSqArray[8];
    alignas(32) int pixelCountArray[8];
    _mm256_store_si256(reinterpret_cast<__m256i*>(totalDistSqArray), totalDistSqVec);
    _mm256_store_si256(reinterpret_cast<__m256i*>(pixelCountArray), pixelCountVec);

    uint64_t totalDistanceSq = 0;
    int pixelCount = 0;

    for (int i = 0; i < 8; i++) {
        totalDistanceSq += totalDistSqArray[i];
        pixelCount += pixelCountArray[i];
    }

    if (pixelCount == 0) return 0;
    if (totalDistanceSq >= 10000ULL * pixelCount) return 0;
    return static_cast<int>(10000ULL - (totalDistanceSq * 10000ULL) / (10000ULL * pixelCount));
}
#endif

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

// Calculate color similarity score (0-10000) between two LAB images
// __declspec(noinline) preserves call site for debugging/profiling
static int CalculateColorSimilarity(const cv::Mat&img1Lab,
                                  const cv::Mat&img2Lab,
                                  GemType type,
                                  int frame) {
    if (img1Lab.empty() || img2Lab.empty()) return 0;

    // Compare LAB images - Reference already has mask applied (background = (0,128,128))
    // Returns similarity score on integer scale 0-10000 (representing 0.00% to 100.00%)

    const MaskLoader::CropBounds& bounds = MaskLoader::GetCropBounds(type, frame);

    return CalculateColorSimilarity(img1Lab, img2Lab, bounds);
}

// __declspec(noinline)
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

    const MaskLoader::CropBounds& bounds = MaskLoader::GetCropBounds(type, frame);

    IterateNonBlackPixels(img1Lab, img2Lab, bounds,
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
        // Use SIMD path if available and enabled
#if defined(_MSC_VER) && defined(ENABLE_AVX2)
        if (cv::checkHardwareSupport(CV_CPU_AVX2)) {
            const auto& bitmask = MaskLoader::GetBitmask(type, frame);
            const auto& bounds = MaskLoader::GetCropBounds(type, frame);
            // std::cout << "[SIMD] Using AVX2 path for " << GemTypeToString(type) << " frame " << frame << std::endl;
            score = CalculateColorSimilaritySIMD(processedImageLab, refImageLab, bitmask, bounds);
        } else
#endif
        {
            // std::cout << "[SCALAR] Using scalar path for " << GemTypeToString(type) << " frame " << frame << std::endl;
            score = CalculateColorSimilarity(processedImageLab, refImageLab, type, frame);
        }
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
