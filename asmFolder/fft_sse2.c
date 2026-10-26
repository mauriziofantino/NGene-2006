/** @file fft_sse2.c
 * @brief EVO SSE2 codelets adapted to Classic's per-stage gain on x86_64.
 * @details Derived from nav-ngene-evo bb61e1b src/dsp/x86/fft_sse2_32.c;
 * x64 uses the same SSE2 intrinsics with the compiler managing platform ABI
 * registers. No CPUID dispatcher, alternative architecture or mutable backup.
 */
#include <emmintrin.h>
#include "fft_kernels.h"

#ifdef _MSC_VER
#define INLINE static __forceinline
#else
#define INLINE static inline __attribute__((always_inline))
#endif

/* Optional per-call accounting; no shared state. Count narrowing operations
 * with actual out-of-range lanes, rather than guessing from output rails. */
INLINE __m128i Pack(__m128i a, __m128i b, unsigned *clipped)
{
    if (clipped) {
        __m128i lo=_mm_set1_epi32(-32768), hi=_mm_set1_epi32(32767);
        __m128i bad=_mm_or_si128(_mm_or_si128(_mm_cmpgt_epi32(lo,a),_mm_cmpgt_epi32(a,hi)),
                                _mm_or_si128(_mm_cmpgt_epi32(lo,b),_mm_cmpgt_epi32(b,hi)));
        if (_mm_movemask_epi8(bad)) (*clipped)++;
    }
    return _mm_packs_epi32(a,b);
}

INLINE __m128i Average(__m128i a, __m128i b)
{
    return _mm_add_epi16(_mm_and_si128(a,b),_mm_srai_epi16(_mm_xor_si128(a,b),1));
}

INLINE __m128i ExtendLow(__m128i value)
{
    return _mm_unpacklo_epi16(value,_mm_srai_epi16(value,15));
}

INLINE __m128i ExtendHigh(__m128i value)
{
    return _mm_unpackhi_epi16(value,_mm_srai_epi16(value,15));
}

/** Preserve EVO's exact first two scaled stages when Q14 headroom holds. */
INLINE __m128i FirstTwoBounded(__m128i value, __m128i rotateMask)
{
    __m128i upper=_mm_shuffle_epi32(value,0x88);
    __m128i lower=_mm_shuffle_epi32(value,0xdd);
    __m128i sum=Average(upper,lower);
    __m128i difference=_mm_sub_epi16(sum,lower);
    value=_mm_unpacklo_epi32(sum,difference);
    lower=_mm_shuffle_epi32(value,0xee);
    lower=_mm_shufflelo_epi16(lower,0xb4);
    lower=_mm_sub_epi16(_mm_xor_si128(lower,rotateMask),rotateMask);
    sum=Average(value,lower);
    difference=_mm_sub_epi16(sum,lower);
    return _mm_unpacklo_epi64(sum,difference);
}

/** Widths 2/4 with independent stage scaling and safe full-int16 arithmetic. */
INLINE __m128i FirstTwoWide(__m128i value, __m128i shift1, __m128i shift2,
                           __m128i twiddle4, unsigned *clipped)
{
    __m128i upper=ExtendLow(_mm_shuffle_epi32(value,0x88));
    __m128i lower=ExtendLow(_mm_shuffle_epi32(value,0xdd));
    __m128i sum=_mm_sra_epi32(_mm_add_epi32(upper,lower),shift1);
    __m128i difference=_mm_sra_epi32(_mm_sub_epi32(upper,lower),shift1);
    value=_mm_shuffle_epi32(Pack(sum,difference,clipped),0xd8);
    upper=ExtendLow(value);
    lower=_mm_srai_epi32(_mm_madd_epi16(_mm_shuffle_epi32(value,0xfa),twiddle4),14);
    sum=_mm_sra_epi32(_mm_add_epi32(upper,lower),shift2);
    difference=_mm_sra_epi32(_mm_sub_epi32(upper,lower),shift2);
    return Pack(sum,difference,clipped);
}

INLINE void WideFour(__m128i upper, __m128i productLow, __m128i productHigh,
                     __m128i shift, __m128i *sum, __m128i *difference, unsigned *clipped)
{
    __m128i low=ExtendLow(upper), high=ExtendHigh(upper);
    __m128i sumLow=_mm_sra_epi32(_mm_add_epi32(low,productLow),shift);
    __m128i sumHigh=_mm_sra_epi32(_mm_add_epi32(high,productHigh),shift);
    __m128i diffLow=_mm_sra_epi32(_mm_sub_epi32(low,productLow),shift);
    __m128i diffHigh=_mm_sra_epi32(_mm_sub_epi32(high,productHigh),shift);
    *sum=Pack(sumLow,sumHigh,clipped);
    *difference=Pack(diffLow,diffHigh,clipped);
}

int FftInputInRangeSse2(const short *input, size_t size)
{
    const __m128i minimum=_mm_set1_epi16(-16384), maximum=_mm_set1_epi16(16384);
    size_t i;
    for (i=0;i<2*size;i+=8) {
        __m128i value=_mm_loadu_si128((const __m128i *)(input+i));
        __m128i bad=_mm_or_si128(_mm_cmpgt_epi16(minimum,value),_mm_cmpgt_epi16(value,maximum));
        if (_mm_movemask_epi8(bad)) return 0;
    }
    return 1;
}

