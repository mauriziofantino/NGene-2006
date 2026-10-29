/**
 * @file carrierSmoothing.c
 *
 * @author Mario Nicola
 *
 * @date 07/11/2014
 *
 * @version 1.0.0
 *
 * @brief Functions used to perform carrier smoothing.
 */
#include <stdlib.h>
#include <limits.h>
#include "carrierSmoothing.h"
#include "pvtDataStructure.h"
#include "ngeneConfiguration.h"
#include "ngeneConstants.h"
#include "ioFunctions.h"
#include "closeManager.h"
void ClearCsmDataChannel(TCarrierSmoothingData *csmDataCh)
{
    csmDataCh->csmMeasure = 0.0;
    csmDataCh->lpc = 0.0;
    csmDataCh->lpcOut = 0.0;
    csmDataCh->lpcOld = 0.0;
    csmDataCh->pseudorangeCsmOld = 0;
    csmDataCh->codeCorrectionOld = 0;
    csmDataCh->csmCounter = 0;
}
void ClearCsmData(TNGeneConfiguration *cfg, TPvtData *pvtData, TPvtResults *pvtRes)
{
    int idx;
    for (idx = 0; idx < cfg->numberOfChannels; idx++) {
        ClearCsmDataChannel(&(pvtData->csmData[idx]));
    }
    pvtData->csmTransient = TRUE_FLAG;
    pvtData->csmTransientStart = pvtData->pvtIndex;
    if (cfg->pvtConfiguration.csmFilterLen > 1) {
        pvtData->weightCode = 1.0 / (double)cfg->pvtConfiguration.csmFilterLen;
        pvtData->weightCarrier =
            ((double)cfg->pvtConfiguration.csmFilterLen - 1.0) * pvtData->weightCode;
    } else {
        pvtData->weightCode = 1.0;
        pvtData->weightCarrier = 0.0;
    }
    pvtRes->clockBiasGpsCumulated = 0.0;
    pvtRes->clockBiasGalileoCumulated = 0.0;
}
int InitCarrierSmoothing(TNGeneConfiguration *cfg, TPvtData *pvtData, TPvtResults *pvtRes)
{
    pvtData->csmData =
        (TCarrierSmoothingData *)malloc(cfg->numberOfChannels * sizeof(TCarrierSmoothingData));
    if (pvtData->csmData == NULL) {
        SwrxLog(NGENE_LOG_SEVERITY_ERROR, "Malloc returned a NULL pointer in InitPvtData");
        CloseNGene(cfg, EXIT_FAILURE);
    }
    ClearCsmData(cfg, pvtData, pvtRes);
    return SUCCESS_RETURN;
}
void DeinitCarrierSmoothing(TPvtData *pvtData)
{
    free(pvtData->csmData);
}
void UpdateCarrierSmoothingMeasure(TNGeneConfiguration *cfg, TChannel *ch, TPvtData *pvtData,
                                   TPvtResults *pvtRes)
{
    TCarrierSmoothingData *csmData = &(pvtData->csmData[ch->chIndex]);
    double fd;
    /* NB the sign of the Doppler shift must be corrected in function of the used RF front-end
     *    but the correction of the clock drift MUST not take into account this sign!!! */
    /* Tracking has already updated dopplerShift for the NEXT block. Integrate
     * the frequency actually applied to the completed samples. */
    fd = (double)cfg->fePars->dopplerCorrection * ch->dopplerShiftOld;
    fd = fd - (pvtRes->clockDrift / ((double)SPEED_OF_LIGHT / (double)L1_FREQUENCY));
    csmData->csmMeasure += (TWICE_M_PI * fd * (double)ch->blocksize * cfg->ts);
}
void PerformCarrierSmoothing(TNGeneConfiguration *cfg, TPvtData *pvtData, TPvtResults *pvtRes)
{
    int chIdx;
    TPvtChannel *pvtCh;
    TChannel *ch;
    TCarrierSmoothingData *csmData;
    double fdOld;
    double diff;
    double pseudoP;
    double codeCorrection, weight;
    /* if carrier smoothing is disabled, returns immediately */
    if (cfg->pvtConfiguration.csmFilterLen < 1) {
        for (chIdx = 0; chIdx < pvtData->numOfOperativeChannels; chIdx++) {
            pvtCh = &(pvtData->pvtChArray[chIdx]);
            ch = pvtCh->ch;
            pvtCh->pseudorangeCsm = pvtCh->pseudorange;
            if (pvtData->pvtLogFile != NULL) {
                if (ch->satType == GPS_SAT) {
                    fprintf(pvtData->pvtLogFile,
                            "[GPS] CHANNEL:%2d  SAT:%2d   pseudorange: %f   smoothed: %f\n",
                            ch->chIndex, ch->satId, pvtCh->pseudorange, pvtCh->pseudorangeCsm);
                } else /* if(ch->satType==GALILEO_SAT) */
                {
                    fprintf(pvtData->pvtLogFile,
                            "[GAL] CHANNEL:%2d  SAT:%2d   pseudorange: %f   smoothed: %f\n",
                            ch->chIndex, ch->satId, pvtCh->pseudorange, pvtCh->pseudorangeCsm);
                }
                fprintf(pvtData->pvtLogFile, "\n");
            }
        }
        return;
    }
    for (chIdx = 0; chIdx < pvtData->numOfOperativeChannels; chIdx++) {
        pvtCh = &(pvtData->pvtChArray[chIdx]);
        ch = pvtCh->ch;
        csmData = &(pvtData->csmData[ch->chIndex]);
        /* NB the sign of the Doppler shift must be corrected in function of the used RF front-end
         *    but the correction of the clock drift MUST not take into account this sign!!! */
        fdOld = (double)cfg->fePars->dopplerCorrection * ch->dopplerShiftOld;
        fdOld = fdOld - (pvtRes->clockDrift / ((double)SPEED_OF_LIGHT / (double)L1_FREQUENCY));
        /* translate from the value evaluated by the tracking to the value relative to the PVT
         * reference time */
        diff = TWICE_M_PI * fdOld * (double)ch->bufferOffset * cfg->ts;
        csmData->lpc = csmData->csmMeasure - diff;
        /* used for output (analogous to the measure in a Rinex file) */
        csmData->lpcOut = csmData->lpc / TWICE_M_PI;
        /* from radiant to meter */
        csmData->lpc *= ((double)SPEED_OF_LIGHT / ((double)L1_FREQUENCY * TWICE_M_PI));
        /* correct bias */
        if (ch->satType == GPS_SAT) {
            csmData->lpc += pvtRes->clockBiasGpsCumulated;
        } else /* if(ch->satType==GALILEO_SAT) */
        {
            csmData->lpc += pvtRes->clockBiasGalileoCumulated;
        }
        if (pvtData->pvtLogFile != NULL) {
            if (ch->satType == GALILEO_SAT) {
                fprintf(pvtData->pvtLogFile, "[GAL] ");
                fprintf(pvtData->pvtLogFile,
                        "CHANNEL:%2d  SAT:%2d      fd=%f    counter=%d    pseudorangeOld=%f\n",
                        ch->chIndex, ch->satId, ch->dopplerShiftOld, csmData->csmCounter,
                        csmData->pseudorangeCsmOld);
                fprintf(pvtData->pvtLogFile,
                        "                              LPC=%f     LPC_OLD=%f     BIAS=%f\n",
                        csmData->lpc, csmData->lpcOld, pvtRes->clockBiasGalileoCumulated);
            } else {
                fprintf(pvtData->pvtLogFile, "[GPS] ");
                fprintf(pvtData->pvtLogFile,
                        "CHANNEL:%2d  SAT:%2d      fd=%f    counter=%d    pseudorangeOld=%f\n",
                        ch->chIndex, ch->satId, ch->dopplerShiftOld, csmData->csmCounter,
                        csmData->pseudorangeCsmOld);
                fprintf(pvtData->pvtLogFile,
                        "                              LPC=%f     LPC_OLD=%f     BIAS=%f\n",
                        csmData->lpc, csmData->lpcOld, pvtRes->clockBiasGpsCumulated);
            }
        }
        /* perform smoothing */
        pvtCh->pseudorangeCsm = pvtCh->pseudorange;
        pseudoP = 0.0;
        codeCorrection = pvtCh->satClkCorr * SPEED_OF_LIGHT + pvtCh->egnosFastCorrection;
        if (csmData->csmCounter > 0) {
            /* Doppler observes the uncorrected signal. Apply changes in clock/SBAS
             * corrections immediately, rather than low-pass filtering them. */
            pseudoP = csmData->pseudorangeCsmOld - (csmData->lpc - csmData->lpcOld) +
                      codeCorrection - csmData->codeCorrectionOld;
            /* Hatch start-up: average available epochs, then use the configured
             * steady-state window. An early noisy code must not dominate 32 epochs. */
            weight = 1.0 / (csmData->csmCounter < cfg->pvtConfiguration.csmFilterLen
                                ? csmData->csmCounter + 1
                                : cfg->pvtConfiguration.csmFilterLen);
            pvtCh->pseudorangeCsm = pvtCh->pseudorange * weight + pseudoP * (1.0 - weight);
        }
        csmData->pseudorangeCsmOld = pvtCh->pseudorangeCsm;
        csmData->codeCorrectionOld = codeCorrection;
        csmData->lpcOld = csmData->lpc;
        if (csmData->csmCounter < INT_MAX)
            csmData->csmCounter++;
        /* update the CSM  */
        /* set the posSigma for Kalman filter */
        if (csmData->csmCounter < CSM_TRANSIENT_LENGTH) {
            pvtCh->posSigma2 = KF_POS_SIGMA2_HIGH_VALUE;
        } else {
            pvtCh->posSigma2 = KF_POS_SIGMA2_LOW_VALUE;
        }
        if ((pvtData->csmTransient == TRUE_FLAG) &&
            ((pvtData->pvtIndex - pvtData->csmTransientStart) > CSM_TRANSIENT_LENGTH)) {
            pvtData->csmTransient = FALSE_FLAG;
        }
        /* log */
        if (pvtData->pvtLogFile != NULL) {
            fprintf(pvtData->pvtLogFile,
                    "                              pseudoP: %f   pseudorange: %f   smoothed: %f\n",
                    pseudoP, pvtCh->pseudorange, pvtCh->pseudorangeCsm);
        }
    }
    if (pvtData->pvtLogFile != NULL) {
        fprintf(pvtData->pvtLogFile, "\n");
    }
}
void UpdateCumulatedBiases(TPvtResults *pvtRes, const TPvtData *data)
{
    /* Only clocks rebased from observations changed the measurement origin. */
    if (data->numOfGpsOpChannels > 0)
        pvtRes->clockBiasGpsCumulated += pvtRes->clockBiasGps;
    if (data->numOfGalileoOpChannels > 0)
        pvtRes->clockBiasGalileoCumulated += pvtRes->clockBiasGalileo;
}
