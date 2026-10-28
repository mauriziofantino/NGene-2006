#include <math.h>
#include <stdio.h>
#include "smallLeastSquares.h"
#include "smallSpd.h"
#include "gnssTimeMath.h"

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr,"PVT QR check failed at line %d\n",__LINE__); return 1; } } while (0)
int main(void)
{
    double a[100], b[20], x[5], q[25], truth[5]={120,-32,5,900,-400};
    int n, m, i, j, k, l;
    for(m=4;m<=5;m++) for(n=m;n<=20;n++) {
        for(i=0;i<n;i++) {
            double az=i*2.399963229728653, el=.1+.8*(i%5)/4.;
            a[i*m]=cos(el)*cos(az); a[i*m+1]=cos(el)*sin(az); a[i*m+2]=sin(el);
            a[i*m+3]=(m==4 || i%2==0)?1:0;
            if(m==5) a[i*m+4]=i%2?1:0;
            b[i]=0;
            for(j=0;j<m;j++) b[i]+=a[i*m+j]*truth[j];
        }
        REQUIRE(SmallLeastSquares(a,b,n,m,x,q));
        for(j=0;j<m;j++) REQUIRE(fabs(x[j]-truth[j])<1e-8);
        /* Independent covariance identity: (A' A) Q = I. */
        for(j=0;j<m;j++) for(k=0;k<m;k++) {
            double value=0;
            for(l=0;l<m;l++) for(i=0;i<n;i++) value+=a[i*m+j]*a[i*m+l]*q[l*m+k];
            REQUIRE(fabs(value-(j==k))<1e-8);
        }
        for(i=0;i<n;i++) b[i]+=.25*sin(i*1.7);
        REQUIRE(SmallLeastSquares(a,b,n,m,x,NULL));
        for(j=0;j<m;j++) {
            double dot=0;
            for(i=0;i<n;i++) {
                double residual=-b[i];
                for(k=0;k<m;k++) residual+=a[i*m+k]*x[k];
                dot+=a[i*m+j]*residual;
            }
            REQUIRE(fabs(dot)<1e-8);
        }
    }
    /* Large condition number without squaring it through normal equations. */
    for(i=0;i<16;i++) a[i]=0;
    for(i=0;i<4;i++) { a[4*i+i]=pow(1e-3,i); b[i]=a[4*i+i]*truth[i]; }
    REQUIRE(SmallLeastSquares(a,b,4,4,x,q));
    for(i=0;i<4;i++) REQUIRE(fabs(x[i]-truth[i])<1e-8);
    a[15]=0;
    REQUIRE(!SmallLeastSquares(a,b,4,4,x,q));
    REQUIRE(!SmallLeastSquares(a,b,3,4,x,q));
    a[15]=NAN;
    REQUIRE(!SmallLeastSquares(a,b,4,4,x,q));
    a[15]=1; b[0]=INFINITY;
    REQUIRE(!SmallLeastSquares(a,b,4,4,x,q));
    {
        double spd[9]={4,1,2,1,5,.5,2,.5,6},lower[9],rhs[3],sol[3];
        for(i=0;i<3;i++){rhs[i]=0;for(j=0;j<3;j++)rhs[i]+=spd[3*i+j]*truth[j];}
        REQUIRE(SpdFactor(spd,3,lower));REQUIRE(SpdSolve(lower,3,rhs,sol));
        for(i=0;i<3;i++)REQUIRE(fabs(sol[i]-truth[i])<1e-10);
        spd[8]=-1;REQUIRE(!SpdFactor(spd,3,lower));
        spd[8]=NAN;REQUIRE(!SpdFactor(spd,3,lower));
    }
    {
        int week=64;double tow=604800.;
        REQUIRE(NormalizeGnssTime(&week,&tow) && week==65 && tow==0);
        tow=-.25;REQUIRE(NormalizeGnssTime(&week,&tow) && week==64 && tow==604799.75);
        REQUIRE(fabs(GnssTimeDifference(65,.01,64,604799.99)-.02)<1e-10);
        REQUIRE(UnrollGnssWeek(63,64,64)==63 && UnrollGnssWeek(0,63,64)==64);
    }
    puts("PVT QR: single/mixed clocks, residual orthogonality, covariance, ill-conditioning and invalid inputs passed");
    return 0;
}
