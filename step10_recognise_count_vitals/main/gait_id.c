/*
 * gait_id.c - see gait_id.h. EXPERIMENTAL: these features separate the
 * synthetic walkers of test_gait.c, which were built to differ. Whether
 * they separate real people is only known after enrolling them on the
 * device, and the published ceiling (gait_id.h) still applies.
 *
 * No FPU on the C5: float only, no per-sample trigonometry (dsp_hann and
 * dsp_fft use rotation recurrences, and nothing here calls sinf/cosf/expf).
 * Per packet gait_push() copies 8 floats into the ring; all the maths runs
 * once per GAIT_HOP packets, in gait_features().
 *
 * RING ORDER
 *   head is the next write index, so once the ring is full the OLDEST
 *   sample sits at head. The covariance only needs sums, so it walks the
 *   ring in storage order; the principal-component signal x(t) is written
 *   out chronologically (oldest first) because the spectrum and the
 *   autocorrelation depend on time order.
 */

#include "gait_id.h"

#include <float.h>
#include <math.h>
#include <string.h>

/* Feature layout, exactly as the gait_id.h comment block defines it. */
#define GF_BAND0      0       /* [0..5]   band power fractions             */
#define GF_NBANDS     6
#define GF_CENTROID   6       /* [6]      spectral centroid, Hz            */
#define GF_CADENCE    7       /* [7]      cadence, Hz                      */
#define GF_ACORR      8       /* [8]      autocorrelation at that lag / r0 */
#define GF_LOGVAR     9       /* [9]      log10(var(x) + 1e-6)             */
#define GF_SHARE0     10      /* [10..17] per-group share of the variance  */

/* Lower band edges, Hz. Band b is [lo[b], lo[b+1]); the last one is
 * [16, fs/2]. The spectrum is only summed from GAIT_F_LO_HZ up: below
 * 0.5 Hz the group signals are already cut by dsp_group_dev's ~0.3 Hz
 * high-pass, and what is left there is drift, not gait. */
static const float k_band_lo[GF_NBANDS] = { 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f };
#define GAIT_F_LO_HZ      0.5f

/* Cadence lag range, seconds: 0.4 s .. 1.4 s is a step or a stride at
 * roughly 0.7-2.5 Hz, which covers slow to brisk walking. */
#define GAIT_LAG_MIN_S    0.4f
#define GAIT_LAG_MAX_S    1.4f

/* Above this packet rate the cadence autocorrelation runs on a 2:1
 * pair-averaged copy of x. A 0.7-2.5 Hz rhythm needs nowhere near 100
 * samples a second, and the direct autocorrelation costs (samples) x
 * (lags): halving both cuts it ~4x, from ~32k to ~9k soft-float
 * operations at 100 Hz (counted, see gait_features). 60 leaves the 50 Hz
 * default (and anything up to 60) on the full-rate signal: halving 50 Hz
 * would leave a 25 Hz signal, a lag grid of 40 ms and the band edge at
 * 12.5 Hz, which is coarser than wanted. */
#define GAIT_DECIM_ABOVE_HZ 60.0f

/* Power-iteration cap for the first principal component. The angle to the
 * true eigenvector shrinks by lambda_2 / lambda_1 per step; a window with
 * one walker has a small ratio and exits early (csi_dsp.c,
 * DSP_EIG_STEP_TOL) after a handful of steps - on test_gait.c's walkers
 * the features match a full Jacobi to float rounding. At a ratio of 0.7,
 * 30 steps still shrink the angle's tangent by a factor 2e-5 (0.7^30);
 * closer to 1 the two leading directions are nearly tied and a mix of
 * them is as good an answer as either (csi_dsp.h). The cap bounds the
 * cost at ~5k operations. */
#define GAIT_EIG_ITERS    30

/* gait_set_rate() fallbacks. 50 Hz is the default ping rate (20 ms). The
 * upper cap is far beyond any ping loop; it only keeps rate * 1.4 s well
 * inside int range for the lag arithmetic. */
#define GAIT_RATE_DEFAULT 50.0f
#define GAIT_RATE_MAX     10000.0f

/* Portable finiteness test, same as csi_ncc.c: false for NaN and +/-inf.
 * Relies on IEEE comparisons (no -ffast-math, which neither the ESP-IDF
 * build nor run_tests.ps1 uses). */
static bool gait_finite(float v)
{
    return (v == v) && fabsf(v) <= FLT_MAX;
}

void gait_set_rate(gait_t *g, float rate_hz)
{
    /* Every frequency feature is scaled by the rate, so a NaN here would
     * make every window NaN; treat it like the header's "<= 0" case. */
    if (!(rate_hz > 0.0f) || rate_hz > GAIT_RATE_MAX) {
        rate_hz = GAIT_RATE_DEFAULT;
    }
    g->rate_hz = rate_hz;
}

