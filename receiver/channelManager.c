/**
 * @file channelManager.c
 *
 * @author Mario Nicola
 *
 * @date 23/08/2014
 *
 * @version 1.0.0
 *
 * @brief Functions used to manage the channel state machine
 */
#include <limits.h>
#include <math.h>
#include "ngeneConfiguration.h"
#include "channelDataStructure.h"
#include "ngeneConstants.h"
#include "circularBuffer.h"
#include "ngeneDataStructure.h"
#include "ioFunctions.h"
#include "coarseAcquisition.h"
#include "refinement.h"
#include "confirmation.h"
#include "tracking.h"
#include "gpsTracking.h"
#include "galileoTracking.h"
#include "gpsMessageParser.h"
#include "galileoMessageParser.h"
#include "fnavMessageParser.h"
#include "egnosMessageParser.h"
#include "egnosCorrections.h"
#include "carrierSmoothing.h"
#include "prnList.h"
#include "osnmaManagement.h"
#include "osnmaValidations.h"
#include "warmStart.h"
#include "navigationMessage.h"
#include "dspWorkerPool.h"
#include "channelManager.h"
/* Acquisition scheduling */
int InitChannelScheduler(TNGeneConfiguration *cfg, TChannelScheduler *chSch,
                         int numOfRealGalileoPrns, int *realGalileoPrns)
{
    int gpsEnabled = TRUE_FLAG;
    int galileoEnabled = TRUE_FLAG;
    int egnosEnabled = TRUE_FLAG;
    /* List of PRNs includes all supported PRNs (whose constellation has allocated channels) */
    if (cfg->numberOfGpsChannels == 0)
        gpsEnabled = FALSE_FLAG;
    if (cfg->numberOfGalileoChannels == 0)
        galileoEnabled = FALSE_FLAG;
    if (cfg->numberOfEgnosChannels == 0)
        egnosEnabled = FALSE_FLAG;
    if (CreatePrnList(&(chSch->prnList), gpsEnabled, galileoEnabled, egnosEnabled,
                      numOfRealGalileoPrns, realGalileoPrns) != SUCCESS_RETURN)
        return FAILURE_RETURN;
/* No channels in acquisition at the start */
#ifdef MAXIMUM_NUMBER_OF_CHANNELS_IN_ACQUISITION
    chSch->maximumNumberOfChannelsInAcquisition = 1;
    if (MAXIMUM_NUMBER_OF_CHANNELS_IN_ACQUISITION > 0) {
        chSch->maximumNumberOfChannelsInAcquisition = MAXIMUM_NUMBER_OF_CHANNELS_IN_ACQUISITION;
    }
#else
    chSch->maximumNumberOfChannelsInAcquisition = 4;
#endif
    chSch->channelsInAcquisition = 0;
    chSch->galileoChannelsInAcquisition = 0;
    /* No channels in tracking at the start */
    chSch->channelsInTracking = 0;
    chSch->gpsChannelsInTracking = 0;
    chSch->egnosChannelsInTracking = 0;
    chSch->galileoChannelsInTracking = 0;
    return SUCCESS_RETURN;
}

void DeinitChannelScheduler(TChannelScheduler *chSch)
{
    DestroyPrnList(&(chSch->prnList));
}
int GetNextPrnForAcquisition(TNGeneConfiguration *cfg, TWarmStartData *warmData,
                             TChannelScheduler *chSch, TChannel *ch)
{
    int satId = INVALID_PRN;
    int visibilityCheck;
    /* only called when channel is in POWERON_STATE */
    if (ch->chState != POWERON_STATE)
        return INVALID_PRN;
/* if MAXIMUM_NUMBER_OF_CHANNELS_IN_ACQUISITION is defined,
 * a fixed maximum number of channels in acquisition is used */
#ifndef MAXIMUM_NUMBER_OF_CHANNELS_IN_ACQUISITION
    /* update the maximum number of channels in acquisition in function of the number of channels in
     * tracking INFO these digits are good for a PC and should be lowered when less performing
     * platforms are used */
    if (chSch->channelsInTracking <= 4)
        chSch->maximumNumberOfChannelsInAcquisition = 4;
    else if (chSch->channelsInTracking <= 10)
        chSch->maximumNumberOfChannelsInAcquisition = 3;
    else if (chSch->channelsInTracking <= 16)
        chSch->maximumNumberOfChannelsInAcquisition = 2;
    else
        chSch->maximumNumberOfChannelsInAcquisition = 1;
#endif
    /* budget for acquisition is finished */
    if (chSch->channelsInAcquisition >= chSch->maximumNumberOfChannelsInAcquisition)
        return INVALID_PRN;
    /* Keep search capacity available to GPS while its channels are still empty.
     * Galileo-only reception retains the full acquisition budget. */
    if (ch->satType == GALILEO_SAT && cfg->numberOfGpsChannels > chSch->gpsChannelsInTracking &&
        chSch->galileoChannelsInAcquisition >= 1)
        return INVALID_PRN;
    /* find the suitable PRN */
    if (cfg->automaticConfigurationFlag == FALSE_FLAG) {
        /* if manual configuration is active,
         * PRN is allocated by the user regardless of PRN list or visibility */
        chSch->channelsInAcquisition++;
        if (ch->satType == GALILEO_SAT)
            chSch->galileoChannelsInAcquisition++;
        return cfg->manuallyConfiguredChannels[ch->chIndex].satId;
    } else {
        /* get next PRN available and (maybe) visible */
        do {
            satId = GetNextAvailableFromPrnList(ch->satType, &(chSch->prnList));
            visibilityCheck =
                CheckSatVisibility(&(cfg->warmConfiguration), warmData, ch->satType, satId);
            if ((satId != INVALID_PRN) && (visibilityCheck == FALSE_FLAG)) {
                FindAndFreeInPrnList(ch->satType, satId, &(chSch->prnList));
            }
        } while (visibilityCheck == FALSE_FLAG);
        if (satId != INVALID_PRN) {
            chSch->channelsInAcquisition++;
            if (ch->satType == GALILEO_SAT)
                chSch->galileoChannelsInAcquisition++;
        }
    }
    return satId;
}
void FreeAcquisitionSpotAfterAcquisitionSuccess(TNGeneData *rx, TChannelScheduler *chSch,
                                                TChannel *ch)
{
    /* if acquisition is successful:
     * one more channel in tracking and the acquisition spot is available */
    chSch->channelsInTracking++;
    if (ch->satType == GPS_SAT)
        chSch->gpsChannelsInTracking++;
    else if (ch->satType == GALILEO_SAT)
        chSch->galileoChannelsInTracking++;
    else if (ch->satType == EGNOS_SAT)
        chSch->egnosChannelsInTracking++;
    chSch->channelsInAcquisition--;
    if (ch->satType == GALILEO_SAT)
        chSch->galileoChannelsInAcquisition--;
    FindAndOccupyInPrnList(ch->satType, ch->satId, &(rx->acqCommon.nonCohManager.testedPrns));
}

