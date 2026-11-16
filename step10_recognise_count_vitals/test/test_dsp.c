/*
 * test_dsp.c - host tests for csi_dsp.c: FFT against a direct DFT,
 * detrend, Hann, parabolic interpolation, median, group map/dev, Jacobi,
 * and the power iteration (dsp_top_eig) against Jacobi.
 */

#include <string.h>
#include "test_util.h"
#include "csi_dsp.h"

static void test_fft_vs_dft(void)
{
    static float re[256], im[256], xr[256], xi[256];
    const int n = 256;
    rng_seed(1);
    for (int i = 0; i < n; i++) {
        xr[i] = rng_gauss();
        xi[i] = rng_gauss();
        re[i] = xr[i];
        im[i] = xi[i];
    }
    CHECK(dsp_fft(re, im, n), "dsp_fft refused n=256");
    double worst = 0.0;
    for (int k = 0; k < n; k++) {
        double sr = 0.0, si = 0.0;
        for (int t = 0; t < n; t++) {
            double a = -2.0 * 3.14159265358979 * (double)k * (double)t / (double)n;
            sr += xr[t] * cos(a) - xi[t] * sin(a);
            si += xr[t] * sin(a) + xi[t] * cos(a);
        }
        double e = fabs(sr - re[k]) + fabs(si - im[k]);
        if (e > worst) worst = e;
    }
    CHECK(worst < 1e-2, "FFT vs DFT max abs error %.3g (values ~16)", worst);

    CHECK(!dsp_fft(re, im, 100), "dsp_fft accepted a non power of two");
    CHECK(!dsp_fft(re, im, 2048), "dsp_fft accepted n > CSI_DSP_MAX_FFT");
}

static void test_fft_1024_tone(void)
{
    static float re[1024], im[1024];
    for (int i = 0; i < 1024; i++) {
        re[i] = cosf(2.0f * CSI_DSP_PI * 37.0f * (float)i / 1024.0f);
        im[i] = 0.0f;
    }
    dsp_fft(re, im, 1024);
    CHECK_NEAR(re[37], 512.0, 0.5, "1024-pt tone bin 37");
    CHECK(fabsf(re[36]) < 0.05f && fabsf(re[38]) < 0.05f, "leakage %g %g", re[36], re[38]);
}

static void test_autocorr_identity(void)
{
    /* dsp_fft of a power spectrum / n == circular autocorrelation */
    static float re[64], im[64], x[64];
    rng_seed(7);
    for (int i = 0; i < 64; i++) { x[i] = rng_gauss(); re[i] = x[i]; im[i] = 0.0f; }
    dsp_fft(re, im, 64);
    for (int i = 0; i < 64; i++) { re[i] = re[i] * re[i] + im[i] * im[i]; im[i] = 0.0f; }
    dsp_fft(re, im, 64);
    for (int lag = 0; lag < 5; lag++) {
        double ac = 0.0;
        for (int i = 0; i < 64; i++) ac += x[i] * x[(i + lag) % 64];
        CHECK_NEAR(re[lag] / 64.0f, ac, 1e-3 * (1.0 + fabs(ac)), "autocorrelation lag");
    }
}

static void test_detrend_hann(void)
{
    float x[50];
    for (int i = 0; i < 50; i++) x[i] = 3.0f + 0.25f * (float)i;
    dsp_detrend(x, 50);
    float worst = 0.0f;
    for (int i = 0; i < 50; i++) if (fabsf(x[i]) > worst) worst = fabsf(x[i]);
    CHECK(worst < 1e-4f, "detrend left %g", worst);

    float w[9];
    for (int i = 0; i < 9; i++) w[i] = 1.0f;
    dsp_hann(w, 9);
    CHECK_NEAR(w[0], 0.0, 1e-5, "hann[0]");
    CHECK_NEAR(w[4], 1.0, 1e-5, "hann[mid]");
    CHECK_NEAR(w[8], 0.0, 1e-5, "hann[last]");
    CHECK_NEAR(w[2], 0.5, 1e-5, "hann[quarter]");
}

