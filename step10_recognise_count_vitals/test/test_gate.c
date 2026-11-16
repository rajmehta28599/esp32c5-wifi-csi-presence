/*
 * test_gate.c - the vitals motion gate (motion_gate.c) on synthetic group
 * signals with known content.
 *
 * Units: every signal is in units of the per-GROUP noise standard
 * deviation (the noise of one grp[] value from dsp_group_dev), since that
 * is what the gate sees. On the device a group averages ~6-7 subcarriers,
 * so one group-noise unit is ~0.4 of the per-subcarrier noise that
 * test_e2e's amplitudes are quoted in.
 *
 * The signals go into the gate RAW. On the device grp has already been
 * high-passed at ~0.32 Hz by the 0.5 s fast EMA, which removes ~40% of a
 * 0.25 Hz breath before the gate sees it; feeding breathing unfiltered is
 * deliberately harsher than the device.
 *
 * Packets: 3% dropped, +/-2 ms arrival jitter, as in test_e2e.
 *
 * What this shows: the gate's arithmetic separates slow (breathing) from
 * fast (walking-like) modulation of synthetic signals, learns its floor
 * only when told the room is empty, and survives gaps and NaNs. It says
 * nothing about how large real walking or real breathing is on real CSI -
 * that is what the JSON "hf" value is for on the device.
 */

#include <string.h>
#include <stdbool.h>

#include "motion_gate.h"
#include "test_util.h"

#define TWO_PI_D     6.283185307179586
#define DROP_P       0.03f
#define JITTER_US    2000
#define THR          4.0f          /* CSI_VITALS_MOTION_RATIO_X100 default 400 */
#define NSIN         16            /* sinusoids per group in the walking signal */

/* ================================================================== */
/*  Synthetic source                                                   */
/* ================================================================== */
typedef struct {
    float   rate;
    long    slot;                  /* nominal packet slots since start      */
    int64_t t_us;                  /* timestamp of the last delivered packet */
    int64_t t_off_us;              /* added to every timestamp (gap tests)  */
    mgate_t g;
} sim_t;

typedef struct {
    float br_amp, br_hz;           /* breathing, identical in every group   */
    float walk_k;                  /* walking RMS, group-noise units; 0 none */
    bool  room_empty;              /* what main.c would pass: !present      */
} scene_t;

/* Walking-like: per group a sum of NSIN sinusoids at random frequencies in
 * 2-15 Hz (torso and limb Doppler), amplitude-modulated at a 1.8 Hz step
 * rate so the energy comes in bursts the way steps do. Scaled to RMS 1. */
static float s_wf[CSI_NGROUPS][NSIN], s_wp[CSI_NGROUPS][NSIN];

static void walk_draw(void)
{
    for (int g = 0; g < CSI_NGROUPS; g++) {
        for (int i = 0; i < NSIN; i++) {
            s_wf[g][i] = 2.0f + 13.0f * rng_uniform();
            s_wp[g][i] = (float)TWO_PI_D * rng_uniform();
        }
    }
}

static float walk_value(int g, double t)
{
    double v = 0.0;
    for (int i = 0; i < NSIN; i++) {
        v += sin(TWO_PI_D * (double)s_wf[g][i] * t + (double)s_wp[g][i]);
    }
    /* sum of NSIN unit sinusoids has power NSIN/2; the envelope's mean
     * square is 1 + 0.6^2/2 = 1.18 */
    double env = 1.0 + 0.6 * sin(TWO_PI_D * 1.8 * t);
    return (float)(v * sqrt(2.0 / (double)NSIN) * env / sqrt(1.18));
}

/* A chest is not a sinusoid: 2nd and 3rd harmonics as in test_e2e. */
static float breath_value(const scene_t *sc, double t)
{
    double w = TWO_PI_D * (double)sc->br_hz * t;
    return (float)((double)sc->br_amp * (sin(w) + 0.3 * sin(2.0 * w + 1.0) + 0.15 * sin(3.0 * w + 2.0)));
}

