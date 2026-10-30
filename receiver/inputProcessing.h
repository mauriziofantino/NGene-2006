#ifndef NGENE_INPUT_PROCESSING_H
#define NGENE_INPUT_PROCESSING_H
#include <math.h>
#include "ngeneDataStructure.h"
#include "arrays.h"
/* Shared numeric boundary, selected once by the frontend. Oscillator/code
 * generation and all receiver decisions remain in their original modules. */

static inline double GenerateAndMixReceiverInput(TNGeneConfiguration *cfg, TNGeneData *rx,
                                                 char *first, unsigned n1, char *second,
                                                 unsigned n2, unsigned n, double frequency,
                                                 double phase)
{
    if (rx->wideI)
        return MixHackrfInput(&rx->hackrfMixer, (const THackrfIq8 *)first, n1,
                              (const THackrfIq8 *)second, n2, n, frequency, phase, cfg->fs,
                              rx->wideI, rx->wideQ);
    phase = GenerateCarrier(cfg, &rx->fifTable, frequency, phase, n, rx->sinArray, rx->cosArray);
    CarrierInputWipeOff(cfg->nativeIq, first, n1, second, n2, rx->cosArray, rx->sinArray, n,
                        rx->cosArray, rx->sinArray);
    return phase;
}
/* Keep HackRF integration in Q6 integers until the complete complex power. */

static inline int64_t ReceiverPrompt(const short *wide, char *legacy, char *code, unsigned n)
{
    return wide ? CorrelateWideInteger(wide, code, n) : ArrayMac(legacy, code, n) + (int)(n / 2);
}

static inline uint64_t ReceiverPower(int wide, int64_t i, int64_t q)
{
    uint64_t power = (uint64_t)(i * i) + (uint64_t)(q * q);
    return wide ? power / (HACKRF_MIXER_SCALE * HACKRF_MIXER_SCALE) : power;
}
#endif