void gait_init(gait_t *g, float rate_hz)
{
    memset(g, 0, sizeof(*g));
    gait_set_rate(g, rate_hz);
}

void gait_reset(gait_t *g)
{
    /* Everything but the rate; re-validated in case g was never
     * initialised (a zeroed static has rate 0). */
    float rate = g->rate_hz;
    memset(g, 0, sizeof(*g));
    gait_set_rate(g, rate);
}

/*
 * One window's features. Runs once per GAIT_HOP packets. Soft-float
 * operations per window - every add, multiply, divide, compare and
 * conversion that is a libgcc call on the C5 - COUNTED on the host by
 * compiling this file with an instrumented float type over test_gait.c's
 * walkers, NOT timed on the C5:
 *   covariance                         ~22.6k
 *   top eigenvector (dsp_top_eig)       ~0.8k (one walker: exits early;
 *                                        ~5.3k when it runs all
 *                                        GAIT_EIG_ITERS, e.g. on noise)
 *   projection                          ~6.1k
 *   FFT                                ~11.8k
 *   autocorrelation                     ~8.7k at 100 Hz (pair-averaged),
 *                                       ~23k at 50 Hz (full rate)
 *   detrend, Hann, spectrum, variance   ~8k
 *   total                              ~58k at 100 Hz, ~73k at 50 Hz;
 *                                       worst seen ~63k / ~77k (noise
 *                                       windows, eigen step at its cap)
 * The previous version (8x8 Jacobi ~21k, full-rate autocorrelation ~32k
 * at 100 Hz) counted ~102k at 100 Hz and ~93k at 50 Hz on the same data,
 * worst ~108k. At 50-100 cycles an operation that is now roughly 12-26 ms
 * at 240 MHz at 100 Hz and 15-32 ms at 50 Hz - an estimate - stalling the
 * pipeline once per GAIT_HOP packets, which the packet queue must absorb.
 * What is left is mostly the covariance and the FFT, both already O(n).
 * Stack (GCC -fstack-usage): 560 bytes here plus 192 inside dsp_top_eig;
 * the 3 KB of large work arrays (x, re, im) live in gait_t, which the
 * caller keeps static.
 *
 * Returns false (feat untouched) when the window has no variance at all, or
 * when anything came out non-finite - e.g. a NaN group sample still inside
 * the window. ncc_add() would reject such a vector anyway; not emitting it
 * keeps the enrolment progress count honest.
 */
