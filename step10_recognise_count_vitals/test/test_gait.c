/*
 * test_gait.c - host tests for gait_id.c:
 *   [1] a clean periodic "walk": the feature definitions themselves
 *       (cadence, band fractions, centroid, variance, group shares);
 *   [2] window timing and the >= GAIT_MIN_MOVING gate;
 *   [3] three synthetic walkers: are their windows separable?
 *   [4] degenerate input (zeros, a constant, a NaN sample): no window, no NaN;
 *   [5] packet rates other than 100 Hz: bands above fs/2, lag ranges.
 *
 * Links gait_id.c + csi_dsp.c only (run_tests.ps1). The classifier in [3]
 * is a few lines here - the same pooled-std RMS distance csi_ncc.c uses -
 * so this test does not depend on csi_ncc.c.
 */

#include <float.h>
#include <string.h>
#include "test_util.h"
#include "gait_id.h"

/* Feature layout, from the gait_id.h comment block. */
#define F_BAND0     0
#define F_NBANDS    6
#define F_CENTROID  6
#define F_CADENCE   7
#define F_ACORR     8
#define F_LOGVAR    9
#define F_SHARE0    10

#define TWO_PI      (2.0f * CSI_DSP_PI)

/* ~11.5 KB: static, as main.c must keep it. */
static gait_t s_g;

static int all_finite(const float *f, int n)
{
    for (int i = 0; i < n; i++) {
        if (!(f[i] == f[i]) || fabsf(f[i]) > FLT_MAX) return 0;
    }
    return 1;
}

static float band_sum(const float *f)
{
    float s = 0.0f;
    for (int b = 0; b < F_NBANDS; b++) s += f[F_BAND0 + b];
    return s;
}

static float share_sum(const float *f)
{
    float s = 0.0f;
    for (int j = 0; j < CSI_NGROUPS; j++) s += f[F_SHARE0 + j];
    return s;
}

static void print_feat(const char *what, const float *f)
{
    printf("    %s\n", what);
    printf("      bands [.5,1) [1,2) [2,4) [4,8) [8,16) [16,fs/2]: "
           "%.3f %.3f %.3f %.3f %.3f %.3f\n",
           (double)f[0], (double)f[1], (double)f[2], (double)f[3],
           (double)f[4], (double)f[5]);
    printf("      centroid %.3f Hz  cadence %.3f Hz  acorr %.3f  log10(var) %.3f\n",
           (double)f[F_CENTROID], (double)f[F_CADENCE], (double)f[F_ACORR],
           (double)f[F_LOGVAR]);
    printf("      shares");
    for (int j = 0; j < CSI_NGROUPS; j++) printf(" %.3f", (double)f[F_SHARE0 + j]);
    printf("\n");
}

/* One packet of independent unit noise in every group. */
static void noise_packet(float grp[CSI_NGROUPS])
{
    for (int j = 0; j < CSI_NGROUPS; j++) grp[j] = rng_gauss();
}

typedef struct {
    float f_hz;
    float amp;
} tone_t;

/* Push npk moving packets of grp_j = mix_j * sum(tones) + noise * N(0,1)
 * into s_g (not reset here). Returns the number of windows; the last
 * window's features go to feat_last and its 1-based push index to
 * *last_at. */
static int feed_tones(float rate, const tone_t *tones, int ntones,
                      const float mix[CSI_NGROUPS], float noise, int npk,
                      float feat_last[GAIT_NFEAT], int *last_at)
{
    float grp[CSI_NGROUPS], feat[GAIT_NFEAT];
    int   windows = 0;
    for (int i = 0; i < npk; i++) {
        float t = (float)i / rate;
        float s = 0.0f;
        for (int k = 0; k < ntones; k++) {
            s += tones[k].amp * sinf(TWO_PI * tones[k].f_hz * t);
        }
        for (int j = 0; j < CSI_NGROUPS; j++) grp[j] = mix[j] * s + noise * rng_gauss();
        if (gait_push(&s_g, grp, true, feat)) {
            windows++;
            memcpy(feat_last, feat, sizeof(feat));
            if (last_at) *last_at = i + 1;
        }
    }
    return windows;
}

static const float k_mix[CSI_NGROUPS] = { 1.0f, 0.8f, 0.6f, 0.4f, -0.3f, -0.5f, 0.2f, 0.1f };

/* cadence of the clean 1.8 Hz window at 100 Hz, compared against 50 Hz in [5] */
static float s_cadence_100 = 0.0f;

