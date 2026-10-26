/* Fraction-preserving input boundary for HackRF. GNSS algorithms keep their
 * existing oscillator, code, integration, FFT size and amplitude conventions. */
#ifndef NGENE_WIDE_INPUT_H
#define NGENE_WIDE_INPUT_H
#include <stdint.h>

typedef struct { short i, q; } TWideIq;
typedef char TWideIqLayoutCheck[sizeof(TWideIq)==4 ? 1 : -1];

/* Q8 fixed-point: 256 integer units = 1 Classic amplitude unit. Keep the
 * existing +/-7 input limiter, with 1/256 spacing instead of integer rounding. */
void AdaptIqWide(const unsigned char *raw, TWideIq *out, unsigned pairs);
void CarrierIqWide(const TWideIq *first, unsigned count1,
    const TWideIq *second, unsigned count2, const char *cosine, const char *sine,
    unsigned count, short *real, short *imaginary);
/* Input bounds +/-3584 and count <=16384 guarantee exact signed32 accumulation. */
float CorrelateWide(const short *signal, const char *code, unsigned count);
/* One pass over I/Q for the six early/prompt/late correlations. Output order:
 * early I/Q, prompt I/Q, late I/Q. Arithmetic is identical to CorrelateWide. */
void CorrelateWideEpl(const short *i, const short *q, const char *early,
    const char *prompt, const char *late, unsigned count, float out[6]);
/* Same compression NCO and accumulation groups as SampleCompression(0,0,...).
 * Optional BOC masks remove the subcarrier from both components before sums. */
void CompressWide(const short *real, const short *imaginary, const char *boc,
    unsigned inputs, int32_t *out, unsigned outputs);
/* Pack only at the FFT boundary. Select a common exponent for the whole complex
 * window and compensate it in FFT gain. Reject unrepresentable input
 * instead of wrapping/clipping. Returns 1 on success, 0 on failure. */
int PackWideFft(const int32_t *input, short *output, unsigned count,
    unsigned log2Size, int requestedGain, int *effectiveGain);
/* Packed local-code spectra use [Re,-Im,Im,Re], as ArrayProductComplex.
 * Restore FFT exponent BEFORE the historical /65536 and int16 narrowing. */
int ProductWideFft(const short *signal, const short *code, short *out,
                   unsigned count, unsigned restoreShift);

#endif
