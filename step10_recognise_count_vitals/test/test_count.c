/*
 * test_count.c - host tests for count_est.c: one mover vs two on synthetic
 * group signals, a nearest-centroid separability check, block timing and
 * duty, the all-zero corner case, scale invariance, the one-pass float
 * covariance against a two-pass double reference, and the eigenvalue
 * features [1] [2] (power iteration + Rayleigh-Ritz, count_est.c) against a
 * full Jacobi (dsp_sym_eig).
 *
 * What passing means: the arithmetic is right and, on SYNTHETIC data where
 * the number of independent sources is known, the features respond to it.
 * It says nothing about whether a real room separates one person from two
 * - that is only known after `count-train 1` / `count-train 2` there.
 *
 * Links count_est.c + csi_dsp.c only (not csi_ncc.c), so the separability
 * check uses a few lines of nearest-centroid code of its own, with the same
 * metric csi_ncc.h describes (pooled per-feature std, floored at 1e-4, RMS
 * over the features).
 */

#include <string.h>
#include "test_util.h"
#include "count_est.h"

#define NB  COUNT_NWIN
#define NG  CSI_NGROUPS

/* ---- synthetic mover --------------------------------------------------- */

/* One mover's motion signal: an AR(1) "leaky random walk" with unit
 * variance, then the 0.5 s EMA subtraction dsp_group_dev applies (alpha
 * 0.04 at 50 packets/s), so the test sees the same kind of high-passed,
 * near-zero-mean signal the firmware feeds count_push().
 *
 * phi = 0.7 (correlation time ~3 packets) and not closer to 1 on purpose:
 * two INDEPENDENT slow signals still look correlated over a short block,
 * with a spurious sample correlation of ~1/sqrt(N_eff), and that pulls the
 * two-mover participation ratio down from 2 towards 1. At phi = 0.9 a
 * 256-packet block has N_eff ~ 14; at 0.7 it is ~45. Real motion at a
 * 50 Hz ping rate sits somewhere in that range, which is one reason
 * main.c votes over several blocks instead of trusting one. */
#define SRC_PHI    0.7f
#define SRC_INNOV  0.71414284f   /* sqrt(1 - phi^2): unit-variance AR(1) */
#define SRC_EMA_A  0.04f
#define NOISE_SD   0.2f          /* per group, in the same units          */

typedef struct {
    float v;
    float ema;
} src_t;

static float src_next(src_t *s)
{
    s->v = SRC_PHI * s->v + SRC_INNOV * rng_gauss();
    s->ema += SRC_EMA_A * (s->v - s->ema);
    return s->v - s->ema;
}

static void src_init(src_t *s)
{
    s->v   = 0.0f;
    s->ema = 0.0f;
    for (int i = 0; i < 200; i++) {
        (void)src_next(s);
    }
}

/* One block of group signals: in[t*NG + g] = ga*a[g]*s1(t) + gb*b[g]*s2(t)
 * + NOISE_SD * noise. b == NULL -> one mover. */
static void make_block(float *in, src_t *s1, const float a[NG], float ga,
                       src_t *s2, const float b[NG], float gb)
{
    for (int t = 0; t < NB; t++) {
        float x1 = src_next(s1);
        float x2 = b ? src_next(s2) : 0.0f;
        for (int g = 0; g < NG; g++) {
            float v = ga * a[g] * x1 + NOISE_SD * rng_gauss();
            if (b) v += gb * b[g] * x2;
            in[t * NG + g] = v;
        }
    }
}

/* Push one block (every packet "moving") into c. True if exactly one block
 * completed, and on the last packet. */
static bool feed_block(count_est_t *c, const float *in, float feat[COUNT_NFEAT])
{
    int done = 0, at = -1;
    for (int t = 0; t < NB; t++) {
        if (count_push(c, &in[t * NG], true, feat)) {
            done++;
            at = t;
        }
    }
    return done == 1 && at == NB - 1;
}

static int is_finite(float x)
{
    return x == x && (x - x) == 0.0f;
}

/* ---- (1) one mover vs two movers --------------------------------------- */