/* ------------------------------------------------------------------ [1] */
static void test_clean_periodic(void)
{
    float f[GAIT_NFEAT];
    int   at = -1;

    printf("[1] clean periodic signal at 100 Hz\n");

    /*
     * Every group carries 3 * mix_j * sin(2 pi 1.8 t) plus 0.05 noise.
     *
     * Which cadence should come out - the 1.8 Hz step rate or the 0.9 Hz
     * stride sub-harmonic? The autocorrelation of a pure 1.8 Hz sine peaks
     * at lags 0.556 s AND 1.111 s (both inside [0.4 s, 1.4 s]). The header
     * specifies the BIASED autocorrelation (sum over the overlap, no
     * 1/(N-lag)), which tapers each peak by (N - lag) / N: at 100 Hz
     * (256 - 56) / 256 = 0.78 versus (256 - 111) / 256 = 0.57 - the same
     * on the 2:1 pair average gait_id.c actually uses at 100 Hz,
     * (128 - 28) / 128 and (128 - 55.5) / 128. So the first period wins
     * and the implementation must report the STEP rate,
     * 1.8 Hz, with acorr ~0.78. The second case below shows the other
     * branch: when alternate steps differ, the stride repeats more strongly
     * and 0.9 Hz is reported instead.
     */
    const tone_t step = { 1.8f, 3.0f };
    gait_init(&s_g, 100.0f);
    rng_seed(11);
    int w = feed_tones(100.0f, &step, 1, k_mix, 0.05f, GAIT_NWIN, f, &at);
    CHECK(w == 1 && at == GAIT_NWIN, "windows %d, first at push %d (want 1 at %d)",
          w, at, GAIT_NWIN);
    print_feat("1.8 Hz sine, 256 packets:", f);
    s_cadence_100 = f[F_CADENCE];

    CHECK(all_finite(f, GAIT_NFEAT), "non-finite feature");
    CHECK(f[F_CADENCE] >= 1.7f && f[F_CADENCE] <= 1.9f,
          "cadence %.3f Hz, want the 1.8 Hz step rate", (double)f[F_CADENCE]);
    CHECK(f[F_ACORR] > 0.65f && f[F_ACORR] <= 0.9f,
          "acorr %.3f, want ~0.78 (biased taper at lag 56)", (double)f[F_ACORR]);
    CHECK_NEAR(band_sum(f), 1.0, 1e-4, "band fractions sum");
    CHECK_NEAR(share_sum(f), 1.0, 1e-4, "group shares sum");
    CHECK(f[F_BAND0 + 1] > 0.8f, "[1,2) Hz band should hold the 1.8 Hz tone: %.3f",
          (double)f[F_BAND0 + 1]);
    CHECK(f[F_BAND0 + 5] < 0.01f, "[16,50] Hz band %.4f, only noise lives there",
          (double)f[F_BAND0 + 5]);
    CHECK(f[F_CENTROID] > 1.6f && f[F_CENTROID] < 2.1f, "centroid %.3f Hz",
          (double)f[F_CENTROID]);

    /* var(x) = 3^2 * |mix|^2 / 2 = 9 * 2.55 / 2 = 11.475 (the window holds
     * 4.6 periods, not a whole number, which moves it by ~2%) */
    float mix2 = 0.0f;
    for (int j = 0; j < CSI_NGROUPS; j++) mix2 += k_mix[j] * k_mix[j];
    CHECK_NEAR(f[F_LOGVAR], log10f(4.5f * mix2), 0.02, "log10 variance of x");
    /* each group's share of the variance is mix_j^2 / |mix|^2 */
    for (int j = 0; j < CSI_NGROUPS; j++) {
        CHECK_NEAR(f[F_SHARE0 + j], k_mix[j] * k_mix[j] / mix2, 0.01, "group share");
    }

    /* Alternate steps differ: 0.9 Hz stride fundamental plus its 1.8 Hz
     * harmonic. Near lag 0.556 s the 0.9 Hz term is at its minimum and
     * outweighs the harmonic, so there is no local maximum there; the
     * stride lag 1.111 s is the only one, and 0.9 Hz is reported. */
    const tone_t stride[2] = { { 0.9f, 3.0f }, { 1.8f, 1.0f } };
    gait_reset(&s_g);
    w = feed_tones(100.0f, stride, 2, k_mix, 0.05f, GAIT_NWIN, f, &at);
    CHECK(w == 1, "stride case: %d windows", w);
    printf("    asymmetric steps (0.9 Hz stride + 1.8 Hz): cadence %.3f Hz, acorr %.3f\n",
           (double)f[F_CADENCE], (double)f[F_ACORR]);
    CHECK(f[F_CADENCE] >= 0.85f && f[F_CADENCE] <= 0.95f,
          "stride case cadence %.3f Hz, want 0.9", (double)f[F_CADENCE]);
    CHECK(all_finite(f, GAIT_NFEAT), "non-finite feature (stride case)");
}

/* ------------------------------------------------------------------ [2] */

/* Reset, then push n_total noise packets; the first n_still are still
 * (moving == false), the rest moving until push index still_from, still
 * again after it (still_from <= 0: moving to the end). Records the 1-based
 * push indices of the windows gait_push() reported. */
