/**
 * @file dspContextDataStructure.h
 *
 * @brief Scratch arrays regenerated for one channel at a time during DSP.
 *
 * @note Stage 1 of the multi-core DSP pool: the receiver owns a single context
 *       reachable through TNGeneData::dsp, so behaviour is identical to the
 *       previous flat fields of TNGeneData and TAcqCommon. A future pool assigns
 *       one context (and one XMM backup, see xmmsBackup.h) to each worker,
 *       since the carrier, code and demodulated samples below are rewritten
 *       at every channel with that channel's frequency, phase and PRN.
 */
#ifndef RECEIVER_DSP_CONTEXT_DATA_STRUCTURE_H_INCLUDED
#define RECEIVER_DSP_CONTEXT_DATA_STRUCTURE_H_INCLUDED
#include "ngeneConstants.h"
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
int InitDspContext(struct NGeneConfigurationStruct *cfg, TDspContext *dsp);
void DeinitDspContext(TDspContext *dsp);
#endif /* RECEIVER_DSP_CONTEXT_DATA_STRUCTURE_H_INCLUDED */
