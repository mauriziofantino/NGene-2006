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
double PntSpatialProjection(const double *covariance, int stride, const double *direction);
#ifdef __cplusplus
}
#endif
#endif
