#include "color_similarity.h"

#ifdef _MSC_VER
#include <immintrin.h>
#endif

#include <cstdint>

namespace {
// Byte-level mask LUT: 8-bit row mask → 8 bytes of 0xFF/0x00
// Only 2KB (vs 8KB for the old __m256i LUT) — fits comfortably in L1
alignas(8) uint64_t s_byteMaskLut[256];
uint8_t s_maskPopcnt[256];
bool s_maskLutInitialized = false;

void InitMaskLut() {
  for (int mask = 0; mask < 256; ++mask) {
    uint64_t byteMask = 0;
    uint8_t count = 0;
    for (int i = 0; i < 8; ++i) {
      if (mask & (1 << i)) {
        byteMask |= (0xFFULL << (i * 8));
        ++count;
      }
    }
    s_byteMaskLut[mask] = byteMask;
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

  const uchar *__restrict capturedL_base = capturedPlanarLab.ptr<uchar>(0);
  const uchar *__restrict capturedA_base = capturedPlanarLab.ptr<uchar>(1);
  const uchar *__restrict capturedB_base = capturedPlanarLab.ptr<uchar>(2);

  const uchar *__restrict referenceL_base = referencePlanarLab.ptr<uchar>(0);
  const uchar *__restrict referenceA_base = referencePlanarLab.ptr<uchar>(1);
  const uchar *__restrict referenceB_base = referencePlanarLab.ptr<uchar>(2);

  // Process in 8x8 blocks. Key SIMD strategy:
  // - Pack 2 rows (16 pixels) into __m128i, widen to epi16 via cvtepu8_epi16
  // - Use _mm256_madd_epi16(diff,diff) for squaring: produces diff[2i]^2 +
  //   diff[2i+1]^2 directly in epi32 — one instruction replaces mullo+add
  // - madd_epi16: 5-cycle latency, 1/cycle throughput
  //   vs mullo_epi32: 10-cycle latency, 0.5/cycle throughput → 4x faster
  // - Masking done at byte level (AND before widening) using 2KB LUT

  for (int blockRow = 0; blockRow < 16; blockRow++) {
    for (int blockCol = 0; blockCol < 16; blockCol++) {
      int maskIdx = blockRow * 16 + blockCol;
      uint64_t mask = bitmask[maskIdx];

      if (mask == 0)
        continue;

      int startY = blockRow * 8;
      int startX = blockCol * 8;

      if (mask == 0xFFFFFFFFFFFFFFFFULL) {
        // Full block: all 64 pixels active, skip masking entirely
        for (int dy = 0; dy < 8; dy += 2) {
          int offset0 = (startY + dy) * imageWidth + startX;
          int offset1 = offset0 + imageWidth;

          // Pack 2 rows of 8 bytes → 16 bytes in __m128i, then widen to epi16
          __m128i capL = _mm_unpacklo_epi64(
              _mm_loadl_epi64((__m128i *)(capturedL_base + offset0)),
              _mm_loadl_epi64((__m128i *)(capturedL_base + offset1)));
          __m128i refL = _mm_unpacklo_epi64(
              _mm_loadl_epi64((__m128i *)(referenceL_base + offset0)),
              _mm_loadl_epi64((__m128i *)(referenceL_base + offset1)));
          __m256i diffL = _mm256_sub_epi16(_mm256_cvtepu8_epi16(capL),
                                           _mm256_cvtepu8_epi16(refL));

          __m128i capA = _mm_unpacklo_epi64(
              _mm_loadl_epi64((__m128i *)(capturedA_base + offset0)),
              _mm_loadl_epi64((__m128i *)(capturedA_base + offset1)));
          __m128i refA = _mm_unpacklo_epi64(
              _mm_loadl_epi64((__m128i *)(referenceA_base + offset0)),
              _mm_loadl_epi64((__m128i *)(referenceA_base + offset1)));
          __m256i diffA = _mm256_sub_epi16(_mm256_cvtepu8_epi16(capA),
                                           _mm256_cvtepu8_epi16(refA));

          __m128i capB = _mm_unpacklo_epi64(
              _mm_loadl_epi64((__m128i *)(capturedB_base + offset0)),
              _mm_loadl_epi64((__m128i *)(capturedB_base + offset1)));
          __m128i refB = _mm_unpacklo_epi64(
              _mm_loadl_epi64((__m128i *)(referenceB_base + offset0)),
              _mm_loadl_epi64((__m128i *)(referenceB_base + offset1)));
          __m256i diffB = _mm256_sub_epi16(_mm256_cvtepu8_epi16(capB),
                                           _mm256_cvtepu8_epi16(refB));

          // madd_epi16: diff[2i]^2 + diff[2i+1]^2 → epi32, 8 lanes
          totalDistSq = _mm256_add_epi32(
              totalDistSq,
              _mm256_add_epi32(
                  _mm256_add_epi32(_mm256_madd_epi16(diffL, diffL),
                                   _mm256_madd_epi16(diffA, diffA)),
                  _mm256_madd_epi16(diffB, diffB)));
        }
        pixelCount += 64;
        continue;
      }

      // Partial block: byte-level masking before widening
      for (int dy = 0; dy < 8; dy += 2) {
        uint8_t rowMask0 = static_cast<uint8_t>((mask >> (dy * 8)) & 0xFF);
        uint8_t rowMask1 =
            static_cast<uint8_t>((mask >> ((dy + 1) * 8)) & 0xFF);

        if ((rowMask0 | rowMask1) == 0)
          continue;

        int offset0 = (startY + dy) * imageWidth + startX;
        int offset1 = offset0 + imageWidth;

        // Build 16-byte mask from two 8-bit row masks via LUT
        __m128i byteMask = _mm_unpacklo_epi64(
            _mm_loadl_epi64((__m128i *)&s_byteMaskLut[rowMask0]),
            _mm_loadl_epi64((__m128i *)&s_byteMaskLut[rowMask1]));

        // Load 2 rows, combine, mask at byte level → zeroed bg pixels
        // Then widen to i16 for madd_epi16 squaring
        __m128i capL = _mm_and_si128(
            _mm_unpacklo_epi64(
                _mm_loadl_epi64((__m128i *)(capturedL_base + offset0)),
                _mm_loadl_epi64((__m128i *)(capturedL_base + offset1))),
            byteMask);
        __m128i refL = _mm_and_si128(
            _mm_unpacklo_epi64(
                _mm_loadl_epi64((__m128i *)(referenceL_base + offset0)),
                _mm_loadl_epi64((__m128i *)(referenceL_base + offset1))),
            byteMask);
        __m256i diffL = _mm256_sub_epi16(_mm256_cvtepu8_epi16(capL),
                                         _mm256_cvtepu8_epi16(refL));

        __m128i capA = _mm_and_si128(
            _mm_unpacklo_epi64(
                _mm_loadl_epi64((__m128i *)(capturedA_base + offset0)),
                _mm_loadl_epi64((__m128i *)(capturedA_base + offset1))),
            byteMask);
        __m128i refA = _mm_and_si128(
            _mm_unpacklo_epi64(
                _mm_loadl_epi64((__m128i *)(referenceA_base + offset0)),
                _mm_loadl_epi64((__m128i *)(referenceA_base + offset1))),
            byteMask);
        __m256i diffA = _mm256_sub_epi16(_mm256_cvtepu8_epi16(capA),
                                         _mm256_cvtepu8_epi16(refA));

        __m128i capB = _mm_and_si128(
            _mm_unpacklo_epi64(
                _mm_loadl_epi64((__m128i *)(capturedB_base + offset0)),
                _mm_loadl_epi64((__m128i *)(capturedB_base + offset1))),
            byteMask);
        __m128i refB = _mm_and_si128(
            _mm_unpacklo_epi64(
                _mm_loadl_epi64((__m128i *)(referenceB_base + offset0)),
                _mm_loadl_epi64((__m128i *)(referenceB_base + offset1))),
            byteMask);
        __m256i diffB = _mm256_sub_epi16(_mm256_cvtepu8_epi16(capB),
                                         _mm256_cvtepu8_epi16(refB));

        totalDistSq = _mm256_add_epi32(
            totalDistSq,
            _mm256_add_epi32(_mm256_add_epi32(_mm256_madd_epi16(diffL, diffL),
                                              _mm256_madd_epi16(diffA, diffA)),
                             _mm256_madd_epi16(diffB, diffB)));

        pixelCount += s_maskPopcnt[rowMask0] + s_maskPopcnt[rowMask1];
      }
    }
  }

  // Horizontal reduction
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
