/*
 * test_ncc.c - host tests for csi_ncc.c: Welford statistics, per-feature
 * standardisation, rejection by distance and by margin, min_n, NaN/inf
 * hygiene, the NVS-blob sanity check, and ncc_distances (the per-class
 * distances main.c's EXPLAIN lines print) against ncc_classify.
 *
 * The clusters here are Gaussian and synthetic. Real gait or count features
 * are neither Gaussian nor equally spread per class, so the accuracy printed
 * below is what the CLASSIFIER does on separable data - it says nothing
 * about how separable real people are (README, Step 10).
 */

#include <float.h>
#include <string.h>
#include "test_util.h"
#include "csi_ncc.h"

/* NaN and inf built from their bit patterns: writing 0.0f/0.0f or 1e39f
 * would be folded at compile time and MSVC /WX rejects that (C4723/C4756). */
static float f_from_bits(uint32_t b)
{
    float f;
    memcpy(&f, &b, sizeof(f));
    return f;
}

static float nan_f(void) { return f_from_bits(0x7FC00000u); }
static float inf_f(void) { return f_from_bits(0x7F800000u); }

/* ------------------------------------------------------------------ */
/* Welford against a double-precision two-pass reference               */
/* ------------------------------------------------------------------ */

#define WF_N 500

static void test_welford(void)
{
    static float xs[WF_N][NCC_MAX_FEAT];
    static ncc_model_t m;
    /* Feature f sits at base[f] with spread sd[f]. The large-offset,
     * small-spread ones (1000 +/- 0.1, -3 +/- 0.05) are where the textbook
     * sum(x^2) - n*mean^2 cancels to noise in float. */
    static const float base[6] = { 1000.0f, -3.0f, 0.0f, 1e-3f, 50.0f, -250.0f };
    static const float sd[6]   = { 0.1f,    0.05f, 1.0f, 1e-4f, 20.0f, 3.0f };

    rng_seed(11);
    ncc_init(&m, NCC_MAX_FEAT);
    for (int i = 0; i < WF_N; i++) {
        for (int f = 0; f < NCC_MAX_FEAT; f++) {
            xs[i][f] = base[f % 6] + sd[f % 6] * rng_gauss();
        }
        CHECK(ncc_add(&m, 2, xs[i]), "ncc_add refused finite vector %d", i);
    }
    CHECK(m.cls[2].n == WF_N, "n = %u", (unsigned)m.cls[2].n);

    double worst_mean = 0.0, worst_var = 0.0, naive_var_err = 0.0;
    for (int f = 0; f < NCC_MAX_FEAT; f++) {
        double s = 0.0;
        for (int i = 0; i < WF_N; i++) s += xs[i][f];
        double mean = s / WF_N;
        double ss = 0.0;
        for (int i = 0; i < WF_N; i++) ss += (xs[i][f] - mean) * (xs[i][f] - mean);
        double var = ss / (WF_N - 1);

        double sdf = sd[f % 6];
        double em  = fabs((double)m.cls[2].mean[f] - mean) / sdf;   /* in units of sd */
        double ev  = fabs((double)m.cls[2].m2[f] / (WF_N - 1) - var) / var;
        if (em > worst_mean) worst_mean = em;
        if (ev > worst_var) worst_var = ev;
        CHECK(em < 1e-2, "feature %d mean off by %.3g sd", f, em);
        CHECK(ev < 1e-3, "feature %d variance relative error %.3g", f, ev);

        if (f == 0) {
            /* What Welford avoids, for the record: the one-pass float
             * formula on the 1000 +/- 0.1 feature. */
            float sx = 0.0f, sxx = 0.0f;
            for (int i = 0; i < WF_N; i++) { sx += xs[i][f]; sxx += xs[i][f] * xs[i][f]; }
            float nv = (sxx - sx * sx / (float)WF_N) / (float)(WF_N - 1);
            naive_var_err = fabs((double)nv - var) / var;
        }
    }
    printf("  Welford vs two-pass (%d x %d): worst mean error %.2e sd, worst variance "
           "relative error %.2e (naive float sum-of-squares on 1000+/-0.1: %.2e)\n",
           WF_N, NCC_MAX_FEAT, worst_mean, worst_var, naive_var_err);

    /* A class with n == 0 starts from scratch whatever its stored numbers
     * say (ncc_valid does not look at unused classes). */
    float one[NCC_MAX_FEAT];
    for (int f = 0; f < NCC_MAX_FEAT; f++) {
        m.cls[3].mean[f] = 1e20f;
        m.cls[3].m2[f]   = -5.0f;
        one[f] = (float)f - 4.5f;
    }
    CHECK(ncc_add(&m, 3, one), "add to a stale empty class refused");
    int fresh = 1;
    for (int f = 0; f < NCC_MAX_FEAT; f++) {
        if (m.cls[3].mean[f] != one[f] || m.cls[3].m2[f] != 0.0f) fresh = 0;
    }
    CHECK(fresh, "empty class did not restart from zero");

    CHECK(!ncc_add(&m, -1, one), "ncc_add accepted class -1");
    CHECK(!ncc_add(&m, NCC_MAX_CLASSES, one), "ncc_add accepted class %d", NCC_MAX_CLASSES);
    CHECK(!ncc_add(NULL, 0, one), "ncc_add accepted a NULL model");
    CHECK(!ncc_add(&m, 0, NULL), "ncc_add accepted a NULL vector");
}

