/* HackRF input is independent of SiGe's sign-only IF/CI8 processing. */
#ifndef NGENE_HACKRF_INPUT_H
#define NGENE_HACKRF_INPUT_H
#include <stdint.h>
typedef struct {
    int8_t i, q;
} THackrfIq8;
typedef char THackrfIq8Layout[sizeof(THackrfIq8) == 2 ? 1 : -1];
typedef struct {
    uint32_t *coefficients;
} THackrfMixer;
/* Q15 unit-circle coefficients. No stream gain, AGC or amplitude limiter. */

int InitHackrfMixer(THackrfMixer *mixer);

void DeinitHackrfMixer(THackrfMixer *mixer);
/* Original CI8 -> centred, rotated int16 Q6. DC is estimated over the exact
 * requested window, independently of USB chunks and physical ring splits.
 * Per-sample processing uses integer SSE2; double is used once per NCO block.
 * Outputs are bounded by +/-23100; <=16384 samples fit int32 dot products. */

double MixHackrfInput(const THackrfMixer *mixer, const THackrfIq8 *first, unsigned count1,
                      const THackrfIq8 *second, unsigned count2, unsigned count, double frequency,
                      double phase, double fs, short *real, short *imaginary);
#define HACKRF_MIXER_SCALE 64
#endif
