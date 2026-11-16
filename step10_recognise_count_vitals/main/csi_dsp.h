/*
 * csi_dsp.h - the small DSP toolbox shared by Step 10's analytics modules
 *             (vitals.c, gait_id.c, count_est.c).
 *
 * Deliberately free of ESP-IDF headers, so the same file compiles on the
 * host (MSVC, see test/run_tests.ps1) and on the C5.
 *
 * THE C5 HAS NO FPU. It is built -march=rv32imac -mabi=ilp32, so every
 * float operation below is a libgcc software routine (__mulsf3, __addsf3,
 * ...), roughly 50-100 cycles each. Consequences for everything that
 * includes this header:
 *   - float only, never double (double emulation is slower still);
 *   - sinf/cosf/expf are hundreds of cycles - never call them per sample,
 *     use a rotation recurrence (dsp_hann and dsp_fft both do);
 *   - per-packet work must stay O(nsub); FFT-sized work is spread so that
 *     no single packet pays for more than one FFT.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#define CSI_DSP_PI      3.14159265358979f

/* Selected subcarriers are split into this many contiguous groups. The
 * gait and count modules work on the group signals, not on individual
 * subcarriers: motion perturbs roughly ten neighbouring subcarriers
 * coherently, so eight groups cover a 52-subcarrier HT20 frame with little
 * lost information and make the covariance matrix a cheap 8 x 8. */
#define CSI_NGROUPS     8

/* Largest FFT dsp_fft accepts. */
#define CSI_DSP_MAX_FFT 1024

/* In-place iterative radix-2 complex FFT (forward, no scaling).
 * n must be a power of two in [2, CSI_DSP_MAX_FFT]; returns false and
 * leaves the data untouched otherwise.
 * For a real, even input (a power spectrum, for example) the forward
 * transform equals n times the inverse, so the autocorrelation of x is
 * dsp_fft(|X|^2, 0) / n - no separate inverse routine is needed. */
bool dsp_fft(float *re, float *im, int n);

/* Remove the least-squares straight line (mean and slope) from x[0..n-1]. */
void dsp_detrend(float *x, int n);

/* Multiply x[0..n-1] by a symmetric Hann window. n >= 2. */
void dsp_hann(float *x, int n);

/* Fractional peak position from three neighbouring spectrum values
 * y[-1], y[0], y[+1] (parabolic interpolation). Returns an offset in
 * [-0.5, 0.5] to add to the centre bin index; 0 if the three points do
 * not form a peak. */
float dsp_parabolic_offset(float ym1, float y0, float yp1);

/* Median of x[0..n-1]. scratch must hold n floats; x is not modified.
 * Insertion sort: intended for n <= ~150. Returns 0 for n <= 0. */
float dsp_median(const float *x, int n, float *scratch);

/* Build the subcarrier -> group map once per baseline.
 * map[k] = group index 0..CSI_NGROUPS-1 for selected subcarriers, split
 * into CSI_NGROUPS contiguous runs of (nearly) equal size in subcarrier
 * order; map[k] = -1 for unselected ones. Returns the number of selected
 * subcarriers. With fewer than CSI_NGROUPS selected, some groups are empty. */
int dsp_group_map(const uint8_t *selected, int nsub, int8_t *map);

/* Per-packet group signals: grp[g] = mean over the subcarriers k in group g
 * of (na[k] - fast[k]) * inv_sigma[k]. That is the signed deviation of the
 * normalised amplitude from its 0.5 s EMA, in units of empty-room noise - a
 * high-passed (~0.3 Hz corner) signal. Empty groups output 0. */
void dsp_group_dev(const float *na, const float *fast, const float *inv_sigma,
                   const int8_t *map, int nsub, float grp[CSI_NGROUPS]);

/* Eigen-decomposition of a symmetric n x n matrix (row-major), cyclic
 * Jacobi, n in [1, CSI_NGROUPS]. The matrix a is destroyed.
 * eig[0..n-1] receives the eigenvalues sorted in DESCENDING order.
 * vec_top, if not NULL, receives the unit eigenvector (n floats) of the
 * largest eigenvalue.
 * The firmware no longer calls this: an 8 x 8 Jacobi costs ~20k soft-float
 * operations (counted on the host, not timed on the C5), and gait_id.c and
 * count_est.c only need the top one or two eigenvalues, which
 * dsp_top_eig() gets for a fraction of that. It stays because it is exact
 * and simple, which makes it the reference test_dsp.c and test_count.c
 * check dsp_top_eig() against. */
void dsp_sym_eig(float *a, int n, float *eig, float *vec_top);