static bool gait_features(gait_t *g, float feat[GAIT_NFEAT])
{
    const int   n    = GAIT_NWIN;
    const float rate = g->rate_hz;
    float cov[CSI_NGROUPS * CSI_NGROUPS];
    float mean[CSI_NGROUPS];
    float diag[CSI_NGROUPS];
    float v[CSI_NGROUPS];
    float f[GAIT_NFEAT];

    /* ---- 1. 8x8 covariance, two-pass: means first, then products of the
     * deviations. The ring is only 256 long, so the second pass is cheap,
     * and unlike a one-pass sum-of-products it does not lose digits to
     * cancellation when a group's mean is large next to its spread. */
    for (int j = 0; j < CSI_NGROUPS; j++) {
        float s = 0.0f;
        for (int t = 0; t < n; t++) s += g->ring[j][t];
        mean[j] = s / (float)n;
    }
    memset(cov, 0, sizeof(cov));
    for (int t = 0; t < n; t++) {
        float d[CSI_NGROUPS];
        for (int j = 0; j < CSI_NGROUPS; j++) d[j] = g->ring[j][t] - mean[j];
        for (int i = 0; i < CSI_NGROUPS; i++) {
            float  di  = d[i];
            float *row = &cov[i * CSI_NGROUPS];
            for (int j = i; j < CSI_NGROUPS; j++) row[j] += di * d[j];
        }
    }
    const float inv_nm1 = 1.0f / (float)(n - 1);
    float trace = 0.0f;
    for (int i = 0; i < CSI_NGROUPS; i++) {
        for (int j = i; j < CSI_NGROUPS; j++) {
            float c = cov[i * CSI_NGROUPS + j] * inv_nm1;
            cov[i * CSI_NGROUPS + j] = c;
            cov[j * CSI_NGROUPS + i] = c;
        }
        diag[i] = cov[i * CSI_NGROUPS + i];   /* a sum of squares: >= 0 */
        trace  += diag[i];
    }
    /* All-zero or constant input has no principal direction; NaN or inf
     * input fails the same test. Either way: no window, no NaN. */
    if (!(trace > 0.0f) || trace > FLT_MAX) {
        return false;
    }

    /* ---- 2. First principal component, by power iteration: only the top
     * eigenvector is needed. A full Jacobi (dsp_sym_eig) counted ~21k
     * operations here; this is ~0.8k on a walking window and at most ~5.3k
     * (the GAIT_EIG_ITERS cap). If the top two eigenvalues nearly tie, v
     * is a mix of the two leading directions (csi_dsp.h explains why that
     * is harmless here).
     * The sign of v is arbitrary; every feature below is invariant to it
     * (power spectrum, autocorrelation, variance). */
    (void)dsp_top_eig(cov, CSI_NGROUPS, v, GAIT_EIG_ITERS);

    /* x(t) = v . (g(t) - mean), oldest sample first. */
    int idx = g->head;
    for (int t = 0; t < n; t++) {
        float s = 0.0f;
        for (int j = 0; j < CSI_NGROUPS; j++) {
            s += v[j] * (g->ring[j][idx] - mean[j]);
        }
        g->x[t] = s;
        if (++idx == n) idx = 0;
    }

    /* [9] variance of x over the window, before detrending. Mathematically
     * this is v' C v = the top eigenvalue; it is computed from x itself so
     * it is exactly the variance of the signal the other features see.
     * x has zero mean up to rounding; the mean is still removed. */
    {
        float xm = 0.0f, var = 0.0f;
        for (int t = 0; t < n; t++) xm += g->x[t];
        xm /= (float)n;
        for (int t = 0; t < n; t++) {
            float d = g->x[t] - xm;
            var += d * d;
        }
        var *= inv_nm1;
        f[GF_LOGVAR] = log10f(var + 1e-6f);
    }

    /* Both the spectrum and the cadence use the detrended x: detrend once,
     * in place. */
    dsp_detrend(g->x, n);

    /* ---- 3. Power spectrum: Hann window, n-point FFT, bins 0..n/2.
     * n is a power of two, so dsp_fft cannot refuse it. */
    for (int t = 0; t < n; t++) {
        g->re[t] = g->x[t];
        g->im[t] = 0.0f;
    }
    dsp_hann(g->re, n);
    (void)dsp_fft(g->re, g->im, n);

    {
        float band[GF_NBANDS];
        float ptot = 0.0f, pf = 0.0f;
        const float df = rate / (float)n;     /* bin width, Hz */
        for (int b = 0; b < GF_NBANDS; b++) band[b] = 0.0f;
        /* k <= n/2 keeps f <= fs/2 exactly (n is a power of two, so
         * k * (rate / n) at k = n/2 is rate / 2 with no rounding), which
         * is why a band above fs/2 simply stays 0. */
        for (int k = 0; k <= n / 2; k++) {
            float fk = (float)k * df;
            if (fk < GAIT_F_LO_HZ) continue;
            float p = g->re[k] * g->re[k] + g->im[k] * g->im[k];
            int   b = GF_NBANDS - 1;
            while (b > 0 && fk < k_band_lo[b]) b--;
            band[b] += p;
            ptot    += p;
            pf      += p * fk;
        }
        for (int b = 0; b < GF_NBANDS; b++) {
            f[GF_BAND0 + b] = (ptot > 0.0f) ? band[b] / ptot : 0.0f;
        }
        f[GF_CENTROID] = (ptot > 0.0f) ? pf / ptot : 0.0f;
    }

    /* ---- 4. Cadence from the linear (biased) autocorrelation of the
     * detrended, UN-windowed x, computed directly for the lags that matter.
     *
     * Biased means r[L] = sum over the n - L overlapping samples, with no
     * 1/(n - L) correction. For a periodic x that tapers each later peak by
     * (n - L) / n, so the first period wins over its multiples and a clean
     * step rhythm reports the step rate, not half of it. A walk whose left
     * and right steps differ has its strongest repeat at the stride period,
     * and then the stride rate wins - either is a property of the walker,
     * which is what an identity feature needs. test_gait.c shows both.
     *
     * Above GAIT_DECIM_ABOVE_HZ it runs on y[k] = (x[2k] + x[2k+1]) / 2,
     * n/2 samples at rate/2, and every lag below is in those samples. Why
     * that is safe: the pair average is a gentle low-pass (gain
     * cos(pi f / rate): at 100 Hz 0.95 at 10 Hz, 0.71 at rate/4, 0 at
     * rate/2), so what folds back past the new Nyquist rate/4 is already
     * attenuated, and the 0.4-1.4 s lags respond to the 0.7-2.5 Hz rhythm,
     * which the average passes with a gain above 0.99 at any rate above
     * 60 Hz. The lag grid becomes 2/rate (20 ms at
     * 100 Hz, the same grid a 50 Hz ping rate always had); the parabolic
     * refinement below interpolates between its points. The biased taper is
     * the same in seconds ((n/2 - L/2) / (n/2) = (n - L) / n), and the
     * longest lag is still half the window. A side effect worth knowing:
     * averaging halves white noise, so r0 loses more noise than the lagged
     * sums do, and for a noisy x [8] can read higher than it would on the
     * full-rate x. Enrolment and recognition run at the same rate, so both
     * see the same definition.
     *
     * y lives in im[] and r[] in re[] (the spectrum is finished with
     * both); r[] is indexed by lag, for lags lo-1 .. hi+1 so every
     * candidate has both neighbours: lo >= 2 keeps lo-1 >= 1, hi <= ny/2
     * keeps hi+1 <= ny/2+1 < n. */
    {
        float cadence = 0.0f, acn = 0.0f;
        const float *y  = g->x;    /* the signal the autocorrelation sees */
        int          ny = n;
        float        ry = rate;    /* and its sample rate                 */
        if (rate > GAIT_DECIM_ABOVE_HZ) {
            ny = n / 2;
            for (int k = 0; k < ny; k++) {
                g->im[k] = 0.5f * (g->x[2 * k] + g->x[2 * k + 1]);
            }
            y  = g->im;
            ry = 0.5f * rate;
        }
        /* The 1e-3 guards the edges against float rounding of
         * ry * 0.4 s (e.g. 40.000004 must still give lag 40). */
        int lo = (int)ceilf(GAIT_LAG_MIN_S * ry - 1e-3f);
        int hi = (int)floorf(GAIT_LAG_MAX_S * ry + 1e-3f);
        if (lo < 2)      lo = 2;
        if (hi > ny / 2) hi = ny / 2;

        float r0 = 0.0f;
        for (int t = 0; t < ny; t++) r0 += y[t] * y[t];

        if (lo <= hi && r0 > 0.0f) {
            float *r = g->re;
            for (int lag = lo - 1; lag <= hi + 1; lag++) {
                float s = 0.0f;
                for (int t = 0; t < ny - lag; t++) s += y[t] * y[t + lag];
                r[lag] = s;
            }
            int best = -1;
            for (int l = lo; l <= hi; l++) {
                if (r[l] > r[l - 1] && r[l] >= r[l + 1] &&
                    (best < 0 || r[l] > r[best])) {
                    best = l;
                }
            }
            if (best > 0) {
                /* Parabolic refinement: on a 50 Hz lag grid (50 Hz pings,
                 * or 100 Hz pair-averaged) one lag step near 1.8 Hz is ~4%
                 * of the cadence; this makes the feature continuous
                 * instead of quantised. The offset is in [-0.5, 0.5] and
                 * best >= 2, so the lag stays >= 1.5. */
                float lag = (float)best +
                            dsp_parabolic_offset(r[best - 1], r[best], r[best + 1]);
                cadence = ry / lag;
                acn     = r[best] / r0;
                /* |r[L]| <= r0 by Cauchy-Schwarz; rounding can step past */
                if (acn > 1.0f)  acn = 1.0f;
                if (acn < -1.0f) acn = -1.0f;
            }
        }
        f[GF_CADENCE] = cadence;
        f[GF_ACORR]   = acn;
    }

    /* ---- 5. [10..17] where the variance is: each group's share. */
    for (int j = 0; j < CSI_NGROUPS; j++) {
        f[GF_SHARE0 + j] = diag[j] / trace;
    }

    /* Absurdly large inputs can still overflow the power sums; never hand
     * out a non-finite feature. */
    for (int i = 0; i < GAIT_NFEAT; i++) {
        if (!gait_finite(f[i])) return false;
    }
    memcpy(feat, f, sizeof(f));
    return true;
}

