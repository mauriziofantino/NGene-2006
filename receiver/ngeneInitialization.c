/**
 * @file ngeneInitialization.c
 *
 * @author Mario Nicola
 *
 * @date 12/08/2014
 *
 * @version 1.0.0
 *
 * @brief Functions used to initialize/de-initialize the receiver.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include "ngeneConfiguration.h"
#include "ngeneDataStructure.h"
#include "ngeneConstants.h"
#include "circularBuffer.h"
#include "prnTable.h"
#include "carrierTable.h"
#include "spreadingCodes.h"
#include "fft.h"
#include "ioFunctions.h"
#include "closeManager.h"
#include "arrays.h"
#include "acquisition.h"
#include "channelDataStructure.h"
#include "channelManager.h"
#include "navigationMessage.h"
#include "warmStart.h"
#include "egnosMessageParser.h"
#include "egnosCorrections.h"
#include "pvt.h"
#include "ncursesApi.h"
#include "viterbi.h"
#include "reedSolomon.h"
#include "tracking.h"
#include "rinexLog.h"
#include "prnList.h"
#include "osnmaManagement.h"
#include "megaBuffer.h"
#include "checkFolder.h"
#include "osnmaValidations.h"
#include "manageLogs.h"

void PreparePaths(TNGeneConfiguration *cfg, TNGeneData *rx)
{
    const char *homeFolder = GetExecutableDirectory();
    char cfgFilename[MAX_ROW_LENGTH];
    char tag[MAX_TAG_LEN];
    int parseReturn;
    char logFilename[MAX_ROW_LENGTH];
    if (!homeFolder || strlen(homeFolder) + 32 >= MAX_ROW_LENGTH) {
        fprintf(stderr, "Cannot determine the folder containing the NGene executable.\n");
        exit(EXIT_FAILURE);
    }
    cfg->sudoer = FALSE_FLAG;
    snprintf(cfg->homePath, sizeof(cfg->homePath), "%s", homeFolder);
    snprintf(cfg->configurationPath, sizeof(cfg->configurationPath), "%s/configuration/",
             homeFolder);
    snprintf(cfg->otherDataPath, sizeof(cfg->otherDataPath), "%s/otherData/", homeFolder);
    /* check if the configuration folder does exist (only when -s is not specified) */
    if ((IsFolderExistent(cfg->configurationPath) == FALSE_FLAG) ||
        (IsFolderExistent(cfg->otherDataPath) == FALSE_FLAG)) {
        fprintf(stderr, "Required folders are missing: %s and %s\n", cfg->configurationPath,
                cfg->otherDataPath);
#ifdef NGENE_NATIVE_WINDOWS
        fprintf(stderr, "Run build.cmd to prepare the configuration and data beside ngene.exe.\n");
#else
        fprintf(stderr, "Run sh build.sh to prepare the configuration and data beside ngene.\n");
#endif
        exit(EXIT_FAILURE);
    }
    /* get the folder for the logs */
    // chosen by the user
    cfgFilename[0] = '\0';
    strcat(cfgFilename, cfg->configurationPath);
    strcat(cfgFilename, "Logs.conf");
    strcpy(tag, "Dump Path");
    parseReturn = GetVal(cfgFilename, tag, (void *)&(cfg->dumpPath), F_STRING, 1);
    if ((parseReturn == SUCCESS_RETURN) && (IsFolderExistent(cfg->dumpPath) == FALSE_FLAG)) {
        parseReturn = FAILURE_RETURN;
    }
    /* Default logs have a fixed location beside the executable too. */
    if (parseReturn == FAILURE_RETURN) {
        if (strlen(homeFolder) + 6 >= sizeof(cfg->dumpPath)) {
            fprintf(
                stderr,
                "The default log path is too long. Set Dump Path in configuration/Logs.conf.\n");
            exit(EXIT_FAILURE);
        }
        snprintf(cfg->dumpPath, sizeof(cfg->dumpPath), "%s/logs", homeFolder);
#ifdef NGENE_NATIVE_WINDOWS
        if (!CreateDirectoryA(cfg->dumpPath, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
            fprintf(stderr, "Cannot create log folder %s (Windows error %lu).\n", cfg->dumpPath,
                    GetLastError());
            exit(EXIT_FAILURE);
        }
#else
        if (mkdir(cfg->dumpPath, 0755) != 0 && errno != EEXIST) {
            fprintf(stderr, "Cannot create log folder %s: %s.\n", cfg->dumpPath, strerror(errno));
            exit(EXIT_FAILURE);
        }
#endif
    }
    // else dumpPath already contains the folder!
    // set the current dumpPath
    sprintf(cfg->dumpPathCurrent, "%s", cfg->dumpPath);
    /* Init log splitter */
    InitLogSplitter(cfg, rx);
    /* prepare log */
    cfg->ncursesData = NULL;
    cfg->ncursesDataInitialized = FALSE_FLAG;
    // Open the log file in the folder specified by cfg->dumpPath!
    // (if needed, i.e. if cfg->logFilename is not an empty string)
    if (strlen(cfg->logFilename) > 0) {
        logFilename[0] = '\0';
        strcat(logFilename, cfg->dumpPathCurrent);
        strcat(logFilename, "/");
        strcat(logFilename, cfg->logFilename);
        cfg->stdlog = fopen(logFilename, "a+");
        if (cfg->stdlog == NULL) {
            cfg->stdlog = stdout;
            fprintf(stderr, "Parsing: can't create log file, printing on the console\n");
        }
    }
    // The completion of the GoogleEarth filename is managed in InitPvtData
}

