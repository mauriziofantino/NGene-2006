/**
 * @file gpsTracking.h
 *
 * @author Mario Nicola
 *
 * @date 04/09/2014
 *
 * @version 1.0.0
 *
 * @brief Functions used to perform tracking for GPS and EGNOS.
 */
#ifndef GPS_TRACKING_H_INCLUDED
#define GPS_TRACKING_H_INCLUDED
#include "ngeneConfiguration.h"
#include "ngeneDataStructure.h"
#include "channelDataStructure.h"
#include "trackingDataStructure.h"

/**
 * @brief Computes the elaboration of one millisecond for a GPS or EGNOS PRN
 * @details Performs the carrier wipe-off, the generation of the despreading code replicas and
 *          the correlations of one millisecond. It is the first half of the elaboration of the
 *          channel: only the data of the channel and the scratch of the calling DSP worker are
 *          touched, so the channels of one execution cycle can be elaborated in parallel
 *          (see receiver/dspWorkerPool.c). The result is committed by CommitGpsTracking().
 *
 * @param cfg points to the receiver configuration
 * @param rx points to the receiver base data structure
 * @param ch points to the current channel
 * @param firstBunch is the address of the first sample to be elaborated
 * @param firstBunchSize is the number of samples starting from firstBunch
 * @param secondBunch is the address of the first sample in the second bunch of samples (if any)
 * @param secondBunchSize is the number of samples starting from secondBunch (if any)
 */
void ComputeGpsTracking(TNGeneConfiguration *cfg, TNGeneData *rx, TChannel *ch, char *firstBunch,
                        unsigned int firstBunchSize, char *secondBunch,
                        unsigned int secondBunchSize);

/**
 * @brief Commits the elaboration of one millisecond for a GPS or EGNOS PRN
 * @details Analyses the correlations computed by ComputeGpsTracking(), evaluates the tracking
 *          lock and the loops, decodes the navigation message and updates the correlations.
 *          Called by the main thread, so the shared state of the receiver is updated in the
 *          same order as the single threaded execution (see receiver/channelManager.c).
 *
 * @param cfg points to the receiver configuration
 * @param rx points to the receiver base data structure
 * @param ch points to the current channel
 */
void CommitGpsTracking(TNGeneConfiguration *cfg, TNGeneData *rx, TChannel *ch);

/**
 * @brief Performs tracking for a GPS or EGNOS PRN
 * @details Elaborates one millisecond of samples.
 *
 * @param cfg points to the receiver configuration
 * @param rx points to the receiver base data structure
 * @param ch points to the current channel
 * @param firstBunch is the address of the first sample to be elaborated
 * @param firstBunchSize is the number of samples starting from firstBunch
 * @param secondBunch is the address of the first sample in the second bunch of samples (if any)
 * @param secondBunchSize is the number of samples starting from secondBunch (if any)
 *
 * @return The current state of the tracking
 */
TTrackingStatus PerformGpsTracking(TNGeneConfiguration *cfg, TNGeneData *rx, TChannel *ch,
                                   char *firstBunch, unsigned int firstBunchSize, char *secondBunch,
                                   unsigned int secondBunchSize);
#endif /* GPS_TRACKING_H_INCLUDED */
