/**
 * @file kalmanFilter.c
 *
 * @author Mario Nicola
 *
 * @date 3/11/2014
 *
 * @version 1.0.0
 *
 * @brief Kalman filter functions.
 */
#include <stdlib.h>
#include <math.h>
#include "ngeneDataStructure.h"
#include "ngeneConfiguration.h"
#include "pvtDataStructure.h"
#include "ngeneConstants.h"
#include "ioFunctions.h"
#include "closeManager.h"
#include "matrices.h"
#include "pvtUtility.h"
#include "pntNumerics.h"

/* These are estimator limits, not static/dynamic receiver modes. The lower
 * density retains manoeuvre observability; loss of Doppler geometry restores
 * the original conservative process density. No ground truth enters them. */
#define KF_MOTION_MIN_PSD 1e-4
#define KF_MOTION_MAX_PSD 1e3
#define KF_MOTION_MEMORY_SECONDS 5.0
#define KF_MOTION_NIS_LIMIT 16.26623619623813 /* chi-square(3), 99.9% */

void ResetKalmanMotionData(TKalmanMotionData *motion)
{
    int i;
    motion->accelerationPsd = 1e2 * KF_SP;
    motion->dopplerNoiseScale = 1;
    motion->referenceSample = 0;
    motion->referenceValid = FALSE_FLAG;
    for (i = 0; i < 3; i++)
        motion->dopplerVelocity[i] = 0;
    for (i = 0; i < 9; i++)
        motion->dopplerVelocityCovariance[i] = 0;
}

static void ApplyKalmanMotionNoise(TKalmanFilterData *kfData, double accelerationPsd)
{
    double ratio = accelerationPsd / (1e2 * KF_SP);
    int mode, n, axis;
    for (mode = 0; mode < 2; mode++) {
        double *q = mode ? kfData->qMulti : kfData->q;
        n = mode ? KF_MULTI_STATES : KF_SINGLE_STATES;
        for (axis = 0; axis < 3; axis++) {
            q[axis * n + axis] *= ratio;
            q[(axis + 3) * n + axis + 3] *= ratio;
            q[axis * n + axis + 3] *= ratio;
            q[(axis + 3) * n + axis] *= ratio;
        }
    }
}

static void AdaptKalmanMotion(TKalmanFilterData *kfData, TKalmanMotionData *motion, int nCh,
                              int nStates, const double *pap, const double *nominalVelocity,
                              double dt, unsigned long long sample, double fs)
{
    double correction[3], covariance[9], scatter, velocity[3];
    double innovationCovariance[9], lower[9], normalized[3];
    double power = 0, noise = 0, interval, raw, alpha, estimate, nis = 0, rateScale;
    int i, j, nMeasures = 2 * nCh;
    if (!isfinite(motion->accelerationPsd) || motion->accelerationPsd < KF_MOTION_MIN_PSD ||
        motion->accelerationPsd > KF_MOTION_MAX_PSD || !isfinite(motion->dopplerNoiseScale) ||
        motion->dopplerNoiseScale < .01)
        ResetKalmanMotionData(motion);
    if (nCh < 6 || !(fs > 0) ||
        !PntDopplerFit(nCh, nStates, kfData->h + nCh * nStates, kfData->r + nCh * (nMeasures + 1),
                       nMeasures, kfData->kfMeasurement + nCh, correction, covariance, &scatter)) {
        motion->accelerationPsd = fmax(motion->accelerationPsd, 1e2 * KF_SP);
        motion->dopplerNoiseScale = fmax(motion->dopplerNoiseScale, 1.0);
        motion->referenceValid = FALSE_FLAG;
        return;
    }
    /* The redundant fit removes velocity and common drift before estimating
     * noise. Learn its sustained level, but inflate immediately on an unusually
     * noisy epoch. A grossly inconsistent epoch cannot train either estimator. */
    if (scatter <= 25.0) {
        alpha = -expm1(-dt / KF_MOTION_MEMORY_SECONDS);
        motion->dopplerNoiseScale =
            fmax(.01, motion->dopplerNoiseScale + alpha * (scatter - motion->dopplerNoiseScale));
    }
    rateScale = fmin(1e6, fmax(motion->dopplerNoiseScale, scatter));
    for (i = 0; i < nCh; i++)
        kfData->r[(nCh + i) * (nMeasures + 1)] *= rateScale;
    for (i = 0; i < 9; i++)
        covariance[i] *= rateScale / fmax(.01, scatter);
    if (scatter > 25.0) {
        motion->referenceValid = FALSE_FLAG;
        return;
    }
    for (i = 0; i < 3; i++) {
        velocity[i] = nominalVelocity[i] + correction[i];
        for (j = 0; j < 3; j++)
            innovationCovariance[i * 3 + j] =
                pap[(i + 3) * nStates + j + 3] + covariance[i * 3 + j];
    }
    estimate = motion->accelerationPsd;
    if (motion->referenceValid && sample > motion->referenceSample) {
        interval = (double)(sample - motion->referenceSample) / fs;
        if (interval <= 1.5 * dt) {
            for (i = 0; i < 3; i++) {
                power += (velocity[i] - motion->dopplerVelocity[i]) *
                         (velocity[i] - motion->dopplerVelocity[i]);
                noise += covariance[i * 3 + i] + motion->dopplerVelocityCovariance[i * 3 + i];
            }
            /* Covariance matching uses signed excess power. Clipping each
             * individual squared difference first would bias stationary Q up. */
            raw = (power - noise) / (3.0 * interval);
            alpha = -expm1(-interval / KF_MOTION_MEMORY_SECONDS);
            estimate += alpha * (raw - estimate);
        }
    }
    /* A statistically significant, geometrically coherent velocity innovation
     * opens Q before this epoch's update, so a quiet history cannot lock out
     * the start of motion. The common Doppler clock term was fitted separately. */
    if (PntSpdFactor(innovationCovariance, 3, lower) &&
        PntSpdSolve(lower, 3, correction, normalized)) {
        power = noise = 0;
        for (i = 0; i < 3; i++) {
            nis += correction[i] * normalized[i];
            power += correction[i] * correction[i];
            noise += innovationCovariance[i * 3 + i];
        }
        if (nis > KF_MOTION_NIS_LIMIT)
            estimate = fmax(estimate, (power - noise) / (3.0 * dt));
    }
    motion->accelerationPsd = fmax(KF_MOTION_MIN_PSD, fmin(KF_MOTION_MAX_PSD, estimate));
    motion->referenceSample = sample;
    motion->referenceValid = TRUE_FLAG;
    for (i = 0; i < 3; i++)
        motion->dopplerVelocity[i] = velocity[i];
    for (i = 0; i < 9; i++)
        motion->dopplerVelocityCovariance[i] = covariance[i];
}