static void test_one_vs_two(void)
{
    /* one mover seen by all 8 groups, with mixed signs and strengths */
    static const float a1[NG] = { 1.0f, 0.8f, -0.6f, 0.9f, 0.5f, -0.7f, 1.1f, 0.4f };
    /* two movers: one mostly on groups 0-3, one mostly on 4-7, overlapping
     * in the middle (cosine between the two mixing vectors 0.18) */
    static const float ma[NG] = { 1.0f, 1.0f, 0.9f, 0.7f, 0.3f, 0.1f, 0.0f, 0.0f };
    static const float mb[NG] = { 0.0f, 0.0f, 0.1f, 0.3f, 0.7f, 0.9f, 1.0f, 1.0f };
    static float in[NB * NG];
    const int nblk = 8;
    count_est_t c;
    float feat[COUNT_NFEAT];
    src_t s1, s2;

    /* Seed 11 is simply the first one tried, not a picked one. Swept over
     * 200 seeds x 8 blocks (a scratch run, not part of this test): no
     * one-mover block reached 1.8, and 3 of the 1600 two-mover blocks dipped
     * to 1.73-1.77 - the spurious-correlation effect described at SRC_PHI,
     * and the reason one block's PR is a feature for a vote, not a count. */
    rng_seed(11);
    src_init(&s1);
    src_init(&s2);

    float lo1 = 99.0f, hi1 = 0.0f, sum1 = 0.0f;
    count_init(&c);
    for (int k = 0; k < nblk; k++) {
        make_block(in, &s1, a1, 1.0f, NULL, NULL, 0.0f);
        CHECK(feed_block(&c, in, feat), "1-mover block %d did not complete once at 255", k);
        CHECK(feat[0] < 1.8f, "1-mover block %d: PR %.3f, want < 1.8", k, feat[0]);
        if (feat[0] < lo1) lo1 = feat[0];
        if (feat[0] > hi1) hi1 = feat[0];
        sum1 += feat[0];
    }

    float lo2 = 99.0f, hi2 = 0.0f, sum2 = 0.0f;
    count_init(&c);
    for (int k = 0; k < nblk; k++) {
        make_block(in, &s1, ma, 1.0f, &s2, mb, 1.0f);
        CHECK(feed_block(&c, in, feat), "2-mover block %d did not complete once at 255", k);
        CHECK(feat[0] > 1.8f, "2-mover block %d: PR %.3f, want > 1.8", k, feat[0]);
        if (feat[0] < lo2) lo2 = feat[0];
        if (feat[0] > hi2) hi2 = feat[0];
        sum2 += feat[0];
    }

    printf("  (1) participation ratio over %d blocks each:\n", nblk);
    printf("      one mover : mean %.3f  min %.3f  max %.3f   (ideal 1, noise adds a little)\n",
           sum1 / (float)nblk, lo1, hi1);
    printf("      two movers: mean %.3f  min %.3f  max %.3f   (ideal 2, noise adds a little)\n",
           sum2 / (float)nblk, lo2, hi2);
}

/* ---- (2) separability with randomised mixing ---------------------------- */

/* A random mixing direction, scaled so the mean per-group power is 1: the
 * mover's overall strength is then the gain alone. */
static void rand_mix(float a[NG])
{
    float s = 0.0f;
    for (int g = 0; g < NG; g++) {
        a[g] = rng_gauss();
        s += a[g] * a[g];
    }
    float k = sqrtf((float)NG / s);
    for (int g = 0; g < NG; g++) a[g] *= k;
}

static const char *const k_feat_name[COUNT_NFEAT] = {
    "PR", "l2/l1", "l1/sum", "log10 tr", "duty", "burst"
};

