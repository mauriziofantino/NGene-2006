/* Compare complete versus abandoned FFT scaling trials on the same real CI8
 * window. Both paths retain precisely the same accepted gain and output. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "fft.h"
#include "hackrfInput.h"
#include "wideInput.h"
#include "ngeneConstants.h"

int SwrxLog(int level, const char *format, ...)
{
    (void)level;
    (void)format;
    return 0;
}
typedef int (*Transform)(TNGeneConfiguration *, TFftTable *, short *, short *, unsigned, int,
                         unsigned *);

static double Now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + 1e-9 * t.tv_nsec;
}
static int Evaluate(TFftTable *table, const int32_t *compressed, short *input, short *output,
                    Transform fn)
{
    int requested = -4, effective;
    unsigned clipped;
    for (;; requested--) {
        if (requested < -12 || !PackWideFft(compressed, input, 4096, 12, requested, &effective))
            return -100;
        if (fn(NULL, table, input, output, 12, effective, &clipped) != SUCCESS_RETURN)
            return -100;
        if (!clipped)
            return requested;
    }
}

int main(int argc, char **argv)
{
    THackrfIq8 raw[8184];
    short real[8184], imaginary[8184], input[8192], expected[8192], output[8192];
    int32_t compressed[8192];
    TFftTable table = {0};
    TNGeneConfiguration cfg = {0};
    THackrfMixer mixer = {0};
    unsigned repeat, batch, method;
    int gain[2];
    FILE *file;
    volatile int checksum = 0;
    Transform functions[2] = {PerformFftChecked, PerformFftTrial};
    if (argc != 2 || !(file = fopen(argv[1], "rb")))
        return 2;
    if (fread(raw, sizeof(raw), 1, file) != 1)
        return 1;
    fclose(file);
    cfg.fs = 8183800;
    if (InitFftTable(&cfg, &table) != SUCCESS_RETURN || !InitHackrfMixer(&mixer))
        return 1;
    MixHackrfInput(&mixer, raw, 8184, NULL, 0, 8184, 5000, 0, cfg.fs, real, imaginary);
    CompressWide(real, imaginary, NULL, 8184, compressed, 4096);
    gain[0] = Evaluate(&table, compressed, input, expected, functions[0]);
    gain[1] = Evaluate(&table, compressed, input, output, functions[1]);
    if (gain[0] == -100 || gain[0] != gain[1] || memcmp(expected, output, sizeof(output)))
        return 1;
    printf("Accepted requested gain %d, exact output equality\n", gain[0]);
    for (batch = 0; batch < 6; batch++)
        for (method = 0; method < 2; method++) {
            unsigned which = method ^ (batch & 1);
            double start = Now();
            for (repeat = 0; repeat < 1000; repeat++) {
                if (Evaluate(&table, compressed, input, output, functions[which]) != gain[0])
                    return 1;
                checksum += output[repeat % 8192];
            }
            printf("batch %u,%s,%.3f us per scaling search\n", batch,
                   which ? "abort-on-clip" : "complete", (Now() - start) * 1000.0);
        }
    printf("checksum %d\n", checksum);
    DeinitHackrfMixer(&mixer);
    DeinitFftTable(&table);
    return 0;
}