static void sim_init(sim_t *s, float rate)
{
    memset(s, 0, sizeof(*s));
    s->rate = rate;
    mgate_init(&s->g, rate, THR);
}

static double sim_now_s(const sim_t *s)
{
    return (double)s->slot / (double)s->rate;
}

/* Advance to the next DELIVERED packet and push it. Returns the gate's
 * decision; *t_s gets the packet's nominal time in seconds. */
static bool sim_step(sim_t *s, const scene_t *sc, double *t_s)
{
    float grp[CSI_NGROUPS];
    do {
        s->slot++;
    } while (rng_uniform() < DROP_P);

    double  t       = sim_now_s(s);
    int64_t nominal = (int64_t)(t * 1e6) + 1000000;       /* never 0 */
    int64_t jit     = (int64_t)((rng_uniform() * 2.0f - 1.0f) * (float)JITTER_US);
    int64_t t_us    = nominal + jit + s->t_off_us;
    if (t_us <= s->t_us) t_us = s->t_us + 1;             /* arrival order kept */
    s->t_us = t_us;

    float b = (sc->br_amp > 0.0f) ? breath_value(sc, t) : 0.0f;
    for (int g = 0; g < CSI_NGROUPS; g++) {
        float w = (sc->walk_k > 0.0f) ? sc->walk_k * walk_value(g, t) : 0.0f;
        grp[g] = rng_gauss() + b + w;
    }
    *t_s = t;
    return mgate_push(&s->g, t_us, grp, sc->room_empty);
}

/* ================================================================== */
/*  Run a scene and summarise it                                       */
/* ================================================================== */
typedef struct {
    double first_still, first_moving;  /* s after the run began, -1 = never */
    double last_unknown;               /* last time the ratio was unknown   */
    int    to_still, to_moving;        /* transitions during the run        */
    long   pk, still_pk;               /* packets after settle_s            */
    float  r_min, r_max;               /* ratio over packets after settle_s */
    double r_sum;
    long   r_n;
    bool   ever_have_empty;
} run_t;

static run_t run(sim_t *s, const scene_t *sc, double dur_s, double settle_s)
{
    run_t  o;
    double t0   = sim_now_s(s), t = t0;
    bool   prev = mgate_still(&s->g);

    memset(&o, 0, sizeof(o));
    o.first_still  = -1.0;
    o.first_moving = -1.0;
    o.last_unknown = -1.0;
    o.r_min        = 1e30f;
    o.r_max        = -1.0f;
    while (t - t0 < dur_s) {
        bool   still = sim_step(s, sc, &t);
        double rel   = t - t0;
        float  r     = mgate_ratio(&s->g);
        if (still && o.first_still < 0.0) o.first_still = rel;
        if (!still && o.first_moving < 0.0) o.first_moving = rel;
        if (still != prev) {
            if (still) o.to_still++;
            else       o.to_moving++;
        }
        prev = still;
        if (r < 0.0f) o.last_unknown = rel;
        if (s->g.have_empty) o.ever_have_empty = true;
        if (rel >= settle_s) {
            o.pk++;
            if (still) o.still_pk++;
            if (r >= 0.0f) {
                if (r < o.r_min) o.r_min = r;
                if (r > o.r_max) o.r_max = r;
                o.r_sum += (double)r;
                o.r_n++;
            }
        }
    }
    if (o.r_n == 0) o.r_min = -1.0f;
    return o;
}

static double r_mean(const run_t *o)
{
    return o->r_n ? o->r_sum / (double)o->r_n : -1.0;
}

static const float k_rates[2] = { 50.0f, 100.0f };