static int gate_run(int n_still, int still_from, int n_total, int *at, int max_at)
{
    float grp[CSI_NGROUPS], feat[GAIT_NFEAT];
    int   n = 0;
    gait_reset(&s_g);
    for (int i = 1; i <= n_total; i++) {
        bool moving = i > n_still && (still_from <= 0 || i <= still_from);
        noise_packet(grp);
        if (gait_push(&s_g, grp, moving, feat)) {
            if (n < max_at) at[n] = i;
            n++;
        }
    }
    return n;
}

static void print_at(const char *what, const int *at, int n)
{
    printf("    %-44s windows at", what);
    if (n == 0) printf(" (none)");
    for (int i = 0; i < n && i < 8; i++) printf(" %d", at[i]);
    printf("\n");
}

static void test_window_timing(void)
{
    int at[8], n;

    printf("[2] window timing and the moving gate (GAIT_NWIN %d, GAIT_HOP %d, >= %.0f%% moving)\n",
           GAIT_NWIN, GAIT_HOP, (double)(GAIT_MIN_MOVING * 100.0f));
    gait_init(&s_g, 100.0f);
    rng_seed(21);

    n = gate_run(0, 0, 700, at, 8);
    print_at("all moving, 700 packets:", at, n);
    CHECK(n == 4 && at[0] == 256 && at[1] == 384 && at[2] == 512 && at[3] == 640,
          "want windows at 256 384 512 640, got %d", n);

    /* 205 / 256 = 0.801 qualifies, 204 / 256 = 0.797 does not */
    n = gate_run(51, 0, 400, at, 8);
    print_at("51 still then moving (205/256 moving):", at, n);
    CHECK(n == 2 && at[0] == 256 && at[1] == 384, "205/256 moving should qualify (n %d)", n);

    /* The rejected window still restarts the hop: the next one is 128
     * packets later (all moving by then), not the very next packet. */
    n = gate_run(52, 0, 400, at, 8);
    print_at("52 still then moving (204/256 moving):", at, n);
    CHECK(n == 1 && at[0] == 384, "204/256 must be rejected, next window at 384 (n %d, at %d)",
          n, n ? at[0] : -1);

    /* Moving until 300, then still: the window at 384 has only 172/256
     * moving, and every later one fewer. */
    n = gate_run(0, 300, 800, at, 8);
    print_at("moving to 300, then still:", at, n);
    CHECK(n == 1 && at[0] == 256, "only the 256 window should qualify (n %d)", n);

    /* gait_reset: the next window needs a full ring again; rate is kept */
    {
        float grp[CSI_NGROUPS], feat[GAIT_NFEAT];
        int   first = -1;
        for (int i = 0; i < 300; i++) {
            noise_packet(grp);
            (void)gait_push(&s_g, grp, true, feat);
        }
        gait_reset(&s_g);
        CHECK(s_g.rate_hz == 100.0f, "gait_reset lost the rate (%g)", (double)s_g.rate_hz);
        for (int i = 1; i <= 300 && first < 0; i++) {
            noise_packet(grp);
            if (gait_push(&s_g, grp, true, feat)) first = i;
        }
        printf("    after gait_reset: first window at push %d\n", first);
        CHECK(first == GAIT_NWIN, "first window after reset at %d, want %d", first, GAIT_NWIN);
    }
}

/* ------------------------------------------------------------------ [3] */
/*
 * Three synthetic walkers. WHAT THIS PROVES AND WHAT IT DOES NOT: each
 * walker was deliberately built with a different torso fluctuation
 * frequency, a different step cadence and a different set of perturbed
 * subcarrier groups. A classifier telling them apart only shows that the
 * 18 features CARRY the information that was put into this synthetic
 * data, and that nothing in gait_id.c destroys it. It says nothing about
 * whether real people differ in these ways, by how much, or whether one
 * ESP32 link can see it - the published figures (~95% for 2 people, ~74%
 * for 3, setup unverified, gait_id.h) are the realistic ceiling, and only
 * an on-device enrolment test can say more.
 *
 * Model of one walk, per packet:
 *   torso(t) = narrowband noise at f_torso (AR(2) resonator, pole radius
 *              0.95 -> ~1.6 Hz wide at 100 Hz, unit variance);
 *   step(t)  = sin(phase), phase advancing at f_step with a random walk
 *              (step-to-step variability);
 *   body(t)  = (1 + 0.6 step) * torso + 0.7 step;
 *   grp_j    = amp * mix_j * body + 0.3 N(0,1).
 * The 0.7 step term is the whole-body sway at the step rate. Without it
 * the step rhythm would exist only as the ENVELOPE of the torso noise
 * (the 0.6 step modulation), which an autocorrelation of x itself - the
 * header's cadence definition - cannot see.
 * Each enrolment and each test run is a separate "walk": speed, strength
 * and path (f_torso, f_step, amp, mix) are re-drawn around the walker's
 * nominal values, so the test windows are held out in the sense that
 * matters - a different session, not just different packets.
 */
