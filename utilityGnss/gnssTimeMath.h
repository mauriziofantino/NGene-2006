#ifndef NGENE_GNSS_TIME_MATH_H
#define NGENE_GNSS_TIME_MATH_H
#include <math.h>
#include <limits.h>
/* Keep sub-second precision by subtracting TOW before combining weeks. */

static inline double GnssTimeDifference(int w1, double t1, int w0, double t0)
{
    return ((double)w1 - (double)w0) * 604800.0 + (t1 - t0);
}

static inline int NormalizeGnssTime(int *week, double *tow)
{
    double carry;
    if (!isfinite(*tow))
        return 0;
    carry = floor(*tow / 604800.0);
    if (carry + (double)*week > INT_MAX || carry + (double)*week < INT_MIN)
        return 0;
    *week = (int)((double)*week + carry);
    *tow -= carry * 604800.0;
    return 1;
}

static inline int UnrollGnssWeek(int truncated, int reference, int modulus)
{
    int delta = (truncated % modulus - reference % modulus + modulus) % modulus;
    if (delta > modulus / 2)
        delta -= modulus;
    return reference + delta;
}
#endif
