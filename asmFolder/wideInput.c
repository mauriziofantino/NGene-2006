#include <emmintrin.h>
#include <math.h>
#include <stdint.h>
#include "wideInput.h"

static __m128i SignedProduct(__m128i v,__m128i mask)
{ return _mm_sub_epi16(_mm_xor_si128(v,mask),mask); }

static __m128i Masks(const char *p)
{
    __m128i m=_mm_loadl_epi64((const __m128i *)p);
    return _mm_unpacklo_epi8(m,_mm_cmpgt_epi8(_mm_setzero_si128(),m));
}

int32_t CorrelateWideInteger(const short *signal,const char *code,unsigned n)
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
    return total;
}

float CorrelateWide(const short *signal,const char *code,unsigned n)
{ return CorrelateWideInteger(signal,code,n)/(float)HACKRF_MIXER_SCALE; }

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
            out[j]=(lanes[0]+lanes[1]+lanes[2]+lanes[3]+tail[j])/(float)HACKRF_MIXER_SCALE;
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
        /* Q6 mixer representation -> fixed-point FFT representation (x256). */
        out[2*k]=r*(256/HACKRF_MIXER_SCALE); out[2*k+1]=i*(256/HACKRF_MIXER_SCALE);
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
    /* Compression already retains mixer fractions. Keep Classic's stage schedule
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
    unsigned bits=16-restoreShift;
    __m128i remainderMask,halfMinusOne;
    if(restoreShift>16) return 0;
    remainderMask=_mm_set1_epi32(bits?(1u<<bits)-1:0);
    halfMinusOne=_mm_set1_epi32(bits?(1u<<(bits-1))-1:0);
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
        if(bits) {
            /* Round the widened product symmetrically. Flooring every noisy
             * bin adds a coherent negative pedestal to the inverse FFT. Use
             * remainder/carry arithmetic to avoid int32 bias-add overflow. */
            __m128i sx=_mm_srli_epi32(x,31),sy=_mm_srli_epi32(y,31);
            __m128i cx=_mm_cmpgt_epi32(_mm_and_si128(x,remainderMask),_mm_add_epi32(halfMinusOne,sx));
            __m128i cy=_mm_cmpgt_epi32(_mm_and_si128(y,remainderMask),_mm_add_epi32(halfMinusOne,sy));
            x=_mm_sub_epi32(_mm_sra_epi32(x,shift),cx);
            y=_mm_sub_epi32(_mm_sra_epi32(y,shift),cy);
        }
        if(_mm_movemask_epi8(_mm_or_si128(_mm_or_si128(_mm_cmpgt_epi32(lo,x),_mm_cmpgt_epi32(x,hi)),
                                       _mm_or_si128(_mm_cmpgt_epi32(lo,y),_mm_cmpgt_epi32(y,hi))))) ok=0;
        _mm_storeu_si128((__m128i *)(out+2*k),_mm_packs_epi32(x,y));
    }
    for(;k<count;k++) {
        int64_t r=(int64_t)signal[2*k]*code[4*k]+(int64_t)signal[2*k+1]*code[4*k+1];
        int64_t i=(int64_t)signal[2*k]*code[4*k+2]+(int64_t)signal[2*k+1]*code[4*k+3];
        if(bits) { r=(r+(INT64_C(1)<<(bits-1))-(r<0))>>bits; i=(i+(INT64_C(1)<<(bits-1))-(i<0))>>bits; }
        if(r < -32768 || r > 32767 || i < -32768 || i > 32767) ok=0;
        out[2*k]=(short)(r < -32768 ? -32768 : r > 32767 ? 32767 : r);
        out[2*k+1]=(short)(i < -32768 ? -32768 : i > 32767 ? 32767 : i);
    }
    return ok;
}

int AccumulateWideFftPower(const short *input,uint64_t *sum,unsigned n,
    unsigned restoreShift,unsigned nonCoherentShift,int reset)
{
    unsigned k=0;
    __m128i left=_mm_cvtsi32_si128(2*(int)restoreShift);
    __m128i right=_mm_cvtsi32_si128((int)nonCoherentShift),zero=_mm_setzero_si128();
    if(restoreShift>16 || nonCoherentShift>31) return 0;
    for(;k+4<=n;k+=4) {
        __m128i v=_mm_loadu_si128((const __m128i *)(input+2*k));
        /* Treat PMADDWD endpoints as uint32: two (-32768)^2 are 2^31. */
        __m128i p=_mm_madd_epi16(v,v);
        __m128i a=_mm_srl_epi64(_mm_sll_epi64(_mm_unpacklo_epi32(p,zero),left),right);
        __m128i b=_mm_srl_epi64(_mm_sll_epi64(_mm_unpackhi_epi32(p,zero),left),right);
        if(!reset) {
            a=_mm_add_epi64(a,_mm_loadu_si128((const __m128i *)(sum+k)));
            b=_mm_add_epi64(b,_mm_loadu_si128((const __m128i *)(sum+k+2)));
        }
        _mm_storeu_si128((__m128i *)(sum+k),a);
        _mm_storeu_si128((__m128i *)(sum+k+2),b);
    }
    for(;k<n;k++) {
        int64_t i=input[2*k],q=input[2*k+1];
        uint64_t p=((uint64_t)(i*i+q*q)<<(2*restoreShift))>>nonCoherentShift;
        sum[k]=p+(reset?0:sum[k]);
    }
    return 1;
}
