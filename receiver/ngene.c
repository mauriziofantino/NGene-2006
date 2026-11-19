/**
 * @file ngene.c
 *
 * @author Mario Nicola
 *
 * @date 05/08/2014
 *
 * @version 1.0.0
 *
 * @brief Main file of NGene receiver
 */
#include "ngeneConstants.h"
#include "xmmsBackup.h"
int threadTESLAKeyVerificationStopIsRunning =
    FALSE_FLAG; /**< Flag indicating whether the TESLA Key Verification Thread has finished or not*/
int stopFrontEndFlag =
    FALSE_FLAG; /**< Flag to stop the thread handling the front-end (threadFrontEnd) */
int stopReceiverFlag = FALSE_FLAG; /**< Flag to stop the receiver, in case a USB error occurs */
#if !defined(FAKE_OSNMA_RECEIVER) && !defined(FAKE_CHIMERA_RECEIVER)
// #ifndef FAKE_OSNMA_RECEIVER
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "timeConstants.h"
#include "frontendThreadFunctions.h"
#include "frontend.h"
#include "ngeneConfiguration.h"
#include "manageConfiguration.h"
#include "argvParser.h"
#include "ioFunctions.h"
#include "ngeneInitialization.h"
#include "closeManager.h"
#include "signalHandler.h"
#include "circularBuffer.h"
#include "channelManager.h"
#include "acquisition.h"
#include "usbInterface.h"
#include "pvt.h"
#include "warmStart.h"
#include "ncursesApi.h"
#include "osnmaDataStructures.h"
#include "mackList.h"
#include "osnmaManagement.h"
#include "osnmaValidations.h"
#include "receiverTime.h"
#include "manageLogs.h"
void ManageFrontEndThreadInitialization(void);
void ManageFrontEndThreadDeinitialization(void);
static TNGeneConfiguration ngeneCfg; /**< Contains all the receiver configuration! */
static TNGeneData ngene;             /**< Contains all the receiver data! */
static TRxFe
    ngeneFe; /**< Contains all the information about the input (USB front-end or binary file) */
static TFrontEndThreadFunctionParameters feThFuncParam;
static pthread_t threadFrontEnd;
// int stopFrontEndFlag = FALSE_FLAG; 	/**< Flag to stop the thread handling the front-end
// (threadFrontEnd) */ int stopReceiverFlag = FALSE_FLAG; 	/**< Flag to stop the receiver, in
// case a USB error occurs */
/**
 * @def TIMEOUT_FOR_RX_OUTPUT
 * @brief The RX output is sent to user interface (ncurses or text)
 *        when a new PVT estimation is available.
 *        If PVT is not available, the output is sent after this timeout occurred.
 *        The value is in milliseconds.
 */
#define TIMEOUT_FOR_RX_OUTPUT 10

