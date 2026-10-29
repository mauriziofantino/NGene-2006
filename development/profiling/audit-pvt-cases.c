/* Independent synthetic fixtures, not part of the production executable.
 * The generated translation unit appends exact NGene function definitions. */
#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ngeneDataStructure.h"
#include "ngeneConfiguration.h"
#include "pvt.h"
#include "pvtUtility.h"
#include "kalmanFilter.h"
#include "carrierSmoothing.h"
#include "matrices.h"
#include "timeConversions.h"
#include "egnosCorrections.h"
#include "ioFunctions.h"
#include "leastSquares.h"
#include "dop.h"
#include "pntNumerics.h"
#include "gnssTimeMath.h"
#include "navigationMessageParser.h"
#include "gpsMessageParser.h"
#include "galileoMessageParser.h"
#include "pvtChecks.h"
int ParseGpsSubframe1(TGpsParser *);
int ParseGalWord01(TGalParser *);
int ParseGalWord04(TGalParser *);
int ParseGalWord05(TNGeneData *, TGalParser *);
int ParseGpsSubframe4(TNGeneData *, TGpsParser *);
void ComputeTransmissionTime(TNGeneConfiguration *, TPvtData *);
int VerifyTransmissionTime(TPvtData *);
void ComputeSatsPositions(TNGeneConfiguration *, TNGeneData *, TPvtData *, TPvtResults *);
void ComputePseudoranges(TNGeneConfiguration *, TNGeneData *, TPvtData *, TPvtResults *);
void DeleteNotSmoothed(TNGeneConfiguration *, TPvtData *);
int ApplyElevationMask(TNGeneConfiguration *, TPvtData *);
static void UpdatePvtMinimumChannels(TPvtData *);
static void ResetPseudorangeOrigin(TNGeneConfiguration *, TNGeneData *, TSatConstellation,
                                   TPvtResults *);
