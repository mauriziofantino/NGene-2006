/* Standalone double-precision matched filter: no receiver mixer, FFT, ring or
 * tracking code. Read the project's documented +/-1 GPS chip records only.
 * Inspect raw-signal code-phase continuity independently of receiver state. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define FFT_N 32768u
#define WINDOW 8184u
#define FS 8183800.0
#define PI2 6.283185307179586476925286766559
typedef struct {
    double r, i;
} Complex;

static void Transform(Complex *a, int inverse)
{
    unsigned i, j = 0, width, k, base;
    for (i = 1; i < FFT_N; i++) {
        unsigned bit = FFT_N >> 1;
        while (j & bit) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if (i < j) {
            Complex t = a[i];
            a[i] = a[j];
            a[j] = t;
        }
    }
    for (width = 2; width <= FFT_N; width *= 2) {
        double angle = (inverse ? 1 : -1) * PI2 / width, wr = cos(angle), wi = sin(angle);
        for (base = 0; base < FFT_N; base += width) {
            double cr = 1, ci = 0;
            for (k = 0; k < width / 2; k++) {
                unsigned x = base + k, y = x + width / 2;
                double r = a[y].r * cr - a[y].i * ci, q = a[y].r * ci + a[y].i * cr;
                double nr = cr * wr - ci * wi;
                a[y].r = a[x].r - r;
                a[y].i = a[x].i - q;
                a[x].r += r;
                a[x].i += q;
                ci = cr * wi + ci * wr;
                cr = nr;
            }
        }
    }
    if (inverse)
        for (i = 0; i < FFT_N; i++) {
            a[i].r /= FFT_N;
            a[i].i /= FFT_N;
        }
}

static unsigned Peak(Complex *signal, const Complex *code, double *strength)
{
    unsigned k, best = 0;
    double maximum = -1, sum = 0;
    Transform(signal, 0);
    for (k = 0; k < FFT_N; k++) {
        double r = signal[k].r * code[k].r + signal[k].i * code[k].i;
        signal[k].i = signal[k].r * code[k].i - signal[k].i * code[k].r;
        signal[k].r = r;
    }
    Transform(signal, 1);
    for (k = 0; k < WINDOW; k++) {
        double power = signal[k].r * signal[k].r + signal[k].i * signal[k].i;
        sum += power;
        if (power > maximum) {
            maximum = power;
            best = k;
        }
    }
    *strength = maximum / (sum / WINDOW);
    return best;
}

int main(int argc, char **argv)
{
    FILE *input, *codes;
    int prn;
    double frequency, start, end, t, strength, step = .1;
    signed char chips[1023], raw[2 * WINDOW];
    unsigned k, index, testPeak;
    Complex *code = calloc(FFT_N, sizeof(*code)), *signal = calloc(FFT_N, sizeof(*signal));
    if ((argc != 7 && argc != 8) || !code || !signal) {
        fprintf(stderr,
                "Usage: probe-gps-code-phase CI8 GPS_CODES PRN NCO_HZ START_S END_S [STEP_S]\n");
        return 2;
    }
    prn = atoi(argv[3]);
    frequency = strtod(argv[4], NULL);
    start = strtod(argv[5], NULL);
    end = strtod(argv[6], NULL);
    if (argc == 8)
        step = strtod(argv[7], NULL);
    if (prn < 1 || prn > 32 || start < 0 || end < start || step <= 0)
        return 2;
    input = fopen(argv[1], "rb");
    codes = fopen(argv[2], "rb");
    if (!input || !codes)
        return 1;
    if (fseek(codes, (prn - 1) * 2046L, SEEK_SET) || fread(chips, 1, 1023, codes) != 1023)
        return 1;
    fclose(codes);
    for (k = 0; k < 1023; k++)
        if (chips[k] != 1 && chips[k] != -1)
            return 1;
    for (k = 0; k < 2 * WINDOW; k++)
        code[k].r = chips[(unsigned)floor(k * 1023000.0 / FS) % 1023];
    /* A known delayed template must be recovered at the exact independent lag. */
    for (k = 0; k < WINDOW; k++)
        signal[k].r = code[k + 1234].r;
    Transform(code, 0);
    testPeak = Peak(signal, code, &strength);
    if (testPeak != 1234) {
        fprintf(stderr, "Matched-filter self-check failed: %u\n", testPeak);
        return 1;
    }
    printf("time_s,prn,first_sample,template_lag_samples,peak_to_mean_power\n");
    for (index = 0;; index++) {
        uint64_t sample;
        double mi = 0, mq = 0;
        t = start + index * step;
        if (t > end + 1e-8)
            break;
        sample = (uint64_t)llround(t * FS);
#ifdef _WIN32
        if (_fseeki64(input, (int64_t)(sample * 2), SEEK_SET))
            return 1;
#else
        if (fseeko(input, (off_t)(sample * 2), SEEK_SET))
            return 1;
#endif
        if (fread(raw, sizeof(raw), 1, input) != 1)
            return 1;
        memset(signal, 0, FFT_N * sizeof(*signal));
        for (k = 0; k < WINDOW; k++) {
            mi += raw[2 * k];
            mq += raw[2 * k + 1];
        }
        mi /= WINDOW;
        mq /= WINDOW;
        for (k = 0; k < WINDOW; k++) {
            double phase = PI2 * frequency * k / FS, c = cos(phase), s = sin(phase),
                   r = raw[2 * k] - mi, q = raw[2 * k + 1] - mq;
            signal[k].r = r * c - q * s;
            signal[k].i = r * s + q * c;
        }
        testPeak = Peak(signal, code, &strength);
        printf("%.3f,%d,%llu,%u,%.3f\n", t, prn, (unsigned long long)sample, testPeak, strength);
    }
    fclose(input);
    free(code);
    free(signal);
    return 0;
}