int main(int argc, char *argv[])
{
    int run = TRUE_FLAG;
    unsigned int samplesNeeded;
    unsigned int samplesToBeDiscarded;
    unsigned int samplesDiscarded;
    unsigned int samplesDiscardedTotal = 0;
    int stopReceiverFlagLocal = FALSE_FLAG;
    unsigned long long int rxCounterLastOutput = 0;
#ifdef OSNMA_PROFILING
    int numberOfMacsVerified = 0;
#endif
#ifdef RX_PROFILING
    int pvtStatus;
    TProfilingSupport receiverLoopProfiling;
    TProfilingSupport receiverIterationProfiling;
    double maximumIterationTime = 0.0;
    unsigned long long profiledSamples = 0;
#endif
    unsigned long long int loopCyclesCounter = 0;
#ifdef MONITOR_INTERMEDIATE_BUFFER
    float percentageIntermediateBufferFull = 0.0;
#endif
    /* prepare the XMM registers' backup of the main thread (every future DSP
     * worker thread must call InitXmmsBackup() at its start) */
    if (InitXmmsBackup() != SUCCESS_RETURN) {
        fprintf(stderr, "Malloc returned a NULL pointer in main");
        exit(EXIT_FAILURE);
    }
#ifdef TEST_PVT_ACCURACY
    double totalTime, timer;
    time_t start, stop;
    timer = atoi(argv[6]);
    time(&start);
#endif
    /* Manage command line options:
     * receiver terminates here if one of -h, -V, -C, -r options is used */
    GetMainName(argv, &ngeneCfg);
    HandleCmdLine(argc, argv, &ngeneCfg);
    /* Preliminary initializations */
    PreparePaths(&ngeneCfg, &ngene);
    PreliminaryInitialization(&ngeneCfg, &ngene);
    /* Parse configuration files */
    ParseConfigurationFiles(&ngeneCfg);
    /* prepare for USB FE initialization */
    feThFuncParam.ngeneConfiguration = &ngeneCfg;
    feThFuncParam.ngeneFrontEnd = &ngeneFe;
    CompleteConfiguration(&ngeneCfg);
    PlotConfiguration(&ngeneCfg);
    if (InitReceiver(&ngeneCfg, &ngene) != SUCCESS_RETURN)
        CloseNGene(&ngeneCfg, EXIT_FAILURE);
    if (ngeneCfg.inputMethod == USB_FRONTEND)
        ManageFrontEndThreadInitialization();
    else {
        InitializeRxFrontEnd(&ngeneCfg, &ngeneFe);
        StartRxFrontEnd(&ngeneCfg, &ngeneFe);
    }
    StartListeningSignals(&ngeneCfg, &run);
/* Execution cycle */
#ifdef RX_PROFILING
    InitProfilingSupport(&receiverLoopProfiling, "Receiver loop (including input waits)");
    InitProfilingSupport(&receiverIterationProfiling, "Receiver iteration");
    StartProfilingUpdate(&receiverLoopProfiling);
#endif
#ifdef OSNMA_PROFILING
    StartProfilingUpdate(&(ngene.osnmaData.mainLoop));
#endif
    while (run == TRUE_FLAG) {
/* Read input samples when needed */
#ifdef RX_PROFILING
        StartProfilingUpdate(&receiverIterationProfiling);
#endif
        samplesNeeded = GetMaximumNumberOfSamplesNeeded(&ngeneCfg, &ngene);
        if (samplesNeeded > (ngene.buf.fill1 + ngene.buf.fill2)) {
            FillInputBuffer(&ngeneCfg, &(ngene.buf), &ngeneFe);
        }
        /* Executes elaboration required by each channel (see receiver/dspWorkerPool.h) */
        RunChannelsCycle(&ngeneCfg, &ngene);
        //***OSNMA: TESLA and MAC verification****************************
        /* Checks about OSNMA */
        if ((ngene.rxCounter % TESLA_THREAD_UPDATE_INTERVAL) == 0) {
            /* Check the presence of results from a terminated thread for the verification of TESLA
             * keys */
            CheckTeslaValidationThread(&ngene.osnmaData);
        }
        if ((ngene.rxCounter % MAC_UPDATE_INTERVAL) == 0) {
/* Check the presence of MACs to be validated.
 * Proceed with the validation if the MAC, the key, and the navigation message are available */
#ifdef OSNMA_PROFILING
            CheckOsnmaDsmTeslaMacseq(&ngeneCfg, &ngene);
            StartProfilingUpdate(&(ngene.osnmaData.allMacsProfiling));
            numberOfMacsVerified = MACVerificationAll(&ngeneCfg, &ngene);
            if (numberOfMacsVerified > 0) {
                StopProfilingUpdate(&(ngene.osnmaData.allMacsProfiling), 1);
                ManageOsnmaAuthentication(ngene.rxCounter, &(ngene.osnmaData), &(ngene.osnmaAuth));
                PredictRxTime(&ngeneCfg, &ngene);
                SaveAuthenticationLog(ngene.pvtData.rxGalWn, ngene.pvtData.rxGalTow,
                                      &(ngene.osnmaData), &(ngene.osnmaAuth));
            } else {
                StopProfilingUpdate(&(ngene.osnmaData.allMacsProfiling), 0);
            }
            if ((ngene.rxCounter % (OSNMA_SIMPLE_LOG_UPDATE_INTERVAL * SECONDS_TO_MILLISECONDS)) ==
                0) {
                ManageOsnmaTimeout(&ngeneCfg, &ngene);
            }
#else
            CheckOsnmaDsmTeslaMacseq(&ngeneCfg, &ngene);
            if (MACVerificationAll(&ngeneCfg, &ngene) > 0) {
                // if new MACs are validated, analyze the validation results
                ManageOsnmaAuthentication(ngene.rxCounter, &(ngene.osnmaData), &(ngene.osnmaAuth));
                PredictRxTime(&ngeneCfg, &ngene);
                SaveAuthenticationLog(ngene.pvtData.rxGalWn, ngene.pvtData.rxGalTow,
                                      &(ngene.osnmaData), &(ngene.osnmaAuth));
            }
            if ((ngene.rxCounter %
                 (OSNMA_SIMPLE_LOG_UPDATE_INTERVAL * (int)SECONDS_TO_MILLISECONDS)) == 0) {
                ManageOsnmaTimeout(&ngeneCfg, &ngene);
            }
#endif
        }
        // manage OSNMA simplified log
        if ((ngene.rxCounter % TESLA_THREAD_UPDATE_INTERVAL) == 0) {
            PredictRxTime(&ngeneCfg, &ngene);
            SaveOsnmaSimpleLog(&ngeneCfg, &ngene);
        }
        //****************************************************************
        /* Discard samples elaborated by all the channels (if possible) */
        samplesToBeDiscarded = GetNumberOfUsedSamples(&ngeneCfg, &ngene);
        samplesDiscarded = DeleteFromCircularBuffer(&(ngene.buf), samplesToBeDiscarded);
#ifdef RX_PROFILING
        profiledSamples += samplesDiscarded;
#endif
        CleanBufferOffsetForAllChannels(&ngeneCfg, &ngene, samplesDiscarded);
        /* Update acquisition threshold */
        if (ngene.rxCounter % INPUT_BUFFER_SIZE == 0) {
            UpdateVariablesForConfirmationThresholds(&ngeneCfg, &ngene);
        }
        /* Update the receiver execution counter:
         * only when at least 1 ms of samples have been discarded from the circular buffer */
        samplesDiscardedTotal += samplesDiscarded;
        if (samplesDiscardedTotal > (unsigned int)ngeneCfg.samplesPerMillisecond) {
            ngene.rxCounter++;
            samplesDiscardedTotal -= (unsigned int)ngeneCfg.samplesPerMillisecond;
            /* update the receiver time emulation if it is valid (used for Galileo OSNMA) */
            if (ngene.osnmaData.receiverClockTow != -1) {
                ngene.osnmaData.receiverClockTow += 1e-3;
                if (ngene.osnmaData.receiverClockTow > SECONDS_PER_WEEK) {
                    ngene.osnmaData.receiverClockTow -= SECONDS_PER_WEEK;
                    ngene.osnmaData.receiverClockWn++;
                }
            }
        }
/* PVT (it manages the PVT rate) */
#ifdef RX_PROFILING
        StartProfilingUpdate(&(ngene.pvtProfiling));
        pvtStatus = PvtManager(&ngeneCfg, &ngene);
        if (pvtStatus == SUCCESS_RETURN && ngene.pvtTime == TRUE_FLAG) {
            StopProfilingUpdate(&(ngene.pvtProfiling), 1);
        }
#else
        PvtManager(&ngeneCfg, &ngene);
#endif
        /* Update the list of visible satellites (for the optional warm start) */
        UpdateVisibilityList(&ngeneCfg, &ngene);
        /* Console output */
        // execute when a new PVT result is available or
        // a PVT period (plus some margin) is expired
        if (((ngene.pvtResults.pvtValid == TRUE_FLAG) &&
             (ngene.pvtResults.pvtPlotted == FALSE_FLAG)) ||
            (ngene.rxCounter >=
             (rxCounterLastOutput + ngeneCfg.pvtPeriodMilliseconds + TIMEOUT_FOR_RX_OUTPUT))) {
            // Update the counter for the last output
            if ((ngene.pvtResults.pvtValid == TRUE_FLAG) &&
                (ngene.pvtResults.pvtPlotted == FALSE_FLAG)) {
                // Here because a new PVT estimation is  available
                rxCounterLastOutput = ngene.rxCounter;
            } else {
                // Here because a timeout occurred
                rxCounterLastOutput = ngene.rxCounter - TIMEOUT_FOR_RX_OUTPUT;
            }
            // In function of the selected interface
            if (ngeneCfg.userInterface == RX_UI_NCURSES) {
                // -n
                NcCompleteLog(&ngeneCfg, &ngene);
            } else {
                // -t
                if ((ngene.pvtResults.pvtValid == FALSE_FLAG) ||
                    (ngene.pvtResults.pvtPlotted == TRUE_FLAG)) {
                    SwrxLog(NGENE_LOG_SEVERITY_IMPORTANT_INFO, "RX Counter %llu", ngene.rxCounter);
                }
                ngene.pvtResults.pvtPlotted = TRUE_FLAG;
            }
        }
        /* Other outputs and split of the log files */
        if ((ngene.rxCounter % ngeneCfg.pvtPeriodMilliseconds) == 0) {
            if (ngeneCfg.inputMethod == USB_FRONTEND) {
#ifdef MONITOR_INTERMEDIATE_BUFFER
                if ((ngeneFe.usbFe.usbIntermediateBufferWriteIndex >=
                     ngeneFe.usbFe.usbIntermediateBufferReadIndex) &&
                    (ngeneFe.usbFe.flagBufferFull == FALSE_FLAG)) {
                    percentageIntermediateBufferFull =
                        (float)(ngeneFe.usbFe.usbIntermediateBufferWriteIndex -
                                ngeneFe.usbFe.usbIntermediateBufferReadIndex) /
                        (float)USB_INTERMEDIATE_BUFFER_SIZE * 100.0;
                } else {
                    percentageIntermediateBufferFull =
                        (float)(1 - (ngeneFe.usbFe.usbIntermediateBufferReadIndex -
                                     ngeneFe.usbFe.usbIntermediateBufferWriteIndex) /
                                        (float)USB_INTERMEDIATE_BUFFER_SIZE) *
                        100.0;
                }
                if (ngeneFe.usbFe.percentageIntBufFullFile != NULL) {
                    fwrite(&percentageIntermediateBufferFull, sizeof(float), 1,
                           ngeneFe.usbFe.percentageIntBufFullFile);
                }
                if (percentageIntermediateBufferFull >= CRITICAL_THRESHOLD) {
                    SwrxLog(NGENE_LOG_SEVERITY_IMPORTANT_INFO, "Real time capability = %f %%",
                            100.00 - percentageIntermediateBufferFull);
                }
#endif
            }
            // split of the log files
            ManageLogSplitter(&ngeneCfg, &ngene);
        }
/* Stop for debug purposes */
#ifdef RX_PROFILING
        StopProfilingUpdate(&receiverIterationProfiling, 1);
        if (receiverIterationProfiling.totalTimeS > maximumIterationTime)
            maximumIterationTime = receiverIterationProfiling.totalTimeS;
        receiverIterationProfiling.totalTimeS = 0.0;
#endif
        if (AllChannelsUnused(&ngeneCfg, &ngene) == TRUE_FLAG)
            run = FALSE_FLAG;
#ifdef TEST_PVT_ACCURACY
        time(&stop);
        totalTime = difftime(stop, start);
        if (totalTime >= timer) {
            run = FALSE_FLAG;
            SwrxLog(NGENE_LOG_SEVERITY_INFO, "Total elapsed time: %f seconds", totalTime);
        }
#endif
        /* Verifies the status of the input (file or USB front-end)
         * and the stop command by the user (CTRL-C) */
        if (ngeneCfg.inputMethod == USB_FRONTEND) {
            pthread_mutex_lock(&(ngeneFe.usbFe.threadsMutexStopReceiver));
            stopReceiverFlagLocal = stopReceiverFlag;
            pthread_mutex_unlock(&(ngeneFe.usbFe.threadsMutexStopReceiver));
        } else if (FrontEndCheckStatus(&ngeneFe) == FAILURE_RETURN) {
            stopReceiverFlagLocal = TRUE_FLAG;
        }
        /* Manages the termination of the elaboration */
        if ((run == FALSE_FLAG) || (stopReceiverFlagLocal == TRUE_FLAG)) {
            SwrxLog(NGENE_LOG_SEVERITY_INFO,
                    "NGene main thread exiting with run=%d and stopReceiverFlagLocal=%d", run,
                    stopReceiverFlagLocal);
            /* Deinitialize the Front-end thread */
            if (ngeneCfg.inputMethod == USB_FRONTEND) {
                ManageFrontEndThreadDeinitialization();
            } else {
                StopRxFrontEnd(&ngeneFe);
                DeinitializeRxFrontEnd(&ngeneCfg, &ngeneFe);
            }
            /* Sleep in order to allow the user a last look of the NCURSES interface */
            if (ngeneCfg.userInterface == RX_UI_NCURSES) {
                sleep(NCURSES_SLEEP);
            }
            /* Terminate the execution cycle */
            run = FALSE_FLAG;
        }
        loopCyclesCounter++;
    }
#ifdef OSNMA_PROFILING
    ngene.osnmaData.mainLoop.totalTimeS -= ngene.osnmaData.teslaProfiling.totalTimeS;
    StopProfilingUpdate(&(ngene.osnmaData.mainLoop), loopCyclesCounter);
    ngene.osnmaData.saveMackInMackListExcludingTESLA.totalTimeS =
        ngene.osnmaData.saveMackInMackList.totalTimeS - ngene.osnmaData.teslaProfiling.totalTimeS;
    ngene.osnmaData.saveMackInMackListExcludingTESLA.numberOfExecutions =
        ngene.osnmaData.saveMackInMackList.numberOfExecutions;
#endif
#ifdef RX_PROFILING
    StopProfilingUpdate(&receiverLoopProfiling, 1);
    ReportProfilingData(&receiverLoopProfiling);
    SwrxLog(NGENE_LOG_SEVERITY_IMPORTANT_INFO,
            "Receiver throughput: %llu samples in %.6f s (%.3f Msamples/s)", profiledSamples,
            receiverLoopProfiling.totalTimeS,
            receiverLoopProfiling.totalTimeS > 0.0
                ? profiledSamples / receiverLoopProfiling.totalTimeS / 1e6
                : 0.0);
    SwrxLog(NGENE_LOG_SEVERITY_IMPORTANT_INFO,
            "Receiver maximum processing iteration (including input waits): %.3f ms",
            maximumIterationTime * 1000.0);
#endif
    /* Deinitialize the receiver */
    DeinitReceiver(&ngeneCfg, &ngene);
    DeinitializeRxFrontEnd(&ngeneCfg, &ngeneFe);
    DeleteConfiguration(&ngeneCfg);
    DeallocateXmmsBackup();
    return 0;
}