static void test_parabolic_median(void)
{
    /* samples of -(x-0.3)^2 at -1, 0, 1 -> peak at +0.3 */
    float ym1 = -(1.3f * 1.3f), y0 = -(0.3f * 0.3f), yp1 = -(0.7f * 0.7f);
    CHECK_NEAR(dsp_parabolic_offset(ym1, y0, yp1), 0.3, 1e-5, "parabolic");
    CHECK_NEAR(dsp_parabolic_offset(1.0f, 0.5f, 2.0f), 0.0, 0.0, "not a peak");

    float v[5] = { 5, 1, 4, 2, 3 }, s[5];
    CHECK_NEAR(dsp_median(v, 5, s), 3.0, 0.0, "median odd");
    CHECK_NEAR(dsp_median(v, 4, s), 3.0, 0.0, "median even {5,1,4,2}");
    CHECK(v[0] == 5.0f, "median modified its input");
}

static void test_groups(void)
{
    uint8_t sel[20];
    int8_t  map[20];
    for (int k = 0; k < 20; k++) sel[k] = (k % 5 != 0);   /* 16 selected */
    int n = dsp_group_map(sel, 20, map);
    CHECK(n == 16, "nsel %d", n);
    int per[CSI_NGROUPS] = { 0 };
    int prev = 0;
    for (int k = 0; k < 20; k++) {
        if (!sel[k]) { CHECK(map[k] == -1, "unselected k=%d mapped", k); continue; }
        CHECK(map[k] >= prev, "groups not contiguous at k=%d", k);
        prev = map[k];
        per[map[k]]++;
    }
    for (int g = 0; g < CSI_NGROUPS; g++) CHECK(per[g] == 2, "group %d has %d", g, per[g]);

    float na[20], fast[20], inv[20], grp[CSI_NGROUPS];
    for (int k = 0; k < 20; k++) { na[k] = 1.0f + 0.1f * (float)k; fast[k] = 1.0f; inv[k] = 2.0f; }
    dsp_group_dev(na, fast, inv, map, 20, grp);
    /* group 0 = subcarriers 1,2 -> mean(0.1,0.2)*2 = 0.3 */
    CHECK_NEAR(grp[0], 0.3, 1e-5, "group 0 dev");
}

static void test_eig(void)
{
    /* A = Q diag(5,3,1,0.5) Q^T with a known Q from 3 Givens rotations */
    float a[16], a2[16], eig[4], vec[4];
    float d[4] = { 3.0f, 0.5f, 5.0f, 1.0f };
    float q[16] = {
        0.5f,  0.5f,  0.5f,  0.5f,
        0.5f, -0.5f,  0.5f, -0.5f,
        0.5f,  0.5f, -0.5f, -0.5f,
        0.5f, -0.5f, -0.5f,  0.5f,
    };  /* orthonormal (Hadamard / 2); columns are the eigenvectors */
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            float s = 0.0f;
            for (int k = 0; k < 4; k++) s += q[i * 4 + k] * d[k] * q[j * 4 + k];
            a[i * 4 + j] = s;
        }
    }
    memcpy(a2, a, sizeof(a));
    dsp_sym_eig(a2, 4, eig, vec);
    CHECK_NEAR(eig[0], 5.0, 1e-4, "eig0");
    CHECK_NEAR(eig[1], 3.0, 1e-4, "eig1");
    CHECK_NEAR(eig[2], 1.0, 1e-4, "eig2");
    CHECK_NEAR(eig[3], 0.5, 1e-4, "eig3");
    /* top eigenvector = column 2 of q, up to sign */
    float dot = 0.0f;
    for (int k = 0; k < 4; k++) dot += vec[k] * q[k * 4 + 2];
    CHECK_NEAR(fabsf(dot), 1.0, 1e-4, "top eigenvector");

    /* 8x8 random SPD: eigenvalues must sum to the trace and be sorted */
    float b[64], b2[64], e8[8];
    rng_seed(3);
    float m[64];
    for (int i = 0; i < 64; i++) m[i] = rng_gauss();
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            float s = 0.0f;
            for (int k = 0; k < 8; k++) s += m[i * 8 + k] * m[j * 8 + k];
            b[i * 8 + j] = s;
        }
    }
    memcpy(b2, b, sizeof(b));
    dsp_sym_eig(b2, 8, e8, NULL);
    float tr = 0.0f, se = 0.0f;
    for (int i = 0; i < 8; i++) { tr += b[i * 8 + i]; se += e8[i]; }
    CHECK_NEAR(se, tr, 1e-3 * tr, "sum of eigenvalues == trace");
    for (int i = 1; i < 8; i++) CHECK(e8[i] <= e8[i - 1], "not sorted at %d", i);
    CHECK(e8[7] > -1e-3f, "SPD matrix gave a negative eigenvalue %g", e8[7]);
}