int PreliminaryInitialization(TNGeneConfiguration *cfg, TNGeneData *rx)
{
    /* NCurses must be initialized after the number of channels is known!!! */
    /* prepare the management of generic text logs through the SwrxLog function */
    InitSwrxLog(cfg, rx);
    return SUCCESS_RETURN;
}

int InitReceiver(TNGeneConfiguration *cfg, TNGeneData *rx)
{
    int errorsFound = 0;
    int index;
    TChannel *ch;
    char filename[MAX_ROW_LENGTH];
    /* NCurses */
    // NCurses must be initialized here because the number of channel is known (now)
    if (cfg->userInterface == RX_UI_NCURSES) {
        InitNCurses(cfg, rx, TRUE_FLAG);
        cfg->ncursesDataInitialized = TRUE_FLAG;
        /* update the management of the generic text logs through the SwrxLog function */
        InitSwrxLog(cfg, rx);
    }
    rx->rxCounter = 0;
    rx->dsp = (TDspContext *)malloc(sizeof(TDspContext));
    if (rx->dsp == NULL || InitDspContext(cfg, rx->dsp) != SUCCESS_RETURN)
        errorsFound++;
    /* the workers of the pool elaborate the channels of one execution cycle in parallel, each
     * one with its own scratch and its own backup of the XMM registers (see
     * receiver/dspWorkerPool.h) */
    if (InitDspWorkerPool(cfg, &(rx->dspWorkerPool), cfg->numberOfDspWorkers) != SUCCESS_RETURN)
        errorsFound++;
    rx->hackrfMixer.coefficients = NULL;
    if (cfg->usedFrontEnd == NGENE_FE_HACKRF) {
        if (!InitHackrfMixer(&rx->hackrfMixer))
            errorsFound++;
    }
    if (errorsFound > 0) {
        SwrxLog(NGENE_LOG_SEVERITY_ERROR, "Malloc returned a NULL pointer in InitReceiver");
        CloseNGene(cfg, EXIT_FAILURE);
    }
    /* other initializations */
    if (InitCircularBuffer(cfg, &(rx->buf)) != SUCCESS_RETURN)
        return FAILURE_RETURN;
    if (InitPrnTable(cfg, &(rx->prnTable)) != SUCCESS_RETURN)
        return FAILURE_RETURN;
    if (InitCarrierTable(cfg, &(rx->fifTable), (double)cfg->fif) != SUCCESS_RETURN)
        return FAILURE_RETURN;
    if (InitCarrierTable(cfg, &(rx->fscTable), (double)GALILEO_CODE_RATE) != SUCCESS_RETURN)
        return FAILURE_RETURN;
    if (InitFftTable(cfg, &(rx->fftTable)) != SUCCESS_RETURN)
        return FAILURE_RETURN;
    if (InitChannelScheduler(cfg, &(rx->channelScheduler), rx->prnTable.numberOfRealGalileoSat,
                             rx->prnTable.prnOfRealGalileoSat) != SUCCESS_RETURN)
        return FAILURE_RETURN;
    if (InitAcquisitionCommon(cfg, rx) != SUCCESS_RETURN)
        return FAILURE_RETURN;
    InitViterbi();
    InitReedSolomon();
    if (InitNavigationMessage(cfg, rx) != SUCCESS_RETURN)
        return FAILURE_RETURN;
    InitWarmStartData(cfg, rx);
    if (InitEgnosCorrections(cfg, &(rx->egnosCorrections)) != SUCCESS_RETURN)
        return FAILURE_RETURN;
    InitOsnmaData(cfg, &(rx->osnmaData));
    InitOsnmaAuthenticationStruct(cfg, &(rx->osnmaAuth));
    if (InitPvtData(cfg, rx) != SUCCESS_RETURN)
        return FAILURE_RETURN;
    /* channels */
    rx->channelArray = (TChannel *)malloc(cfg->numberOfChannels * sizeof(TChannel));
    if (rx->channelArray == NULL) {
        SwrxLog(NGENE_LOG_SEVERITY_ERROR, "Malloc returned a NULL pointer in InitReceiver");
        CloseNGene(cfg, EXIT_FAILURE);
    }
    for (index = 0; index < cfg->numberOfChannels; index++) {
        ch = &(rx->channelArray[index]);
        ch->chIndex = index;
        if (cfg->automaticConfigurationFlag == FALSE_FLAG) {
            ch->satType = cfg->manuallyConfiguredChannels[index].satType;
            if (ch->satType == GPS_SAT)
                ch->satSignal = L1_CA;
            else if (ch->satType == GALILEO_SAT)
                ch->satSignal = E1_BC;
            else // if(ch->satType==EGNOS_SAT)
                ch->satSignal = L1;
            ch->satId = INVALID_PRN;
        } else {
            if (index < cfg->numberOfGpsChannels) {
                ch->satType = GPS_SAT;
                ch->satSignal = L1_CA;
            } else if (index < (cfg->numberOfGpsChannels + cfg->numberOfGalileoChannels)) {
                ch->satType = GALILEO_SAT;
                ch->satSignal = E1_BC;
            } else if (index < (cfg->numberOfGpsChannels + cfg->numberOfGalileoChannels +
                                cfg->numberOfGalileoE5aChannels)) {
                ch->satType = GALILEO_SAT;
                ch->satSignal = E5_A;
            } else {
                ch->satType = EGNOS_SAT;
                ch->satSignal = L1;
            }
            ch->satId = INVALID_PRN;
        }
        FromPrnToString(ch->satType, ch->satId, ch->prnName);
        InitChannel(cfg, rx, ch);
    }
    /*** logs ****/
    if (cfg->logConfiguration.enableTrkLog == TRUE_FLAG) {
        filename[0] = '\0';
        strcat(filename, cfg->dumpPathCurrent);
        strcat(filename, "/trackingCorrelators.bin");
        rx->trkCorrelatorsFile = fopen(filename, "wb");
        if (rx->trkCorrelatorsFile == NULL) {
            SwrxLog(NGENE_LOG_SEVERITY_NOTE,
                    "The file trackingCorrelators.bin has not been correctly created!");
        }
    } else {
        rx->trkCorrelatorsFile = NULL;
    }
    /* tracking multiple correlations file */
    if ((cfg->logConfiguration.enableTrkLog == TRUE_FLAG) &&
        (cfg->trkConfiguration.multipleCorrelatorsFlag == TRUE_FLAG)) {
        filename[0] = '\0';
        strcat(filename, cfg->dumpPathCurrent);
        strcat(filename, "/trackingMultiCorrelators.bin");
        rx->trkMultiCorrelatorsFile = fopen(filename, "wb");
        if (rx->trkMultiCorrelatorsFile == NULL) {
            SwrxLog(NGENE_LOG_SEVERITY_NOTE,
                    "The file trackingMultiCorrelators.bin has not been correctly created!");
        } else {
            fwrite(&(cfg->trkConfiguration.numberOfMultipleCorrelators), sizeof(int), 1,
                   rx->trkMultiCorrelatorsFile);
            fwrite(&(cfg->trkConfiguration.multipleCorrelatorsGpsSpacing), sizeof(int), 1,
                   rx->trkMultiCorrelatorsFile);
            fwrite(&(cfg->trkConfiguration.multipleCorrelatorsGalileoSpacing), sizeof(int), 1,
                   rx->trkMultiCorrelatorsFile);
            fwrite(&(cfg->trkConfiguration.multipleCorrelatorsEgnosSpacing), sizeof(int), 1,
                   rx->trkMultiCorrelatorsFile);
        }
    } else {
        rx->trkMultiCorrelatorsFile = NULL;
    }
    if ((TRACKING_LOG_SET_SIZE * sizeof(double)) >
        (((2 * cfg->trkConfiguration.numberOfMultipleCorrelators) + 4) * sizeof(float))) {
        rx->trkLogArray = (char *)malloc(TRACKING_LOG_SET_SIZE * sizeof(double));
    } else {
        rx->trkLogArray = (char *)malloc(
            ((2 * cfg->trkConfiguration.numberOfMultipleCorrelators) + 4) * sizeof(float));
    }
    /* finish */
    rx->dataValid = TRUE_FLAG;
#ifdef RX_PROFILING
    InitProfilingSupport(&(rx->pvtProfiling), "PVT");
    rx->pvtTime = FALSE_FLAG;
#endif
    SwrxLog(NGENE_LOG_SEVERITY_NOTE, "Receiver initialized!");
    return SUCCESS_RETURN;
}

