#include "pntNumerics.h"
#include "matrices.h"
#include <Eigen/Dense>
#include <algorithm>
#include <cmath>
#include <limits>
namespace {
using Mat = Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;
using Vec = Eigen::VectorXd;
using View = Eigen::Map<Mat>;
using Read = Eigen::Map<const Mat>;
constexpr double eps = std::numeric_limits<double>::epsilon();
bool factor(const Mat &a, Eigen::LLT<Mat> &llt)
{
    if (!a.allFinite() || a.rows() < 1 || a.rows() != a.cols())
        return false;
    const double scale = a.diagonal().maxCoeff();
    if (!(scale > 0) || (a - a.transpose()).cwiseAbs().maxCoeff() > 128 * eps * a.rows() * scale)
        return false;
    llt.compute((0.5 * (a + a.transpose())).eval());
    return llt.info() == Eigen::Success &&
           (llt.matrixLLT().diagonal().array().square() > 64 * eps * a.rows() * scale).all();
}
bool validProcessNoise(const Mat &a)
{
    if (!a.allFinite())
        return false;
    const double scale = a.cwiseAbs().maxCoeff();
    if ((a - a.transpose()).cwiseAbs().maxCoeff() > 128 * eps * a.rows() * scale)
        return false;
    Eigen::LDLT<Mat> ldlt((0.5 * (a + a.transpose())).eval());
    return ldlt.info() == Eigen::Success && ldlt.isPositive();
}
}
extern "C" int PntLeastSquares(const double *a, const double *b, int rows, int cols,
                               double *solution, double *covariance)
{
    if (!a || rows < cols || cols < 1 || cols > 5 || (solution && !b))
        return 0;
    try {
        Mat design = Read(a, rows, cols);
        if (!design.allFinite())
            return 0;
        if (b && !Eigen::Map<const Vec>(b, rows).allFinite())
            return 0;
        Eigen::ColPivHouseholderQR<Mat> qr(design);
        qr.setThreshold(64 * eps * rows);
        if (qr.rank() != cols)
            return 0;
        Vec x;
        Mat cov;
        if (solution) {
            x = qr.solve(Eigen::Map<const Vec>(b, rows));
            if (!x.allFinite())
                return 0;
        }
        if (covariance) {
            // A P = Q R: covariance = P R^-1 R^-T P^T. No A^T A.
            Mat inverseR = qr.matrixR()
                               .topLeftCorner(cols, cols)
                               .triangularView<Eigen::Upper>()
                               .solve(Mat::Identity(cols, cols));
            cov = qr.colsPermutation() * (inverseR * inverseR.transpose()) *
                  qr.colsPermutation().transpose();
            if (!cov.allFinite())
                return 0;
        }
        if (solution)
            Eigen::Map<Vec>(solution, cols) = x;
        if (covariance)
            View(covariance, cols, cols) = cov;
        return 1;
    } catch (...) {
        return 0;
    }
}
extern "C" int PntSpdFactor(const double *a, int n, double *lower)
{
    if (!a || !lower || n < 1)
        return 0;
    try {
        Eigen::LLT<Mat> llt;
        if (!factor(Read(a, n, n), llt))
            return 0;
        View(lower, n, n) = llt.matrixL();
        return 1;
    } catch (...) {
        return 0;
    }
}
extern "C" int PntSpdSolve(const double *lower, int n, const double *b, double *x)
{
    if (!lower || !b || !x || n < 1)
        return 0;
    try {
        Mat l = Read(lower, n, n).triangularView<Eigen::Lower>();
        if (!l.allFinite() || (l.diagonal().array() <= 0).any() ||
            !Eigen::Map<const Vec>(b, n).allFinite())
            return 0;
        Vec y = l.triangularView<Eigen::Lower>().solve(Eigen::Map<const Vec>(b, n));
        Vec result = l.transpose().triangularView<Eigen::Upper>().solve(y);
        if (!result.allFinite())
            return 0;
        Eigen::Map<Vec>(x, n) = result;
        return 1;
    } catch (...) {
        return 0;
    }
}
extern "C" int PntKalmanStep(int n, int m, const double *phi, const double *q, const double *h,
                             const double *r, const double *z, double *state, double *covariance,
                             double *predicted)
{
    if (n < 1 || n > 9 || m < 0 || !phi || !q || !state || !covariance || !predicted ||
        (m && (!h || !r || !z)))
        return 0;
    try {
        Read f(phi, n, n), noise(q, n, n);
        Eigen::LLT<Mat> priorCheck;
        if (!f.allFinite() || !validProcessNoise(noise) ||
            !Eigen::Map<const Vec>(state, n).allFinite() ||
            !factor(Read(covariance, n, n), priorCheck))
            return 0;
        Vec xp = f * Eigen::Map<const Vec>(state, n), x = xp;
        Mat p = f * Read(covariance, n, n) * f.transpose() + noise;
        Eigen::LLT<Mat> pCheck;
        if (!factor(p, pCheck))
            return 0;
        if (m) {
            Read observation(h, m, n), measurementNoise(r, m, m);
            Eigen::Map<const Vec> measurement(z, m);
            Eigen::LLT<Mat> rCheck, innovation;
            if (!observation.allFinite() || !measurement.allFinite() ||
                !factor(measurementNoise, rCheck))
                return 0;
            Mat pht = p * observation.transpose();
            if (!factor(observation * pht + measurementNoise, innovation))
                return 0;
            Mat gain = innovation.solve(pht.transpose()).transpose();
            x += gain * (measurement - observation * xp);
            Mat residual = Mat::Identity(n, n) - gain * observation;
            // Joseph form preserves the covariance through finite precision.
            p = (residual * p * residual.transpose() + gain * measurementNoise * gain.transpose())
                    .eval();
        }
        if (!x.allFinite() || !factor(p, pCheck))
            return 0;
        p = (0.5 * (p + p.transpose())).eval();
        View(covariance, n, n) = p;
        Eigen::Map<Vec>(state, n) = x;
        Eigen::Map<Vec>(predicted, n) = xp;
        return 1;
    } catch (...) {
        return 0;
    }
}
extern "C" double PntSpatialProjection(const double *cov, int stride, const double *direction)
{
    const Eigen::Map<const Eigen::Vector3d> up(direction);
    return up.dot(Read(cov, stride, stride).topLeftCorner<3, 3>() * up);
}
// Legacy C names are storage adapters only; no local matrix algorithms.
extern "C" void MatrixInit(double *m, int rows, int cols)
{
    View(m, rows, cols).setZero();
}
extern "C" void MatrixProduct(double *a, int ar, int ac, double *b, int br, int bc, double *out)
{
    if (ac == br)
        View(out, ar, bc) = (Read(a, ar, ac) * Read(b, br, bc)).eval();
}
extern "C" void MatrixTrasp(double *a, int rows, int cols, double *out)
{
    View(out, cols, rows) = Read(a, rows, cols).transpose().eval();
}
extern "C" void MatrixSum(double *a, int ar, int ac, double *b, int br, int bc, double *out)
{
    if (ar == br && ac == bc)
        View(out, ar, ac) = Read(a, ar, ac) + Read(b, br, bc);
}
extern "C" void MatrixDiff(double *a, int ar, int ac, double *b, int br, int bc, double *out)
{
    if (ar == br && ac == bc)
        View(out, ar, ac) = Read(a, ar, ac) - Read(b, br, bc);
}
extern "C" void MergeMatricesVertically(double *a, int ar, int ac, double *b, int br, int bc,
                                        double *out)
{
    if (ac == bc) {
        View(out, ar + br, ac).topRows(ar) = Read(a, ar, ac);
        View(out, ar + br, ac).bottomRows(br) = Read(b, br, bc);
    }
}
extern "C" void MatrixIdentity(double *out, int n)
{
    View(out, n, n).setIdentity();
}