/* Largest eigenvalue of a symmetric positive semi-definite n x n matrix
 * (row-major), n in [1, CSI_NGROUPS], by power iteration. a is NOT
 * modified. Returns the Rayleigh quotient v'Av of the unit vector it
 * writes to vec (n floats). The zero matrix returns 0 with vec = e0.
 * Entries must be finite; otherwise the result is meaningless (but the
 * call still ends after at most iters steps).
 *
 * START: v = A s, where s_k = +1 or -1 is the sign of (column k) . (column
 *   kmax) and kmax is the largest diagonal element. For the usual CSI
 *   covariance - one dominant mover plus noise - every column is roughly
 *   lambda_1 u_k u, so aligning their signs makes them add up along u and
 *   the start is already close. The obvious fixed starts are not safe:
 *   all-ones, or the diagonal, is exactly orthogonal to the top eigenvector
 *   whenever that vector has mixed signs that cancel (a mover that raises
 *   some subcarrier groups and lowers others), and power iteration then
 *   never finds it; test_dsp.c has such a matrix. A s lies in the range of
 *   A, where a PSD matrix is positive definite, so A v never vanishes
 *   during the iteration. If A s rounds to ~0, column kmax is used instead.
 *   No start is right for every matrix. Over 20000 random 8x8 covariance
 *   (Wishart) matrices, measured on the host, this one came within 0.6
 *   degrees of orthogonal to the top eigenvector (|cos| < 0.01) in 0.085%
 *   of cases, all-ones in 2.2%. Such a start costs about
 *   log(100) / log(lambda_1 / lambda_2) extra steps: nothing when one
 *   eigenvalue dominates, more than the cap allows once lambda_2 /
 *   lambda_1 passes ~0.8 - and the result is then the mix described
 *   under CONVERGENCE.
 *
 * STOPPING: at most iters multiplications by A; earlier once no component
 *   of the iterate (scaled to max |component| = 1) moves by more than 1e-6
 *   in one step. The test is relative, so the result does not depend on
 *   the scale of a. Scaling by the largest component instead of the 2-norm
 *   needs no square root per step and cannot overflow or underflow.
 *
 * CONVERGENCE: the angle between v and the top eigenvector u1 shrinks by
 *   lambda_2 / lambda_1 per step, so a clear top eigenvalue (one mover:
 *   ratio ~0.01) converges in 3-4 steps. With near-equal lambda_1 and
 *   lambda_2 it barely moves, and vec comes back as a MIX of u1 and u2.
 *   The returned value is still accurate: the Rayleigh quotient is off by
 *   about (lambda_1 - lambda_2) sin^2(angle) - second order in the angle,
 *   and small in exactly the near-tie case where the angle stays large,
 *   because there lambda_1 - lambda_2 is small. What the mix means for the
 *   callers:
 *   - gait_id.c projects the group signals onto vec. When lambda_1 ~
 *     lambda_2 there is no single "first principal component" - an exact
 *     solver's u1 would swing between the two directions from one window
 *     to the next on noise alone - and any unit vector in the top two
 *     dimensions captures between lambda_2 and lambda_1 of the variance.
 *     The mix is deterministic in the data, so enrolment and recognition
 *     see the same thing.
 *   - count_est.c deflates, C - lambda v v', and runs this again for
 *     lambda_2. With v at angle t from u1 the deflated matrix keeps
 *     ~lambda_2 + (lambda_1 - lambda_2) sin^2 t instead of lambda_2, and
 *     gains a small negative eigenvalue, while the first call returned
 *     ~lambda_1 - (lambda_1 - lambda_2) sin^2 t. So the two estimates are
 *     pulled together: lambda_2 / lambda_1 reads high by about
 *     2 (1 - lambda_2 / lambda_1) sin^2 t - only when the two are already
 *     close, but measurably (0.873 read as 0.885 on a synthetic
 *     two-mover block). count_est.c removes that with a 2x2
 *     Rayleigh-Ritz step on the two vectors, because the plane they span
 *     converges much faster than either vector does.
 *   In a cluster of near-equal SMALL eigenvalues (the noise floor behind
 *   one mover, where count_est.c looks for lambda_2) the vector never
 *   settles, but the Rayleigh quotient still lands inside the cluster,
 *   i.e. at the noise level, which is what lambda_2 is there - somewhat
 *   below the cluster's top (count_est.c, COUNT_EIG_ITERS, has the size).
 *
 * COST for n = 8: the start is ~200 soft-float operations; each step is
 * one matrix-vector product (64 multiplies + 64 adds), one division, 16
 * multiplies/subtracts and ~17 compares, ~170 operations. 30 steps come to
 * ~5.3k, a call that exits after 3-4 steps to ~0.8k, against ~20k for
 * dsp_sym_eig on the same matrices. Counted on the host with an
 * instrumented float type, not timed on the C5. */
float dsp_top_eig(const float *a, int n, float *vec, int iters);