/* ================================================================== */
/*  (a) white noise, empty room                                        */
/* ================================================================== */
static void test_noise(void)
{
    printf("(a) white noise only, room empty, 30 s (group noise sd = 1 unit)\n");
    for (int ri = 0; ri < 2; ri++) {
        sim_t   s;
        scene_t sc = { 0.0f, 0.0f, 0.0f, true };
        rng_seed(100u + (uint32_t)ri);
        sim_init(&s, k_rates[ri]);
        run_t o = run(&s, &sc, 30.0, 3.0);
        printf("    %3.0f Hz: first STILL at %.2f s; after 3 s still %ld/%ld packets; "
               "ratio mean %.2f min %.2f max %.2f; floor_empty %.3f\n",
               (double)k_rates[ri], o.first_still, o.still_pk, o.pk, r_mean(&o),
               (double)o.r_min, (double)o.r_max, (double)s.g.floor_empty);
        CHECK(o.first_still > 0.0 && o.first_still <= 2.5,
              "%.0f Hz: first STILL at %.2f s, want 1 s bin + 1 s hold", (double)k_rates[ri], o.first_still);
        CHECK(o.still_pk == o.pk, "%.0f Hz: only %ld/%ld noise packets STILL", (double)k_rates[ri],
              o.still_pk, o.pk);
        CHECK(r_mean(&o) >= 0.7 && r_mean(&o) <= 1.3, "%.0f Hz: noise ratio mean %.2f, want ~1",
              (double)k_rates[ri], r_mean(&o));
        CHECK(o.r_max < THR, "%.0f Hz: noise ratio reached %.2f", (double)k_rates[ri], (double)o.r_max);
        CHECK(o.to_moving == 0, "%.0f Hz: %d STILL -> MOVING flips on noise", (double)k_rates[ri], o.to_moving);
    }
}

/* ================================================================== */
/*  (b) breathing: must stay STILL                                     */
/* ================================================================== */
static void test_breathing(void)
{
    static const float amps[] = { 1.0f, 2.0f, 4.0f, 8.0f };

    printf("(b) floor learned in 10 s of empty room, then a still person breathing\n"
           "    (coherent in all 8 groups, 2nd/3rd harmonics 0.3/0.15, NOT pre-filtered),\n"
           "    room not empty, 60 s. Must stay STILL.\n");
    for (int ri = 0; ri < 2; ri++) {
        for (size_t ai = 0; ai < sizeof(amps) / sizeof(amps[0]); ai++) {
            for (int fi = 0; fi < 2; fi++) {
                /* 15/min asserted; 30/min (the band edge, harmonics at 1.0
                 * and 1.5 Hz - next to the gate's 1.6 Hz corner) printed */
                float   hz = fi ? 0.5f : 0.25f;
                sim_t   s;
                scene_t empty = { 0.0f, 0.0f, 0.0f, true };
                scene_t sc    = { amps[ai], hz, 0.0f, false };
                rng_seed(200u + (uint32_t)(ri * 16 + (int)ai * 2 + fi));
                sim_init(&s, k_rates[ri]);
                (void)run(&s, &empty, 10.0, 0.0);
                bool  was_still = mgate_still(&s.g);
                run_t o = run(&s, &sc, 60.0, 0.0);
                printf("    %3.0f Hz, %2.0f/min, amplitude %.0fx: still %ld/%ld packets, "
                       "ratio mean %.2f max %.2f%s\n",
                       (double)k_rates[ri], (double)(hz * 60.0f), (double)amps[ai], o.still_pk, o.pk,
                       r_mean(&o), (double)o.r_max, fi ? "   (info)" : "");
                if (fi == 0) {
                    CHECK(was_still, "%.0f Hz: not STILL after 10 s of empty room", (double)k_rates[ri]);
                    CHECK(o.still_pk == o.pk && o.to_moving == 0,
                          "%.0f Hz breathing %.0fx: MOVING on %ld packets (%d flips)",
                          (double)k_rates[ri], (double)amps[ai], o.pk - o.still_pk, o.to_moving);
                    CHECK(o.r_max < THR, "%.0f Hz breathing %.0fx: ratio reached %.2f",
                          (double)k_rates[ri], (double)amps[ai], (double)o.r_max);
                }
            }
        }
    }
}