void InitKalmanFilterPhi(double *phi, double *phiMulti, double dt)
{
    int idx;
    /* Initialize phi matrices for Kalman filter */
    MatrixInit(phi, KF_SINGLE_STATES, KF_SINGLE_STATES);
    for (idx = 0; idx < KF_SINGLE_STATES; idx++) {
        phi[(idx * KF_SINGLE_STATES) + idx] = 1.0;
        if (idx < 3) {
            phi[(idx * KF_SINGLE_STATES) + idx + 3] = dt;
        }
        if (idx == 6) {
            phi[(idx * KF_SINGLE_STATES) + idx + 1] = dt;
        }
    }
    MatrixInit(phiMulti, KF_MULTI_STATES, KF_MULTI_STATES);
    for (idx = 0; idx < KF_MULTI_STATES; idx++) {
        phiMulti[(idx * KF_MULTI_STATES) + idx] = 1.0;
        if (idx < 3) {
            phiMulti[(idx * KF_MULTI_STATES) + idx + 3] = dt;
        }
        if (idx == 6) {
            phiMulti[(idx * KF_MULTI_STATES) + idx + 1] = dt;
        }
        if (idx == 8) {
            phiMulti[(idx * KF_MULTI_STATES) + idx - 1] = dt;
        }
    }
}

void InitKalmanFilterPostCovarianceMatrices(double *pPost, double *pPostMulti)
{
    /* Initialize  P a posteriori covariance matrices for Kalman filter */
    MatrixInit(pPost, KF_SINGLE_STATES, KF_SINGLE_STATES);
    pPost[0] = 100.0;
    pPost[9] = 100.0;
    pPost[18] = 100.0;
    pPost[27] = 10.0;
    pPost[36] = 10.0;
    pPost[45] = 10.0;
    pPost[54] = 1000.0;
    pPost[63] = 1.0;
    MatrixInit(pPostMulti, KF_MULTI_STATES, KF_MULTI_STATES);
    pPostMulti[0] = 100.0;
    pPostMulti[10] = 100.0;
    pPostMulti[20] = 100.0;
    pPostMulti[30] = 10.0;
    pPostMulti[40] = 10.0;
    pPostMulti[50] = 10.0;
    pPostMulti[60] = 1000.0;
    pPostMulti[70] = 1.0;
    pPostMulti[80] = 1000.0;
}

