/**
 * @file leastSquares.c
 *
 * @author Mario Nicola
 *
 * @date 24/10/2014
 *
 * @version 1.0.0
 *
 * @brief Least squares functions.
 */

#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "ngeneDataStructure.h"
#include "ngeneConfiguration.h"
#include "pvtDataStructure.h"
#include "ngeneConstants.h"
#include "ioFunctions.h"
#include "closeManager.h"
#include "matrices.h"
#include "pvtUtility.h"
#include "pntNumerics.h"


int InitLeastSquaresData(TNGeneConfiguration *cfg,TNGeneData *rx)
{
	int n = cfg->numberOfChannels;
	TLeastSquaresData *lsData = &(rx->pvtData.lsData);
	int errors = 0;

	lsData->a = (double *)malloc(n*LS_MULTI_SIZE*sizeof(double));
	if(lsData->a==NULL)
		errors++;
	lsData->ac = (double *)malloc(n*4*sizeof(double));
	if(lsData->ac==NULL)
		errors++;
	lsData->satRadialVelAndDoppler = (double *)malloc(n*1*sizeof(double));
	if(lsData->satRadialVelAndDoppler==NULL)
		errors++;
	lsData->x = (double *)malloc(3*n*sizeof(double));
	if(lsData->x==NULL)
		errors++;
	lsData->xRotate = (double *)malloc(3*n*sizeof(double));
	if(lsData->xRotate==NULL)
		errors++;
	lsData->b = (double *)malloc(n*1*sizeof(double));
	if(lsData->b==NULL)
		errors++;
	lsData->el = (double *)malloc(1*n*sizeof(double));
	if(lsData->el==NULL)
		errors++;

	if(errors>0)
	{
		SwrxLog(NGENE_LOG_SEVERITY_ERROR,"Malloc returned a NULL pointer in InitLeastSquaresData");
		CloseNGene(cfg,EXIT_FAILURE);
	}

	return SUCCESS_RETURN;
}


void ClearLeastSquaresData(TNGeneConfiguration *cfg,TLeastSquaresData *lsData)
{
	int n = cfg->numberOfChannels;

	MatrixInit(lsData->a,n,LS_MULTI_SIZE);
	MatrixInit(lsData->invAtA,LS_MULTI_SIZE,LS_MULTI_SIZE);
	MatrixInit(lsData->pos,LS_MULTI_SIZE,1);
	MatrixInit(lsData->sol,LS_MULTI_SIZE,1);
	MatrixInit(lsData->ac,n,4);
	MatrixInit(lsData->userVel,4,1);
	MatrixInit(lsData->satVel,3,1);
	MatrixInit(lsData->distVect,1,3);
	lsData->distance = 0.0;
	MatrixInit(lsData->satRadialVelAndDoppler,n,1);
	MatrixInit(lsData->x,3,n);
	MatrixInit(lsData->xRotate,3,n);
	MatrixInit(lsData->b,n,1);
	MatrixInit(lsData->el,1,n);
}


void DeinitLeastSquaresData(TNGeneData *rx)
{
	TLeastSquaresData *lsData = &(rx->pvtData.lsData);

	free(lsData->a);
	free(lsData->ac);
	free(lsData->satRadialVelAndDoppler);
	free(lsData->x);
	free(lsData->xRotate);
	free(lsData->b);
	free(lsData->el);
}