static void test_separability(void)
{
    enum { NTRAIN = 20, NTEST = 10, NPER = NTRAIN + NTEST };
    static float in[NB * NG];
    static float fv[2][NPER][COUNT_NFEAT];
    count_est_t c;
    src_t s1, s2;
    float a[NG], b[NG];

    rng_seed(2024);
    src_init(&s1);
    src_init(&s2);
    count_init(&c);

    /* Every block draws fresh random mixing directions and a gain that is
     * log-uniform over 0.7..3 (a ~20x spread in power), so log10(trace)
     * varies a lot WITHIN each class and cannot carry the label by itself.
     * Every packet is "moving", so duty is exactly 1 in both classes and
     * contributes nothing either.
     *
     * The second mover's gain is kept within 1.5x of the first. That is a
     * real limit of these features, not test tuning: at a 3:1 amplitude
     * ratio the two eigenvalues are 9:1, PR ~ 1.2, and a weak second mover
     * looks like one mover - in a room as in this test.
     *
     * Seed 2024 is the first one tried. Its 18/20 is at the low end: over
     * 200 seeds (a scratch run) held-out accuracy averaged 96% and 8 seeds
     * fell below 90% (16-17/20). With this seed both misses are two-mover
     * blocks read as one. That is the expected failure: a second mover
     * whose mixing direction lies close to the first's, or whose signal
     * happens to correlate with it over the block, leaves one dominant
     * eigenvalue, and no eigenvalue feature can undo that. */
    for (int cls = 0; cls < 2; cls++) {
        for (int k = 0; k < NPER; k++) {
            float ga = 0.7f * powf(3.0f / 0.7f, rng_uniform());
            rand_mix(a);
            if (cls == 0) {
                make_block(in, &s1, a, ga, NULL, NULL, 0.0f);
            } else {
                float gb = ga * powf(1.5f, 2.0f * rng_uniform() - 1.0f);
                rand_mix(b);
                make_block(in, &s1, a, ga, &s2, b, gb);
            }
            CHECK(feed_block(&c, in, fv[cls][k]), "class %d block %d timing", cls, k);
        }
    }

    /* nearest centroid, trained on the first NTRAIN blocks of each class */
    double mean[2][COUNT_NFEAT], sd[COUNT_NFEAT];
    for (int f = 0; f < COUNT_NFEAT; f++) {
        double ss = 0.0;
        for (int cls = 0; cls < 2; cls++) {
            double m = 0.0;
            for (int k = 0; k < NTRAIN; k++) m += fv[cls][k][f];
            m /= (double)NTRAIN;
            mean[cls][f] = m;
            for (int k = 0; k < NTRAIN; k++) {
                double d = fv[cls][k][f] - m;
                ss += d * d;
            }
        }
        sd[f] = sqrt(ss / (double)(2 * (NTRAIN - 1)));
        if (sd[f] < 1e-4) sd[f] = 1e-4;
    }

    int conf[2][2] = { { 0, 0 }, { 0, 0 } };   /* [true][predicted] */
    for (int cls = 0; cls < 2; cls++) {
        for (int k = NTRAIN; k < NPER; k++) {
            double d2[2];
            for (int m = 0; m < 2; m++) {
                double s = 0.0;
                for (int f = 0; f < COUNT_NFEAT; f++) {
                    double z = (fv[cls][k][f] - mean[m][f]) / sd[f];
                    s += z * z;
                }
                d2[m] = s / (double)COUNT_NFEAT;
            }
            conf[cls][d2[1] < d2[0] ? 1 : 0]++;
        }
    }
    int correct = conf[0][0] + conf[1][1];

    printf("  (2) class means over %d training blocks (pooled std):\n", NTRAIN);
    for (int f = 0; f < COUNT_NFEAT; f++) {
        printf("      %-9s one %7.3f   two %7.3f   (sd %.3f)\n", k_feat_name[f],
               mean[0][f], mean[1][f], sd[f]);
    }
    printf("      confusion on %d held-out blocks per class (rows = truth):\n", NTEST);
    printf("                 pred one  pred two\n");
    printf("      true one   %8d  %8d\n", conf[0][0], conf[0][1]);
    printf("      true two   %8d  %8d\n", conf[1][0], conf[1][1]);
    printf("      accuracy %d/%d = %.0f%%  (synthetic data - not a room result)\n",
           correct, 2 * NTEST, 100.0 * (double)correct / (double)(2 * NTEST));
    CHECK(correct * 10 >= 9 * 2 * NTEST, "held-out accuracy %d/%d < 90%%", correct, 2 * NTEST);
}

/* ---- (3) block timing, duty, reset -------------------------------------- */

