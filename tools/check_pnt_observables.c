/* Independent measurement contracts, outside the receiver executable. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "carrierSmoothing.h"
#include "ngeneConstants.h"
#include "egnosPrns.h"
#include "prnList.h"
#include "sbasValidity.h"

int SwrxLog(int severity,const char *format,...) { (void)severity;(void)format;return 0; }
void CloseNGene(TNGeneConfiguration *cfg,int status) { (void)cfg;(void)status;abort(); }
#define CHECK(x) do {if(!(x)){fprintf(stderr,"PNT observable failure line %d: %s\n",__LINE__,#x);return 1;}}while(0)

static int CheckSbasCodes(void)
{
    const unsigned first10[]={0671,0160,0013,01037,0536,01545};
    char bpsk[1023],mask[1023],legacy[2046],path[1024];
    TPrnList list={0};
    FILE *file;
    int i,k;
    snprintf(path,sizeof(path),"%sEGNOS_codes_v2.dat",NGENE_TEST_DATA_DIRECTORY);
    file=fopen(path,"rb");CHECK(file);
    for(i=0;i<EGNOS_NUMBER_OF_PRNS;i++) {
        unsigned bits=0;
        CHECK(GenerateEgnosCode(EgnosPrnAt(i),bpsk,mask));
        for(k=0;k<10;k++) bits=(bits<<1)|(bpsk[k]==1);
        CHECK(bits==first10[i]);
        for(k=0;k<1023;k++) CHECK((bpsk[k]==1 && mask[k]==0)||(bpsk[k]==-1 && mask[k]==-1));
        if(i<EGNOS_LEGACY_PRNS) {
            CHECK(fread(legacy,1,sizeof(legacy),file)==sizeof(legacy));
            CHECK(!memcmp(legacy,bpsk,1023));CHECK(!memcmp(legacy+1023,mask,1023));
        }
    }
    fclose(file);
    CHECK(!GenerateEgnosCode(999,bpsk,mask));
    CHECK(CreatePrnList(&list,0,0,1,0,NULL)==SUCCESS_RETURN);
    CHECK(GetNextAvailableFromPrnList(EGNOS_SAT,&list)==121);
    CHECK(GetNextAvailableFromPrnList(EGNOS_SAT,&list)==136);
    CHECK(DestroyPrnList(&list)==SUCCESS_RETURN);
    return 0;
}

static int CheckSmoothing(void)
{
    TNGeneConfiguration cfg={0};TPvtData data={0};TPvtResults result={0};
    TChannel channel={0};TPvtChannel observation={0};TCarrierSmoothingData history={0};
    TUsbFePars frontend={0};
    int sign;
    cfg.numberOfChannels=1;cfg.fePars=&frontend;cfg.ts=1e-6;
    cfg.pvtConfiguration.csmFilterLen=32;
    data.numOfOperativeChannels=1;data.numOfGpsOpChannels=1;
    data.csmData=&history;data.pvtChArray=&observation;observation.ch=&channel;
    channel.satType=GPS_SAT;channel.blocksize=1000;
    for(sign=-1;sign<=1;sign+=2) {
        ClearCsmData(&cfg,&data,&result);frontend.dopplerCorrection=sign;
        channel.dopplerShiftOld=sign*100;channel.dopplerShift=sign*250;
        UpdateCarrierSmoothingMeasure(&cfg,&channel,&data,&result);
        CHECK(fabs(history.csmMeasure-TWICE_M_PI*.1)<1e-12);
        /* All of the last block lies after the requested common receive epoch. */
        channel.bufferOffset=1000;observation.pseudorange=20000000;
        PerformCarrierSmoothing(&cfg,&data,&result);
        CHECK(fabs(history.lpc)<1e-12);
        channel.bufferOffset=0;
    }
    ClearCsmData(&cfg,&data,&result);channel.dopplerShiftOld=0;
    observation.pseudorange=20000010;PerformCarrierSmoothing(&cfg,&data,&result);
    observation.pseudorange=20000000;PerformCarrierSmoothing(&cfg,&data,&result);
    CHECK(fabs(observation.pseudorangeCsm-20000005)<1e-8);
    /* Same signal, changed broadcast clock/SBAS correction: no smoothing lag. */
    ClearCsmData(&cfg,&data,&result);observation.pseudorange=20000000;
    PerformCarrierSmoothing(&cfg,&data,&result);
    observation.satClkCorr=1e-6;observation.egnosFastCorrection=3;
    observation.pseudorange=20000000+SPEED_OF_LIGHT*1e-6+3;
    PerformCarrierSmoothing(&cfg,&data,&result);
    CHECK(fabs(observation.pseudorangeCsm-observation.pseudorange)<1e-8);
    /* Rebase receiver time by 100 m: preserve the continuous history. */
    result.clockBiasGpsCumulated=100;observation.pseudorange-=100;
    PerformCarrierSmoothing(&cfg,&data,&result);
    CHECK(fabs(observation.pseudorangeCsm-observation.pseudorange)<1e-8);
    ClearCsmDataChannel(&history);
    CHECK(history.csmCounter==0 && history.codeCorrectionOld==0 && history.csmMeasure==0);
    cfg.pvtConfiguration.csmFilterLen=0;observation.pseudorange=123;
    PerformCarrierSmoothing(&cfg,&data,&result);CHECK(observation.pseudorangeCsm==123);
    return 0;
}
int main(void)
{
    static TEgnosCorrections bank;
    bank.fastCorrections[0].valid=1;bank.longTermCorrections[0].validVelocityCode0=1;
    bank.longTermCorrections[0].validVelocityCode1=1;
    bank.bandIonoCorrections[0].igpIonoCorrections[0].valid=1;
    CHECK(!SbasAcceptMessage(&bank,0,604790));
    CHECK(!bank.fastCorrections[0].valid && !bank.longTermCorrections[0].validVelocityCode0);
    CHECK(!bank.longTermCorrections[0].validVelocityCode1);
    CHECK(!bank.bandIonoCorrections[0].igpIonoCorrections[0].valid);
    CHECK(!SbasAcceptMessage(&bank,2,49.999));
    CHECK(SbasAcceptMessage(&bank,2,50));
    CHECK(!SbasAcceptMessage(&bank,0,51));
    CHECK(!SbasAcceptMessage(&bank,0,56));
    CHECK(!SbasAcceptMessage(&bank,2,115.999));
    CHECK(SbasAcceptMessage(&bank,2,116));
    CHECK(CheckSbasCodes()==0);CHECK(CheckSmoothing()==0);
    puts("PNT observables: ICD SBAS codes, search list, Doppler epoch, Hatch ramp, clock/SBAS correction and rebase passed");
    return 0;
}