void ResetChannelVariables(TNGeneConfiguration *cfg, TChannel *ch)
{
    ch->satId = INVALID_PRN;
    FromPrnToString(ch->satType, ch->satId, ch->prnName);
    ch->carrierFrequency = cfg->fif;
    ch->dopplerShift = 0.0;
    ch->carrierFrequencyOld = cfg->fif;
    ch->dopplerShiftOld = 0.0;
    ch->carrierPhase = 0.0;
    if (ch->satType == GPS_SAT) {
        ch->codeLen = GPS_CODE_LEN;
        ch->codeDuration = GPS_CODE_DURATION;
        ch->codeRate = GPS_CODE_RATE;
        ch->bitDuration = GPS_BIT_DURATION;
    } else if (ch->satType == GALILEO_SAT) {
        ch->codeLen = GALILEO_CODE_LEN;
        ch->codeDuration = GALILEO_CODE_DURATION;
        ch->codeRate = GALILEO_CODE_RATE;
        ch->bitDuration = GALILEO_BIT_DURATION;
    } else {
        ch->codeLen = EGNOS_CODE_LEN;
        ch->codeDuration = EGNOS_CODE_DURATION;
        ch->codeRate = EGNOS_CODE_RATE;
        ch->bitDuration = EGNOS_BIT_DURATION;
    }
    ch->codePhase = 0.0;
    ch->codePhaseFrac = 0.0;
    ch->newNavMsgInformation = FALSE_FLAG;
    ch->dsmStatus = TO_BE_VALIDATED;
    ch->keyStatus = TO_BE_VALIDATED;
    ch->msgStatus = TO_BE_VALIDATED;
    ch->macseqStatus = TO_BE_VALIDATED;
}
void FreeAcquisitionSpotAfterAcquisitionFailure(TNGeneConfiguration *cfg, TChannelScheduler *chSch,
                                                TChannel *ch)
{
    /* if acquisition fails,
     * the PRN is again available and the acquisition spot too */
    FindAndFreeInPrnList(ch->satType, ch->satId, &(chSch->prnList));
    ResetChannelVariables(cfg, ch);
    chSch->channelsInAcquisition--;
    if (ch->satType == GALILEO_SAT)
        chSch->galileoChannelsInAcquisition--;
}

void ReleaseTracking(TNGeneConfiguration *cfg, TChannelScheduler *chSch, TChannel *ch)
{
    /* if tracking is lost,
     * one less channel in tracking, the PRN is again available */
    chSch->channelsInTracking--;
    if (ch->satType == GPS_SAT)
        chSch->gpsChannelsInTracking--;
    else if (ch->satType == GALILEO_SAT)
        chSch->galileoChannelsInTracking--;
    else if (ch->satType == EGNOS_SAT)
        chSch->egnosChannelsInTracking--;
    FindAndFreeInPrnList(ch->satType, ch->satId, &(chSch->prnList));
    ResetChannelVariables(cfg, ch);
}

