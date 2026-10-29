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
    /* Initialize Q noise covariance matrices for Kalman filter */
    MatrixInit(q, KF_SINGLE_STATES, KF_SINGLE_STATES);
    q[0] = 1e3 * (KF_SP / 3) * pow(dt, 3.0);
    q[9] = 1e3 * (KF_SP / 3) * pow(dt, 3.0);
    q[18] = 1e3 * (KF_SP / 3) * pow(dt, 3.0);
    q[27] = 1e2 * KF_SP * dt;
    q[36] = 1e2 * KF_SP * dt;
    q[45] = 1e2 * KF_SP * dt;
    q[54] = 1e2 * ((KF_SF * dt) + ((KF_SG / 2) * pow(dt, 3.0)));
    q[63] = 1e2 * (KF_SG)*dt;
    q[3] = q[12] = q[21] = q[24] = q[33] = q[42] = (KF_SP / 2) * pow(dt, 2.0);
    q[55] = q[62] = (KF_SG / 2) * pow(dt, 2.0);
    MatrixInit(qMulti, KF_MULTI_STATES, KF_MULTI_STATES);
    qMulti[0] = 1e3 * (KF_SP / 3) * pow(dt, 3.0);
    qMulti[10] = 1e3 * (KF_SP / 3) * pow(dt, 3.0);
    qMulti[20] = 1e3 * (KF_SP / 3) * pow(dt, 3.0);
    qMulti[30] = 1e2 * KF_SP * dt;
    qMulti[40] = 1e2 * KF_SP * dt;
    qMulti[50] = 1e2 * KF_SP * dt;
    qMulti[60] = 1e2 * ((KF_SF * dt) + ((KF_SG / 2) * pow(dt, 3.0)));
    qMulti[70] = 1e2 * (KF_SG)*dt;
    qMulti[80] = 1e2 * ((KF_SF * dt) + ((KF_SG / 2) * pow(dt, 3.0)));
    qMulti[3] = qMulti[13] = qMulti[23] = qMulti[27] = qMulti[37] = qMulti[47] =
        (KF_SP / 2) * pow(dt, 2.0);
    qMulti[69] = qMulti[71] = (KF_SG / 2) * pow(dt, 2.0);
    qMulti[61] = qMulti[79] = (KF_SG / 2) * pow(dt, 2.0);
}
int InitKalmanFilterData(TNGeneConfiguration *cfg, TNGeneData *rx)
{
    int nCh = cfg->numberOfChannels;
    int nCh2 = 2 * nCh;
    TKalmanFilterData *kfData = &(rx->pvtData.kfData);
    int errors = 0;
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
                kfData->prCorr[idx] = kfData->pr[idx] - pvtRes->clockBiasGps;
            } else /* if(pvtCh->ch->satType == GALILEO_SAT) */
            {
                kfData->prCorr[idx] = kfData->pr[idx] - pvtRes->clockBiasGalileo;
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
    return SUCCESS_RETURN;
}