#define N_WALKERS  3
#define N_ENROL    25
#define N_TEST     15

typedef struct {
    const char *name;
    float       f_torso;               /* torso fluctuation centre, Hz */
    float       f_step;                /* step cadence, Hz             */
    float       mix[CSI_NGROUPS];      /* which groups the body moves  */
} walker_t;

static const walker_t k_walkers[N_WALKERS] = {
    { "A",  4.0f, 1.6f, { 1.0f, 0.9f, 0.7f, 0.5f, 0.3f, 0.2f, 0.1f, 0.1f } },
    { "B",  7.0f, 1.9f, { 0.2f, 0.4f, 0.8f, 1.0f, 0.9f, 0.5f, 0.2f, 0.1f } },
    { "C", 11.0f, 2.2f, { 0.1f, 0.1f, 0.2f, 0.3f, 0.5f, 0.8f, 1.0f, 0.9f } },
};

typedef struct {
    float amp, f_step;
    float mix[CSI_NGROUPS];
    float phi1, phi2, e_gain;          /* AR(2) torso resonator        */
    float y1, y2, phase;
} session_t;

static void session_start(session_t *s, const walker_t *w, float rate)
{
    float ft = w->f_torso * (1.0f + 0.05f * rng_gauss());
    s->f_step = w->f_step * (1.0f + 0.03f * rng_gauss());
    s->amp    = 2.0f * (1.0f + 0.2f * rng_gauss());
    if (s->amp < 0.5f) s->amp = 0.5f;
    for (int j = 0; j < CSI_NGROUPS; j++) {
        s->mix[j] = w->mix[j] * (1.0f + 0.2f * rng_gauss());
    }
    /* y = phi1 y1 + phi2 y2 + e; poles at 0.95 exp(+-j 2 pi ft / rate).
     * Stationary variance (1 - phi2) / ((1 + phi2)((1 - phi2)^2 - phi1^2))
     * sets e_gain for unit output variance. */
    float rr = 0.95f, wc = TWO_PI * ft / rate;
    s->phi1 = 2.0f * rr * cosf(wc);
    s->phi2 = -rr * rr;
    float var = (1.0f - s->phi2) /
                ((1.0f + s->phi2) * ((1.0f - s->phi2) * (1.0f - s->phi2) - s->phi1 * s->phi1));
    s->e_gain = 1.0f / sqrtf(var);
    s->y1 = 0.0f;
    s->y2 = 0.0f;
    s->phase = TWO_PI * rng_uniform();
}

static void session_sample(session_t *s, float rate, float grp[CSI_NGROUPS])
{
    float y = s->phi1 * s->y1 + s->phi2 * s->y2 + s->e_gain * rng_gauss();
    s->y2 = s->y1;
    s->y1 = y;
    s->phase += TWO_PI * s->f_step / rate + 0.03f * rng_gauss();
    if (s->phase > TWO_PI) s->phase -= TWO_PI;
    float st   = sinf(s->phase);
    float body = (1.0f + 0.6f * st) * y + 0.7f * st;
    for (int j = 0; j < CSI_NGROUPS; j++) {
        grp[j] = s->amp * s->mix[j] * body + 0.3f * rng_gauss();
    }
}

/* One walk: reset the gait state and collect nwin windows. */
static int collect_walk(const walker_t *w, float rate, int nwin, float (*out)[GAIT_NFEAT])
{
    session_t s;
    float     grp[CSI_NGROUPS], feat[GAIT_NFEAT];
    int       got = 0;
    gait_reset(&s_g);
    session_start(&s, w, rate);
    for (int i = 0; got < nwin && i < 100000; i++) {
        session_sample(&s, rate, grp);
        if (gait_push(&s_g, grp, true, feat)) {
            memcpy(out[got], feat, sizeof(feat));
            got++;
        }
    }
    return got;
}

static float s_enrol[N_WALKERS][N_ENROL][GAIT_NFEAT];
static float s_test[N_WALKERS][N_TEST][GAIT_NFEAT];

/* Pooled-std nearest centroid over features [f_lo, f_hi): per-feature
 * z-scores against the pooled within-class std (floored at 1e-4), RMS over
 * the features - the distance csi_ncc.c uses, no rejection. ADDS to
 * conf[true][predicted] (the caller zeroes it) and returns the accuracy. */
