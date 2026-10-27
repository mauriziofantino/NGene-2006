/** @file check_fft.c @brief Independent scalar/DFT checks of the EVO-to-Classic FFT port. */
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "fft.h"
#include "ngeneConstants.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FFT check failed at %d: %s\n",__LINE__,#c); exit(1); } } while (0)
static unsigned checked;
static uint32_t randomState=0x4e47454eu;

int SwrxLog(int severity, const char *format, ...)
{
    (void)severity; (void)format;
    return 0;
}

static uint32_t Random(void)
{
    randomState=randomState*1664525u+1013904223u;
    return randomState;
}

/** Independent signed64 arithmetic, scalar scatter and breadth-first schedule. */
static int64_t FloorShift(int64_t value, unsigned shift)
{
    int64_t divisor=(int64_t)1<<shift;
    return value>=0 ? value/divisor : -1-((-1-value)/divisor);
}

static short Narrow(int64_t value)
{
    return (short)(value<SHRT_MIN ? SHRT_MIN : value>SHRT_MAX ? SHRT_MAX : value);
}

static void Reference(const short *input, short *output, unsigned bits, int gain)
{
    unsigned n=1u<<bits, i, width, stage;
    for (i=0;i<n;i++) {
        unsigned value=i, reverse=0, bit;
        for (bit=0;bit<bits;bit++) { reverse=(reverse<<1)|(value&1u); value>>=1; }
        output[2*reverse]=input[2*i]; output[2*reverse+1]=input[2*i+1];
    }
    for (width=2,stage=1;width<=n;width*=2,stage++) {
        unsigned j, base, shift=stage<=(unsigned)-gain;
        for (j=0;j<width/2;j++) {
            double phase=2.0*M_PI*j/width;
            short c=(short)(16384*cos(phase));
            short s=-(short)(16384*sin(phase));
            for (base=0;base<n;base+=width) {
                unsigned u=2*(base+j), v=u+width;
                int64_t real=FloorShift((int64_t)output[v]*c-(int64_t)output[v+1]*s,14);
                int64_t imag=FloorShift((int64_t)output[v]*s+(int64_t)output[v+1]*c,14);
                int64_t a=output[u], b=output[u+1];
                output[u]=Narrow(FloorShift(a+real,shift));
                output[u+1]=Narrow(FloorShift(b+imag,shift));
                output[v]=Narrow(FloorShift(a-real,shift));
                output[v+1]=Narrow(FloorShift(b-imag,shift));
            }
        }
    }
}

static void Compare(TFftTable *table, const short *input, unsigned bits, int gain, unsigned skew)
{
    unsigned n=1u<<bits, i, clipped, trialClipped;
    size_t count=2u*n+24;
    short *source=malloc(count*sizeof(short)), *workspace=malloc(count*sizeof(short));
    short *expected=malloc(2u*n*sizeof(short));
    short *a, *b;
    CHECK(source && workspace && expected);
    for (i=0;i<count;i++) source[i]=workspace[i]=23130;
    a=source+1+skew; b=workspace+8-skew;
    memcpy(a,input,2u*n*sizeof(short));
    Reference(input,expected,bits,gain);
    CHECK(PerformFftChecked(NULL,table,a,b,bits,gain,&clipped)==SUCCESS_RETURN);
    for (i=0;i<2*n;i++) {
        if (b[i]!=expected[i]) {
            fprintf(stderr,"N=%u gain=%d component=%u actual=%d reference=%d\n",n,gain,i,b[i],expected[i]);
            CHECK(b[i]==expected[i]);
        }
    }
    CHECK(PerformFftTrial(NULL,table,a,b,bits,gain,&trialClipped)==SUCCESS_RETURN);
    CHECK((trialClipped!=0)==(clipped!=0));
    if (!trialClipped) CHECK(memcmp(b,expected,2u*n*sizeof(short))==0);
    CHECK(memcmp(a,input,2u*n*sizeof(short))==0);
    for (i=0;i<count;i++) {
        if (i<1+skew || i>=1+skew+2*n) CHECK(source[i]==23130);
        if (i<8-skew || i>=8-skew+2*n) CHECK(workspace[i]==23130);
    }
    checked++;
    free(source); free(workspace); free(expected);
}

