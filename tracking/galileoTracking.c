/**
 * @file  galileoTracking.c
 *
 * @author Mario Nicola
 *
 * @date 08/09/2014
 *
 * @version 1.0.0
 *
 * @brief Functions used to perform tracking for Galileo.
 */
#include "ngeneConfiguration.h"
#include "ngeneDataStructure.h"
#include "channelDataStructure.h"
#include "trackingDataStructure.h"
#include "arrays.h"
#include "inputProcessing.h"
#include "tracking.h"
#include "trackingDiscriminatorsLoops.h"
#include "galileoMessageParser.h"
void ComputeGalileoTracking(TNGeneConfiguration *cfg, TNGeneData *rx, TChannel *ch, char *firstBunch,
                            unsigned int firstBunchSize, char *secondBunch,
                            unsigned int secondBunchSize)
{
    TTracking *trk = &(ch->tracking);
    /* the elaboration of one channel runs on the worker owning this scratch (see
     * receiver/dspWorkerPool.c), so the replicas of the spreading code are re-anchored here */
    TDspContext *dsp = NGENE_DSP(rx);
    /* the committing half reads the flag below (see CommitGalileoTracking). The holdover state
     * cannot change while the channels of one execution cycle are elaborated, so the value
     * is the one the single threaded execution read at the beginning of the millisecond */
    trk->checkedBeforeLoops = trk->holdover.anchorValid || trk->holdover.active;
    PointTrackingScratchToDsp(trk, dsp);
    /* carrier wipe-off */
    ch->carrierPhase = GenerateAndMixReceiverInput(cfg, rx, firstBunch, firstBunchSize, secondBunch,
                                                   secondBunchSize, ch->blocksize,
                                                   ch->carrierFrequency, ch->carrierPhase);
    ch->carrierFrequencyOld = ch->carrierFrequency;
    ch->dopplerShiftOld = ch->dopplerShift;
    /* code wipeoff */
    // generate the subcarrier
    // prompt
    GenerateSine(cfg, &(rx->fscTable), ch->codeRate, TWICE_M_PI * ch->codePhaseFrac, ch->blocksize,
                 dsp->subcarrierArray);
    if (cfg->lowFsElaboration == TRUE_FLAG) {
        // early
        GenerateSine(cfg, &(rx->fscTable), ch->codeRate, TWICE_M_PI * trk->codeEarlyPhaseFrac,
                     ch->blocksize, dsp->subcarrierArrayEarly);
        // late
        GenerateSine(cfg, &(rx->fscTable), ch->codeRate, TWICE_M_PI * trk->codeLatePhaseFrac,
                     ch->blocksize, dsp->subcarrierArrayLate);
    }
    // generate the code
    // prompt
    GenerateCode(cfg, &(rx->prnTable), ch->satType, ch->satId, ch->codeRate, &(ch->codePhase),
                 &(ch->codePhaseFrac), NGENE_MODULATION, ch->blocksize, trk->promptCode);
    if (cfg->lowFsElaboration == TRUE_FLAG) {
        // early
        GenerateCode(cfg, &(rx->prnTable), ch->satType, ch->satId, ch->codeRate,
                     &(trk->codeEarlyPhase), &(trk->codeEarlyPhaseFrac), NGENE_MODULATION,
                     ch->blocksize, trk->earlyCode);
        // late
        GenerateCode(cfg, &(rx->prnTable), ch->satType, ch->satId, ch->codeRate,
                     &(trk->codeLatePhase), &(trk->codeLatePhaseFrac), NGENE_MODULATION,
                     ch->blocksize, trk->lateCode);
    }
    // add subcarrier to the code
    ArrayXor(dsp->subcarrierArray, trk->promptCode, trk->promptCode, ch->blocksize);
    if (cfg->lowFsElaboration == TRUE_FLAG) {
        ArrayXor(dsp->subcarrierArrayEarly, trk->earlyCode, trk->earlyCode, ch->blocksize);
        ArrayXor(dsp->subcarrierArrayLate, trk->lateCode, trk->lateCode, ch->blocksize);
    }
    // correlations
    PerformAllTrackingCorrelations(ch, trk, dsp->cosArray, dsp->sinArray, dsp->wideI, dsp->wideQ);
}

void CommitGalileoTracking(TNGeneConfiguration *cfg, TNGeneData *rx, TChannel *ch)
{
    TTracking *trk = &(ch->tracking);
    /* value of (holdover.anchorValid || holdover.active) read by ComputeGalileoTracking */
    int checkedBeforeLoops = trk->checkedBeforeLoops;
    /* update ms counter, evaluate loops */
    trk->msCounter++;
    if (checkedBeforeLoops)
        CheckTrackingLock(cfg, rx, ch, trk);
    if (((trk->msCounter % trk->integrationPeriod) == 0) || (trk->msCounter == ch->bitDuration)) {
        DumpCorrelationsToFile(cfg, rx, ch, trk);
        if (TrackingLoopsEnabled(trk)) {
            if (ChooseCarrierLoop(cfg, rx, ch, trk) == CARRIER_FLL) {
                FllDiscriminator(trk);
            } else {
                PllDiscriminator(trk);
            }
            CarrierLoop(cfg, ch, trk);
            DllDiscriminator(trk);
            CodeLoop(ch, trk);
        }
    }
    /* manage navigation message */
    if (trk->msCounter == ch->bitDuration && TrackingNavigationEnabled(trk)) {
        ParseGalMessage(cfg, rx, ch, trk->pIForNav);
    }
    if (!checkedBeforeLoops)
        CheckTrackingLock(cfg, rx, ch, trk);
    /* reset finished correlations */
    UpdateCorrelations(ch, trk);
    /* Final update to the counters */
    if (trk->msCounter >= ch->bitDuration) {
        trk->msCounter = 0;
    }
    trk->trkCounter++;
}

TTrackingStatus PerformGalileoTracking(TNGeneConfiguration *cfg, TNGeneData *rx, TChannel *ch,
                                       char *firstBunch, unsigned int firstBunchSize,
                                       char *secondBunch, unsigned int secondBunchSize)
{
    ComputeGalileoTracking(cfg, rx, ch, firstBunch, firstBunchSize, secondBunch, secondBunchSize);
    CommitGalileoTracking(cfg, rx, ch);
    return ch->tracking.trkStatus;
}