/* ------------------------------------------------------------------ */
/* Three Gaussian clusters, 18-D, per-feature scales 1e-2 .. 1e3        */
/* ------------------------------------------------------------------ */

#define CL_NCLS   3
#define CL_NTRAIN 30          /* = CSI_ID_ENROLL_WINDOWS default */
#define CL_NTEST  200
#define CL_MIN_N  10          /* = CSI_ID_MIN_WINDOWS default    */

static float cl_scale[NCC_MAX_FEAT];
static float cl_mu[CL_NCLS][NCC_MAX_FEAT];

/* Features 0..15 are strongly informative (class means 1 or 2 of their own
 * std apart) with std spanning 1e-2 .. 1. Features 16, 17 have std 1e3 and
 * are only weakly informative (0.2 std). Unscaled Euclidean distance is
 * then decided by the two big, nearly useless features; standardised
 * distance weighs every feature by its own spread. Pairwise separation is
 * the same for all three pairs (Mahalanobis ~5.7), which puts the ideal
 * nearest-centroid error well under 1% - headroom below the 95% bar. */
static void cl_setup(void)
{
    for (int f = 0; f < NCC_MAX_FEAT; f++) {
        float s   = (f < 16) ? powf(10.0f, -2.0f + 2.0f * (float)f / 15.0f) : 1000.0f;
        float amp = (f < 16) ? 1.0f : 0.2f;
        float b   = (f & 1) ? -7.0f : 3.0f;          /* off zero, both signs */
        cl_scale[f] = s;
        for (int c = 0; c < CL_NCLS; c++) {
            float off = (float)(((f + c) % 3) - 1);  /* -1, 0, +1 cycling */
            cl_mu[c][f] = s * (b + amp * off);
        }
    }
}

static void cl_sample(int c, float *x)
{
    for (int f = 0; f < NCC_MAX_FEAT; f++) {
        x[f] = cl_mu[c][f] + cl_scale[f] * rng_gauss();
    }
}

/* Plain nearest centroid on raw Euclidean distance, same centroids: the
 * control that shows the standardisation is doing the work. */
static int raw_euclid_class(const ncc_model_t *m, const float *x)
{
    int    best = -1;
    double bd   = 0.0;
    for (int c = 0; c < CL_NCLS; c++) {
        double d = 0.0;
        for (int f = 0; f < NCC_MAX_FEAT; f++) {
            double e = (double)x[f] - (double)m->cls[c].mean[f];
            d += e * e;
        }
        if (best < 0 || d < bd) { best = c; bd = d; }
    }
    return best;
}

static void test_clusters(void)
{
    static ncc_model_t m;
    float x[NCC_MAX_FEAT];

    cl_setup();
    rng_seed(2024);
    ncc_init(&m, NCC_MAX_FEAT);
    for (int c = 0; c < CL_NCLS; c++) {
        for (int i = 0; i < CL_NTRAIN; i++) {
            cl_sample(c, x);
            CHECK(ncc_add(&m, c, x), "training add refused");
        }
    }
    CHECK(ncc_trained_classes(&m, CL_MIN_N) == 3, "trained classes %d",
          ncc_trained_classes(&m, CL_MIN_N));
    CHECK(ncc_valid(&m, NCC_MAX_FEAT), "trained model fails ncc_valid");

    int    ok_std = 0, ok_raw = 0, total = 0;
    int    def_right = 0, def_unknown = 0, def_wrong = 0;
    double own_dist = 0.0;
    for (int i = 0; i < CL_NTEST; i++) {
        for (int c = 0; c < CL_NCLS; c++) {
            cl_sample(c, x);
            total++;
            /* reject/margin disabled: pure classifier accuracy */
            ncc_result_t r = ncc_classify(&m, x, CL_MIN_N, 1e9f, 1.0f);
            if (r.best == c) ok_std++;
            if (raw_euclid_class(&m, x) == c) ok_raw++;
            if (r.nearest == c) own_dist += r.dist;

            /* the firmware defaults: reject 2.50, margin 1.15 */
            ncc_result_t d = ncc_classify(&m, x, CL_MIN_N, 2.5f, 1.15f);
            if (d.best == c)       def_right++;
            else if (d.best < 0)   def_unknown++;
            else                   def_wrong++;
        }
    }
    double acc_std = 100.0 * ok_std / total;
    double acc_raw = 100.0 * ok_raw / total;
    double mean_own = own_dist / (ok_std > 0 ? ok_std : 1);
    printf("  3 clusters x 18-D, %d held out: standardised nearest-centroid %.1f%%, "
           "raw Euclidean %.1f%%, mean own-class RMS distance %.3f\n",
           total, acc_std, acc_raw, mean_own);
    printf("  same data at reject 2.50 / margin 1.15: correct %.1f%%, unknown %.1f%%, "
           "wrong %.1f%%\n", 100.0 * def_right / total, 100.0 * def_unknown / total,
           100.0 * def_wrong / total);

    CHECK(acc_std >= 95.0, "standardised accuracy %.1f%% < 95%%", acc_std);
    CHECK(acc_raw < 70.0, "raw Euclidean %.1f%% - the test no longer shows that "
          "standardisation matters", acc_raw);
    /* The header's promise: a vector from an enrolled class sits near 1.0
     * from its own centroid regardless of feature count or scale. */
    CHECK_NEAR(mean_own, 1.0, 0.15, "mean own-class distance");
    CHECK(100.0 * def_right / total >= 90.0, "default-threshold correct %.1f%%",
          100.0 * def_right / total);
    CHECK(100.0 * def_wrong / total <= 2.0, "default-threshold wrong %.1f%%",
          100.0 * def_wrong / total);

    /* Far from everything: rejected, but nearest still reported. */
    for (int f = 0; f < NCC_MAX_FEAT; f++) x[f] = cl_mu[1][f] + 10.0f * cl_scale[f];
    ncc_result_t r = ncc_classify(&m, x, CL_MIN_N, 2.5f, 1.15f);
    CHECK(r.best == -1, "far point accepted as %d (dist %.3g)", r.best, r.dist);
    CHECK(r.nearest >= 0 && r.nearest < CL_NCLS, "far point nearest %d", r.nearest);
    CHECK(r.dist > 2.5f && r.dist < 1e6f, "far point dist %.3g", r.dist);
    CHECK(r.second >= r.dist && r.second < 1e6f, "far point second %.3g", r.second);

    /* A NaN input gives "no result", never a NaN distance that would slip
     * past the "> reject" test. */
    cl_sample(0, x);
    x[7] = nan_f();
    r = ncc_classify(&m, x, CL_MIN_N, 1e9f, 1.0f);
    CHECK(r.best == -1 && r.nearest == -1, "NaN input classified: best %d nearest %d",
          r.best, r.nearest);

    /* A NaN threshold rejects rather than accepts. */
    cl_sample(0, x);
    r = ncc_classify(&m, x, CL_MIN_N, nan_f(), 1.0f);
    CHECK(r.best == -1 && r.nearest == 0, "NaN reject_rms: best %d nearest %d",
          r.best, r.nearest);
}