void InitChannel(TNGeneConfiguration *cfg, TNGeneData *rx, TChannel *ch)
{
    // init the state taking into account if the elaboration is enabled
    if (((ch->satType == GPS_SAT) || (ch->satType == EGNOS_SAT)) &&
        (cfg->disableGpsElaboration == TRUE_FLAG)) {
        ch->chState = UNUSED_STATE;
    } else if ((ch->satType == GALILEO_SAT) && (cfg->disableGalElaboration == TRUE_FLAG)) {
        ch->chState = UNUSED_STATE;
    } else {
        ch->chState = POWERON_STATE;
    }
    //
    ch->bufferOffset = 0;
    ch->blocksize = 0;
    ch->blocksizeNext = 0;
    /* the cycle of the channel stores the data of the current execution cycle (see
     * receiver/dspWorkerPool.h) and queues its log messages (see utility/logCapture.h) */
    /* the queue of the log of this channel and the data of its execution cycle live in the
     * channel structure (see utility/logCapture.h, receiver/dspWorkerPool.h) */
    InitLogCapture(&(ch->logCapture));
    ch->cycle.elaborates = FALSE_FLAG;
    ch->cycle.firstBunch = NULL;
    ch->cycle.firstBunchSize = 0;
    ch->cycle.secondBunch = NULL;
    ch->cycle.secondBunchSize = 0;
    ResetChannelVariables(cfg, ch);
    InitCoarseAcquisition(cfg, &(rx->acqCommon), ch);
    InitRefinement(cfg, &(ch->refinement), ch);
#ifndef DISABLE_CONFIRMATION_THRESHOLD
    InitConfirmation(cfg, rx, ch);
#else
    InitConfirmation(cfg, ch);
#endif
    InitTracking(cfg, rx, ch);
    InitGpsParser(cfg, ch);
    InitGalParser(cfg, ch);
    InitFNavParser(cfg, ch);
    InitEgnosParser(cfg, ch);
    ch->egnosPrnMaskIndex = INVALID_VALUE;
    ch->pvtInfo.usedForPVT = FALSE_FLAG;
    ch->pvtInfo.satElevation = 0.0;
    ch->pvtInfo.satAzimuth = 0.0;
    ch->pvtInfo.pseudorange = 0.0;
    ch->pvtInfo.satEcefX = 0.0;
    ch->pvtInfo.satEcefY = 0.0;
    ch->pvtInfo.satEcefZ = 0.0;
//**
//**
#ifdef RX_PROFILING
    InitProfilingSupport(&(ch->gpsCoarseAcqProfiling), "GPS Coarse Acquisition");
    InitProfilingSupport(&(ch->galileoCoarseAcqProfiling), "GALILEO Coarse Acquisition");
    InitProfilingSupport(&(ch->gpsDopplerRefinementProfiling), "GPS Doppler Refinement");
    InitProfilingSupport(&(ch->galileoDopplerRefinementProfiling), "GALILEO Doppler Refinement");
    InitProfilingSupport(&(ch->gpsCodeRefinementProfiling), "GPS Code Refinement");
    InitProfilingSupport(&(ch->galileoCodeRefinementProfiling), "GALILEO Code Refinement");
    InitProfilingSupport(&(ch->gpsConfirmationProfiling), "GPS Confirmation");
    InitProfilingSupport(&(ch->galileoConfirmationProfiling), "Galileo Confirmation");
    InitProfilingSupport(&(ch->gpsTrackingProfiling), "GPS Tracking");
    InitProfilingSupport(&(ch->galileoTrackingProfiling), "Galileo Tracking");
    InitProfilingSupport(&(ch->gpsFftProfiling), "GPS FFT");
    InitProfilingSupport(&(ch->galileoFftProfiling), "GALILEO FFT");
    InitProfilingSupport(&(ch->gpsSampleCompressionProfiling), "GPS Sample Compression");
    InitProfilingSupport(&(ch->galileoSampleCompressionProfiling), "GALILEO Sample Compression");
#endif
    //**
}

void DenitChannel(TChannel *ch)
{
    DeinitCoarseAcquisition(&(ch->coarse));
    DeinitTracking(&(ch->tracking));
    DeinitGpsParser(ch);
    DeinitGalParser(ch);
    DeinitFNavParser(ch);
    DeinitEgnosParser(ch);
#ifdef RX_PROFILING
    ReportProfilingData(&(ch->gpsCoarseAcqProfiling));
    ReportProfilingData(&(ch->galileoCoarseAcqProfiling));
    ReportProfilingData(&(ch->gpsDopplerRefinementProfiling));
    ReportProfilingData(&(ch->galileoDopplerRefinementProfiling));
    ReportProfilingData(&(ch->gpsCodeRefinementProfiling));
    ReportProfilingData(&(ch->galileoCodeRefinementProfiling));
    ReportProfilingData(&(ch->gpsConfirmationProfiling));
    ReportProfilingData(&(ch->galileoConfirmationProfiling));
    ReportProfilingData(&(ch->gpsTrackingProfiling));
    ReportProfilingData(&(ch->galileoTrackingProfiling));
    ReportProfilingData(&(ch->gpsFftProfiling));
    ReportProfilingData(&(ch->galileoFftProfiling));
    ReportProfilingData(&(ch->gpsSampleCompressionProfiling));
    ReportProfilingData(&(ch->galileoSampleCompressionProfiling));
#endif
}