void InitQStateNoiseCovarianceMatrices(double *q, double *qMulti, double dt)
{
    /* Continuous white acceleration and oscillator noise. Keep the effective
     * velocity/drift spectral densities of the previous one-second model;
     * integrate every covariance term using the same density. */
    const double acceleration = 1e2 * KF_SP;
    const double phase = 1e2 * KF_SF;
    const double frequency = 1e2 * KF_SG;
    const double dt2 = dt * dt, dt3 = dt2 * dt;
    double *noise;
    int mode, n, axis;
    MatrixInit(q, KF_SINGLE_STATES, KF_SINGLE_STATES);
    MatrixInit(qMulti, KF_MULTI_STATES, KF_MULTI_STATES);
    for (mode = 0; mode < 2; mode++) {
        n = mode ? KF_MULTI_STATES : KF_SINGLE_STATES;
        noise = mode ? qMulti : q;
        for (axis = 0; axis < 3; axis++) {
            noise[axis * n + axis] = acceleration * dt3 / 3.0;
            noise[(axis + 3) * n + axis + 3] = acceleration * dt;
            noise[axis * n + axis + 3] = noise[(axis + 3) * n + axis] = acceleration * dt2 / 2.0;
        }
        noise[6 * n + 6] = phase * dt + frequency * dt3 / 3.0;
        noise[7 * n + 7] = frequency * dt;
        noise[6 * n + 7] = noise[7 * n + 6] = frequency * dt2 / 2.0;
        if (mode) {
            noise[8 * n + 8] = noise[6 * n + 6];
            noise[8 * n + 7] = noise[7 * n + 8] = frequency * dt2 / 2.0;
            /* Both constellation biases integrate the same oscillator drift. */
            noise[6 * n + 8] = noise[8 * n + 6] = frequency * dt3 / 3.0;
        }
    }
}

int InitKalmanFilterData(TNGeneConfiguration *cfg, TNGeneData *rx)
{
    int nCh = cfg->numberOfChannels;
    int nCh2 = 2 * nCh;
    TKalmanFilterData *kfData = &(rx->pvtData.kfData);
    int errors = 0;
    ResetKalmanMotionData(&kfData->motion);
    kfData->aaa = (double *)malloc(1 * nCh * sizeof(double));
    if (kfData->aaa == NULL)
        errors++;
    kfData->az = (double *)malloc(1 * nCh * sizeof(double));
    if (kfData->az == NULL)
        errors++;
    kfData->el = (double *)malloc(1 * nCh * sizeof(double));
    if (kfData->el == NULL)
        errors++;
    kfData->r = (double *)malloc(nCh2 * nCh2 * sizeof(double));
    if (kfData->r == NULL)
        errors++;
    kfData->range = (double *)malloc(nCh * 1 * sizeof(double));
    if (kfData->range == NULL)
        errors++;
    kfData->deltaRange = (double *)malloc(nCh * 1 * sizeof(double));
    if (kfData->deltaRange == NULL)
        errors++;
    kfData->u = (double *)malloc(nCh * KF_MULTI_SIZE * sizeof(double));
    if (kfData->u == NULL)
        errors++;
    kfData->pr = (double *)malloc(nCh * 1 * sizeof(double));
    if (kfData->pr == NULL)
        errors++;
    kfData->prCorr = (double *)malloc(nCh * 1 * sizeof(double));
    if (kfData->prCorr == NULL)
        errors++;
    kfData->deltaPrCorr = (double *)malloc(nCh * 1 * sizeof(double));
    if (kfData->deltaPrCorr == NULL)
        errors++;
    kfData->zCurr = (double *)malloc(nCh2 * 1 * sizeof(double));
    if (kfData->zCurr == NULL)
        errors++;
    kfData->nominalMeasurement = (double *)malloc(nCh2 * 1 * sizeof(double));
    if (kfData->nominalMeasurement == NULL)
        errors++;
    kfData->kfMeasurement = (double *)malloc(nCh2 * 1 * sizeof(double));
    if (kfData->kfMeasurement == NULL)
        errors++;
    kfData->h = (double *)malloc(nCh2 * KF_MULTI_STATES * sizeof(double));
    if (kfData->h == NULL)
        errors++;
    if (errors > 0) {
        SwrxLog(NGENE_LOG_SEVERITY_ERROR, "Malloc returned a NULL pointer in InitKalmanFilterData");
        CloseNGene(cfg, EXIT_FAILURE);
    }
    InitKalmanFilterPhi(kfData->phi, kfData->phiMulti, cfg->pvtPeriodSeconds);
    InitKalmanFilterPostCovarianceMatrices(kfData->pPost, kfData->pPostMulti);
    InitQStateNoiseCovarianceMatrices(kfData->q, kfData->qMulti, cfg->pvtPeriodSeconds);
    return SUCCESS_RETURN;
}