static float nc_eval(int f_lo, int f_hi, int conf[N_WALKERS][N_WALKERS])
{
    float mean[N_WALKERS][GAIT_NFEAT], sd[GAIT_NFEAT];
    int   correct = 0;

    for (int f = f_lo; f < f_hi; f++) {
        float ss = 0.0f;
        for (int c = 0; c < N_WALKERS; c++) {
            float m = 0.0f;
            for (int i = 0; i < N_ENROL; i++) m += s_enrol[c][i][f];
            m /= (float)N_ENROL;
            mean[c][f] = m;
            for (int i = 0; i < N_ENROL; i++) {
                float d = s_enrol[c][i][f] - m;
                ss += d * d;
            }
        }
        sd[f] = sqrtf(ss / (float)(N_WALKERS * (N_ENROL - 1)));
        if (sd[f] < 1e-4f) sd[f] = 1e-4f;
    }

    for (int c = 0; c < N_WALKERS; c++) {
        for (int i = 0; i < N_TEST; i++) {
            int   best  = -1;
            float bestd = FLT_MAX;
            for (int k = 0; k < N_WALKERS; k++) {
                float d2 = 0.0f;
                for (int f = f_lo; f < f_hi; f++) {
                    float z = (s_test[c][i][f] - mean[k][f]) / sd[f];
                    d2 += z * z;
                }
                float d = sqrtf(d2 / (float)(f_hi - f_lo));
                if (d < bestd) {
                    bestd = d;
                    best  = k;
                }
            }
            conf[c][best]++;
            if (best == c) correct++;
        }
    }
    return (float)correct / (float)(N_WALKERS * N_TEST);
}

static void print_conf(int conf[N_WALKERS][N_WALKERS])
{
    printf("      true\\pred");
    for (int k = 0; k < N_WALKERS; k++) printf("   %s", k_walkers[k].name);
    printf("\n");
    for (int c = 0; c < N_WALKERS; c++) {
        printf("      %-9s", k_walkers[c].name);
        for (int k = 0; k < N_WALKERS; k++) printf(" %3d", conf[c][k]);
        printf("\n");
    }
}

/* Seeds for the repeated experiment: one seed could be lucky, five
 * independent sets of walks (different session draws and noise) are
 * less likely to be. */
#define N_SEEDS    5

static void test_walkers(void)
{
    const float rate = 100.0f;
    int   conf[N_WALKERS][N_WALKERS], conf_s[N_WALKERS][N_WALKERS];
    int   conf_one[N_WALKERS][N_WALKERS];
    float acc_min = 1.0f, acc_sum = 0.0f, spec_sum = 0.0f, share_sum_acc = 0.0f;

    printf("[3] three synthetic walkers, %d enrolment + %d held-out windows each, %.0f Hz,"
           " %d seeds\n", N_ENROL, N_TEST, (double)rate, N_SEEDS);
    memset(conf, 0, sizeof(conf));
    gait_init(&s_g, rate);

    for (int seed = 0; seed < N_SEEDS; seed++) {
        rng_seed(31u + (uint32_t)seed);
        for (int c = 0; c < N_WALKERS; c++) {
            int ne = collect_walk(&k_walkers[c], rate, N_ENROL, s_enrol[c]);
            int nt = collect_walk(&k_walkers[c], rate, N_TEST, s_test[c]);
            CHECK(ne == N_ENROL && nt == N_TEST, "walker %s: %d/%d windows",
                  k_walkers[c].name, ne, nt);
            int finite = 1;
            for (int i = 0; i < N_ENROL; i++) finite &= all_finite(s_enrol[c][i], GAIT_NFEAT);
            for (int i = 0; i < N_TEST; i++)  finite &= all_finite(s_test[c][i], GAIT_NFEAT);
            CHECK(finite, "walker %s: non-finite feature", k_walkers[c].name);

            if (seed != 0) continue;
            /* Does the cadence feature recover the step rate that was
             * built in? Diagnostic only (printed for the first seed);
             * separability is what is asserted. */
            float cm = 0.0f, cs = 0.0f, ce = 0.0f;
            for (int i = 0; i < N_ENROL; i++) {
                cm += s_enrol[c][i][F_CADENCE];
                ce += s_enrol[c][i][F_CENTROID];
            }
            cm /= (float)N_ENROL;
            ce /= (float)N_ENROL;
            for (int i = 0; i < N_ENROL; i++) {
                float d = s_enrol[c][i][F_CADENCE] - cm;
                cs += d * d;
            }
            cs = sqrtf(cs / (float)(N_ENROL - 1));
            printf("    walker %s (torso %.0f Hz, step %.1f Hz): enrol cadence %.2f +/- %.2f Hz, "
                   "centroid %.2f Hz\n", k_walkers[c].name, (double)k_walkers[c].f_torso,
                   (double)k_walkers[c].f_step, (double)cm, (double)cs, (double)ce);
        }

        memset(conf_one, 0, sizeof(conf_one));
        float acc = nc_eval(0, GAIT_NFEAT, conf_one);
        for (int c = 0; c < N_WALKERS; c++) {
            for (int k = 0; k < N_WALKERS; k++) conf[c][k] += conf_one[c][k];
        }
        /* Which features carry it? Printed, not asserted: the split
         * depends entirely on how the synthetic walkers were built. */
        memset(conf_s, 0, sizeof(conf_s));
        float acc_spec  = nc_eval(0, F_SHARE0, conf_s);
        float acc_share = nc_eval(F_SHARE0, GAIT_NFEAT, conf_s);
        printf("    seed %d: all 18 features %5.1f%%   [0..9] spectral/cadence/variance %5.1f%%"
               "   [10..17] group shares %5.1f%%\n", seed, (double)(acc * 100.0f),
               (double)(acc_spec * 100.0f), (double)(acc_share * 100.0f));
        CHECK(acc >= 0.9f, "seed %d: accuracy %.1f%% < 90%%", seed, (double)(acc * 100.0f));
        if (acc < acc_min) acc_min = acc;
        acc_sum       += acc;
        spec_sum      += acc_spec;
        share_sum_acc += acc_share;
    }

    printf("    all 18 features: mean %.1f%%, worst seed %.1f%% (%d test windows per seed)\n",
           (double)(acc_sum * 100.0f / (float)N_SEEDS), (double)(acc_min * 100.0f),
           N_WALKERS * N_TEST);
    printf("    subsets, mean over seeds: [0..9] %.1f%%   [10..17] %.1f%%\n",
           (double)(spec_sum * 100.0f / (float)N_SEEDS),
           (double)(share_sum_acc * 100.0f / (float)N_SEEDS));
    printf("    confusion, all features, pooled over %d seeds:\n", N_SEEDS);
    print_conf(conf);
}