void UpdateBlocksize(TNGeneConfiguration *cfg, TChannel *ch)
{
    /* verify the goodness of codePhaseFrac */
    if ((ch->codePhaseFrac >= 1.0) || (ch->codePhaseFrac < 0.0)) {
        ch->codePhaseFrac -= floor(ch->codePhaseFrac);
    }
    /* Also Galileo elaborates one ms of samples per execution cycle
     * and exhibits the same rate of GPS and EGNOS */
    ch->blocksizeNext =
        (unsigned int)((((double)GPS_CODE_LEN - ch->codePhaseFrac) / ch->codeRate) * cfg->fs) + 1;
}

unsigned int GetMaximumNumberOfSamplesNeeded(TNGeneConfiguration *cfg, TNGeneData *rx)
{
    unsigned int neededSamples = (unsigned int)cfg->samplesPerMillisecond;
    TChannel *ch;
    unsigned int neededSamplesByCurrentChannel;
    int index;
    for (index = 0; index < cfg->numberOfChannels; index++) {
        ch = &(rx->channelArray[index]);
        neededSamplesByCurrentChannel = ch->bufferOffset + ch->blocksizeNext;
        /* take into account additional code delay used in coarse acquisition and code refinement */
        if (ch->chState == COARSE_ACQUISITION_STATE) {
            neededSamplesByCurrentChannel += (ch->coarse.delays[ch->coarse.delayIndex]);
        } else if (ch->chState == CODE_REFINEMENT_STATE) {
            neededSamplesByCurrentChannel += (rx->acqCommon.codeRefSteps[ch->refinement.indexBin]);
        }
        /* select the maximum */
        if (neededSamplesByCurrentChannel > neededSamples)
            neededSamples = neededSamplesByCurrentChannel;
    }
    return neededSamples;
}

unsigned int GetNumberOfUsedSamples(TNGeneConfiguration *cfg, TNGeneData *rx)
{
    unsigned int usedSamples = UINT_MAX;
    int index;
    /* for each channel:
     * the samples in the buffer up to channel offset have been elaborated.
     * The minimum channel offset in the receiver is the number of samples elaborated
     * by all the channels and can be discarded from the input buffer */
    for (index = 0; index < cfg->numberOfChannels; index++) {
        if (rx->channelArray[index].bufferOffset < usedSamples)
            usedSamples = rx->channelArray[index].bufferOffset;
    }
    /* return the number of samples that can be discarded from the input buffer */
    return usedSamples;
}
void CleanBufferOffsetForAllChannels(TNGeneConfiguration *cfg, TNGeneData *rx,
                                     unsigned int deletedSamples)
{
    int index;
    /* nothing to be discarded */
    if (deletedSamples == 0) {
        return;
    }
    /* reduces the buffer offset in each channel */
    for (index = 0; index < cfg->numberOfChannels; index++) {
        rx->channelArray[index].bufferOffset -= deletedSamples;
    }
}
void GetSamplePointersForChannel(TChannel *ch, TCircularBuffer *buf, char **firstBunch,
                                 unsigned int *firstBunchSize, char **secondBunch,
                                 unsigned int *secondBunchSize)
{
    /* Gets the sample pointer taking into account:
     * - buffer pointers
     * - channel offset
     */
    if (ch->bufferOffset < buf->fill1) {
        /* useful samples starts in the first bunch of samples in the buffer */
        *firstBunch = &(buf->buffer[(buf->tail + ch->bufferOffset) * buf->sampleStride]);
        *firstBunchSize = buf->fill1 - ch->bufferOffset;
        if (buf->fill2 > 0) {
            *secondBunch = &(buf->buffer[0]);
            *secondBunchSize = buf->fill2;
        } else {
            *secondBunch = NULL;
            *secondBunchSize = 0;
        }
    } else if (ch->bufferOffset < (buf->fill1 + buf->fill2)) {
        /* useful samples starts in the second bunch of samples in the buffer  */
        *firstBunch = &(buf->buffer[(ch->bufferOffset - buf->fill1) * buf->sampleStride]);
        *firstBunchSize = buf->fill1 + buf->fill2 - ch->bufferOffset;
        *secondBunch = NULL;
        *secondBunchSize = 0;
    } else {
        /* channel offset exceeds available samples */
        *firstBunch = NULL;
        *firstBunchSize = 0;
        *secondBunch = NULL;
        *secondBunchSize = 0;
    }
}

int AllChannelsUnused(TNGeneConfiguration *cfg, TNGeneData *rx)
{
    int allChannelsUnused = TRUE_FLAG;
    int chIndex;
    for (chIndex = 0; chIndex < cfg->numberOfChannels; chIndex++) {
        if (rx->channelArray[chIndex].chState != UNUSED_STATE)
            allChannelsUnused = FALSE_FLAG;
    }
    return allChannelsUnused;
}
/**
 * @brief Computes one execution cycle of one channel on a DSP worker.
 * @details Elaborates the samples stored in the cycle of the channel, with the scratch owned
 *          by the calling worker (see receiver/dspWorkerPool.h), and stores the result of the
 *          acquisition in the cycle. The log messages of the elaboration are queued, and
 *          replayed in channel index order by CommitChannelCycle() (see utility/logCapture.h).
 */
