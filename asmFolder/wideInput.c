#include <emmintrin.h>
#include <math.h>
#include <stdint.h>
#include "wideInput.h"

/* Sum signed CI8 components and energy eight IQ pairs at a time. Bounded
 * partial vectors are reduced to int64 before their int32 lanes can overflow. */
static void Statistics(const unsigned char *raw, unsigned pairs, int64_t *si, int64_t *sq, int64_t *ss)
{
    unsigned k=0;
    const __m128i zero=_mm_setzero_si128(), im=_mm_set1_epi32(1), qm=_mm_set1_epi32(65536);
    *si=*sq=*ss=0;
    while(k+8<=pairs) {
        unsigned end=k+16384, j;
        int lanes[4];
        __m128i ai=zero,aq=zero,as=zero;
        if(end>pairs) end=pairs;
        for(;k+8<=end;k+=8) {
            __m128i v=_mm_loadu_si128((const __m128i *)(raw+2*k));
            __m128i sign=_mm_cmpgt_epi8(zero,v);
            __m128i a=_mm_unpacklo_epi8(v,sign),b=_mm_unpackhi_epi8(v,sign);
            ai=_mm_add_epi32(ai,_mm_add_epi32(_mm_madd_epi16(a,im),_mm_madd_epi16(b,im)));
            aq=_mm_add_epi32(aq,_mm_add_epi32(_mm_madd_epi16(a,qm),_mm_madd_epi16(b,qm)));
            as=_mm_add_epi32(as,_mm_add_epi32(_mm_madd_epi16(a,a),_mm_madd_epi16(b,b)));
        }
        _mm_storeu_si128((__m128i *)lanes,ai); for(j=0;j<4;j++) *si+=lanes[j];
        _mm_storeu_si128((__m128i *)lanes,aq); for(j=0;j<4;j++) *sq+=lanes[j];
        _mm_storeu_si128((__m128i *)lanes,as); for(j=0;j<4;j++) *ss+=lanes[j];
    }
    for(;k<pairs;k++) { int i=(signed char)raw[2*k],q=(signed char)raw[2*k+1]; *si+=i; *sq+=q; *ss+=i*i+q*q; }
}

static __m128i Center(__m128i x, __m128i dc, __m128i left, __m128i right, int rounding)
{
    x=_mm_sll_epi32(_mm_sub_epi32(_mm_slli_epi32(x,8),dc),left);
    if(rounding) x=_mm_add_epi32(x,_mm_add_epi32(_mm_set1_epi32(rounding),_mm_srai_epi32(x,31)));
    return _mm_sra_epi32(x,right);
}

