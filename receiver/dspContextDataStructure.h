/**
 * @file dspContextDataStructure.h
 *
 * @brief Scratch arrays regenerated for one channel at a time during DSP.
 *
 * @note Stage 1 of the multi-core DSP pool: the receiver owns a single context
 *       reachable through TNGeneData::dsp. Stage 2 (see dspWorkerPool.h) assigns
 *       one context (and one XMM backup, see xmmsBackup.h) to each worker,
 *       since the carrier, code and demodulated samples below are rewritten
 *       at every channel with that channel's frequency, phase and PRN. The
 *       thread-local override ngeneDspOverride redirects the scratch access
 *       of a worker to its own context; when it is NULL the receiver context
 *       reachable through TNGeneData::dsp is used, as before.
 */
#ifndef RECEIVER_DSP_CONTEXT_DATA_STRUCTURE_H_INCLUDED
#define RECEIVER_DSP_CONTEXT_DATA_STRUCTURE_H_INCLUDED
#include "ngeneConstants.h"
#include "xmmsBackup.h"
/**
 * @struct DspContextStruct
 * @brief Contains the scratch arrays owned by a DSP worker.
 */
typedef struct DspContextStruct {
    char *codeArray; /**< contains the despreading code at fs */
    char *codeEarlyArray; /**< contains the despreading code early replica, at fs (low fs only) */
    char *codeLateArray; /**< contains the despreading code late replica, at fs (low fs only) */
    char *cosArray; /**< contains the cosine carrier at fs */
    char *sinArray; /**< contains the sine carrier at fs */
    char *subcarrierArray; /**< contains the subcarrier used to wipe-off the BOC(1,1) in Galileo */
    char *subcarrierArrayEarly; /**< contains the BOC(1,1) subcarrier (Trk Early replica, low fs) */
    char *subcarrierArrayLate; /**< contains the BOC(1,1) subcarrier (Trk Late replica, low fs) */
    short *wideI, *wideQ; /**< HackRF full precision demodulated samples; NULL for legacy paths */
    char *demodulatedCos; /**< Input samples demodulated using the cosine (coarse acquisition) */
    char *demodulatedSin; /**< Input samples demodulated using the sine (coarse acquisition) */
} TDspContext;
/**
 * @def NGENE_DSP
 * @brief Scratch context of the calling thread: the worker's own context when set,
 *        the receiver context otherwise (see receiver/dspWorkerPool.c).
 */
#define NGENE_DSP(rx) (ngeneDspOverride != NULL ? ngeneDspOverride : (rx)->dsp)
/** @brief Scratch context of the calling DSP worker thread, NULL on other threads. */
extern NGENE_THREAD_LOCAL TDspContext *ngeneDspOverride;
int InitDspContext(struct NGeneConfigurationStruct *cfg, TDspContext *dsp);
void DeinitDspContext(TDspContext *dsp);
#endif /* RECEIVER_DSP_CONTEXT_DATA_STRUCTURE_H_INCLUDED */
