/* Exercise production tracking/state transitions with controlled correlations.
 * Only RF input and C/N0 estimation are replaced, not the supervisor or loops. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ngeneDataStructure.h"
#include "tracking.h"
#include "gpsTracking.h"
#include "galileoTracking.h"
#include "trackingDiscriminatorsLoops.h"
#include "mathFunctions.h"
#include "arrays.h"
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "Tracking failure line %d: %s\n", __LINE__, #x);                       \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
static TNGeneConfiguration cfg;
static TNGeneData rx;
static TChannel ch;
static TUsbFePars frontend;
static TGpsParser gps;
static TGalParser gal;
static TEgnosParser sbas;
static TNavigationMessage nav;
static double testCno, testI, testQ;
static int decodedBits;
static short wide;
int SwrxLog(int severity, const char *format, ...)
{
    return 0;
}
void CloseNGene(TNGeneConfiguration *c, int status)
{
    abort();
}
void ArrayClean(char *p, int n)
{
    memset(p, 0, n);
}
void ArrayCopy(char *p, char *q, int n)
{
    memcpy(q, p, n);
}
void ArrayXor(char *a, char *b, char *out, unsigned n)
{
}
int ArrayMac(char *a, char *b, unsigned n)
{
    return 0;
}
float CorrelateWide(const short *s, const char *code, unsigned n)
{
    return 0;
}
void CorrelateWideEpl(const short *i, const short *q, const char *e, const char *p, const char *l,
                      unsigned n, float out[6])
{
    out[0] = out[4] = .1f;
    out[1] = out[5] = 0;
    out[2] = (float)testI;
    out[3] = (float)testQ;
}
double GenerateCarrier(TNGeneConfiguration *c, TSinCosTable *t, double f, double phase, unsigned n,
                       char *s, char *co)
{
    return fmod(phase + TWICE_M_PI * f * n / c->fs, TWICE_M_PI);
}
double GenerateSine(TNGeneConfiguration *c, TSinCosTable *t, double f, double phase, unsigned n,
                    char *s)
{
    return phase;
}
void CarrierInputWipeOff(int native, char *a, unsigned na, char *b, unsigned nb, char *co, char *si,
                         unsigned n, char *oi, char *oq)
{
}
double MixHackrfInput(const THackrfMixer *m, const THackrfIq8 *a, unsigned na, const THackrfIq8 *b,
                      unsigned nb, unsigned n, double f, double phase, double fs, short *i,
                      short *q)
{
    return fmod(phase + TWICE_M_PI * f * n / fs, TWICE_M_PI);
}
int GenerateCode(TNGeneConfiguration *c, TPrnTable *t, TSatConstellation type, int id, double rate,
                 double *phase, double *frac, TEmployedModulation mod, int n, char *out)
{
    double value = *phase + rate * n / c->fs;
    value = fmod(value, type == GALILEO_SAT ? 4092 : 1023);
    *phase = value;
    *frac = value - floor(value);
    return 0;
}
void AddCodePhases(TSatConstellation type, double phase, double add, double *out, double *frac)
{
    *out = fmod(phase + add, type == GALILEO_SAT ? 4092 : 1023);
    if (*out < 0)
        *out += type == GALILEO_SAT ? 4092 : 1023;
    *frac = *out - floor(*out);
}
double MeasureCno(TCnoTable *c, double i, double q, TNGeneConfiguration *conf)
{
    return testCno;
}
double ComputeEquivalentBandwidth(TCnoTable *c, double t, int n, double r)
{
    return 0;
}
void ComputeSigmaJ(TCnoTable *c, double value)
{
}
void SetCnoMeasurement(TCnoTable *c, double i, double q, int n, int m, double last)
{
}
void ParseGpsMessage(TNGeneConfiguration *c, TNGeneData *r, TChannel *channel, float v)
{
    decodedBits++;
}
void ParseGalMessage(TNGeneConfiguration *c, TNGeneData *r, TChannel *channel, float v)
{
    decodedBits++;
}
void ParseEgnosMessage(TNGeneConfiguration *c, TNGeneData *r, TChannel *channel, float v,
                       double time)
{
    decodedBits++;
}
void PredictRxTime(TNGeneConfiguration *c, TNGeneData *r)
{
}
void ResetReedSolomonData(TGalParser *p)
{
}
void InitChannelOsnmaVariables(TChannel *channel, TChannelOsnmaVariables *v)
{
    memset(v, 0, sizeof(*v));
}
void CleanEgnosParserArrays(TEgnosParser *p, int clean)
{
    memset(p->symbols, 0, sizeof(p->symbols));
}
static void Metrics(double ratio, double cno)
{
    TTrackingStatusChecker *c = &ch.tracking.trkStatusChecker;
    testI = sqrt((1 + ratio) / 2);
    testQ = sqrt((1 - ratio) / 2);
    ch.tracking.eI = ch.tracking.lI = .1f;
    ch.tracking.eQ = ch.tracking.lQ = 0;
    ch.tracking.pI = ch.tracking.pIForCno = (float)testI;
    ch.tracking.pQ = ch.tracking.pQForCno = (float)testQ;
    c->nbd = ratio;
    c->nbp = 1;
    /* One injected statistic per check, without estimator settling time. */
    InitAverageWindow(&c->pIMean, 1, testI * testI);
    InitAverageWindow(&c->pQMean, 1, testQ * testQ);
    c->cnoEstimator.estimatedCNo = cno;
    testCno = cno - (ch.satType == GALILEO_SAT ? GALILEO_CN0_CORRECTION_FACTOR
                                               : GPS_CN0_CORRECTION_FACTOR);
}
static void Setup(TSatConstellation type)
{
    memset(&cfg, 0, sizeof(cfg));
    memset(&rx, 0, sizeof(rx));
    memset(&ch, 0, sizeof(ch));
    memset(&gps, 0, sizeof(gps));
    memset(&gal, 0, sizeof(gal));
    memset(&sbas, 0, sizeof(sbas));
    memset(&nav, 0, sizeof(nav));
    cfg.fs = 1000;
    cfg.ts = .001;
    cfg.fePars = &frontend;
    frontend.dopplerCorrection = 1;
    cfg.automaticConfigurationFlag = TRUE_FLAG;
    cfg.trkConfiguration.holdoverTimeoutMs = 5000;
    cfg.trkConfiguration.cutoffCno = 30;
    cfg.trkConfiguration.gpsDllGain = cfg.trkConfiguration.galileoDllGain = 1;
    cfg.trkConfiguration.gpsDllBw = cfg.trkConfiguration.galileoDllBw =
        cfg.trkConfiguration.egnosDllBw = 2;
    cfg.trkConfiguration.gpsPllBw = cfg.trkConfiguration.galileoPllBw =
        cfg.trkConfiguration.egnosPllBw = 15;
    cfg.trkConfiguration.gpsFllBw = cfg.trkConfiguration.galileoFllBw =
        cfg.trkConfiguration.egnosFllBw = 15;
    cfg.trkConfiguration.gpsFllGain = cfg.trkConfiguration.galileoFllGain = 1;
    cfg.trkConfiguration.gpsPllGain = cfg.trkConfiguration.galileoPllGain = 1;
    cfg.trkConfiguration.egnosDllGain = cfg.trkConfiguration.egnosFllGain =
        cfg.trkConfiguration.egnosPllGain = 1;
    cfg.trkConfiguration.fllPllChoiceFilterLen = 20;
    ch.satType = type;
    ch.satId = 7;
    ch.chState = TRACKING_STATE;
    ch.blocksize = 1;
    ch.codeDuration = type == GALILEO_SAT ? 4 : 1;
    ch.bitDuration = type == GPS_SAT ? 20 : (type == GALILEO_SAT ? 4 : 2);
    ch.carrierFrequency = ch.dopplerShift = 100.125;
    ch.codeRate = GPS_CODE_RATE * (1 + ch.dopplerShift / L1_FREQUENCY);
    ch.tracking.trkCounter = 200;
    ch.tracking.trkStatus = TRACKING_LOCKED;
    ch.tracking.integrationPeriod = ch.codeDuration;
    ch.tracking.integrationPeriodUser = ch.codeDuration;
    ch.tracking.integrationPeriodUserActive = TRUE_FLAG;
    ch.tracking.bitSynchronizationFound = ch.tracking.fllLocked = TRUE_FLAG;
    ch.gpsParser = &gps;
    ch.galParser = &gal;
    ch.egnosParser = &sbas;
    gps.navMsg = gal.navMsg = &nav;
    nav.towValid = nav.wnValid = nav.ephemeridesReceived = TRUE_FLAG;
    rx.wideI = rx.wideQ = &wide;
    InitTrackingStatusChecker(&cfg, &ch, &ch.tracking);
    StartTrackingStatusChecker(&cfg, &ch.tracking);
    InitTrackingLoops(&cfg, &ch, &ch.tracking);
    InitAverageWindow(&ch.codeRateMean, 200, ch.codeRate);
    Metrics(.9, 40);
    CheckTrackingLock(&cfg, &rx, &ch, &ch.tracking);
    decodedBits = 0;
}
static void Finish(void)
{
    DeinitTrackingStatusChecker(&ch.tracking);
}
static void Tick(unsigned long long ms, double ratio, double cno)
{
    ch.tracking.trkCounter = ms;
    Metrics(ratio, cno);
    CheckTrackingLock(&cfg, &rx, &ch, &ch.tracking);
}
static int CheckRecovery(void)
{
    unsigned long long start, t;
    Setup(GPS_SAT);
    CHECK(ch.tracking.holdover.anchorValid);
    ch.pvtInfo.usedForPVT = TRUE_FLAG;
    Tick(301, .2, 20);
    start = ch.tracking.holdover.firstFailureCounter;
    CHECK(ch.tracking.trkStatus == TRACKING_HOLDOVER && !ch.pvtInfo.usedForPVT);
    CHECK(!nav.towValid && nav.wnValid && nav.ephemeridesReceived);
    /* A single good statistic is insufficient; both metrics must agree. */
    Tick(start + 100, .9, 20);
    Tick(start + 200, .2, 40);
    Tick(start + 300, .6, 36);
    CHECK(ch.tracking.trkStatus == TRACKING_HOLDOVER);
    Tick(start + 400, .6, 36);
    Tick(start + 500, .6, 36);
    CHECK(ch.tracking.trkStatus == TRACKING_RELOCK);
    /* Recover a previously good GPS below 0.8 without the reentry trap. */
    for (t = start + 600; t < start + 4900 && ch.tracking.trkStatus != TRACKING_LOCKED; t++)
        Tick(t, .6, 36);
    CHECK(ch.tracking.trkStatus == TRACKING_LOCKED && !ch.tracking.holdover.active);
    CHECK(t - start < 1600); /* no fixed post-correction wait when no correction occurred */
    CHECK(!TrackingNavigationEnabled(&ch.tracking)); /* partial first bit */
    CHECK(TrackingNavigationEnabled(&ch.tracking));
    CHECK(!nav.towValid); /* lock alone must not invent a decoded TOW */
    Finish();
    return 0;
}
static int CheckDeadline(void)
{
    unsigned long long start, t;
    Setup(GALILEO_SAT);
    Tick(304, .1, 20);
    start = ch.tracking.holdover.firstFailureCounter;
    for (t = start + 100; t < start + 5000; t += 100) {
        Tick(t, (t / 100) % 4 == 0 ? .1 : .9, 36);
        CHECK(ch.tracking.trkStatus != TRACKING_LOCKED);
        CHECK(ch.tracking.holdover.firstFailureCounter == start);
    }
    Tick(start + 5000, .9, 40);
    CHECK(ch.tracking.trkStatus == TRACKING_LOST);
    Finish();
    Setup(GPS_SAT);
    Tick(301, .1, 20);
    start = ch.tracking.holdover.firstFailureCounter;
    ch.tracking.trkStatus = TRACKING_PLL_FALSE_LOCK_CONFIRMATION;
    Tick(start + 5000, .99, 45);
    CHECK(ch.tracking.trkStatus == TRACKING_LOST); /* includes all confirmation */
    Finish();
    return 0;
}
static int CheckUnalignedCarrier(void)
{
    unsigned long long start;
    Setup(GPS_SAT);
    Tick(301, .1, 20);
    start = ch.tracking.holdover.firstFailureCounter;
    /* Strong code peak with the carrier near quadrature: allow the PLL to
     * align itself, but never call this a recovered navigation measurement. */
    Tick(start + 100, -.9, 40);
    Tick(start + 200, -.9, 40);
    Tick(start + 300, -.9, 40);
    CHECK(ch.tracking.trkStatus == TRACKING_RELOCK);
    CHECK(!TrackingNavigationEnabled(&ch.tracking));
    Tick(start + 400, -.9, 40);
    CHECK(ch.tracking.trkStatus == TRACKING_HOLDOVER);
    CHECK(ch.tracking.holdover.firstFailureCounter == start);
    Finish();
    return 0;
}
static int CheckPredictedPhase(void)
{
    double carrier, code;
    Setup(GPS_SAT);
    carrier = fmod(TWICE_M_PI * ch.carrierFrequency * .1, TWICE_M_PI);
    code = fmod(ch.codeRate * .1, 1023);
    ch.bufferOffset = 100;
    ch.carrierPhase = 1;
    ch.codePhase = 99;
    ch.dopplerShiftOld = 42;
    Tick(301, .1, 20);
    CHECK(fabs(ch.carrierPhase - carrier) < 1e-10 && fabs(ch.codePhase - code) < 1e-10);
    CHECK(ch.dopplerShiftOld == 42); /* completed-block observable is not rewritten */
    Finish();
    return 0;
}
static int CheckCorrectedCarrier(void)
{
    unsigned long long start, t;
    double previous;
    Setup(GPS_SAT);
    Tick(301, .1, 20);
    start = ch.tracking.holdover.firstFailureCounter;
    for (t = start + 1; t <= start + 600; t++)
        Tick(t, .9, 40);
    CHECK(ch.tracking.trkStatus == TRACKING_PLL_FALSE_LOCK_CHECK);
    previous = ch.dopplerShift;
    InitAverageWindow(&ch.codeRateMean, 200, GPS_CODE_RATE * (1 + (previous + 500) / L1_FREQUENCY));
    for (; t <= start + 2100; t++)
        Tick(t, .9, 40);
    CHECK(ch.tracking.trkStatus == TRACKING_PLL_FALSE_LOCK_CONFIRMATION);
    CHECK(fabs(ch.dopplerShift - previous - 500) < 1e-8);
    for (; t <= start + 2700; t++)
        Tick(t, .9, 40);
    CHECK(ch.tracking.trkStatus == TRACKING_LOCKED);
    Finish();
    return 0;
}
static int CheckSignalTime(TSatConstellation type)
{
    double frequency, rate, phase;
    int i;
    Setup(type);
    Tick(304, .1, 20);
    frequency = ch.carrierFrequency;
    rate = ch.codeRate;
    phase = ch.carrierPhase;
    Metrics(.1, 20);
    for (i = 0; i < 200; i++) {
        if (type == GALILEO_SAT)
            PerformGalileoTracking(&cfg, &rx, &ch, NULL, 1, NULL, 0);
        else
            PerformGpsTracking(&cfg, &rx, &ch, NULL, 1, NULL, 0);
        CHECK(ch.carrierFrequency == frequency && ch.codeRate == rate);
        CHECK(ch.tracking.trkStatus == TRACKING_HOLDOVER);
    }
    CHECK(decodedBits == 0 && ch.tracking.trkCounter == 504);
    CHECK(fabs(ch.carrierPhase - phase) > .01);
    CHECK(ch.codePhase + ch.codePhaseFrac > 0);
    Finish();
    return 0;
}
static int CheckStartupAndInvalidMetrics(void)
{
    Setup(GPS_SAT);
    ch.tracking.holdover.anchorValid = FALSE_FLAG;
    ch.tracking.trkStatus = TRACKING_PLL_FALSE_LOCK_CHECK;
    ch.tracking.trkStatusChecker.pllLastCheckCounter = 200;
    Tick(201, .6, 36);
    CHECK(ch.tracking.trkStatus == TRACKING_PLL_FALSE_LOCK_CHECK);
    Tick(300, .6, 36);
    CHECK(ch.tracking.trkStatus == TRACKING_PLL_CHECK_FAILED);
    Tick(1301, .6, 36);
    CHECK(ch.tracking.trkStatus == TRACKING_LOST);
    CHECK(!ch.tracking.holdover.active);
    Finish();
    Setup(GPS_SAT);
    Tick(301, .1, NAN);
    CHECK(ch.tracking.trkStatus == TRACKING_HOLDOVER);
    Tick(401, NAN, 45);
    CHECK(ch.tracking.trkStatus == TRACKING_HOLDOVER);
    StartTracking(&cfg, &ch, &ch.tracking);
    CHECK(!ch.tracking.holdover.anchorValid && !ch.tracking.holdover.active);
    Finish();
    return 0;
}
int main(void)
{
    if (CheckRecovery() || CheckDeadline() || CheckUnalignedCarrier() || CheckPredictedPhase() ||
        CheckCorrectedCarrier() || CheckSignalTime(GPS_SAT) || CheckSignalTime(GALILEO_SAT) ||
        CheckSignalTime(EGNOS_SAT) || CheckStartupAndInvalidMetrics())
        return 1;
    puts(
        "Tracking holdover: recovery, deadline, signal time, erasures, startup and invalid metrics PASS");
    return 0;
}
