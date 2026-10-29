/* Native SiGe I/Q input boundary. Correlators consume the existing byte format. */
#include <emmintrin.h>
#include <math.h>
#include "arrays.h"
#include "sige.h"
static __m128i AdaptFour(__m128i values, __m128i dc, __m128i left, __m128i right, __m128i bias)
{
    values = _mm_sll_epi32(_mm_sub_epi32(_mm_slli_epi32(values, 8), dc), left);
    values = _mm_add_epi32(values, _mm_add_epi32(bias, _mm_srai_epi32(values, 31)));
    return _mm_sra_epi32(values, right);
}
void AdaptIqClassic(const unsigned char *raw, char *iq, unsigned int pairs,
                    TNativeIqScaling *scaling)
{
    int64_t sumI = 0, sumQ = 0, sumSquares = 0;
    unsigned int k, bytes = 2 * pairs;
    TNativeIqScaling scale = {0};
    int shift, leftBits, rightBits, rounding;
    double variance, rms;
    __m128i dc, left, right, bias;
    const __m128i zero = _mm_setzero_si128();
    const __m128i lower = _mm_set1_epi16(-7), upper = _mm_set1_epi16(7);
    if (!pairs) {
        if (scaling)
            *scaling = scale;
        return;
    }
    /* Statistics must be complete before stores: raw and iq may alias. */
    for (k = 0; k < pairs; k++) {
        int i = (signed char)raw[2 * k], q = (signed char)raw[2 * k + 1];
        sumI += i;
        sumQ += q;
        sumSquares += i * i + q * q;
    }
    scale.meanIQ8 = (int)lround((double)sumI * 256.0 / pairs);
    scale.meanQQ8 = (int)lround((double)sumQ * 256.0 / pairs);
    variance =
        ((double)sumSquares - ((double)sumI * sumI + (double)sumQ * sumQ) / pairs) / (2.0 * pairs);
    scale.rms = rms = sqrt(variance > 0.0 ? variance : 0.0);
    /* Common gain, never an independent I/Q gain. Keep RMS in [1.5,3] where
     * possible; bounds include ADC endpoints and even nearly-constant blocks. */
    shift = 0;
    if (rms > 0.0) {
        while (rms < 1.5 && shift < 8) {
            rms *= 2;
            shift++;
        }
        while (rms > 3.0 && shift > -7) {
            rms *= 0.5;
            shift--;
        }
    }
    scale.gainShift = shift;
    if (scaling)
        *scaling = scale;
    leftBits = shift > 0 ? shift : 0;
    rightBits = 8 + (shift < 0 ? -shift : 0);
    rounding = 1 << (rightBits - 1);
    dc = _mm_set_epi32(scale.meanQQ8, scale.meanIQ8, scale.meanQQ8, scale.meanIQ8);
    left = _mm_cvtsi32_si128(leftBits);
    right = _mm_cvtsi32_si128(rightBits);
    bias = _mm_set1_epi32(rounding);
    for (k = 0; k + 16 <= bytes; k += 16) {
        __m128i input = _mm_loadu_si128((const __m128i *)(raw + k));
        __m128i sign = _mm_cmpgt_epi8(zero, input);
        __m128i lo = _mm_unpacklo_epi8(input, sign), hi = _mm_unpackhi_epi8(input, sign);
        __m128i loSign = _mm_srai_epi16(lo, 15), hiSign = _mm_srai_epi16(hi, 15);
        __m128i a = AdaptFour(_mm_unpacklo_epi16(lo, loSign), dc, left, right, bias);
        __m128i b = AdaptFour(_mm_unpackhi_epi16(lo, loSign), dc, left, right, bias);
        __m128i c = AdaptFour(_mm_unpacklo_epi16(hi, hiSign), dc, left, right, bias);
        __m128i d = AdaptFour(_mm_unpackhi_epi16(hi, hiSign), dc, left, right, bias);
        lo = _mm_min_epi16(upper, _mm_max_epi16(lower, _mm_packs_epi32(a, b)));
        hi = _mm_min_epi16(upper, _mm_max_epi16(lower, _mm_packs_epi32(c, d)));
        _mm_storeu_si128((__m128i *)(iq + k), _mm_packs_epi16(lo, hi));
    }
    for (; k < bytes; k++) {
        int centered = (signed char)raw[k] * 256 - ((k & 1) ? scale.meanQQ8 : scale.meanIQ8);
        int scaled = centered * (1 << leftBits);
        int value = (scaled + rounding - (scaled < 0)) >> rightBits;
        iq[k] = (char)(value < -7 ? -7 : value > 7 ? 7 : value);
    }
}
int SigeDecodeNativeIq(const unsigned char *raw, char *iq, unsigned int pairs)
{
    unsigned int n = 2 * pairs, k = 0;
    const __m128i one = _mm_set1_epi8(1);
    const __m128i marker = _mm_set1_epi8(2);
    const __m128i expected = _mm_set1_epi16(2); /* I has bit 1, Q does not. */
    for (; k + 16 <= n; k += 16) {
        __m128i bytes = _mm_loadu_si128((const __m128i *)(raw + k));
        if (_mm_movemask_epi8(_mm_cmpeq_epi8(_mm_and_si128(bytes, marker), expected)) != 0xffff)
            return 1;
        bytes = _mm_sub_epi8(one, _mm_slli_epi16(_mm_and_si128(bytes, one), 1));
        _mm_storeu_si128((__m128i *)(iq + k), bytes);
    }
    for (; k < n; k++) {
        if ((raw[k] & 2) != ((k & 1) ? 0 : 2))
            return 1;
        iq[k] = (char)(1 - 2 * (raw[k] & 1));
    }
    return 0;
}
static __m128i SignProduct(__m128i value, __m128i mask)
{
    return _mm_sub_epi8(_mm_xor_si128(value, mask), mask);
}
static void RotateIq(char *iq, unsigned int n, unsigned int sampleOffset, char *cosMask,
                     char *sinMask, char *realOut, char *imagOut)
{
    unsigned int k = 0;
    /* ArrayMac sums XOR-coded bytes and its callers add N/2. It therefore
     * interprets a stored byte a as a+1/2. The complex rotation produces integer
     * levels, so encode its half-LSB offset with balanced rounding at
     * alternating samples. Using the low-IF oscillator mask as this offset
     * would inject an unwanted half-amplitude copy of that oscillator.
     * This is numeric packing; the I/Q signal is never remodulated to real IF. */
    const __m128i bias = _mm_set1_epi16((short)((sampleOffset & 1) ? 0x00ff : 0xff00));
    for (; k + 16 <= n; k += 16) {
        __m128i a = _mm_loadu_si128((const __m128i *)(iq + 2 * k));
        __m128i b = _mm_loadu_si128((const __m128i *)(iq + 2 * k + 16));
        __m128i i = _mm_packs_epi16(_mm_srai_epi16(_mm_slli_epi16(a, 8), 8),
                                    _mm_srai_epi16(_mm_slli_epi16(b, 8), 8));
        __m128i q = _mm_packs_epi16(_mm_srai_epi16(a, 8), _mm_srai_epi16(b, 8));
        __m128i c = _mm_loadu_si128((const __m128i *)(cosMask + k));
        __m128i s = _mm_loadu_si128((const __m128i *)(sinMask + k));
        __m128i r = _mm_sub_epi8(SignProduct(i, c), SignProduct(q, s));
        __m128i j = _mm_add_epi8(SignProduct(i, s), SignProduct(q, c));
        /* Coarse acquisition subtracts these masks. Other callers use the
         * XOR MAC convention. Load both oscillators before writing aliases. */
        _mm_storeu_si128((__m128i *)(cosMask + k), bias);
        _mm_storeu_si128((__m128i *)(sinMask + k), bias);
        _mm_storeu_si128((__m128i *)(realOut + k), _mm_add_epi8(r, bias));
        _mm_storeu_si128((__m128i *)(imagOut + k), _mm_add_epi8(j, bias));
    }
    for (; k < n; k++) {
        int i = (signed char)iq[2 * k], q = (signed char)iq[2 * k + 1];
        int cm = (signed char)cosMask[k], sm = (signed char)sinMask[k];
        int c = cm == 0 ? 1 : -1, s = sm == 0 ? 1 : -1;
        int packing = ((sampleOffset + k) & 1) ? -1 : 0;
        cosMask[k] = sinMask[k] = (char)packing;
        realOut[k] = (char)(i * c - q * s + packing);
        imagOut[k] = (char)(i * s + q * c + packing);
    }
}
void CarrierInputWipeOff(int nativeIq, char *input1, unsigned int size1, char *input2,
                         unsigned int size2, char *cosArray, char *sinArray,
                         unsigned int carrierSize, char *cosDemod, char *sinDemod)
{
    unsigned int first;
    if (!nativeIq) {
        CarrierComplexWipeOff(input1, size1, input2, size2, cosArray, sinArray, carrierSize,
                              cosDemod, sinDemod);
        return;
    }
    if (size1 + size2 < carrierSize)
        return; /* Same contract as legacy wipe-off. */
    first = size1 < carrierSize ? size1 : carrierSize;
    RotateIq(input1, first, 0, cosArray, sinArray, cosDemod, sinDemod);
    if (first < carrierSize)
        RotateIq(input2, carrierSize - first, first, cosArray + first, sinArray + first,
                 cosDemod + first, sinDemod + first);
}