/* ------------------------------------------------------------------ */
/* Hand-built two-class model: exact ties, margin, min_n                */
/* ------------------------------------------------------------------ */

/* Class 0 mean f, class 1 mean f + 2, n = 20 and m2 = 19 each, so the
 * pooled std is exactly 1 and every distance below is exact in float. */
static void build_two(ncc_model_t *m)
{
    ncc_init(m, NCC_MAX_FEAT);
    for (int f = 0; f < NCC_MAX_FEAT; f++) {
        m->cls[0].mean[f] = (float)f;
        m->cls[1].mean[f] = (float)f + 2.0f;
        m->cls[0].m2[f]   = 19.0f;
        m->cls[1].m2[f]   = 19.0f;
    }
    m->cls[0].n = 20;
    m->cls[1].n = 20;
    ncc_set_name(m, 0, "alice");
    ncc_set_name(m, 1, "bob");
}

static void fill_offset(float *x, float off)
{
    for (int f = 0; f < NCC_MAX_FEAT; f++) x[f] = (float)f + off;
}

static void test_margin_reject(void)
{
    static ncc_model_t m;
    float x[NCC_MAX_FEAT];
    ncc_result_t r;

    build_two(&m);
    CHECK(ncc_valid(&m, NCC_MAX_FEAT), "hand-built model invalid");

    /* exactly halfway: dist == second == 1 */
    for (int f = 0; f < NCC_MAX_FEAT; f++) x[f] = 0.5f * (m.cls[0].mean[f] + m.cls[1].mean[f]);
    r = ncc_classify(&m, x, 10, 2.5f, 1.15f);
    CHECK(r.best == -1, "midpoint accepted as %d with margin 1.15", r.best);
    CHECK(r.nearest == 0, "tie should go to the lower index, got %d", r.nearest);
    CHECK_NEAR(r.dist, 1.0, 0.0, "midpoint dist");
    CHECK_NEAR(r.second, 1.0, 0.0, "midpoint second");
    r = ncc_classify(&m, x, 10, 2.5f, 1.0f);
    CHECK(r.best == 0, "midpoint with margin 1.0 (disabled): best %d", r.best);
    r = ncc_classify(&m, x, 10, 2.5f, 0.5f);
    CHECK(r.best == 0, "midpoint with margin 0.5 (disabled): best %d", r.best);

    /* dist 0.95 vs 1.05: ratio 1.105 < 1.15 -> ambiguous, rejected */
    fill_offset(x, 0.95f);
    r = ncc_classify(&m, x, 10, 2.5f, 1.15f);
    CHECK(r.best == -1 && r.nearest == 0, "ratio 1.105: best %d nearest %d", r.best, r.nearest);
    /* dist 0.9 vs 1.1: ratio 1.22 > 1.15 -> accepted */
    fill_offset(x, 0.9f);
    r = ncc_classify(&m, x, 10, 2.5f, 1.15f);
    CHECK(r.best == 0, "ratio 1.22: best %d", r.best);
    CHECK_NEAR(r.dist, 0.9, 1e-5, "dist at +0.9");
    CHECK_NEAR(r.second, 1.1, 1e-5, "second at +0.9");
    /* nearer class 1 */
    fill_offset(x, 2.25f);
    r = ncc_classify(&m, x, 10, 2.5f, 1.15f);
    CHECK(r.best == 1, "near bob: best %d", r.best);

    /* 3 std below class 0: over reject 2.5, nearest still 0 */
    fill_offset(x, -3.0f);
    r = ncc_classify(&m, x, 10, 2.5f, 1.15f);
    CHECK(r.best == -1 && r.nearest == 0, "far below: best %d nearest %d", r.best, r.nearest);
    CHECK_NEAR(r.dist, 3.0, 1e-6, "far below dist");
    CHECK_NEAR(r.second, 5.0, 1e-6, "far below second");
    /* ...and accepted once the threshold allows it */
    r = ncc_classify(&m, x, 10, 3.5f, 1.15f);
    CHECK(r.best == 0, "reject 3.5: best %d", r.best);
}

