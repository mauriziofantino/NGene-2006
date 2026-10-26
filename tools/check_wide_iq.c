#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef NGENE_NATIVE_WINDOWS
#include <windows.h>
#endif
#include "wideInput.h"
#include "fft.h"
#include "sampleCompression.h"
#include "carrierTable.h"
#include "ngeneConstants.h"

char *ngeneXmmsBackup;
int SwrxLog(int severity, const char *format, ...) { (void)severity; (void)format; return 0; }
void CloseNGene(TNGeneConfiguration *cfg, int result) { (void)cfg; (void)result; abort(); }
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"Wide IQ failure line %d: %s\n",__LINE__,#x); return 1; } } while(0)

static int Adapter(void)
{
    unsigned char raw[1026];
    TWideIq out[514];
    unsigned n, offset, k, pattern;
    for(pattern=0;pattern<4;pattern++) for(offset=0;offset<16;offset++) for(n=1;n<=512;n++) {
        double si=0,sq=0,ss=0,rms,gain=1;
        if(2*n+offset>sizeof(raw)) continue;
        for(k=0;k<n;k++) {
            int i=pattern==0?(int)(k%256)-128:pattern==1?(int)(k%29)-12:pattern==2?127:((k==0)?-128:127);
            int q=pattern==0?127-(int)(k%256):pattern==1?(int)((k*7)%31)-10:pattern==2?-128:127;
            raw[offset+2*k]=(unsigned char)i; raw[offset+2*k+1]=(unsigned char)q;
            si+=i; sq+=q; ss+=i*i+q*q;
        }
        si/=n; sq/=n;
        rms=sqrt(fmax(0,ss/(2*n)-(si*si+sq*sq)/2));
        if(rms>0) { while(rms<1.5 && gain<256) { rms*=2; gain*=2; }
            while(rms>3 && gain>1.0/128) { rms*=0.5; gain*=0.5; } }
        out[n].i=999;
        AdaptIqWide(raw+offset,out,n);
        CHECK(out[n].i==999);
        for(k=0;k<n;k++) {
            double ei=lround(((signed char)raw[offset+2*k]*256-lround(si*256))*gain);
            double eq=lround(((signed char)raw[offset+2*k+1]*256-lround(sq*256))*gain);
            CHECK(out[k].i==fmin(1792,fmax(-1792,ei)));
            CHECK(out[k].q==fmin(1792,fmax(-1792,eq)));
            if(pattern==0 && k && k%256 && abs(out[k].i)<1792 && abs(out[k-1].i)<1792) CHECK(out[k].i>out[k-1].i);
        }
    }
    /* Full 16 ms radio blocks span several bounded SIMD statistic reductions. */
    {
        const unsigned length=131072;
        unsigned char *large=malloc(2*length);
        TWideIq *result=malloc(length*sizeof(*result));
        CHECK(large && result);
        for(k=0;k<length;k++) { large[2*k]=(unsigned char)(k%256); large[2*k+1]=(unsigned char)(255-k%256); }
        AdaptIqWide(large,result,length);
        for(k=0;k<length;k++) {
            CHECK(result[k].i==((signed char)large[2*k]*256+128)/32);
            CHECK(result[k].q==((signed char)large[2*k+1]*256+128)/32);
        }
        free(large); free(result);
    }
    return 0;
}

