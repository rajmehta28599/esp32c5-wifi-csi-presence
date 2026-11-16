/*
 * csi_dsp.c - see csi_dsp.h. No FPU on the C5: float only, no per-sample
 * trigonometry.
 */

#include "csi_dsp.h"

#include <math.h>
#include <string.h>

bool dsp_fft(float *re, float *im, int n)
{
    if (n < 2 || n > CSI_DSP_MAX_FFT || (n & (n - 1)) != 0) {
        return false;
    }

    /* bit-reversal permutation */
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }

    /* Butterflies. The twiddle loop is OUTSIDE the block loop, so each
     * twiddle factor is produced once per stage by a rotation recurrence:
     * one sinf/cosf pair per stage instead of one per butterfly. Over at
     * most 512 steps the recurrence drifts by ~1e-5, far below the CSI
     * noise it is used on. */
    for (int len = 2; len <= n; len <<= 1) {
        int   half = len >> 1;
        float ang  = -2.0f * CSI_DSP_PI / (float)len;
        float wr   = cosf(ang), wi = sinf(ang);
        float cr   = 1.0f,      ci = 0.0f;
        for (int k = 0; k < half; k++) {
            for (int i = k; i < n; i += len) {
                int   b  = i + half;
                float tr = re[b] * cr - im[b] * ci;
                float ti = re[b] * ci + im[b] * cr;
                re[b] = re[i] - tr;
                im[b] = im[i] - ti;
                re[i] += tr;
                im[i] += ti;
            }
            float ncr = cr * wr - ci * wi;
            ci = cr * wi + ci * wr;
            cr = ncr;
        }
    }
    return true;
}

void dsp_detrend(float *x, int n)
{
    if (n <= 0) return;
    if (n == 1) { x[0] = 0.0f; return; }

    float tm = 0.5f * (float)(n - 1);
    float xm = 0.0f;
    for (int i = 0; i < n; i++) xm += x[i];
    xm /= (float)n;

    float num = 0.0f, den = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i - tm;
        num += t * (x[i] - xm);
        den += t * t;
    }
    float slope = (den > 0.0f) ? num / den : 0.0f;
    for (int i = 0; i < n; i++) {
        x[i] -= xm + slope * ((float)i - tm);
    }
}

void dsp_hann(float *x, int n)
{
    if (n < 2) return;
    /* w[i] = 0.5 - 0.5 cos(2 pi i / (n-1)); cos(i*d) by rotation. */
    float d  = 2.0f * CSI_DSP_PI / (float)(n - 1);
    float cd = cosf(d), sd = sinf(d);
    float c  = 1.0f,    s  = 0.0f;
    for (int i = 0; i < n; i++) {
        x[i] *= 0.5f - 0.5f * c;
        float nc = c * cd - s * sd;
        s = s * cd + c * sd;
        c = nc;
    }
}

float dsp_parabolic_offset(float ym1, float y0, float yp1)
{
    float den = ym1 - 2.0f * y0 + yp1;
    if (!(y0 >= ym1 && y0 >= yp1) || den >= 0.0f) {
        return 0.0f;
    }
    float p = 0.5f * (ym1 - yp1) / den;
    if (p > 0.5f)  p = 0.5f;
    if (p < -0.5f) p = -0.5f;
    return p;
}

float dsp_median(const float *x, int n, float *scratch)
{
    if (n <= 0) return 0.0f;
    memcpy(scratch, x, (size_t)n * sizeof(float));
    for (int i = 1; i < n; i++) {
        float v = scratch[i];
        int   j = i - 1;
        while (j >= 0 && scratch[j] > v) {
            scratch[j + 1] = scratch[j];
            j--;
        }
        scratch[j + 1] = v;
    }
    return (n & 1) ? scratch[n / 2] : 0.5f * (scratch[n / 2 - 1] + scratch[n / 2]);
}

int dsp_group_map(const uint8_t *selected, int nsub, int8_t *map)
{
    int nsel = 0;
    for (int k = 0; k < nsub; k++) {
        if (selected[k]) nsel++;
    }
    int idx = 0;
    for (int k = 0; k < nsub; k++) {
        if (selected[k]) {
            map[k] = (int8_t)((idx * CSI_NGROUPS) / nsel);
            idx++;
        } else {
            map[k] = -1;
        }
    }
    return nsel;
}