static void test_min_n(void)
{
    static ncc_model_t m, ref;
    float x[NCC_MAX_FEAT];

    build_two(&m);
    ref = m;                               /* the same model without class 2 */
    fill_offset(x, 7.0f);

    /* class 2: sits exactly on x, with a huge spread, one window short */
    for (int f = 0; f < NCC_MAX_FEAT; f++) {
        m.cls[2].mean[f] = x[f];
        m.cls[2].m2[f]   = 1e6f;
    }
    m.cls[2].n = 9;
    ncc_set_name(&m, 2, "carol");

    CHECK(ncc_trained_classes(&m, 10) == 2, "trained(10) = %d", ncc_trained_classes(&m, 10));
    CHECK(ncc_trained_classes(&m, 9) == 3, "trained(9) = %d", ncc_trained_classes(&m, 9));
    CHECK(ncc_trained_classes(&m, 0) == 3, "trained(0) = %d", ncc_trained_classes(&m, 0));
    CHECK(ncc_trained_classes(&m, 21) == 0, "trained(21) = %d", ncc_trained_classes(&m, 21));

    /* Below min_n it must change nothing - neither the winner nor, through
     * the pooled variance, the distances. */
    ncc_result_t r  = ncc_classify(&m, x, 10, 1e9f, 1.0f);
    ncc_result_t r0 = ncc_classify(&ref, x, 10, 1e9f, 1.0f);
    CHECK(r.nearest == 1, "under-trained class won: nearest %d", r.nearest);
    CHECK(r.dist == r0.dist && r.second == r0.second,
          "under-trained class changed distances: %.6g/%.6g vs %.6g/%.6g",
          r.dist, r.second, r0.dist, r0.second);

    /* one more window and it counts */
    m.cls[2].n = 10;
    r = ncc_classify(&m, x, 10, 1e9f, 1.0f);
    CHECK(r.nearest == 2 && r.dist == 0.0f, "class 2 at min_n: nearest %d dist %.3g",
          r.nearest, r.dist);

    /* nothing usable */
    r = ncc_classify(&m, x, 1000, 1e9f, 1.0f);
    CHECK(r.best == -1 && r.nearest == -1, "no usable class: best %d nearest %d",
          r.best, r.nearest);
    static ncc_model_t empty;
    ncc_init(&empty, 6);
    r = ncc_classify(&empty, x, 1, 1e9f, 1.0f);
    CHECK(r.best == -1 && r.nearest == -1, "empty model: best %d nearest %d",
          r.best, r.nearest);
    CHECK(ncc_trained_classes(&empty, 0) == 0, "empty model has trained classes");
}

/* ------------------------------------------------------------------ */
/* One enrolled class (the usual first state of the ID model)          */
/* ------------------------------------------------------------------ */

static void test_single_class(void)
{
    static ncc_model_t m;
    static const float sd[6] = { 0.02f, 0.5f, 3.0f, 0.1f, 40.0f, 1.0f };
    float x[6];

    rng_seed(99);
    ncc_init(&m, 6);                       /* COUNT_NFEAT-sized */
    for (int i = 0; i < 30; i++) {
        for (int f = 0; f < 6; f++) x[f] = 1.0f + sd[f] * rng_gauss();
        ncc_add(&m, 1, x);                 /* slot 1, not 0: no index assumptions */
    }
    CHECK(ncc_trained_classes(&m, 10) == 1, "trained %d", ncc_trained_classes(&m, 10));

    /* a fresh sample: accepted, and margin 1.15 does not apply with one class */
    int accepted = 0;
    for (int i = 0; i < 50; i++) {
        for (int f = 0; f < 6; f++) x[f] = 1.0f + sd[f] * rng_gauss();
        ncc_result_t r = ncc_classify(&m, x, 10, 2.5f, 1.15f);
        if (r.best == 1 && r.second == 1e30f) accepted++;
    }
    CHECK(accepted >= 49, "single class accepted %d/50 own samples", accepted);

    ncc_result_t r = ncc_classify(&m, m.cls[1].mean, 10, 2.5f, 1.15f);
    CHECK(r.best == 1 && r.dist < 1e-6f, "own centroid: best %d dist %.3g", r.best, r.dist);
    CHECK(r.second == 1e30f, "no runner-up should give second 1e30, got %.3g", r.second);

    for (int f = 0; f < 6; f++) x[f] = 1.0f + 10.0f * sd[f];
    r = ncc_classify(&m, x, 10, 2.5f, 1.15f);
    CHECK(r.best == -1 && r.nearest == 1, "far from the only class: best %d nearest %d",
          r.best, r.nearest);
    CHECK(r.dist > 5.0f, "far dist %.3g", r.dist);
}

/* ------------------------------------------------------------------ */
/* Non-finite inputs and corrupted-but-finite blobs                    */
/* ------------------------------------------------------------------ */

