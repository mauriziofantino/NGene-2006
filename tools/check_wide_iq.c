#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef NGENE_NATIVE_WINDOWS
#include <windows.h>
#endif
#include "wideInput.h"
#include "fft.h"
#include "sampleCompression.h"
#include "carrierTable.h"
#include "ngeneConstants.h"
#include "inputProcessing.h"
#include "hackrfThreshold.h"
#include "xmmsBackup.h"
NGENE_THREAD_LOCAL char *ngeneXmmsBackup;

int SwrxLog(int severity, const char *format, ...)
{
    (void)severity;
    (void)format;
    return 0;
}

void CloseNGene(TNGeneConfiguration *cfg, int result)
{
    (void)cfg;
    (void)result;
    abort();
}
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "Wide IQ failure line %d: %s\n", __LINE__, #x);                        \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int MixerAndMac(void)
{
    THackrfMixer mixer = {0};
    unsigned char storage[2 * 16400 + 16];
    short r[16400], q[16400];
    char code[16400], early[16400], late[16400];
    unsigned n, k, split, offset;
    CHECK(InitHackrfMixer(&mixer));
    CHECK(ReceiverPower(1, INT64_C(6400000), 0) == UINT64_C(10000000000));
    CHECK(ReceiverPower(1, INT64_C(6400000), INT64_C(-6400000)) == UINT64_C(20000000000));
    for (offset = 0; offset < 16; offset++) {
        THackrfIq8 *input = (THackrfIq8 *)(storage + offset);
        for (k = 0; k < 16400; k++) {
            input[k].i = (int8_t)(k * 19);
            input[k].q = (int8_t)(255 - k * 37);
            code[k] = k % 3 ? -1 : 0;
            early[k] = k & 1 ? -1 : 0;
            late[k] = k & 2 ? -1 : 0;
        }
        for (n = 0; n <= (offset ? 128u : 16384u); n = n < 32 ? n + 1 : n + 137)
            for (split = 0; split <= n; split += n / 7 + 1) {
                int64_t sumI = 0, sumQ = 0, totalI = 0, totalQ = 0;
                int di = 0, dq = 0;
                const double frequency = -2376.25, phase = 1.234;
                const double turn = 2 * M_PI;
                uint32_t start = (uint32_t)(uint64_t)llround(phase * 4294967296.0 / turn);
                uint32_t step = (uint32_t)(int64_t)llround(frequency / 8183800 * 4294967296.0);
                double out, expectedPhase = fmod(phase + turn * n * (frequency / 8183800), turn);
                for (k = 0; k < n; k++) {
                    sumI += input[k].i;
                    sumQ += input[k].q;
                }
                if (n) {
                    di = sumI >= 0 ? (int)((sumI + n / 2) / n) : -(int)((-sumI + n / 2) / n);
                    dq = sumQ >= 0 ? (int)((sumQ + n / 2) / n) : -(int)((-sumQ + n / 2) / n);
                }
                r[n] = q[n] = 12345;
                out = MixHackrfInput(&mixer, input, split, input + split, n - split, n, frequency,
                                     phase, 8183800, r, q);
                CHECK(r[n] == 12345 && q[n] == 12345);
                if (expectedPhase < 0)
                    expectedPhase += turn;
                CHECK(fabs(out - expectedPhase) < 2e-12);
                for (k = 0; k < n; k++) {
                    uint32_t p = start + k * step;
                    uint32_t packed = mixer.coefficients[(p + 32768u) >> 16];
                    int c = (int16_t)packed, s = (int16_t)(packed >> 16);
                    int i = input[k].i - di, j = input[k].q - dq;
                    int ri = i * c - j * s, rq = i * s + j * c;
                    CHECK(r[k] == (ri + 256 - (ri < 0)) >> 9);
                    CHECK(q[k] == (rq + 256 - (rq < 0)) >> 9);
                    CHECK(abs(r[k]) < 23100 && abs(q[k]) < 23100);
                    totalI += (code[k] ? -1 : 1) * r[k];
                    totalQ += (code[k] ? -1 : 1) * q[k];
                }
                CHECK(CorrelateWide(r, code, n) == (float)totalI / HACKRF_MIXER_SCALE);
                CHECK(CorrelateWide(q, code, n) == (float)totalQ / HACKRF_MIXER_SCALE);
                {
                    float epl[6];
                    CorrelateWideEpl(r, q, early, code, late, n, epl);
                    CHECK(epl[0] == CorrelateWide(r, early, n) &&
                          epl[1] == CorrelateWide(q, early, n));
                    CHECK(epl[2] == CorrelateWide(r, code, n) &&
                          epl[3] == CorrelateWide(q, code, n));
                    CHECK(epl[4] == CorrelateWide(r, late, n) &&
                          epl[5] == CorrelateWide(q, late, n));
                }
            }
    }
    /* Constant DC is removed at mixing; no integer ADC level is normalized. */
    for (k = 0; k < 16384; k++) {
        ((THackrfIq8 *)storage)[k].i = 127;
        ((THackrfIq8 *)storage)[k].q = -128;
    }
    MixHackrfInput(&mixer, (THackrfIq8 *)storage, 73, (THackrfIq8 *)storage + 73, 16384 - 73, 16384,
                   0, 0, 8183800, r, q);
    for (k = 0; k < 16384; k++)
        CHECK(r[k] == 0 && q[k] == 0);
    DeinitHackrfMixer(&mixer);
    return 0;
}

