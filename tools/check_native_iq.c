#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "carrierTable.h"
#include "arrays.h"
#include "sige.h"
#include "circularBuffer.h"
#include "ngeneConstants.h"

char *ngeneXmmsBackup;
static unsigned char wire[81];
static unsigned int wireIndex, readCalls;

int SwrxLog(int severity, const char *format, ...) { (void)severity; (void)format; return 0; }
void CloseNGene(TNGeneConfiguration *cfg, int result) { (void)cfg; (void)result; abort(); }
unsigned int ReadRxFrontEnd(TRxFe *frontend, char *out, int capacity)
{
    static const unsigned int fragments[] = {1, 1, 3, 7, 2, 13, 1, 19};
    unsigned int n = fragments[readCalls++ % 8];
    (void)frontend;
    if (n > (unsigned int)capacity) n = capacity;
    if (n > sizeof(wire)-wireIndex) n = sizeof(wire)-wireIndex;
    memcpy(out, wire+wireIndex, n);
    wireIndex += n;
    return n;
}

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "Native IQ failure at line %d\n", __LINE__); return 1; } } while (0)

static int VerifyHackrfDoppler(void)
{
    const unsigned int length = 8184;
    TNGeneConfiguration config = {0};
    TSinCosTable table = {0};
    char *input, *cosine, *sine, *real, *imaginary, *code;
    int direction, trial;
    unsigned int k;
    double power[2];
    config.fs = 8183800.0; config.ts = 1.0 / config.fs;
    REQUIRE(posix_memalign((void **)&input, 16, 2 * length) == 0);
    REQUIRE(posix_memalign((void **)&cosine, 16, length) == 0);
    REQUIRE(posix_memalign((void **)&sine, 16, length) == 0);
    REQUIRE(posix_memalign((void **)&real, 16, length) == 0);
    REQUIRE(posix_memalign((void **)&imaginary, 16, length) == 0);
    code = calloc(length, 1);
    REQUIRE(code != NULL);
    REQUIRE(InitCarrierTable(&config, &table, 38400.0) == SUCCESS_RETURN);
    for (direction = -1; direction <= 1; direction += 2) {
        double physicalDoppler = direction * 2000.0;
        for (k = 0; k < length; k++) {
            double phase = 0.17 + 2.0 * 3.14159265358979323846 *
                           (-38400.0 + physicalDoppler) * k / config.fs;
            input[2*k] = cos(phase) < 0 ? -1 : 1;
            input[2*k+1] = sin(phase) < 0 ? -1 : 1;
        }
        for (trial = 0; trial < 2; trial++) {
            double ncoDoppler = (trial ? 1 : -1) * physicalDoppler;
            int sumI, sumQ;
            GenerateCarrier(&config, &table, 38400.0 + ncoDoppler, 0, length, sine, cosine);
            CarrierInputWipeOff(1, input, 73, input + 146, length - 73,
                               cosine, sine, length, real, imaginary);
            sumI = ArrayMac(real, code, length) + length / 2;
            sumQ = ArrayMac(imaginary, code, length) + length / 2;
            power[trial] = (double)sumI * sumI + (double)sumQ * sumQ;
        }
        /* Production carrier generator, SSE2 IQ rotation and XOR MAC agree:
         * for native HackRF, NCO Doppler=-physical Doppler, correction=-1. */
        REQUIRE(power[0] > 2.0 * length * length);
        REQUIRE(power[0] > 100.0 * power[1]);
    }
    DeinitCarrierTable(&table);
    free(input); free(cosine); free(sine); free(real); free(imaginary); free(code);
    return 0;
}