void dsp_group_dev(const float *na, const float *fast, const float *inv_sigma,
                   const int8_t *map, int nsub, float grp[CSI_NGROUPS])
{
    float sum[CSI_NGROUPS];
    int   cnt[CSI_NGROUPS];
    for (int g = 0; g < CSI_NGROUPS; g++) {
        sum[g] = 0.0f;
        cnt[g] = 0;
    }
    for (int k = 0; k < nsub; k++) {
        int g = map[k];
        if (g < 0) continue;
        sum[g] += (na[k] - fast[k]) * inv_sigma[k];
        cnt[g]++;
    }
    for (int g = 0; g < CSI_NGROUPS; g++) {
        grp[g] = cnt[g] ? sum[g] / (float)cnt[g] : 0.0f;
    }
}

void dsp_sym_eig(float *a, int n, float *eig, float *vec_top)
{
    float v[CSI_NGROUPS * CSI_NGROUPS];

    if (n < 1) return;
    if (n > CSI_NGROUPS) n = CSI_NGROUPS;

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            v[i * n + j] = (i == j) ? 1.0f : 0.0f;
        }
    }

    /* Cyclic Jacobi. Each rotation zeroes a[p][q]; 8 x 8 converges in
     * well under 10 sweeps. */
    for (int sweep = 0; sweep < 30; sweep++) {
        float off = 0.0f, diag = 0.0f;
        for (int i = 0; i < n; i++) {
            diag += fabsf(a[i * n + i]);
            for (int j = i + 1; j < n; j++) {
                off += fabsf(a[i * n + j]);
            }
        }
        if (off <= 1e-7f * diag || off == 0.0f) {
            break;
        }
        for (int p = 0; p < n - 1; p++) {
            for (int q = p + 1; q < n; q++) {
                float apq = a[p * n + q];
                if (fabsf(apq) < 1e-30f) continue;
                float theta = (a[q * n + q] - a[p * n + p]) / (2.0f * apq);
                float t = 1.0f / (fabsf(theta) + sqrtf(theta * theta + 1.0f));
                if (theta < 0.0f) t = -t;
                float c = 1.0f / sqrtf(t * t + 1.0f);
                float s = t * c;
                /* A <- A J (columns p, q) */
                for (int k = 0; k < n; k++) {
                    float akp = a[k * n + p], akq = a[k * n + q];
                    a[k * n + p] = c * akp - s * akq;
                    a[k * n + q] = s * akp + c * akq;
                }
                /* A <- J^T A (rows p, q) */
                for (int k = 0; k < n; k++) {
                    float apk = a[p * n + k], aqk = a[q * n + k];
                    a[p * n + k] = c * apk - s * aqk;
                    a[q * n + k] = s * apk + c * aqk;
                }
                /* V <- V J */
                for (int k = 0; k < n; k++) {
                    float vkp = v[k * n + p], vkq = v[k * n + q];
                    v[k * n + p] = c * vkp - s * vkq;
                    v[k * n + q] = s * vkp + c * vkq;
                }
            }
        }
    }

    int order[CSI_NGROUPS];
    for (int i = 0; i < n; i++) {
        eig[i]   = a[i * n + i];
        order[i] = i;
    }
    /* sort descending, carrying the column index for the eigenvector */
    for (int i = 1; i < n; i++) {
        float e = eig[i];
        int   o = order[i];
        int   j = i - 1;
        while (j >= 0 && eig[j] < e) {
            eig[j + 1]   = eig[j];
            order[j + 1] = order[j];
            j--;
        }
        eig[j + 1]   = e;
        order[j + 1] = o;
    }
    if (vec_top) {
        int col = order[0];
        for (int k = 0; k < n; k++) {
            vec_top[k] = v[k * n + col];
        }
    }
}

/* dsp_top_eig's early exit: the largest change of any component of the
 * max-scaled iterate in one step. Rounding alone moves a component by a
 * few 1e-7 per step (an 8-term dot product), so this sits just above that
 * floor: tighter would rarely exit early, looser would stop further from
 * the eigenvector when lambda_2 / lambda_1 is large. */
#define DSP_EIG_STEP_TOL 1e-6f

/* Largest |x[i]|, 0 for all-zero input. NaN entries are skipped (every
 * comparison with NaN is false); dsp_top_eig documents non-finite input as
 * meaningless, this only keeps it from misbehaving. */
