/* HackRF integer correlations and FFT boundary; independent of SiGe coding. */
#ifndef NGENE_WIDE_INPUT_H
#define NGENE_WIDE_INPUT_H
#include <stdint.h>
#include "hackrfInput.h"
/* Signed32 dot products; convert to ADC amplitude units only after summing. */

int32_t CorrelateWideInteger(const short *signal, const char *code, unsigned count);

float CorrelateWide(const short *signal, const char *code, unsigned count);
/* One pass over I/Q for the six early/prompt/late correlations. Output order:
 * early I/Q, prompt I/Q, late I/Q. Arithmetic is identical to CorrelateWide. */

void CorrelateWideEpl(const short *i, const short *q, const char *early, const char *prompt,
                      const char *late, unsigned count, float out[6]);
/* Same compression NCO and accumulation groups as SampleCompression(0,0,...).
 * Optional BOC masks remove the subcarrier from both components before sums. */

void CompressWide(const short *real, const short *imaginary, const char *boc, unsigned inputs,
                  int32_t *out, unsigned outputs);
/* Pack only at the FFT boundary. Select a common exponent for the whole complex
 * window and compensate it in FFT gain. Reject unrepresentable input
 * instead of wrapping/clipping. Returns 1 on success, 0 on failure. */

int PackWideFft(const int32_t *input, short *output, unsigned count, unsigned log2Size,
                int requestedGain, int *effectiveGain);
/* Packed local-code spectra use [Re,-Im,Im,Re], as ArrayProductComplex.
 * Restore FFT exponent BEFORE the historical /65536 and int16 narrowing. */

int ProductWideFft(const short *signal, const short *code, short *out, unsigned count,
                   unsigned restoreShift);
/* Restore the two transform exponents in uint64 powers, after all int16
 * transforms. Noncoherent scaling keeps even endpoint powers within uint64. */

int AccumulateWideFftPower(const short *input, uint64_t *sum, unsigned count, unsigned restoreShift,
                           unsigned nonCoherentShift, int reset);
#endif
