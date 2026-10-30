#ifndef NGENE_HACKRF_THRESHOLD_H
#define NGENE_HACKRF_THRESHOLD_H
#include "ngeneDataStructure.h"
/* Q6 integer GPS I/Q (one ms) and Galileo I/Q (four ms) noise probes.
 * Return zero when fewer than four ms of valid ring data are available. */

int MeasureHackrfConfirmationNoise(TNGeneConfiguration *cfg, TNGeneData *rx, int64_t out[4]);
#endif