static void test_nonfinite(void)
{
    static ncc_model_t m, snap;
    float x[NCC_MAX_FEAT];

    rng_seed(5);
    ncc_init(&m, NCC_MAX_FEAT);
    for (int i = 0; i < 12; i++) {
        for (int f = 0; f < NCC_MAX_FEAT; f++) x[f] = rng_gauss();
        ncc_add(&m, 0, x);
    }
    snap = m;

    /* bad value in the LAST feature: nothing before it may be committed */
    for (int f = 0; f < NCC_MAX_FEAT; f++) x[f] = 0.5f;
    x[NCC_MAX_FEAT - 1] = nan_f();
    CHECK(!ncc_add(&m, 0, x), "NaN accepted");
    CHECK(memcmp(&m, &snap, sizeof(m)) == 0, "NaN add changed the model");
    x[NCC_MAX_FEAT - 1] = inf_f();
    CHECK(!ncc_add(&m, 0, x), "+inf accepted");
    x[NCC_MAX_FEAT - 1] = -inf_f();
    CHECK(!ncc_add(&m, 0, x), "-inf accepted");
    x[0] = nan_f();
    x[NCC_MAX_FEAT - 1] = 0.5f;
    CHECK(!ncc_add(&m, 0, x), "NaN in feature 0 accepted");
    CHECK(memcmp(&m, &snap, sizeof(m)) == 0, "rejected adds changed the model");
    CHECK(m.cls[0].n == 12, "n = %u", (unsigned)m.cls[0].n);

    /* finite inputs whose update overflows: -3e38 then +3e38 */
    for (int f = 0; f < NCC_MAX_FEAT; f++) x[f] = -3e38f;
    CHECK(ncc_add(&m, 1, x), "-3e38 (finite) refused");
    snap = m;
    for (int f = 0; f < NCC_MAX_FEAT; f++) x[f] = 3e38f;
    CHECK(!ncc_add(&m, 1, x), "overflowing update accepted");
    CHECK(memcmp(&m, &snap, sizeof(m)) == 0, "overflowing add changed the model");

    /* A blob that passes ncc_valid with absurd but finite numbers must not
     * produce a NaN distance (NaN compares false with everything, so a NaN
     * distance would never be "> reject" and would be ACCEPTED). */
    static ncc_model_t bad;
    ncc_init(&bad, 4);
    for (int c = 0; c < 2; c++) {
        bad.cls[c].n = 20;
        for (int f = 0; f < 4; f++) {
            bad.cls[c].mean[f] = (c == 0) ? FLT_MAX : -FLT_MAX;
            bad.cls[c].m2[f]   = FLT_MAX;          /* sum of the two overflows */
        }
    }
    CHECK(ncc_valid(&bad, 4), "finite extreme blob should pass ncc_valid");
    float y[4] = { 0.0f, 1.0f, -1.0f, 2.0f };
    ncc_result_t r = ncc_classify(&bad, y, 10, 2.5f, 1.15f);
    CHECK(r.dist == r.dist && r.second == r.second, "NaN distance from extreme blob");
    CHECK(r.nearest >= 0, "extreme blob: nearest %d", r.nearest);

    for (int f = 0; f < 4; f++) {
        bad.cls[0].m2[f] = 1.0f;
        bad.cls[1].m2[f] = 1.0f;
        y[f] = -FLT_MAX;                           /* (x - mean)^2 overflows */
    }
    r = ncc_classify(&bad, y, 10, 1e9f, 1.0f);     /* the count model's settings */
    CHECK(r.dist == r.dist, "NaN distance on overflow");
    CHECK(r.nearest == 1 && r.dist == 0.0f, "overflow case: nearest %d dist %.3g",
          r.nearest, r.dist);
    CHECK(r.second == 1e30f, "overflowed distance should read as 1e30, got %.3g", r.second);
}

/* ------------------------------------------------------------------ */
/* ncc_distances: the numbers behind every decision (main.c "explain") */
/* ------------------------------------------------------------------ */

/* ncc_classify exactly as it was before ncc_distances was added (the
 * 2026-09-24 csi_ncc.c, inlined, private copies of its constants). The
 * refactor moved its body into helpers shared with ncc_distances; this
 * copy is the proof that no result changed - compared bit for bit. */
#define REF_STD_FLOOR  1e-4f
#define REF_FAR        1e30f

static int ref_finite(float v)
{
    return (v == v) && fabsf(v) <= FLT_MAX;
}