void ComputeChannelCycle(TNGeneConfiguration *cfg, TNGeneData *rx, TChannel *ch)
{
    TChannelCycle *cycle = &(ch->cycle);
    if (cycle->elaborates == FALSE_FLAG)
    {
        /* PrepareChannelCycle() refused the elaboration of this execution cycle (see
         * receiver/channelManager.c) */
        return;
    }
    BeginLogCapture(&(ch->logCapture));
    switch (cycle->state) {
    case COARSE_ACQUISITION_STATE:
#ifdef RX_PROFILING
        if (ch->satType == GPS_SAT) {
            StartProfilingUpdate(&(ch->gpsCoarseAcqProfiling));
        } else if (ch->satType == GALILEO_SAT) {
            StartProfilingUpdate(&(ch->galileoCoarseAcqProfiling));
        }
#endif
        cycle->acqStatus = PerformCoarseAcquisition(cfg, rx, ch, cycle->firstBunch,
                                                    cycle->firstBunchSize, cycle->secondBunch,
                                                    cycle->secondBunchSize);
#ifdef RX_PROFILING
        if (ch->satType == GPS_SAT) {
            StopProfilingUpdate(&(ch->gpsCoarseAcqProfiling), 1);
        } else if (ch->satType == GALILEO_SAT && ch->coarse.msCounter == 0) {
            StopProfilingUpdate(&(ch->galileoCoarseAcqProfiling), 1);
        } else if (ch->satType == GALILEO_SAT && ch->coarse.msCounter != 0) {
            TotalTimeProfilingUpdate(&(ch->galileoCoarseAcqProfiling));
        }
#endif
        break;
    case DOPPLER_REFINEMENT_STATE:
#ifdef RX_PROFILING
        if (ch->satType == GPS_SAT) {
            StartProfilingUpdate(&(ch->gpsDopplerRefinementProfiling));
        } else if (ch->satType == GALILEO_SAT) {
            StartProfilingUpdate(&(ch->galileoDopplerRefinementProfiling));
        }
#endif
        cycle->acqStatus = PerformDopplerRefinement(cfg, rx, ch, cycle->firstBunch,
                                                    cycle->firstBunchSize, cycle->secondBunch,
                                                    cycle->secondBunchSize);
#ifdef RX_PROFILING
        if (ch->satType == GPS_SAT) {
            StopProfilingUpdate(&(ch->gpsDopplerRefinementProfiling), 1);
        } else if (ch->satType == GALILEO_SAT && ch->refinement.msCounter == 0) {
            StopProfilingUpdate(&(ch->galileoDopplerRefinementProfiling), 1);
        } else if (ch->satType == GALILEO_SAT && ch->refinement.msCounter != 0) {
            TotalTimeProfilingUpdate(&(ch->galileoDopplerRefinementProfiling));
        }
#endif
        break;
    case CODE_REFINEMENT_STATE:
#ifdef RX_PROFILING
        if (ch->satType == GPS_SAT) {
            StartProfilingUpdate(&(ch->gpsCodeRefinementProfiling));
        } else if (ch->satType == GALILEO_SAT) {
            StartProfilingUpdate(&(ch->galileoCodeRefinementProfiling));
        }
#endif
        cycle->acqStatus = PerformCodeRefinement(cfg, rx, ch, cycle->firstBunch,
                                                 cycle->firstBunchSize, cycle->secondBunch,
                                                 cycle->secondBunchSize);
#ifdef RX_PROFILING
        if (ch->satType == GPS_SAT) {
            StopProfilingUpdate(&(ch->gpsCodeRefinementProfiling), 1);
        } else if (ch->satType == GALILEO_SAT && ch->refinement.msCounter == 0) {
            StopProfilingUpdate(&(ch->galileoCodeRefinementProfiling), 1);
        } else if (ch->satType == GALILEO_SAT && ch->refinement.msCounter != 0) {
            TotalTimeProfilingUpdate(&(ch->galileoCodeRefinementProfiling));
        }
#endif
        break;
    case CONFIRMATION_STATE:
#ifdef RX_PROFILING
        if (ch->satType == GPS_SAT) {
            StartProfilingUpdate(&(ch->gpsConfirmationProfiling));
        } else if (ch->satType == GALILEO_SAT) {
            StartProfilingUpdate(&(ch->galileoConfirmationProfiling));
        }
#endif
        cycle->acqStatus = PerformConfirmation(cfg, rx, ch, cycle->firstBunch,
                                               cycle->firstBunchSize, cycle->secondBunch,
                                               cycle->secondBunchSize);
#ifdef RX_PROFILING
        if (ch->satType == GPS_SAT) {
            StopProfilingUpdate(&(ch->gpsConfirmationProfiling), 1);
        } else if (ch->satType == GALILEO_SAT && ch->confirmation.msCounter == 0) {
            StopProfilingUpdate(&(ch->galileoConfirmationProfiling), 1);
        } else if (ch->satType == GALILEO_SAT && ch->confirmation.msCounter != 0) {
            TotalTimeProfilingUpdate(&(ch->galileoConfirmationProfiling));
        }
#endif
        break;
    case TRACKING_STATE:
        if (ch->satType == GALILEO_SAT) {
#ifdef RX_PROFILING
            StartProfilingUpdate(&(ch->galileoTrackingProfiling));
#endif
            ComputeGalileoTracking(cfg, rx, ch, cycle->firstBunch, cycle->firstBunchSize,
                                   cycle->secondBunch, cycle->secondBunchSize);
        } else {
#ifdef RX_PROFILING
            StartProfilingUpdate(&(ch->gpsTrackingProfiling));
#endif
            ComputeGpsTracking(cfg, rx, ch, cycle->firstBunch, cycle->firstBunchSize,
                               cycle->secondBunch, cycle->secondBunchSize);
        }
        break;
    default:
        /* POWERON_STATE, UNUSED_STATE and the other states have no elaboration to compute:
         * the assignment of a PRN and the transitions of the state machine are committed by
         * CommitChannelCycle() on the main thread (see receiver/channelManager.c) */
        break;
    }
    EndLogCapture();
}
/**
 * @brief Prepares one execution cycle of one channel on the main thread.
 * @details Reads the state of the channel and the samples of the cycle, and accepts the
 *          elaboration. It runs before any channel of the current cycle is elaborated, so the
 *          eligibility of the elaboration does not depend on the elaboration of the other
 *          channels (see receiver/dspWorkerPool.h).
 */