static int ConfirmationNoise(void)
{
    TNGeneConfiguration cfg = {0};
    TNGeneData *rx = calloc(1, sizeof(*rx));
    TDspContext *dsp = calloc(1, sizeof(*dsp));
    THackrfIq8 logical[32736], *ring = malloc(40000 * sizeof(*ring));
    char code[32736];
    short i[8184], q[8184];
    int64_t baseline[4], actual[4];
    unsigned k, scale, tail;
    uint32_t random = 19;
    CHECK(rx && dsp && ring && InitHackrfMixer(&rx->hackrfMixer));
    cfg.fs = 8183800;
    cfg.fif = 0;
    rx->dsp = dsp;
    dsp->wideI = i;
    dsp->wideQ = q;
    rx->buf.buffer = (char *)ring;
    rx->buf.dim = 40000;
    rx->acqCommon.thr.code = code;
    rx->acqCommon.thr.blocksize = 32736;
    for (k = 0; k < 32736; k++) {
        random = random * 1664525u + 1013904223u;
        code[k] = random >> 31 ? -1 : 0;
        logical[k].i = (int8_t)(((int)((k / 2 * 37) % 63) - 31) * (k & 1 ? -1 : 1));
        logical[k].q = (int8_t)(((int)((k / 2 * 13) % 63) - 31) * (k & 1 ? -1 : 1));
    }
    for (scale = 0; scale <= 2; scale++)
        for (tail = 0; tail < 2; tail++) {
            rx->buf.tail = tail ? 34567 : 0;
            rx->buf.fill1 = 40000 - rx->buf.tail;
            if (rx->buf.fill1 > 32736)
                rx->buf.fill1 = 32736;
            rx->buf.fill2 = 32736 - rx->buf.fill1;
            for (k = 0; k < 32736; k++) {
                ring[(rx->buf.tail + k) % 40000].i = (int8_t)(logical[k].i * (int)scale);
                ring[(rx->buf.tail + k) % 40000].q = (int8_t)(logical[k].q * (int)scale);
            }
            CHECK(MeasureHackrfConfirmationNoise(&cfg, rx, actual));
            if (!scale)
                for (k = 0; k < 4; k++)
                    CHECK(actual[k] == 0);
            if (scale == 1 && !tail) {
                memcpy(baseline, actual, sizeof(baseline));
                CHECK(baseline[0] != 0 || baseline[1] != 0);
            }
            if (scale == 1)
                CHECK(memcmp(baseline, actual, sizeof(baseline)) == 0);
            if (scale == 2)
                for (k = 0; k < 4; k++)
                    CHECK(llabs(actual[k] - 2 * baseline[k]) <= 2 * 32736);
        }
    rx->buf.fill1 = rx->buf.fill2 = 0;
    CHECK(!MeasureHackrfConfirmationNoise(&cfg, rx, actual));
    DeinitHackrfMixer(&rx->hackrfMixer);
    free(rx->dsp);
    free(rx);
    free(ring);
    return 0;
}