static ncc_result_t ref_classify(const ncc_model_t *m, const float *x, uint32_t min_n,
                                 float reject_rms, float margin)
{
    ncc_result_t r;
    r.best    = -1;
    r.nearest = -1;
    r.dist    = REF_FAR;
    r.second  = REF_FAR;

    if (!m || !x) return r;
    int nf = m->nfeat;
    if (nf < 1 || nf > NCC_MAX_FEAT) return r;
    if (min_n < 1) min_n = 1;

    int   use[NCC_MAX_CLASSES];
    int   usable = 0;
    float dof    = 0.0f;
    for (int c = 0; c < NCC_MAX_CLASSES; c++) {
        use[c] = (m->cls[c].n >= min_n);
        if (use[c]) {
            usable++;
            dof += (float)(m->cls[c].n - 1u);
        }
    }
    if (usable == 0) return r;
    for (int f = 0; f < nf; f++) {
        if (!ref_finite(x[f])) return r;
    }
    float inv_s[NCC_MAX_FEAT];
    for (int f = 0; f < nf; f++) {
        float s = REF_STD_FLOOR;
        if (dof > 0.0f) {
            float num = 0.0f;
            for (int c = 0; c < NCC_MAX_CLASSES; c++) {
                if (use[c]) num += m->cls[c].m2[f];
            }
            float var = num / dof;
            if (!(var <= FLT_MAX)) var = FLT_MAX;
            float sd = sqrtf(var);
            if (sd > s) s = sd;
        }
        inv_s[f] = 1.0f / s;
    }
    for (int c = 0; c < NCC_MAX_CLASSES; c++) {
        if (!use[c]) continue;
        const ncc_class_t *k = &m->cls[c];
        float sum = 0.0f;
        for (int f = 0; f < nf; f++) {
            float z = (x[f] - k->mean[f]) * inv_s[f];
            sum += z * z;
        }
        float d = sqrtf(sum / (float)nf);
        if (!(d <= REF_FAR)) d = REF_FAR;
        if (r.nearest < 0 || d < r.dist) {
            r.second  = r.dist;
            r.dist    = d;
            r.nearest = c;
        } else if (d < r.second) {
            r.second = d;
        }
    }
    r.best = r.nearest;
    if (!(r.dist <= reject_rms)) {
        r.best = -1;
    } else if (usable >= 2 && margin > 1.0f && r.second < margin * r.dist) {
        r.best = -1;
    }
    return r;
}

/* One random model: nfeat 1..NCC_MAX_FEAT, each class unused, short of
 * min_n, exactly at it or above it; per-feature scales 1e-3 .. 1e3; now
 * and then a class that duplicates another's mean (an exact distance tie)
 * or a zero spread (the std floor). */
static void rand_model(ncc_model_t *m, uint32_t min_n)
{
    int nf = 1 + (int)(rng_u32() % NCC_MAX_FEAT);
    ncc_init(m, nf);
    for (int c = 0; c < NCC_MAX_CLASSES; c++) {
        uint32_t pick = rng_u32() % 5;
        uint32_t n = (pick == 0) ? 0u :
                     (pick == 1) ? (min_n > 1 ? min_n - 1 : 0u) :
                     (pick == 2) ? min_n : min_n + 1 + rng_u32() % 40;
        m->cls[c].n = n;
        if (n == 0) continue;
        ncc_set_name(m, c, "p");
        for (int f = 0; f < nf; f++) {
            float scale = powf(10.0f, -3.0f + 6.0f * rng_uniform());
            m->cls[c].mean[f] = scale * rng_gauss();
            m->cls[c].m2[f]   = (rng_u32() % 8 == 0) ? 0.0f
                              : scale * scale * (float)(n > 1 ? n - 1 : 1) * rng_uniform();
        }
    }
    /* an exact tie now and then: class 3 copies class 1 */
    if (rng_u32() % 4 == 0 && m->cls[1].n > 0) {
        m->cls[3] = m->cls[1];
    }
}

