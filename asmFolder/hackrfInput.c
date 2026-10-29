#include <emmintrin.h>
#include <math.h>
#include <stdlib.h>
#include "hackrfInput.h"
#define TWO_PI 6.283185307179586476925286766559
#define TABLE_SIZE 65536u
int InitHackrfMixer(THackrfMixer *mixer)
{
    unsigned k;
    mixer->coefficients = malloc(TABLE_SIZE * sizeof(*mixer->coefficients));
    if (!mixer->coefficients)
        return 0;
    for (k = 0; k < TABLE_SIZE; k++) {
        double phase = TWO_PI * k / TABLE_SIZE;
        int c = (int)lround(32767 * cos(phase)), s = (int)lround(32767 * sin(phase));
        mixer->coefficients[k] = (uint16_t)c | ((uint32_t)(uint16_t)s << 16);
    }
    return 1;
}
void DeinitHackrfMixer(THackrfMixer *mixer)
{
    free(mixer->coefficients);
    mixer->coefficients = NULL;
}
static void SumSamples(const THackrfIq8 *input, unsigned n, int64_t *si, int64_t *sq)
{
    unsigned k = 0, j;
    const __m128i zero = _mm_setzero_si128(), im = _mm_set1_epi32(1), qm = _mm_set1_epi32(65536);
    __m128i i = zero, q = zero;
    int lanes[4];
    for (; k + 8 <= n; k += 8) {
        __m128i v = _mm_loadu_si128((const __m128i *)(input + k));
        __m128i sign = _mm_cmpgt_epi8(zero, v);
        __m128i a = _mm_unpacklo_epi8(v, sign), b = _mm_unpackhi_epi8(v, sign);
        i = _mm_add_epi32(i, _mm_add_epi32(_mm_madd_epi16(a, im), _mm_madd_epi16(b, im)));
        q = _mm_add_epi32(q, _mm_add_epi32(_mm_madd_epi16(a, qm), _mm_madd_epi16(b, qm)));
    }
    _mm_storeu_si128((__m128i *)lanes, i);
    for (j = 0; j < 4; j++)
        *si += lanes[j];
    _mm_storeu_si128((__m128i *)lanes, q);
    for (j = 0; j < 4; j++)
        *sq += lanes[j];
    for (; k < n; k++) {
        *si += input[k].i;
        *sq += input[k].q;
    }
}
static __m128i RoundQ6(__m128i v)
{
    /* Q15 -> Q6, ties away from zero. The shift changes representation;
     * it is not a sample-dependent amplitude normalization. */
    return _mm_srai_epi32(
        _mm_add_epi32(v, _mm_add_epi32(_mm_set1_epi32(256), _mm_srai_epi32(v, 31))), 9);
}
static __m128i Gather(const uint32_t *table, uint32_t *phase, uint32_t step)
{
    uint32_t a = table[(*phase + 32768u) >> 16];
    *phase += step;
    uint32_t b = table[(*phase + 32768u) >> 16];
    *phase += step;
    uint32_t c = table[(*phase + 32768u) >> 16];
    *phase += step;
    uint32_t d = table[(*phase + 32768u) >> 16];
    *phase += step;
    return _mm_set_epi32((int)d, (int)c, (int)b, (int)a);
}
static void Rotate(const THackrfMixer *mixer, const THackrfIq8 *input, unsigned n, int di, int dq,
                   uint32_t *phase, uint32_t step, short *real, short *imaginary)
{
    unsigned k = 0;
    const __m128i zero = _mm_setzero_si128(), signQ = _mm_set1_epi32((int)0xffff0001u);
    const __m128i dc = _mm_set1_epi32((uint16_t)di | ((uint32_t)(uint16_t)dq << 16));
    for (; k + 8 <= n; k += 8) {
        __m128i v = _mm_loadu_si128((const __m128i *)(input + k));
        __m128i sign = _mm_cmpgt_epi8(zero, v);
        __m128i a = _mm_sub_epi16(_mm_unpacklo_epi8(v, sign), dc);
        __m128i b = _mm_sub_epi16(_mm_unpackhi_epi8(v, sign), dc);
        __m128i lo = Gather(mixer->coefficients, phase, step),
                hi = Gather(mixer->coefficients, phase, step);
        __m128i ri = RoundQ6(_mm_madd_epi16(a, _mm_mullo_epi16(lo, signQ)));
        __m128i rj = RoundQ6(_mm_madd_epi16(b, _mm_mullo_epi16(hi, signQ)));
        __m128i il = _mm_or_si128(_mm_slli_epi32(lo, 16), _mm_srli_epi32(lo, 16));
        __m128i ih = _mm_or_si128(_mm_slli_epi32(hi, 16), _mm_srli_epi32(hi, 16));
        _mm_storeu_si128((__m128i *)(real + k), _mm_packs_epi32(ri, rj));
        _mm_storeu_si128(
            (__m128i *)(imaginary + k),
            _mm_packs_epi32(RoundQ6(_mm_madd_epi16(a, il)), RoundQ6(_mm_madd_epi16(b, ih))));
    }
    for (; k < n; k++) {
        uint32_t v = mixer->coefficients[(*phase + 32768u) >> 16];
        int c = (int16_t)v, s = (int16_t)(v >> 16), i = input[k].i - di, q = input[k].q - dq;
        int r = i * c - q * s, j = i * s + q * c;
        real[k] = (short)((r + 256 - (r < 0)) >> 9);
        imaginary[k] = (short)((j + 256 - (j < 0)) >> 9);
        *phase += step;
    }
}
double MixHackrfInput(const THackrfMixer *mixer, const THackrfIq8 *a, unsigned na,
                      const THackrfIq8 *b, unsigned nb, unsigned n, double f, double phase,
                      double fs, short *real, short *imaginary)
{
    unsigned first = na < n ? na : n;
    int64_t si = 0, sq = 0;
    int di, dq;
    uint32_t accumulator, step;
    double out;
    if (!n)
        return phase;
    if (na + nb < n || !mixer->coefficients || fs <= 0)
        return NAN;
    SumSamples(a, first, &si, &sq);
    SumSamples(b, n - first, &si, &sq);
    di = si >= 0 ? (int)((si + n / 2) / n) : -(int)((-si + n / 2) / n);
    dq = sq >= 0 ? (int)((sq + n / 2) / n) : -(int)((-sq + n / 2) / n);
    phase = fmod(phase, TWO_PI);
    if (phase < 0)
        phase += TWO_PI;
    accumulator = (uint32_t)(uint64_t)llround(phase * (4294967296.0 / TWO_PI));
    step = (uint32_t)(int64_t)llround((f / fs) * 4294967296.0);
    if (first)
        Rotate(mixer, a, first, di, dq, &accumulator, step, real, imaginary);
    if (first < n)
        Rotate(mixer, b, n - first, di, dq, &accumulator, step, real + first, imaginary + first);
    out = fmod(phase + TWO_PI * n * (f / fs), TWO_PI);
    if (out < 0)
        out += TWO_PI;
    return out;
}