static int CompressionAndFft(void)
{
    char a[9000], b[9000], boc[9000], bc[9000], bs[9000];
    short r[9000], q[9000];
    int32_t compressed[8192];
    short legacy[8192], packed[8192], spectrum[8192];
    unsigned n, m, k, mode;
    int gain;
    TNGeneConfiguration cfg = {0};
    TFftTable fft = {0};
    const unsigned inputs[] = {8183, 8184, 8185};
    for (mode = 0; mode < 2; mode++)
        for (n = 0; n < 3; n++)
            for (m = 1024; m <= 4096; m *= 4) {
                for (k = 0; k < inputs[n]; k++) {
                    a[k] = (char)((int)(k % 15) - 7);
                    b[k] = (char)(7 - (int)((k * 3) % 15));
                    boc[k] = k & 1 ? -1 : 0;
                    r[k] = a[k] * HACKRF_MIXER_SCALE;
                    q[k] = b[k] * HACKRF_MIXER_SCALE;
                    bc[k] = mode && boc[k] ? -a[k] : a[k];
                    bs[k] = mode && boc[k] ? -b[k] : b[k];
                }
                SampleCompression(0, 0, bc, bs, inputs[n], legacy, m);
                CompressWide(r, q, mode ? boc : NULL, inputs[n], compressed, m);
                for (k = 0; k < 2 * m; k++)
                    CHECK(compressed[k] == legacy[k]);
            }
    CHECK(InitFftTable(&cfg, &fft) == SUCCESS_RETURN);
    for (mode = 0; mode < 4; mode++) {
        /* Tiny amplitudes, normal input and peaks beyond int16 all retain the
         * same physical FFT amplitude after exponent compensation. */
        double amplitude = mode == 0 ? 1 : mode == 1 ? 16 : mode == 2 ? 1000 : 100000;
        for (k = 0; k < 4096; k++) {
            double phase = 2 * M_PI * 31 * k / 4096;
            compressed[2 * k] = (int32_t)lround(amplitude * cos(phase));
            compressed[2 * k + 1] = (int32_t)lround(amplitude * sin(phase));
        }
        CHECK(PackWideFft(compressed, packed, 4096, 12, -4, &gain));
        CHECK(gain >= -12 && gain <= 0);
        CHECK(PerformFft(&cfg, &fft, packed, spectrum, 12, gain) == SUCCESS_RETURN);
        if (mode <= 1)
            CHECK(fabs(spectrum[62] - amplitude * 256) < 100);
        for (k = 0; k < 8192; k++)
            CHECK(abs((int)packed[k]) <= 32767);
    }
    CHECK(!PackWideFft(compressed, packed, 4096, 12, 1, &gain));
    compressed[0] = INT32_MAX;
    CHECK(!PackWideFft(compressed, packed, 4096, 12, -4, &gain));
    DeinitFftTable(&fft);
    return 0;
}