/* ================================================================== */
/*  (c) walking starts, (d) walking ends                               */
/* ================================================================== */
static void test_walking(void)
{
    static const float ks[] = { 1.0f, 2.0f, 5.0f };

    printf("(c)+(d) floor learned (10 s empty), 5 s still, then 8 s of walking-like\n"
           "    2-15 Hz modulation (step-rate bursts) at RMS k x group noise, then the\n"
           "    walker stands still (room not empty from the walk on)\n");
    for (int ri = 0; ri < 2; ri++) {
        for (size_t ki = 0; ki < sizeof(ks) / sizeof(ks[0]); ki++) {
            sim_t   s;
            scene_t empty = { 0.0f, 0.0f, 0.0f, true };
            scene_t quiet = { 0.0f, 0.0f, 0.0f, false };
            scene_t walk  = { 0.0f, 0.0f, ks[ki], false };
            rng_seed(300u + (uint32_t)(ri * 8 + (int)ki));
            walk_draw();
            sim_init(&s, k_rates[ri]);
            (void)run(&s, &empty, 10.0, 0.0);
            (void)run(&s, &quiet, 5.0, 0.0);
            bool  pre_still = mgate_still(&s.g);
            float floor0    = s.g.floor_empty;
            run_t w = run(&s, &walk, 8.0, 1.0);
            run_t q = run(&s, &quiet, 6.0, 0.0);
            bool  detected = w.first_moving >= 0.0;
            char  det[64], back[64];
            if (detected) snprintf(det, sizeof(det), "MOVING after %.2f s", w.first_moving);
            else          snprintf(det, sizeof(det), "NOT DETECTED");
            if (detected && q.first_still >= 0.0) snprintf(back, sizeof(back), "STILL %.2f s after it ends", q.first_still);
            else if (detected)                    snprintf(back, sizeof(back), "never STILL again (6 s)");
            else                                  snprintf(back, sizeof(back), "-");
            printf("    %3.0f Hz, walking %.0fx: %-20s ratio mean %6.2f min %6.2f; "
                   "moving %ld/%ld after 1 s; %s\n",
                   (double)k_rates[ri], (double)ks[ki], det, r_mean(&w), (double)w.r_min,
                   w.pk - w.still_pk, w.pk, back);

            CHECK(pre_still, "%.0f Hz: not STILL before the walk", (double)k_rates[ri]);
            /* the floor is learnt from empty-room seconds only */
            CHECK(s.g.floor_empty == floor0, "%.0f Hz walk %.0fx: floor_empty moved %.4f -> %.4f "
                  "while the room was not empty", (double)k_rates[ri], (double)ks[ki],
                  (double)floor0, (double)s.g.floor_empty);
            if (ks[ki] >= 5.0f) {
                CHECK(detected && w.first_moving <= 0.5, "%.0f Hz walk 5x: MOVING after %.2f s, want <= 0.5",
                      (double)k_rates[ri], w.first_moving);
            } else if (ks[ki] >= 2.0f) {
                CHECK(detected && w.first_moving <= 1.0, "%.0f Hz walk 2x: MOVING after %.2f s, want <= 1.0",
                      (double)k_rates[ri], w.first_moving);
            }
            if (ks[ki] >= 2.0f) {
                /* once moving, it must not drop back to STILL mid-walk */
                CHECK(w.to_still == 0, "%.0f Hz walk %.0fx: %d MOVING -> STILL flips mid-walk",
                      (double)k_rates[ri], (double)ks[ki], w.to_still);
                /* es decays (tau 0.5 s) from the walking level, then the
                 * 1 s hold: ~1 s after a weak walk, ~2.5 s after a strong one */
                CHECK(q.first_still >= 0.0 && q.first_still <= 3.0,
                      "%.0f Hz walk %.0fx: STILL again after %.2f s, want <= 3",
                      (double)k_rates[ri], (double)ks[ki], q.first_still);
                CHECK(q.first_still >= 1.0, "%.0f Hz walk %.0fx: STILL again after only %.2f s - the "
                      "1 s hold was skipped", (double)k_rates[ri], (double)ks[ki], q.first_still);
            }
        }
    }
    printf("    -> sensitivity: at the default threshold %.1f the gate needs walking of\n"
           "       roughly 2x the group noise RMS; whether real walking reaches that is\n"
           "       for the device's \"hf\" value to show.\n", (double)THR);
}

