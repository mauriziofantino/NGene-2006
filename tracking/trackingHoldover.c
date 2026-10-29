#include <math.h>
#include <string.h>
#include "ngeneConfiguration.h"
#include "ngeneDataStructure.h"
#include "tracking.h"
#include "trackingDiscriminatorsLoops.h"
#include "mathFunctions.h"
#include "ioFunctions.h"
#include "prnTable.h"
#include "galileoMessageParser.h"
#include "egnosMessageParser.h"
#include "osnmaManagement.h"

static void DiscardPartialNavigation(TChannel *ch)
{
	/* Keep validated ephemerides and WN, but require a new decoded TOW.
	 * Never join a word/page or authentication fragment across an erasure. */
	if(ch->satType==GPS_SAT && ch->gpsParser!=NULL)
	{
		TGpsParser *p=ch->gpsParser;
		p->status=SEARCH_SYNCHRONIZATION;
		p->lastWord=0; p->bitIndex=-2; p->wordIndex=0;
		p->crcPassed=0; p->bitIndexTotal=0; p->lastSavedSubframe=0;
		if(p->navMsg!=NULL) p->navMsg->towValid=FALSE_FLAG;
	}
	else if(ch->satType==GALILEO_SAT && ch->galParser!=NULL)
	{
		TGalParser *p=ch->galParser;
		p->status=SEARCH_SYNCHRONIZATION;
		p->byteIndex=0; p->bitIndex=0; p->bitIndexTotal=0;
		p->synchronizationSuccess=FALSE_FLAG;
		memset(p->pagePart,0,sizeof(p->pagePart));
		if(p->navMsg!=NULL) p->navMsg->towValid=FALSE_FLAG;
		ResetReedSolomonData(p);
		InitChannelOsnmaVariables(ch,&p->chOsnma);
	}
	else if(ch->satType==EGNOS_SAT && ch->egnosParser!=NULL)
	{
		TEgnosParser *p=ch->egnosParser;
		CleanEgnosParserArrays(p,TRUE_FLAG);
		p->symbolIndex=0; p->polarityCorrection=0;
		p->status=SEARCH_SYNCHRONIZATION;
		p->expectedPreamble=EGNOS_PREAMBLE_INVALID_INDEX;
	}
}

void RememberTrackingNco(TNGeneConfiguration *cfg,TNGeneData *rx,TChannel *ch)
{
	TTracking *trk=&ch->tracking;
	TTrackingHoldover *h=&ch->tracking.holdover;
	double power=(double)trk->pI*trk->pI+(double)trk->pQ*trk->pQ;
	double early=(double)trk->eI*trk->eI+(double)trk->eQ*trk->eQ;
	double late=(double)trk->lI*trk->lI+(double)trk->lQ*trk->lQ;
	double carrier=GetAverageWindowValue(&trk->carrierLoop.meanCarrierFreq);
	double code=GetAverageWindowValue(&ch->codeRateMean);
	(void)cfg;
	/* Do not overwrite the last good trajectory with noise while the slow
	 * lock estimators still remember the signal. The relative 6 dB guard
	 * only qualifies the saved predictor; it never declares tracking lost. */
	if(!isfinite(power) || power<=early || power<=late ||
	   (h->anchorValid && power<.25*h->anchorPower)) return;
	/* The existing smoothed NCO estimates predict a short gap better than
	 * the noisy instantaneous DLL/PLL updates. Arithmetic remains double. */
	if(isfinite(carrier) && isfinite(code) && code>0)
	{
		h->carrierFrequency=carrier;
		h->codeRate=code;
		h->carrierPhase=ch->carrierPhase;
		h->codePhase=ch->codePhase;
		h->anchorSample=rx->buf.discardedSamples+ch->bufferOffset+ch->blocksize;
		h->anchorPower=power;
		h->anchorValid=TRUE_FLAG;
	}
}