void ClearKalmanFilterData(TNGeneConfiguration *cfg, TKalmanFilterData *kfData)
{
    int n = cfg->numberOfChannels;
    int n2 = 2 * n;
    MatrixInit(kfData->aaa, 1, n);
    MatrixInit(kfData->az, 1, n);
    MatrixInit(kfData->el, 1, n);
    MatrixInit(kfData->r, n2, n2);
    MatrixInit(kfData->range, n, 1);
    MatrixInit(kfData->deltaRange, n, 1);
    MatrixInit(kfData->u, n, KF_MULTI_SIZE);
    MatrixInit(kfData->pr, n, 1);
    MatrixInit(kfData->prCorr, n, 1);
    MatrixInit(kfData->deltaPrCorr, n, 1);
    MatrixInit(kfData->zCurr, n2, 1);
    MatrixInit(kfData->nominalMeasurement, n2, 1);
    MatrixInit(kfData->kfMeasurement, n2, 1);
    MatrixInit(kfData->h, n2, KF_MULTI_STATES);
    MatrixInit(kfData->stateAPosteriori, KF_MULTI_STATES, 1);
    MatrixInit(kfData->xPred, KF_MULTI_STATES, 1);
}

void DeinitKalmanFilterData(TNGeneData *rx)
{
    TKalmanFilterData *kfData = &(rx->pvtData.kfData);
    free(kfData->aaa);
    free(kfData->az);
    free(kfData->el);
    free(kfData->r);
    free(kfData->range);
    free(kfData->deltaRange);
    free(kfData->u);
    free(kfData->pr);
    free(kfData->prCorr);
    free(kfData->deltaPrCorr);
    free(kfData->zCurr);
    free(kfData->nominalMeasurement);
    free(kfData->kfMeasurement);
    free(kfData->h);
}

int KalmanFilterPrediction(int nStates, TKalmanFilterData *kfData, double *phi, double *q,
                           double *pap)
{
    return PntKalmanStep(nStates, 0, phi, q, NULL, NULL, NULL, kfData->stateAPosteriori, pap,
                         kfData->xPred)
               ? SUCCESS_RETURN
               : FAILURE_RETURN;
}

int KalmanFilterRoutine(int nCh, int nStates, TKalmanFilterData *kfData, double *pap, double *phi,
                        double *q)
{
    return PntKalmanStep(nStates, 2 * nCh, phi, q, kfData->h, kfData->r, kfData->kfMeasurement,
                         kfData->stateAPosteriori, pap, kfData->xPred)
               ? SUCCESS_RETURN
               : FAILURE_RETURN;
}

void PreparePPost(TKalmanFilterData *kfData, TSatConstellation satType)
{
    int rowIdx;
    int colIdx;
    int idx;
    if (satType == GPS_SAT) {
        /* updates all the variables */
        for (rowIdx = 0; rowIdx < KF_SINGLE_STATES; rowIdx++) {
            for (colIdx = 0; colIdx < KF_SINGLE_STATES; colIdx++) {
                kfData->pPost[(rowIdx * KF_SINGLE_STATES) + colIdx] =
                    kfData->pPostMulti[(rowIdx * KF_MULTI_STATES) + colIdx];
            }
        }
    } else /* if(satType==GALILEO_SAT) */
    {
        /* updates position and velocity variables */
        for (rowIdx = 0; rowIdx < 6; rowIdx++) {
            for (colIdx = 0; colIdx < 6; colIdx++) {
                kfData->pPost[(rowIdx * KF_SINGLE_STATES) + colIdx] =
                    kfData->pPostMulti[(rowIdx * KF_MULTI_STATES) + colIdx];
            }
        }
        /* update clock drift variables */
        for (idx = 0; idx < 6; idx++) {
            kfData->pPost[(idx * KF_SINGLE_STATES) + 7] =
                kfData->pPostMulti[(idx * KF_MULTI_STATES) + 7];
            kfData->pPost[(7 * KF_SINGLE_STATES) + idx] =
                kfData->pPostMulti[(7 * KF_MULTI_STATES) + idx];
        }
        kfData->pPost[(7 * KF_SINGLE_STATES) + 7] = kfData->pPostMulti[(7 * KF_MULTI_STATES) + 7];
        /* update Galileo clock bias variables */
        for (idx = 0; idx < 6; idx++) {
            kfData->pPost[(idx * KF_SINGLE_STATES) + 6] =
                kfData->pPostMulti[(idx * KF_MULTI_STATES) + 8];
            kfData->pPost[(6 * KF_SINGLE_STATES) + idx] =
                kfData->pPostMulti[(8 * KF_MULTI_STATES) + idx];
        }
        kfData->pPost[(6 * KF_SINGLE_STATES) + 6] = kfData->pPostMulti[(8 * KF_MULTI_STATES) + 8];
        kfData->pPost[(7 * KF_SINGLE_STATES) + 6] = kfData->pPostMulti[(8 * KF_MULTI_STATES) + 7];
        kfData->pPost[(6 * KF_SINGLE_STATES) + 7] = kfData->pPostMulti[(7 * KF_MULTI_STATES) + 8];
    }
}