/* ================================================================== */
/*  (e) where the floor comes from                                     */
/* ================================================================== */
static void test_floor_source(void)
{
    printf("(e) floor source\n");
    for (int ri = 0; ri < 2; ri++) {
        sim_t   s;
        scene_t occ = { 0.0f, 0.0f, 0.0f, false };
        rng_seed(400u + (uint32_t)ri);
        sim_init(&s, k_rates[ri]);
        run_t o = run(&s, &occ, 20.0, 5.0);
        printf("    %3.0f Hz, room never empty, noise: ratio unknown until %.2f s, first STILL at %.2f s, "
               "have_empty %d; after 5 s ratio mean %.2f max %.2f (min-statistics floor)\n",
               (double)k_rates[ri], o.last_unknown, o.first_still, (int)o.ever_have_empty, r_mean(&o),
               (double)o.r_max);
        CHECK(!o.ever_have_empty, "%.0f Hz: floor_empty learnt with the room occupied", (double)k_rates[ri]);
        CHECK(o.last_unknown >= 2.9 && o.last_unknown <= 3.2,
              "%.0f Hz: floor became known at %.2f s, want after %d one-second slots", (double)k_rates[ri],
              o.last_unknown, MGATE_MIN_VALID);
        CHECK(o.first_still > o.last_unknown, "%.0f Hz: STILL at %.2f s, before the floor was known (%.2f s)",
              (double)k_rates[ri], o.first_still, o.last_unknown);
        CHECK(o.first_still > 0.0 && o.first_still <= 4.5, "%.0f Hz: fallback STILL at %.2f s, want ~4",
              (double)k_rates[ri], o.first_still);
        CHECK(o.still_pk == o.pk, "%.0f Hz: fallback floor: %ld/%ld noise packets STILL",
              (double)k_rates[ri], o.still_pk, o.pk);
    }

    /* A second is "empty" only if EVERY packet in it said so. */
    {
        sim_t s;
        rng_seed(410u);
        sim_init(&s, 50.0f);
        double t = 0.0;
        bool   any = false;
        for (int i = 0; i < 50 * 20; i++) {
            scene_t sc = { 0.0f, 0.0f, 0.0f, (i % 50) < 45 };   /* 90% of each second */
            (void)sim_step(&s, &sc, &t);
            if (s.g.have_empty) any = true;
        }
        printf("    room empty for 90%% of every second, 20 s: have_empty %d\n", (int)any);
        CHECK(!any, "a second that was not wholly empty taught floor_empty");
    }

    /* The documented weakness: motion from the gate's first second, room
     * never empty. Printed, and asserted only as "this is what happens",
     * so a change to it is noticed. */
    {
        sim_t   s;
        scene_t walk = { 0.0f, 0.0f, 5.0f, false };
        scene_t occ  = { 0.0f, 0.0f, 0.0f, false };
        rng_seed(420u);
        walk_draw();
        sim_init(&s, 50.0f);
        run_t w  = run(&s, &walk, 20.0, 5.0);
        (void)run(&s, &occ, 3.0, 0.0);
        run_t w2 = run(&s, &walk, 5.0, 1.0);
        printf("    KNOWN WEAKNESS - walking 5x from the first packet, room never empty:\n"
               "      STILL at %.2f s and still %ld/%ld packets after 5 s (the walk IS the floor);\n"
               "      after 3 s standing still, a new walk is MOVING after %.2f s\n",
               w.first_still, w.still_pk, w.pk, w2.first_moving);
        CHECK(w.first_still > 0.0, "the min-statistics floor no longer adopts continuous motion - "
              "update motion_gate.h's 'known weakness' text");
        CHECK(w2.first_moving >= 0.0 && w2.first_moving <= 0.5,
              "one quiet stretch did not repair the fallback floor (MOVING after %.2f s)", w2.first_moving);
    }

    /* set_rate: keep_floor true keeps it, false forgets it */
    {
        sim_t   s;
        scene_t empty = { 0.0f, 0.0f, 0.0f, true };
        rng_seed(430u);
        sim_init(&s, 50.0f);
        (void)run(&s, &empty, 5.0, 0.0);
        float f0 = s.g.floor_empty;
        mgate_set_rate(&s.g, 100.0f, true);
        bool kept = s.g.have_empty && s.g.floor_empty == f0 && !mgate_still(&s.g);
        mgate_set_rate(&s.g, 100.0f, false);
        bool gone = !s.g.have_empty && s.g.slot_count == 0 && mgate_ratio(&s.g) < 0.0f;
        printf("    set_rate(keep_floor=true) keeps the floor: %d; (false) forgets it: %d\n", (int)kept, (int)gone);
        CHECK(kept, "set_rate(keep_floor=true) lost the floor or did not restart the decision");
        CHECK(gone, "set_rate(keep_floor=false) kept a floor");
    }

    /* A zeroed static that only ever sees mgate_set_rate (main.c once
     * forgot the mgate_init call): the threshold must not be left at 0,
     * where every packet counts as motion and the gate is never STILL. */
    {
        static sim_t z;                       /* zero-initialised, like s_gate */
        scene_t empty = { 0.0f, 0.0f, 0.0f, true };
        rng_seed(440u);
        z.rate = 50.0f;
        mgate_set_rate(&z.g, 50.0f, false);
        run_t o = run(&z, &empty, 10.0, 3.0);
        printf("    zeroed gate, set_rate only: threshold %.2f, STILL after %.2f s\n",
               (double)z.g.ratio_thr, o.first_still);
        CHECK(z.g.ratio_thr == THR, "zeroed gate: threshold %.2f after set_rate", (double)z.g.ratio_thr);
        CHECK(o.first_still > 0.0 && o.still_pk == o.pk, "zeroed gate never became STILL on noise");
    }
}

