/**
 * @file circularBuffer.c
 *
 * @author Mario Nicola
 *
 * @date 14/08/2014
 *
 * @version 1.0.0
 *
 * @brief Managing of the pointers in the circular buffer.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "circularBuffer.h"
#include "ngeneConstants.h"
#include "ioFunctions.h"
#include "timeConstants.h"
#include "usbFrontEndConfiguration.h"
#include "sige.h"
#include "sigeV3.h"
#include "frontend.h"
#include "closeManager.h"
#include "arrays.h"


/* Perform the Circular buffer initialization */
int InitCircularBuffer(TNGeneConfiguration *cfg, TCircularBuffer *buf)
{
    int index;

    buf->dim = (unsigned int) ((cfg->fs * INPUT_BUFFER_SIZE) / SECONDS_TO_MILLISECONDS);
    buf->sampleStride = cfg->nativeIq ? 2 : 1;
    buf->sigeIqRaw = NULL;
    buf->sigeIqHasPending = buf->sigeIqSynchronized = 0;
    buf->sigeIqGeneration = 0;
    buf->buffer = (char *) malloc((size_t)buf->dim * buf->sampleStride);
    if (buf->buffer == NULL) {
	SwrxLog(NGENE_LOG_SEVERITY_ERROR, "malloc returned NULL pointer in InitCircularBuffer function");
	return FAILURE_RETURN;
    }
    if (cfg->nativeIq) {
        buf->sigeIqRaw = malloc((size_t)buf->dim * 2 + 1);
        if (buf->sigeIqRaw == NULL) {
            free(buf->buffer);
            buf->buffer = NULL;
            return FAILURE_RETURN;
        }
    }
    /* after the creation, the buffer is empty */
    buf->fill1 = 0;
    buf->fill2 = 0;
    buf->tail = 0;		/* tail is at the beginning of the buffer */
    buf->head = 0;		/* head is at the beginning of the buffer */
    buf->free1 = buf->dim;	/* only one free space with size dim */
    buf->free2 = 0;
    buf->discardedSamples = 0;	/* no samples have been elaborated yet */
     /**/ buf->sigeExpected = 0x02;
    buf->sigeMaskOffset = 0;
    if (cfg->fePars->resolution > 4) {
	buf->shiftAmount = cfg->fePars->resolution - 4;
	buf->unsignedToSignedCorrection[0] = (unsigned char) cfg->fePars->meanValue >> buf->shiftAmount;
    } else {
	buf->shiftAmount = 0;
	buf->unsignedToSignedCorrection[0] = (unsigned char) cfg->fePars->meanValue;
    }
    for (index = 1; index < XMM_REGISTER_SIZE; index++) {
	buf->unsignedToSignedCorrection[index] = buf->unsignedToSignedCorrection[index - 1];
    }
    return SUCCESS_RETURN;
}


/* It is the opposite of the Initialize function. It destroy and clean everything */
void DeinitCircularBuffer(TCircularBuffer *buf)
{
    free(buf->sigeIqRaw);
    buf->sigeIqRaw = NULL;
    if (buf->buffer != NULL) {
	free(buf->buffer);
	buf->buffer = NULL;
    }
    buf->dim = 0;
    buf->fill1 = 0;
    buf->fill2 = 0;
    buf->tail = 0;
    buf->head = 0;
    buf->free1 = 0;
    buf->free2 = 0;
    buf->discardedSamples = 0;
}


int InsertIntoCircularBuffer(TCircularBuffer *buf, unsigned int displacement)
{
    /* No new samples */
    if (displacement == 0) {
	return SUCCESS_RETURN;
    }
    /* Too many new samples! */
    if (displacement > buf->free1 + buf->free2) {
	return FAILURE_RETURN;
    }
    /* update the position of head */
    buf->head += displacement;
    if (buf->head >= buf->dim) {
	buf->head -= buf->dim;
    }
    /* update fill1, fill2, free1 and free2 */
    if (buf->head > buf->tail) {
	/* one bunch of samples */
	buf->fill1 = buf->head - buf->tail;
	buf->fill2 = 0;
	buf->free1 = buf->dim - buf->head;
	buf->free2 = buf->tail;
    } else {
	/* two bunches of samples or buffer full */
	buf->fill1 = buf->dim - buf->tail;
	buf->fill2 = buf->head;
	buf->free1 = buf->tail - buf->head;
	buf->free2 = 0;
    }
    return SUCCESS_RETURN;
}


