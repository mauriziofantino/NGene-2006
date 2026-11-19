/**
 * @file dspContext.c
 *
 * @brief Allocation of the scratch arrays owned by a DSP worker.
 *        Sizes and initialization are the ones previously owned by
 *        InitReceiver/DeinitReceiver on the flat fields of TNGeneData.
 */
#include <stdlib.h>
#include "ngeneDataStructure.h"
#include "ngeneConfiguration.h"
#include "ngeneConstants.h"
#include "arrays.h"
#include "dspContextDataStructure.h"

int InitDspContext(TNGeneConfiguration *cfg, TDspContext *dsp)
{
    unsigned int arraySize = NGENE_ARRAYS_SIZE * cfg->samplesPerMillisecond;
    int errorsFound = 0;
    dsp->codeArray = dsp->codeEarlyArray = dsp->codeLateArray = NULL;
    dsp->cosArray = dsp->sinArray = NULL;
    dsp->wideI = dsp->wideQ = NULL;
    dsp->demodulatedCos = dsp->demodulatedSin = NULL;
    posix_memalign((void **)&(dsp->codeArray), POSIX_ALIGNMENT, arraySize * sizeof(char));
    if (dsp->codeArray == NULL)
        errorsFound++;
    if (cfg->lowFsElaboration == TRUE_FLAG) {
        posix_memalign((void **)&(dsp->codeEarlyArray), POSIX_ALIGNMENT, arraySize * sizeof(char));
        if (dsp->codeEarlyArray == NULL)
            errorsFound++;
        posix_memalign((void **)&(dsp->codeLateArray), POSIX_ALIGNMENT, arraySize * sizeof(char));
        if (dsp->codeLateArray == NULL)
            errorsFound++;
    }
    posix_memalign((void **)&(dsp->cosArray), POSIX_ALIGNMENT, arraySize * sizeof(char));
    if (dsp->cosArray == NULL)
        errorsFound++;
    posix_memalign((void **)&(dsp->sinArray), POSIX_ALIGNMENT, arraySize * sizeof(char));
    if (dsp->sinArray == NULL)
        errorsFound++;
    if (cfg->usedFrontEnd == NGENE_FE_HACKRF) {
        posix_memalign((void **)&dsp->wideI, POSIX_ALIGNMENT, arraySize * sizeof(short));
        posix_memalign((void **)&dsp->wideQ, POSIX_ALIGNMENT, arraySize * sizeof(short));
        if (!dsp->wideI || !dsp->wideQ)
            errorsFound++;
    }
    posix_memalign((void **)&(dsp->subcarrierArray), POSIX_ALIGNMENT, arraySize * sizeof(char));
    if (dsp->subcarrierArray == NULL)
        errorsFound++;
    posix_memalign((void **)&(dsp->subcarrierArrayEarly), POSIX_ALIGNMENT, arraySize * sizeof(char));
    if (dsp->subcarrierArrayEarly == NULL)
        errorsFound++;
    posix_memalign((void **)&(dsp->subcarrierArrayLate), POSIX_ALIGNMENT, arraySize * sizeof(char));
    if (dsp->subcarrierArrayLate == NULL)
        errorsFound++;
    /* Coarse acquisition demodulation scratch (previously in TAcqCommon). */
    posix_memalign((void **)&(dsp->demodulatedCos), POSIX_ALIGNMENT, arraySize * sizeof(char));
    if (dsp->demodulatedCos == NULL)
        errorsFound++;
    posix_memalign((void **)&(dsp->demodulatedSin), POSIX_ALIGNMENT, arraySize * sizeof(char));
    if (dsp->demodulatedSin == NULL)
        errorsFound++;
    if (errorsFound > 0)
        return FAILURE_RETURN;
    ArrayClean(dsp->codeArray, arraySize * sizeof(char));
    if (cfg->lowFsElaboration == TRUE_FLAG) {
        ArrayClean(dsp->codeEarlyArray, arraySize * sizeof(char));
        ArrayClean(dsp->codeLateArray, arraySize * sizeof(char));
    }
    ArrayClean(dsp->cosArray, arraySize * sizeof(char));
    ArrayClean(dsp->sinArray, arraySize * sizeof(char));
    ArrayClean(dsp->subcarrierArray, arraySize * sizeof(char));
    ArrayClean(dsp->subcarrierArrayEarly, arraySize * sizeof(char));
    ArrayClean(dsp->subcarrierArrayLate, arraySize * sizeof(char));
    return SUCCESS_RETURN;
}

void DeinitDspContext(TDspContext *dsp)
{
    if (dsp == NULL)
        return;
    free(dsp->codeArray);
    free(dsp->codeEarlyArray);
    free(dsp->codeLateArray);
    free(dsp->cosArray);
    free(dsp->sinArray);
    free(dsp->wideI);
    free(dsp->wideQ);
    free(dsp->subcarrierArray);
    free(dsp->subcarrierArrayEarly);
    free(dsp->subcarrierArrayLate);
    free(dsp->demodulatedCos);
    free(dsp->demodulatedSin);
    dsp->codeArray = dsp->codeEarlyArray = dsp->codeLateArray = NULL;
    dsp->cosArray = dsp->sinArray = NULL;
    dsp->wideI = dsp->wideQ = NULL;
    dsp->subcarrierArray = dsp->subcarrierArrayEarly = dsp->subcarrierArrayLate = NULL;
    dsp->demodulatedCos = dsp->demodulatedSin = NULL;
}
