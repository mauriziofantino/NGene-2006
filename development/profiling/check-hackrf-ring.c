/* Independent full-recording byte oracle for the production HackRF input ring.
 * Hardware reads alone are replaced by fread with whole or fragmented reads.
 * No DSP or receiver decisions run here. Compile separately from the receiver. */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../frontEnd/circularBuffer.c"
#ifdef NGENE_CHECK_CHANNEL_TIMELINE
#include "channelDataStructure.h"
#include "channel-timeline-production.inc"
#include "../../asmFolder/complexInput.c"
#endif
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
        if (Next() % 17u == 0)
            return 0; /* Empty reads must not duplicate the preceding samples. */
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

#ifndef NGENE_CHECK_CHANNEL_TIMELINE
int SigeDecodeNativeIq(const unsigned char *a, char *b, unsigned n)
{
    (void)a;
    (void)b;
    (void)n;
    abort();
}
#else
void CarrierComplexWipeOff(char *a, unsigned na, char *b, unsigned nb, char *c, char *s,
                           unsigned n, char *r, char *j)
{
    abort(); /* The input-ring check never calls a carrier mixer. */
}
#endif

int main(int argc, char **argv)
{
    int mode;
    int sige = argc == 3 && strcmp(argv[2], "--sige") == 0;
    if (argc != 2 && !sige) {
        fprintf(stderr, "Usage: check-hackrf-ring INPUT_CI8 [--sige]\n");
        return 2;
    }
#ifndef NGENE_CHECK_CHANNEL_TIMELINE
    if (sige)
        return 2;
#endif
    for (mode = 0; mode < 2; mode++) {
        TNGeneConfiguration cfg = {0};
        TUsbFePars pars = {0};
        TRxFe fe = {0};
        TCircularBuffer ring = {0};
        FILE *oracle = fopen(argv[1], "rb");
        unsigned char *expected;
        uint64_t consumed = 0;
        cfg.fs = 8183800;
        cfg.nativeIq = 1;
        cfg.sigeNativeIq = sige;
        cfg.usedFrontEnd = sige ? NGENE_FE_SIGE : NGENE_FE_HACKRF;
        cfg.fePars = &pars;
        pars.resolution = 8;
        fe.incomingFile = fopen(argv[1], "rb");
        fragmented = mode;
        randomState = 1;
        delivered = 0;
        if (!oracle || !fe.incomingFile || InitCircularBuffer(&cfg, &ring) != SUCCESS_RETURN)
            return 1;
        expected = malloc(2 * ring.dim);
        if (!expected)
            return 1;
        for (;;) {
            unsigned available, take, first;
            FillInputBuffer(&cfg, &ring, &fe);
            available = ring.fill1 + ring.fill2;
            if (ring.head >= ring.dim || ring.tail >= ring.dim ||
                available + ring.free1 + ring.free2 != ring.dim ||
                ring.discardedSamples != consumed)
                return 1;
            if (!available) {
                if (feof(fe.incomingFile))
                    break;
                continue;
            }
            take = 1 + Next() % 16384u;
            if (take > available)
                take = available;
            if (fread(expected, 2, available, oracle) != available)
                return 1;
            if (sige) {
                unsigned k;
                for (k = 0; k < 2 * available; k++) {
                    if ((expected[k] & 2) != ((k & 1) ? 0 : 2))
                        return 1;
                    expected[k] = (unsigned char)(1 - 2 * (expected[k] & 1));
                }
            }
            first = available < ring.fill1 ? available : ring.fill1;
            if (memcmp(expected, ring.buffer + 2 * ring.tail, 2 * first) ||
                memcmp(expected + 2 * first, ring.buffer, 2 * (available - first))) {
                fprintf(stderr, "Byte mismatch after %llu samples, mode %d\n",
                        (unsigned long long)consumed, mode);
                return 1;
            }
#ifdef NGENE_CHECK_CHANNEL_TIMELINE
            {
                TNGeneData rx = {0};
                TChannel channels[8] = {0};
                unsigned offsetsBefore[8];
                unsigned c;
                cfg.numberOfChannels = 8;
                rx.channelArray = channels;
                for (c = 0; c < 8; c++) {
                    char *a, *b;
                    unsigned na, nb, offset;
                    offset = c == 0 ? take : take + Next() % (available - take + 1);
                    channels[c].bufferOffset = offset;
                    offsetsBefore[c] = offset;
                    GetSamplePointersForChannel(&channels[c], &ring, &a, &na, &b, &nb);
                    if (na + nb != available - offset ||
                        (na && memcmp(a, expected + 2 * offset, 2 * na)) ||
                        (nb && memcmp(b, expected + 2 * (offset + na), 2 * nb)) ||
                        (!na && a != NULL) || (!nb && b != NULL))
                        return 1;
                }
                if (GetNumberOfUsedSamples(&cfg, &rx) != take)
                    return 1;
                CleanBufferOffsetForAllChannels(&cfg, &rx, take);
                for (c = 0; c < 8; c++)
                    if (channels[c].bufferOffset + take != offsetsBefore[c])
                        return 1;
            }
#endif
            if (fseek(oracle, -(long)(2 * (available - take)), SEEK_CUR))
                return 1;
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
        free(expected);
        DeinitCircularBuffer(&ring);
    }
    return 0;
}
