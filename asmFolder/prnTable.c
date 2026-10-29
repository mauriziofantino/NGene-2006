#include "egnosPrns.h"
/**
 * @file prnTable.c
 *
 * @author Mario Nicola
 *
 * @date 18/08/2014
 *
 * @version 1.0.0
 *
 * @brief Functions that manage the sampling of the spreading codes.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include <emmintrin.h>
#include "simd.h"
#include "ngeneConfiguration.h"
#include "prnTable.h"
#include "ngeneConstants.h"
#include "closeManager.h"
#include "spreadingCodes.h"
#include "ioFunctions.h"
#include "arrays.h"

/* Required GPS/Galileo records must be complete before they are oversampled.
 * SBAS codes are generated from ICD delays below. */
static int ReadRequiredCode(TNGeneConfiguration *cfg, FILE *file, char *code,
                            size_t length, const char *filename)
{
    if(fread(code, sizeof(char), length, file)==length)
        return SUCCESS_RETURN;
    SwrxLog(NGENE_LOG_SEVERITY_ERROR,
        "Incomplete or unreadable PRN code block in %s (expected %u bytes)",
        filename, (unsigned int)length);
    fclose(file);
    CloseNGene(cfg, EXIT_FAILURE);
    return FAILURE_RETURN;
}

void OversampleCode(char *code,int codeLen,s128 *oversampled)
{
    int i;
    for(i=0;i<codeLen;i++) {
        __m128i row=_mm_set1_epi8(code[i]);
        _mm_store_si128((__m128i *)(oversampled+i),row);
        _mm_store_si128((__m128i *)(oversampled+codeLen+i),row);
    }
    _mm_store_si128((__m128i *)(oversampled+2*codeLen),_mm_set1_epi8(code[0]));
}
int InitPrnTable(TNGeneConfiguration *cfg,TPrnTable *t)
{
	int errorsFound = 0;
	int index;
	int gpsSize = (2*GPS_CODE_LEN)+1;
	int egnosSize = (2*EGNOS_CODE_LEN)+1;
	int galileoSize = (2*GALILEO_CODE_LEN)+1;
	char code[GALILEO_CODE_LEN];
	char filename[MAX_ROW_LENGTH];
	FILE *codeFile;
	char row[MAX_ROW_LENGTH];
	int prn;

	/* Allocate GPS */
	for(index=0;index<GPS_NUMBER_OF_PRNS;index++)
	{
		posix_memalign((void **)&(t->gpsChipsOversampledBpsk[index]),POSIX_ALIGNMENT,gpsSize*sizeof(s128));
		if(t->gpsChipsOversampledBpsk[index]==NULL)
			errorsFound++;

		posix_memalign((void **)&(t->gpsChipsOversampled[index]),POSIX_ALIGNMENT,gpsSize*sizeof(s128));
		if(t->gpsChipsOversampled[index]==NULL)
			errorsFound++;
	}
	/* Allocate EGNOS */
	for(index=0;index<EGNOS_NUMBER_OF_PRNS;index++)
	{
		posix_memalign((void **)&(t->egnosChipsOversampledBpsk[index]),POSIX_ALIGNMENT,egnosSize*sizeof(s128));
		if(t->egnosChipsOversampledBpsk[index]==NULL)
			errorsFound++;

		posix_memalign((void **)&(t->egnosChipsOversampled[index]),POSIX_ALIGNMENT,egnosSize*sizeof(s128));
		if(t->egnosChipsOversampled[index]==NULL)
			errorsFound++;
	}
	/* Allocate Galileo */
	for(index=0;index<GALILEO_NUMBER_OF_PRNS;index++)
	{
		posix_memalign((void **)&(t->galileoChipsOversampledBpsk[index]),POSIX_ALIGNMENT,galileoSize*sizeof(s128));
		if(t->galileoChipsOversampledBpsk[index]==NULL)
			errorsFound++;

		posix_memalign((void **)&(t->galileoChipsOversampled[index]),POSIX_ALIGNMENT,galileoSize*sizeof(s128));
		if(t->galileoChipsOversampled[index]==NULL)
			errorsFound++;
	}
	/* errors found */
	if(errorsFound>0)
	{
		SwrxLog(NGENE_LOG_SEVERITY_ERROR,"Malloc returned a NULL pointer in InitPrnTable");
		CloseNGene(cfg,EXIT_FAILURE);
	}
	/* Initialize from file */
	/* GPS */
	filename[0] = '\0';
	strcat(filename, cfg->otherDataPath);
	strcat(filename, "GPS_codes.dat");
	codeFile = fopen(filename,"rb");
	if(codeFile==NULL)
	{
		SwrxLog(NGENE_LOG_SEVERITY_ERROR,"Cannot read file with GPS despreading codes (%s)",filename);
		CloseNGene(cfg,EXIT_FAILURE);
	}
	for(index=0;index<GPS_NUMBER_OF_PRNS;index++)
	{
		if(ReadRequiredCode(cfg,codeFile,code,GPS_CODE_LEN,filename)!=SUCCESS_RETURN)
			return FAILURE_RETURN;
		OversampleCode(code, GPS_CODE_LEN,t->gpsChipsOversampledBpsk[index]);
		if(ReadRequiredCode(cfg,codeFile,code,GPS_CODE_LEN,filename)!=SUCCESS_RETURN)
			return FAILURE_RETURN;
		OversampleCode(code, GPS_CODE_LEN,t->gpsChipsOversampled[index]);
	}
	fclose(codeFile);
    /* Generate SBAS codes from ICD delays; no silent zero-filled PRNs. */
    for(index=0;index<EGNOS_NUMBER_OF_PRNS;index++) {
        char mask[EGNOS_CODE_LEN];
        if(!GenerateEgnosCode(EgnosPrnAt(index),code,mask)) return FAILURE_RETURN;
        OversampleCode(code,EGNOS_CODE_LEN,t->egnosChipsOversampledBpsk[index]);
        OversampleCode(mask,EGNOS_CODE_LEN,t->egnosChipsOversampled[index]);
    }
	/* Galileo E1b */
	filename[0] = '\0';
	strcat(filename, cfg->otherDataPath);
	strcat(filename, "Galileo_E1b_codes.dat");
	codeFile = fopen(filename,"rb");
	if(codeFile==NULL)
	{
		SwrxLog(NGENE_LOG_SEVERITY_ERROR,"Cannot read file with Galileo despreading codes (%s)",filename);
		CloseNGene(cfg,EXIT_FAILURE);
	}
	for(index=0;index<GALILEO_NUMBER_OF_PRNS;index++)
	{
		if(ReadRequiredCode(cfg,codeFile,code,GALILEO_CODE_LEN,filename)!=SUCCESS_RETURN)
			return FAILURE_RETURN;
		OversampleCode(code, GALILEO_CODE_LEN,t->galileoChipsOversampledBpsk[index]);
		if(ReadRequiredCode(cfg,codeFile,code,GALILEO_CODE_LEN,filename)!=SUCCESS_RETURN)
			return FAILURE_RETURN;
		OversampleCode(code, GALILEO_CODE_LEN,t->galileoChipsOversampled[index]);
	}
	fclose(codeFile);
	/* Manage real Galileo satellites */
	t->numberOfRealGalileoSat = 0;
	t->prnOfRealGalileoSat = NULL;
	if(cfg->numberOfGalileoChannels>0)
	{
		filename[0] = '\0';
		strcat(filename, cfg->otherDataPath);
		strcat(filename, "Galileo_real_PRNs.txt");
		codeFile = fopen(filename,"r");
		if(codeFile==NULL)
		{
			SwrxLog(NGENE_LOG_SEVERITY_ERROR,"Cannot read Galileo PRN search list %s",filename);
			return FAILURE_RETURN;
		}
		else
		{
			while(fgets(row,MAX_ROW_LENGTH,codeFile)!=NULL)
			{
				if((row[0]!='#') && (sscanf(row,"%d",&prn)==1))
				{
					if(prn<BASE_PRN || prn>GALILEO_NUMBER_OF_PRNS) {
						fclose(codeFile);
						SwrxLog(NGENE_LOG_SEVERITY_ERROR,"Invalid Galileo PRN %d in %s",prn,filename);
						return FAILURE_RETURN;
					}
					t->numberOfRealGalileoSat++;
				}
			}
			if(t->numberOfRealGalileoSat==0) {
				fclose(codeFile);
				SwrxLog(NGENE_LOG_SEVERITY_ERROR,"Empty Galileo PRN search list %s",filename);
				return FAILURE_RETURN;
			}
			t->prnOfRealGalileoSat = (int *)malloc(t->numberOfRealGalileoSat*sizeof(int));
			if(t->prnOfRealGalileoSat==NULL)
			{
				SwrxLog(NGENE_LOG_SEVERITY_ERROR,"Malloc returned a NULL pointer in InitPrnTable");
				CloseNGene(cfg,EXIT_FAILURE);
			}
			rewind(codeFile);
			index = 0;
			while(fgets(row,MAX_ROW_LENGTH,codeFile)!=NULL)
			{
				if((row[0]!='#') && (sscanf(row,"%d",&prn)==1))
				{
					t->prnOfRealGalileoSat[index] = prn;
					index++;
				}
			}
			fclose(codeFile);
		}
	}
	/* other variables */
	t->scaling = floor((double)UINT_MAX/(4.0*cfg->fs));
	t->ncoThreshold = (unsigned int)floor(t->scaling*cfg->fs);
	t->chipShortLen = (unsigned int)floor(cfg->fs/(double)GPS_CODE_RATE);
	t->chipLongLen = t->chipShortLen +1;
	return SUCCESS_RETURN;
}