/* This function is used to update the pointer for the buffer data handling */
unsigned int DeleteFromCircularBuffer(TCircularBuffer *buf, unsigned int displacement)
{
    /* if no samples have to be discarded, then returns immediately */
    if (displacement == 0) {
	return 0;
    }
    /* Number of samples to be freed exceeds buffer content */
    if (displacement > buf->fill1 + buf->fill2) {
	return 0;
    }
    /* Set tail to new position and wrap around (if needed) */
    buf->tail += displacement;
    if (buf->tail >= buf->dim) {
	buf->tail -= buf->dim;
    }
    if (buf->head >= buf->tail) {	/* Two bunches of free locations */
	buf->fill1 = buf->head - buf->tail;
	buf->fill2 = 0;
	buf->free1 = buf->dim - buf->head;
	buf->free2 = buf->tail;
    } else {			/* one bunch of free locations */
	buf->fill1 = buf->dim - buf->tail;
	buf->fill2 = buf->head;
	buf->free1 = buf->tail - buf->head;
	buf->free2 = 0;
    }
    /* some samples have been elaborated and discarded! */
    buf->discardedSamples += displacement;
    return displacement;
}


void ConvertSamplesIntoNGeneFormat(TNGeneConfiguration *cfg, TCircularBuffer *buf, char *ptr, int n)
{
    if (n <= 0)
	return;

    switch (cfg->usedFrontEnd) {
    case NGENE_FE_SIGE:
	if ((ptr[0] & 0x02) != buf->sigeExpected) {
	    buf->sigeExpected = buf->sigeExpected ^ 0x02;
	    buf->sigeMaskOffset = (buf->sigeMaskOffset + 1) & 0x03;
	}
	if ((n % 2) != 0) {
	    buf->sigeExpected = buf->sigeExpected ^ 0x02;
	}
	SigeSamplesElab(ptr, ptr, n, buf->sigeMaskOffset);
	buf->sigeMaskOffset = (buf->sigeMaskOffset + n) & 0x03;
	break;
    case NGENE_FE_SIGE_V3:
	SigeV3SamplesElab(ptr, ptr, n);
	break;
    default: break;
    }
}