static int MixerAndMac(void)
{
    TWideIq *input=malloc(16400*sizeof(TWideIq));
    short *r=malloc(16400*sizeof(short)), *q=malloc(16400*sizeof(short));
    char *c=malloc(16400),*s=malloc(16400),*code=malloc(16400);
    unsigned n,k,split;
    CHECK(input && r && q && c && s && code);
    for(k=0;k<8200;k++) {
        input[k].i=(short)(((int)(k%256)-128)*8);
        input[k].q=(short)((127-(int)((k*19)%256))*8);
        c[k]=k&1?-1:0; s[k]=k&2?-1:0; code[k]=k%3?-1:0;
    }
    for(n=1;n<=8184;n=n<32?n+1:n+137) for(split=0;split<=n;split+=n/7+1) {
        double expectedR=0,expectedQ=0;
        r[n]=q[n]=12345;
        CarrierIqWide(input,split,input+split,n-split,c,s,n,r,q);
        CHECK(r[n]==12345 && q[n]==12345);
        for(k=0;k<n;k++) {
            float ci=c[k]?-1:1,si=s[k]?-1:1;
            CHECK(r[k]==input[k].i*ci-input[k].q*si);
            CHECK(q[k]==input[k].i*si+input[k].q*ci);
            expectedR+=(code[k]?-1:1)*r[k]; expectedQ+=(code[k]?-1:1)*q[k];
        }
        CHECK(CorrelateWide(r,code,n)==expectedR/256);
        CHECK(CorrelateWide(q,code,n)==expectedQ/256);
        {
            float epl[6];
            CorrelateWideEpl(r,q,c,code,s,n,epl);
            CHECK(epl[0]==CorrelateWide(r,c,n)); CHECK(epl[1]==CorrelateWide(q,c,n));
            CHECK(epl[2]==CorrelateWide(r,code,n)); CHECK(epl[3]==CorrelateWide(q,code,n));
            CHECK(epl[4]==CorrelateWide(r,s,n)); CHECK(epl[5]==CorrelateWide(q,s,n));
        }
    }
    /* The full two-ms workspace, with all IQ/oscillator rail combinations,
     * must not saturate any signed16 mixer or signed32 correlation lane. */
    for(split=0;split<16;split++) {
        int iv=split&1?-1792:1792,qv=split&2?-1792:1792;
        int cv=split&4?-1:1,sv=split&8?-1:1;
        n=16384;
        for(k=0;k<n;k++) {
            input[k].i=(short)iv; input[k].q=(short)qv;
            c[k]=cv<0?-1:0; s[k]=sv<0?-1:0; code[k]=0;
        }
        CarrierIqWide(input,73,input+73,n-73,c,s,n,r,q);
        CHECK(CorrelateWide(r,code,n)==(iv*cv-qv*sv)*(double)n/256);
        CHECK(CorrelateWide(q,code,n)==(iv*sv+qv*cv)*(double)n/256);
        {
            float epl[6];
            CorrelateWideEpl(r,q,c,code,s,n,epl);
            CHECK(epl[0]==CorrelateWide(r,c,n) && epl[1]==CorrelateWide(q,c,n));
            CHECK(epl[2]==CorrelateWide(r,code,n) && epl[3]==CorrelateWide(q,code,n));
            CHECK(epl[4]==CorrelateWide(r,s,n) && epl[5]==CorrelateWide(q,s,n));
        }
    }
    free(input); free(r); free(q); free(c); free(s); free(code);
    return 0;
}

static int CompressionAndFft(void)
{
    char a[9000],b[9000],boc[9000],bc[9000],bs[9000];
    short r[9000],q[9000]; int32_t compressed[8192];
    short legacy[8192],packed[8192],spectrum[8192];
    unsigned n,m,k,mode;
    int gain;
    TNGeneConfiguration cfg={0};
    TFftTable fft={0};
    const unsigned inputs[]={8183,8184,8185};
    for(mode=0;mode<2;mode++) for(n=0;n<3;n++) for(m=1024;m<=4096;m*=4) {
        for(k=0;k<inputs[n];k++) {
            a[k]=(char)((int)(k%15)-7); b[k]=(char)(7-(int)((k*3)%15));
            boc[k]=k&1?-1:0; r[k]=a[k]*256; q[k]=b[k]*256;
            bc[k]=mode && boc[k]?-a[k]:a[k]; bs[k]=mode && boc[k]?-b[k]:b[k];
        }
        SampleCompression(0,0,bc,bs,inputs[n],legacy,m);
        CompressWide(r,q,mode?boc:NULL,inputs[n],compressed,m);
        for(k=0;k<2*m;k++) CHECK(compressed[k]==legacy[k]);
    }
    CHECK(InitFftTable(&cfg,&fft)==SUCCESS_RETURN);
    for(mode=0;mode<4;mode++) {
        /* Tiny amplitudes, normal input and peaks beyond int16 all retain the
         * same physical FFT amplitude after exponent compensation. */
        double amplitude=mode==0?1:mode==1?16:mode==2?1000:100000;
        for(k=0;k<4096;k++) {
            double phase=2*M_PI*31*k/4096;
            compressed[2*k]=(int32_t)lround(amplitude*cos(phase));
            compressed[2*k+1]=(int32_t)lround(amplitude*sin(phase));
        }
        CHECK(PackWideFft(compressed,packed,4096,12,-4,&gain));
        CHECK(gain>=-12 && gain<=0);
        CHECK(PerformFft(&cfg,&fft,packed,spectrum,12,gain)==SUCCESS_RETURN);
        if(mode<=1) CHECK(fabs(spectrum[62]-amplitude*256)<100);
        for(k=0;k<8192;k++) CHECK(abs((int)packed[k])<=32767);
    }
    CHECK(!PackWideFft(compressed,packed,4096,12,1,&gain));
    compressed[0]=INT32_MAX;
    CHECK(!PackWideFft(compressed,packed,4096,12,-4,&gain));
    DeinitFftTable(&fft);
    return 0;
}