/* ------------------------------------------------------------------ [4] */
static void test_degenerate(void)
{
    float grp[CSI_NGROUPS], feat[GAIT_NFEAT];
    int   w;

    printf("[4] degenerate input\n");
    for (int i = 0; i < GAIT_NFEAT; i++) feat[i] = 0.0f;

    /* all zero: no variance, no principal direction -> no window */
    gait_init(&s_g, 100.0f);
    w = 0;
    for (int j = 0; j < CSI_NGROUPS; j++) grp[j] = 0.0f;
    for (int i = 0; i < 1024; i++) w += gait_push(&s_g, grp, true, feat) ? 1 : 0;
    printf("    all-zero input, 1024 moving packets: %d windows\n", w);
    CHECK(w == 0, "all-zero input produced %d windows", w);
    CHECK(all_finite(feat, GAIT_NFEAT), "feat written with non-finite values");

    /* constant per group (a DC offset, no fluctuation): same */
    gait_reset(&s_g);
    w = 0;
    for (int j = 0; j < CSI_NGROUPS; j++) grp[j] = 5.0f + (float)j;
    for (int i = 0; i < 1024; i++) w += gait_push(&s_g, grp, true, feat) ? 1 : 0;
    printf("    constant input, 1024 moving packets: %d windows\n", w);
    CHECK(w == 0, "constant input produced %d windows", w);
    CHECK(all_finite(feat, GAIT_NFEAT), "feat written with non-finite values");

    /* A pure ramp: variance > 0, so a window IS produced, but detrending
     * leaves only float rounding residue, so the spectral and cadence
     * values describe that residue and mean nothing. What is asserted is
     * that they are finite. Not a case the device meets: dsp_group_dev's
     * ~0.3 Hz high-pass cannot output a 2.56 s ramp. */
    gait_reset(&s_g);
    w = 0;
    for (int i = 0; i < GAIT_NWIN; i++) {
        for (int j = 0; j < CSI_NGROUPS; j++) grp[j] = 0.01f * (float)(i * (j + 1));
        if (gait_push(&s_g, grp, true, feat)) {
            w++;
            CHECK(all_finite(feat, GAIT_NFEAT), "ramp: non-finite feature");
            CHECK_NEAR(share_sum(feat), 1.0, 1e-4, "ramp: shares sum");
        }
    }
    CHECK(w == 1, "ramp: %d windows", w);
    printf("    pure ramp: %d window(s), cadence %.3f (rounding residue), log10(var) %.3f\n", w,
           (double)feat[F_CADENCE], (double)feat[F_LOGVAR]);

    /* One NaN sample (e.g. a corrupt inv_sigma): every window that
     * contains it is dropped, the first one after it has left the ring is
     * not, and nothing non-finite is ever returned. */
    {
        volatile float zero = 0.0f;
        const float    qnan = zero / zero;
        int at[8], n = 0, bad = 0;
        gait_reset(&s_g);
        rng_seed(41);
        for (int i = 1; i <= 700; i++) {
            noise_packet(grp);
            if (i == 300) grp[3] = qnan;
            if (gait_push(&s_g, grp, true, feat)) {
                if (n < 8) at[n] = i;
                n++;
                if (!all_finite(feat, GAIT_NFEAT)) bad++;
            }
        }
        print_at("NaN in group 3 at push 300:", at, n);
        CHECK(n == 2 && at[0] == 256 && at[1] == 640,
              "want windows at 256 and 640 only (n %d)", n);
        CHECK(bad == 0, "%d windows returned non-finite features", bad);
    }
}