static float dsp_max_abs(const float *x, int n)
{
    float m = 0.0f;
    for (int i = 0; i < n; i++) {
        float ax = fabsf(x[i]);
        if (ax > m) m = ax;
    }
    return m;
}

float dsp_top_eig(const float *a, int n, float *vec, int iters)
{
    float v[CSI_NGROUPS], w[CSI_NGROUPS], u[CSI_NGROUPS];

    if (n < 1) return 0.0f;
    if (n > CSI_NGROUPS) n = CSI_NGROUPS;
    if (iters < 0) iters = 0;

    /* kmax: the largest diagonal element. For a PSD matrix it is also the
     * largest |entry| (|a_ij|^2 <= a_ii a_jj), so a non-positive maximum
     * means the zero matrix. */
    int   kmax = 0;
    float dmax = a[0];
    for (int i = 1; i < n; i++) {
        if (a[i * n + i] > dmax) {
            dmax = a[i * n + i];
            kmax = i;
        }
    }
    if (!(dmax > 0.0f)) {
        for (int i = 0; i < n; i++) vec[i] = (i == 0) ? 1.0f : 0.0f;
        return 0.0f;
    }

    /* Start v = A s, s_k = sign((column k) . (column kmax)); see csi_dsp.h
     * for why. a is symmetric, so row k is column k and the loops can walk
     * rows. The sign is applied by adding or subtracting, not by
     * multiplying by +-1. */
    for (int i = 0; i < n; i++) v[i] = 0.0f;
    for (int k = 0; k < n; k++) {
        const float *rk = &a[k * n];
        const float *rm = &a[kmax * n];
        float d = 0.0f;
        for (int i = 0; i < n; i++) d += rk[i] * rm[i];
        if (d < 0.0f) {
            for (int i = 0; i < n; i++) v[i] -= rk[i];
        } else {
            for (int i = 0; i < n; i++) v[i] += rk[i];
        }
    }
    float m = dsp_max_abs(v, n);
    /* A s ~ 0 (s nearly in the null space): column kmax, A e_kmax, is a
     * safe start instead - its Rayleigh quotient is at least dmax. 1e-5 is
     * ~20x the rounding residue of an 8-term sum of entries <= dmax
     * (8 x 6e-8), i.e. "zero up to rounding". */
    if (!(m > 1e-5f * dmax)) {
        for (int i = 0; i < n; i++) v[i] = a[kmax * n + i];
        m = dsp_max_abs(v, n);
    }
    {
        float inv = 1.0f / m;
        for (int i = 0; i < n; i++) v[i] *= inv;
    }

    /* Power steps. w = A v is formed at the top of each pass, so when the
     * loop ends w belongs to the v that is returned and the Rayleigh
     * quotient below needs no extra product. On convergence the OLD v is
     * kept for the same reason: it differs from the new one by at most
     * DSP_EIG_STEP_TOL per component. */
    for (int it = 0; ; it++) {
        for (int i = 0; i < n; i++) {
            const float *ri = &a[i * n];
            float s = 0.0f;
            for (int j = 0; j < n; j++) s += ri[j] * v[j];
            w[i] = s;
        }
        if (it >= iters) break;
        m = dsp_max_abs(w, n);
        /* A v == 0: v is in the null space and 0 is its Rayleigh quotient.
         * Cannot happen for a PSD a in exact arithmetic (see the start);
         * kept so that rounding, or a matrix that is only nearly PSD like
         * count_est.c's deflated one, cannot divide by zero. */
        if (!(m > 0.0f)) break;
        float inv  = 1.0f / m;
        float step = 0.0f;
        for (int i = 0; i < n; i++) {
            u[i] = w[i] * inv;
            float d = fabsf(u[i] - v[i]);
            if (d > step) step = d;
        }
        if (step <= DSP_EIG_STEP_TOL) break;
        for (int i = 0; i < n; i++) v[i] = u[i];
    }

    /* Rayleigh quotient v'Av / v'v, then scale v to unit length. v'v is in
     * [1, n] (the largest component is 1), so neither overflows. */
    float vw = 0.0f, vv = 0.0f;
    for (int i = 0; i < n; i++) {
        vw += v[i] * w[i];
        vv += v[i] * v[i];
    }
    float inv = 1.0f / sqrtf(vv);
    for (int i = 0; i < n; i++) vec[i] = v[i] * inv;
    return vw / vv;
}