/**
 * @brief After a Single constellation PVT has been performed, the pPost matrix is updated.
 * This function updates the pPostMulti matrix accordingly
 */

void UpdatePPostMulti(TKalmanFilterData *kfData, TSatConstellation satType)
{
    int rowIdx;
    int colIdx;
    int idx;
    if (satType == GPS_SAT) {
        /* updates all the variables */
        for (rowIdx = 0; rowIdx < KF_SINGLE_STATES; rowIdx++) {
            for (colIdx = 0; colIdx < KF_SINGLE_STATES; colIdx++) {
                kfData->pPostMulti[(rowIdx * KF_MULTI_STATES) + colIdx] =
                    kfData->pPost[(rowIdx * KF_SINGLE_STATES) + colIdx];
            }
        }
        /* Galileo clock bias variables are maintained */
    } else /* if(satType==GALILEO_SAT) */
    {
        /* updates position and velocity variables */
        for (rowIdx = 0; rowIdx < 6; rowIdx++) {
            for (colIdx = 0; colIdx < 6; colIdx++) {
                kfData->pPostMulti[(rowIdx * KF_MULTI_STATES) + colIdx] =
                    kfData->pPost[(rowIdx * KF_SINGLE_STATES) + colIdx];
            }
        }
        /* update clock drift variables */
        for (idx = 0; idx < 6; idx++) {
            kfData->pPostMulti[(idx * KF_MULTI_STATES) + 7] =
                kfData->pPost[(idx * KF_SINGLE_STATES) + 7];
            kfData->pPostMulti[(7 * KF_MULTI_STATES) + idx] =
                kfData->pPost[(7 * KF_SINGLE_STATES) + idx];
        }
        kfData->pPostMulti[(7 * KF_MULTI_STATES) + 7] = kfData->pPost[(7 * KF_SINGLE_STATES) + 7];
        /* update Galileo clock bias variables */
        for (idx = 0; idx < 6; idx++) {
            kfData->pPostMulti[(idx * KF_MULTI_STATES) + 8] =
                kfData->pPost[(idx * KF_SINGLE_STATES) + 6];
            kfData->pPostMulti[(8 * KF_MULTI_STATES) + idx] =
                kfData->pPost[(6 * KF_SINGLE_STATES) + idx];
        }
        kfData->pPostMulti[(8 * KF_MULTI_STATES) + 8] = kfData->pPost[(6 * KF_SINGLE_STATES) + 6];
        kfData->pPostMulti[(8 * KF_MULTI_STATES) + 7] = kfData->pPost[(7 * KF_SINGLE_STATES) + 6];
        kfData->pPostMulti[(7 * KF_MULTI_STATES) + 8] = kfData->pPost[(6 * KF_SINGLE_STATES) + 7];
        /* GPS clock bias variables are maintained */
    }
}