static void test_blocks_and_duty(void)
{
    static float in[NB * NG], junk[NB * NG];
    count_est_t c;
    float feat[COUNT_NFEAT], ref[COUNT_NFEAT], again[COUNT_NFEAT];

    rng_seed(5);
    for (int i = 0; i < NB * NG; i++) {
        in[i]   = rng_gauss();
        junk[i] = 3.0f * rng_gauss() + 1.0f;
    }

    /* three blocks with known moving patterns: blocks complete at packet
     * 255, 511 and 767 exactly, and nowhere else */
    const int want_moving[3] = { 64, 77, 0 };
    int blocks = 0, bad_at = 0;
    count_init(&c);
    for (int p = 0; p < 3 * NB; p++) {
        int  t = p % NB, blk = p / NB;
        bool mv;
        if (blk == 0)      mv = (t % 4 == 0);   /* 64 of 256 */
        else if (blk == 1) mv = (t < 77);        /* 77 of 256 */
        else               mv = false;
        if (count_push(&c, &in[t * NG], mv, feat)) {
            if (t != NB - 1) bad_at++;
            /* k/256 is exact in float, so the duty must be exactly equal */
            CHECK(feat[4] == (float)want_moving[blocks] / (float)NB,
                  "block %d duty %.8f, want %d/256", blocks, feat[4], want_moving[blocks]);
            blocks++;
        }
    }
    CHECK(blocks == 3 && bad_at == 0, "%d blocks, %d at the wrong packet", blocks, bad_at);
    printf("  (3) 768 packets -> %d blocks, all ending on packet 255 mod 256; duty exact\n",
           blocks);

    /* No carry-over: a block pushed after another block, or after a
     * count_reset() mid-block, gives bit-identical features to the same
     * block pushed into a fresh estimator. */
    count_init(&c);
    CHECK(feed_block(&c, in, ref), "fresh block timing");
    count_init(&c);
    CHECK(feed_block(&c, junk, feat), "junk block timing");
    CHECK(feed_block(&c, in, again), "block-after-block timing");
    CHECK(memcmp(ref, again, sizeof(ref)) == 0, "features carried over from the previous block");
    for (int t = 0; t < 100; t++) {
        CHECK(!count_push(&c, &junk[t * NG], true, feat), "early block at %d", t);
    }
    count_reset(&c);
    CHECK(feed_block(&c, in, again), "block-after-reset timing");
    CHECK(memcmp(ref, again, sizeof(ref)) == 0, "count_reset() did not clear the partial block");
}

/* ---- (4) all-zero input -------------------------------------------------- */

static void test_all_zero(void)
{
    static const float zero[NG] = { 0 };
    count_est_t c;
    float feat[COUNT_NFEAT];
    int   done = 0;

    count_init(&c);
    for (int t = 0; t < NB; t++) {
        if (count_push(&c, zero, false, feat)) done++;
    }
    CHECK(done == 1, "zero input: %d blocks", done);
    for (int f = 0; f < COUNT_NFEAT; f++) {
        CHECK(is_finite(feat[f]), "zero input: feature %d not finite", f);
    }
    CHECK_NEAR(feat[0], 1.0, 0.0, "zero input: PR");
    CHECK_NEAR(feat[1], 0.0, 0.0, "zero input: l2/l1");
    CHECK_NEAR(feat[2], 0.0, 0.0, "zero input: l1/sum");
    CHECK_NEAR(feat[3], -6.0, 1e-4, "zero input: log10(trace + 1e-6)");
    CHECK_NEAR(feat[4], 0.0, 0.0, "zero input: duty");
    CHECK_NEAR(feat[5], 0.0, 0.0, "zero input: burstiness");
    printf("  (4) all-zero block -> PR %.3f  l2/l1 %.3f  l1/sum %.3f  log10tr %.5f  duty %.3f  burst %.3f\n",
           feat[0], feat[1], feat[2], feat[3], feat[4], feat[5]);
}

/* ---- (5) scale invariance ------------------------------------------------ */