void PrepareChannelCycle(TNGeneConfiguration *cfg, TNGeneData *rx, TChannel *ch, char *firstBunch,
                         unsigned int firstBunchSize, char *secondBunch,
                         unsigned int secondBunchSize)
{
    TChannelCycle *cycle = &(ch->cycle);
    cycle->elaborates = FALSE_FLAG;
    /* Manage conditions that do not allow elaboration during the current execution cycle */
    /* Channel offset exceeds the number of samples in the buffer */
    if (firstBunch == NULL)
        return;
    /* If channel offset grows too much fast */
    if (ch->bufferOffset > (unsigned int)cfg->samplesPerMillisecond)
        return;
    /* Blocksize exceeds the number of samples in the buffer */
    if (ch->blocksizeNext > (firstBunchSize + secondBunchSize))
        return;
    /* The state and the samples of the current elaboration are stored for the worker thread and
     * for the commit on the main thread (see receiver/dspWorkerPool.h) */
    cycle->state = ch->chState;
    cycle->firstBunch = firstBunch;
    cycle->firstBunchSize = firstBunchSize;
    cycle->secondBunch = secondBunch;
    cycle->secondBunchSize = secondBunchSize;
    /* Prepare for the elaboration */
    ch->blocksize = ch->blocksizeNext;
    cycle->elaborates = TRUE_FLAG;
}
/**
 * @brief Commits one execution cycle of one channel on the main thread.
 * @details Replays the log messages queued by the elaboration of this channel (see
 *          utility/logCapture.h) and applies the transitions of the channel state machine, in
 *          the order of the channel index and therefore exactly as the single threaded execution
 *          did (see receiver/dspWorkerPool.h).
 */