void AdaptIqWide(const unsigned char *raw, TWideIq *out, unsigned pairs)
{
    int64_t si,sq,ss;
    unsigned k,bytes=2*pairs;
    int shift=0,di,dq,lb,rb,rounding;
    double variance,rms;
    __m128i dc,left,right;
    const __m128i zero=_mm_setzero_si128(),lo=_mm_set1_epi16(-1792),hi=_mm_set1_epi16(1792);
    if(!pairs) return;
    Statistics(raw,pairs,&si,&sq,&ss);
    di=(int)lround((double)si*256/pairs); dq=(int)lround((double)sq*256/pairs);
    variance=((double)ss-((double)si*si+(double)sq*sq)/pairs)/(2.0*pairs);
    rms=sqrt(variance>0?variance:0);
    if(rms>0) { while(rms<1.5 && shift<8) { rms*=2; shift++; }
        while(rms>3 && shift>-7) { rms*=0.5; shift--; } }
    lb=shift>0?shift:0; rb=shift<0?-shift:0; rounding=rb?1<<(rb-1):0;
    dc=_mm_set_epi32(dq,di,dq,di); left=_mm_cvtsi32_si128(lb); right=_mm_cvtsi32_si128(rb);
    for(k=0;k+16<=bytes;k+=16) {
        __m128i v=_mm_loadu_si128((const __m128i *)(raw+k));
        __m128i sign=_mm_cmpgt_epi8(zero,v);
        __m128i a=_mm_unpacklo_epi8(v,sign),b=_mm_unpackhi_epi8(v,sign);
        __m128i sa=_mm_srai_epi16(a,15),sb=_mm_srai_epi16(b,15);
        __m128i al=Center(_mm_unpacklo_epi16(a,sa),dc,left,right,rounding);
        __m128i ah=Center(_mm_unpackhi_epi16(a,sa),dc,left,right,rounding);
        __m128i bl=Center(_mm_unpacklo_epi16(b,sb),dc,left,right,rounding);
        __m128i bh=Center(_mm_unpackhi_epi16(b,sb),dc,left,right,rounding);
        _mm_storeu_si128((__m128i *)((short *)out+k),_mm_min_epi16(hi,_mm_max_epi16(lo,_mm_packs_epi32(al,ah))));
        _mm_storeu_si128((__m128i *)((short *)out+k+8),_mm_min_epi16(hi,_mm_max_epi16(lo,_mm_packs_epi32(bl,bh))));
    }
    for(;k<bytes;k++) {
        int x=((signed char)raw[k]*256-((k&1)?dq:di))*(1<<lb);
        if(rb) x=(x+rounding-(x<0))>>rb;
        ((short *)out)[k]=(short)(x < -1792?-1792:x>1792?1792:x);
    }
}

static __m128i SignedProduct(__m128i v,__m128i mask)
{ return _mm_sub_epi16(_mm_xor_si128(v,mask),mask); }

static __m128i Masks(const char *p)
{
    __m128i m=_mm_loadl_epi64((const __m128i *)p);
    return _mm_unpacklo_epi8(m,_mm_cmpgt_epi8(_mm_setzero_si128(),m));
}

static void Rotate(const TWideIq *input,const char *c,const char *s,unsigned n,short *real,short *imaginary)
{
    unsigned k;
    for(k=0;k+8<=n;k+=8) {
        __m128i a=_mm_loadu_si128((const __m128i *)(input+k)),b=_mm_loadu_si128((const __m128i *)(input+k+4));
        __m128i i=_mm_packs_epi32(_mm_srai_epi32(_mm_slli_epi32(a,16),16),_mm_srai_epi32(_mm_slli_epi32(b,16),16));
        __m128i q=_mm_packs_epi32(_mm_srai_epi32(a,16),_mm_srai_epi32(b,16));
        __m128i cm=Masks(c+k),sm=Masks(s+k);
        _mm_storeu_si128((__m128i *)(real+k),_mm_sub_epi16(SignedProduct(i,cm),SignedProduct(q,sm)));
        _mm_storeu_si128((__m128i *)(imaginary+k),_mm_add_epi16(SignedProduct(i,sm),SignedProduct(q,cm)));
    }
    for(;k<n;k++) { int ci=c[k]?-1:1,si=s[k]?-1:1;
        real[k]=(short)(input[k].i*ci-input[k].q*si); imaginary[k]=(short)(input[k].i*si+input[k].q*ci); }
}

void CarrierIqWide(const TWideIq *a,unsigned na,const TWideIq *b,unsigned nb,
    const char *c,const char *s,unsigned n,short *real,short *imaginary)
{
    unsigned first=na<n?na:n;
    if(na+nb<n) return;
    Rotate(a,c,s,first,real,imaginary);
    if(first<n) Rotate(b,c+first,s+first,n-first,real+first,imaginary+first);
}

float CorrelateWide(const short *signal,const char *code,unsigned n)
{
    unsigned k; int lanes[4],total;
    __m128i sum=_mm_setzero_si128(),ones=_mm_set1_epi16(1);
    for(k=0;k+8<=n;k+=8) {
        __m128i v=_mm_loadu_si128((const __m128i *)(signal+k));
        sum=_mm_add_epi32(sum,_mm_madd_epi16(SignedProduct(v,Masks(code+k)),ones));
    }
    _mm_storeu_si128((__m128i *)lanes,sum);
    total=lanes[0]+lanes[1]+lanes[2]+lanes[3];
    for(;k<n;k++) total+=code[k]?-signal[k]:signal[k];
    return total/256.0f;
}