/* ================================================================== */
/*  (f) gaps and time running backwards                                */
/* ================================================================== */
static void test_gaps(void)
{
    printf("(f) gaps\n");
    for (int ri = 0; ri < 2; ri++) {
        sim_t   s;
        scene_t empty = { 0.0f, 0.0f, 0.0f, true };
        double  t     = 0.0;
        rng_seed(500u + (uint32_t)ri);
        sim_init(&s, k_rates[ri]);
        (void)run(&s, &empty, 10.0, 0.0);
        CHECK(mgate_still(&s.g), "%.0f Hz: not STILL before the gap test", (double)k_rates[ri]);

        /* 0.9 s without packets: under MGATE_GAP_US, nothing restarts
         * (the packet after it may close an empty second, which updates
         * floor_empty normally - so the floor is snapshotted after it) */
        s.t_off_us += 900000;
        bool still_short = sim_step(&s, &empty, &t);
        float f0 = s.g.floor_empty;

        /* 1.5 s: restart, floor kept - and the partial second before the
         * gap is dropped, not averaged into the floor */
        s.t_off_us += 1500000;
        bool still_long = sim_step(&s, &empty, &t);
        bool floor_kept = s.g.have_empty && s.g.floor_empty == f0;
        run_t o = run(&s, &empty, 5.0, 3.0);
        printf("    %3.0f Hz: 0.9 s gap -> still %d; 1.5 s gap -> still %d, floor kept %d, "
               "STILL again %.2f s later\n", (double)k_rates[ri], (int)still_short, (int)still_long,
               (int)floor_kept, o.first_still);
        CHECK(still_short, "%.0f Hz: a 0.9 s gap restarted the gate", (double)k_rates[ri]);
        CHECK(!still_long, "%.0f Hz: a 1.5 s gap did not restart the gate", (double)k_rates[ri]);
        CHECK(floor_kept, "%.0f Hz: the gap restart lost the floor", (double)k_rates[ri]);
        CHECK(o.first_still >= 0.9 && o.first_still <= 1.5, "%.0f Hz: STILL again %.2f s after the "
              "restart, want the 1 s hold", (double)k_rates[ri], o.first_still);
        CHECK(o.still_pk == o.pk, "%.0f Hz: not settled after the gap", (double)k_rates[ri]);
    }

    /* time running backwards: restart as well, floor kept */
    {
        sim_t   s;
        scene_t empty = { 0.0f, 0.0f, 0.0f, true };
        float   grp[CSI_NGROUPS];
        rng_seed(510u);
        sim_init(&s, 50.0f);
        (void)run(&s, &empty, 10.0, 0.0);
        for (int g = 0; g < CSI_NGROUPS; g++) grp[g] = rng_gauss();
        bool still = mgate_push(&s.g, s.t_us - 50000, grp, true);
        printf("    time 50 ms backwards -> still %d, floor kept %d\n", (int)still, (int)s.g.have_empty);
        CHECK(!still && s.g.have_empty, "time running backwards: still %d, have_empty %d",
              (int)still, (int)s.g.have_empty);
    }
}