/* ---- dsp_top_eig ---------------------------------------------------------
 * Power iteration, checked against dsp_sym_eig (Jacobi) and against
 * matrices built from known eigenvectors. */

#define N8 8

/* A = Q diag(d) Q' for a Q whose COLUMNS are orthonormal (row-major),
 * formed in double and rounded once. */
static void build_sym(const double *q, const double *d, int n, float *a)
{
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            double s = 0.0;
            for (int k = 0; k < n; k++) s += q[i * n + k] * d[k] * q[j * n + k];
            a[i * n + j] = (float)s;
        }
    }
}

/* 8x8 Sylvester Hadamard / sqrt(8): H[i][j] = (-1)^popcount(i & j). Column
 * 0 is all ones; every other column has four +1 and four -1 entries. */
static void hadamard8(double *q)
{
    for (int i = 0; i < N8; i++) {
        for (int j = 0; j < N8; j++) {
            int b = i & j, p = 0;
            while (b) { p ^= b & 1; b >>= 1; }
            q[i * N8 + j] = (p ? -1.0 : 1.0) / sqrt(8.0);
        }
    }
}

/* Random orthonormal columns: Gram-Schmidt (twice, for accuracy) on
 * Gaussian columns, in double. */
static void rand_orth8(double *q)
{
    for (int k = 0; k < N8; k++) {
        for (int i = 0; i < N8; i++) q[i * N8 + k] = (double)rng_gauss();
        for (int pass = 0; pass < 2; pass++) {
            for (int j = 0; j < k; j++) {
                double dot = 0.0;
                for (int i = 0; i < N8; i++) dot += q[i * N8 + j] * q[i * N8 + k];
                for (int i = 0; i < N8; i++) q[i * N8 + k] -= dot * q[i * N8 + j];
            }
        }
        double nn = 0.0;
        for (int i = 0; i < N8; i++) nn += q[i * N8 + k] * q[i * N8 + k];
        nn = sqrt(nn);
        for (int i = 0; i < N8; i++) q[i * N8 + k] /= nn;
    }
}

/* dsp_top_eig plus the checks every call gets: a untouched, vec unit
 * length, and the return value equal to vec's Rayleigh quotient. */
static float top_eig_checked(const float *a, int n, float *vec, int iters, const char *what)
{
    float  copy[N8 * N8];
    memcpy(copy, a, (size_t)(n * n) * sizeof(float));
    float  lam = dsp_top_eig(a, n, vec, iters);
    double nv = 0.0, ray = 0.0, scale = 0.0;
    for (int i = 0; i < n; i++) {
        double avi = 0.0;
        for (int j = 0; j < n; j++) {
            avi += (double)a[i * n + j] * vec[j];
            scale = fmax(scale, fabs((double)a[i * n + j]));
        }
        ray += (double)vec[i] * avi;
        nv  += (double)vec[i] * vec[i];
    }
    CHECK(memcmp(copy, a, (size_t)(n * n) * sizeof(float)) == 0, "%s: a was modified", what);
    CHECK(fabs(nv - 1.0) < 1e-5, "%s: |vec|^2 = %.7f", what, nv);
    CHECK(fabs(ray - (double)lam) <= 1e-5 * (double)n * scale,
          "%s: returned %.7g but vec's Rayleigh quotient is %.7g", what, (double)lam, ray);
    return lam;
}

static double abs_dot_col(const float *vec, const double *q, int col)
{
    double d = 0.0;
    for (int i = 0; i < N8; i++) d += (double)vec[i] * q[i * N8 + col];
    return fabs(d);
}