static void CheckDft(TFftTable *table)
{
    short input[128], output[128];
    unsigned bits, n, i, k;
    for (bits=3;bits<=6;bits++) {
        n=1u<<bits;
        for (i=0;i<2*n;i++) input[i]=(short)((int)(Random()%4097)-2048);
        CHECK(PerformFft(NULL,table,input,output,bits,-(int)bits)==SUCCESS_RETURN);
        for (k=0;k<n;k++) {
            double real=0,imag=0;
            for (i=0;i<n;i++) {
                double phase=-2*M_PI*k*i/n, c=cos(phase), s=sin(phase);
                real+=input[2*i]*c-input[2*i+1]*s;
                imag+=input[2*i]*s+input[2*i+1]*c;
            }
            CHECK(fabs(output[2*k]-real/n)<=bits+2);
            CHECK(fabs(output[2*k+1]-imag/n)<=bits+2);
        }
    }
    for (i=0;i<64;i++) {
        input[2*i]=(short)(2048*cos(2*M_PI*5*i/64));
        input[2*i+1]=(short)(2048*sin(2*M_PI*5*i/64));
    }
    CHECK(PerformFft(NULL,table,input,output,6,-6)==SUCCESS_RETURN);
    CHECK(abs(output[10]-2048)<=8 && abs(output[11])<=8);
    CHECK(abs(output[2*(64-5)])<=8 && abs(output[2*(64-5)+1])<=8);
    {
        /* Historical saturation-before-/2 error: independent DFT gives
         * bin 5 imaginary = 19777.2375. Classic returned 16383. */
        short rails[16]={-16384,16384,16384,-16384,-16384,-16384,16384,16384,
                         -16384,-16384,-16384,16384,16384,-16384,-16384,-16384};
        CHECK(PerformFft(NULL,table,rails,output,3,-3)==SUCCESS_RETURN);
        CHECK(output[11]==19777);
    }
}

typedef struct {
    TFftTable *table;
    short input[2048], expected[2048];
    int ok;
} TParallelCase;

static void *Parallel(void *argument)
{
    TParallelCase *test=argument;
    short output[2048];
    unsigned repeat;
    test->ok=1;
    for (repeat=0;repeat<100;repeat++) {
        if (PerformFft(NULL,test->table,test->input,output,10,-4)!=SUCCESS_RETURN ||
            memcmp(output,test->expected,sizeof(output))) { test->ok=0; break; }
    }
    return NULL;
}

int main(void)
{
    TFftTable table;
    short *input=malloc(32768*sizeof(short));
    unsigned bits, pattern, i, n;
    int gain;
    CHECK(input && InitFftTable(NULL,&table)==SUCCESS_RETURN);
    for (bits=3;bits<=14;bits++) {
        n=1u<<bits;
        for (pattern=0;pattern<8;pattern++) {
            for (i=0;i<2*n;i++) {
                switch(pattern) {
                case 0: input[i]=0; break;
                case 1: input[i]=(short)((int)(Random()%9)-4); break;
                case 2: input[i]=(short)((int)(Random()%4097)-2048); break;
                case 3: input[i]=(short)((int)(Random()%32769)-16384); break;
                case 4: input[i]=(short)((int)(Random()&65535)-32768); break;
                case 5: input[i]=(i&1) ? 16384 : -16384; break;
                case 6: input[i]=(i&2) ? SHRT_MAX : SHRT_MIN; break;
                default: input[i]=(i&3) ? -1 : 1; break;
                }
            }
            for (gain=0;gain>=-(int)bits;gain--)
                Compare(&table,input,bits,gain,(bits+pattern+(unsigned)-gain)%8);
        }
    }
    /* All independent I/Q rail-sign assignments exercise the fused codelet. */
    for (pattern=0;pattern<65536;pattern++) {
        for(i=0;i<16;i++) input[i]=(pattern&(1u<<i)) ? 16384 : -16384;
        for(gain=0;gain>=-3;gain--) Compare(&table,input,3,gain,pattern%8);
    }
    CheckDft(&table);
    CHECK(PerformFft(NULL,&table,input,input,3,0)==FAILURE_RETURN);
    CHECK(PerformFft(NULL,&table,input,input+1,3,0)==FAILURE_RETURN);
    CHECK(PerformFft(NULL,&table,input,input+32,2,0)==FAILURE_RETURN);
    CHECK(PerformFft(NULL,&table,input,input+32,15,0)==FAILURE_RETURN);
    CHECK(PerformFft(NULL,&table,input,input+32,3,1)==FAILURE_RETURN);
    CHECK(PerformFft(NULL,&table,input,input+32,3,INT_MIN)==FAILURE_RETURN);
    CHECK(PerformFftTrial(NULL,&table,input,input+32,3,0,NULL)==FAILURE_RETURN);
    {
        TParallelCase cases[2]; pthread_t threads[2];
        for (pattern=0;pattern<2;pattern++) {
            cases[pattern].table=&table;
            for (i=0;i<2048;i++) cases[pattern].input[i]=(short)((int)(Random()%16385)-8192);
            Reference(cases[pattern].input,cases[pattern].expected,10,-4);
            CHECK(pthread_create(&threads[pattern],NULL,Parallel,&cases[pattern])==0);
        }
        for (pattern=0;pattern<2;pattern++) CHECK(pthread_join(threads[pattern],NULL)==0 && cases[pattern].ok);
    }
    DeinitFftTable(&table); DeinitFftTable(&table);
    CHECK(PerformFft(NULL,&table,input,input+32,3,0)==FAILURE_RETURN);
    free(input);
    printf("FFT: %u SIMD/reference cases, all sizes/gains, guarded alignments, DFT, tone, invalid inputs and concurrent calls passed.\n",checked);
    return 0;
}