static int Doppler(void)
{
    TNGeneConfiguration cfg = {0};
    TSinCosTable old = {0};
    THackrfMixer mixer = {0};
    THackrfIq8 input[8184];
    short r[8184], q[8184];
    char c[8184], s[8184], code[8184];
    const double frequencies[] = {-2000, -5, -0.5, 0, 0.5, 5, 2000};
    unsigned k, f, p;
    cfg.fs = 8183800;
    cfg.ts = 1 / cfg.fs;
    CHECK(InitHackrfMixer(&mixer));
    CHECK(InitCarrierTable(&cfg, &old, 0) == SUCCESS_RETURN);
    for (k = 0; k < 8184; k++)
        code[k] = k & 1 ? -1 : 0;
    /* Reproduce the old zero-IF phase failure: for a purely real baseband
     * signal, sign-only cosine/sine have equal power at EVERY NCO phase. */
    for (p = 0; p < 32; p++) {
        int64_t i = 0, j = 0;
        GenerateCarrier(&cfg, &old, 0, 2 * M_PI * p / 32, 8184, s, c);
        for (k = 0; k < 8184; k++) {
            i += c[k] ? -64 : 64;
            j += s[k] ? -64 : 64;
        }
        CHECK(i * i - j * j == 0);
    }
    for (f = 0; f < sizeof(frequencies) / sizeof(frequencies[0]); f++)
        for (p = 0; p < 32; p++) {
            double phase = 2 * M_PI * p / 32, fd = frequencies[f], pi, pq, metric;
            for (k = 0; k < 8184; k++) {
                double angle = phase + 2 * M_PI * fd * k / cfg.fs;
                int bit = code[k] ? -1 : 1;
                input[k].i = (int8_t)(48 + bit * lround(64 * cos(angle)));
                input[k].q = (int8_t)(-32 + bit * lround(64 * sin(angle)));
            }
            MixHackrfInput(&mixer, input, 73, input + 73, 8111, 8184, -fd, -phase, cfg.fs, r, q);
            pi = CorrelateWide(r, code, 8184);
            pq = CorrelateWide(q, code, 8184);
            metric = (pi * pi - pq * pq) / (pi * pi + pq * pq);
            CHECK(metric > 0.999);
            /* Two rounded ADC components contribute at most sqrt(0.5) counts
             * of radial error; keep that quantization separate from mixer error. */
            CHECK(fabs(pi / 8184 - 64) < 0.75);
            if (fabs(fd) == 2000) {
                double wrongI, wrongQ;
                MixHackrfInput(&mixer, input, 1, input + 1, 8183, 8184, fd, -phase, cfg.fs, r, q);
                wrongI = CorrelateWide(r, code, 8184);
                wrongQ = CorrelateWide(q, code, 8184);
                CHECK(pi * pi + pq * pq > 100 * (wrongI * wrongI + wrongQ * wrongQ));
            }
        }
    puts(
        "Zero/near-zero IF: sign-only NCO PLL metric 0; Q15 unit-circle metric >0.999 for all phases and Doppler signs.");
    DeinitCarrierTable(&old);
    DeinitHackrfMixer(&mixer);
    return 0;
}