void DeinitPrnTable(TPrnTable *t)
{
	int index;

	for(index=0;index<GPS_NUMBER_OF_PRNS;index++)
	{
		free(t->gpsChipsOversampledBpsk[index]);
		free(t->gpsChipsOversampled[index]);
	}
	for(index=0;index<EGNOS_NUMBER_OF_PRNS;index++)
	{
		free(t->egnosChipsOversampledBpsk[index]);
		free(t->egnosChipsOversampled[index]);
	}
	for(index=0;index<GALILEO_NUMBER_OF_PRNS;index++)
	{
		free(t->galileoChipsOversampledBpsk[index]);
		free(t->galileoChipsOversampled[index]);
	}
	if(t->prnOfRealGalileoSat!=NULL)
	{
		free(t->prnOfRealGalileoSat);
		t->prnOfRealGalileoSat = NULL;
	}
}

int GenerateCode(TNGeneConfiguration *cfg,TPrnTable *t,TSatConstellation prnConstellation,int prn,double codeRate,double *codePhase,double *codePhaseFrac,TEmployedModulation employedModulation,int n,char *codeOut)
{
	char *code;
	unsigned int chipIndex;
	unsigned int nco;
	unsigned int ncoIncrement;
	unsigned int ncoChipVeryShort;
	unsigned int ncoChipShort;
	int ncoIncrementChipShort;
	int ncoIncrementChipLong;
	char *ptrOut = codeOut;
	int index=n;
	unsigned int index2;
	__m128i row;

	/* initialization */
	if(prnConstellation==GPS_SAT)
	{
		if(prn>GPS_NUMBER_OF_PRNS)
			return FAILURE_RETURN;
		code = &(t->gpsChipsOversampled[prn-BASE_PRN][0].b[0]);
		if(employedModulation==BPSK_MODULATION)
			code = &(t->gpsChipsOversampledBpsk[prn-BASE_PRN][0].b[0]);
	}
	else if(prnConstellation==GALILEO_SAT)
	{
		if(prn>GALILEO_NUMBER_OF_PRNS)
			return FAILURE_RETURN;
		code = &(t->galileoChipsOversampled[prn-BASE_PRN][0].b[0]);
		if(employedModulation==BPSK_MODULATION)
			code = &(t->galileoChipsOversampledBpsk[prn-BASE_PRN][0].b[0]);
	}
	else /* EGNOS_SAT */
	{
        int slot=EgnosPrnIndex(prn);
        if(slot<0) return FAILURE_RETURN;
        code=&t->egnosChipsOversampled[slot][0].b[0];
        if(employedModulation==BPSK_MODULATION) code=&t->egnosChipsOversampledBpsk[slot][0].b[0];
	}
	/* Without floor in the following lines,
	 * the receiver compiled in release and in Ubuntu 64 bit does not work:
	 * codePhase is fixed to 0.0 and tracking unlocks */
	/* TODO Discover why!!! */
	chipIndex = (unsigned int)floor(*codePhase);
	nco = (unsigned int)floor(((double)t->ncoThreshold)*(*codePhaseFrac));
	ncoIncrement = (unsigned int)floor(codeRate*t->scaling);
	ncoChipShort = t->chipShortLen*ncoIncrement;
	ncoChipVeryShort = ncoChipShort - ncoIncrement;
	ncoIncrementChipShort = ncoChipShort - t->ncoThreshold;
	ncoIncrementChipLong = ncoIncrementChipShort + ncoIncrement;
	code += (chipIndex*XMM_REGISTER_SIZE);
	/* initial transient */
	if((nco+ncoChipVeryShort)>t->ncoThreshold)
	{
		while(nco<t->ncoThreshold)
		{
			ptrOut[0]=code[0];
			index--;
			nco += ncoIncrement;
			ptrOut++;
		}
		code += XMM_REGISTER_SIZE;
		nco -= t->ncoThreshold;
	}
	/**/
	/* Intrinsics declare register use, so optimization preserves codeRate. */
	while(index>=0)
	{
		if((nco+ncoChipShort)<t->ncoThreshold)
		{
			index2=0;
			while(index2<t->chipLongLen)
			{
				row = _mm_loadu_si128((const __m128i *)code);
				_mm_storeu_si128((__m128i *)(ptrOut+index2),row);
				index2 += XMM_REGISTER_SIZE;
			}
			nco += ncoIncrementChipLong;
			code += XMM_REGISTER_SIZE;
			index -= t->chipLongLen;
			ptrOut += t->chipLongLen;
		}
		else
		{
			index2=0;
			while(index2<t->chipShortLen)
			{
				row = _mm_loadu_si128((const __m128i *)code);
				_mm_storeu_si128((__m128i *)(ptrOut+index2),row);
				index2 += XMM_REGISTER_SIZE;
			}
			code += XMM_REGISTER_SIZE;
			nco += ncoIncrementChipShort;
			index -= t->chipShortLen;
			ptrOut += t->chipShortLen;
		}
	}

	/* update codePhase */
	UpdateCodePhase(cfg,prnConstellation,codePhase,codePhaseFrac,n,codeRate);
	return SUCCESS_RETURN;
}