bool gait_push(gait_t *g, const float grp[CSI_NGROUPS], bool moving,
               float feat[GAIT_NFEAT])
{
    for (int j = 0; j < CSI_NGROUPS; j++) {
        g->ring[j][g->head] = grp[j];
    }
    g->moving[g->head] = (uint8_t)(moving ? 1 : 0);
    if (++g->head == GAIT_NWIN) g->head = 0;
    if (g->count < GAIT_NWIN) g->count++;
    g->since_window++;

    /* The first window needs a full ring (GAIT_NWIN packets after a reset);
     * after that one window every GAIT_HOP packets. */
    if (g->count < GAIT_NWIN || g->since_window < GAIT_HOP) {
        return false;
    }
    /* The hop restarts whether or not this window qualifies, so windows
     * stay on a fixed GAIT_HOP grid. */
    g->since_window = 0;

    /* Gait features of a mostly still window describe breathing and noise,
     * not a walk. Summing 256 bytes once per hop is cheaper than keeping a
     * running count per packet. */
    int n_moving = 0;
    for (int t = 0; t < GAIT_NWIN; t++) n_moving += g->moving[t];
    if ((float)n_moving < GAIT_MIN_MOVING * (float)GAIT_NWIN) {
        return false;
    }
    return gait_features(g, feat);
}