void ManageFrontEndThreadInitialization(void)
{
    if (ngeneCfg.inputMethod == USB_FRONTEND) {
#ifdef MONITOR_INTERMEDIATE_BUFFER
        InitUsbFrontEndThreadFunction(&ngeneFe, &feThFuncParam, &ngeneCfg);
#else
        InitUsbFrontEndThreadFunction(&ngeneFe, &feThFuncParam);
#endif
        pthread_create(&threadFrontEnd, NULL, (void *)UsbFrontEndThreadFunction,
                       (void *)&feThFuncParam);
    }
    // else do nothing
}

void ManageFrontEndThreadDeinitialization(void)
{
    if (ngeneCfg.inputMethod == USB_FRONTEND) {
        pthread_mutex_lock(&(ngeneFe.usbFe.threadsMutexStopFrontEnd));
        stopFrontEndFlag = TRUE_FLAG;
        pthread_mutex_unlock(&(ngeneFe.usbFe.threadsMutexStopFrontEnd));
        /* If the front-end thread is blocked on a cond_wait, we need to release it */
        pthread_mutex_lock(&(ngeneFe.usbFe.frontEndMutex));
        ngeneFe.usbFe.shutdownRequested = TRUE_FLAG;
        pthread_cond_broadcast(&(ngeneFe.usbFe.intermediateBufferFull));
        pthread_cond_broadcast(&(ngeneFe.usbFe.intermediateBufferEmpty));
        pthread_mutex_unlock(&(ngeneFe.usbFe.frontEndMutex));
        pthread_join(threadFrontEnd, NULL);
        DeinitUsbFrontEndThreadFunction(&ngeneFe, &feThFuncParam);
    }
    // else do nothing
}
#endif