static void test_scale(void)
{
    static const float ma[NG] = { 1.0f, 1.0f, 0.9f, 0.7f, 0.3f, 0.1f, 0.0f, 0.0f };
    static const float mb[NG] = { 0.0f, 0.0f, 0.1f, 0.3f, 0.7f, 0.9f, 1.0f, 1.0f };
    static float in[NB * NG], in10[NB * NG];
    count_est_t c;
    float f1[COUNT_NFEAT], f10[COUNT_NFEAT];
    src_t s1, s2;

    rng_seed(99);
    src_init(&s1);
    src_init(&s2);
    make_block(in, &s1, ma, 1.0f, &s2, mb, 0.8f);
    for (int i = 0; i < NB * NG; i++) in10[i] = 10.0f * in[i];

    count_init(&c);
    CHECK(feed_block(&c, in, f1), "x1 block timing");
    CHECK(feed_block(&c, in10, f10), "x10 block timing");

    /* count_est.c divides C by its largest diagonal entry before anything
     * else, and the power iteration's stopping test is relative, so the
     * eigenvalue ratios are scale-free up to float rounding; trace scales
     * by 100. */
    const int inv[4] = { 0, 1, 2, 5 };
    float worst = 0.0f;
    for (int i = 0; i < 4; i++) {
        int   f = inv[i];
        float r = fabsf(f10[f] - f1[f]) / (fabsf(f1[f]) + 1e-6f);
        if (r > worst) worst = r;
        CHECK(r < 1e-4f, "%s changed with scale: %.6f -> %.6f", k_feat_name[f], f1[f], f10[f]);
    }
    CHECK_NEAR(f10[3] - f1[3], 2.0, 1e-4, "log10(trace) shift for x10");
    CHECK(f10[4] == f1[4], "duty changed with scale");
    printf("  (5) x10 input: PR %.5f -> %.5f, burst %.5f -> %.5f, worst rel change %.2g;"
           " log10tr %.5f -> %.5f (shift %.5f)\n",
           f1[0], f10[0], f1[5], f10[5], worst, f1[3], f10[3], f10[3] - f1[3]);
}

/* ---- one-pass float vs two-pass double ---------------------------------- */

/* Reference features in double, two-pass (mean first, then deviations).
 * No eigensolver needed for PR: for a symmetric C, sum(lambda) = trace(C)
 * and sum(lambda^2) = ||C||_F^2, so PR = trace^2 / sum_ij C_ij^2. */
static void ref_features(const float *in, double *pr, double *logtr, double *burst)
{
    double m[NG], cov[NG][NG];
    for (int g = 0; g < NG; g++) {
        double s = 0.0;
        for (int t = 0; t < NB; t++) s += in[t * NG + g];
        m[g] = s / (double)NB;
    }
    double tr = 0.0, fro = 0.0;
    for (int i = 0; i < NG; i++) {
        for (int j = 0; j < NG; j++) {
            double s = 0.0;
            for (int t = 0; t < NB; t++) {
                s += ((double)in[t * NG + i] - m[i]) * ((double)in[t * NG + j] - m[j]);
            }
            cov[i][j] = s / (double)(NB - 1);
            fro += cov[i][j] * cov[i][j];
        }
        tr += cov[i][i];
    }
    *pr    = (fro > 0.0) ? tr * tr / fro : 1.0;
    *logtr = log10(tr + 1e-6);

    double es = 0.0, e2 = 0.0;
    for (int t = 0; t < NB; t++) {
        double e = 0.0;
        for (int g = 0; g < NG; g++) e += (double)in[t * NG + g] * (double)in[t * NG + g];
        es += e;
        e2 += e * e;
    }
    double me = es / (double)NB, var = e2 / (double)NB - me * me;
    *burst = (me > 0.0) ? sqrt(var > 0.0 ? var : 0.0) / me : 0.0;
}