int PerformLeastSquares(TNGeneConfiguration *cfg,TNGeneData *rx)
{
	TPvtData *pvtData = &(rx->pvtData);
	TLeastSquaresData *lsData = &(pvtData->lsData);
	TPvtChannel *pvtCh;
	TPvtResults *pvtRes = &(rx->pvtTemporaryResults);
	TChannel *ch;
	TSatStatus *satStatus;
	int curSize;
	int idxGpsBias;
	int idxGalBias;
	int iter;
	int chIdx;
	int nCh = pvtData->numOfOperativeChannels;
	double topoXRotate[3*1];
	double geomRange;
	double aCorrectRow[1*3];
	double prod;
	double geod[3],angle,vs[3],weight;
	int converged=FALSE_FLAG,k;

	/* Initializations */
	ClearLeastSquaresData(cfg,lsData);
	if(pvtData->pvtIndex>0)
	{
		lsData->pos[0]=pvtRes->ecefPosition[0];
		lsData->pos[1]=pvtRes->ecefPosition[1];
		lsData->pos[2]=pvtRes->ecefPosition[2];
	}
	if((pvtData->numOfGpsOpChannels>0) && (pvtData->numOfGalileoOpChannels>0))
	{
		curSize = LS_MULTI_SIZE;
		idxGpsBias = 3;
		idxGalBias = 4;
	}
	else
	{
		curSize = LS_SINGLE_SIZE;
		idxGpsBias = 3;
		idxGalBias = 3;
	}
	if(nCh < curSize) return FAILURE_RETURN;
	for(chIdx=0;chIdx<nCh;chIdx++)
	{
		satStatus = pvtData->pvtChArray[chIdx].satStatus;
		lsData->x[(0*nCh) + chIdx] = satStatus->ecefSatPos[0];
		lsData->x[(1*nCh) + chIdx] = satStatus->ecefSatPos[1];
		lsData->x[(2*nCh) + chIdx] = satStatus->ecefSatPos[2];
	}
	/* Iterative computation of the position and clock bias (biases, in the multiple constellation case) */
	for(iter=0;iter<LS_ITERATIONS;iter++)
	{
		if(iter>0) Ecef2Geod(lsData->pos,geod);
		for(chIdx=0;chIdx<nCh;chIdx++)
		{
			pvtCh = &(pvtData->pvtChArray[chIdx]);
			ch = pvtCh->ch;
			satStatus = pvtCh->satStatus;
			if(iter==0)
			{
				lsData->xRotate[(0*nCh) + chIdx] = lsData->x[(0*nCh) + chIdx];
				lsData->xRotate[(1*nCh) + chIdx] = lsData->x[(1*nCh) + chIdx];
				lsData->xRotate[(2*nCh) + chIdx] = lsData->x[(2*nCh) + chIdx];
			}
			else
			{
				RotateSatellitePosition(satStatus->ecefSatPos,lsData->pos,topoXRotate);
				for(k=0;k<3;k++) lsData->xRotate[k*nCh+chIdx]=topoXRotate[k];
				satStatus->ecefSatPosRot[0] = topoXRotate[0];
				satStatus->ecefSatPosRot[1] = topoXRotate[1];
				satStatus->ecefSatPosRot[2] = topoXRotate[2];
				Topocent(lsData->pos,topoXRotate,lsData->el);
				satStatus->elevation = lsData->el[0] * DEGREE_TO_RADIAN;
				satStatus->azimuth = lsData->el[1] * DEGREE_TO_RADIAN;
				pvtCh->tropoCorrection = -TropoHopfieldModel(satStatus->elevation,geod[2]);
			}
			/* Calculate geometric range */
			geomRange = sqrt((lsData->xRotate[(0*nCh)+chIdx]-lsData->pos[0]) * (lsData->xRotate[(0*nCh)+chIdx]-lsData->pos[0]) +
							 (lsData->xRotate[(1*nCh)+chIdx]-lsData->pos[1]) * (lsData->xRotate[(1*nCh)+chIdx]-lsData->pos[1]) +
							 (lsData->xRotate[(2*nCh)+chIdx]-lsData->pos[2]) * (lsData->xRotate[(2*nCh)+chIdx]-lsData->pos[2]));
			/* Build the A matrix */
			if(!isfinite(geomRange) || geomRange <= 0) return FAILURE_RETURN;
			lsData->a[(curSize*chIdx)+0] = -(lsData->xRotate[(0*nCh)+chIdx] - lsData->pos[0] ) / geomRange;
			lsData->a[(curSize*chIdx)+1] = -(lsData->xRotate[(1*nCh)+chIdx] - lsData->pos[1] ) / geomRange;
			lsData->a[(curSize*chIdx)+2] = -(lsData->xRotate[(2*nCh)+chIdx] - lsData->pos[2] ) / geomRange;
			lsData->a[(curSize*chIdx)+idxGpsBias]=0.0; /* TODO dovrebbe già essere zero: verificare! */
			lsData->a[(curSize*chIdx)+idxGalBias]=0.0; /* TODO dovrebbe già essere zero: verificare! */
			if(ch->satType==GPS_SAT)
			{
				lsData->b[chIdx] = pvtCh->pseudorangeCsm - geomRange - lsData->pos[idxGpsBias] - pvtCh->tropoCorrection - pvtCh->ionoCorrection;
				lsData->a[(curSize*chIdx)+idxGpsBias]=1.0;
			}
			else /* if(ch->satType==GALILEO_SAT) */
			{
				lsData->b[chIdx] = pvtCh->pseudorangeCsm - geomRange - lsData->pos[idxGalBias] - pvtCh->tropoCorrection - pvtCh->ionoCorrection;
				lsData->a[(curSize*chIdx)+idxGalBias]=1.0;
			}
		}
		/* Whiten each observation using the same variance/elevation model as KF.
		 * The origin iteration has no meaningful elevation; initialize unweighted. */
		if(cfg->pvtConfiguration.weightedLs && iter>0) for(chIdx=0;chIdx<nCh;chIdx++) {
			pvtCh=&pvtData->pvtChArray[chIdx];
			if(!isfinite(pvtCh->posSigma2) || pvtCh->posSigma2<=0 ||
			   !isfinite(pvtCh->satStatus->elevation)) return FAILURE_RETURN;
			weight=sqrt(fmax(1e-3,sin(pvtCh->satStatus->elevation))/pvtCh->posSigma2);
			for(k=0;k<curSize;k++) lsData->a[chIdx*curSize+k]*=weight;
			lsData->b[chIdx]*=weight;
		}
		if(!PntLeastSquares(lsData->a,lsData->b,nCh,curSize,lsData->sol,NULL))
			return FAILURE_RETURN;
		MatrixSum(lsData->pos,curSize,1,lsData->sol,curSize,1,lsData->pos);
		if(iter>0) {
			converged=TRUE_FLAG;
			for(k=0;k<curSize;k++) if(!isfinite(lsData->pos[k]) || fabs(lsData->sol[k])>1e-4) converged=FALSE_FLAG;
			if(converged) break;
		}
	}
	if(!converged) return FAILURE_RETURN;
	/* Compute the velocity and the clock drift */
	for(chIdx=0;chIdx<nCh;chIdx++)
	{
		pvtCh = &(pvtData->pvtChArray[chIdx]);
		satStatus = pvtCh->satStatus;
		lsData->distVect[0] = lsData->xRotate[(0*nCh)+chIdx] - lsData->pos[0];
		lsData->distVect[1] = lsData->xRotate[(1*nCh)+chIdx] - lsData->pos[1];
		lsData->distVect[2] = lsData->xRotate[(2*nCh)+chIdx] - lsData->pos[2];
		MatrixProduct(lsData->distVect,1,3,lsData->distVect,3,1,&(lsData->distance));
		lsData->distance = sqrt(lsData->distance);
		if(!isfinite(lsData->distance) || lsData->distance<=0) return FAILURE_RETURN;
		aCorrectRow[0] = lsData->ac[(4*chIdx)+0] = lsData->distVect[0]/lsData->distance;
		aCorrectRow[1] = lsData->ac[(4*chIdx)+1] = lsData->distVect[1]/lsData->distance;
		aCorrectRow[2] = lsData->ac[(4*chIdx)+2] = lsData->distVect[2]/lsData->distance;
		lsData->ac[(4*chIdx)+3] = 1.0;
		angle=OMEGAE_DOT*lsData->distance/SPEED_OF_LIGHT;
		memcpy(vs,satStatus->ecefSatVel,sizeof(vs));
		lsData->satVel[0] = cos(angle)*vs[0]+sin(angle)*vs[1];
		lsData->satVel[1] = -sin(angle)*vs[0]+cos(angle)*vs[1];
		lsData->satVel[2] = vs[2];
		MatrixProduct(aCorrectRow,1,3,lsData->satVel,3,1,&prod);
		lsData->satRadialVelAndDoppler[chIdx] = SPEED_OF_LIGHT / L1_FREQUENCY * pvtCh->deltaPseudorange + prod-SPEED_OF_LIGHT*pvtCh->satClkDrift;
		if(cfg->pvtConfiguration.weightedLs) {
			if(!isfinite(pvtCh->velSigma2) || pvtCh->velSigma2<=0 ||
			   !isfinite(satStatus->elevation)) return FAILURE_RETURN;
			weight=sqrt(fmax(1e-3,sin(satStatus->elevation))/pvtCh->velSigma2);
			for(k=0;k<4;k++) lsData->ac[4*chIdx+k]*=weight;
			lsData->satRadialVelAndDoppler[chIdx]*=weight;
		}
	}
	if(!PntLeastSquares(lsData->ac,lsData->satRadialVelAndDoppler,nCh,4,lsData->userVel,NULL))
		return FAILURE_RETURN;
	/* Save the results */
	pvtRes->ecefPosition[0] = lsData->pos[0];
	pvtRes->ecefPosition[1] = lsData->pos[1];
	pvtRes->ecefPosition[2] = lsData->pos[2];
	pvtRes->clockBiasGps = lsData->pos[idxGpsBias];
	pvtRes->clockBiasGalileo = lsData->pos[idxGalBias];
	pvtRes->ecefVelocity[0] = lsData->userVel[0];
	pvtRes->ecefVelocity[1] = lsData->userVel[1];
	pvtRes->ecefVelocity[2] = lsData->userVel[2];
	pvtRes->clockDrift = lsData->userVel[3];
	if(pvtData->numOfGpsOpChannels>0)
	{
		pvtRes->clockBiasGpsValid=TRUE_FLAG;
	}
	else
	{
		pvtRes->clockBiasGpsValid=FALSE_FLAG;
	}
	if(pvtData->numOfGalileoOpChannels>0)
	{
		pvtRes->clockBiasGalileoValid=TRUE_FLAG;
	}
	else
	{
		pvtRes->clockBiasGalileoValid=FALSE_FLAG;
	}
	/* correct the atmospheric errors in pseudorangeCsm (for the log) */
	for(chIdx=0;chIdx<nCh;chIdx++)
	{
		pvtCh = &(pvtData->pvtChArray[chIdx]);
		pvtCh->pseudorangeCsm += (-pvtCh->tropoCorrection - pvtCh->ionoCorrection);
	}
	return SUCCESS_RETURN;
}
