/* Independent quantized Gaussian fixtures and physical gain response model. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "hackrfGain.h"
#include "frontendStatus.h"
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "Gain test line %d: %s\n", __LINE__, #x);                              \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static void Gaussian(THackrfAdcHistogram *h, double muI, double muQ, double sigmaI, double sigmaQ)
{
    const double n = 1048576;
    double mu[2] = {muI, muQ}, sigma[2] = {sigmaI, sigmaQ};
    int c, x;
    memset(h, 0, sizeof(*h));
    h->pairs = (uint64_t)n;
    for (c = 0; c < 2; c++) {
        uint64_t before = 0;
        for (x = -128; x <= 127; x++) {
            uint64_t cumulative =
                x == 127 ? (uint64_t)n
                         : (uint64_t)llround(n * .5 *
                                             (1 + erf((x + .5 - mu[c]) / (sigma[c] * sqrt(2.)))));
            h->bins[c][(unsigned char)x] = cumulative - before;
            before = cumulative;
        }
    }
}

int main(void)
{
    char status[160] = "";
    THackrfAdcHistogram h;
    THackrfAdcStats s;
    int lna, vga, i, decision;
    CHECK(FrontendStatusUpdate(status, sizeof(status), "HackRF autotune 1/12: LNA 32 VGA 40"));
    CHECK(FrontendStatusUpdate(status, sizeof(status), "HackRF autotune settled: LNA 32 VGA 56"));
    CHECK(!FrontendStatusUpdate(status, sizeof(status), "Receiver initialized"));
    CHECK(strcmp(status, "HackRF autotune settled: LNA 32 VGA 56") == 0);
    CHECK(FrontendStatusUpdate(
        status, sizeof(status),
        "[time] WARNING: HackRF autotune bounded without confirmed target: LNA 0 VGA 0"));
    CHECK(strncmp(status, "HackRF autotune bounded", 22) == 0);
    CHECK(FrontendStatusUpdate(status, sizeof(status), "HackRF autotune failed: ADC 50%"));
    CHECK(strstr(status, "50%") != NULL);
    unsigned char bytes[] = {128, 127, 0, 0, 1, 255, 127, 128};
    memset(&h, 0, sizeof(h));
    HackrfAdcAccumulate(&h, bytes, sizeof(bytes));
    CHECK(h.pairs == 4 && h.bins[0][128] == 1 && h.bins[1][255] == 1);
    CHECK(HackrfAdcMeasure(&h, &s) && s.railFraction[0] == .5 && s.railFraction[1] == .5);
    Gaussian(&h, 10, -7, 28, 28);
    CHECK(HackrfAdcMeasure(&h, &s));
    CHECK(fabs(s.mean[0] - 10) < .02 && fabs(s.mean[1] + 7) < .02);
    CHECK(fabs(s.sigma[0] - 28) < .02 && fabs(s.sigma[1] - 28) < .02);
    CHECK(fabs(s.skew[0]) < .01 && fabs(s.kurtosis[1] - 3) < .02);
    lna = 32;
    vga = 56;
    CHECK(HackrfGainNext(&s, 28, &lna, &vga) == 0);
    Gaussian(&h, 0, 0, 4, 28);
    HackrfAdcMeasure(&h, &s);
    CHECK(HackrfGainNext(&s, 28, &lna, &vga) == 0); /* shared gain cannot balance I/Q */
    Gaussian(&h, 100, -90, 12, 12);
    HackrfAdcMeasure(&h, &s);
    lna = 32;
    vga = 40;
    CHECK(HackrfGainNext(&s, 28, &lna, &vga) == 1 && vga < 40);
    for (i = 0; i < 2; i++) {
        int trial;
        lna = 32;
        vga = i ? 62 : 16;
        decision = 1;
        for (trial = 0; trial < 12 && decision == 1; trial++) {
            double sigma = 28 * pow(10., (lna + vga - 88) / 20.);
            Gaussian(&h, 0, 0, sigma, sigma);
            HackrfAdcMeasure(&h, &s);
            decision = HackrfGainNext(&s, 28, &lna, &vga);
            CHECK(lna >= 0 && lna <= 40 && lna % 8 == 0 && vga >= 0 && vga <= 62 && vga % 2 == 0);
        }
        CHECK(decision == 0 && s.railFraction[0] < .001 && s.sigma[0] > 23 && s.sigma[0] < 33);
    }
    memset(&s, 0, sizeof(s));
    lna = 40;
    vga = 62;
    CHECK(HackrfGainNext(&s, 28, &lna, &vga) == 2);
    s.railFraction[0] = s.railFraction[1] = 1;
    lna = vga = 0;
    CHECK(HackrfGainNext(&s, 28, &lna, &vga) == 2);
    Gaussian(&h, 0, 0, 20, 20);
    HackrfAdcMeasure(&h, &s);
    lna = 32;
    vga = 62;
    CHECK(HackrfGainNext(&s, 28, &lna, &vga) == 1 && lna == 40 && vga < 62);
    s.sigma[0] = NAN;
    CHECK(HackrfGainNext(&s, 28, &lna, &vga) == 2);
    puts(
        "HackRF autotune: Gaussian/DC moments, clipping, convergence, IF/VGA limits and bounded failure PASS");
    return 0;
}
