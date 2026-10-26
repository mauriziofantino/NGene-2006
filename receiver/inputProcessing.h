#ifndef NGENE_INPUT_PROCESSING_H
#define NGENE_INPUT_PROCESSING_H
#include <math.h>
#include "ngeneDataStructure.h"
#include "arrays.h"

/* Shared numeric boundary, selected once by the frontend. Oscillator/code
 * generation and all receiver decisions remain in their original modules. */
static inline void WipeOffReceiverInput(TNGeneConfiguration *cfg, TNGeneData *rx,
    char *first, unsigned n1, char *second, unsigned n2, unsigned n)
{
    if (rx->wideI)
        CarrierIqWide((const TWideIq *)first, n1, (const TWideIq *)second, n2,
            rx->cosArray, rx->sinArray, n, rx->wideI, rx->wideQ);
    else
        CarrierInputWipeOff(cfg->nativeIq, first, n1, second, n2,
            rx->cosArray, rx->sinArray, n, rx->cosArray, rx->sinArray);
}

/* Refinement/confirmation retain their integer integration accumulators.
 * Round once after the full dot product, rather than each ADC sample. */
static inline int ReceiverPrompt(const short *wide, char *legacy, char *code, unsigned n)
{
    return wide ? (int)lroundf(CorrelateWide(wide, code, n)) : ArrayMac(legacy,code,n)+(int)(n/2);
}
#endif