void FftFirstStagesSse2(short *data, size_t size, unsigned scaledStages, int bounded, unsigned *clipped)
{
    const __m128i rotateMask=_mm_setr_epi16(0,0,0,-1,0,0,0,-1);
    const __m128i twiddle4=_mm_setr_epi16(16384,0,0,16384,0,16384,-16384,0);
    const __m128i twiddleLow=_mm_setr_epi16(16384,0,0,16384,11585,11585,-11585,11585);
    const __m128i twiddleHigh=_mm_setr_epi16(0,16384,-16384,0,-11585,11585,-11585,-11585);
    const __m128i shift1=_mm_cvtsi32_si128(scaledStages>0);
    const __m128i shift2=_mm_cvtsi32_si128(scaledStages>1);
    const __m128i shift3=_mm_cvtsi32_si128(scaledStages>2);
    size_t i;
    if (bounded && scaledStages>=3) {
        for (i=0;i<size;i+=8) {
            __m128i upper=FirstTwoBounded(_mm_loadu_si128((const __m128i *)(data+2*i)),rotateMask);
            __m128i lower=FirstTwoBounded(_mm_loadu_si128((const __m128i *)(data+2*i+8)),rotateMask);
            __m128i low=_mm_srai_epi32(_mm_madd_epi16(_mm_shuffle_epi32(lower,0x50),twiddleLow),14);
            __m128i high=_mm_srai_epi32(_mm_madd_epi16(_mm_shuffle_epi32(lower,0xfa),twiddleHigh),14);
            __m128i product=Pack(low,high,clipped);
            __m128i sum=Average(upper,product);
            _mm_storeu_si128((__m128i *)(data+2*i),sum);
            _mm_storeu_si128((__m128i *)(data+2*i+8),_mm_sub_epi16(sum,product));
        }
    } else {
        for (i=0;i<size;i+=8) {
            __m128i upper=FirstTwoWide(_mm_loadu_si128((const __m128i *)(data+2*i)),shift1,shift2,twiddle4,clipped);
            __m128i lower=FirstTwoWide(_mm_loadu_si128((const __m128i *)(data+2*i+8)),shift1,shift2,twiddle4,clipped);
            __m128i low=_mm_srai_epi32(_mm_madd_epi16(_mm_shuffle_epi32(lower,0x50),twiddleLow),14);
            __m128i high=_mm_srai_epi32(_mm_madd_epi16(_mm_shuffle_epi32(lower,0xfa),twiddleHigh),14);
            __m128i sum,difference;
            WideFour(upper,low,high,shift3,&sum,&difference,clipped);
            _mm_storeu_si128((__m128i *)(data+2*i),sum);
            _mm_storeu_si128((__m128i *)(data+2*i+8),difference);
        }
    }
}

void FftButterfliesSse2(short *upper, short *lower, const short *twiddle,
                       size_t count, int scaled, int bounded, unsigned *clipped)
{
    size_t i;
    const __m128i shift=_mm_cvtsi32_si128(scaled!=0);
    if (scaled && bounded) {
        for (i=0;i<count;i+=4) {
            __m128i value=_mm_loadu_si128((const __m128i *)(lower+2*i));
            __m128i low=_mm_srai_epi32(_mm_madd_epi16(_mm_shuffle_epi32(value,0x50),
                _mm_loadu_si128((const __m128i *)(twiddle+4*i))),14);
            __m128i high=_mm_srai_epi32(_mm_madd_epi16(_mm_shuffle_epi32(value,0xfa),
                _mm_loadu_si128((const __m128i *)(twiddle+4*i+8))),14);
            __m128i product=Pack(low,high,clipped);
            __m128i a=_mm_loadu_si128((const __m128i *)(upper+2*i));
            __m128i sum=Average(a,product);
            _mm_storeu_si128((__m128i *)(upper+2*i),sum);
            _mm_storeu_si128((__m128i *)(lower+2*i),_mm_sub_epi16(sum,product));
        }
    } else {
        for (i=0;i<count;i+=4) {
            __m128i value=_mm_loadu_si128((const __m128i *)(lower+2*i));
            __m128i low=_mm_srai_epi32(_mm_madd_epi16(_mm_shuffle_epi32(value,0x50),
                _mm_loadu_si128((const __m128i *)(twiddle+4*i))),14);
            __m128i high=_mm_srai_epi32(_mm_madd_epi16(_mm_shuffle_epi32(value,0xfa),
                _mm_loadu_si128((const __m128i *)(twiddle+4*i+8))),14);
            __m128i sum,difference;
            WideFour(_mm_loadu_si128((const __m128i *)(upper+2*i)),low,high,shift,&sum,&difference,clipped);
            _mm_storeu_si128((__m128i *)(upper+2*i),sum);
            _mm_storeu_si128((__m128i *)(lower+2*i),difference);
        }
    }
}
