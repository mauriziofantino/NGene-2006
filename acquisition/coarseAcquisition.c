/**
 * @file coarseAcquisition.c
 *
 * @author Mario Nicola
 *
 * @date 24/08/2014
 *
 * @version 1.0.0
 *
 * @brief Functions performing coarse acquisition.
 */
#include <stdlib.h>
#include <math.h>
#include <limits.h>
#include "acquisitionDataStructure.h"
#include "acquisition.h"
#include "ngeneConfiguration.h"
#include "channelDataStructure.h"
#include "carrierTable.h"
#include "arrays.h"
#include "sampleCompression.h"
#include "fft.h"
#include "ioFunctions.h"
#include "data_shift.h"
#include "closeManager.h"
#include "nonCoherentSumsManager.h"

void InitCoarseAcquisition(TNGeneConfiguration *cfg, TAcqCommon *acqCommon, TChannel *ch)
{
    TCoarseAcquisition *acq = &(ch->coarse);
    TAcqConfiguration *acqCfg = &(cfg->acqConfiguration);
    int errorsFound = 0;
    int signalFftLen = 2 * ACQ_DEFAULT_FFT_SIZE;

    if (ch->satType == GPS_SAT) {
	acq->numDopplers = acqCommon->gpsNumDopplers;
	acq->dopplers = acqCommon->gpsDopplers;
	acq->numDelays = acqCommon->gpsNumDelays;
	acq->delays = acqCommon->gpsDelays;
	acq->threshold = UINT_MAX;
	acq->compressedLength = acqCfg->gpsFftSize;
	acq->fftSize = acqCfg->gpsFftSize;
	acq->log2FftSize = acqCfg->gpsLog2FftSize;
	acq->fftGain = acqCfg->gpsFftGain;
	acq->ifftGain = acqCfg->gpsIfftGain;
	if (cfg->automaticConfigurationFlag == TRUE_FLAG) {
	    acq->maxNumOfNonCoherentSums = cfg->acqConfiguration.gpsNumOfNonCohSums;
	} else {
	    acq->maxNumOfNonCoherentSums = (int) (cfg->manuallyConfiguredChannels[ch->chIndex].nonCoherentAcqIntTime);
	}
    } else if (ch->satType == GALILEO_SAT) {
	acq->numDopplers = acqCommon->galileoNumDopplers;
	acq->dopplers = acqCommon->galileoDopplers;
	acq->numDelays = acqCommon->galileoNumDelays;
	acq->delays = acqCommon->galileoDelays;
	acq->threshold = UINT_MAX;
	acq->compressedLength = acqCfg->galileoFftSize / GALILEO_CODE_DURATION;
	acq->fftSize = acqCfg->galileoFftSize;
	acq->log2FftSize = acqCfg->galileoLog2FftSize;
	acq->fftGain = acqCfg->galileoFftGain;
	acq->ifftGain = acqCfg->galileoIfftGain;
	if (cfg->automaticConfigurationFlag == TRUE_FLAG) {
	    acq->maxNumOfNonCoherentSums = cfg->acqConfiguration.galileoNumOfNonCohSums;
	} else {
	    acq->maxNumOfNonCoherentSums = (int) (cfg->manuallyConfiguredChannels[ch->chIndex].nonCoherentAcqIntTime);
	}
    } else {                    /* EGNOS_SAT */
	acq->numDopplers = acqCommon->egnosNumDopplers;
	acq->dopplers = acqCommon->egnosDopplers;
	acq->numDelays = acqCommon->egnosNumDelays;
	acq->delays = acqCommon->egnosDelays;
	acq->threshold = UINT_MAX;
	acq->compressedLength = acqCfg->gpsFftSize;
	acq->fftSize = acqCfg->gpsFftSize;
	acq->log2FftSize = acqCfg->gpsLog2FftSize;
	acq->fftGain = acqCfg->gpsFftGain;
	acq->ifftGain = acqCfg->gpsIfftGain;
	if (cfg->automaticConfigurationFlag == TRUE_FLAG) {
	    acq->maxNumOfNonCoherentSums = cfg->acqConfiguration.egnosNumOfNonCohSums;
	} else {
	    acq->maxNumOfNonCoherentSums = (int) (cfg->manuallyConfiguredChannels[ch->chIndex].nonCoherentAcqIntTime);
	}
    }
    acq->numOfNonCoherentSums = 1;
    acq->shiftsForNonCohSums = 0;
    acq->acqBinLog = NULL;
    // allocate arrays
    signalFftLen = 2 * acq->fftSize;
    acq->wideCompressed = NULL;
    acq->wideNonCoherentSums = NULL;
    acq->widePowerShift = 0;
    acq->fftHeadroomReported = 0;
    if (cfg->usedFrontEnd == NGENE_FE_HACKRF) {
        acq->wideCompressed = malloc(signalFftLen * sizeof(int32_t));
        acq->wideNonCoherentSums = calloc(acq->fftSize, sizeof(uint64_t));
        if (!acq->wideCompressed || !acq->wideNonCoherentSums) errorsFound++;
    }
    posix_memalign((void **) &(acq->signalCompressed), POSIX_ALIGNMENT, signalFftLen * sizeof(short int));
    if (acq->signalCompressed == NULL)
	errorsFound++;
    posix_memalign((void **) &(acq->signalFft), POSIX_ALIGNMENT, signalFftLen * sizeof(short int));
    if (acq->signalFft == NULL)
	errorsFound++;
    posix_memalign((void **) &(acq->ifftOut), POSIX_ALIGNMENT, signalFftLen * sizeof(short int));
    if (acq->ifftOut == NULL)
	errorsFound++;
    posix_memalign((void **) &(acq->ifftAbs), POSIX_ALIGNMENT, acq->fftSize * sizeof(unsigned int));
    if (acq->ifftAbs == NULL)
	errorsFound++;
    posix_memalign((void **) &(acq->nonCoherentSums), POSIX_ALIGNMENT, acq->fftSize * sizeof(unsigned int));
    if (acq->nonCoherentSums == NULL)
	errorsFound++;
    if (errorsFound > 0) {
	SwrxLog(NGENE_LOG_SEVERITY_ERROR, "Malloc returned a NULL pointer in InitAcquisitionCommon");
	CloseNGene(cfg, EXIT_FAILURE);
    }
}

