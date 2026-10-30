#ifndef NGENE_PNT_NUMERICS_H
#define NGENE_PNT_NUMERICS_H
#ifdef __cplusplus
extern "C" {
#endif
/* C boundary only: all decompositions and matrix arithmetic use Eigen, double.
 * Row-major storage; success=1, failure=0. Failed solves leave outputs intact.
 * Covariance is unit-variance geometry, not an accuracy/integrity guarantee. */

int PntLeastSquares(const double *a, const double *b, int rows, int cols, double *solution,
                    double *covariance);

int PntSpdFactor(const double *a, int n, double *lower);

int PntSpdSolve(const double *lower, int n, const double *b, double *x);

int PntKalmanStep(int n, int m, const double *phi, const double *q, const double *h,
                  const double *r, const double *z, double *state, double *covariance,
                  double *predicted);

/* Redundant Doppler-only fit; the common clock drift is a nuisance parameter.
 * h selects state columns 3..5 and 7. r is the rate-noise block, row-major
 * with rStride columns. covariance is the 3x3 velocity marginal; scatter is
 * the reduced weighted residual sum of squares. At least 6 rows are required. */
int PntDopplerFit(int rows, int stateCols, const double *h, const double *r, int rStride,
                  const double *z, double *velocityCorrection, double *covariance, double *scatter);

double PntSpatialProjection(const double *covariance, int stride, const double *direction);
#ifdef __cplusplus
}
#endif
#endif
