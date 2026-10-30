/* Independent full-recording byte oracle for the production HackRF input ring.
 * Hardware reads alone are replaced by fread with whole or fragmented reads.
 * No DSP or receiver decisions run here. Compile separately from the receiver. */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../frontEnd/circularBuffer.c"
static int fragmented;
static unsigned randomState;
static uint64_t delivered;

static unsigned Next(void)
{
    randomState = randomState * 1664525u + 1013904223u;
    return randomState;
}

int SwrxLog(int severity, const char *format, ...)
{
    va_list args;
    (void)severity;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
    return 0;
}

void CloseNGene(TNGeneConfiguration *cfg, int result)
{
    (void)cfg;
    exit(result);
}

unsigned int ReadRxFrontEnd(TRxFe *fe, char *out, int capacity)
{
    unsigned n = (unsigned)capacity;
    if (fragmented) {
        unsigned limit = 1 + Next() % 65537u;
        if (n > limit)
            n = limit;
    }
    n = (unsigned)fread(out, 1, n, fe->incomingFile);
    delivered += n;
    return n;
}
/* These adapters must never be invoked by the HackRF branch under test. */

void SigeSamplesElab(char *a, char *b, int n, int m)
{
    (void)a;
    (void)b;
    (void)n;
    (void)m;
    abort();
}

void SigeV3SamplesElab(char *a, char *b, int n)
{
    (void)a;
    (void)b;
    (void)n;
    abort();
}

int SigeDecodeNativeIq(const unsigned char *a, char *b, unsigned n)
{
    (void)a;
    (void)b;
    (void)n;
    abort();
}

int main(int argc, char **argv)
{
    int mode;
    if (argc != 2) {
        fprintf(stderr, "Usage: check-hackrf-ring INPUT_CI8\n");
        return 2;
    }
    for (mode = 0; mode < 2; mode++) {
        TNGeneConfiguration cfg = {0};
        TUsbFePars pars = {0};
        TRxFe fe = {0};
        TCircularBuffer ring = {0};
        FILE *oracle = fopen(argv[1], "rb");
        unsigned char expected[32768];
        uint64_t consumed = 0;
        cfg.fs = 8183800;
        cfg.nativeIq = 1;
        cfg.usedFrontEnd = NGENE_FE_HACKRF;
        cfg.fePars = &pars;
        pars.resolution = 8;
        fe.incomingFile = fopen(argv[1], "rb");
        fragmented = mode;
        randomState = 1;
        delivered = 0;
        if (!oracle || !fe.incomingFile || InitCircularBuffer(&cfg, &ring) != SUCCESS_RETURN)
            return 1;
        for (;;) {
            unsigned available, take, first;
            FillInputBuffer(&cfg, &ring, &fe);
            available = ring.fill1 + ring.fill2;
            if (!available) {
                if (feof(fe.incomingFile))
                    break;
                continue;
            }
            take = 1 + Next() % 16384u;
            if (take > available)
                take = available;
            if (fread(expected, 2, take, oracle) != take)
                return 1;
            first = take < ring.fill1 ? take : ring.fill1;
            if (memcmp(expected, ring.buffer + 2 * ring.tail, 2 * first) ||
                memcmp(expected + 2 * first, ring.buffer, 2 * (take - first))) {
                fprintf(stderr, "Byte mismatch after %llu samples, mode %d\n",
                        (unsigned long long)consumed, mode);
                return 1;
            }
            if (DeleteFromCircularBuffer(&ring, take) != take)
                return 1;
            consumed += take;
        }
        if (fgetc(oracle) != EOF || 2 * consumed != delivered || ring.sigeIqHasPending)
            return 1;
        printf("mode %d: %llu original bytes preserved through production ring\n", mode,
               (unsigned long long)delivered);
        fclose(oracle);
        fclose(fe.incomingFile);
        DeinitCircularBuffer(&ring);
    }
    return 0;
}
