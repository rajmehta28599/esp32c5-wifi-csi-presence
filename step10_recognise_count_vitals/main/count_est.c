/*
 * count_est.c - see count_est.h. EXPERIMENTAL: this produces features that
 * separate "one mover" from "several movers" on synthetic data. Whether a
 * real room separates them is only known after count-training there.
 *
 * No FPU on the C5: float only, no trigonometry. Per packet the work is
 * 8 adds + 36 multiply-adds into running sums (no sample ring); the
 * block-end maths (covariance, two power iterations, a 2x2 Rayleigh-Ritz
 * step) runs once per COUNT_NWIN packets.
 *
 * sum_xy LAYOUT
 *   The upper triangle (j >= i) of sum_t grp_i(t) * grp_j(t), row by row:
 *   (0,0) (0,1) .. (0,7) (1,1) .. (1,7) .. (7,7) - element (i, j) sits at
 *   i * (2*CSI_NGROUPS - i + 1) / 2 + (j - i). count_push() and
 *   count_features() both walk it in exactly that order with a running
 *   index, so neither evaluates the formula.
 */

#include "count_est.h"

#include <math.h>
#include <string.h>

/* Power-iteration cap for lambda_1 and for lambda_2 (csi_dsp.h). lambda_1
 * of a one-mover block converges in a few steps and exits early; lambda_2
 * of such a block sits in the cluster of near-equal noise eigenvalues,
 * where the vector never settles and the cap is what stops it. Its
 * Rayleigh quotient is then inside the cluster, i.e. at the noise level,
 * which is all lambda_2 means there - but somewhat BELOW the top of the
 * cluster: on test_count.c's synthetic one-mover blocks lambda_2 /
 * lambda_1 reads up to ~7% low, ~1e-3 absolute, against a gap of ~0.4
 * between the one- and two-mover class means. test_count.c compares both
 * ratios against Jacobi and prints the worst error. */
#define COUNT_EIG_ITERS 30

void count_init(count_est_t *c)
{
    count_reset(c);
}

void count_reset(count_est_t *c)
{
    memset(c, 0, sizeof(*c));
}

/*
 * Block-end features. Runs once per COUNT_NWIN packets. Soft-float
 * operations per block - every add, multiply, divide, compare and
 * conversion that is a libgcc call on the C5 - COUNTED on the host by
 * compiling this file with an instrumented float type over synthetic
 * blocks, NOT timed on the C5: ~6-7k with one or two movers (worst seen
 * ~7.2k), of which the two dsp_top_eig() calls are ~5-6k and the
 * covariance, scaling, deflation and Rayleigh-Ritz step ~1k; ~11.5k on
 * pure noise, where all eight eigenvalues nearly tie and both power
 * iterations run to COUNT_EIG_ITERS. The 8x8 Jacobi this replaced counted
 * ~19-24k per block (worst ~27k) on the same data. At 50-100 cycles an
 * operation that is roughly 1.5-5 ms at 240 MHz where it was 4-11 ms - an
 * estimate. It matters because a block can end on the same packet as a
 * gait window and a vitals FFT, and the CSI queue has to absorb all of
 * them together. Stack (GCC -fstack-usage): 704 bytes here plus 192
 * inside dsp_top_eig; count_est_t itself stays under 1 KB and holds no
 * work arrays.
 */