/* ------------------------------------------------------------------ [5] */
static void test_rates(void)
{
    float f[GAIT_NFEAT];
    int   w, at = -1;

    printf("[5] other packet rates\n");

    /* 50 Hz: fs/2 = 25, so the last band is [16, 25]. A 20 Hz tone of
     * half the amplitude of the 1.8 Hz one holds 1.5^2 / (3^2 + 1.5^2) =
     * 20% of the power; nothing is read past bin 128 (25 Hz). */
    {
        const tone_t t2[2] = { { 1.8f, 3.0f }, { 20.0f, 1.5f } };
        gait_init(&s_g, 50.0f);
        rng_seed(51);
        w = feed_tones(50.0f, t2, 2, k_mix, 0.05f, GAIT_NWIN, f, &at);
        CHECK(w == 1 && at == GAIT_NWIN, "50 Hz: %d windows, at %d", w, at);
        print_feat("50 Hz: 1.8 Hz + 20 Hz tones:", f);
        CHECK(all_finite(f, GAIT_NFEAT), "50 Hz: non-finite feature");
        CHECK_NEAR(band_sum(f), 1.0, 1e-4, "50 Hz band fractions sum");
        CHECK_NEAR(f[F_BAND0 + 5], 0.2, 0.03, "50 Hz: [16,25] Hz band fraction");
        CHECK_NEAR(f[F_BAND0 + 1], 0.8, 0.1, "50 Hz: [1,2) Hz band fraction");
        /* No cadence assertion here: a strong, perfectly periodic 20 Hz
         * tone ripples the autocorrelation every 2.5 lags and can pull the
         * peak a lag or two - see the printed value. Real torso motion is
         * broadband and decorrelates well within the 0.4 s minimum lag. */
    }

    /* 50 Hz, 1.8 Hz alone: the cadence agrees with the 100 Hz one */
    {
        const tone_t t1 = { 1.8f, 3.0f };
        gait_reset(&s_g);
        w = feed_tones(50.0f, &t1, 1, k_mix, 0.05f, GAIT_NWIN, f, &at);
        printf("    50 Hz, 1.8 Hz alone: cadence %.3f Hz (100 Hz gave %.3f), acorr %.3f\n",
               (double)f[F_CADENCE], (double)s_cadence_100, (double)f[F_ACORR]);
        CHECK(w == 1, "50 Hz: %d windows", w);
        CHECK(f[F_CADENCE] >= 1.7f && f[F_CADENCE] <= 1.9f, "50 Hz cadence %.3f",
              (double)f[F_CADENCE]);
        CHECK(fabsf(f[F_CADENCE] - s_cadence_100) < 0.05f, "50 vs 100 Hz cadence %.3f vs %.3f",
              (double)f[F_CADENCE], (double)s_cadence_100);
    }

    /* 20 Hz: fs/2 = 10, so [16, fs/2] is empty and must be exactly 0, and
     * [8,16) only reaches 10 Hz. Lags 8..28 still contain 1.8 Hz (11.1). */
    {
        const tone_t t2[2] = { { 1.8f, 3.0f }, { 9.0f, 1.5f } };
        gait_init(&s_g, 20.0f);
        w = feed_tones(20.0f, t2, 2, k_mix, 0.05f, GAIT_NWIN, f, &at);
        CHECK(w == 1, "20 Hz: %d windows", w);
        print_feat("20 Hz: 1.8 Hz + 9 Hz tones:", f);
        CHECK(f[F_BAND0 + 5] == 0.0f, "20 Hz: band above fs/2 is %.4f", (double)f[F_BAND0 + 5]);
        CHECK_NEAR(f[F_BAND0 + 4], 0.2, 0.03, "20 Hz: [8,10] Hz band fraction");
        CHECK_NEAR(band_sum(f), 1.0, 1e-4, "20 Hz band fractions sum");
        CHECK(f[F_CADENCE] >= 1.7f && f[F_CADENCE] <= 1.9f, "20 Hz cadence %.3f",
              (double)f[F_CADENCE]);
    }

    /* Around the pair-average threshold (gait_id.c: above 60 Hz the
     * cadence autocorrelation runs on (x[2k] + x[2k+1]) / 2 at half the
     * rate). 60 Hz uses x itself; 62.5, 100 and 200 Hz use the pair
     * average, and the lag found must be converted back to seconds with
     * the HALVED rate - get that wrong and the answer is off by 2x. All
     * four must report the 1.8 Hz step rate. At 200 Hz the lag range is
     * 0.4-0.64 s (half the 1.28 s window), which still holds the 0.556 s
     * step period. */
    {
        const tone_t t1 = { 1.8f, 3.0f };
        const float  rates[4] = { 60.0f, 62.5f, 100.0f, 200.0f };
        printf("    1.8 Hz tone around the 60 Hz pair-average threshold:\n");
        for (int i = 0; i < 4; i++) {
            gait_init(&s_g, rates[i]);
            rng_seed(53);
            w = feed_tones(rates[i], &t1, 1, k_mix, 0.05f, GAIT_NWIN, f, &at);
            printf("      %5.1f Hz (%s): cadence %.3f Hz, acorr %.3f\n", (double)rates[i],
                   rates[i] > 60.0f ? "pair-averaged" : "full rate    ",
                   (double)f[F_CADENCE], (double)f[F_ACORR]);
            CHECK(w == 1, "%.1f Hz: %d windows", (double)rates[i], w);
            CHECK(all_finite(f, GAIT_NFEAT), "%.1f Hz: non-finite feature", (double)rates[i]);
            CHECK(f[F_CADENCE] >= 1.7f && f[F_CADENCE] <= 1.9f, "%.1f Hz cadence %.3f",
                  (double)rates[i], (double)f[F_CADENCE]);
        }
    }

    /* 400 Hz: pair-averaged to 200 Hz, where 0.4 s is 80 lags, past the 64
     * that half of the 128 averaged samples allows - no lag is in range,
     * so no cadence (0, 0), and nothing indexes past the ring. */
    {
        const tone_t t1 = { 1.8f, 3.0f };
        gait_init(&s_g, 400.0f);
        w = feed_tones(400.0f, &t1, 1, k_mix, 0.05f, GAIT_NWIN, f, &at);
        printf("    400 Hz: cadence %.3f acorr %.3f centroid %.3f\n", (double)f[F_CADENCE],
               (double)f[F_ACORR], (double)f[F_CENTROID]);
        CHECK(w == 1, "400 Hz: %d windows", w);
        CHECK(f[F_CADENCE] == 0.0f && f[F_ACORR] == 0.0f, "400 Hz: cadence should be empty");
        CHECK(all_finite(f, GAIT_NFEAT), "400 Hz: non-finite feature");
        CHECK_NEAR(band_sum(f), 1.0, 1e-4, "400 Hz band fractions sum");
    }

    /* 1 Hz: the analysed range [0.5, fs/2] is the single Nyquist bin and
     * 1.4 s is 1 lag, below the minimum of 2: no cadence, still finite. */
    {
        float grp[CSI_NGROUPS], feat[GAIT_NFEAT];
        int   n = 0;
        gait_init(&s_g, 1.0f);
        rng_seed(52);
        for (int i = 0; i < GAIT_NWIN; i++) {
            noise_packet(grp);
            if (gait_push(&s_g, grp, true, feat)) {
                n++;
                memcpy(f, feat, sizeof(feat));
            }
        }
        CHECK(n == 1, "1 Hz: %d windows", n);
        printf("    1 Hz: band0 %.3f cadence %.3f\n", (double)f[F_BAND0], (double)f[F_CADENCE]);
        CHECK(all_finite(f, GAIT_NFEAT), "1 Hz: non-finite feature");
        CHECK(f[F_CADENCE] == 0.0f, "1 Hz: cadence %.3f", (double)f[F_CADENCE]);
    }

    /* invalid rates fall back to 50 Hz */
    {
        volatile float zero = 0.0f;
        gait_init(&s_g, 0.0f);
        CHECK(s_g.rate_hz == 50.0f, "rate 0 -> %g", (double)s_g.rate_hz);
        gait_set_rate(&s_g, -3.0f);
        CHECK(s_g.rate_hz == 50.0f, "rate -3 -> %g", (double)s_g.rate_hz);
        gait_set_rate(&s_g, zero / zero);
        CHECK(s_g.rate_hz == 50.0f, "rate NaN -> %g", (double)s_g.rate_hz);
        gait_set_rate(&s_g, 1.0f / zero);
        CHECK(s_g.rate_hz == 50.0f, "rate inf -> %g", (double)s_g.rate_hz);
        gait_set_rate(&s_g, 100.0f);
        CHECK(s_g.rate_hz == 100.0f, "rate 100 -> %g", (double)s_g.rate_hz);
    }
}

int main(void)
{
    test_clean_periodic();
    test_window_timing();
    test_walkers();
    test_degenerate();
    test_rates();
    return test_summary("test_gait");
}