void UpdateCodePhase(TNGeneConfiguration *cfg,TSatConstellation satType, double *codePhase, double *codePhaseFrac, unsigned int nSamples, double codeRate)
{
	float codeLen;

	if(satType==GPS_SAT)
		codeLen = (float)GPS_CODE_LEN;
	else if(satType==GALILEO_SAT)
		codeLen = (float)GALILEO_CODE_LEN;
	else
		codeLen = (float)EGNOS_CODE_LEN;

	(*codePhase) += ((double)nSamples*codeRate*cfg->ts);
	while((*codePhase)>=codeLen)
		(*codePhase) -= codeLen;

	(*codePhaseFrac) = (*codePhase)-floor(*codePhase);
}

void AddCodePhases(TSatConstellation satType, double codePhase1, double codePhase2, double *codePhase, double *codePhaseFrac)
{
	float codeLen;

	if(satType==GPS_SAT)
		codeLen = (float)GPS_CODE_LEN;
	else if(satType==GALILEO_SAT)
		codeLen = (float)GALILEO_CODE_LEN;
	else
		codeLen = (float)EGNOS_CODE_LEN;

	(*codePhase) = codePhase1 + codePhase2;

	while((*codePhase)>=codeLen)
		(*codePhase) -= codeLen;

	while((*codePhase)<0)
		(*codePhase) += codeLen;

	(*codePhaseFrac) = (*codePhase)-floor(*codePhase);
}