void DeinitCoarseAcquisition(TCoarseAcquisition *acq)
{
    free(acq->wideCompressed);
    free(acq->wideNonCoherentSums);
    free(acq->signalCompressed);
    free(acq->signalFft);
    free(acq->ifftOut);
    free(acq->ifftAbs);
    free(acq->nonCoherentSums);
    if (acq->acqBinLog != NULL) {
	fclose(acq->acqBinLog);
	acq->acqBinLog = NULL;
    }
}

void StartCoarseAcquisition(TNGeneConfiguration *cfg, TNGeneData *rx, TAcqCommon *acqCommon, TChannel *ch)
{
    TCoarseAcquisition *acq = &(ch->coarse);
    TAcqConfiguration *acqCfg = &(cfg->acqConfiguration);
    char filename[2 * MAX_ROW_LENGTH];
    int fftSize;

    if (ch->satType == GPS_SAT) {
	acq->localCode = acqCommon->gpsCodesFft[ch->satId - BASE_PRN];
	fftSize = acqCfg->gpsFftSize;
    } else if (ch->satType == GALILEO_SAT) {
	acq->localCode = acqCommon->galileoCodesFft[ch->satId - BASE_PRN];
	fftSize = acqCfg->galileoFftSize;
    } else {                    /* EGNOS_SAT */
	if (ch->satId == EGNOS_FIRST_PRN)
	    acq->localCode = acqCommon->egnosCodesFft[0];
	else if (ch->satId == EGNOS_SECOND_PRN)
	    acq->localCode = acqCommon->egnosCodesFft[1];
	else if (ch->satId == EGNOS_THIRD_PRN)
	    acq->localCode = acqCommon->egnosCodesFft[2];
	else                    /* EGNOS_FOURTH_PRN */
	    acq->localCode = acqCommon->egnosCodesFft[3];
	fftSize = acqCfg->gpsFftSize;
    }
    PerformNonCoherentSumsManager(cfg, rx, ch->satType, ch->satId, &(acqCommon->nonCohManager));
    acq->numOfNonCoherentSums = acqCommon->nonCohManager.currentNumberOfNonCoherentSums;
    if (acq->numOfNonCoherentSums > acq->maxNumOfNonCoherentSums)
	acq->numOfNonCoherentSums = acq->maxNumOfNonCoherentSums;
    acq->shiftsForNonCohSums = CeilLog2(acq->numOfNonCoherentSums);
    acq->dopplerIndex = FAKE_DOPPLER_INDEX;
    acq->delayIndex = 0;
    acq->msCounter = 0;
    acq->nonCoherentSumsIndex = 0;
    acq->maximum = 0;
    acq->dopplerMax = 0.0;
    acq->delayMax = 0;
    acq->discardedSamples = 0;

    if (cfg->logConfiguration.enableAcqLog == TRUE_FLAG) {
	if (acq->acqBinLog != NULL) {
	    fclose(acq->acqBinLog);
	}
	sprintf(filename, "%s/CAF_%s.bin", cfg->dumpPathCurrent, ch->prnName);
	acq->acqBinLog = fopen(filename, "rb");
	if (acq->acqBinLog != NULL) {
	    /* if the log file already exists, append */
	    fclose(acq->acqBinLog);
	    acq->acqBinLog = fopen(filename, "ab");
	} else {
	    /* if the log file does not exist, create and initializes */
	    acq->acqBinLog = fopen(filename, "wb");
	    fwrite(&fftSize, sizeof(int), 1, acq->acqBinLog);
	    fwrite(&(acq->numDopplers), sizeof(int), 1, acq->acqBinLog);
	    fwrite(acq->dopplers, sizeof(float), acq->numDopplers, acq->acqBinLog);
	    fwrite(&(acq->numDelays), sizeof(int), 1, acq->acqBinLog);
	    fwrite(acq->delays, sizeof(int), acq->numDelays, acq->acqBinLog);
	}
    }
}