void ClearKalmanFilterData(TNGeneConfiguration *, TKalmanFilterData *);
int KalmanFilterPrediction(int, TKalmanFilterData *, double *, double *, double *);
void PreparePPost(TKalmanFilterData *, TSatConstellation);
void UpdatePPostMulti(TKalmanFilterData *, TSatConstellation);
/* Disable external corrections/logging only; tested mathematics is original. */
int SwrxLog(int severity, const char *format, ...)
{
    (void)severity;
    (void)format;
    return 0;
}
void CloseNGene(TNGeneConfiguration *cfg, int status)
{
    (void)cfg;
    exit(status ? status : 99);
}
void ResetPvt(TNGeneConfiguration *, TPvtData *, TPvtResults *, int);
double GetEgnosFastCorrection(TNGeneConfiguration *c, int i, TEgnosCorrections *e, double t)
{
    (void)c;
    (void)i;
    (void)e;
    (void)t;
    return 0;
}
double GetEgnosLongTermClockCorrection(TNGeneConfiguration *c, int d, int i, TEgnosCorrections *e,
                                       double t)
{
    (void)c;
    (void)d;
    (void)i;
    (void)e;
    (void)t;
    return 0;
}
void GetEgnosLongTermPositionCorrection(TNGeneConfiguration *c, int d, int i, TEgnosCorrections *e,
                                        double t, double *x, double *y, double *z)
{
    (void)c;
    (void)d;
    (void)i;
    (void)e;
    (void)t;
    *x = *y = *z = 0;
}
void GetIonosphericPiercePoint(double *p, double a, double e, double *lat, double *lon, double *f)
{
    (void)p;
    (void)a;
    (void)e;
    *lat = *lon = 0;
    *f = 1;
}
static TNGeneData rx;
static TNGeneConfiguration cfg;
static TUsbFePars fe;
static TChannel channels[8];
static TPvtChannel observations[8];
static TSatStatus satellites[8];
static TSatStatus gpsStatus[GPS_NUMBER_OF_PRNS], galStatus[GALILEO_NUMBER_OF_PRNS];
static TGpsParser gps[8];
static TGalParser gal[8];
static TNavigationMessage nav[8];
static TNavigationMessage galBank[GALILEO_NUMBER_OF_PRNS];
static TCarrierSmoothingData smoothing[8];
static int mismatches;
static void Fixture(int n, int mixed)
{
    int i;
    memset(&rx, 0, sizeof(rx));
    memset(&cfg, 0, sizeof(cfg));
    memset(&fe, 0, sizeof(fe));
    memset(channels, 0, sizeof(channels));
    memset(observations, 0, sizeof(observations));
    memset(nav, 0, sizeof(nav));
    memset(satellites, 0, sizeof(satellites));
    memset(smoothing, 0, sizeof(smoothing));
    memset(galBank, 0, sizeof(galBank));
    rx.galNavigationMessage = galBank;
    cfg.numberOfChannels = 8;
    cfg.numberOfGpsChannels = 8;
    cfg.numberOfGalileoChannels = mixed ? 4 : 0;
    cfg.fs = 8183800;
    cfg.ts = 1 / cfg.fs;
    cfg.pvtPeriodSeconds = 1;
    cfg.fePars = &fe;
    fe.dopplerCorrection = 1;
    rx.pvtData.pvtChArray = observations;
    rx.pvtData.csmData = smoothing;
    rx.channelArray = channels;
    rx.pvtTemporaryResults.gpsSatStatus = gpsStatus;
    rx.pvtTemporaryResults.galSatStatus = galStatus;
    rx.pvtResults.gpsSatStatus = gpsStatus;
    rx.pvtResults.galSatStatus = galStatus;
    rx.pvtData.numOfOperativeChannels = n;
    rx.pvtData.numOfGpsOpChannels = mixed ? n / 2 : n;
    rx.pvtData.numOfGalileoOpChannels = mixed ? n - n / 2 : 0;
    for (i = 0; i < n; i++) {
        channels[i].satType = mixed && i >= n / 2 ? GALILEO_SAT : GPS_SAT;
        channels[i].satId = i + 1;
        channels[i].chIndex = i;
        channels[i].gpsParser = &gps[i];
        channels[i].galParser = &gal[i];
        gps[i].navMsg = &nav[i];
        gal[i].navMsg = &nav[i];
        observations[i].ch = &channels[i];
        observations[i].satStatus = &satellites[i];
        nav[i].eph.sqrtA = 5153.7954775;
        nav[i].eph.eccentricity = .01;
        nav[i].eph.i0 = .96;
        nav[i].eph.meanAnomaly0 = .5 + i;
        nav[i].eph.capitalOmega0 = .3 + i;
        nav[i].eph.omega = .6;
        nav[i].eph.capitalOmegaDot = -8e-9;
        nav[i].eph.toe = 100000;
        nav[i].eph.capitalCuc = 2e-6;
        nav[i].eph.capitalCus = 8e-6;
        nav[i].eph.capitalCrc = 200;
        nav[i].eph.capitalCrs = -60;
        observations[i].transmissionTime = 103600;
        nav[i].clk.toc = 100000;
    }
}
static void Result(const char *name, double actual, double expected, double tolerance)
{
    if (!isfinite(actual) || fabs(actual - expected) > tolerance)
        mismatches++;
    printf("%s,%.12g,%.12g,%s\n", name, actual, expected,
           isfinite(actual) && fabs(actual - expected) <= tolerance ? "PASS" : "MISMATCH");
}
/* Independent transcription of ICD orbit equations, with Newton's method. */
static void OrbitReference(const TGnssEphemerides *e, double t, double mu, double p[3])
{
    double a = e->sqrtA * e->sqrtA, tk = t - e->toe, n = sqrt(mu / (a * a * a)) + e->deltaN;
    double m = e->meanAnomaly0 + n * tk, E = m, phi, u, r, inc, omega, x, y;
    int k;
    for (k = 0; k < 12; k++)
        E -= (E - e->eccentricity * sin(E) - m) / (1 - e->eccentricity * cos(E));
    phi = atan2(sqrt(1 - e->eccentricity * e->eccentricity) * sin(E), cos(E) - e->eccentricity) +
          e->omega;
    u = phi + e->capitalCus * sin(2 * phi) + e->capitalCuc * cos(2 * phi);
    r = a * (1 - e->eccentricity * cos(E)) + e->capitalCrs * sin(2 * phi) +
        e->capitalCrc * cos(2 * phi);
    inc = e->i0 + e->iDot * tk + e->capitalCis * sin(2 * phi) + e->capitalCic * cos(2 * phi);
    omega = e->capitalOmega0 + (e->capitalOmegaDot - OMEGAE_DOT) * tk - OMEGAE_DOT * e->toe;
    x = r * cos(u);
    y = r * sin(u);
    p[0] = x * cos(omega) - y * cos(inc) * sin(omega);
    p[1] = x * sin(omega) + y * cos(inc) * cos(omega);
    p[2] = y * sin(inc);
}
static void PerfectKf(double bias)
{
    int i, j;
    Fixture(8, 1);
    InitKalmanFilterData(&cfg, &rx);
    rx.pvtTemporaryResults.ecefPosition[0] = 4.5e6;
    rx.pvtTemporaryResults.ecefPosition[2] = 4.5e6;
    rx.pvtTemporaryResults.clockBiasGpsValid = rx.pvtTemporaryResults.clockBiasGalileoValid = 1;
    rx.pvtTemporaryResults.clockBiasGps = rx.pvtTemporaryResults.clockBiasGalileo = bias;
    for (i = 0; i < 8; i++) {
        double dir[3] = {cos(i * 2.4) * .7, sin(i * 2.4) * .7, sqrt(.51)},
               v[3] = {-1000, 1500, 2000};
        double range = 2.2e7, angle = range / SPEED_OF_LIGHT * WEDOT, rot[3], radial = 0;
        double p[3] = {4.5e6, 0, 4.5e6};
        for (j = 0; j < 3; j++) {
            rot[j] = p[j] + range * dir[j];
            satellites[i].ecefSatVel[j] = v[j];
        }
        radial = (cos(angle) * v[0] + sin(angle) * v[1]) * dir[0] +
                 (-sin(angle) * v[0] + cos(angle) * v[1]) * dir[1] + v[2] * dir[2];
        satellites[i].ecefSatPos[0] = rot[0] * cos(angle) - rot[1] * sin(angle);
        satellites[i].ecefSatPos[1] = rot[0] * sin(angle) + rot[1] * cos(angle);
        satellites[i].ecefSatPos[2] = rot[2];
        satellites[i].elevation = .7;
        observations[i].pseudorangeCsm = range + bias;
        observations[i].deltaPseudorange = -radial / LAMBDA;
        observations[i].posSigma2 = 10;
        observations[i].velSigma2 = 1;
    }
}
static void CheckLeastSquares(int mixed, int weighted)
{
    const double lat = 45.06538716666666 * DEGREE_TO_RADIAN,
                 lon = 7.659323472222223 * DEGREE_TO_RADIAN;
    double radius = A_CONST / sqrt(1 - E_SQUARE_CONST * sin(lat) * sin(lat)), truth[3], error = 0;
    int i, j;
    Fixture(8, mixed);
    InitLeastSquaresData(&cfg, &rx);
    rx.pvtData.pvtIndex = 1;
    cfg.pvtConfiguration.weightedLs = weighted;
    for (i = 0; i < 8; i++) {
        observations[i].posSigma2 = 10;
        observations[i].velSigma2 = 1;
    }
    truth[0] = (radius + 311.9349) * cos(lat) * cos(lon);
    truth[1] = (radius + 311.9349) * cos(lat) * sin(lon);
    truth[2] = (radius * (1 - E_SQUARE_CONST) + 311.9349) * sin(lat);
    for (j = 0; j < 3; j++)
        rx.pvtTemporaryResults.ecefPosition[j] = truth[j] + 50;
    for (i = 0; i < 8; i++) {
        double az = i * 2.4, el = .2 + .1 * (i % 5), east = cos(el) * sin(az),
               north = cos(el) * cos(az), up = sin(el);
        double dir[3] = {-sin(lon) * east - sin(lat) * cos(lon) * north + cos(lat) * cos(lon) * up,
                         cos(lon) * east - sin(lat) * sin(lon) * north + cos(lat) * sin(lon) * up,
                         cos(lat) * north + sin(lat) * up};
        double range = 2.2e7, angle = range / SPEED_OF_LIGHT * OMEGAE_DOT, rot[3];
        for (j = 0; j < 3; j++)
            rot[j] = truth[j] + range * dir[j];
        satellites[i].ecefSatPos[0] = rot[0] * cos(angle) - rot[1] * sin(angle);
        satellites[i].ecefSatPos[1] = rot[0] * sin(angle) + rot[1] * cos(angle);
        satellites[i].ecefSatPos[2] = rot[2];
        observations[i].pseudorangeCsm = range + (channels[i].satType == GPS_SAT ? 100 : 200000) -
                                         TropoHopfieldModel(el, 311.9349);
        observations[i].satClkDrift = i * 1e-11;
        observations[i].deltaPseudorange = SPEED_OF_LIGHT * observations[i].satClkDrift / LAMBDA;
    }
    Result(mixed ? "mixed_ls_noiseless_success" : "gps_ls_noiseless_success",
           PerformLeastSquares(&cfg, &rx), SUCCESS_RETURN, 0);
    for (j = 0; j < 3; j++)
        error += pow(rx.pvtTemporaryResults.ecefPosition[j] - truth[j], 2);
    Result(mixed ? "mixed_ls_noiseless_position_error_m" : "gps_ls_noiseless_position_error_m",
           sqrt(error), 0, 1e-3);
    Result(
        mixed ? "mixed_ls_satellite_clock_rate_speed_mps" : "gps_ls_satellite_clock_rate_speed_mps",
        hypot(hypot(rx.pvtTemporaryResults.ecefVelocity[0], rx.pvtTemporaryResults.ecefVelocity[1]),
              rx.pvtTemporaryResults.ecefVelocity[2]),
        0, 1e-8);
    rx.pvtTemporaryResults.pvtValid = 1;
    Result(mixed ? "mixed_dop_available" : "gps_dop_available", DopEvaluation(&rx, LMS_BASED_PVT),
           TRUE_FLAG, 0);
    {
        double *d = rx.pvtTemporaryResults.dop;
        Result("dop_enu_trace_identity", d[PDOP_INDEX_IN_DOP_ARRAY] * d[PDOP_INDEX_IN_DOP_ARRAY],
               d[HDOP_INDEX_IN_DOP_ARRAY] * d[HDOP_INDEX_IN_DOP_ARRAY] +
                   d[VDOP_INDEX_IN_DOP_ARRAY] * d[VDOP_INDEX_IN_DOP_ARRAY],
               1e-9);
    }
    if (!mixed) {
        rx.pvtData.numOfOperativeChannels = rx.pvtData.numOfGpsOpChannels = 3;
        Result("three_kf_measurements_geometric_dop_unavailable",
               DopEvaluation(&rx, KALMAN_BASED_PVT), FALSE_FLAG, 0);
    }
    DeinitLeastSquaresData(&rx);
}
/* Bit locations are transcribed from the ICD tables, not parser macros.
 * GPS decoded words retain two preceding parity bits and six trailing parity
 * bits in NGene's 32-bit container; this tests fields, not CRC/FEC recovery. */