static void test_top_eig(void)
{
    float  a[N8 * N8], b[N8 * N8], vec[N8], vref[N8], e8[N8];
    double q[N8 * N8];

    printf("  dsp_top_eig vs dsp_sym_eig\n");

    /* (a) The start-vector traps. Hadamard eigenvectors, top eigenvalue on
     * column 5 (mixed signs): the top eigenvector is orthogonal to the
     * all-ones vector, and the diagonal is constant (every entry is
     * mean(d)), so a start from all-ones OR from the normalised diagonal
     * is exactly the eigenvector of column 0 - A 1 = 1.0 * 1 - and power
     * iteration from it can never leave. dsp_top_eig must still find 6. */
    {
        const double d[N8] = { 1.0, 0.5, 2.0, 0.8, 0.3, 6.0, 1.5, 0.2 };
        hadamard8(q);
        build_sym(q, d, N8, a);
        float worst_ones = 0.0f;
        for (int i = 0; i < N8; i++) {
            float s = 0.0f;
            for (int j = 0; j < N8; j++) s += a[i * N8 + j];
            worst_ones = fmaxf(worst_ones, fabsf(s - 1.0f));
        }
        CHECK(worst_ones < 1e-5f, "trap matrix: A*ones should be 1.0*ones (off by %g)",
              (double)worst_ones);
        float lam = top_eig_checked(a, N8, vec, 30, "Hadamard");
        CHECK_NEAR(lam, 6.0, 1e-5 * 6.0, "Hadamard top eigenvalue (all-ones/diagonal start gets 1.0)");
        CHECK(abs_dot_col(vec, q, 5) > 1.0 - 1e-6, "Hadamard top eigenvector: |dot| %.8f",
              abs_dot_col(vec, q, 5));
        printf("    Hadamard trap: lambda %.6f (want 6), |dot| with u1 %.8f\n", (double)lam,
               abs_dot_col(vec, q, 5));
    }

    /* (b) Rank one, mixed signs: A = x x' with sum(x) = 0 and
     * sum(x^3) = 0, so A * ones = 0 AND A * diag(A) = 0 - both obvious
     * starts give a zero vector. Answer: |x|^2 = 28.5, vec = +-x/|x|. */
    {
        const float x[N8] = { 1.0f, -1.0f, 2.0f, -2.0f, 0.5f, -0.5f, 3.0f, -3.0f };
        for (int i = 0; i < N8; i++) {
            for (int j = 0; j < N8; j++) a[i * N8 + j] = x[i] * x[j];
        }
        float lam = top_eig_checked(a, N8, vec, 30, "rank 1");
        double dot = 0.0;
        for (int i = 0; i < N8; i++) dot += (double)vec[i] * x[i];
        CHECK_NEAR(lam, 28.5, 1e-5 * 28.5, "rank-1 eigenvalue");
        CHECK_NEAR(fabs(dot) / sqrt(28.5), 1.0, 1e-6, "rank-1 eigenvector");
    }

    /* (c) The 4x4 matrix of test_eig (n < CSI_NGROUPS), and n = 1. */
    {
        const double d4[4] = { 3.0, 0.5, 5.0, 1.0 };
        const double q4[16] = {
            0.5,  0.5,  0.5,  0.5,
            0.5, -0.5,  0.5, -0.5,
            0.5,  0.5, -0.5, -0.5,
            0.5, -0.5, -0.5,  0.5,
        };
        float a4[16];
        build_sym(q4, d4, 4, a4);
        float lam = top_eig_checked(a4, 4, vec, 30, "4x4");
        double dot = 0.0;
        for (int i = 0; i < 4; i++) dot += (double)vec[i] * q4[i * 4 + 2];
        CHECK_NEAR(lam, 5.0, 1e-5 * 5.0, "4x4 top eigenvalue");
        CHECK_NEAR(fabs(dot), 1.0, 1e-6, "4x4 top eigenvector");

        float one = 4.0f;
        lam = top_eig_checked(&one, 1, vec, 30, "1x1");
        CHECK(lam == 4.0f && vec[0] == 1.0f, "1x1: %g, vec %g", (double)lam, (double)vec[0]);
    }

    /* (d) The zero matrix: 0 and e0. */
    {
        memset(a, 0, sizeof(a));
        for (int i = 0; i < N8; i++) vec[i] = 7.0f;
        float lam = top_eig_checked(a, N8, vec, 30, "zero");
        int e0 = (vec[0] == 1.0f);
        for (int i = 1; i < N8; i++) e0 &= (vec[i] == 0.0f);
        CHECK(lam == 0.0f && e0, "zero matrix: lambda %g, vec not e0", (double)lam);
    }

    /* (e) 1000 random Wishart 8x8 matrices (M M', M Gaussian) against
     * Jacobi. The angle to u1 shrinks by r = lambda_2 / lambda_1 per step,
     * from wherever the start put it. Always true, asserted per matrix: a
     * Rayleigh quotient never exceeds lambda_1. For separated matrices
     * (r <= 0.8) 30 steps normally leave float-level errors, but not
     * always: any start can land nearly orthogonal to u1 by chance - over
     * 20000 such matrices (a scratch run while writing this) the start
     * was within 0.6 degrees of orthogonal in 0.085% of cases (an all-ones
     * start: 2.2%) - and at r = 0.8 that costs ~20 more steps than the cap
     * allows. So the assertion is on the FRACTION that misses (at most
     * 1%), and the misses are printed. Same for lambda_2 by deflation, as
     * count_est.c gets its first estimate, where also lambda_3 / lambda_2
     * <= 0.8. */
    {
        double worst_sep_l = 0.0, worst_sep_v = 0.0, worst_near_l = 0.0, worst_l2 = 0.0;
        double worst_near_r = 0.0;
        int    nsep = 0, sep_miss = 0, nnear = 0, nl2 = 0, l2_miss = 0;
        rng_seed(4242);
        for (int trial = 0; trial < 1000; trial++) {
            float m[N8 * N8];
            for (int i = 0; i < N8 * N8; i++) m[i] = rng_gauss();
            for (int i = 0; i < N8; i++) {
                for (int j = 0; j < N8; j++) {
                    double s = 0.0;
                    for (int k = 0; k < N8; k++) s += (double)m[i * N8 + k] * m[j * N8 + k];
                    a[i * N8 + j] = (float)s;
                }
            }
            memcpy(b, a, sizeof(a));
            dsp_sym_eig(b, N8, e8, vref);
            float  lam = top_eig_checked(a, N8, vec, 30, "Wishart");
            double r   = (double)e8[1] / (double)e8[0];
            double el  = fabs((double)lam - e8[0]) / e8[0];
            CHECK((double)lam <= (double)e8[0] * (1.0 + 1e-5),
                  "trial %d: Rayleigh %.7g above lambda_1 %.7g", trial, (double)lam, (double)e8[0]);
            if (r <= 0.8) {
                double dot = 0.0;
                for (int i = 0; i < N8; i++) dot += (double)vec[i] * vref[i];
                double ev = 1.0 - fabs(dot);
                nsep++;
                worst_sep_l = fmax(worst_sep_l, el);
                worst_sep_v = fmax(worst_sep_v, ev);
                if (el >= 1e-5 || ev >= 1e-4) {
                    sep_miss++;
                    printf("    (miss) trial %d, r %.3f: lambda1 rel err %.2e, 1-|dot| %.2e\n",
                           trial, r, el, ev);
                }

                if ((double)e8[2] <= 0.8 * (double)e8[1]) {
                    float dfl[N8 * N8], v2[N8];
                    for (int i = 0; i < N8; i++) {
                        for (int j = 0; j < N8; j++) {
                            dfl[i * N8 + j] = a[i * N8 + j] - lam * vec[i] * vec[j];
                        }
                    }
                    float  l2  = top_eig_checked(dfl, N8, v2, 30, "deflated");
                    double el2 = fabs((double)l2 - e8[1]) / e8[1];
                    nl2++;
                    worst_l2 = fmax(worst_l2, el2);
                    if (el2 >= 1e-4) {
                        l2_miss++;
                        printf("    (miss) trial %d, lambda3/lambda2 %.3f: deflated lambda2 "
                               "rel err %.2e\n", trial, (double)e8[2] / (double)e8[1], el2);
                    }
                }
            } else {
                nnear++;
                if (el > worst_near_l) {
                    worst_near_l = el;
                    worst_near_r = r;
                }
            }
        }
        CHECK(sep_miss * 100 <= nsep, "lambda1: %d of %d separated matrices missed", sep_miss, nsep);
        CHECK(l2_miss * 100 <= nl2, "deflated lambda2: %d of %d missed", l2_miss, nl2);
        printf("    Wishart, r = lambda2/lambda1 <= 0.8: %d of %d within (1e-5 on lambda1, 1e-4 on "
               "1-|dot|); max rel err %.2e, max 1-|dot| %.2e\n", nsep - sep_miss, nsep,
               worst_sep_l, worst_sep_v);
        printf("    Wishart, r > 0.8 (%d): max rel err lambda1 %.2e (at r = %.4f)\n",
               nnear, worst_near_l, worst_near_r);
        printf("    deflated lambda2 (lambda3/lambda2 <= 0.8): %d of %d within 1e-4, max rel err %.2e\n",
               nl2 - l2_miss, nl2, worst_l2);
    }

    /* (f) Near-degenerate top pair, known eigenvectors: 10 and 9.99, then
     * a clear gap (3, 2, 1, ...). The vector is NOT expected to match u1 -
     * after 30 steps at r = 0.999 it is a mix of u1 and u2 - but it must
     * lie in their plane, and the Rayleigh quotient must lie in
     * [lambda_2, lambda_1]. Twice: random eigenvectors and Hadamard ones
     * (top pair on mixed-sign columns). */
    {
        const double d[N8] = { 10.0, 9.99, 3.0, 2.0, 1.0, 0.5, 0.2, 0.1 };
        const double dh[N8] = { 1.0, 0.5, 3.0, 10.0, 0.2, 9.99, 2.0, 0.1 };
        for (int pass = 0; pass < 2; pass++) {
            int c1 = 0, c2 = 1;
            if (pass == 0) {
                rng_seed(99);
                rand_orth8(q);
                build_sym(q, d, N8, a);
            } else {
                hadamard8(q);
                build_sym(q, dh, N8, a);
                c1 = 3;
                c2 = 5;
            }
            float  lam = top_eig_checked(a, N8, vec, 30, "near-degenerate");
            double p1 = abs_dot_col(vec, q, c1), p2 = abs_dot_col(vec, q, c2);
            double out = 1.0 - p1 * p1 - p2 * p2;
            CHECK((double)lam >= 9.99 * (1.0 - 1e-5) && (double)lam <= 10.0 * (1.0 + 1e-5),
                  "near-degenerate (%s): lambda %.7f outside [9.99, 10]",
                  pass ? "Hadamard" : "random", (double)lam);
            CHECK(out < 1e-5, "near-degenerate (%s): %.2e of vec outside span{u1,u2}",
                  pass ? "Hadamard" : "random", out);
            printf("    near-degenerate 10 / 9.99 (%s Q): lambda %.5f, vec = %.3f u1 + %.3f u2 "
                   "(abs), outside the plane %.1e\n", pass ? "Hadamard" : "random",
                   (double)lam, p1, p2, out);
        }
    }

    /* (g) Scale: the stopping test is relative and the iterate is scaled
     * by its largest component, so 1e-33 A and 1e33 A give the same vector
     * and a scaled eigenvalue (no overflow in the squares of 1e33). */
    {
        rng_seed(5);
        rand_orth8(q);
        const double d[N8] = { 4.0, 2.0, 1.0, 0.8, 0.5, 0.3, 0.2, 0.1 };
        build_sym(q, d, N8, a);
        float lam1 = top_eig_checked(a, N8, vref, 30, "scale 1");
        const float sc[2] = { 1e-33f, 1e33f };
        for (int s = 0; s < 2; s++) {
            for (int i = 0; i < N8 * N8; i++) b[i] = a[i] * sc[s];
            float  lam = top_eig_checked(b, N8, vec, 30, "scaled");
            double dot = 0.0;
            for (int i = 0; i < N8; i++) dot += (double)vec[i] * vref[i];
            CHECK_NEAR((double)lam / ((double)lam1 * (double)sc[s]), 1.0, 1e-5, "scaled lambda");
            CHECK_NEAR(fabs(dot), 1.0, 1e-6, "scaled vector");
        }
    }
}

int main(void)
{
    test_fft_vs_dft();
    test_fft_1024_tone();
    test_autocorr_identity();
    test_detrend_hann();
    test_parabolic_median();
    test_groups();
    test_eig();
    test_top_eig();
    return test_summary("test_dsp");
}