void EnterTrackingHoldover(TNGeneConfiguration *cfg,TNGeneData *rx,TChannel *ch)
{
	TTracking *trk=&ch->tracking;
	TTrackingHoldover *h=&trk->holdover;
	unsigned long long sample=rx->buf.discardedSamples+ch->bufferOffset+ch->blocksize;
	double elapsed,phase;
	/* A stale or backwards sample reference cannot seed dead reckoning.
	 * Leave the legacy failed-lock timeout active in this case. */
	if(!h->active && (sample<h->anchorSample ||
	   (double)(sample-h->anchorSample)>cfg->fs*RELOCK_TIMEOUT/1000.0))
	{
		h->anchorValid=FALSE_FLAG;
		return;
	}
	if(!h->active)
	{
		h->active=TRUE_FLAG;
		h->firstFailureCounter=trk->trkCounter;
		SwrxLog(NGENE_LOG_SEVERITY_INFO,"Channel %2d %s tracking holdover at sample %llu, trk counter %llu (limit %d ms)",ch->chIndex,ch->prnName,rx->buf.discardedSamples+ch->bufferOffset+ch->blocksize,trk->trkCounter,cfg->trkConfiguration.holdoverTimeoutMs);
	}
	trk->trkStatus=TRACKING_HOLDOVER;
	h->lastCheckCounter=trk->trkCounter;
	h->goodChecks=0;
	/* Phase-independent evidence of the code peak over about 100 ms.
	 * A coasting PLL is allowed to have any residual carrier phase. */
	InitAverageWindow(&h->earlyPower,TRACKING_RECOVERY_CHECK_MS/trk->integrationPeriod,0);
	InitAverageWindow(&h->promptPower,TRACKING_RECOVERY_CHECK_MS/trk->integrationPeriod,0);
	InitAverageWindow(&h->latePower,TRACKING_RECOVERY_CHECK_MS/trk->integrationPeriod,0);
	h->discardNavUntilBoundary=TRUE_FLAG;
	ch->pvtInfo.usedForPVT=FALSE_FLAG;
	ch->carrierFrequency=h->carrierFrequency;
	ch->dopplerShift=ch->carrierFrequency-cfg->fif;
	ch->codeRate=h->codeRate;
	/* Propagate the trusted trajectory to the current sample. This also
	 * removes phase drift accumulated while the loss detectors were settling.
	 * *Old still describes the block actually processed. Bit cadence is kept. */
	elapsed=(double)(sample-h->anchorSample)/cfg->fs;
	phase=h->carrierPhase+TWICE_M_PI*h->carrierFrequency*elapsed;
	ch->carrierPhase=phase-TWICE_M_PI*floor(phase/TWICE_M_PI);
	AddCodePhases(ch->satType,h->codePhase,h->codeRate*elapsed,&ch->codePhase,&ch->codePhaseFrac);
	if(cfg->lowFsElaboration)
	{
		AddCodePhases(ch->satType,ch->codePhase,-trk->spacing,&trk->codeEarlyPhase,&trk->codeEarlyPhaseFrac);
		AddCodePhases(ch->satType,ch->codePhase,trk->spacing,&trk->codeLatePhase,&trk->codeLatePhaseFrac);
	}
	RestartCarrierLoop(ch,trk);
	RestartCodeLoop(ch,trk);
	trk->pIForNav=0;
	DiscardPartialNavigation(ch);
}