void CommitChannelCycle(TNGeneConfiguration *cfg, TNGeneData *rx, TChannel *ch)
{
    TChannelCycle *cycle = &(ch->cycle);
    FlushLogCapture(&(ch->logCapture));
    /* Channel state machine */
    switch (cycle->state) {
    case POWERON_STATE:
        ch->satId =
            GetNextPrnForAcquisition(cfg, &(rx->warmStartData), &(rx->channelScheduler), ch);
        if (ch->satId != INVALID_PRN) {
            FromPrnToString(ch->satType, ch->satId, ch->prnName);
            SwrxLog(NGENE_LOG_SEVERITY_NOTE, "Channel %2d allocated to %s", ch->chIndex,
                    ch->prnName);
            StartCoarseAcquisition(cfg, rx, &(rx->acqCommon), ch);
            ch->chState = COARSE_ACQUISITION_STATE;
        }
        break;
    case COARSE_ACQUISITION_STATE:
        if (cycle->acqStatus == ACQUISITION_SKIPPED) {
            /* when acquisition is skipped, codePhase and blocksize are not updated;
             * so at the next execution the elaboration will start without skipping samples */
            return;
        } else if (cycle->acqStatus == ACQUISITION_FAILURE) {
            SwrxLog(NGENE_LOG_SEVERITY_NOTE, "Channel %2d discarded %s after coarse acquisition",
                    ch->chIndex, ch->prnName);
            FreeInTheVisibilityList(&(cfg->warmConfiguration), &(rx->warmStartData), ch->satType,
                                    ch->satId);
            FreeAcquisitionSpotAfterAcquisitionFailure(cfg, &(rx->channelScheduler), ch);
            ch->chState = POWERON_STATE;
        } else if (cycle->acqStatus == ACQUISITION_SUCCESS) {
            SwrxLog(
                NGENE_LOG_SEVERITY_NOTE,
                "Channel %2d %s Doppler shift refinement starts at samples %llu with Doppler %f Hz",
                ch->chIndex, ch->prnName,
                rx->buf.discardedSamples + ch->bufferOffset + ch->blocksize, ch->dopplerShift);
            StartDopplerRefinement(cfg, rx, ch);
            ch->chState = DOPPLER_REFINEMENT_STATE;
        }
        break;
    case DOPPLER_REFINEMENT_STATE:
        if (cycle->acqStatus == ACQUISITION_TERMINATED) {
            SwrxLog(
                NGENE_LOG_SEVERITY_NOTE,
                "Channel %2d %s code delay refinement starts at samples %llu with Doppler %f Hz",
                ch->chIndex, ch->prnName,
                rx->buf.discardedSamples + ch->bufferOffset + ch->blocksize, ch->dopplerShift);
            StartCodeRefinement(cfg, rx, ch);
            ch->chState = CODE_REFINEMENT_STATE;
        }
        break;
    case CODE_REFINEMENT_STATE:
        if (cycle->acqStatus == ACQUISITION_SKIPPED) {
            /* when acquisition is skipped, codePhase and blocksize are not updated;
             * so at the next execution the elaboration will start without skipping samples */
            return;
        } else if (cycle->acqStatus == ACQUISITION_TERMINATED) {
            SwrxLog(NGENE_LOG_SEVERITY_NOTE,
                    "Channel %2d %s confirmation starts at samples %llu with Doppler %f Hz",
                    ch->chIndex, ch->prnName,
                    rx->buf.discardedSamples + ch->bufferOffset + ch->blocksize, ch->dopplerShift);
#ifndef DISABLE_CONFIRMATION_THRESHOLD
            StartConfirmation(cfg, rx, ch);
#else
            StartConfirmation(ch);
#endif
            ch->chState = CONFIRMATION_STATE;
        }
        break;
    case CONFIRMATION_STATE:
        if (cycle->acqStatus == ACQUISITION_FAILURE) {
            SwrxLog(NGENE_LOG_SEVERITY_NOTE, "Channel %2d discarded %s after confirmation",
                    ch->chIndex, ch->prnName);
            FreeInTheVisibilityList(&(cfg->warmConfiguration), &(rx->warmStartData), ch->satType,
                                    ch->satId);
            FreeAcquisitionSpotAfterAcquisitionFailure(cfg, &(rx->channelScheduler), ch);
            ch->chState = POWERON_STATE;
        } else if (cycle->acqStatus == ACQUISITION_SUCCESS) {
            SwrxLog(NGENE_LOG_SEVERITY_INFO,
                    "Channel %2d %s tracking starts at samples %llu with Doppler %f Hz",
                    ch->chIndex, ch->prnName,
                    rx->buf.discardedSamples + ch->bufferOffset + ch->blocksize, ch->dopplerShift);
            FreeAcquisitionSpotAfterAcquisitionSuccess(rx, &(rx->channelScheduler), ch);
            StartTracking(cfg, ch, &(ch->tracking));
            StartGpsParser(cfg, rx, ch);
            StartGalParser(cfg, rx, ch);
            StartEgnosParser(cfg, ch);
            ClearCsmDataChannel(&(rx->pvtData.csmData[ch->chIndex]));
            ch->chState = TRACKING_STATE;
            SetEgnosPrnMaskIndex(1, ch, &(rx->egnosCorrections));
        }
        break;
    case TRACKING_STATE:
        if (ch->satType == GALILEO_SAT) {
            CommitGalileoTracking(cfg, rx, ch);
#ifdef RX_PROFILING
            /* the profiling of the elaboration starts in ComputeChannelCycle() */
            if ((ch->tracking.msCounter % ch->tracking.integrationPeriod) == 0) {
                StopProfilingUpdate(&(ch->galileoTrackingProfiling), 1);
            } else {
                TotalTimeProfilingUpdate(&(ch->galileoTrackingProfiling));
            }
#endif
        } else {
            CommitGpsTracking(cfg, rx, ch);
#ifdef RX_PROFILING
            /* the profiling of the elaboration starts in ComputeChannelCycle() */
            StopProfilingUpdate(&(ch->gpsTrackingProfiling), 1);
#endif
        }
        if (ch->tracking.trkStatus == TRACKING_LOST) {
            FreeInTheVisibilityList(&(cfg->warmConfiguration), &(rx->warmStartData), ch->satType,
                                    ch->satId);
            ReleaseTracking(cfg, &(rx->channelScheduler), ch);
            DeleteChannelFromOsnmaData(&(rx->osnmaData), ch->chIndex);
            ResetChannelAuthenticationStruct(&(rx->osnmaAuth), ch->chIndex);
            ch->chState = POWERON_STATE;
        }
        /* carrier smoothing */
        if (ch->tracking.trkStatus == TRACKING_LOCKED) {
            UpdateCarrierSmoothingMeasure(cfg, ch, &(rx->pvtData), &(rx->pvtResults));
        } else {
            ClearCsmDataChannel(&(rx->pvtData.csmData[ch->chIndex]));
        }
        break;
    case UNUSED_STATE:
    default:
        break;
    }
    /* Discard elaborated samples */
    ch->bufferOffset += ch->blocksize;
    /* Prepare next elaboration */
    UpdateBlocksize(cfg, ch);
}