static void count_features(const count_est_t *c, float feat[COUNT_NFEAT])
{
    float cov[CSI_NGROUPS * CSI_NGROUPS];   /* C, then C / max diagonal */
    float dfl[CSI_NGROUPS * CSI_NGROUPS];   /* the deflated matrix      */
    float v1[CSI_NGROUPS], v2[CSI_NGROUPS], q[CSI_NGROUPS];
    const float n = (float)c->n;

    /* One-pass covariance, C_ij = (S_ij - S_i * S_j / n) / (n - 1).
     *
     * The usual objection to this form is cancellation: when a signal's
     * mean is large next to its spread, S_ij and S_i * S_j / n are two
     * big, nearly equal numbers and float keeps only a few digits of their
     * difference. It does not bite here because the group signals are
     * high-passed by construction (dsp_group_dev subtracts the 0.5 s EMA,
     * a ~0.3 Hz corner), so over a block their mean is small next to their
     * standard deviation and the subtraction removes a small correction,
     * not most of the value. The error then stays near the float error of
     * accumulating 256 terms (~1e-6 typical, ~1e-5 worst case, relative),
     * far below the CSI noise. A signal carrying a DC offset of many
     * standard deviations would degrade it; dsp_group_dev does not produce
     * one. (test_count.c checks this against a two-pass double reference,
     * with and without a moderate offset.) */
    int   k     = 0;
    float trace = 0.0f, dmax = 0.0f;
    for (int i = 0; i < CSI_NGROUPS; i++) {
        for (int j = i; j < CSI_NGROUPS; j++) {
            float x = (c->sum_xy[k] - c->sum[i] * c->sum[j] / n) / (n - 1.0f);
            if (i == j) {
                /* a variance is >= 0; a constant group can round to -tiny */
                if (x < 0.0f) x = 0.0f;
                trace += x;
                if (x > dmax) dmax = x;
            }
            cov[i * CSI_NGROUPS + j] = x;
            cov[j * CSI_NGROUPS + i] = x;
            k++;
        }
    }

    /* No variance at all (dmax == 0 exactly when every diagonal element,
     * and so the whole PSD matrix, is 0): PR = 1 and both ratios are 0. */
    float pr = 1.0f, r21 = 0.0f, r1 = 0.0f;
    if (dmax > 0.0f) {
        /* Work on C / dmax. For a PSD matrix |C_ij| <= sqrt(C_ii C_jj) <=
         * dmax, so every entry lands in [-1, 1], the trace in [1, 8] and
         * the squared Frobenius norm in [1, 64]: nothing below can
         * overflow or underflow whatever the input scale, and all three
         * features are ratios, so the scale drops out. Division, not a
         * reciprocal: 1 / dmax overflows for a denormal dmax. 36 divisions
         * once a block is noise next to the power iterations. */
        float tr = 0.0f, fro = 0.0f;
        for (int i = 0; i < CSI_NGROUPS; i++) {
            for (int j = i; j < CSI_NGROUPS; j++) {
                float x = cov[i * CSI_NGROUPS + j] / dmax;
                cov[i * CSI_NGROUPS + j] = x;
                cov[j * CSI_NGROUPS + i] = x;
                if (i == j) {
                    tr  += x;
                    fro += x * x;
                } else {
                    fro += 2.0f * x * x;
                }
            }
        }

        /* [0] PR = (sum lambda)^2 / sum lambda^2 = trace^2 / ||C||_F^2,
         * exactly, for any symmetric C (sum lambda = trace, sum lambda^2
         * = sum of squared entries) - no eigenvalues needed. fro >= 1 (the
         * largest diagonal entry is 1), so the division is safe. */
        pr = tr * tr / fro;
        /* rounding can step a hair outside the mathematical range */
        if (pr < 1.0f) pr = 1.0f;
        if (pr > (float)CSI_NGROUPS) pr = (float)CSI_NGROUPS;

        /* The true lambda_1 is at least the largest diagonal entry, 1 here
         * (e_k's Rayleigh quotient is C_kk); the estimate approaches it
         * from below, and the l1 > 0 test further down guards the
         * divisions all the same. */
        float l1 = dsp_top_eig(cov, CSI_NGROUPS, v1, COUNT_EIG_ITERS);

        /* lambda_2, first estimate: the top eigenvalue of the deflated
         * matrix C - lambda_1 v1 v1'. With v1 off u1 by an angle t, that
         * matrix keeps ~lambda_2 + (lambda_1 - lambda_2) sin^2 t and gains
         * a small negative eigenvalue (csi_dsp.h works it through). */
        for (int i = 0; i < CSI_NGROUPS; i++) {
            float li = l1 * v1[i];
            for (int j = 0; j < CSI_NGROUPS; j++) {
                int ij = i * CSI_NGROUPS + j;
                dfl[ij] = cov[ij] - li * v1[j];
            }
        }
        float l2 = dsp_top_eig(dfl, CSI_NGROUPS, v2, COUNT_EIG_ITERS);

        /* Rayleigh-Ritz on span{v1, v2}: the exact eigenvalues of C
         * restricted to that plane. Why: with two movers of similar
         * strength lambda_2 / lambda_1 is close to 1, v1 converges slowly
         * and is still a mix of u1 and u2 after COUNT_EIG_ITERS steps, so
         * the two estimates above are pulled together (on a synthetic
         * two-mover block, 0.873 read as 0.885 without this step). But the
         * PLANE of v1 and v2 converges at lambda_3 / lambda_2 - fast,
         * because the third eigenvalue of a two-mover block is the noise
         * floor - and the eigenvalues of C within that plane are then
         * lambda_1 and lambda_2 whatever the mix. Cost: two matrix-vector
         * products and two square roots. With one mover v2 sits in the
         * noise cluster, the plane is v1 plus a noise direction, and the
         * step changes nothing that matters. By Cauchy interlacing the
         * Ritz values never exceed lambda_1 and lambda_2, and the first is
         * never below the power-iteration l1. */
        {
            float d12 = 0.0f;
            for (int i = 0; i < CSI_NGROUPS; i++) d12 += v1[i] * v2[i];
            float nq2 = 0.0f;
            for (int i = 0; i < CSI_NGROUPS; i++) {
                q[i] = v2[i] - d12 * v1[i];
                nq2 += q[i] * q[i];
            }
            /* v2 (almost) parallel to v1 means the deflated matrix had
             * nothing left - C of rank ~1: keep the estimates above. The
             * threshold is |q| > 0.01, far above rounding. */
            if (nq2 > 1e-4f) {
                float inq = 1.0f / sqrtf(nq2);
                for (int i = 0; i < CSI_NGROUPS; i++) q[i] *= inq;
                float h11 = 0.0f, h12 = 0.0f, h22 = 0.0f;
                for (int i = 0; i < CSI_NGROUPS; i++) {
                    const float *ri = &cov[i * CSI_NGROUPS];
                    float cv = 0.0f, cq = 0.0f;
                    for (int j = 0; j < CSI_NGROUPS; j++) {
                        cv += ri[j] * v1[j];
                        cq += ri[j] * q[j];
                    }
                    h11 += v1[i] * cv;
                    h12 += v1[i] * cq;
                    h22 += q[i] * cq;
                }
                /* eigenvalues of [[h11, h12], [h12, h22]] */
                float hm  = 0.5f * (h11 + h22);
                float hd  = 0.5f * (h11 - h22);
                float rad = sqrtf(hd * hd + h12 * h12);
                l1 = hm + rad;
                l2 = hm - rad;
            }
        }

        /* C is PSD, so a negative lambda_2 is rounding or the deflated
         * matrix's negative eigenvalue: 0. lambda_2 / lambda_1 <= 1 and
         * lambda_1 / trace <= 1 hold mathematically; rounding can step a
         * hair past when the values nearly tie. */
        if (l2 < 0.0f) l2 = 0.0f;
        if (l1 > 0.0f) {
            r21 = l2 / l1;
            if (r21 > 1.0f) r21 = 1.0f;
            /* [2] lambda_1 / sum lambda = lambda_1 / trace, in [1/8, 1] */
            r1 = l1 / tr;
            if (r1 > 1.0f) r1 = 1.0f;
        }
    }

    /* Burstiness, std(e) / mean(e) with the population std over the block.
     * Here the one-pass form DOES cancel - e >= 0 is not zero-mean - so the
     * absolute error in b^2 is ~(2 + b^2) x the float accumulation error
     * (~1e-6). That only matters when b is tiny, and it is not: even pure
     * noise in 8 groups makes e chi-square with 8 degrees of freedom,
     * b = sqrt(2/8) = 0.5, and motion is burstier than noise. The clamp
     * covers the rounding case of an exactly constant e. */
    float me  = c->e_sum / n;
    float me2 = c->e_sum2 / n;
    float var = me2 - me * me;
    if (var < 0.0f) var = 0.0f;
    float burst = (me > 0.0f) ? sqrtf(var) / me : 0.0f;

    feat[0] = pr;
    feat[1] = r21;
    feat[2] = r1;
    feat[3] = log10f(trace + 1e-6f);
    feat[4] = (float)c->n_moving / n;
    feat[5] = burst;
}

bool count_push(count_est_t *c, const float grp[CSI_NGROUPS], bool moving,
                float feat[COUNT_NFEAT])
{
    /* 36 multiply-adds per packet; the i == j product doubles as the
     * energy term, so e(t) costs no extra multiplies. */
    int   k = 0;
    float e = 0.0f;
    for (int i = 0; i < CSI_NGROUPS; i++) {
        float xi = grp[i];
        float sq = xi * xi;
        c->sum[i] += xi;
        c->sum_xy[k++] += sq;               /* (i, i) */
        e += sq;
        for (int j = i + 1; j < CSI_NGROUPS; j++) {
            c->sum_xy[k++] += xi * grp[j];  /* (i, j), j > i */
        }
    }
    c->e_sum  += e;
    c->e_sum2 += e * e;
    if (moving) c->n_moving++;
    c->n++;

    if (c->n < COUNT_NWIN) {
        return false;
    }
    /* Always emit the features at block end, whatever the duty - which
     * blocks are worth training on or classifying is the caller's call
     * (main.c uses COUNT_MIN_DUTY). */
    count_features(c, feat);
    count_reset(c);
    return true;
}