void CorrelateWideEpl(const short *i,const short *q,const char *early,
    const char *prompt,const char *late,unsigned n,float out[6])
{
    unsigned k,j;
    int lanes[4],tail[6]={0};
    __m128i ei=_mm_setzero_si128(),eq=ei,pi=ei,pq=ei,li=ei,lq=ei;
    const __m128i ones=_mm_set1_epi16(1);
    for(k=0;k+8<=n;k+=8) {
        __m128i vi=_mm_loadu_si128((const __m128i *)(i+k));
        __m128i vq=_mm_loadu_si128((const __m128i *)(q+k));
        __m128i e=Masks(early+k),p=Masks(prompt+k),l=Masks(late+k);
        ei=_mm_add_epi32(ei,_mm_madd_epi16(SignedProduct(vi,e),ones));
        eq=_mm_add_epi32(eq,_mm_madd_epi16(SignedProduct(vq,e),ones));
        pi=_mm_add_epi32(pi,_mm_madd_epi16(SignedProduct(vi,p),ones));
        pq=_mm_add_epi32(pq,_mm_madd_epi16(SignedProduct(vq,p),ones));
        li=_mm_add_epi32(li,_mm_madd_epi16(SignedProduct(vi,l),ones));
        lq=_mm_add_epi32(lq,_mm_madd_epi16(SignedProduct(vq,l),ones));
    }
    for(;k<n;k++) {
        int e=early[k]?-1:1,p=prompt[k]?-1:1,l=late[k]?-1:1;
        tail[0]+=i[k]*e; tail[1]+=q[k]*e; tail[2]+=i[k]*p;
        tail[3]+=q[k]*p; tail[4]+=i[k]*l; tail[5]+=q[k]*l;
    }
    {
        __m128i sums[6]={ei,eq,pi,pq,li,lq};
        for(j=0;j<6;j++) {
            _mm_storeu_si128((__m128i *)lanes,sums[j]);
            out[j]=(lanes[0]+lanes[1]+lanes[2]+lanes[3]+tail[j])/256.0f;
        }
    }
}