TAcquisitionStatus PerformCoarseAcquisition(TNGeneConfiguration *cfg, TNGeneData *rx, TChannel *ch, char *firstBunch, unsigned int firstBunchSize, char *secondBunch, unsigned int secondBunchSize)
{
    TCoarseAcquisition *acq = &(ch->coarse);
    TAcqCommon *acqCom = &(rx->acqCommon);
    int curDelay = acq->delays[acq->delayIndex];
    char *input1;
    unsigned int size1;
    char *input2;
    unsigned int size2;
    double fc;
    int outputIndex;
    uint64_t curMax;
    unsigned int curIndexMax;
    double codeRateNew;
    long long int elapsedSamples;
    long long int elapsedSamplesCorrect;
    int sampleCorrection;
    int effectiveFftGain = acq->fftGain, requestedFftGain = acq->fftGain;
    int requestedIfftGain = acq->ifftGain;

    /* the code offset could require more samples then those available */
    if (((unsigned int) curDelay + ch->blocksize) > (firstBunchSize + secondBunchSize))
	return ACQUISITION_SKIPPED;
    /* apply the code delay  */
    if ((unsigned int) curDelay < firstBunchSize) {
	input1 = &(firstBunch[curDelay * rx->buf.sampleStride]);
	size1 = firstBunchSize - curDelay;
	input2 = secondBunch;
	size2 = secondBunchSize;
    } else {
	input1 = &(secondBunch[(curDelay - firstBunchSize) * rx->buf.sampleStride]);
	size1 = secondBunchSize - curDelay + firstBunchSize;
	input2 = NULL;
	size2 = 0;
    }
    /* prepare the carrier */
    if (acq->dopplerIndex == FAKE_DOPPLER_INDEX) {
	ch->carrierFrequency = fc = cfg->fif + (double) THRESHOLD_FAKE_DOPPLER;
	ch->dopplerShift = (double) THRESHOLD_FAKE_DOPPLER;
    } else {
	ch->carrierFrequency = fc = cfg->fif + (double) acq->dopplers[acq->dopplerIndex];
	ch->dopplerShift = (double) acq->dopplers[acq->dopplerIndex];
    }
    if (!rx->wideI) ch->carrierPhase = GenerateCarrier(cfg, &(rx->fifTable), fc, ch->carrierPhase, ch->blocksize, rx->sinArray, rx->cosArray);
    /* remove the carrier (and, for Galileo, also subcarrier) */
    if (rx->wideI) {
        ch->carrierPhase = MixHackrfInput(&rx->hackrfMixer, (THackrfIq8 *)input1, size1, (THackrfIq8 *)input2, size2,
            ch->blocksize, fc, ch->carrierPhase, cfg->fs, rx->wideI, rx->wideQ);
    } else if (ch->satType == GALILEO_SAT && !cfg->nativeIq) {
	GenerateSine(cfg, &(rx->fscTable), ch->codeRate, 0.0, ch->blocksize, rx->subcarrierArray);
	CarrierRealWipeOff(input1, size1, input2, size2, rx->subcarrierArray, ch->blocksize, acqCom->demodulatedSin);
	ArraySub(acqCom->demodulatedSin, rx->subcarrierArray, acqCom->demodulatedSin, ch->blocksize);
	ArrayXor(acqCom->demodulatedSin, rx->cosArray, acqCom->demodulatedCos, ch->blocksize);
	ArrayXor(acqCom->demodulatedSin, rx->sinArray, acqCom->demodulatedSin, ch->blocksize);
	ArraySub(acqCom->demodulatedCos, rx->cosArray, acqCom->demodulatedCos, ch->blocksize);
	ArraySub(acqCom->demodulatedSin, rx->sinArray, acqCom->demodulatedSin, ch->blocksize);
    } else {
	CarrierInputWipeOff(cfg->nativeIq, input1, size1, input2, size2, rx->cosArray, rx->sinArray, ch->blocksize, acqCom->demodulatedCos, acqCom->demodulatedSin);
	ArraySub(acqCom->demodulatedCos, rx->cosArray, acqCom->demodulatedCos, ch->blocksize);
	ArraySub(acqCom->demodulatedSin, rx->sinArray, acqCom->demodulatedSin, ch->blocksize);
    }
    if (ch->satType == GALILEO_SAT && cfg->nativeIq && !rx->wideI) {
        /* The complex input has two components; remove BOC from both after
         * the input rotation. The remaining acquisition steps are unchanged. */
        GenerateSine(cfg, &(rx->fscTable), ch->codeRate, 0.0, ch->blocksize, rx->subcarrierArray);
        ArrayXor(acqCom->demodulatedCos, rx->subcarrierArray, acqCom->demodulatedCos, ch->blocksize);
        ArraySub(acqCom->demodulatedCos, rx->subcarrierArray, acqCom->demodulatedCos, ch->blocksize);
        ArrayXor(acqCom->demodulatedSin, rx->subcarrierArray, acqCom->demodulatedSin, ch->blocksize);
        ArraySub(acqCom->demodulatedSin, rx->subcarrierArray, acqCom->demodulatedSin, ch->blocksize);
    }
    /* compress */
    outputIndex = 2 * acq->msCounter * acq->compressedLength;
#ifdef RX_PROFILING
    if (ch->satType == GPS_SAT) {
	StartProfilingUpdate(&(ch->gpsSampleCompressionProfiling));
    } else if (ch->satType == GALILEO_SAT) {
	StartProfilingUpdate(&(ch->galileoSampleCompressionProfiling));
    }
#endif
    if (rx->wideI) {
        char *boc = NULL;
        if (ch->satType == GALILEO_SAT) {
            GenerateSine(cfg, &(rx->fscTable), ch->codeRate, 0.0, ch->blocksize, rx->subcarrierArray);
            boc = rx->subcarrierArray;
        }
        CompressWide(rx->wideI, rx->wideQ, boc, ch->blocksize,
            acq->wideCompressed + outputIndex, acq->compressedLength);
    } else SampleCompression(0, 0, acqCom->demodulatedCos, acqCom->demodulatedSin, ch->blocksize, &(acq->signalCompressed[outputIndex]), acq->compressedLength);
#ifdef RX_PROFILING
    if (ch->satType == GPS_SAT) {
	StopProfilingUpdate(&(ch->gpsSampleCompressionProfiling), 1);
    } else if (ch->satType == GALILEO_SAT) {
	StopProfilingUpdate(&(ch->galileoSampleCompressionProfiling), 1);
    }
#endif
    acq->msCounter++;
    if (acq->msCounter == ch->codeDuration) {
	acq->msCounter = 0;

	/* remove the code */
#ifdef RX_PROFILING
	if (ch->satType == GPS_SAT) {
	    StartProfilingUpdate(&(ch->gpsFftProfiling));
	} else if (ch->satType == GALILEO_SAT) {
	    StartProfilingUpdate(&(ch->galileoFftProfiling));
	}
#endif
    if (rx->wideI) {
        for (;;) {
            unsigned clipped;
            if (!PackWideFft(acq->wideCompressed, acq->signalCompressed,
                    acq->fftSize, acq->log2FftSize, requestedFftGain, &effectiveFftGain) ||
                PerformFftTrial(cfg, &(rx->fftTable), acq->signalCompressed,
                    acq->signalFft, acq->log2FftSize, effectiveFftGain, &clipped) != SUCCESS_RETURN) {
                SwrxLog(NGENE_LOG_SEVERITY_ERROR, "%s: input exceeds representable FFT headroom", ch->prnName);
                return ACQUISITION_FAILURE;
            }
            if (!clipped) break;
            /* Retry from the original int32 window, never from clipped data.
             * Restore this exponent in the widened spectral product below. */
            if (requestedFftGain <= -acq->log2FftSize) {
                SwrxLog(NGENE_LOG_SEVERITY_ERROR, "%s: cannot obtain unclipped signal FFT", ch->prnName);
                return ACQUISITION_FAILURE;
            }
            requestedFftGain--;
        }
        if (requestedFftGain != acq->fftGain && !acq->fftHeadroomReported) {
            acq->fftHeadroomReported = 1;
            SwrxLog(NGENE_LOG_SEVERITY_NOTE, "%s: FFT headroom recovered with %d extra shifts; amplitude restored in 64-bit powers",
                ch->prnName, acq->fftGain-requestedFftGain);
        }
    } else PerformFft(cfg, &(rx->fftTable), acq->signalCompressed, acq->signalFft, acq->log2FftSize, acq->fftGain);
#ifdef RX_PROFILING
	if (ch->satType == GPS_SAT) {
	    StopProfilingUpdate(&(ch->gpsFftProfiling), 1);
	} else if (ch->satType == GALILEO_SAT) {
	    StopProfilingUpdate(&(ch->galileoFftProfiling), 1);
	}
#endif
    if (rx->wideI) {
        /* Carry the block exponent through both transforms. Restoring it here
         * would overflow the inverse transform at raw HackRF ADC amplitudes. */
        if (!ProductWideFft(acq->signalFft, acq->localCode, acq->signalFft,
                acq->fftSize, 0)) {
            SwrxLog(NGENE_LOG_SEVERITY_ERROR, "%s: spectral product exceeds int16 headroom", ch->prnName);
            return ACQUISITION_FAILURE;
        }
    } else ArrayProductComplex(cfg, acq->signalFft, acq->localCode, acq->signalFft, acq->fftSize);
#ifdef RX_PROFILING
	if (ch->satType == GPS_SAT) {
	    StartProfilingUpdate(&(ch->gpsFftProfiling));
	} else if (ch->satType == GALILEO_SAT) {
	    StartProfilingUpdate(&(ch->galileoFftProfiling));
	}
#endif
	/* do ifft (since we call fft, the output time is reversed) */
    if (rx->wideI) {
        for (;;) {
            unsigned clipped;
            if (PerformFftTrial(cfg, &rx->fftTable, acq->signalFft, acq->ifftOut,
                    acq->log2FftSize, requestedIfftGain, &clipped) != SUCCESS_RETURN)
                return ACQUISITION_FAILURE;
            if (!clipped) break;
            if (requestedIfftGain <= -acq->log2FftSize) {
                SwrxLog(NGENE_LOG_SEVERITY_ERROR, "%s: cannot obtain unclipped inverse FFT", ch->prnName);
                return ACQUISITION_FAILURE;
            }
            requestedIfftGain--;
        }
        acq->widePowerShift = acq->fftGain-requestedFftGain + acq->ifftGain-requestedIfftGain;
    } else PerformFft(cfg, &(rx->fftTable), acq->signalFft, acq->ifftOut, acq->log2FftSize, acq->ifftGain);
#ifdef RX_PROFILING
	if (ch->satType == GPS_SAT) {
	    StopProfilingUpdate(&(ch->gpsFftProfiling), 1);
	} else if (ch->satType == GALILEO_SAT) {
	    StopProfilingUpdate(&(ch->galileoFftProfiling), 1);
	}
#endif
	/* analyze the result */
	if (acq->dopplerIndex == FAKE_DOPPLER_INDEX) {
	    UpdateCoarseThreshold(cfg, ch, acq->numOfNonCoherentSums);
	    uint64_t tmp = (acq->threshold >> acq->shiftsForNonCohSums);
	    if (acq->acqBinLog != NULL) {
            if (rx->wideI) fwrite(&tmp, sizeof(tmp), 1, acq->acqBinLog);
            else { unsigned int legacy=(unsigned int)tmp; fwrite(&legacy,sizeof(legacy),1,acq->acqBinLog); }
	    }
	} else {
        if (rx->wideI) {
            if (!AccumulateWideFftPower(acq->ifftOut,acq->wideNonCoherentSums,acq->fftSize,
                    acq->widePowerShift,acq->shiftsForNonCohSums,acq->nonCoherentSumsIndex==0)) {
                SwrxLog(NGENE_LOG_SEVERITY_ERROR,"%s: unrepresentable acquisition power exponent",ch->prnName);
                return ACQUISITION_FAILURE;
            }
        } else {
	    ArrayAbs2(cfg, acq->ifftOut, acq->ifftAbs, acq->fftSize);
	    if (acq->nonCoherentSumsIndex == 0) {
		ArrayClean((char *) (acq->nonCoherentSums), acq->fftSize * sizeof(unsigned int));
	    }
	    RightShiftUnsignedInt(acq->ifftAbs, acq->ifftAbs, acq->fftSize, acq->shiftsForNonCohSums);
	    ArrayAddUnsignedInt(acq->ifftAbs, acq->nonCoherentSums, acq->nonCoherentSums, acq->fftSize);
        }
	    acq->nonCoherentSumsIndex++;
	    if (acq->nonCoherentSumsIndex >= acq->numOfNonCoherentSums) {
            if (rx->wideI) {
                unsigned k; curMax=0; curIndexMax=0;
                for (k=0;k<(unsigned)acq->fftSize;k++) if (acq->wideNonCoherentSums[k]>curMax) {
                    curMax=acq->wideNonCoherentSums[k]; curIndexMax=k;
                }
            } else { unsigned int legacyMax; ArrayMax(acq->nonCoherentSums,&legacyMax,&curIndexMax,acq->fftSize); curMax=legacyMax; }
		curIndexMax = acq->fftSize - curIndexMax;
		if (curMax > acq->maximum) {
		    /* a new peak has been found! */
		    acq->maximum = curMax;
		    acq->dopplerMax = acq->dopplers[acq->dopplerIndex];
		    acq->delayMax = (int) ((float) curIndexMax * ((ch->codeDuration * (float) ch->blocksize) / (float) acq->fftSize)) + curDelay;
		    acq->discardedSamples = rx->buf.discardedSamples + ch->bufferOffset + ch->blocksize;
		    SwrxLog(NGENE_LOG_SEVERITY_NOTE, "    %s: code delay %d", ch->prnName, curDelay);
		    SwrxLog(NGENE_LOG_SEVERITY_NOTE, "    %s: max found %llu at sample %u  with fd %f Hz", ch->prnName, (unsigned long long)acq->maximum, acq->delayMax, acq->dopplerMax);
		}
		if (acq->acqBinLog != NULL) {
                if (rx->wideI) fwrite(acq->wideNonCoherentSums,sizeof(uint64_t),acq->fftSize,acq->acqBinLog);
                else fwrite(acq->nonCoherentSums, sizeof(unsigned int), acq->fftSize, acq->acqBinLog);
		}
	    }
	}
	/* prepare next execution (if needed) */
	if (acq->dopplerIndex == FAKE_DOPPLER_INDEX) {
	    acq->dopplerIndex = 0;
	} else if (acq->nonCoherentSumsIndex >= acq->numOfNonCoherentSums) {
	    acq->nonCoherentSumsIndex = 0;
	    acq->delayIndex++;
	    if (acq->delayIndex == acq->numDelays) {
		acq->delayIndex = 0;
		acq->dopplerIndex++;
	    }
	}
    }
    /* acquisition finished: compare with threshold */
    if (acq->dopplerIndex == acq->numDopplers) {
	if (acq->maximum > (acq->threshold >> acq->shiftsForNonCohSums)) {
	    codeRateNew = ch->codeRate * (1.0 + ((double) cfg->fePars->dopplerCorrection * ((double) acq->dopplerMax / L1_FREQUENCY)));
	    elapsedSamples = rx->buf.discardedSamples + ch->bufferOffset + ch->blocksize - acq->discardedSamples;
	    elapsedSamplesCorrect = (long long int) ((double) elapsedSamples * (ch->codeRate / codeRateNew));
	    sampleCorrection = (int) (elapsedSamplesCorrect - elapsedSamples);
	    ch->blocksize += (acq->delayMax + sampleCorrection);
	    ch->codePhase = 0.0;
	    ch->codePhaseFrac = 0.0;
	    ch->codeRate = codeRateNew;
	    ch->carrierFrequency = cfg->fif + (double) acq->dopplerMax;
	    ch->dopplerShift = (double) acq->dopplerMax;
	    return ACQUISITION_SUCCESS;
	} else {
	    ch->codePhase = 0.0;
	    ch->codePhaseFrac = 0.0;
	    return ACQUISITION_FAILURE;
	}
    }
    UpdateCodePhase(cfg, ch->satType, &(ch->codePhase), &(ch->codePhaseFrac), ch->blocksize, ch->codeRate);
    return ACQUISITION_IN_EXECUTION;
}
