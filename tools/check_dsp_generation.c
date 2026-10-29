/* Preservation fixture recorded from the original Windows /Od generators.
 * Covers code/carrier bytes, lookahead, output guards and continuous NCO phase.
 * Sample bytes are compared exactly on every platform. Phase uses the analytic
 * tolerance: Linux evaluates the historical L-suffixed constants in extended
 * precision, whereas native Windows long double has double precision.
 * No receiver loop/search constants are changed by intrinsic optimization. */
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef NGENE_NATIVE_WINDOWS
#include <windows.h>
#endif
#include "carrierTable.h"
#include "prnTable.h"
#include "ngeneConstants.h"
char *ngeneXmmsBackup;
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
            fprintf(stderr, "DSP generator failure line %d: %s\n", __LINE__, #x);                  \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
static uint64_t Digest(uint64_t hash, const void *data, unsigned n)
{
    const unsigned char *p = data;
    unsigned k;
    for (k = 0; k < n; k++) {
        hash ^= p[k];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}
int main(int argc, char **argv)
{
    const double rates[] = {8183800, 16367600};
    const double bases[] = {0, 38400, -38500, 1023000};
    const double offsets[] = {-5000, -0.1, 0, 0.1, 5000};
    const double phases[] = {0, 0.17, 1.5707963267948966, 3.1415926535897932, 6.2831843071795865};
    const unsigned sizes[] = {0, 1, 7, 15, 16, 17, 31, 32, 33, 63, 127, 8183, 8184, 8185};
    const TSatConstellation sats[] = {GPS_SAT,     GPS_SAT,     GPS_SAT,   GPS_SAT,   GALILEO_SAT,
                                      GALILEO_SAT, GALILEO_SAT, EGNOS_SAT, EGNOS_SAT, EGNOS_SAT};
    const unsigned prns[] = {1, 8, 18, 32, 1, 2, 36, 120, 124, 126};
    TNGeneConfiguration cfg = {0};
    unsigned a, b, c, d, e, n, k, cases = 0;
    char *s, *co, *code;
    uint64_t carrierHash = UINT64_C(14695981039346656037), codeHash = carrierHash;
    uint64_t carrierSamplesHash = carrierHash;
    double maxPhaseError = 0;
    int record = argc == 2 && !strcmp(argv[1], "--record");
#ifdef NGENE_NATIVE_WINDOWS
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    CHECK(posix_memalign((void **)&ngeneXmmsBackup, 16, 128) == 0);
    CHECK(posix_memalign((void **)&s, 16, 17000) == 0);
    CHECK(posix_memalign((void **)&co, 16, 17000) == 0);
    CHECK(posix_memalign((void **)&code, 16, 17000) == 0);
    for (a = 0; a < 2; a++) {
        cfg.fs = rates[a];
        cfg.ts = 1 / cfg.fs;
        for (b = 0; b < 4; b++) {
            TSinCosTable table = {0};
            CHECK(InitCarrierTable(&cfg, &table, bases[b]) == SUCCESS_RETURN);
            carrierHash = Digest(carrierHash, table.table, table.tableSize);
            carrierSamplesHash = Digest(carrierSamplesHash, table.table, table.tableSize);
            for (c = 0; c < 5; c++)
                for (d = 0; d < 5; d++)
                    for (e = 0; e < 14; e++) {
                        double f = bases[b] + offsets[c], out, expected;
                        n = sizes[e];
                        memset(s, 0x55, 17000);
                        memset(co, 0x55, 17000);
                        out = GenerateCarrier(&cfg, &table, f, phases[d], n, s, co);
                        expected = fmod(phases[d] + 2 * M_PI * n * f / cfg.fs, 2 * M_PI);
                        if (expected < 0)
                            expected += 2 * M_PI;
                        CHECK(fabs(out - expected) < 2e-12);
                        if (fabs(out - expected) > maxPhaseError)
                            maxPhaseError = fabs(out - expected);
                        for (k = n; k < n + 32; k++)
                            CHECK(s[k] == 0x55 && co[k] == 0x55);
                        for (k = 0; k < n; k++)
                            CHECK((s[k] == 0 || s[k] == -1) && (co[k] == 0 || co[k] == -1));
                        carrierHash = Digest(carrierHash, s, n);
                        carrierHash = Digest(carrierHash, co, n);
                        carrierHash = Digest(carrierHash, &out, sizeof(out));
                        carrierSamplesHash = Digest(carrierSamplesHash, s, n);
                        carrierSamplesHash = Digest(carrierSamplesHash, co, n);
                        memset(s, 0x55, 17000);
                        out = GenerateSine(&cfg, &table, f, phases[d], n, s);
                        CHECK(fabs(out - expected) < 2e-12);
                        if (fabs(out - expected) > maxPhaseError)
                            maxPhaseError = fabs(out - expected);
                        for (k = n; k < n + 32; k++)
                            CHECK(s[k] == 0x55);
                        carrierHash = Digest(carrierHash, s, n);
                        carrierHash = Digest(carrierHash, &out, sizeof(out));
                        carrierSamplesHash = Digest(carrierSamplesHash, s, n);
                        cases++;
                    }
            DeinitCarrierTable(&table);
        }
        {
            TPrnTable table = {0};
            snprintf(cfg.otherDataPath, sizeof(cfg.otherDataPath), "%s", NGENE_TEST_DATA_DIRECTORY);
            CHECK(InitPrnTable(&cfg, &table) == SUCCESS_RETURN);
            for (b = 0; b < 10; b++)
                for (c = 0; c < 2; c++)
                    for (d = 0; d < 4; d++)
                        for (e = 0; e < 14; e++) {
                            double len = sats[b] == GALILEO_SAT ? 4092 : 1023;
                            double phase = d == 0 ? 0 : d == 1 ? 0.125 : d == 2 ? 12.5 : len - 0.25;
                            double fraction = phase - floor(phase);
                            double rate = 1023000 + (int)(e % 3) * 4 - 4;
                            n = sizes[e];
                            memset(code, 0x55, 17000);
                            CHECK(GenerateCode(&cfg, &table, sats[b], prns[b], rate, &phase,
                                               &fraction, c ? BPSK_MODULATION : NGENE_MODULATION, n,
                                               code + 32) == SUCCESS_RETURN);
                            for (k = 0; k < 32; k++)
                                CHECK(code[k] == 0x55);
                            for (k = n + 64; k < n + 80; k++)
                                CHECK(code[k] == 0x55);
                            codeHash = Digest(codeHash, code + 32, n + 32);
                            codeHash = Digest(codeHash, &phase, sizeof(phase));
                            codeHash = Digest(codeHash, &fraction, sizeof(fraction));
                            cases++;
                        }
            DeinitPrnTable(&table);
        }
    }
    printf("%u generator cases: carrier %016llx, code %016llx\n", cases,
           (unsigned long long)carrierHash, (unsigned long long)codeHash);
    printf("Carrier sample bytes %016llx, max phase error %.3g rad\n",
           (unsigned long long)carrierSamplesHash, maxPhaseError);
    if (!record) {
        CHECK(carrierSamplesHash == UINT64_C(0x59d84d236984551a));
#if defined(NGENE_NATIVE_WINDOWS) && LDBL_MANT_DIG == 53
        /* Retain the original byte-for-byte double-phase Windows fixture too.
         * Linux phase results need not have the same last bits as Windows. */
        CHECK(carrierHash == UINT64_C(0x1de1df372a432ed0));
#endif
        CHECK(codeHash == UINT64_C(0xcf683d065183ee9d));
    }
    free(s);
    free(co);
    free(code);
    free(ngeneXmmsBackup);
    return 0;
}
