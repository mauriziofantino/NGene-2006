/* Native SiGe I/Q input boundary. Correlators consume the existing byte format. */
#include <emmintrin.h>
#include "arrays.h"
#include "sige.h"

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
        if ((raw[k] & 2) != ((k & 1) ? 0 : 2)) return 1;
        iq[k] = (char)(1 - 2 * (raw[k] & 1));
    }
    return 0;
}

static __m128i SignProduct(__m128i value, __m128i mask)
{
    return _mm_sub_epi8(_mm_xor_si128(value, mask), mask);
}

static void RotateIq(char *iq, unsigned int n, unsigned int sampleOffset, char *cosMask, char *sinMask,
                     char *realOut, char *imagOut)
{
    unsigned int k = 0;
    /* ArrayMac sums XOR-coded bytes and its callers add N/2. It therefore
     * interprets a stored byte a as a+1/2. A complex rotation produces integer
     * levels {-2,0,2}, so encode its half-LSB offset with balanced rounding at
     * alternating samples. Using the low-IF oscillator mask as this offset
     * would inject an unwanted half-amplitude copy of that oscillator.
     * This is numeric packing; the I/Q signal is never remodulated to real IF. */
    const __m128i bias = _mm_set1_epi16((short)((sampleOffset & 1) ? 0x00ff : 0xff00));
    for (; k + 16 <= n; k += 16) {
        __m128i a = _mm_loadu_si128((const __m128i *)(iq + 2*k));
        __m128i b = _mm_loadu_si128((const __m128i *)(iq + 2*k + 16));
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
        int i = (signed char)iq[2*k], q = (signed char)iq[2*k+1];
        int cm = (signed char)cosMask[k], sm = (signed char)sinMask[k];
        int c = cm == 0 ? 1 : -1, s = sm == 0 ? 1 : -1;
        int packing = ((sampleOffset+k)&1) ? -1 : 0;
        cosMask[k] = sinMask[k] = (char)packing;
        realOut[k] = (char)(i*c - q*s + packing);
        imagOut[k] = (char)(i*s + q*c + packing);
    }
}

void CarrierInputWipeOff(int nativeIq, char *input1, unsigned int size1,
    char *input2, unsigned int size2, char *cosArray, char *sinArray,
    unsigned int carrierSize, char *cosDemod, char *sinDemod)
{
    unsigned int first;
    if (!nativeIq) {
        CarrierComplexWipeOff(input1, size1, input2, size2, cosArray, sinArray,
                              carrierSize, cosDemod, sinDemod);
        return;
    }
    if (size1 + size2 < carrierSize) return; /* Same contract as legacy wipe-off. */
    first = size1 < carrierSize ? size1 : carrierSize;
    RotateIq(input1, first, 0, cosArray, sinArray, cosDemod, sinDemod);
    if (first < carrierSize)
        RotateIq(input2, carrierSize-first, first, cosArray+first, sinArray+first,
                 cosDemod+first, sinDemod+first);
}