static void PutBits(unsigned char *bytes, int start, int count, uint32_t value)
{
    int k;
    for (k = 0; k < count; k++) {
        unsigned mask = 1u << (7 - (start + k) % 8);
        bytes[(start + k) / 8] =
            (bytes[(start + k) / 8] & ~mask) | (((value >> (count - 1 - k)) & 1) ? mask : 0);
    }
}
static void CheckNavigationFields(void)
{
    Fixture(1, 0);
    memset(gal[0].bitsForParse, 0, 16);
    PutBits(gal[0].bitsForParse, 0, 6, 1);
    PutBits(gal[0].bitsForParse, 6, 10, 123);
    PutBits(gal[0].bitsForParse, 16, 14, 1000);
    PutBits(gal[0].bitsForParse, 30, 32, (uint32_t)-1234567);
    PutBits(gal[0].bitsForParse, 62, 32, 85899346);
    PutBits(gal[0].bitsForParse, 94, 32, 2700000000u);
    ParseGalWord01(&gal[0]);
    Result("gal_word1_signed_M0_rad", nav[0].ephTmp.meanAnomaly0, -1234567 * ldexp(M_PI, -31),
           1e-14);
    Result("gal_word1_unsigned_sqrtA", nav[0].ephTmp.sqrtA, ldexp(2700000000., -19), 1e-10);
    memset(gal[0].bitsForParse, 0, 16);
    PutBits(gal[0].bitsForParse, 0, 6, 4);
    PutBits(gal[0].bitsForParse, 54, 14, 1000);
    PutBits(gal[0].bitsForParse, 68, 31, (uint32_t)-123456);
    PutBits(gal[0].bitsForParse, 99, 21, (uint32_t)-12345);
    PutBits(gal[0].bitsForParse, 120, 6, (uint32_t)-3);
    ParseGalWord04(&gal[0]);
    Result("gal_word4_toc_s", nav[0].clkTmp.toc, 60000, 0);
    Result("gal_word4_af0_s", nav[0].clkTmp.af[0], ldexp(-123456., -34), 1e-15);
    Result("gal_word4_af1_sps", nav[0].clkTmp.af[1], ldexp(-12345., -46), 1e-20);
    Result("gal_word4_af2_sps2", nav[0].clkTmp.af[2], ldexp(-3., -59), 1e-25);
    memset(gps[0].subframe, 0, 40);
    PutBits(gps[0].subframe, 66, 10, 352);
    PutBits(gps[0].subframe, 82, 6, 2);
    PutBits(gps[0].subframe, 234, 16, 1000);
    PutBits(gps[0].subframe, 258, 8, (uint32_t)-3);
    PutBits(gps[0].subframe, 266, 16, (uint32_t)-1234);
    PutBits(gps[0].subframe, 290, 22, (uint32_t)-123456);
    ParseGpsSubframe1(&gps[0]);
    Result("gps_sf1_toc_s", nav[0].clkTmp.toc, 16000, 0);
    Result("gps_sf1_af0_s", nav[0].clkTmp.af[0], ldexp(-123456., -31), 1e-15);
    Result("gps_sf1_af1_sps", nav[0].clkTmp.af[1], ldexp(-1234., -43), 1e-20);
    Result("gps_sf1_af2_sps2", nav[0].clkTmp.af[2], ldexp(-3., -55), 1e-25);
    Result("gps_all_signals_dead_health", nav[0].ephTmp.health, SIGNAL_OUT_OF_SERVICE, 0);
    memset(gal[0].bitsForParse, 0, 16);
    PutBits(gal[0].bitsForParse, 0, 6, 5);
    PutBits(gal[0].bitsForParse, 6, 11, 321);
    PutBits(gal[0].bitsForParse, 17, 11, (uint32_t)-123);
    PutBits(gal[0].bitsForParse, 28, 14, (uint32_t)-456);
    PutBits(gal[0].bitsForParse, 47, 10, (uint32_t)-123);
    PutBits(gal[0].bitsForParse, 57, 10, 234);
    PutBits(gal[0].bitsForParse, 73, 12, 1400);
    PutBits(gal[0].bitsForParse, 85, 20, 345678);
    ParseGalWord05(&rx, &gal[0]);
    Result("gal_iono_ai0_sfu", nav[0].iono.alphaGal[0], 321 * .25, 0);
    Result("gal_iono_ai1_signed", nav[0].iono.alphaGal[1], ldexp(-123., -8), 0);
    Result("gal_iono_ai2_signed", nav[0].iono.alphaGal[2], ldexp(-456., -15), 0);
    Result("gal_bgd_e1e5a_signed_s", nav[0].clkTmp.bgd_E1E5a, ldexp(-123., -32), 0);
    Result("gal_bgd_e1e5b_s", nav[0].clkTmp.bgd_E1E5b, ldexp(234., -32), 0);
    Result("gal_word5_tow_s", nav[0].tow, 345678, 0);
    memset(gps[0].subframe, 0, 40);
    PutBits(gps[0].subframe, 66, 2, 1);
    PutBits(gps[0].subframe, 68, 6, 56);
    PutBits(gps[0].subframe, 74, 8, (uint32_t)-3);
    PutBits(gps[0].subframe, 82, 8, 5);
    PutBits(gps[0].subframe, 98, 8, (uint32_t)-7);
    PutBits(gps[0].subframe, 106, 8, 11);
    PutBits(gps[0].subframe, 114, 8, 67);
    PutBits(gps[0].subframe, 130, 8, (uint32_t)-13);
    PutBits(gps[0].subframe, 138, 8, 17);
    PutBits(gps[0].subframe, 146, 8, (uint32_t)-19);
    ParseGpsSubframe4(&rx, &gps[0]);
    Result("gps_iono_alpha0_signed", nav[0].iono.alphaGps[0], ldexp(-3., -30), 0);
    Result("gps_iono_alpha1", nav[0].iono.alphaGps[1], ldexp(5., -27), 0);
    Result("gps_iono_alpha2_signed", nav[0].iono.alphaGps[2], ldexp(-7., -24), 0);
    Result("gps_iono_alpha3", nav[0].iono.alphaGps[3], ldexp(11., -24), 0);
    Result("gps_iono_beta0_s", nav[0].iono.beta[0], ldexp(67., 11), 0);
    Result("gps_iono_beta1_signed", nav[0].iono.beta[1], ldexp(-13., 14), 0);
    Result("gps_iono_beta2", nav[0].iono.beta[2], ldexp(17., 16), 0);
    Result("gps_iono_beta3_signed", nav[0].iono.beta[3], ldexp(-19., 16), 0);
}
int main(int argc, char **argv)
{
    int i, j;
    double before[3], after[3], velocity[3], error = 0;
    setvbuf(stdout, NULL, _IONBF, 0);
    puts("case,actual,reference,result");
    /* Same reception sample, distinct transmission times and buffer offsets. */
    Fixture(2, 0);
    nav[0].tow = nav[1].tow = 100000;
    gps[0].bitIndexTotal = 50;
    gps[1].bitIndexTotal = 49;
    channels[0].bufferOffset = 8184;
    channels[1].bufferOffset = 0;
    ComputeTransmissionTime(&cfg, &rx.pvtData);
    Result("common_rx_alignment_s",
           observations[0].transmissionTime - observations[1].transmissionTime,
           .020 - 8184 / cfg.fs, 1e-10);
    /* Valid transmit times straddle a week boundary, only 20 ms apart. */
    Fixture(2, 0);
    rx.pvtData.minNumOfChannels = 2;
    observations[0].wn = 100;
    observations[0].transmissionTime = .01;
    observations[1].wn = 99;
    observations[1].transmissionTime = 604799.99;
    VerifyTransmissionTime(&rx.pvtData);
    Result("week_boundary_channels_kept", rx.pvtData.numOfOperativeChannels, 2, 0);
    Fixture(5, 0);
    channels[4].satType = GALILEO_SAT;
    rx.pvtData.numOfGpsOpChannels = 4;
    rx.pvtData.numOfGalileoOpChannels = 1;
    rx.pvtData.minNumOfChannels = 5;
    cfg.pvtConfiguration.elevationMaskFlag = 1;
    cfg.pvtConfiguration.elevationMask = .1f;
    for (i = 0; i < 5; i++) {
        satellites[i].satInformationValid = 1;
        satellites[i].elevation = i == 4 ? 0 : .6;
    }
    Result("four_gps_after_last_galileo_masked_usable", ApplyElevationMask(&cfg, &rx.pvtData),
           TRUE_FLAG, 0);
    /* ICD requires reconstructing WN0G within 31 weeks of current GST week. */
    Fixture(1, 0);
    rx.galNavigationMessage[0].gal2GpsReceived = 1;
    rx.galNavigationMessage[0].gal2Gps.wn0G = 63;
    rx.galNavigationMessage[0].gal2Gps.a1G = 1e-13;
    {
        double offset;
        GetGstToGpsTimeOffset(&rx, 64, 0, &offset);
        Result("ggto_week64_reference63_s", offset, 604800e-13, 1e-15);
    }
    /* Orbit derivative must equal a central difference of the position model. */
    Fixture(1, 0);
    ComputeSatsPositions(&cfg, &rx, &rx.pvtData, &rx.pvtTemporaryResults);
    memcpy(velocity, satellites[0].ecefSatVel, sizeof(velocity));
    observations[0].transmissionTime -= .1;
    ComputeSatsPositions(&cfg, &rx, &rx.pvtData, &rx.pvtTemporaryResults);
    memcpy(before, satellites[0].ecefSatPos, sizeof(before));
    observations[0].transmissionTime += .2;
    ComputeSatsPositions(&cfg, &rx, &rx.pvtData, &rx.pvtTemporaryResults);
    memcpy(after, satellites[0].ecefSatPos, sizeof(after));
    for (i = 0; i < 3; i++)
        error += pow(velocity[i] - (after[i] - before[i]) / .2, 2);
    Result("sat_velocity_derivative_error_mps", sqrt(error), 0, 1e-4);
    for (j = 0; j < 2; j++) {
        double ref[3], norm = 0;
        channels[0].satType = j ? GALILEO_SAT : GPS_SAT;
        OrbitReference(&nav[0].eph, observations[0].transmissionTime,
                       j ? 3.986004418e14 : 3.986005e14, ref);
        ComputeSatsPositions(&cfg, &rx, &rx.pvtData, &rx.pvtTemporaryResults);
        for (i = 0; i < 3; i++)
            norm += pow(satellites[0].ecefSatPos[i] - ref[i], 2);
        Result(j ? "galileo_icd_orbit_error_m" : "gps_icd_orbit_error_m", sqrt(norm), 0, 1e-4);
    }
    /* Perigee is a valid epoch, not a singular orbit. */
    nav[0].eph.meanAnomaly0 = 0;
    observations[0].transmissionTime = nav[0].eph.toe;
    ComputeSatsPositions(&cfg, &rx, &rx.pvtData, &rx.pvtTemporaryResults);
    Result("perigee_velocity_finite", isfinite(satellites[0].ecefSatVel[0]), 1, 0);
    /* Ranges identical in geometry, only clock reference differs. */
    Fixture(2, 0);
    rx.pvtData.rxGpsTimeValid = 1;
    rx.pvtData.rxGpsWn = 2400;
    rx.pvtData.rxGpsTow = 100000.08;
    observations[0].wn = observations[1].wn = 2400;
    observations[0].transmissionTime = 100000.01;
    observations[1].transmissionTime = 100000;
    ComputePseudoranges(&cfg, &rx, &rx.pvtData, &rx.pvtTemporaryResults);
    Result("pseudorange_difference_m",
           observations[1].pseudorangeRaw - observations[0].pseudorangeRaw, .01 * SPEED_OF_LIGHT,
           .02);
    /* The single-constellation KF must not wipe smoothing every epoch. */
    Fixture(8, 0);
    cfg.pvtConfiguration.csmFilterLen = 32;
    rx.pvtData.pvtAlgorithm = KALMAN_BASED_PVT;
    rx.pvtData.minNumOfChannels = 0;
    for (i = 0; i < 8; i++) {
        smoothing[i].csmCounter = 20;
        observations[i].pseudorange = 2.2e7;
    }
    PerformCarrierSmoothing(&cfg, &rx.pvtData, &rx.pvtTemporaryResults);
    Result("single_kf_smoothing_counter", smoothing[0].csmCounter, 21, 0);
    DeleteNotSmoothed(&cfg, &rx.pvtData);
    Result("single_kf_measurements_after_csm", rx.pvtData.numOfOperativeChannels, 8, 0);
    /* In zero-measurement prediction, single covariance must be propagated
     * back to the multi-state bank used on the next epoch. */
    Fixture(0, 0);
    InitKalmanFilterData(&cfg, &rx);
    rx.pvtTemporaryResults.clockBiasGpsValid = 1;
    PerformKalmanFilter(&cfg, &rx);
    Result("kf_outage_covariance_bank_x", rx.pvtData.kfData.pPostMulti[0], 110 + 1e3 * (KF_SP / 3),
           1e-8);
    DeinitKalmanFilterData(&rx);
    /* A constant-velocity moving user and noiseless consistent measurements
     * should not produce spurious corrections in the error-state filter. */
    Fixture(8, 1);
    InitKalmanFilterData(&cfg, &rx);
    rx.pvtTemporaryResults.ecefPosition[0] = 4.5e6;
    rx.pvtTemporaryResults.ecefPosition[2] = 4.5e6;
    rx.pvtTemporaryResults.ecefVelocity[0] = 10;
    rx.pvtTemporaryResults.clockBiasGpsValid = rx.pvtTemporaryResults.clockBiasGalileoValid = 1;
    for (i = 0; i < 8; i++) {
        double dir[3] = {cos(i * 2.4) * .7, sin(i * 2.4) * .7, sqrt(.51)},
               v[3] = {-1000, 1500, 2000};
        double range = 2.2e7, angle = range / SPEED_OF_LIGHT * WEDOT, rot[3], radial = 0;
        double p[3] = {4.5e6 + 10, 0, 4.5e6};
        for (j = 0; j < 3; j++) {
            rot[j] = p[j] + range * dir[j];
            satellites[i].ecefSatVel[j] = v[j];
        }
        radial = (cos(angle) * v[0] + sin(angle) * v[1] - 10) * dir[0] +
                 (-sin(angle) * v[0] + cos(angle) * v[1]) * dir[1] + v[2] * dir[2];
        satellites[i].ecefSatPos[0] = rot[0] * cos(angle) - rot[1] * sin(angle);
        satellites[i].ecefSatPos[1] = rot[0] * sin(angle) + rot[1] * cos(angle);
        satellites[i].ecefSatPos[2] = rot[2];
        satellites[i].elevation = .7;
        observations[i].pseudorangeCsm = range;
        observations[i].deltaPseudorange = -radial / LAMBDA;
        observations[i].posSigma2 = 10;
        observations[i].velSigma2 = 1;
    }
    PerformKalmanFilter(&cfg, &rx);
    Result("kf_zero_innovation_position_m", rx.pvtTemporaryResults.ecefPosition[0], 4.5e6 + 10,
           1e-6);
    DeinitKalmanFilterData(&rx);
    PerfectKf(0);
    PerformKalmanFilter(&cfg, &rx);
    memcpy(before, rx.pvtTemporaryResults.ecefPosition, sizeof(before));
    DeinitKalmanFilterData(&rx);
    PerfectKf(1e6);
    PerformKalmanFilter(&cfg, &rx);
    error = 0;
    for (i = 0; i < 3; i++)
        error += pow(rx.pvtTemporaryResults.ecefPosition[i] - before[i], 2);
    Result("kf_clock_reference_invariance_m", sqrt(error), 0, 1e-6);
    DeinitKalmanFilterData(&rx);
    CheckLeastSquares(0, 0);
    CheckLeastSquares(1, 0);
    CheckLeastSquares(0, 1);
    CheckLeastSquares(1, 1);
    CheckNavigationFields();
    {
        TPvtChannel saved[8];
        int stage, failed = 0;
        PerfectKf(0);
        memcpy(saved, observations, sizeof(saved));
        for (stage = 0; stage < 5; stage++) {
            if (stage == 0 || stage == 4) {
                memcpy(observations, saved, sizeof(saved));
                rx.pvtData.numOfOperativeChannels = 8;
                rx.pvtData.numOfGpsOpChannels = rx.pvtData.numOfGalileoOpChannels = 4;
            } else {
                memcpy(observations, saved + (stage == 2 ? 4 : 0), 4 * sizeof(saved[0]));
                rx.pvtData.numOfOperativeChannels = stage == 3 ? 3 : 4;
                rx.pvtData.numOfGpsOpChannels = stage == 2 ? 0 : rx.pvtData.numOfOperativeChannels;
                rx.pvtData.numOfGalileoOpChannels = stage == 2 ? 4 : 0;
            }
            if (PerformKalmanFilter(&cfg, &rx) != SUCCESS_RETURN)
                failed++;
        }
        Result("kf_constellation_transition_failures", failed, 0, 0);
        rx.pvtData.numOfOperativeChannels = rx.pvtData.numOfGpsOpChannels =
            rx.pvtData.numOfGalileoOpChannels = 0;
        failed = 0;
        for (stage = 0; stage < 30; stage++)
            if (PerformKalmanFilter(&cfg, &rx) != SUCCESS_RETURN) {
                failed = 1;
                break;
            }
        Result("kf_unbounded_prediction_rejected", failed, 1, 0);
        DeinitKalmanFilterData(&rx);
    }
    PerfectKf(0);
    observations[0].posSigma2 = 0;
    Result("kf_invalid_noise_rejected", PerformKalmanFilter(&cfg, &rx), FAILURE_RETURN, 0);
    DeinitKalmanFilterData(&rx);
    PerfectKf(0);
    cfg.pvtConfiguration.csmFilterLen = 32;
    smoothing[0].csmCounter = 20;
    ResetPvt(&cfg, &rx.pvtData, &rx.pvtTemporaryResults, FALSE_FLAG);
    Result("soft_pvt_reset_preserves_smoothing", smoothing[0].csmCounter, 20, 0);
    rx.pvtTemporaryResults.clockBiasGalileoValid = TRUE_FLAG;
    channels[0].chState = TRACKING_STATE;
    nav[0].ephemeridesReceived = TRUE_FLAG;
    ResetPvt(&cfg, &rx.pvtData, &rx.pvtTemporaryResults, TRUE_FLAG);
    Result("hard_pvt_reset_clears_galileo_validity", rx.pvtTemporaryResults.clockBiasGalileoValid,
           FALSE_FLAG, 0);
    Result("pnt_reset_preserves_rf_tracking", channels[0].chState, TRACKING_STATE, 0);
    Result("pnt_reset_preserves_ephemerides", nav[0].ephemeridesReceived, TRUE_FLAG, 0);
    DeinitKalmanFilterData(&rx);
    {
        int failure;
        PerfectKf(0);
        cfg.pvtConfiguration.maxGdop = 100;
        cfg.pvtConfiguration.staticReceiver = TRUE_FLAG;
        rx.pvtData.pvtAlgorithm = LMS_BASED_PVT;
        rx.pvtResults = rx.pvtTemporaryResults;
        rx.pvtResults.pvtValid = TRUE_FLAG;
        rx.pvtTemporaryResults.dopValid = rx.pvtResults.dopValid = TRUE_FLAG;
        for (i = 0; i < 5; i++)
            rx.pvtTemporaryResults.dop[i] = rx.pvtResults.dop[i] = 1;
        rx.pvtTemporaryResults.geodPosition[0] = 7;
        rx.pvtTemporaryResults.geodPosition[1] = 45;
        rx.pvtTemporaryResults.geodPosition[2] = 311;
        rx.pvtTemporaryResults.ecefPosition[0] += 200;
        Result("pnt_large_jump_rejected",
               CheckPvtResultsGoodness(&cfg, &rx.pvtData, &rx.pvtTemporaryResults, &rx.pvtResults,
                                       &failure),
               FALSE_FLAG, 0);
        Result("pnt_large_jump_is_failure", failure, TRUE_FLAG, 0);
        ResetPvt(&cfg, &rx.pvtData, &rx.pvtResults, TRUE_FLAG);
        Result("pnt_reset_releases_bad_position_reference",
               CheckPvtResultsGoodness(&cfg, &rx.pvtData, &rx.pvtTemporaryResults, &rx.pvtResults,
                                       &failure),
               TRUE_FLAG, 0);
        rx.pvtTemporaryResults.geodPosition[2] = 1e7;
        Result("pnt_reset_does_not_bypass_sanity",
               CheckPvtResultsGoodness(&cfg, &rx.pvtData, &rx.pvtTemporaryResults, &rx.pvtResults,
                                       &failure),
               FALSE_FLAG, 0);
        DeinitKalmanFilterData(&rx);
    }
    Fixture(8, 1);
    rx.pvtData.numOfGalileoOpChannels = 0;
    rx.pvtTemporaryResults.clockBiasGps = 1;
    rx.pvtTemporaryResults.clockBiasGalileo = 2;
    UpdateCumulatedBiases(&rx.pvtTemporaryResults, &rx.pvtData);
    Result("unobserved_clock_origin_not_accumulated",
           rx.pvtTemporaryResults.clockBiasGalileoCumulated, 0, 0);
    PerfectKf(0);
    for (i = 0; i < 8; i++) {
        observations[i].satClkDrift = (i - 3) * 1e-11;
        observations[i].deltaPseudorange += SPEED_OF_LIGHT * observations[i].satClkDrift / LAMBDA;
    }
    PerformKalmanFilter(&cfg, &rx);
    Result(
        "kf_satellite_clock_rate_speed_mps",
        hypot(hypot(rx.pvtTemporaryResults.ecefVelocity[0], rx.pvtTemporaryResults.ecefVelocity[1]),
              rx.pvtTemporaryResults.ecefVelocity[2]),
        0, 1e-8);
    DeinitKalmanFilterData(&rx);
    return argc > 1 && strcmp(argv[1], "--verify") == 0 && mismatches ? 1 : 0;
}