void CompressWide(const short *real,const short *imaginary,const char *boc,unsigned inputs,int32_t *out,unsigned outputs)
{
    unsigned rem=0,limit=(unsigned)((double)inputs*65536/outputs);
    unsigned shortCount=inputs/outputs,pos=0,k,j;
    for(k=0;k<outputs;k++) {
        unsigned count=shortCount; int r=0,i=0;
        rem+=shortCount*65536;
        if(rem<limit) { rem+=65536; count++; }
        rem-=limit;
        for(j=0;j<count;j++,pos++) { int sign=boc && boc[pos]?-1:1; r+=real[pos]*sign; i+=imaginary[pos]*sign; }
        /* Q8 input cancels Classic's compression multiplier of 256. */
        out[2*k]=r; out[2*k+1]=i;
    }
}
int PackWideFft(const int32_t *input,short *output,unsigned count,unsigned log2Size,int requestedGain,int *effectiveGain)
{
    unsigned k;
    int shift=0,lb,rb,rounding;
    uint64_t peak=0;
    __m128i left,right,bias;
    if(log2Size<3 || log2Size>14 || requestedGain>0 || requestedGain<-(int)log2Size) return 0;
    for(k=0;k<2*count;k++) {
        uint64_t magnitude=input[k]<0?-(int64_t)input[k]:input[k];
        if(magnitude>peak) peak=magnitude;
    }
    while(peak>32767 && requestedGain-shift<0) { peak=(peak+1)/2; shift--; }
    if(peak>32767) return 0;
    /* Compression already retains Q8 fractions. Keep Classic's stage schedule
     * when the int16 input fits; extra early divisions alter floor-rounding
     * bias even if a matching input left shift preserves the nominal gain. */
    lb=shift>0?shift:0; rb=shift<0?-shift:0; rounding=rb?1<<(rb-1):0;
    left=_mm_cvtsi32_si128(lb); right=_mm_cvtsi32_si128(rb); bias=_mm_set1_epi32(rounding);
    for(k=0;k+8<=2*count;k+=8) {
        __m128i a=_mm_loadu_si128((const __m128i *)(input+k));
        __m128i b=_mm_loadu_si128((const __m128i *)(input+k+4));
        if(rb) {
            /* Raw input must fit the selected FFT exponent; with <=14 stages
             * it is bounded well below int32 rails before adding this bias. */
            a=_mm_sra_epi32(_mm_add_epi32(a,_mm_add_epi32(bias,_mm_srai_epi32(a,31))),right);
            b=_mm_sra_epi32(_mm_add_epi32(b,_mm_add_epi32(bias,_mm_srai_epi32(b,31))),right);
        } else { a=_mm_sll_epi32(a,left); b=_mm_sll_epi32(b,left); }
        _mm_storeu_si128((__m128i *)(output+k),_mm_packs_epi32(a,b));
    }
    for(;k<2*count;k++) {
        int64_t value=input[k];
        value=rb?(value+rounding-(value<0))>>rb:value*(1<<lb);
        output[k]=(short)value;
    }
    *effectiveGain=requestedGain-shift;
    return 1;
}
int ProductWideFft(const short *signal, const short *code, short *out,
                   unsigned count, unsigned restoreShift)
{
    unsigned k;
    int ok=1;
    __m128i shift=_mm_cvtsi32_si128(16-(int)restoreShift);
    const __m128i lo=_mm_set1_epi32(-32768), hi=_mm_set1_epi32(32767);
    if(restoreShift>16) return 0;
    for(k=0;k+4<=count;k+=4) {
        __m128i a=_mm_loadu_si128((const __m128i *)(signal+2*k));
        __m128i cl=_mm_loadu_si128((const __m128i *)(code+4*k));
        __m128i ch=_mm_loadu_si128((const __m128i *)(code+4*k+8));
        __m128i x=_mm_madd_epi16(_mm_shuffle_epi32(a,0x50),cl);
        __m128i y=_mm_madd_epi16(_mm_shuffle_epi32(a,0xfa),ch);
        /* PMADDWD's only int32 overflow is (-32768)^2+(-32768)^2.
         * Use the independent int64 tail for this exceptional quartet. */
        if(_mm_movemask_epi8(_mm_or_si128(_mm_cmpeq_epi32(x,_mm_set1_epi32(INT32_MIN)),
                                       _mm_cmpeq_epi32(y,_mm_set1_epi32(INT32_MIN))))) break;
        x=_mm_sra_epi32(x,shift); y=_mm_sra_epi32(y,shift);
        if(_mm_movemask_epi8(_mm_or_si128(_mm_or_si128(_mm_cmpgt_epi32(lo,x),_mm_cmpgt_epi32(x,hi)),
                                       _mm_or_si128(_mm_cmpgt_epi32(lo,y),_mm_cmpgt_epi32(y,hi))))) ok=0;
        _mm_storeu_si128((__m128i *)(out+2*k),_mm_packs_epi32(x,y));
    }
    for(;k<count;k++) {
        int64_t r=((int64_t)signal[2*k]*code[4*k]+(int64_t)signal[2*k+1]*code[4*k+1])>>(16-restoreShift);
        int64_t i=((int64_t)signal[2*k]*code[4*k+2]+(int64_t)signal[2*k+1]*code[4*k+3])>>(16-restoreShift);
        if(r < -32768 || r > 32767 || i < -32768 || i > 32767) ok=0;
        out[2*k]=(short)(r < -32768 ? -32768 : r > 32767 ? 32767 : r);
        out[2*k+1]=(short)(i < -32768 ? -32768 : i > 32767 ? 32767 : i);
    }
    return ok;
}