static void test_vs_reference(void)
{
    static const float ma[NG] = { 1.0f, 1.0f, 0.9f, 0.7f, 0.3f, 0.1f, 0.0f, 0.0f };
    static const float mb[NG] = { 0.0f, 0.0f, 0.1f, 0.3f, 0.7f, 0.9f, 1.0f, 1.0f };
    static float in[NB * NG], off[NB * NG];
    count_est_t c;
    float  feat[COUNT_NFEAT];
    double pr, lt, bu;
    src_t  s1, s2;

    rng_seed(314);
    src_init(&s1);
    src_init(&s2);
    make_block(in, &s1, ma, 1.5f, &s2, mb, 1.2f);

    /* plain block: the regime dsp_group_dev produces (near-zero mean) */
    ref_features(in, &pr, &lt, &bu);
    count_init(&c);
    CHECK(feed_block(&c, in, feat), "reference block timing");
    double e_pr = fabs(feat[0] - pr) / pr;
    double e_lt = fabs(feat[3] - lt);
    double e_bu = fabs(feat[5] - bu) / bu;
    CHECK(e_pr < 1e-3, "PR %.6f vs double two-pass %.6f", feat[0], pr);
    CHECK(e_lt < 1e-4, "log10tr %.6f vs double two-pass %.6f", feat[3], lt);
    CHECK(e_bu < 1e-3, "burst %.6f vs double %.6f", feat[5], bu);
    printf("  (ref) vs two-pass double: PR %.5f/%.5f (rel %.1e), log10tr abs %.1e, burst rel %.1e\n",
           feat[0], pr, e_pr, e_lt, e_bu);
    printf("  (ref) all six features: %.6f %.6f %.6f %.6f %.6f %.6f\n",
           feat[0], feat[1], feat[2], feat[3], feat[4], feat[5]);

    /* Same block plus a DC offset of 3 x each group's own std. Covariance,
     * and so PR and trace, must not move; this is where a one-pass formula
     * starts to cancel, and it must still hold at a moderate offset. */
    double sdg[NG];
    for (int g = 0; g < NG; g++) {
        double acc = 0.0, acc2 = 0.0;
        for (int t = 0; t < NB; t++) {
            acc  += in[t * NG + g];
            acc2 += (double)in[t * NG + g] * (double)in[t * NG + g];
        }
        double mm = acc / (double)NB;
        sdg[g] = sqrt(acc2 / (double)NB - mm * mm);
    }
    for (int t = 0; t < NB; t++) {
        for (int g = 0; g < NG; g++) {
            off[t * NG + g] = in[t * NG + g] + (float)(3.0 * sdg[g]);
        }
    }
    ref_features(off, &pr, &lt, &bu);
    CHECK(feed_block(&c, off, feat), "offset block timing");
    e_pr = fabs(feat[0] - pr) / pr;
    e_lt = fabs(feat[3] - lt);
    CHECK(e_pr < 1e-3, "offset: PR %.6f vs double two-pass %.6f", feat[0], pr);
    CHECK(e_lt < 1e-4, "offset: log10tr %.6f vs double two-pass %.6f", feat[3], lt);
    printf("  (ref) +3 sd DC offset:     PR %.5f/%.5f (rel %.1e), log10tr abs %.1e\n",
           feat[0], pr, e_pr, e_lt);

    /* Informational only, no check: a 100 sd offset is what the one-pass
     * form cannot handle (count_est.c says so) and dsp_group_dev never
     * produces. Printed so the size of that caveat is on record. */
    for (int t = 0; t < NB; t++) {
        for (int g = 0; g < NG; g++) {
            off[t * NG + g] = in[t * NG + g] + (float)(100.0 * sdg[g]);
        }
    }
    ref_features(off, &pr, &lt, &bu);
    CHECK(feed_block(&c, off, feat), "large-offset block timing");
    printf("  (info) +100 sd DC offset: PR %.5f/%.5f (rel %.1e) - outside the design regime\n",
           feat[0], pr, fabs(feat[0] - pr) / pr);
}

/* ---- (6) features [1] [2] against a full eigen-decomposition ----------- */

/* lambda_2 / lambda_1 and lambda_1 / sum(lambda) from Jacobi on the
 * two-pass double covariance, rounded to float once. */
static void ref_eig_ratios(const float *in, double *r21, double *r1)
{
    double m[NG];
    float  cov[NG * NG], lam[NG];
    for (int g = 0; g < NG; g++) {
        double s = 0.0;
        for (int t = 0; t < NB; t++) s += in[t * NG + g];
        m[g] = s / (double)NB;
    }
    for (int i = 0; i < NG; i++) {
        for (int j = 0; j < NG; j++) {
            double s = 0.0;
            for (int t = 0; t < NB; t++) {
                s += ((double)in[t * NG + i] - m[i]) * ((double)in[t * NG + j] - m[j]);
            }
            cov[i * NG + j] = (float)(s / (double)(NB - 1));
        }
    }
    dsp_sym_eig(cov, NG, lam, NULL);
    double sum = 0.0;
    for (int i = 0; i < NG; i++) sum += lam[i] > 0.0f ? (double)lam[i] : 0.0;
    *r21 = (double)(lam[1] > 0.0f ? lam[1] : 0.0f) / (double)lam[0];
    *r1  = (double)lam[0] / sum;
}