static int Doppler(void)
{
    TNGeneConfiguration cfg={0}; TSinCosTable table={0};
    TWideIq input[8184]; short r[8184],q[8184]; char c[8184],s[8184],code[8184]={0};
    double powers[2]; int sign,trial; unsigned k;
    cfg.fs=8183800; cfg.ts=1/cfg.fs;
    CHECK(InitCarrierTable(&cfg,&table,0)==SUCCESS_RETURN);
    for(sign=-1;sign<=1;sign+=2) {
        for(k=0;k<8184;k++) {
            double phase=0.17+2*M_PI*sign*2000*k/cfg.fs;
            input[k].i=(short)lround(512*cos(phase)); input[k].q=(short)lround(512*sin(phase));
        }
        for(trial=0;trial<2;trial++) {
            double fd=(trial?1:-1)*sign*2000;
            GenerateCarrier(&cfg,&table,fd,0,8184,s,c);
            CarrierIqWide(input,73,input+73,8111,c,s,8184,r,q);
            double re=CorrelateWide(r,code,8184),im=CorrelateWide(q,code,8184);
            powers[trial]=re*re+im*im;
        }
        CHECK(powers[0]>100*powers[1]);
    }
    DeinitCarrierTable(&table);
    return 0;
}

static int HeadroomAndProduct(void)
{
    TNGeneConfiguration cfg={0}; TFftTable fft={0};
    int32_t input[8192]; short packed[8192],spectrum[8192],reference[8192],code[16384];
    unsigned k,n,restore,clipped=0;
    int requested=-4,effective;
    CHECK(InitFftTable(&cfg,&fft)==SUCCESS_RETURN);
    for(k=0;k<4096;k++) {
        double phase=2*M_PI*31*k/4096;
        input[2*k]=(int32_t)lround(1000*cos(phase)); input[2*k+1]=(int32_t)lround(1000*sin(phase));
        code[4*k]=code[4*k+3]=1024; code[4*k+1]=code[4*k+2]=0;
    }
    for(;;requested--) {
        CHECK(PackWideFft(input,packed,4096,12,requested,&effective));
        CHECK(PerformFftChecked(&cfg,&fft,packed,spectrum,12,effective,&clipped)==SUCCESS_RETURN);
        CHECK(PerformFft(&cfg,&fft,packed,reference,12,effective)==SUCCESS_RETURN);
        CHECK(memcmp(spectrum,reference,sizeof(spectrum))==0);
        if(requested==-4) CHECK(clipped>0);
        if(!clipped) break;
        CHECK(requested>-12);
    }
    CHECK(requested<-4);
    CHECK(ProductWideFft(spectrum,code,spectrum,4096,-4-requested));
    CHECK(abs(spectrum[62]-4000)<30 && abs(spectrum[63])<30);
    DeinitFftTable(&fft);
    /* Scalar int64 oracle, arbitrary tails, signed floor rounding, aliases and
     * PMADDWD's exceptional overflow endpoints. */
    for(restore=0;restore<=8;restore++) for(n=1;n<=128;n++) {
        int expectedOk=1,ok;
        for(k=0;k<2*n;k++) packed[k]=(short)(k*23117u+n*7919u);
        for(k=0;k<4*n;k++) code[k]=(short)(k*31991u+n*11717u);
        packed[0]=packed[1]=code[0]=code[1]=-32768;
        for(k=0;k<2*n;k++) {
            unsigned bin=k/2,part=k%2;
            int64_t value=((int64_t)packed[2*bin]*code[4*bin+2*part]+
                           (int64_t)packed[2*bin+1]*code[4*bin+2*part+1])>>(16-restore);
            if(value < -32768 || value > 32767) expectedOk=0;
            reference[k]=(short)(value < -32768?-32768:value > 32767?32767:value);
        }
        ok=ProductWideFft(packed,code,spectrum,n,restore);
        CHECK(ok==expectedOk && memcmp(spectrum,reference,2*n*sizeof(short))==0);
        ok=ProductWideFft(packed,code,packed,n,restore);
        CHECK(ok==expectedOk && memcmp(packed,reference,2*n*sizeof(short))==0);
    }
    return 0;
}

int main(void)
{
    int result;
#ifdef NGENE_NATIVE_WINDOWS
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    CHECK(posix_memalign((void **)&ngeneXmmsBackup,16,128)==0);
    result=Adapter() || MixerAndMac() || CompressionAndFft() || Doppler() || HeadroomAndProduct();
    free(ngeneXmmsBackup);
    if(result) return 1;
    puts("CI8/Q8 precision, scalar/SIMD statistics/mixer/MAC, compression, FFT headroom recovery and signed zero-IF Doppler passed.");
    return 0;
}
