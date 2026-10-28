#ifndef NGENE_SMALL_LEAST_SQUARES_H
#define NGENE_SMALL_LEAST_SQUARES_H
#include <float.h>
#include <math.h>

/* Streaming Givens QR for the four/five GNSS states. Row-major inputs remain
 * unchanged. No allocation or normal equations. Optional covariance is
 * (A' A)^-1 for unit measurement variance, not a measurement error estimate.
 * Returns zero for invalid inputs or numerically deficient column rank. */
static inline int SmallLeastSquares(const double *a, const double *b,
    int rows, int cols, double *solution, double *covariance)
{
    double r[25] = {0}, z[5] = {0}, inv[25] = {0}, scale = 0;
    int i, j, k;
    if (!a || rows < cols || cols < 1 || cols > 5 ||
        (solution && !b)) return 0;
    for (i = 0; i < rows; ++i) {
        double v[5], y = b ? b[i] : 0;
        if (!isfinite(y)) return 0;
        for (j = 0; j < cols; ++j) {
            v[j] = a[i*cols+j];
            if (!isfinite(v[j])) return 0;
        }
        for (j = 0; j < cols; ++j) {
            double norm = hypot(r[j*cols+j], v[j]), c, s, t;
            if (!isfinite(norm)) return 0;
            if (norm == 0) continue;
            c = r[j*cols+j]/norm; s = v[j]/norm;
            r[j*cols+j] = norm;
            for (k = j+1; k < cols; ++k) {
                t = c*r[j*cols+k] + s*v[k];
                v[k] = -s*r[j*cols+k] + c*v[k];
                r[j*cols+k] = t;
            }
            t = c*z[j] + s*y;
            y = -s*z[j] + c*y; z[j] = t;
        }
    }
    for (j = 0; j < cols; ++j)
        if (r[j*cols+j] > scale) scale = r[j*cols+j];
    for (j = 0; j < cols; ++j)
        if (!(r[j*cols+j] > 64*DBL_EPSILON*rows*scale)) return 0;
    if (solution) for (j = cols-1; j >= 0; --j) {
        double t = z[j];
        for (k = j+1; k < cols; ++k) t -= r[j*cols+k]*solution[k];
        solution[j] = t/r[j*cols+j];
        if (!isfinite(solution[j])) return 0;
    }
    if (covariance) {
        for (i = 0; i < cols; ++i) for (j = cols-1; j >= 0; --j) {
            double t = i == j ? 1 : 0;
            for (k = j+1; k < cols; ++k) t -= r[j*cols+k]*inv[k*cols+i];
            inv[j*cols+i] = t/r[j*cols+j];
        }
        for (i = 0; i < cols; ++i) for (j = 0; j < cols; ++j) {
            double t = 0;
            for (k = 0; k < cols; ++k) t += inv[i*cols+k]*inv[j*cols+k];
            if (!isfinite(t)) return 0;
            covariance[i*cols+j] = t;
        }
    }
    return 1;
}
#endif
