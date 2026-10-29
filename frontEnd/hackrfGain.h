#ifndef NGENE_HACKRF_GAIN_H
#define NGENE_HACKRF_GAIN_H
#include <stdint.h>
typedef struct HackrfAdcHistogram {
    uint64_t bins[2][256];
    uint64_t pairs;
} THackrfAdcHistogram;
typedef struct HackrfAdcStats {
    double mean[2], sigma[2], railFraction[2], skew[2], kurtosis[2];
} THackrfAdcStats;
/* Calibration only. ADC bytes are inspected, never modified. */
void HackrfAdcAccumulate(THackrfAdcHistogram *h, const unsigned char *iq, unsigned bytes);
int HackrfAdcMeasure(const THackrfAdcHistogram *h, THackrfAdcStats *s);
/* 0: accepted; 1: another setting; 2: hardware limit. */
int HackrfGainNext(const THackrfAdcStats *s, double target, int *lna, int *vga);
#endif