int ManageTrackingHoldover(TNGeneConfiguration *cfg,TNGeneData *rx,TChannel *ch)
{
	TTracking *trk=&ch->tracking;
	TTrackingHoldover *h=&trk->holdover;
	TTrackingStatusChecker *c=&trk->trkStatusChecker;
	double ratio=c->nbd/c->nbp;
	int good;
	if(!h->active) return FALSE_FLAG;
	if(trk->trkCounter-h->firstFailureCounter>=(unsigned)cfg->trkConfiguration.holdoverTimeoutMs)
	{
		SwrxLog(NGENE_LOG_SEVERITY_INFO,"Channel %2d %s tracking lost at sample %llu, trk counter %llu (holdover timeout)",ch->chIndex,ch->prnName,rx->buf.discardedSamples+ch->bufferOffset+ch->blocksize,trk->trkCounter);
		trk->trkStatus=TRACKING_LOST;
		h->active=h->anchorValid=FALSE_FLAG;
		return TRUE_FLAG;
	}
	if(trk->trkStatus!=TRACKING_HOLDOVER && trk->trkStatus!=TRACKING_RELOCK)
		return FALSE_FLAG; /* false-lock confirmation shares this same deadline */
	if(trk->msCounter%trk->integrationPeriod==0 || trk->msCounter==ch->bitDuration)
	{
		PerformAverageWindow(&h->earlyPower,(double)trk->eI*trk->eI+(double)trk->eQ*trk->eQ);
		PerformAverageWindow(&h->promptPower,(double)trk->pI*trk->pI+(double)trk->pQ*trk->pQ);
		PerformAverageWindow(&h->latePower,(double)trk->lI*trk->lI+(double)trk->lQ*trk->lQ);
	}
	if(trk->trkCounter-h->lastCheckCounter<TRACKING_RECOVERY_CHECK_MS)
		return TRUE_FLAG;
	h->lastCheckCounter=trk->trkCounter;
	/* First detect a phase-independent code peak, then require both PLL
	 * and C/N0 repeatedly with the loops running. Cold PLL lock still uses
	 * the historical high threshold. */
	good=isfinite(c->cnoEstimator.estimatedCNo) &&
		c->cnoEstimator.estimatedCNo>cfg->trkConfiguration.cutoffCno+CNO_HIGH_THRESHOLD_DIFF;
	if(trk->trkStatus==TRACKING_HOLDOVER)
		good=good && isfinite(h->promptPower.filterValue) &&
			h->promptPower.filterValue>h->earlyPower.filterValue &&
			h->promptPower.filterValue>h->latePower.filterValue;
	else
		good=good && isfinite(ratio) && c->nbp>0 && ratio>PLL_CHECKER_LOW_THRESHOLD;
	if(!good)
	{
		h->goodChecks=0;
		if(trk->trkStatus==TRACKING_RELOCK) EnterTrackingHoldover(cfg,rx,ch);
		return TRUE_FLAG;
	}
	if(++h->goodChecks<TRACKING_RECOVERY_GOOD_CHECKS) return TRUE_FLAG;
	h->goodChecks=0;
	if(trk->trkStatus==TRACKING_HOLDOVER)
	{
		trk->trkStatus=TRACKING_RELOCK;
		RestartCarrierLoop(ch,trk);
		RestartCodeLoop(ch,trk);
		InitAverageWindow(&ch->codeRateMean,PLL_FALSE_LOCK_FILTER_LENGTH,ch->codeRate);
	}
	else
	{
		trk->trkStatus=TRACKING_PLL_FALSE_LOCK_CHECK;
		c->pflcTotalTests=TRACKING_FALSE_LOCK_TESTS;
		c->pflcLastCheckCounter=trk->trkCounter;
		c->pllLastCheckCounter=c->cnoLastCheckCounter=trk->trkCounter;
	}
	return TRUE_FLAG;
}

int TrackingLoopsEnabled(const TTracking *trk)
{
	return trk->trkStatus!=TRACKING_HOLDOVER && trk->trkStatus!=TRACKING_LOST;
}

int TrackingNavigationEnabled(TTracking *trk)
{
	if(trk->holdover.active) return FALSE_FLAG;
	if(trk->holdover.discardNavUntilBoundary)
	{
		trk->holdover.discardNavUntilBoundary=FALSE_FLAG;
		return FALSE_FLAG;
	}
	return trk->trkStatus!=TRACKING_LOST;
}
