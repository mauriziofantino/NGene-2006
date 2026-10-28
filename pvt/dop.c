/**
 * @file dop.c
 *
 * @author Mario Nicola
 *
 * @date 27/10/2014
 *
 * @version 1.0.0
 *
 * @brief DOP computation
 */

#include <math.h>
#include "pvtConfiguration.h"
#include "ngeneDataStructure.h"
#include "dop.h"
#include "matrices.h"
#include "pvtUtility.h"
#include "smallLeastSquares.h"

int DopEvaluation(TNGeneData *rx,int performedPvt)
{
	TPvtData *pvtData = &(rx->pvtData);
	TLeastSquaresData *lsData = &(pvtData->lsData);
	TPvtChannel *pvtCh;
	TChannel *ch;
	TPvtResults *pvtRes = &(rx->pvtTemporaryResults);
	TSatStatus *satStatus;
	int nCh = pvtData->numOfOperativeChannels;
	int size;
	double *satPos;
	double pos[5];
	int chIdx;
	double range, geod[3], up[3], vertical = 0, spatial;
	int i, j;
	(void)performedPvt;
	pvtRes->dopValid=FALSE_FLAG;
	for(i=0;i<5;i++) pvtRes->dop[i]=NAN;

	/* prepare inputs */
	pos[0] = pvtRes->ecefPosition[0];
	pos[1] = pvtRes->ecefPosition[1];
	pos[2] = pvtRes->ecefPosition[2];
	/* single or double constellation?
	 * size is the number of columns in the A matrix
	 * and also the minimum number of satellites in order to proceed
	 * with DOP calculation */
	if((pvtData->numOfGpsOpChannels>0) && (pvtData->numOfGalileoOpChannels>0))
	{
		size = LS_MULTI_SIZE;
		pos[3] = pvtRes->clockBiasGps;
		pos[4] = pvtRes->clockBiasGalileo;
	}
	else
	{
		size = LS_SINGLE_SIZE;
		if(pvtData->numOfGpsOpChannels>0)
		{
			pos[3] = pvtRes->clockBiasGps;
		}
		else
		{
			pos[3] = pvtRes->clockBiasGalileo;
		}
	}
	/* the DOP cannot be evaluated */
	if((pvtRes->pvtValid==FALSE_FLAG) || (pvtData->numOfOperativeChannels<size))
	{
		return FALSE_FLAG;
	}
	/* Rebuild at the final position, also after the last LS update. */
	{
		for(chIdx=0;chIdx<nCh;chIdx++)
		{
			pvtCh = &(pvtData->pvtChArray[chIdx]);
			ch = pvtCh->ch;
			satStatus = pvtCh->satStatus;
			satPos = satStatus->ecefSatPos;
			lsData->x[(0*nCh)+chIdx] = satPos[0];
			lsData->x[(1*nCh)+chIdx] = satPos[1];
			lsData->x[(2*nCh)+chIdx] = satPos[2];
			{
				double rotated[3];
				range=RotateSatellitePosition(satPos,pos,rotated);
				for(i=0;i<3;i++) lsData->xRotate[i*nCh+chIdx]=rotated[i];
			}
			if(!isfinite(range) || range <= 0) return FALSE_FLAG;
			// Build the A matrix
			lsData->a[(chIdx*size)+0] = -(lsData->xRotate[(0*nCh)+chIdx]-pos[0])/range;
			lsData->a[(chIdx*size)+1] = -(lsData->xRotate[(1*nCh)+chIdx]-pos[1])/range;
			lsData->a[(chIdx*size)+2] = -(lsData->xRotate[(2*nCh)+chIdx]-pos[2])/range;
			if(size==LS_MULTI_SIZE)
			{
				if(ch->satType==GPS_SAT)
				{
					lsData->a[(chIdx*size)+3] = 1.0;
					lsData->a[(chIdx*size)+4] = 0.0;
				}
				else
				{
					lsData->a[(chIdx*size)+3] = 0.0;
					lsData->a[(chIdx*size)+4] = 1.0;
				}
			}
			else
			{
				lsData->a[(chIdx*size)+3] = 1.0;
			}
		}
	}
	/* Elaborate A matrix and find DOPs */
	if(!SmallLeastSquares(lsData->a,NULL,nCh,size,NULL,lsData->invAtA)) return FALSE_FLAG;
	if(size==LS_MULTI_SIZE)
	{
		/* double constellation */
		pvtRes->dop[GDOP_INDEX_IN_DOP_ARRAY]  = sqrt(lsData->invAtA[0] + lsData->invAtA[6] + lsData->invAtA[12] + lsData->invAtA[18] + lsData->invAtA[24]);
		pvtRes->dop[PDOP_INDEX_IN_DOP_ARRAY]  = sqrt(lsData->invAtA[0] + lsData->invAtA[6] + lsData->invAtA[12]);
		pvtRes->dop[TDOP_INDEX_IN_DOP_ARRAY]  = sqrt(lsData->invAtA[18] + lsData->invAtA[24]);
	}
	else
	{
		/* single constellation */
		pvtRes->dop[GDOP_INDEX_IN_DOP_ARRAY]  = sqrt(lsData->invAtA[0] + lsData->invAtA[5] + lsData->invAtA[10] + lsData->invAtA[15]);
		pvtRes->dop[PDOP_INDEX_IN_DOP_ARRAY]  = sqrt(lsData->invAtA[0] + lsData->invAtA[5] + lsData->invAtA[10]);
		pvtRes->dop[TDOP_INDEX_IN_DOP_ARRAY]  = sqrt(lsData->invAtA[15]);
	}
	/* Up is the geodetic surface normal, not the ECEF Z axis. The trace
	 * of the spatial covariance is invariant under the ENU rotation. */
	Ecef2Geod(pos,geod); /* longitude, latitude, height */
	up[0] = cos(geod[1]*DEGREE_TO_RADIAN)*cos(geod[0]*DEGREE_TO_RADIAN);
	up[1] = cos(geod[1]*DEGREE_TO_RADIAN)*sin(geod[0]*DEGREE_TO_RADIAN);
	up[2] = sin(geod[1]*DEGREE_TO_RADIAN);
	for(i=0;i<3;i++) for(j=0;j<3;j++) vertical += up[i]*lsData->invAtA[i*size+j]*up[j];
	spatial = lsData->invAtA[0]+lsData->invAtA[size+1]+lsData->invAtA[2*size+2];
	if(!isfinite(vertical) || !isfinite(spatial)) return FALSE_FLAG;
	pvtRes->dop[VDOP_INDEX_IN_DOP_ARRAY] = sqrt(fmax(0,vertical));
	pvtRes->dop[HDOP_INDEX_IN_DOP_ARRAY] = sqrt(fmax(0,spatial-vertical));
	pvtRes->dopValid=TRUE_FLAG;
	return TRUE_FLAG;
}