int PerformKalmanFilter(TNGeneConfiguration *cfg, TNGeneData *rx)
{
    TPvtData *pvtData = &(rx->pvtData);
    TKalmanFilterData *kfData = &(pvtData->kfData);
    TKalmanMotionData motion = kfData->motion;
    int nCh = pvtData->numOfOperativeChannels;
    int nCh2 = nCh * 2;
    TPvtChannel *pvtCh;
    TSatStatus *satStatus;
    TPvtResults *pvtRes = &(rx->pvtTemporaryResults);
    double dt = cfg->pvtPeriodSeconds;
    double positionAndVelocity[KF_MULTI_STATES];
    TSatConstellation satTypeForSingleConst = INVALID_SAT;
    int nStates;
    int size;
    int idxGpsBias;
    int idxGalBias;
    int idx;
    double *phi;
    double *q;
    double *pap;
    double dX, dY, dZ, dXv, dYv, dZv;
    double vs[3], angle, sinEl, covFactor[KF_MULTI_STATES * KF_MULTI_STATES];
    ClearKalmanFilterData(cfg, kfData);
    if (rx->pvtResults.pvtValid && cfg->fs > 0 &&
        rx->buf.discardedSamples > rx->pvtResults.referenceSample)
        dt = (double)(rx->buf.discardedSamples - rx->pvtResults.referenceSample) / cfg->fs;
    if (!isfinite(dt) || dt <= 0)
        return FAILURE_RETURN;
    InitKalmanFilterPhi(kfData->phi, kfData->phiMulti, dt);
    InitQStateNoiseCovarianceMatrices(kfData->q, kfData->qMulti, dt);
    /* Predict new position using last estimated velocity */
    positionAndVelocity[0] = pvtRes->ecefPosition[0] + (pvtRes->ecefVelocity[0] * dt);
    positionAndVelocity[1] = pvtRes->ecefPosition[1] + (pvtRes->ecefVelocity[1] * dt);
    positionAndVelocity[2] = pvtRes->ecefPosition[2] + (pvtRes->ecefVelocity[2] * dt);
    positionAndVelocity[3] = pvtRes->ecefVelocity[0];
    positionAndVelocity[4] = pvtRes->ecefVelocity[1];
    positionAndVelocity[5] = pvtRes->ecefVelocity[2];
    positionAndVelocity[6] = pvtRes->clockBiasGps;
    positionAndVelocity[7] = pvtRes->clockDrift;
    positionAndVelocity[8] = pvtRes->clockBiasGalileo;
    /* SetReferenceRxTime has already subtracted the last observed bias from
     * the time used to form this epoch's pseudoranges. Its nominal is now zero;
     * the posterior uncertainty remains in P. Unobserved clocks retain their
     * estimate until their own reference is refreshed. */
    if (rx->pvtResults.pvtValid) {
        if (pvtRes->rxGpsTime.referenceValid &&
            pvtRes->rxGpsTime.referenceSample == rx->pvtResults.referenceSample)
            positionAndVelocity[6] = 0;
        if (pvtRes->rxGalTime.referenceValid &&
            pvtRes->rxGalTime.referenceSample == rx->pvtResults.referenceSample)
            positionAndVelocity[8] = 0;
    }
    if (nCh == 0) {
        /* No measures are available, prediction is performed */
        if ((pvtRes->clockBiasGpsValid == TRUE_FLAG) &&
            (pvtRes->clockBiasGalileoValid == TRUE_FLAG)) {
            size = KF_MULTI_SIZE;
            nStates = KF_MULTI_STATES;
            phi = kfData->phiMulti;
            q = kfData->qMulti;
            pap = kfData->pPostMulti;
        } else {
            if (pvtRes->clockBiasGalileoValid == TRUE_FLAG) {
                satTypeForSingleConst = GALILEO_SAT;
            } else {
                satTypeForSingleConst = GPS_SAT;
            }
            PreparePPost(kfData, satTypeForSingleConst);
            size = KF_SINGLE_SIZE;
            nStates = KF_SINGLE_STATES;
            phi = kfData->phi;
            q = kfData->q;
            pap = kfData->pPost;
        }
        AdaptKalmanMotion(kfData, &motion, 0, nStates, pap, positionAndVelocity + 3, dt,
                          rx->buf.discardedSamples, cfg->fs);
        ApplyKalmanMotionNoise(kfData, motion.accelerationPsd);
        if (KalmanFilterPrediction(nStates, kfData, phi, q, pap) != SUCCESS_RETURN)
            return FAILURE_RETURN;
    } else {
        /* Uses the pseudorange and Doppler measurements */
        if (pvtRes->clockBiasGpsValid && pvtRes->clockBiasGalileoValid) {
            /* Multiple constellation */
            size = KF_MULTI_SIZE;
            nStates = KF_MULTI_STATES;
            idxGpsBias = 3;
            idxGalBias = 4;
            phi = kfData->phiMulti;
            q = kfData->qMulti;
            pap = kfData->pPostMulti;
        } else {
            /* Single constellation */
            if (pvtData->numOfGalileoOpChannels > 0) {
                satTypeForSingleConst = GALILEO_SAT;
            } else {
                satTypeForSingleConst = GPS_SAT;
            }
            PreparePPost(kfData, satTypeForSingleConst);
            size = KF_SINGLE_SIZE;
            nStates = KF_SINGLE_STATES;
            idxGpsBias = 3;
            idxGalBias = 3;
            phi = kfData->phi;
            q = kfData->q;
            pap = kfData->pPost;
        }
        for (idx = 0; idx < nCh; idx++) {
            pvtCh = &(pvtData->pvtChArray[idx]);
            satStatus = pvtCh->satStatus;
            kfData->range[idx] = RotateSatellitePosition(satStatus->ecefSatPos, positionAndVelocity,
                                                         satStatus->ecefSatPosRot);
            kfData->aaa[idx] = kfData->range[idx] / SPEED_OF_LIGHT * OMEGAE_DOT;
            kfData->el[idx] = pvtCh->satStatus->elevation;
            kfData->az[idx] = pvtCh->satStatus->azimuth;
            pvtCh->pseudorangeCsm +=
                (-pvtCh->tropoCorrection - pvtCh->ionoCorrection); /* Pseudo2True */
            if (!isfinite(kfData->el[idx]) || !isfinite(pvtCh->posSigma2) ||
                !isfinite(pvtCh->velSigma2) || pvtCh->posSigma2 <= 0 || pvtCh->velSigma2 <= 0)
                return FAILURE_RETURN;
            sinEl = fmax(1e-3, sin(kfData->el[idx]));
            kfData->r[idx * (nCh2 + 1)] = pvtCh->posSigma2 / sinEl;
            kfData->r[(idx + nCh) * (nCh2 + 1)] = pvtCh->velSigma2 / sinEl;
            dX = satStatus->ecefSatPosRot[0] - positionAndVelocity[0];
            dY = satStatus->ecefSatPosRot[1] - positionAndVelocity[1];
            dZ = satStatus->ecefSatPosRot[2] - positionAndVelocity[2];
            angle = kfData->aaa[idx];
            vs[0] = cos(angle) * satStatus->ecefSatVel[0] + sin(angle) * satStatus->ecefSatVel[1];
            vs[1] = -sin(angle) * satStatus->ecefSatVel[0] + cos(angle) * satStatus->ecefSatVel[1];
            vs[2] = satStatus->ecefSatVel[2];
            dXv = vs[0] - positionAndVelocity[3];
            dYv = vs[1] - positionAndVelocity[4];
            dZv = vs[2] - positionAndVelocity[5];
            kfData->range[idx] = sqrt(pow(dX, 2.0) + pow(dY, 2.0) + pow(dZ, 2.0));
            if (!isfinite(kfData->range[idx]) || kfData->range[idx] <= 0)
                return FAILURE_RETURN;
            kfData->deltaRange[idx] = ((dXv * dX) + (dYv * dY) + (dZv * dZ)) / kfData->range[idx];
            kfData->u[(idx * size) + 0] = dX / kfData->range[idx];
            kfData->u[(idx * size) + 1] = dY / kfData->range[idx];
            kfData->u[(idx * size) + 2] = dZ / kfData->range[idx];
            kfData->u[(idx * size) + idxGpsBias] =
                0.0; /* TODO dovrebbe già essere zero: verificare! */
            kfData->u[(idx * size) + idxGalBias] =
                0.0; /* TODO dovrebbe già essere zero: verificare! */
            if (pvtCh->ch->satType == GPS_SAT) {
                kfData->u[(idx * size) + idxGpsBias] = -1.0;
            } else /* if(pvtCh->ch->satType==GALILEO_SAT) */
            {
                kfData->u[(idx * size) + idxGalBias] = -1.0;
            }
            kfData->pr[idx] = pvtCh->pseudorangeCsm;
            if (pvtCh->ch->satType == GPS_SAT) {
                kfData->prCorr[idx] = kfData->pr[idx] - positionAndVelocity[6];
            } else /* if(pvtCh->ch->satType == GALILEO_SAT) */
            {
                kfData->prCorr[idx] = kfData->pr[idx] - positionAndVelocity[8];
            }
            kfData->deltaPrCorr[idx] = (-(pvtCh->deltaPseudorange) * LAMBDA) + pvtRes->clockDrift +
                                       SPEED_OF_LIGHT * pvtCh->satClkDrift;
            kfData->h[(idx * nStates) + 0] = -kfData->u[(idx * size) + 0];
            kfData->h[(idx * nStates) + 1] = -kfData->u[(idx * size) + 1];
            kfData->h[(idx * nStates) + 2] = -kfData->u[(idx * size) + 2];
            kfData->h[(idx * nStates) + 6] = -kfData->u[(idx * size) + 3];
            if (size == KF_MULTI_SIZE)
                kfData->h[(idx * nStates) + 8] = -kfData->u[(idx * size) + 4];
            kfData->h[(idx * nStates) + 0 + (nCh * nStates) + 3] = -kfData->u[(idx * size) + 0];
            kfData->h[(idx * nStates) + 1 + (nCh * nStates) + 3] = -kfData->u[(idx * size) + 1];
            kfData->h[(idx * nStates) + 2 + (nCh * nStates) + 3] = -kfData->u[(idx * size) + 2];
            kfData->h[(idx * nStates) + (nCh * nStates) + 7] = 1.0;
        }
        MergeMatricesVertically(kfData->prCorr, nCh, 1, kfData->deltaPrCorr, nCh, 1, kfData->zCurr);
        MergeMatricesVertically(kfData->range, nCh, 1, kfData->deltaRange, nCh, 1,
                                kfData->nominalMeasurement);
        MatrixDiff(kfData->zCurr, nCh2, 1, kfData->nominalMeasurement, nCh2, 1,
                   kfData->kfMeasurement);
        AdaptKalmanMotion(kfData, &motion, nCh, nStates, pap, positionAndVelocity + 3, dt,
                          rx->buf.discardedSamples, cfg->fs);
        ApplyKalmanMotionNoise(kfData, motion.accelerationPsd);
        if (KalmanFilterRoutine(nCh, nStates, kfData, pap, phi, q) != SUCCESS_RETURN)
            return FAILURE_RETURN;
        /* Correct PVT solution */
        positionAndVelocity[0] = positionAndVelocity[0] + kfData->stateAPosteriori[0];
        positionAndVelocity[1] = positionAndVelocity[1] + kfData->stateAPosteriori[1];
        positionAndVelocity[2] = positionAndVelocity[2] + kfData->stateAPosteriori[2];
        positionAndVelocity[3] = positionAndVelocity[3] + kfData->stateAPosteriori[3];
        positionAndVelocity[4] = positionAndVelocity[4] + kfData->stateAPosteriori[4];
        positionAndVelocity[5] = positionAndVelocity[5] + kfData->stateAPosteriori[5];
        positionAndVelocity[7] = positionAndVelocity[7] - kfData->stateAPosteriori[7];
        if (nStates == KF_MULTI_STATES) {
            positionAndVelocity[6] = positionAndVelocity[6] + kfData->stateAPosteriori[6];
            positionAndVelocity[8] = positionAndVelocity[8] + kfData->stateAPosteriori[8];
        } else if (pvtData->numOfGpsOpChannels > 0) {
            positionAndVelocity[6] = positionAndVelocity[6] + kfData->stateAPosteriori[6];
        } else if (pvtData->numOfGalileoOpChannels > 0) {
            positionAndVelocity[8] = positionAndVelocity[8] + kfData->stateAPosteriori[6];
        }
        /* PredictRxTime applies the newly estimated drift over the full age
         * of an unrefreshed clock reference. Express its nominal bias in that
         * updated time origin too. Observed clocks are refreshed by the manager
         * using the current pseudoranges and must retain their current bias. */
        if (pvtData->numOfGpsOpChannels == 0 && pvtRes->rxGpsTime.referenceValid &&
            rx->buf.discardedSamples >= pvtRes->rxGpsTime.referenceSample)
            positionAndVelocity[6] -=
                kfData->stateAPosteriori[7] *
                (double)(rx->buf.discardedSamples - pvtRes->rxGpsTime.referenceSample) / cfg->fs;
        if (pvtData->numOfGalileoOpChannels == 0 && pvtRes->rxGalTime.referenceValid &&
            rx->buf.discardedSamples >= pvtRes->rxGalTime.referenceSample)
            positionAndVelocity[8] -=
                kfData->stateAPosteriori[7] *
                (double)(rx->buf.discardedSamples - pvtRes->rxGalTime.referenceSample) / cfg->fs;
    }
    /* Update PPostMulti when single constellation PVT is performed,
     * or just one bias is predicted (outage) */
    if (size == KF_SINGLE_SIZE) {
        UpdatePPostMulti(kfData, satTypeForSingleConst);
    }
    /* Joseph covariance must remain finite, symmetric and positive definite.
     * Limit prediction uncertainty independently of instantaneous geometry. */
    if (!PntSpdFactor(pap, nStates, covFactor) || pap[0] + pap[nStates + 1] + pap[2 * nStates + 2] >
                                                      KF_MAX_POSITION_SIGMA * KF_MAX_POSITION_SIGMA)
        return FAILURE_RETURN;
    for (idx = 0; idx < KF_MULTI_STATES; idx++)
        if (!isfinite(positionAndVelocity[idx]))
            return FAILURE_RETURN;
    /* Save the results */
    pvtRes->ecefPosition[0] = positionAndVelocity[0];
    pvtRes->ecefPosition[1] = positionAndVelocity[1];
    pvtRes->ecefPosition[2] = positionAndVelocity[2];
    pvtRes->ecefVelocity[0] = positionAndVelocity[3];
    pvtRes->ecefVelocity[1] = positionAndVelocity[4];
    pvtRes->ecefVelocity[2] = positionAndVelocity[5];
    if (pvtRes->clockBiasGpsValid == TRUE_FLAG) {
        pvtRes->clockBiasGps = positionAndVelocity[6];
    }
    pvtRes->clockDrift = positionAndVelocity[7];
    if (pvtRes->clockBiasGalileoValid == TRUE_FLAG) {
        pvtRes->clockBiasGalileo = positionAndVelocity[8];
    }
    kfData->motion = motion;
    return SUCCESS_RETURN;
}