int main(void)
{
    unsigned char raw[320];
    char iq[320], c[160], s[160], r[160], j[160], cr[160], sr[160];
    unsigned int offset, n, split, k, alias;
    TCircularBuffer ring = {0};
    TNGeneConfiguration cfg = {0};
    TUsbFePars pars = {0};
    TRxFe frontend = {0};
    char copied[32];
    unsigned int consumed = 0, iterations = 0;
    REQUIRE(posix_memalign((void **)&ngeneXmmsBackup, 16, 128) == 0);
    REQUIRE(VerifyHackrfDoppler() == 0);
    /* Every CI8 bit pattern, SIMD alignment and scalar tail; no marker bits. */
    for (offset = 0; offset < 16; offset++) for (n = 0; n <= 128; n++) {
        for (k = 0; k < 2*n; k++) raw[offset+k] = (unsigned char)k;
        memset(iq, 0x55, sizeof(iq));
        QuantizeIqSigns(raw+offset, iq+offset, n);
        for (k = 0; k < 2*n; k++)
            REQUIRE(iq[offset+k] == ((signed char)raw[offset+k] < 0 ? -1 : 1));
        REQUIRE(iq[offset+2*n] == 0x55);
    }

    /* Conditioning must change only the period-four modulation, never the
     * native component amplitudes. Check against the actual working IF code. */
    for (k=0; k<128; k++) raw[k] = ((k&1) ? 0 : 2) | ((k>>1)&1);
    REQUIRE(SigeDecodeNativeIq(raw, iq, 64) == 0);
    SigeSamplesElab((char *)raw, r, 128, 0);
    for (k=0; k<128; k++) REQUIRE(r[k] == iq[k] * ((k&2) ? -1 : 1));
    for (k=0; k<256; k++) {
        raw[0]=(unsigned char)(k|2); raw[1]=(unsigned char)(k&~2u);
        REQUIRE(SigeDecodeNativeIq(raw,iq,1) == 0);
        REQUIRE(iq[0] == 1-2*(k&1) && iq[1] == 1-2*(k&1));
    }

    /* Every byte value, all SIMD alignments and tails; marker/sign bits have
     * distinct meanings and upper wire bits must not affect the sign. */
    for (offset=0; offset<16; offset++) {
        for (n=0; n<=128; n++) {
            memset(iq, 0x55, sizeof(iq));
            for (k=0; k<2*n; k++) raw[offset+k] = (unsigned char)(((k*17+(k>>1)) & ~2u) | ((k&1) ? 0 : 2));
            REQUIRE(SigeDecodeNativeIq(raw+offset, iq+offset, n) == 0);
            for (k=0; k<2*n; k++) REQUIRE(iq[offset+k] == 1-2*(raw[offset+k]&1));
            if (n) {
                raw[offset+n] ^= 2;
                REQUIRE(SigeDecodeNativeIq(raw+offset, iq+offset, n) != 0);
            }
            REQUIRE(iq[offset+2*n] == 0x55);
        }
    }

    /* All I/Q and oscillator sign combinations, every split position and
     * in-place output aliases used by refinement, confirmation and tracking. */
    for (n=1; n<=65; n++) for (split=0; split<=n; split++) for (alias=0; alias<2; alias++) {
        for (k=0; k<n; k++) {
            iq[2*k] = (k&1) ? -1 : 1;
            iq[2*k+1] = (k&2) ? -1 : 1;
            c[k] = (k&4) ? -1 : 0;
            s[k] = (k&8) ? -1 : 0;
            cr[k] = c[k]; sr[k] = s[k];
        }
        CarrierInputWipeOff(1, iq, split, iq+2*split, n-split, c, s, n,
                             alias ? c : r, alias ? s : j);
        for (k=0; k<n; k++) {
            int ci = cr[k] ? -1 : 1, si = sr[k] ? -1 : 1;
            int bias = (k&1) ? -1 : 0;
            REQUIRE((alias ? c[k] : r[k]) == iq[2*k]*ci-iq[2*k+1]*si+bias);
            REQUIRE((alias ? s[k] : j[k]) == iq[2*k]*si+iq[2*k+1]*ci+bias);
            if (!alias) REQUIRE(c[k] == bias && s[k] == bias);
        }
    }

    /* Verify amplitude and zero-quadrature using the unmodified production
     * XOR MAC and its existing N/2 correction, for both code polarities. */
    for (k=0; k<64; k++) { iq[2*k]=1; iq[2*k+1]=-1; c[k]=s[k]=cr[k]=0; }
    CarrierInputWipeOff(1, iq, 64, NULL, 0, c, s, 64, c, s);
    REQUIRE(ArrayMac(c,cr,64)+32 == 128);
    REQUIRE(ArrayMac(s,cr,64)+32 == 0);
    memset(cr,-1,64);
    REQUIRE(ArrayMac(c,cr,64)+32 == -128);
    REQUIRE(ArrayMac(s,cr,64)+32 == 0);

    /* Real-input selection remains byte-for-byte the historical mixer. */
    for (k=0; k<65; k++) { iq[k] = (k&1) ? -1 : 1; c[k] = (k&2) ? -1 : 0; s[k] = (k&4) ? -1 : 0; }
    CarrierInputWipeOff(0, iq, 17, iq+17, 48, c, s, 65, r, j);
    for (k=0; k<65; k++) { REQUIRE(r[k] == (char)(iq[k]^c[k])); REQUIRE(j[k] == (char)(iq[k]^s[k])); }

    /* Transport starts on Q; odd and one-byte reads must retain the pending I.
     * Exercise real production ring insertion, deletion and physical wrap. */
    cfg.fs = 1000.0; cfg.nativeIq = cfg.sigeNativeIq = 1; cfg.usedFrontEnd = NGENE_FE_SIGE;
    cfg.fePars = &pars;
    REQUIRE(InitCircularBuffer(&cfg, &ring) == SUCCESS_RETURN);
    REQUIRE(ring.sampleStride == 2 && ring.dim == 16);
    wire[0] = 0;
    for (k=0; k<40; k++) { wire[1+2*k] = 2 | (k&1); wire[2+2*k] = (k>>1)&1; }
    while (consumed < 40 && iterations++ < 200) {
        unsigned int available, take;
        FillInputBuffer(&cfg, &ring, &frontend);
        available = ring.fill1+ring.fill2;
        take = available < 3 ? available : 3;
        for (k=0; k<take; k++) {
            unsigned int position = (ring.tail+k) % ring.dim;
            REQUIRE(ring.buffer[2*position] == 1-2*((consumed+k)&1));
            REQUIRE(ring.buffer[2*position+1] == 1-2*(((consumed+k)>>1)&1));
        }
        REQUIRE(DeleteFromCircularBuffer(&ring, take) == take);
        consumed += take;
    }
    REQUIRE(consumed == 40 && wireIndex == sizeof(wire) && !ring.sigeIqHasPending);
    /* HackRF signed CI8 has no SiGe marker bits: preserve chronological I/Q,
     * quantize every signed byte value, retain odd fragments and wrap pairs. */
    DeinitCircularBuffer(&ring);
    cfg.usedFrontEnd = NGENE_FE_HACKRF;
    REQUIRE(InitCircularBuffer(&cfg, &ring) == SUCCESS_RETURN);
    wireIndex = readCalls = consumed = iterations = 0;
    for (k = 0; k < sizeof(wire); k++) wire[k] = (unsigned char)(k * 73);
    while (consumed < 40 && iterations++ < 200) {
        unsigned int take;
        FillInputBuffer(&cfg, &ring, &frontend);
        take = ring.fill1 + ring.fill2;
        if (take > 3) take = 3;
        for (k = 0; k < 2 * take; k++) {
            unsigned int position = (ring.tail + k / 2) % ring.dim;
            REQUIRE(ring.buffer[2 * position + k % 2] ==
                    ((signed char)wire[2 * consumed + k] < 0 ? -1 : 1));
        }
        REQUIRE(DeleteFromCircularBuffer(&ring, take) == take);
        consumed += take;
    }
    REQUIRE(consumed == 40 && wireIndex == sizeof(wire) && ring.sigeIqHasPending);
    DeinitCircularBuffer(&ring);
    cfg.usedFrontEnd = NGENE_FE_SIGE;
    REQUIRE(InitCircularBuffer(&cfg, &ring) == SUCCESS_RETURN);
    wire[0] = 0;
    for (k=0; k<40; k++) { wire[1+2*k] = 2 | (k&1); wire[2+2*k] = (k>>1)&1; }
    /* A USB restart discards a pending old I and aligns the new stream by
     * its own markers, including a one-byte leading Q. */
    cfg.inputMethod = USB_FRONTEND;
    ring.sigeIqHasPending = ring.sigeIqSynchronized = 1;
    ring.sigeIqPending = 3;
    frontend.usbFe.usbReadGeneration = 1;
    wireIndex = readCalls = consumed = iterations = 0;
    while (consumed < 40 && iterations++ < 200) {
        unsigned int available, take;
        FillInputBuffer(&cfg, &ring, &frontend);
        available = ring.fill1+ring.fill2;
        take = available < 3 ? available : 3;
        for (k=0; k<take; k++) {
            unsigned int position = (ring.tail+k) % ring.dim;
            REQUIRE(ring.buffer[2*position] == 1-2*((consumed+k)&1));
            REQUIRE(ring.buffer[2*position+1] == 1-2*(((consumed+k)>>1)&1));
        }
        REQUIRE(DeleteFromCircularBuffer(&ring, take) == take);
        consumed += take;
    }
    REQUIRE(consumed == 40 && ring.sigeIqGeneration == 1 && !ring.sigeIqHasPending);
    /* A missing I marker must discard its unmatched Q, not swap the two
     * components of every subsequent sample or terminate the receiver. */
    wire[9] &= ~2u; /* Pair 4 is invalid. */
    frontend.usbFe.usbReadGeneration = 2;
    wireIndex = readCalls = consumed = iterations = 0;
    while (consumed < 39 && iterations++ < 200) {
        unsigned int available, take;
        FillInputBuffer(&cfg, &ring, &frontend);
        available = ring.fill1+ring.fill2;
        take = available < 3 ? available : 3;
        for (k=0; k<take; k++) {
            unsigned int source = consumed+k < 4 ? consumed+k : consumed+k+1;
            unsigned int position = (ring.tail+k) % ring.dim;
            REQUIRE(ring.buffer[2*position] == 1-2*(source&1));
            REQUIRE(ring.buffer[2*position+1] == 1-2*((source>>1)&1));
        }
        REQUIRE(DeleteFromCircularBuffer(&ring,take) == take);
        consumed += take;
    }
    REQUIRE(consumed == 39 && wireIndex == sizeof(wire) && !ring.sigeIqHasPending);
    for (k=0; k<ring.dim; k++) { ring.buffer[2*k] = (char)k; ring.buffer[2*k+1] = (char)(-1-(int)k); }
    ring.head = 3;
    REQUIRE(CopyABunchOfContiguousSamples(&ring, 7, copied) == SUCCESS_RETURN);
    for (k=0; k<7; k++) {
        unsigned int p = (ring.dim+ring.head-7+k) % ring.dim;
        REQUIRE(copied[2*k] == (char)p && copied[2*k+1] == (char)(-1-(int)p));
    }
    DeinitCircularBuffer(&ring);
    free(ngeneXmmsBackup);
    puts("Native I/Q decoding, scalar/SSE2 rotation, aliases, split reads and circular wrap passed.");
    return 0;
}
