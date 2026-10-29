/* Atmospheric model boundaries and units, outside the receiver executable. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "ngeneDataStructure.h"
#include "pvtUtility.h"
#include "nequick.h"
#include "ionoCalcModip.h"
#include "nequickComputation.h"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"Atmosphere failure line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static double capturedAz,capturedHeight,capturedLat,capturedUT;
static TNeQuickData model;
int SwrxLog(int severity,const char *format,...) {return 0;}
TBoolean IonoCalcModip(TModip *m,double lat,double lon,double *out) {*out=40;return FALSE;}
TReturnValue NeQuickComputation(TNeQuickInputData *in,TCurrentCcir *c,double *stec)
{
    capturedAz=in->pdCoeff[0];capturedHeight=in->pdGssPosLLH[2];
    capturedLat=in->pdGssPosLLH[0];capturedUT=in->dUT;
    *stec=1e17; /* 10 TECU, electrons/m^2 */
    return E_OK;
}
int main(void)
{
    TGnssIono gps={0};
    double rx[3]={45,7,311.9349},sat[3]={30,15,20200000};
    double coeffs[3]={0,0,0},delay,zenith,low,high;
    model.stModipValid=model.stCcirValid=TRUE;
    delay=GalileoIonosphericCorrection(&model,coeffs,10,12.25,rx,sat);
    CHECK(capturedAz==63.7);
    CHECK(fabs(delay-1.6237244751199473)<1e-12); /* 40.3 * 1e17 / 1575420000^2 */
    CHECK(fabs(capturedLat-M_PI/4)<1e-15 && capturedHeight==311.9349 && capturedUT==12.25);
    coeffs[0]=100;coeffs[1]=2;coeffs[2]=.1;
    GalileoIonosphericCorrection(&model,coeffs,10,12.25,rx,sat);CHECK(fabs(capturedAz-340)<1e-12);
    coeffs[0]=500;coeffs[1]=coeffs[2]=0;
    GalileoIonosphericCorrection(&model,coeffs,10,12.25,rx,sat);CHECK(capturedAz==400);
    coeffs[0]=-1;
    GalileoIonosphericCorrection(&model,coeffs,10,12.25,rx,sat);CHECK(capturedAz==0);
    /* GPS ICD night-time floor: zero alpha, zenith, midnight, zero lat/lon. */
    delay=GpsIonosphericCorrection(&gps,M_PI/2,0,0,0,0);
    CHECK(fabs(delay-5.00216e-9)<1e-20);
    CHECK(fabs(GpsIonosphericCorrection(&gps,.5,.2,.7,-.4,1000)-
               GpsIonosphericCorrection(&gps,.5,.2,.7,-.4,605800))<1e-20);
    /* The historical API returns a negative code correction, in metres.
     * Its consumers negate it to obtain the positive propagation delay. */
    zenith=-TropoHopfieldModel(M_PI/2,0);
    low=-TropoHopfieldModel(10*M_PI/180,0);
    high=-TropoHopfieldModel(M_PI/2,3000);
    CHECK(zenith>2 && zenith<3 && low>3*zenith && low<10*zenith);
    CHECK(high>0 && high<.8*zenith);
    puts("Atmosphere: NeQuick Az/default/units, Klobuchar day rollover, tropo signs/height/elevation PASS");
    return 0;
}
