#include <math.h>
#include <float.h>
#include <limits.h>
#include <stdlib.h>
#include "hackrfGain.h"

void HackrfAdcAccumulate(THackrfAdcHistogram *h, const unsigned char *iq, unsigned bytes)
{
    unsigned k;
    for (k = 0; k + 1 < bytes; k += 2) {
        h->bins[0][iq[k]]++;
        h->bins[1][iq[k + 1]]++;
    }
    h->pairs += bytes / 2;
}

int HackrfAdcMeasure(const THackrfAdcHistogram *h, THackrfAdcStats *s)
{
    unsigned c, b;
    if (h->pairs < 2)
        return 0;
    for (c = 0; c < 2; c++) {
        double sum = 0, m2 = 0, m3 = 0, m4 = 0;
        for (b = 0; b < 256; b++)
            sum += (b < 128 ? (double)b : (double)b - 256) * h->bins[c][b];
        s->mean[c] = sum / (double)h->pairs;
        for (b = 0; b < 256; b++) {
            double x = (b < 128 ? (double)b : (double)b - 256) - s->mean[c];
            double p = (double)h->bins[c][b] / h->pairs, x2 = x * x;
            m2 += p * x2;
            m3 += p * x2 * x;
            m4 += p * x2 * x2;
        }
        s->sigma[c] = sqrt(m2);
        s->skew[c] = m2 > 0 ? m3 / (m2 * sqrt(m2)) : 0;
        s->kurtosis[c] = m2 > 0 ? m4 / (m2 * m2) : 0;
        s->railFraction[c] = (double)(h->bins[c][127] + h->bins[c][128]) / h->pairs;
    }
    return 1;
}

int HackrfGainNext(const THackrfAdcStats *s, double target, int *lna, int *vga)
{
    double desired = DBL_MAX;
    int c, delta, wanted, bestLna = *lna, bestVga = *vga, bestCost = INT_MAX;
    int clipped = 0, inBand = 1, hasEnergy = 0, l, v;
    if (!isfinite(target) || target < 8 || target > 40)
        return 2;
    for (c = 0; c < 2; c++) {
        double headroom = fmin(127 - s->mean[c], 128 + s->mean[c]);
        double limit = fmax(1, fmin(target, headroom / 4));
        double db;
        if (!isfinite(s->sigma[c]) || !isfinite(s->mean[c]))
            return 2;
        /* Four sigma of Gaussian noise plus raw DC must fit before digitizing.
         * Counting physical ADC rails catches clipping that DC removal cannot undo. */
        if (s->railFraction[c] > .001)
            clipped = 1;
        if (s->sigma[c] > fmin(1.15 * target, headroom / 4))
            inBand = 0;
        if (s->sigma[c] >= .85 * limit)
            hasEnergy = 1;
        db = s->sigma[c] > .25 ? 20 * log10(limit / s->sigma[c]) : 8;
        if (db < desired)
            desired = db; /* protect the more strongly driven component */
    }
    if (inBand && hasEnergy && !clipped)
        return 0;
    if (clipped)
        desired = fmin(desired, -8);
    if (desired > 8)
        desired = 8;
    if (desired < -8)
        desired = -8;
    delta = 2 * (int)lround(desired / 2);
    if (!delta)
        delta = desired < 0 ? -2 : 2;
    wanted = *lna + *vga + delta;
    if (wanted < 0)
        wanted = 0;
    if (wanted > 102)
        wanted = 102;
    /* Prefer fine VGA changes. Move the 8 dB IF stage when VGA runs out of
     * range, compensating with VGA to retain a 2 dB total step. */
    for (l = 0; l <= 40; l += 8)
        for (v = 0; v <= 62; v += 2) {
            int cost = 1000 * abs(l + v - wanted) + abs(l - *lna) * 4 + abs(v - *vga);
            if (cost < bestCost) {
                bestCost = cost;
                bestLna = l;
                bestVga = v;
            }
        }
    if (bestLna == *lna && bestVga == *vga)
        return 2;
    *lna = bestLna;
    *vga = bestVga;
    return 1;
}