static void test_distances(void)
{
    static ncc_model_t m;
    float x[NCC_MAX_FEAT];
    float out[NCC_MAX_CLASSES];
    int   cases = 0, with_two = 0, ties = 0, bad = 0, ref_bad = 0;

    rng_seed(4242);
    for (int it = 0; it < 4000; it++) {
        uint32_t min_n = (uint32_t)(rng_u32() % 16);     /* 0 acts as 1 */
        rand_model(&m, min_n);
        int nf = m.nfeat;
        /* near a random class, or anywhere */
        int near = (int)(rng_u32() % NCC_MAX_CLASSES);
        for (int f = 0; f < nf; f++) {
            float s = fabsf(m.cls[near].mean[f]) + 1e-3f;
            x[f] = m.cls[near].mean[f] + s * rng_gauss() * ((it & 1) ? 0.2f : 3.0f);
        }
        float reject = 0.5f + 4.0f * rng_uniform();
        float margin = (it % 3 == 0) ? 1.0f : 1.0f + 0.5f * rng_uniform();

        int          cnt = ncc_distances(&m, x, min_n, out);
        ncc_result_t r   = ncc_classify(&m, x, min_n, reject, margin);
        ncc_result_t ref = ref_classify(&m, x, min_n, reject, margin);
        cases++;

        /* ncc_classify unchanged, bit for bit */
        if (!(r.best == ref.best && r.nearest == ref.nearest &&
              r.dist == ref.dist && r.second == ref.second)) {
            ref_bad++;
            CHECK(0, "case %d: classify %d/%d %.9g/%.9g vs reference %d/%d %.9g/%.9g", it,
                  r.best, r.nearest, r.dist, r.second, ref.best, ref.nearest, ref.dist, ref.second);
        }

        uint32_t eff = min_n < 1 ? 1u : min_n;
        int want = 0;
        for (int c = 0; c < NCC_MAX_CLASSES; c++) {
            bool usable = m.cls[c].n >= eff;
            want += usable ? 1 : 0;
            if (usable != (out[c] >= 0.0f)) {
                bad++;
                CHECK(0, "case %d class %d: n %u min_n %u but distance %.6g", it, c,
                      (unsigned)m.cls[c].n, (unsigned)min_n, out[c]);
            }
            if (!usable && out[c] != NCC_NO_DIST) {
                bad++;
                CHECK(0, "case %d class %d: unusable class reads %.6g, not NCC_NO_DIST", it, c, out[c]);
            }
        }
        if (cnt != want || cnt != ncc_trained_classes(&m, min_n)) {
            bad++;
            CHECK(0, "case %d: ncc_distances returned %d, %d classes usable", it, cnt, want);
        }
        if (cnt == 0) {
            if (r.nearest != -1) { bad++; CHECK(0, "case %d: no distances but nearest %d", it, r.nearest); }
            continue;
        }

        /* nearest = the smallest distance, lowest index on a tie; its
         * distance and the runner-up's are ncc_classify's, exactly */
        int   nn = -1;
        float d1 = 0.0f, d2 = 1e30f;
        for (int c = 0; c < NCC_MAX_CLASSES; c++) {
            if (out[c] < 0.0f) continue;
            if (nn < 0 || out[c] < d1) {
                if (nn >= 0) d2 = d1;
                nn = c;
                d1 = out[c];
            } else if (out[c] < d2) {
                d2 = out[c];
            }
            if (nn != c && out[c] == d1) ties++;
        }
        if (cnt >= 2) with_two++;
        if (nn != r.nearest || d1 != r.dist || d2 != r.second) {
            bad++;
            CHECK(0, "case %d: distances say nearest %d %.9g second %.9g, classify %d %.9g %.9g",
                  it, nn, d1, d2, r.nearest, r.dist, r.second);
        }
    }
    printf("  ncc_distances vs ncc_classify: %d random models (%d with 2+ usable classes, "
           "%d exact ties), %d mismatches; ncc_classify vs its pre-refactor copy: %d differ\n",
           cases, with_two, ties, bad, ref_bad);
    CHECK(bad == 0, "%d mismatches", bad);
    CHECK(ref_bad == 0, "ncc_classify changed in %d cases", ref_bad);
    CHECK(with_two > 1000 && ties > 50, "random models too easy: %d with 2+, %d ties", with_two, ties);

    /* the hand-built model: exact numbers */
    build_two(&m);
    fill_offset(x, 0.9f);
    CHECK(ncc_distances(&m, x, 10, out) == 2, "two usable classes");
    CHECK_NEAR(out[0], 0.9, 1e-5, "distance to alice at +0.9");
    CHECK_NEAR(out[1], 1.1, 1e-5, "distance to bob at +0.9");
    CHECK(out[2] == NCC_NO_DIST && out[3] == NCC_NO_DIST, "empty slots: %.3g %.3g", out[2], out[3]);
    CHECK(ncc_distances(&m, x, 21, out) == 0 && out[0] == NCC_NO_DIST && out[1] == NCC_NO_DIST,
          "min_n above every class: nothing usable");

    /* "no result" cases: NaN / inf input, no model, no output array */
    x[3] = nan_f();
    CHECK(ncc_distances(&m, x, 10, out) == 0, "NaN input gave distances");
    CHECK(out[0] == NCC_NO_DIST && out[1] == NCC_NO_DIST, "NaN input: %.3g %.3g", out[0], out[1]);
    x[3] = inf_f();
    CHECK(ncc_distances(&m, x, 10, out) == 0, "inf input gave distances");
    fill_offset(x, 0.0f);
    CHECK(ncc_distances(NULL, x, 10, out) == 0 && out[0] == NCC_NO_DIST, "NULL model");
    CHECK(ncc_distances(&m, NULL, 10, out) == 0 && out[1] == NCC_NO_DIST, "NULL vector");
    CHECK(ncc_distances(&m, x, 10, NULL) == 0, "NULL output array");
    static ncc_model_t empty;
    ncc_init(&empty, 0);
    CHECK(ncc_distances(&empty, x, 1, out) == 0, "zero-feature model");

    /* A corrupted-but-finite blob: distances are capped at 1e30, never
     * NaN, and still agree with ncc_classify (test_nonfinite's case). */
    static ncc_model_t big;
    ncc_init(&big, 4);
    for (int c = 0; c < 2; c++) {
        big.cls[c].n = 20;
        for (int f = 0; f < 4; f++) {
            big.cls[c].mean[f] = 1.0f;
            big.cls[c].m2[f]   = 1.0f;
        }
    }
    float y[4] = { -FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX };
    CHECK(ncc_distances(&big, y, 10, out) == 2, "extreme input");
    ncc_result_t r = ncc_classify(&big, y, 10, 1e9f, 1.0f);
    CHECK(out[0] == 1e30f && out[1] == 1e30f, "overflowed distances %.3g %.3g", out[0], out[1]);
    CHECK(r.dist == out[0] && r.second == out[1] && r.nearest == 0,
          "extreme input: classify %d %.3g %.3g", r.nearest, r.dist, r.second);
}

/* ------------------------------------------------------------------ */
/* ncc_valid, clear_class, names, init                                 */
/* ------------------------------------------------------------------ */

