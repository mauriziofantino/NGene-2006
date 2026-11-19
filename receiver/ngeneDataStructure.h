/**
 * @file ngeneDataStructure.h
 *
 * @author Mario Nicola
 *
 * @date 12/08/2014
 *
 * @version 1.0.0
 *
 * @brief Declares the basic data structure of the receiver.
 */
#ifndef NGENE_DATA_STRUCTURE_H_INCLUDED
#define NGENE_DATA_STRUCTURE_H_INCLUDED
#include "ngeneConstants.h"
#include "circularBuffer.h"
#include "dspContextDataStructure.h"
#include "prnTable.h"
#include "carrierTable.h"
#include "fft.h"
#include "acquisitionDataStructure.h"
#include "channelDataStructure.h"
#include "warmStartDataStructure.h"
#include "navigationMessageDataStructure.h"
#include "egnosCorrectionsDataStructure.h"
#include "pvtDataStructure.h"
#include "ncursesDataStructure.h"
#include "rinexDataStructure.h"
#include "osnmaDataStructures.h"
#include "osnmaValidationsDataStructures.h"
#include "logSplitterDataStructure.h"
/**
 * @def NGENE_ARRAYS_SIZE
 * @brief The size of the arrays in the NGene structure (despreading code, carrier) in ms
 */
#define NGENE_ARRAYS_SIZE 2
/**
 * @struct NGeneDataStruct
 *
 * @brief Contains all the basic data for the NGene receiver
 */
typedef struct NGeneDataStruct {
    int dataValid; /**< if FALSE_FLAG, the de-initialization function has already been executed */
    unsigned long long int rxCounter; /**< counts the execution cycles of the receiver (about, the
                                         number of ms from the start ) */
    TDspContext *dsp; /**< per DSP worker scratch (code, carrier, subcarrier, demodulated,
                        wide I/Q); Stage 1: the receiver owns a single context */
    THackrfMixer hackrfMixer; /**< HackRF unit-circle Q15 NCO coefficients. */
    TCircularBuffer buf;       /**< buffer storing input samples */
    TPrnTable prnTable;        /**< table used to generate despreading codes at fs */
    TSinCosTable fifTable;     /**< table used to generate carrier */
    TSinCosTable fscTable;     /**< table used to generate the subcarrier for Galileo E1b */
    TFftTable fftTable;        /**< table used to perform FFT and IFFT */
    TAcqCommon
        acqCommon; /**< structure used to store acquisition variables common to all the channels */
    TChannelScheduler channelScheduler; /**< structure used to manage channel allocation */
    TChannel *channelArray;             /**< points to all the channels */
    /* Warm Start */
    TWarmStartData warmStartData; /**< data used to manage the optional warm start */
    /* Navigation message */
    int gpsLastDecodedPage;     /**< index of the page for the last decoded GPS subframe */
    int gpsLastDecodedSubframe; /**< index of the last decoded GPS subframe */
    TNavigationMessage *gpsNavigationMessage; /**< The navigation message (and almanac) for all the
                                                 supported GPS PRNs */
    int gpsIonoValid;                         /**< TRUE_FLAG when gpsIono is valid */
    TGnssIono gpsIono; /**< stores the ionosphere correction in a data structure common to all GPS
                          satellites */
    int galLastDecodedPage;     /**< index of the subframe for the last decoded Galileo word */
    int galLastDecodedSubframe; /**< index of the last decoded Galileo word */
    TNavigationMessage *galNavigationMessage;  /**< The I/NAV navigation message (and almanac) for
                                                  all the supported Galileo PRNs */
    TNavigationMessage *fnavNavigationMessage; /**< The F/NAV navigation message (and almanac) for
                                                  all the supported Galileo PRNs */
    int galIonoValid;                          /**< TRUE_FLAG when galIono is valid */
    TGnssIono galIono; /**< stores the ionosphere correction in a data structure common to all
                          Galileo satellites */
    TEgnosCorrections egnosCorrections; /**< Stores the corrections read from the EGNOS message */
    /* OSNMA */
    TOsnmaData osnmaData; /**< All variables used to manage OSNMA */
    TOsnmaAuthentication
        osnmaAuth; /**< All variables used to manage the authentications obtained through OSNMA */
    /* PVT computation */
    TPvtData pvtData; /**< This structure contains all the information related to PVT computation */
    TPvtResults pvtTemporaryResults; /**< This structure contains the results of the PVT before the
                                        check of their goodness is performed */
    TPvtResults pvtResults; /**< This structure contains the results of the PVT computation */
    /* Other logs */
    TLogSplitter logSplitter;      /**< Used to manage the splitting of the receiver logs */
    FILE *trkCorrelatorsFile;      /**< File where all the correlations from tracking are saved */
    FILE *trkMultiCorrelatorsFile; /**< File where all the multiple correlations from tracking are
                                      saved */
    TRinexLog rinexGps; /**< Variable used to save the RINEX navigation file for the GPS navigation
                           message (and ALL the observation data) */
    TRinexLog rinexGal; /**< Variable used to save the RINEX navigation file for the Galileo
                           navigation message */
    FILE *nmeaFile;     /**< File where the NMEA sentences are saved */
    char *trkLogArray;  /**< Array used to write the tracking related logs */
    TNCursesData ncursesData; /**< Variables used to manage the NCurses interface */
#ifdef RX_PROFILING
    TProfilingSupport pvtProfiling; /**< profiling of the PVT */
    int pvtTime;                    /**< flag indicating if it's time for a new PVT computation*/
#endif
} TNGeneData;
#endif /* NGENE_DATA_STRUCTURE_H_INCLUDED */