void DoChannelManager(TNGeneConfiguration *cfg, TNGeneData *rx, TChannel *ch, char *firstBunch,
                      unsigned int firstBunchSize, char *secondBunch, unsigned int secondBunchSize)
{
    PrepareChannelCycle(cfg, rx, ch, firstBunch, firstBunchSize, secondBunch, secondBunchSize);
    if (ch->cycle.elaborates == FALSE_FLAG)
        return;
    ComputeChannelCycle(cfg, rx, ch);
    CommitChannelCycle(cfg, rx, ch);
}

void RunChannelsCycle(TNGeneConfiguration *cfg, TNGeneData *rx)
{
    char *firstBunch;
    char *secondBunch;
    unsigned int firstBunchSize;
    unsigned int secondBunchSize;
    TChannel *ch;
    int index;
    /* phase 1: the eligibility of the elaboration and the samples of the cycle are read on the
     * main thread, in the order of the channel index (see receiver/dspWorkerPool.h) */
    for (index = 0; index < cfg->numberOfChannels; index++) {
        ch = &(rx->channelArray[index]);
        GetSamplePointersForChannel(ch, &(rx->buf), &firstBunch, &firstBunchSize, &secondBunch,
                                    &secondBunchSize);
        PrepareChannelCycle(cfg, rx, ch, firstBunch, firstBunchSize, secondBunch, secondBunchSize);
    }
    /* phase 2: the DSP of the channels is computed by the workers of the pool. The main thread
     * waits there, so the samples elaborated by the channels are discarded only after the commit
     * of all the channels of the cycle (see frontEnd/circularBuffer.c) */
    DispatchDspJobs(&(rx->dspWorkerPool), cfg, rx, cfg->numberOfChannels);
    /* phase 3: the transitions of the state machine and the log of the channels are committed on
     * the main thread, in the order of the channel index */
    for (index = 0; index < cfg->numberOfChannels; index++) {
        ch = &(rx->channelArray[index]);
        if (ch->cycle.elaborates == FALSE_FLAG) {
            continue;
        }
        CommitChannelCycle(cfg, rx, ch);
    }
}

int FindChId(TNGeneConfiguration *cfg, TNGeneData *rx, TSatConstellation satType, int satId)
{
    int chIndex;
    for (chIndex = 0; chIndex < cfg->numberOfChannels; chIndex++) {
        if ((rx->channelArray[chIndex].satType == satType) &&
            (rx->channelArray[chIndex].satId == satId)) {
            return chIndex;
        }
    }
    return -1;
}

void HardRestartChannels(TNGeneConfiguration *cfg, TNGeneData *rx)
{
    int chIndex;
    TChannel *ch;
    int prnIdx;
    for (chIndex = 0; chIndex < cfg->numberOfChannels; chIndex++) {
        ch = &(rx->channelArray[chIndex]);
        // unlock tracking
        if (ch->chState == TRACKING_STATE) {
            FreeInTheVisibilityList(&(cfg->warmConfiguration), &(rx->warmStartData), ch->satType,
                                    ch->satId);
            ReleaseTracking(cfg, &(rx->channelScheduler), ch);
            DeleteChannelFromOsnmaData(&(rx->osnmaData), ch->chIndex);
            ResetChannelAuthenticationStruct(&(rx->osnmaAuth), ch->chIndex);
            ch->chState = POWERON_STATE;
        }
    }
    // discard navigation message
    for (prnIdx = 0; prnIdx < GPS_NUMBER_OF_PRNS; prnIdx++) {
        rx->gpsNavigationMessage[prnIdx].towValid = FALSE_FLAG;
        rx->gpsNavigationMessage[prnIdx].wnValid = FALSE_FLAG;
        rx->gpsNavigationMessage[prnIdx].ephemeridesReceived = FALSE_FLAG;
        ResetEphemerides(&(rx->gpsNavigationMessage[prnIdx].eph));
        ResetClockCorrections(&(rx->gpsNavigationMessage[prnIdx].clk));
        //
        rx->galNavigationMessage[prnIdx].towValid = FALSE_FLAG;
        rx->galNavigationMessage[prnIdx].wnValid = FALSE_FLAG;
        rx->galNavigationMessage[prnIdx].ephemeridesReceived = FALSE_FLAG;
        ResetEphemerides(&(rx->galNavigationMessage[prnIdx].eph));
        ResetClockCorrections(&(rx->galNavigationMessage[prnIdx].clk));
    }
    // end with Galileo
    for (; prnIdx < GALILEO_NUMBER_OF_PRNS; prnIdx++) {
        rx->galNavigationMessage[prnIdx].towValid = FALSE_FLAG;
        rx->galNavigationMessage[prnIdx].wnValid = FALSE_FLAG;
        rx->galNavigationMessage[prnIdx].ephemeridesReceived = FALSE_FLAG;
        ResetEphemerides(&(rx->galNavigationMessage[prnIdx].eph));
        ResetClockCorrections(&(rx->galNavigationMessage[prnIdx].clk));
    }
}