/*
 * count_est.c no longer runs a full eigen-decomposition: lambda_1 and
 * lambda_2 come from two power iterations and a 2x2 Rayleigh-Ritz step.
 * How close is that to Jacobi, block by block?
 *   - one mover: lambda_1 is exact to float; lambda_2 sits in the cluster
 *     of near-equal noise eigenvalues, where the power iteration stops at
 *     its cap with a Rayleigh quotient somewhat BELOW the top of the
 *     cluster (count_est.c, COUNT_EIG_ITERS). With this seed: up to ~7%
 *     of lambda_2, ~1e-3 absolute on [1] - against a gap of ~0.4 between
 *     the one- and two-mover class means in (2). Asserted: 2e-3 absolute
 *     on [1], twice the worst seen and 1/200 of that gap; 1e-4 on [2].
 *   - two movers, including nearly equal ones (lambda_2 / lambda_1 up to
 *     ~0.95): the Rayleigh-Ritz step makes both ratios float-exact
 *     whenever the third eigenvalue is well below the second, which the
 *     noise floor ensures. Asserted: 1e-4 on both.
 * Mixing directions and gains are random, as in (2).
 */
static void test_vs_jacobi(void)
{
    static float in[NB * NG];
    count_est_t c;
    float feat[COUNT_NFEAT];
    src_t s1, s2;
    float a[NG], b[NG];
    const char *const cls_name[3] = { "one mover", "two movers", "two equal movers" };

    rng_seed(606);
    src_init(&s1);
    src_init(&s2);
    count_init(&c);
    printf("  (6) [1] l2/l1 and [2] l1/sum vs Jacobi, 40 blocks per case:\n");
    for (int cls = 0; cls < 3; cls++) {
        double w21 = 0.0, w21_rel = 0.0, w1 = 0.0, r21_max = 0.0;
        for (int k = 0; k < 40; k++) {
            float ga = 0.7f * powf(3.0f / 0.7f, rng_uniform());
            rand_mix(a);
            if (cls == 0) {
                make_block(in, &s1, a, ga, NULL, NULL, 0.0f);
            } else {
                float gb = (cls == 1) ? ga * powf(1.5f, 2.0f * rng_uniform() - 1.0f) : ga;
                rand_mix(b);
                make_block(in, &s1, a, ga, &s2, b, gb);
            }
            CHECK(feed_block(&c, in, feat), "(6) block timing");
            double r21, r1;
            ref_eig_ratios(in, &r21, &r1);
            double e21 = fabs((double)feat[1] - r21), e1 = fabs((double)feat[2] - r1);
            w21     = fmax(w21, e21);
            w21_rel = fmax(w21_rel, e21 / fmax(r21, 1e-12));
            w1      = fmax(w1, e1);
            r21_max = fmax(r21_max, r21);
            CHECK(e21 <= (cls == 0 ? 2e-3 : 1e-4), "(6) %s block %d: l2/l1 %.6f vs Jacobi %.6f",
                  cls_name[cls], k, feat[1], r21);
            CHECK(e1 <= 1e-4, "(6) %s block %d: l1/sum %.6f vs Jacobi %.6f",
                  cls_name[cls], k, feat[2], r1);
        }
        printf("      %-16s  l2/l1 max abs err %.1e (rel %.1e)  l1/sum max abs err %.1e"
               "  (largest Jacobi l2/l1 %.3f)\n", cls_name[cls], w21, w21_rel, w1, r21_max);
    }
}

int main(void)
{
    test_one_vs_two();
    test_separability();
    test_blocks_and_duty();
    test_all_zero();
    test_scale();
    test_vs_reference();
    test_vs_jacobi();
    return test_summary("test_count");
}
