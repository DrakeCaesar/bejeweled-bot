#include "color_similarity.h"

#ifdef _MSC_VER
#include <immintrin.h>
#endif

#include <cstdint>

namespace {
alignas(32) __m256i s_maskLut[256];
uint8_t s_maskPopcnt[256];
bool s_maskLutInitialized = false;

void InitMaskLut() {
  for (int mask = 0; mask < 256; ++mask) {
    alignas(32) int32_t vals[8];
    uint8_t count = 0;
    for (int i = 0; i < 8; ++i) {
      if (mask & (1 << i)) {
        vals[i] = -1;
        ++count;
      } else {
        vals[i] = 0;
      }
    }
    s_maskLut[mask] = _mm256_load_si256((__m256i *)vals);
    s_maskPopcnt[mask] = count;
  }
}
} // namespace

void InitializeColorSimilarityLut() {
  if (!s_maskLutInitialized) {
    InitMaskLut();
    s_maskLutInitialized = true;
  }
}

int CalculateColorSimilarity(const cv::Mat &capturedPlanarLab,
                             const cv::Mat &referencePlanarLab,
                             const std::vector<uint64_t> &bitmask,
                             int imageWidth, int imageHeight) {
  __m256i totalDistSq = _mm256_setzero_si256();
  int pixelCount = 0;

  // OPTIMIZATION: Hoist planar base pointers outside all loops to eliminate
  // repeated ptr() calls (6 per active row => 12,000+ calls eliminated!)
  const uchar *__restrict capturedL_base = capturedPlanarLab.ptr<uchar>(0);
  const uchar *__restrict capturedA_base = capturedPlanarLab.ptr<uchar>(1);
  const uchar *__restrict capturedB_base = capturedPlanarLab.ptr<uchar>(2);

  const uchar *__restrict referenceL_base = referencePlanarLab.ptr<uchar>(0);
  const uchar *__restrict referenceA_base = referencePlanarLab.ptr<uchar>(1);
  const uchar *__restrict referenceB_base = referencePlanarLab.ptr<uchar>(2);

  // Process the image in 8x8 blocks (aligned with the mask for cache
  // efficiency) 128x128 image = 16 rows of blocks x 16 columns of blocks = 256
  // blocks total
  for (int blockRow = 0; blockRow < 16; blockRow++) {
    for (int blockCol = 0; blockCol < 16; blockCol++) {
      int maskIdx = blockRow * 16 + blockCol;
      uint64_t mask = bitmask[maskIdx];

      // Skip entire 8x8 block if all pixels are background
      if (mask == 0)
        continue;

      // Calculate pixel coordinates for this block
      int startY = blockRow * 8;
      int startX = blockCol * 8;

      // Process each row in the 8x8 block
      // OPTIMIZATION: Extract row masks on-demand instead of pre-computing all
      // 8 This eliminates the rowMasks array (32 bytes stack, cache pollution)
      // and only computes masks for rows we actually process
      for (int dy = 0; dy < 8; dy++) {
        // Extract 8-bit row mask from 64-bit block mask on-demand
        // Row 0 uses bits 0-7, Row 1 uses bits 8-15, etc.
        uint8_t rowMask = static_cast<uint8_t>((mask >> (dy * 8)) & 0xFF);
        if (rowMask == 0)
          continue; // Skip empty rows

        int y = startY + dy;
        int rowOffset = y * imageWidth;

        // OPTIMIZATION: Process channels sequentially to reduce register
        // pressure Instead of loading all 3 channels simultaneously (~12 active
        // registers), process one channel at a time (~4 active registers,
        // reused)

        // Initialize accumulator for squared differences
        __m256i diffSqSum = _mm256_setzero_si256();

        // Process L channel
        {
          const uchar *capturedL = capturedL_base + rowOffset + startX;
          const uchar *referenceL = referenceL_base + rowOffset + startX;

          __m128i capturedL_64 = _mm_loadl_epi64((__m128i *)capturedL);
          __m128i referenceL_64 = _mm_loadl_epi64((__m128i *)referenceL);

          __m256i capturedL_32 = _mm256_cvtepu8_epi32(capturedL_64);
          __m256i referenceL_32 = _mm256_cvtepu8_epi32(referenceL_64);

          __m256i diffL = _mm256_sub_epi32(capturedL_32, referenceL_32);
          diffSqSum = _mm256_mullo_epi32(diffL, diffL);
        }

        // Process A channel and accumulate
        {
          const uchar *capturedA = capturedA_base + rowOffset + startX;
          const uchar *referenceA = referenceA_base + rowOffset + startX;

          __m128i capturedA_64 = _mm_loadl_epi64((__m128i *)capturedA);
          __m128i referenceA_64 = _mm_loadl_epi64((__m128i *)referenceA);

          __m256i capturedA_32 = _mm256_cvtepu8_epi32(capturedA_64);
          __m256i referenceA_32 = _mm256_cvtepu8_epi32(referenceA_64);

          __m256i diffA = _mm256_sub_epi32(capturedA_32, referenceA_32);
          diffSqSum =
              _mm256_add_epi32(diffSqSum, _mm256_mullo_epi32(diffA, diffA));
        }

        // Process B channel and accumulate
        {
          const uchar *capturedB = capturedB_base + rowOffset + startX;
          const uchar *referenceB = referenceB_base + rowOffset + startX;

          __m128i capturedB_64 = _mm_loadl_epi64((__m128i *)capturedB);
          __m128i referenceB_64 = _mm_loadl_epi64((__m128i *)referenceB);

          __m256i capturedB_32 = _mm256_cvtepu8_epi32(capturedB_64);
          __m256i referenceB_32 = _mm256_cvtepu8_epi32(referenceB_64);

          __m256i diffB = _mm256_sub_epi32(capturedB_32, referenceB_32);
          diffSqSum =
              _mm256_add_epi32(diffSqSum, _mm256_mullo_epi32(diffB, diffB));
        }

        // Mask vector from LUT (one int32 per pixel, -1 if active, 0 if bg)
        __m256i maskVec = s_maskLut[rowMask];

        // Apply mask: zero out background pixels
        __m256i maskedDiffSq = _mm256_and_si256(diffSqSum, maskVec);

        // Accumulate to total
        totalDistSq = _mm256_add_epi32(totalDistSq, maskedDiffSq);

        // Count active pixels using LUT popcount
        pixelCount += s_maskPopcnt[rowMask];
      }
    }
  }

  // Optimized horizontal reduction using tree-based approach
  // Reduce 8 lanes to 4, then 4 to 2, then 2 to 1
  __m256i sum4 = _mm256_hadd_epi32(totalDistSq, totalDistSq);
  __m256i sum2 = _mm256_hadd_epi32(sum4, sum4);
  __m256i sum1 = _mm256_shuffle_epi32(sum2, _MM_SHUFFLE(2, 0, 2, 0));

  alignas(32) int temp[8];
  _mm256_store_si256((__m256i *)temp, sum1);
  uint64_t totalDistanceSq =
      static_cast<uint64_t>(temp[0]) + static_cast<uint64_t>(temp[4]);

  if (pixelCount == 0)
    return 0;
  if (totalDistanceSq >= 10000ULL * pixelCount)
    return 0;
  return static_cast<int>(10000ULL - (totalDistanceSq * 10000ULL) /
                                         (10000ULL * pixelCount));
}
