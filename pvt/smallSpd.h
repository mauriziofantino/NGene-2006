#ifndef NGENE_SMALL_SPD_H
#define NGENE_SMALL_SPD_H
#include <float.h>
#include <math.h>

/* Cholesky without regularization: caller owns the row-major scratch storage.
 * Reject invalid covariance rather than silently modifying the noise model. */
static inline int SpdFactor(const double *a,int n,double *lower)
{
    int i,j,k;double scale=0;
    if(n<1) return 0;
    for(i=0;i<n;i++) {
        if(!isfinite(a[i*n+i])) return 0;
        if(a[i*n+i]>scale) scale=a[i*n+i];
    }
    for(i=0;i<n;i++) for(j=0;j<=i;j++) {
        double value=.5*(a[i*n+j]+a[j*n+i]);
        if(!isfinite(value) || fabs(a[i*n+j]-a[j*n+i])>128*DBL_EPSILON*n*scale) return 0;
        for(k=0;k<j;k++) value-=lower[i*n+k]*lower[j*n+k];
        if(i==j) {
            if(!(value>64*DBL_EPSILON*n*scale)) return 0;
            lower[i*n+j]=sqrt(value);
        } else lower[i*n+j]=value/lower[j*n+j];
    }
    return 1;
}
static inline int SpdSolve(const double *lower,int n,const double *b,double *x)
{
    int i,j;
    for(i=0;i<n;i++) {
        double value=b[i];
        for(j=0;j<i;j++) value-=lower[i*n+j]*x[j];
        x[i]=value/lower[i*n+i];
        if(!isfinite(x[i])) return 0;
    }
    for(i=n-1;i>=0;i--) {
        double value=x[i];
        for(j=i+1;j<n;j++) value-=lower[j*n+i]*x[j];
        x[i]=value/lower[i*n+i];
        if(!isfinite(x[i])) return 0;
    }
    return 1;
}
#endif
