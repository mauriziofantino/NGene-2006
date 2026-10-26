/** @file fft.c
 * @brief Native x64 adaptation of EVO's Q14 FFT scheduling.
 * @details Eight-point codelets, contiguous bit-reversal gathers and cache-local
 * subtrees derive from nav-ngene-evo bb61e1b (src/dsp/fft/fft.c). The Classic
 * gain API and truncated Q14 coefficients are retained: FFT_codes.dat, the
 * correlation product, thresholds and inverse-by-forward convention stay valid.
 */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "fft.h"
#include "fft_kernels.h"
#include "ngeneConstants.h"
#include "ioFunctions.h"

#define MAX_LOG2_FFT_SIZE 14u

int InitFftTable(TNGeneConfiguration *cfg, TFftTable *table)
{
    unsigned i, width, bits;
    short *tw;
    (void)cfg;
    if (!table) return FAILURE_RETURN;
    memset(table,0,sizeof(*table));
    table->maxLog2FftSize=MAX_LOG2_FFT_SIZE;
    table->maxFftSize=1u<<MAX_LOG2_FFT_SIZE;
    table->reverse=malloc(table->maxFftSize*sizeof(*table->reverse));
    table->twiddles=malloc(4u*(table->maxFftSize-1u)*sizeof(short));
    if (!table->reverse || !table->twiddles) {
        DeinitFftTable(table);
        SwrxLog(NGENE_LOG_SEVERITY_ERROR,"Cannot allocate FFT tables");
        return FAILURE_RETURN;
    }
    for (i=0;i<table->maxFftSize;i++) {
        unsigned value=i, reversed=0;
        for (bits=0;bits<table->maxLog2FftSize;bits++) {
            reversed=(reversed<<1)|(value&1u);
            value>>=1;
        }
        table->reverse[i]=reversed;
    }
    tw=(short *)table->twiddles;
    for (width=2;width<=table->maxFftSize;width*=2) {
        for (i=0;i<width/2;i++) {
            double phase=2.0*M_PI*i/width;
            /* Classic truncates instead of EVO's lround. Keeping this convention
             * avoids changing the quantization of cached local-code spectra. */
            short c=(short)(16384.0*cos(phase));
            short s=-(short)(16384.0*sin(phase));
            *tw++=c; *tw++=(short)-s; *tw++=s; *tw++=c;
        }
    }
    return SUCCESS_RETURN;
}

void DeinitFftTable(TFftTable *table)
{
    if (!table) return;
    free(table->reverse);
    free(table->twiddles);
    memset(table,0,sizeof(*table));
}

int PerformFftChecked(TNGeneConfiguration *cfg, TFftTable *table, short *input,
               short *output, unsigned log2Size, int gain, unsigned *clipped)
{
    unsigned n, stride, quarter, i, chunk, block, width, base, stage;
    unsigned scaledStages;
    int bounded;
    uintptr_t a=(uintptr_t)input, b=(uintptr_t)output;
    const short *tw;
    (void)cfg;
    if (clipped) *clipped=0;
    if (!table || !table->reverse || !table->twiddles || !input || !output ||
        log2Size<3 || log2Size>table->maxLog2FftSize || log2Size>MAX_LOG2_FFT_SIZE ||
        gain>0 || gain<-(int)log2Size || ((a|b)&1u)) {
        SwrxLog(NGENE_LOG_SEVERITY_ERROR,"Invalid FFT plan, size, gain or int16 alignment");
        return FAILURE_RETURN;
    }
    n=1u<<log2Size;
    if ((a>b ? a-b : b-a)<2u*n*sizeof(short)) {
        SwrxLog(NGENE_LOG_SEVERITY_ERROR,"FFT input and output overlap");
        return FAILURE_RETURN;
    }
    scaledStages=(unsigned)-gain;
    bounded=FftInputInRangeSse2(input,n);
    stride=table->maxFftSize/n;
    quarter=n/4;
    /* EVO's involutive gather writes contiguous quartets rather than scattering. */
    for (i=0;i<n;i+=4) {
        unsigned r=table->reverse[i*stride];
        memcpy(output+2*i,input+2*r,2*sizeof(short));
        memcpy(output+2*(i+1),input+2*(r+2*quarter),2*sizeof(short));
        memcpy(output+2*(i+2),input+2*(r+quarter),2*sizeof(short));
        memcpy(output+2*(i+3),input+2*(r+3*quarter),2*sizeof(short));
    }
    tw=(const short *)table->twiddles;
    block=n<512 ? n : 512;
    /* Stage scaling is determined by the absolute radix-2 stage, not by chunk.
     * This retains Classic's first -gain right shifts even across subtrees. */
    for (chunk=0;chunk<n;chunk+=block) {
        FftFirstStagesSse2(output+2*chunk,block,scaledStages,bounded,clipped);
        for (width=16,stage=4;width<=block;width*=2,stage++) {
            for (base=chunk;base<chunk+block;base+=width)
                FftButterfliesSse2(output+2*base,output+2*base+width,
                    tw+2*width-4,width/2,stage<=scaledStages,bounded,clipped);
        }
    }
    for (width=2*block; width<=n; width*=2) {
        stage=0;
        for (i=width;i>1;i>>=1) stage++;
        for (base=0;base<n;base+=width)
            FftButterfliesSse2(output+2*base,output+2*base+width,
                tw+2*width-4,width/2,stage<=scaledStages,bounded,clipped);
    }
    return SUCCESS_RETURN;
}

int PerformFft(TNGeneConfiguration *cfg, TFftTable *table, short *input,
               short *output, unsigned log2Size, int gain)
{
    return PerformFftChecked(cfg,table,input,output,log2Size,gain,NULL);
}