/* ================================================================== */
/*  (g) NaN / inf input                                                */
/* ================================================================== */

/* Built from bit patterns, as in test_ncc.c: 0.0f/0.0f or 1e39f would be
 * folded at compile time, which MSVC /WX rejects (C4723 / C4756). */
static float f_from_bits(uint32_t b)
{
    float f;
    memcpy(&f, &b, sizeof(f));
    return f;
}

static void test_nan(void)
{
    sim_t   s;
    scene_t empty = { 0.0f, 0.0f, 0.0f, true };
    float   grp[CSI_NGROUPS];
    double  t = 0.0;
    mgate_t before;
    const float qnan = f_from_bits(0x7FC00000u);
    const float pinf = f_from_bits(0x7F800000u);

    printf("(g) non-finite input\n");
    rng_seed(600u);
    sim_init(&s, 50.0f);
    (void)run(&s, &empty, 10.0, 0.0);

    memcpy(&before, &s.g, sizeof(before));
    for (int g = 0; g < CSI_NGROUPS; g++) grp[g] = rng_gauss();
    grp[3] = qnan;
    bool st1 = mgate_push(&s.g, s.t_us + 20000, grp, true);
    grp[3] = pinf;
    bool st2 = mgate_push(&s.g, s.t_us + 40000, grp, false);
    bool same = memcmp(&before, &s.g, sizeof(before)) == 0;
    printf("    NaN and inf packets while STILL: returned %d %d, state untouched %d\n",
           (int)st1, (int)st2, (int)same);
    CHECK(st1 && st2, "a non-finite packet changed the decision");
    CHECK(same, "a non-finite packet modified the gate state");

    /* normal packets carry on as if nothing happened */
    run_t o = run(&s, &empty, 5.0, 0.0);
    CHECK(o.still_pk == o.pk && o.r_max < THR, "after NaN: %ld/%ld STILL, ratio max %.2f",
          o.still_pk, o.pk, (double)o.r_max);

    /* 1.5 s of nothing but NaN is a gap: the next good packet restarts */
    for (int i = 0; i < 75; i++) {
        grp[0] = qnan;
        (void)mgate_push(&s.g, s.t_us + (int64_t)(i + 1) * 20000, grp, true);
    }
    s.t_off_us += 1500000;
    bool st3 = sim_step(&s, &empty, &t);
    float r = mgate_ratio(&s.g);
    printf("    1.5 s of NaN packets, then a good one: still %d (restart), ratio %.2f finite %d\n",
           (int)st3, (double)r, (int)(r == r));
    CHECK(!st3, "1.5 s of NaN packets was not treated as a gap");
    CHECK(r == r && s.g.es == s.g.es, "NaN leaked into the gate state");
}

int main(void)
{
    printf("test_gate: motion_gate.c on SYNTHETIC group signals (units: group noise sd).\n"
           "Threshold %.1f (CSI_VITALS_MOTION_RATIO_X100 default), tau %.2f s, holds %.1f / %.1f s.\n",
           (double)THR, (double)MGATE_TAU_S, MGATE_MOVE_HOLD_US / 1e6, MGATE_STILL_HOLD_US / 1e6);
    test_noise();
    test_breathing();
    test_walking();
    test_floor_source();
    test_gaps();
    test_nan();
    return test_summary("test_gate");
}