int FillInputBuffer(TNGeneConfiguration *cfg, TCircularBuffer *buf, TRxFe *rxFe)
{
    unsigned int read1 = 0;
    unsigned int read2 = 0;
    unsigned int readTot;

    if (cfg->usedFrontEnd == NGENE_FE_HACKRF) {
        unsigned int freeSamples = buf->free1 + buf->free2;
        unsigned int prefix = buf->sigeIqHasPending ? 1 : 0;
        unsigned int bytes, pairs, first;
        if (!freeSamples) return SUCCESS_RETURN;
        if (prefix) buf->sigeIqRaw[0] = buf->sigeIqPending;
        bytes = prefix + ReadRxFrontEnd(rxFe, (char *)buf->sigeIqRaw + prefix,
                                        2 * freeSamples - prefix);
        if (bytes == prefix) return FAILURE_RETURN;
        pairs = bytes / 2;
        first = pairs < buf->free1 ? pairs : buf->free1;
        /* Correct DC before narrowing; use common IQ gain across both ring
         * segments. No modulation, conjugation, I/Q exchange or decimation. */
        AdaptIqClassic(buf->sigeIqRaw, (char *)buf->sigeIqRaw, pairs, NULL);
        memcpy(buf->buffer + 2 * buf->head, buf->sigeIqRaw, 2 * first);
        memcpy(buf->buffer, buf->sigeIqRaw + 2 * first, 2 * (pairs - first));
        buf->sigeIqHasPending = bytes & 1;
        if (buf->sigeIqHasPending) buf->sigeIqPending = buf->sigeIqRaw[bytes - 1];
        if (InsertIntoCircularBuffer(buf, pairs) != SUCCESS_RETURN) return FAILURE_RETURN;
        return buf->free1 + buf->free2 == 0 ? SUCCESS_RETURN : FAILURE_RETURN;
    }

    if (cfg->sigeNativeIq) {
        unsigned int freeSamples = buf->free1 + buf->free2;
        unsigned int prefix = buf->sigeIqHasPending ? 1 : 0;
        unsigned int bytes, offset = 0, pairs, first;
        if (freeSamples == 0) return SUCCESS_RETURN;
        if (prefix) buf->sigeIqRaw[0] = buf->sigeIqPending;
        bytes = prefix + ReadRxFrontEnd(rxFe, (char *)buf->sigeIqRaw + prefix,
                                        2 * freeSamples - prefix);
        if (bytes == prefix) return FAILURE_RETURN;
        if (cfg->inputMethod == USB_FRONTEND &&
            buf->sigeIqGeneration != rxFe->usbFe.usbReadGeneration) {
            buf->sigeIqGeneration = rxFe->usbFe.usbReadGeneration;
            buf->sigeIqHasPending = buf->sigeIqSynchronized = 0;
            offset = prefix; /* Discard the old stream's incomplete I. */
        }
        if (!buf->sigeIqSynchronized) {
            /* Start at the first I marker, even if USB starts on a Q byte. */
            if (!(buf->sigeIqRaw[offset] & 2)) offset++;
            if (bytes == offset) return FAILURE_RETURN;
            buf->sigeIqSynchronized = 1;
        }
        pairs = (bytes - offset) / 2;
        first = pairs < buf->free1 ? pairs : buf->free1;
        if (SigeDecodeNativeIq(buf->sigeIqRaw + offset,
                              buf->buffer + 2 * buf->head, first) != 0 ||
            SigeDecodeNativeIq(buf->sigeIqRaw + offset + 2 * first,
                              buf->buffer, pairs - first) != 0 ||
            (((bytes-offset)&1) && !(buf->sigeIqRaw[bytes-1]&2))) {
            unsigned int k;
            /* USB restart/FIFO remnants can contain unmatched components.
             * Recover only adjacent I,Q pairs, never exchange I and Q or
             * fabricate a counterpart. All valid streams use the SIMD path. */
            if (buf->sigeIqSynchronized != 2)
                SwrxLog(NGENE_LOG_SEVERITY_WARNING,
                        "Native SiGe IQ: discarding unmatched components (sample discontinuity)");
            buf->sigeIqSynchronized = 2;
            pairs = 0;
            buf->sigeIqHasPending = 0;
            for (k=offset; k<bytes; k++) {
                if (buf->sigeIqRaw[k]&2) {
                    buf->sigeIqPending = buf->sigeIqRaw[k];
                    buf->sigeIqHasPending = 1;
                } else if (buf->sigeIqHasPending) {
                    unsigned int pos = (buf->head+pairs) % buf->dim;
                    buf->buffer[2*pos] = 1-2*(buf->sigeIqPending&1);
                    buf->buffer[2*pos+1] = 1-2*(buf->sigeIqRaw[k]&1);
                    pairs++;
                    buf->sigeIqHasPending = 0;
                }
            }
        } else {
            buf->sigeIqHasPending = (bytes - offset) & 1;
            if (buf->sigeIqHasPending) buf->sigeIqPending = buf->sigeIqRaw[bytes - 1];
        }
        if (InsertIntoCircularBuffer(buf, pairs) != SUCCESS_RETURN) return FAILURE_RETURN;
        return buf->free1 + buf->free2 == 0 ? SUCCESS_RETURN : FAILURE_RETURN;
    }

    if (buf->free1 > 0) {
        read1 = ReadRxFrontEnd(rxFe, &(buf->buffer[buf->head]), buf->free1);
        ConvertSamplesIntoNGeneFormat(cfg, buf, &(buf->buffer[buf->head]), read1);
    }
    if ((read1 == buf->free1) && (buf->free2 > 0)) {
        read2 = ReadRxFrontEnd(rxFe, buf->buffer, buf->free2);
        ConvertSamplesIntoNGeneFormat(cfg, buf, buf->buffer, read2);
    }

    readTot = read1 + read2;
    if (InsertIntoCircularBuffer(buf, readTot) == FAILURE_RETURN) {
	SwrxLog(NGENE_LOG_SEVERITY_ERROR, "Unsupported buffer condition: investigate!");
	CloseNGene(cfg, EXIT_FAILURE);
    }
    if ((buf->free1 + buf->free2) > 0)
	return FAILURE_RETURN;
    else
	return SUCCESS_RETURN;
}


int CopyABunchOfContiguousSamples(TCircularBuffer *buf, int neededSamples, char *outputArray)
{
    /* check if the input buffer is large enough */
    if (buf->dim < (unsigned int) neededSamples) {
	return FAILURE_RETURN;
    }
    /* Copy in chronological order, including both components in native mode. */
    {
        unsigned int start = (buf->head + buf->dim - neededSamples) % buf->dim;
        unsigned int first = buf->dim - start;
        if (first > (unsigned int)neededSamples) first = neededSamples;
        memcpy(outputArray, buf->buffer + start * buf->sampleStride, first * buf->sampleStride);
        memcpy(outputArray + first * buf->sampleStride, buf->buffer,
               (neededSamples - first) * buf->sampleStride);
    }
    return SUCCESS_RETURN;
}

int CopyCircularBufferComponent(TCircularBuffer *buf, int neededSamples,
                                unsigned int component, char *outputArray)
{
    unsigned int k, start;
    if (neededSamples < 0 || (unsigned int)neededSamples > buf->dim ||
        component >= buf->sampleStride) return FAILURE_RETURN;
    if (buf->sampleStride == 1)
        return CopyABunchOfContiguousSamples(buf, neededSamples, outputArray);
    start = (buf->head + buf->dim - neededSamples) % buf->dim;
    for (k = 0; k < (unsigned int)neededSamples; k++) {
        outputArray[k] = buf->buffer[start * buf->sampleStride + component];
        if (++start == buf->dim) start = 0;
    }
    return SUCCESS_RETURN;
}