static int HeadroomAndProduct(void)
{
    TNGeneConfiguration cfg = {0};
    TFftTable fft = {0};
    int32_t input[8192];
    short packed[8192], spectrum[8192], reference[8192], code[16384];
    unsigned k, n, restore, clipped = 0;
    int requested = -4, effective;
    uint64_t powers[4096];
    CHECK(InitFftTable(&cfg, &fft) == SUCCESS_RETURN);
    for (k = 0; k < 4096; k++) {
        double phase = 2 * M_PI * 31 * k / 4096;
        input[2 * k] = (int32_t)lround(1000 * cos(phase));
        input[2 * k + 1] = (int32_t)lround(1000 * sin(phase));
        code[4 * k] = code[4 * k + 3] = 1024;
        code[4 * k + 1] = code[4 * k + 2] = 0;
    }
    for (;; requested--) {
        CHECK(PackWideFft(input, packed, 4096, 12, requested, &effective));
        CHECK(PerformFftChecked(&cfg, &fft, packed, spectrum, 12, effective, &clipped) ==
              SUCCESS_RETURN);
        CHECK(PerformFft(&cfg, &fft, packed, reference, 12, effective) == SUCCESS_RETURN);
        CHECK(memcmp(spectrum, reference, sizeof(spectrum)) == 0);
        if (requested == -4)
            CHECK(clipped > 0);
        if (!clipped)
            break;
        CHECK(requested > -12);
    }
    CHECK(requested < -4);
    CHECK(ProductWideFft(spectrum, code, spectrum, 4096, -4 - requested));
    CHECK(abs(spectrum[62] - 4000) < 30 && abs(spectrum[63]) < 30);
    /* Exercise the complete acquisition chain at full ADC-related amplitude.
     * Carry the forward exponent through an independently checked inverse FFT;
     * use a constant to separate exponent errors from Classic's retained
     * twiddle/floor quantization (covered by the separate FFT DFT fixture). */
    for (k = 0; k < 4096; k++) {
        input[2 * k] = 1000;
        input[2 * k + 1] = 0;
    }
    requested = -4;
    for (;; requested--) {
        CHECK(PackWideFft(input, packed, 4096, 12, requested, &effective));
        CHECK(PerformFftChecked(&cfg, &fft, packed, spectrum, 12, effective, &clipped) ==
              SUCCESS_RETURN);
        if (!clipped)
            break;
        CHECK(requested > -12);
    }
    CHECK(ProductWideFft(spectrum, code, reference, 4096, 0));
    {
        int inverse = -4;
        for (;; inverse--) {
            CHECK(PerformFftChecked(&cfg, &fft, reference, packed, 12, inverse, &clipped) ==
                  SUCCESS_RETURN);
            if (!clipped)
                break;
            CHECK(inverse > -12);
        }
        CHECK(AccumulateWideFftPower(packed, powers, 4096, -4 - requested - 4 - inverse, 0, 1));
        /* Known tone and flat local-code spectrum: each CAF component has
         * amplitude 1000 * 4096 / (16 * 64 * 16) = 250. */
        for (k = 0; k < 4096; k++)
            CHECK(llabs((int64_t)powers[k] - 62500) < 5000);
    }
    DeinitFftTable(&fft);
    for (n = 1; n <= 128; n++)
        for (restore = 0; restore <= 16; restore++) {
            unsigned pass;
            for (k = 0; k < 2 * n; k++)
                packed[k] = k % 3 ? -32768 : 32767;
            for (pass = 0; pass < 8; pass++) {
                CHECK(AccumulateWideFftPower(packed, powers, n, restore, 3, pass == 0));
                for (k = 0; k < n; k++) {
                    int64_t i = packed[2 * k], q = packed[2 * k + 1];
                    uint64_t expected =
                        (((uint64_t)(i * i + q * q) << (2 * restore)) >> 3) * (pass + 1);
                    CHECK(powers[k] == expected);
                }
            }
        }
    CHECK(!AccumulateWideFftPower(packed, powers, 1, 17, 0, 1));
    /* Scalar int64 oracle, arbitrary tails, symmetric rounding, aliases and
     * PMADDWD's exceptional overflow endpoints. */
    for (restore = 0; restore <= 8; restore++)
        for (n = 1; n <= 128; n++) {
            int expectedOk = 1, ok;
            for (k = 0; k < 2 * n; k++)
                packed[k] = (short)(k * 23117u + n * 7919u);
            for (k = 0; k < 4 * n; k++)
                code[k] = (short)(k * 31991u + n * 11717u);
            packed[0] = packed[1] = code[0] = code[1] = -32768;
            for (k = 0; k < 2 * n; k++) {
                unsigned bin = k / 2, part = k % 2;
                int64_t value = (int64_t)packed[2 * bin] * code[4 * bin + 2 * part] +
                                (int64_t)packed[2 * bin + 1] * code[4 * bin + 2 * part + 1];
                value = (value + (INT64_C(1) << (15 - restore)) - (value < 0)) >> (16 - restore);
                if (value < -32768 || value > 32767)
                    expectedOk = 0;
                reference[k] = (short)(value < -32768 ? -32768 : value > 32767 ? 32767 : value);
            }
            ok = ProductWideFft(packed, code, spectrum, n, restore);
            CHECK(ok == expectedOk && memcmp(spectrum, reference, 2 * n * sizeof(short)) == 0);
            ok = ProductWideFft(packed, code, packed, n, restore);
            CHECK(ok == expectedOk && memcmp(packed, reference, 2 * n * sizeof(short)) == 0);
        }
    return 0;
}

int main(void)
{
    int result;
#ifdef NGENE_NATIVE_WINDOWS
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    CHECK(posix_memalign((void **)&ngeneXmmsBackup, 16, 128) == 0);
    result = MixerAndMac() || ConfirmationNoise() || CompressionAndFft() || Doppler() ||
             HeadroomAndProduct();
    free(ngeneXmmsBackup);
    if (result)
        return 1;
    puts(
        "Raw CI8, Q15 unit-circle mixer, exact SIMD integer MAC, compression, FFT headroom recovery and signed zero-IF Doppler passed.");
    return 0;
}
