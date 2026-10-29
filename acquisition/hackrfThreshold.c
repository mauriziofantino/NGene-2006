#include "hackrfThreshold.h"
#include "acquisition.h"
int MeasureHackrfConfirmationNoise(TNGeneConfiguration *cfg, TNGeneData *rx, int64_t out[4])
{
    TThresholdManager *thr = &rx->acqCommon.thr;
    unsigned offset = 0, ms, gps = thr->blocksize / 4;
    double phase = 0;
    out[0] = out[1] = out[2] = out[3] = 0;
    if (!gps || rx->buf.fill1 + rx->buf.fill2 < thr->blocksize)
        return 0;
    for (ms = 0; ms < 4; ms++) {
        unsigned pos = (rx->buf.tail + offset) % rx->buf.dim;
        unsigned n = ms == 3 ? thr->blocksize - offset : gps;
        unsigned first = rx->buf.dim - pos;
        int32_t i, q;
        if (first > n)
            first = n;
        phase =
            MixHackrfInput(&rx->hackrfMixer, (THackrfIq8 *)rx->buf.buffer + pos, first,
                           (THackrfIq8 *)rx->buf.buffer, n - first, n,
                           cfg->fif + THRESHOLD_FAKE_DOPPLER, phase, cfg->fs, rx->wideI, rx->wideQ);
        i = CorrelateWideInteger(rx->wideI, thr->code + offset, n);
        q = CorrelateWideInteger(rx->wideQ, thr->code + offset, n);
        if (!ms) {
            out[0] = i;
            out[1] = q;
        }
        out[2] += i;
        out[3] += q;
        offset += n;
    }
    return 1;
}