static void test_valid(void)
{
    static ncc_model_t good, m;

    build_two(&good);
    CHECK(ncc_valid(&good, NCC_MAX_FEAT), "good model rejected");
    CHECK(!ncc_valid(NULL, NCC_MAX_FEAT), "NULL accepted");

    m = good; m.magic ^= 1u;
    CHECK(!ncc_valid(&m, NCC_MAX_FEAT), "bad magic accepted");
    m = good; m.version = NCC_VERSION + 1;
    CHECK(!ncc_valid(&m, NCC_MAX_FEAT), "bad version accepted");
    m = good;
    CHECK(!ncc_valid(&m, 6), "nfeat mismatch accepted");
    m = good; m.nfeat = NCC_MAX_FEAT + 1;
    CHECK(!ncc_valid(&m, NCC_MAX_FEAT + 1), "nfeat > NCC_MAX_FEAT accepted");
    m = good;
    memset(m.cls[3].name, 'A', NCC_NAME_LEN);        /* unused slot, still checked */
    CHECK(!ncc_valid(&m, NCC_MAX_FEAT), "unterminated name accepted");
    m = good; m.cls[1].mean[17] = nan_f();
    CHECK(!ncc_valid(&m, NCC_MAX_FEAT), "NaN mean accepted");
    m = good; m.cls[0].m2[3] = inf_f();
    CHECK(!ncc_valid(&m, NCC_MAX_FEAT), "inf m2 accepted");
    m = good; m.cls[0].m2[0] = -1.0f;
    CHECK(!ncc_valid(&m, NCC_MAX_FEAT), "negative m2 accepted");

    /* an unused class (n == 0) is never read, so its numbers do not matter */
    m = good; m.cls[2].mean[0] = nan_f(); m.cls[2].m2[0] = -1.0f;
    CHECK(ncc_valid(&m, NCC_MAX_FEAT), "garbage in an unused class rejected");

    /* documented strictness: a zero-feature model is never valid */
    ncc_init(&m, 0);
    CHECK(!ncc_valid(&m, 0), "zero-feature model accepted");
}

static void test_clear_and_names(void)
{
    static ncc_model_t m, snap;
    static ncc_class_t zero;              /* all-zero reference */
    float x[NCC_MAX_FEAT];

    build_two(&m);
    fill_offset(x, 5.0f);
    for (int i = 0; i < 12; i++) ncc_add(&m, 2, x);
    ncc_set_name(&m, 2, "carol");
    CHECK(ncc_trained_classes(&m, 10) == 3, "before clear: %d", ncc_trained_classes(&m, 10));

    snap = m;
    ncc_clear_class(&m, 1);
    CHECK(memcmp(&m.cls[1], &zero, sizeof(zero)) == 0, "cleared class not all zero");
    CHECK(memcmp(&m.cls[0], &snap.cls[0], sizeof(ncc_class_t)) == 0, "clear touched class 0");
    CHECK(memcmp(&m.cls[2], &snap.cls[2], sizeof(ncc_class_t)) == 0, "clear touched class 2");
    CHECK(ncc_trained_classes(&m, 10) == 2, "after clear: %d", ncc_trained_classes(&m, 10));
    CHECK(ncc_valid(&m, NCC_MAX_FEAT), "model invalid after clear");
    snap = m;
    ncc_clear_class(&m, -1);
    ncc_clear_class(&m, NCC_MAX_CLASSES);
    CHECK(memcmp(&m, &snap, sizeof(m)) == 0, "out-of-range clear changed the model");

    /* names */
    CHECK(ncc_set_name(&m, 1, "raj"), "set_name refused 'raj'");
    CHECK(strcmp(m.cls[1].name, "raj") == 0, "name '%s'", m.cls[1].name);
    CHECK(ncc_set_name(&m, 1, "abcdefghijklmnopqrst"), "set_name refused a long name");
    CHECK(strlen(m.cls[1].name) == (size_t)(NCC_NAME_LEN - 1), "long name length %u",
          (unsigned)strlen(m.cls[1].name));
    CHECK(strcmp(m.cls[1].name, "abcdefghijklmno") == 0, "truncated to '%s'", m.cls[1].name);
    CHECK(ncc_set_name(&m, 1, "bo"), "set_name refused 'bo'");
    int stale = 0;
    for (int i = 2; i < NCC_NAME_LEN; i++) if (m.cls[1].name[i] != '\0') stale++;
    CHECK(stale == 0, "%d stale bytes after a shorter name", stale);
    CHECK(!ncc_set_name(&m, 1, ""), "empty name accepted");
    CHECK(!ncc_set_name(&m, 1, NULL), "NULL name accepted");
    CHECK(strcmp(m.cls[1].name, "bo") == 0, "rejected set_name changed the name to '%s'",
          m.cls[1].name);
    CHECK(!ncc_set_name(&m, -1, "x"), "class -1 accepted");
    CHECK(!ncc_set_name(&m, NCC_MAX_CLASSES, "x"), "class %d accepted", NCC_MAX_CLASSES);
    CHECK(ncc_valid(&m, NCC_MAX_FEAT), "model invalid after renames");

    /* init clamps nfeat and zeroes everything */
    ncc_init(&m, 40);
    CHECK(m.nfeat == NCC_MAX_FEAT, "nfeat 40 -> %d", (int)m.nfeat);
    CHECK(m.magic == NCC_MAGIC && m.version == NCC_VERSION, "init header");
    CHECK(ncc_trained_classes(&m, 1) == 0 && m.cls[0].name[0] == '\0', "init not empty");
    ncc_init(&m, -3);
    CHECK(m.nfeat == 0, "nfeat -3 -> %d", (int)m.nfeat);
    CHECK(!ncc_add(&m, 0, x), "add to a zero-feature model accepted");
}

int main(void)
{
    test_welford();
    test_clusters();
    test_margin_reject();
    test_min_n();
    test_single_class();
    test_nonfinite();
    test_distances();
    test_valid();
    test_clear_and_names();
    return test_summary("test_ncc");
}