void DeinitReceiver(TNGeneConfiguration *cfg, TNGeneData *rx)
{
    int index;
    if (rx->dataValid == TRUE_FLAG) {
#ifdef RX_PROFILING
        ReportProfilingData(&(rx->pvtProfiling));
#endif
        DeinitDspWorkerPool(&(rx->dspWorkerPool));
        DeinitDspContext(rx->dsp);
        free(rx->dsp);
        DeinitHackrfMixer(&rx->hackrfMixer);
        DeinitCircularBuffer(&(rx->buf));
        DeinitPrnTable(&(rx->prnTable));
        DeinitCarrierTable(&(rx->fifTable));
        DeinitCarrierTable(&(rx->fscTable));
        DeinitFftTable(&(rx->fftTable));
        DeinitAcquisitionCommon(&(rx->acqCommon));
        DeinitChannelScheduler(&(rx->channelScheduler));
        DeinitReedSolomon();
        DeinitNavigationMessage(cfg, rx);
        DeinitWarmStartData(&(rx->warmStartData));
        DeinitEgnosCorrections(&(rx->egnosCorrections));
        DeinitOsnmaData(&(rx->osnmaData));
        DeinitOsnmaAuthenticationStruct(&(rx->osnmaAuth));
        DeinitPvtData(rx);
        for (index = 0; index < cfg->numberOfChannels; index++) {
            DenitChannel(&(rx->channelArray[index]));
        }
        free(rx->channelArray);
        if (rx->trkCorrelatorsFile != NULL) {
            fclose(rx->trkCorrelatorsFile);
        }
        if (rx->trkMultiCorrelatorsFile != NULL) {
            fclose(rx->trkMultiCorrelatorsFile);
        }
        free(rx->trkLogArray);
        DeinitRinexLog(&(rx->rinexGps));
        DeinitRinexLog(&(rx->rinexGal));
        if (rx->nmeaFile != NULL) {
            fclose(rx->nmeaFile);
            rx->nmeaFile = NULL;
        }
        rx->dataValid = FALSE_FLAG;
        SwrxLog(NGENE_LOG_SEVERITY_NOTE, "Receiver deinitialized!");
        /* NCurses */
        if (cfg->userInterface == RX_UI_NCURSES) {
            DeinitNCurses(&(rx->ncursesData));
        }
    }
}
