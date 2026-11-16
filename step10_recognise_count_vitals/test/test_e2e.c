/*
 * test_e2e.c - Step 10 end to end on the host: synthetic per-subcarrier
 * CSI -> main.c's front end -> every analytics module -> the decisions
 * main.c makes with their outputs.
 *
 * The unit tests (test_gait, test_count, test_vitals) feed each module the
 * signal it consumes directly. This one starts a step earlier, from int8
 * CSI of 64 subcarriers, and runs it through the same arithmetic main.c
 * applies before any module sees it:
 *   - amplitude sqrt(im^2 + re^2) of int8 (imag, real) pairs, as csi_amp();
 *   - AGC normalisation: divide by the median amplitude (floored at 1.0);
 *   - calibration in an empty room: per-subcarrier mean and sigma of the
 *     normalised amplitude (sigma floored at SIGMA_FLOOR), selection
 *     mu >= CSI_SELECT_MIN_AMP / 100, then Step 08's motion metric M and
 *     its empty-room mean / std;
 *   - the fast EMA with alpha = 1 - exp(-1 / (rate * TAU_FAST_S)), rate
 *     MEASURED from packet arrivals (3% dropped, +/-2 ms jitter);
 *   - dsp_group_map / dsp_group_dev -> gait_push / count_push, and
 *     vitals_push on the normalised amplitudes with the packet timestamp.
 * The constants below are copied from main.c / Kconfig defaults as of
 * 2026-09-24; if those change, change them here.
 *
 * WHAT DRIVES "moving" / "still": ground truth (walking = moving, sitting =
 * still) for the modules' own accuracy, AND the DEVICE PATH, computed here
 * exactly as main.c computes it:
 *   - Step 08's motion flag from M (threshold / release / smoothing);
 *   - Step 08's presence from that flag and the 5 s hold. NOT modelled
 *     in (a)-(e): the static (D) branch ((f) computes a SHADOW of it that
 *     feeds nothing, see below). It can only keep PRESENT on longer,
 *     i.e. mark more seconds "not empty" for the gate's floor below, and
 *     lapse a seated person LATER - it never changes the gate's filters;
 *   - the group signals once per packet, and the vitals motion gate
 *     (motion_gate.c) fed with room_empty = !(that presence);
 *   - vitals still = the gate's STILL; gait / count moving = flag && !still.
 * (b) trains and classifies count blocks from that device path, as main.c
 * does. (a) classifies gait windows gated by ground truth and only COUNTS
 * the windows the device path would have produced.
 * (c) runs vitals three ways - truth, the gate (device), and Step 08's flag
 * (the previous device path, for comparison) - because if breathing itself
 * trips the flag, a flag-gated device never gets the stillness vitals need,
 * whatever the host tests say. That is what the gate fixed.
 * (a)-(d) stop at Step 08's presence: no breathing hold, no presence-end
 * clearing, no person_vitals.
 *
 * (e) WHOSE VITALS adds main.c's per-packet Step 10 glue on top of the same
 * front end (gl_packet(), in main.c's order - see there), with every
 * feature on (Kconfig defaults) and both models trained:
 *   - CSI_VITALS_HOLDS_PRESENCE: the still episode's "began present" flag
 *     and the hold (still && began present && br_valid -> PRESENT, and
 *     last_trigger refreshed, so Step 08's own hold carries it on);
 *   - the presence transition: pv_presence_end, and analytics_reset(false)'s
 *     clearing of the gait window, the vote ring and the count latch - with
 *     the count carried across the lapse (CSI_CNT_CARRY) and dropped on
 *     movement or after 60 s while absent; a lapse lasts until the
 *     breathing hold asserts PRESENT (s_carry_wait: pv_presence_start and
 *     the count handed back then, not at Step 08's start), and nothing is
 *     named or credited while it waits (who_present);
 *   - count_step / id_step's decisions (5-block strict majority, the vote
 *     ring cleared when count turns 2, no vote while count >= 2, every
 *     decisive tally to pv_on_label) and pv_packet / pv_analysis;
 *   - report_build's vitals rule only: "multiple" = count >= 2 or
 *     pv_multiple, vitals_of = pv_vitals_of beside reported rates;
 *   - act_push (activity.h) with the post-hold presence, the gate's still
 *     and hf and the hold flag, after id_step as in main.c; act_reset in
 *     analytics_reset(true). e4 prints the activity state per segment of
 *     a walk-sit-fidget-walk-leave visit and asserts what is certain.
 * NOT modelled in (e): the static branch (above; only (f)'s shadow), report_build's other
 * fields and the JSON text, the no-CSI watchdog and recalibration (they
 * call analytics_reset(true), i.e. pv_release), sessions and the console
 * (enrolment and count training run offline as in (a) / (b), truth-gated
 * for enrolment), 'forget' / 'vitals-reset', and the NVS save. The
 * person_vitals rules themselves - every latch transition and edge case -
 * are test_pvitals.c's; (e) shows they hold on packets that came through
 * the real front end, the gate and vitals.
 *
 * (f) AGC (2026-10-30) runs the same front end and (e)'s glue under a
 * MULTIPLICATIVE receiver model, because two 2026 papers (arXiv
 * 2607.01882, 2605.26836) find that receiver AGC distorts CSI
 * multiplicatively - a common per-packet gain that jumps in steps, plus
 * fixed per-subcarrier gain nonlinearity - and that additive noise does
 * not reproduce it. Applied in make_packet() before the int8 rounding:
 * common gain steps (re-locking), slow drift, a constant offset (sweep),
 * a fixed per-subcarrier ripple, a ripple that changes with the gain
 * state, and soft compression (Rapp, p = 2) of strong tones, alone and
 * combined. The papers give no magnitudes; every number is an ASSUMPTION
 * (see k_agc_cfg). The AGC process draws from its own PRNG, so a run with
 * it off is bit-identical to one without it, and every packet carries an
 * unperturbed twin from the same noise draws (the float residual below).
 * (f) also carries a SHADOW of Step 08's static (D) branch (fe_t.slow ..
 * sh_present): computed as main.c computes it, NOT fed to presence, the
 * gate or the glue, so (a)-(e) and the per-packet order are unchanged. It
 * is there because a gain-dependent SHAPE change passes through M in a
 * fast-EMA time constant but sits in D for the slow EMA's 60 s - and,
 * once D holds PRESENT, the slow EMA freezes and D never decays.
 *
 * After every calibration the room stays empty for a few seconds of
 * detection before anybody walks in, as on a device (the user is outside
 * while it calibrates): that is where the gate learns its floor.
 *
 * WHAT THIS PROVES AND WHAT IT DOES NOT
 *   It proves the modules fit together: shapes, units and scaling are
 *   consistent from int8 CSI to a class label / breathing rate, windows
 *   and blocks line up at a measured (not assumed) packet rate, the
 *   classifier thresholds main.c uses behave sensibly on features that
 *   came through the real front end.
 *   It does NOT prove that real people can be recognised, counted or have
 *   their heart rate read. Every signal here is SYNTHETIC, and separable by
 *   construction: walkers are built to differ (test_gait.c's model), the
 *   two movers of (b) perturb nearly orthogonal groups, and (c)'s chest is
 *   a clean sinusoidal heartbeat on independent per-subcarrier noise - its
 *   rows at heart = breathing / 10 are the plausible SIZE ratio, but real
 *   CSI noise is neither white nor independent, and a real heartbeat is
 *   not a sine. Hardware validation is the README's Step 10 procedure.
 *
 * Motion is injected in units of the per-subcarrier noise and coherently
 * within a subcarrier group, because dsp_group_dev() averages each group:
 * random signs inside a group would cancel. Signs DO alternate between
 * groups, as they do physically (a path change raises some subcarriers'
 * amplitude and lowers others'), which also keeps the median normalisation
 * from removing the motion as if it were an AGC step.
 */

#include <string.h>
#include <stdbool.h>

#include "csi_dsp.h"
#include "csi_ncc.h"
#include "vitals.h"
#include "gait_id.h"
#include "count_est.h"
#include "motion_gate.h"
#include "person_vitals.h"
#include "activity.h"
#include "test_util.h"

#define TWO_PI        6.28318530718f

/* ---- main.c / Kconfig defaults, mirrored ---- */
#define TAU_FAST_S      0.5f
#define SIGMA_FLOOR     0.005f
#define METRIC_SMOOTH   0.15f
#define SELECT_MIN_AMP  0.20f      /* CONFIG_CSI_SELECT_MIN_AMP 20 / 100     */
#define THR_MOTION      5.0f       /* CONFIG_CSI_MOTION_SIGMA_X100 500       */
#define THR_RELEASE     2.5f       /* CONFIG_CSI_MOTION_RELEASE_X100 250     */
#define CAL_WARMUP_S    5
#define CAL_A_S         15
#define CAL_B_S         15
#define ID_ENROLL       30         /* CONFIG_CSI_ID_ENROLL_WINDOWS           */
#define ID_MIN_WIN      10         /* CONFIG_CSI_ID_MIN_WINDOWS              */
#define ID_REJECT       2.50f      /* CONFIG_CSI_ID_REJECT_X100 250          */
#define ID_MARGIN       1.15f      /* CONFIG_CSI_ID_MARGIN_X100 115          */
#define ID_VOTES        7          /* main.c                                 */
#define ID_MIN_VOTES    3
#define CNT_TRAIN       20         /* CONFIG_CSI_COUNT_TRAIN_WINDOWS         */
#define CNT_VOTES       5
#define PRESENCE_HOLD_US 5000000   /* CONFIG_CSI_PRESENCE_HOLD_MS 5000       */
#ifndef GATE_RATIO                 /* overridable (/DGATE_RATIO=3.0f) to study the threshold */
#define GATE_RATIO      4.0f       /* CONFIG_CSI_VITALS_MOTION_RATIO_X100 400 */
#endif
#define EMPTY_AFTER_CAL 5.0f       /* s of empty-room detection before entry */
#define ACT_LO          2.5f       /* CONFIG_CSI_ACTIVITY_RESTLESS_X100 250 (2026-10-16) */
/* the static (D) branch, mirrored only as (f)'s shadow (2026-10-30) */
#define TAU_SLOW_S      60.0f      /* main.c TAU_SLOW_S                      */
#define THR_STATIC      5.0f       /* CONFIG_CSI_STATIC_SIGMA_X100 500       */
#define STATIC_SUSTAIN_US 4000000  /* CONFIG_CSI_STATIC_SUSTAIN_MS 4000      */
#define SLOW_FREEZE_MAX_US 600000000LL  /* CONFIG_CSI_SLOW_FREEZE_MAX_S 600  */

static const vitals_cfg_t k_vcfg = { 30, 20, 4.0f, 5.0f };  /* Kconfig defaults */

/* ---- the synthetic radio ---- */
#define NSUB          64
#define NOISE_LSB     0.6f         /* thermal noise on the amplitude, LSB     */
#define DROP_P        0.03f        /* packets that never arrive               */
#define JITTER_US     2000

static bool is_data_sub(int k)
{
    /* HT20: guard bands and DC carry nothing; 52 data subcarriers. */
    return !(k <= 5 || k == 32 || k >= 59);
}

/* ================================================================== */
/*  Room: static channel and the sim's own subcarrier groups           */
/* ================================================================== */
typedef struct {
    float  base[NSUB];      /* static amplitude, LSB (frequency-selective) */
    float  cph[NSUB], sph[NSUB];
    int8_t band[NSUB];      /* group a data subcarrier belongs to, -1 else */
    float  wk[NSUB];        /* per-subcarrier sensitivity within a group   */
} room_t;

static room_t s_room;

static void room_make(void)
{
    float p1 = TWO_PI * rng_uniform(), p2 = TWO_PI * rng_uniform();
    int   idx = 0;
    for (int k = 0; k < NSUB; k++) {
        float ph = TWO_PI * rng_uniform();
        s_room.cph[k] = cosf(ph);
        s_room.sph[k] = sinf(ph);
        if (is_data_sub(k)) {
            /* smooth fading across the band, 17..47 LSB */
            s_room.base[k] = 32.0f + 10.0f * sinf(TWO_PI * (float)k / 23.0f + p1)
                                   + 5.0f * sinf(TWO_PI * (float)k / 9.0f + p2);
            s_room.band[k] = (int8_t)((idx * CSI_NGROUPS) / 52);
            s_room.wk[k]   = 0.8f + 0.4f * rng_uniform();
            idx++;
        } else {
            s_room.base[k] = 1.5f;
            s_room.band[k] = -1;
            s_room.wk[k]   = 0.0f;
        }
    }
}

/* One packet's int8 CSI -> amplitude, as csi_amp() computes it. motion[k]
 * is the perturbation in units of NOISE_LSB. The AGC gain wanders 3% per
 * packet: the median normalisation is what removes it. */
static int to_i8(float x)
{
    float r = floorf(x + 0.5f);
    if (r > 127.0f) r = 127.0f;
    if (r < -128.0f) r = -128.0f;
    return (int)r;
}

/* ---- (f) the AGC-style receiver model (2026-10-30) ----
 * On top of the 3 % per-packet wander below (which stays), per subcarrier k:
 *   a' = C( a * G(t) * R[k] * S[state][k] )
 *   G(t)  common gain, dB = offset + steps + drift * sin(2 pi t / period):
 *         steps of 1, 2 or 3 dB (random sign) at exponential intervals,
 *         the running total kept within +/- step_lim_db (AGC re-locking);
 *   R[k]  fixed per-subcarrier gain: ripple_db * (0.6 sin + 0.4 sin) of two
 *         random periods across the band, plus ripple_rand_db rms per
 *         subcarrier - present from boot, so calibration sees it;
 *   S     a per-subcarrier response that depends on the GAIN STATE (one
 *         pattern per 1-dB level of offset + steps, state_db rms, smooth
 *         across the band; the calibration state's pattern is 1): a gain
 *         stage switching changes the shape, not just the scale;
 *   C     soft compression, Rapp AM/AM with p = 2:
 *         y = x / (1 + (x / A)^4)^(1/4), A = comp_1db / 0.8746 so that
 *         an input of comp_1db LSB is compressed by 1 dB.
 * Steps, drift and the offset act from on_from_us (after calibration, or
 * from boot); R, S and C are receiver properties and always act. Draws
 * come from s_agc.rng only. */
#define AGC_NSTATE   9             /* step levels -4 .. +4 dB              */
#define AGC_S0       4             /* the 0 dB (calibration) state         */

typedef struct {
    const char *name;
    float offset_db;               /* constant common offset               */
    float step_mean_s;             /* mean dwell between steps; 0 = none   */
    float step_lim_db;             /* |running total of steps| <= this     */
    float drift_db, drift_period_s;
    float ripple_db, ripple_rand_db;
    float state_db;                /* gain-state-dependent ripple, rms dB  */
    float comp_1db;                /* 1 dB compression input, LSB; 0 = off */
} agc_cfg_t;

typedef struct {
    bool      on;
    agc_cfg_t c;
    uint32_t  rng;
    int64_t   on_from_us, next_us;
    float     step_db;             /* running total of the steps           */
    int       state;
    int       steps;               /* steps taken                          */
    float     lin;                 /* this packet's G                      */
    float     comp_a;
    float     rip[NSUB];
    float     srip[AGC_NSTATE][NSUB];
    /* the unperturbed twin of the last packet (same draws), float */
    float     amp_ref[NSUB], amp_pert[NSUB];
} agc_t;

static agc_t s_agc;                /* zeroed: off                          */

static float agc_uniform(void)
{
    uint32_t x = s_agc.rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_agc.rng = x;
    return (float)(x >> 8) * (1.0f / 16777216.0f);
}

static float agc_gauss(void)
{
    float u1 = agc_uniform();
    float u2 = agc_uniform();
    if (u1 < 1e-7f) u1 = 1e-7f;
    return sqrtf(-2.0f * logf(u1)) * cosf(TWO_PI * u2);
}

static float db_lin(float db) { return powf(10.0f, db / 20.0f); }

/* Turn the model on with this configuration (all zero = on, but exactly
 * the unperturbed radio: every factor is 1.0f). The time-varying parts
 * wait for agc_start(). */
static void agc_configure(const agc_cfg_t *c, uint32_t seed)
{
    memset(&s_agc, 0, sizeof(s_agc));
    s_agc.on         = true;
    s_agc.c          = *c;
    s_agc.rng        = seed ? seed : 0x9e3779b9u;
    s_agc.on_from_us = INT64_MAX;
    s_agc.next_us    = INT64_MAX;
    s_agc.state      = AGC_S0;
    s_agc.lin        = 1.0f;
    s_agc.comp_a     = (c->comp_1db > 0.0f) ? c->comp_1db / 0.8746f : 0.0f;
    float q1 = 9.0f + 14.0f * agc_uniform(), q2 = 4.0f + 5.0f * agc_uniform();
    float f1 = TWO_PI * agc_uniform(), f2 = TWO_PI * agc_uniform();
    for (int k = 0; k < NSUB; k++) {
        float db = c->ripple_db * (0.6f * sinf(TWO_PI * (float)k / q1 + f1) +
                                   0.4f * sinf(TWO_PI * (float)k / q2 + f2));
        float rn = agc_gauss();
        s_agc.rip[k] = (c->ripple_db > 0.0f || c->ripple_rand_db > 0.0f)
                     ? db_lin(db + c->ripple_rand_db * rn) : 1.0f;
    }
    for (int s = 0; s < AGC_NSTATE; s++) {
        float q = 8.0f + 22.0f * agc_uniform(), f = TWO_PI * agc_uniform();
        for (int k = 0; k < NSUB; k++) {
            float db = c->state_db * 1.41421356f * sinf(TWO_PI * (float)k / q + f);
            s_agc.srip[s][k] = (s == AGC_S0 || c->state_db <= 0.0f) ? 1.0f : db_lin(db);
        }
    }
}

static void agc_off(void) { memset(&s_agc, 0, sizeof(s_agc)); }

static void agc_start(int64_t t_us)
{
    if (!s_agc.on) return;
    s_agc.on_from_us = t_us;
    if (s_agc.c.step_mean_s > 0.0f) {
        s_agc.next_us = t_us + (int64_t)(-logf(1.0f - agc_uniform()) * s_agc.c.step_mean_s * 1e6f);
    }
}

/* Once per arriving packet, before make_packet(): G and the state at t. */
static void agc_tick(int64_t t_us)
{
    if (!s_agc.on) return;
    float db = 0.0f;
    if (t_us >= s_agc.on_from_us) {
        while (t_us >= s_agc.next_us) {
            float size = 1.0f + floorf(3.0f * agc_uniform());     /* 1, 2 or 3 dB */
            float sgn  = (agc_uniform() < 0.5f) ? -1.0f : 1.0f;
            if (fabsf(s_agc.step_db + sgn * size) > s_agc.c.step_lim_db) sgn = -sgn;
            if (fabsf(s_agc.step_db + sgn * size) > s_agc.c.step_lim_db) size = 1.0f;
            s_agc.step_db += sgn * size;
            s_agc.steps++;
            float dwell = -logf(1.0f - agc_uniform()) * s_agc.c.step_mean_s;
            if (dwell < 0.2f) dwell = 0.2f;
            s_agc.next_us += (int64_t)(dwell * 1e6f);
        }
        double ts = (double)(t_us - s_agc.on_from_us) * 1e-6;
        db = s_agc.c.offset_db + s_agc.step_db;
        if (s_agc.c.drift_db > 0.0f && s_agc.c.drift_period_s > 0.0f) {
            db += s_agc.c.drift_db * (float)sin(6.283185307179586 * ts / (double)s_agc.c.drift_period_s);
        }
    }
    float lvl = (t_us >= s_agc.on_from_us) ? s_agc.c.offset_db + s_agc.step_db : 0.0f;
    int   st  = AGC_S0 + (int)floorf(lvl + 0.5f);       /* the gain state: drift is not a state */
    s_agc.state = (st < 0) ? 0 : (st >= AGC_NSTATE ? AGC_NSTATE - 1 : st);
    s_agc.lin   = (db != 0.0f) ? db_lin(db) : 1.0f;
}

static float agc_comp(float a)
{
    if (s_agc.comp_a > 0.0f) {
        float x = fabsf(a) / s_agc.comp_a;
        float x2 = x * x;
        a /= powf(1.0f + x2 * x2, 0.25f);
    }
    return a;
}

static float agc_apply(int k, float a)
{
    return agc_comp(a * s_agc.lin * s_agc.rip[k] * s_agc.srip[s_agc.state][k]);
}

/* the same receiver held at the calibration state (G = 1, state 0 dB):
 * the twin the residual compares against */
static float agc_twin(int k, float a)
{
    return agc_comp(a * s_agc.rip[k]);
}

static void make_packet(const float *motion, float *amp)
{
    float g = 1.0f + 0.03f * rng_gauss();
    for (int k = 0; k < NSUB; k++) {
        float a  = g * (s_room.base[k] + NOISE_LSB * (rng_gauss() + motion[k]));
        if (s_agc.on) {            /* (f): no draws from g_rng */
            s_agc.amp_ref[k] = fabsf(agc_twin(k, a));
            a = agc_apply(k, a);
            s_agc.amp_pert[k] = fabsf(a);
        }
        int   im = to_i8(a * s_room.cph[k]);
        int   re = to_i8(a * s_room.sph[k]);
        amp[k] = sqrtf((float)(im * im + re * re));
    }
}

/* ================================================================== */
/*  main.c's front end                                                 */
/* ================================================================== */
typedef struct {
    float   rate_nom;          /* ping rate the clock runs at             */
    float   rate;              /* MEASURED in warm-up, as PH_WARMUP does  */
    float   alpha_fast;
    int64_t slot;
    int64_t t_us;              /* timestamp of the last packet            */
    float   na[NSUB], fast[NSUB], scratch[NSUB];
    bool    ema_init;
    /* baseline */
    float   mu[NSUB], sigma[NSUB], inv_sigma[NSUB];
    uint8_t sel[NSUB];
    int8_t  map[NSUB];
    int     nsel;
    float   m_mean, m_std;
    /* Step 08 motion flag and presence (motion branch + hold) */
    bool    calibrated, live_init, motion_flag, present;
    float   live_m, m_sigma;
    int64_t last_trigger_us;
    /* Step 10: group signals and the motion gate, once per packet */
    float   grp[CSI_NGROUPS];
    mgate_t gate;
    bool    still;
    /* (f) SHADOW of Step 08's static branch (main.c PH_B / PH_DETECT): the
     * slow EMA (frozen while present, at most 600 s), D, d_sigma, the 4 s
     * sustain and the 5 s hold. Read by (f) only; nothing here feeds
     * present, the gate or the glue. sh_present = this packet's motion-
     * branch presence (new_present, before THIS packet's breathing hold)
     * OR the static trigger OR its hold. The freeze and the hold term read
     * s_fe.present and last_trigger_us: in f1 (no glue) Step 08's presence;
     * in f2 (e)'s glue sets both from the post-breathing-hold presence, so
     * there the shadow freezes and holds on STEP 10's presence. */
    float   slow[NSUB], alpha_slow;
    float   d_mean, d_std, live_d, d_sigma;
    int64_t static_since_us, static_last_us, freeze_since_us;
    bool    static_flag, sh_present;
} fe_t;

static fe_t s_fe;

/* (f) shadow: main.c freezes the slow EMA while `present` (the last
 * packet's, after the hold) in PH_DETECT, for at most 600 s. Here the
 * shadow's present: the mirror's or the static branch's. True = freeze
 * this packet. (Factored out of fe_packet for test_agc's f-shadow script.) */
static bool sh_freeze(void)
{
    bool freeze = false;
    if (s_fe.calibrated && (s_fe.present || s_fe.sh_present)) {
        if (s_fe.freeze_since_us == 0) s_fe.freeze_since_us = s_fe.t_us;
        freeze = s_fe.t_us - s_fe.freeze_since_us < SLOW_FREEZE_MAX_US;
    } else {
        s_fe.freeze_since_us = 0;
    }
    return freeze;
}

/* (f) shadow: the static trigger (d_sigma >= THR_STATIC sustained for
 * STATIC_SUSTAIN_US) and sh_present, from this packet's s_fe.d_sigma and
 * t_us and the motion branch's new_present; before s_fe.present is
 * updated (the hold term reads the previous packet's). */
static void sh_static(bool new_present)
{
    if (s_fe.d_sigma >= THR_STATIC) {
        if (s_fe.static_since_us == 0) s_fe.static_since_us = s_fe.t_us;
        else if (s_fe.t_us - s_fe.static_since_us >= STATIC_SUSTAIN_US) s_fe.static_flag = true;
    } else {
        s_fe.static_since_us = 0;
        s_fe.static_flag     = false;
    }
    if (s_fe.static_flag) s_fe.static_last_us = s_fe.t_us;
    s_fe.sh_present = new_present || s_fe.static_flag ||
        ((s_fe.sh_present || s_fe.present) && s_fe.static_last_us != 0 &&
         s_fe.t_us - s_fe.static_last_us < PRESENCE_HOLD_US);
}

static void fe_packet(const float *amp)
{
    float med = dsp_median(amp, NSUB, s_fe.scratch);   /* = main.c median_of */
    if (med < 1.0f) med = 1.0f;
    for (int k = 0; k < NSUB; k++) {
        s_fe.na[k] = amp[k] / med;
    }
    bool freeze = sh_freeze();
    if (!s_fe.ema_init) {
        for (int k = 0; k < NSUB; k++) s_fe.fast[k] = s_fe.na[k];
        for (int k = 0; k < NSUB; k++) s_fe.slow[k] = s_fe.na[k];
        s_fe.ema_init = true;
    } else {
        for (int k = 0; k < NSUB; k++) {
            s_fe.fast[k] += s_fe.alpha_fast * (s_fe.na[k] - s_fe.fast[k]);
        }
        if (!freeze) {
            for (int k = 0; k < NSUB; k++) s_fe.slow[k] += s_fe.alpha_slow * (s_fe.na[k] - s_fe.slow[k]);
        }
    }
    if (!s_fe.calibrated) return;

    /* Step 08's motion branch, PH_DETECT (and (f)'s shadow D) */
    float sm  = 0.0f, sd = 0.0f;
    int   cnt = 0;
    for (int k = 0; k < NSUB; k++) {
        if (!s_fe.sel[k]) continue;
        sm += fabsf(s_fe.na[k] - s_fe.fast[k]) / s_fe.sigma[k];
        sd += fabsf(s_fe.fast[k] - s_fe.slow[k]) / s_fe.sigma[k];
        cnt++;
    }
    if (cnt == 0) return;
    float m = sm / (float)cnt;
    float d = sd / (float)cnt;
    if (!s_fe.live_init) { s_fe.live_m = m; s_fe.live_d = d; s_fe.live_init = true; }
    s_fe.live_m += METRIC_SMOOTH * (m - s_fe.live_m);
    s_fe.live_d += METRIC_SMOOTH * (d - s_fe.live_d);
    s_fe.d_sigma = (s_fe.d_std > 0.0f) ? (s_fe.live_d - s_fe.d_mean) / s_fe.d_std : 0.0f;
    s_fe.m_sigma = (s_fe.m_std > 0.0f) ? (s_fe.live_m - s_fe.m_mean) / s_fe.m_std : 0.0f;
    if (!s_fe.motion_flag && s_fe.m_sigma >= THR_MOTION)      s_fe.motion_flag = true;
    else if (s_fe.motion_flag && s_fe.m_sigma < THR_RELEASE)  s_fe.motion_flag = false;

    /* Step 08's presence: trigger (motion branch only, see the header)
     * or the hold after the last trigger */
    if (s_fe.motion_flag) s_fe.last_trigger_us = s_fe.t_us;
    bool new_present = s_fe.motion_flag ||
        (s_fe.present && s_fe.last_trigger_us != 0 &&
         s_fe.t_us - s_fe.last_trigger_us < PRESENCE_HOLD_US);
    sh_static(new_present);

    /* main.c PH_DETECT: grp once per packet; the gate gets Step 08's
     * decision of THIS packet (before the breathing hold) as room_empty */
    dsp_group_dev(s_fe.na, s_fe.fast, s_fe.inv_sigma, s_fe.map, NSUB, s_fe.grp);
    s_fe.still   = mgate_push(&s_fe.gate, s_fe.t_us, s_fe.grp, !new_present);
    s_fe.present = new_present;
}

/* ================================================================== */
/*  Sources                                                            */
/* ================================================================== */

/* Walkers: test_gait.c's model (see there), with SIGNED group mixes whose
 * sign patterns differ between walkers (|mix| is test_gait's), and one
 * addition: PATH DRIFT. test_gait draws a walk's strength and group mix
 * once and holds them; here they wander as an AR(1) (time constant
 * PATH_TAU_S) with the same +/-20% spread, started from a random state -
 * so a walk still differs from the last one by as much as before, but a
 * person who walks around a room for the ~40 s an enrolment takes does not
 * hold one path, and the template sees that variety.
 * Why it matters: csi_ncc measures distance in units of the WITHIN-
 * enrolment spread. With the path frozen for a whole enrolment the group
 * shares (features 10..17) barely vary, their pooled std collapses, and
 * every later walk of the SAME person lands 2-9 units away - "unknown" at
 * reject 2.5. (a) prints that case too; on a device it is the enrolment
 * that walks one fixed route. */
#define PATH_TAU_S    8.0f
static bool s_path_drift = true;
typedef struct {
    const char *name;
    float       f_torso, f_step;
    float       mix[CSI_NGROUPS];
} walker_t;

static const walker_t k_walkers[3] = {
    { "A",  4.0f, 1.6f, { 1.0f, -0.9f,  0.7f, -0.5f,  0.3f, -0.2f,  0.1f, -0.1f } },
    { "B",  7.0f, 1.9f, { 0.2f,  0.4f, -0.8f, -1.0f,  0.9f,  0.5f, -0.2f, -0.1f } },
    { "C", 11.0f, 2.2f, { 0.1f, -0.1f, -0.2f,  0.3f,  0.5f, -0.8f, -1.0f,  0.9f } },
};

typedef struct {
    float f_step;
    const float *mix0;               /* the walker's nominal mix            */
    float d[CSI_NGROUPS + 1];        /* path state: per-group mix, strength  */
    float rho, innov;                /* AR(1); rho 1 = frozen for the walk   */
    float phi1, phi2, e_gain;
    float y1, y2, phase;
} session_t;

static void session_start(session_t *s, const walker_t *w, float rate)
{
    float ft = w->f_torso * (1.0f + 0.05f * rng_gauss());
    s->f_step = w->f_step * (1.0f + 0.03f * rng_gauss());
    s->mix0   = w->mix;
    for (int j = 0; j <= CSI_NGROUPS; j++) {
        s->d[j] = rng_gauss();       /* stationary start: a random path     */
    }
    s->rho   = s_path_drift ? expf(-1.0f / (PATH_TAU_S * rate)) : 1.0f;
    s->innov = sqrtf(1.0f - s->rho * s->rho);
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

/* drive[j]: this walker's perturbation of group j, noise-sd units. */
static void session_step(session_t *s, float rate, float drive[CSI_NGROUPS])
{
    float y = s->phi1 * s->y1 + s->phi2 * s->y2 + s->e_gain * rng_gauss();
    s->y2 = s->y1;
    s->y1 = y;
    s->phase += TWO_PI * s->f_step / rate + 0.03f * rng_gauss();
    if (s->phase > TWO_PI) s->phase -= TWO_PI;
    float st   = sinf(s->phase);
    float body = (1.0f + 0.6f * st) * y + 0.7f * st;
    if (s->innov > 0.0f) {
        for (int j = 0; j <= CSI_NGROUPS; j++) {
            s->d[j] = s->rho * s->d[j] + s->innov * rng_gauss();
        }
    }
    /* test_gait's per-walk draws: amp 2 (1 + 0.2 N) floored at 0.5,
     * mix_j (1 + 0.2 N) - here N is the path state instead. */
    float amp = 2.0f * (1.0f + 0.2f * s->d[CSI_NGROUPS]);
    if (amp < 0.5f) amp = 0.5f;
    for (int j = 0; j < CSI_NGROUPS; j++) {
        drive[j] = amp * s->mix0[j] * (1.0f + 0.2f * s->d[j]) * body;
    }
}

/* A chest: breathing (with 2nd / 3rd harmonics - a real chest does not
 * move sinusoidally) and a heartbeat, per data subcarrier with random
 * sign / phase / strength, plus a slow drift. Amplitudes are in units of
 * the per-packet noise (NOISE_LSB). */
typedef struct {
    float f_br, f_hr;
    float a_br[NSUB], p_br[NSUB], p_h2[NSUB], p_h3[NSUB];
    float a_hr[NSUB], p_hr[NSUB], drift[NSUB];
} chest_t;

static void chest_make(chest_t *c, float a_br, float a_hr, float bpm_br, float bpm_hr)
{
    c->f_br = bpm_br / 60.0f;
    c->f_hr = bpm_hr / 60.0f;
    for (int k = 0; k < NSUB; k++) {
        bool  d   = is_data_sub(k);
        float sgn = (rng_uniform() < 0.5f) ? -1.0f : 1.0f;
        c->a_br[k]  = d ? sgn * a_br * (0.5f + rng_uniform()) : 0.0f;
        c->a_hr[k]  = d ? a_hr * (0.5f + rng_uniform()) : 0.0f;
        c->p_br[k]  = TWO_PI * rng_uniform();
        c->p_h2[k]  = TWO_PI * rng_uniform();
        c->p_h3[k]  = TWO_PI * rng_uniform();
        c->p_hr[k]  = TWO_PI * rng_uniform();
        c->drift[k] = d ? 0.01f * rng_gauss() : 0.0f;     /* per second */
    }
}

static float chest_value(const chest_t *c, int k, double t)
{
    double wb = 6.283185307179586 * (double)c->f_br * t;
    double wh = 6.283185307179586 * (double)c->f_hr * t;
    double v  = (double)c->a_br[k] * (sin(wb + (double)c->p_br[k]) +
                                      0.3 * sin(2.0 * wb + (double)c->p_h2[k]) +
                                      0.15 * sin(3.0 * wb + (double)c->p_h3[k]))
              + (double)c->a_hr[k] * sin(wh + (double)c->p_hr[k])
              + (double)c->drift[k] * t;
    return (float)v;
}

typedef struct {
    session_t     *walker[2];
    int            nwalker;
    float          wgain;            /* (e) e4: walkers' strength; 0 = 1     */
    const chest_t *chest;
    const chest_t *chest2;           /* (e): a second seated person, or NULL */
    int64_t        chest_t0_us;
} scene_t;

static float s_motion[NSUB], s_amp[NSUB];

/* (f) the float residual: this packet's normalised amplitude against its
 * twin - the same draws through the same receiver held at the CALIBRATION
 * state (G = 1, the 0 dB gain state; fixed ripple and compression kept),
 * both BEFORE the int8 rounding - mean over the selected subcarriers in
 * units of their empty-room sigma. It is what the median normalisation
 * leaves of the receiver's changes since calibration: a pure common gain
 * leaves float rounding only; a fixed ripple or compression alone leaves
 * exactly 0 (calibration saw them); a gain change THROUGH compression or a
 * gain-state ripple leaves a shape change. */
static double s_res_sum;
static float  s_res_max;
static long   s_res_n;

static void agc_residual(void)
{
    float sc1[NSUB], sc2[NSUB];
    float mr = dsp_median(s_agc.amp_ref, NSUB, sc1);
    float mp = dsp_median(s_agc.amp_pert, NSUB, sc2);
    if (mr < 1.0f) mr = 1.0f;
    if (mp < 1.0f) mp = 1.0f;
    float r = 0.0f;
    int   n = 0;
    for (int k = 0; k < NSUB; k++) {
        if (!s_fe.sel[k]) continue;
        r += fabsf(s_agc.amp_pert[k] / mp - s_agc.amp_ref[k] / mr) * s_fe.inv_sigma[k];
        n++;
    }
    if (n == 0) return;
    r /= (float)n;
    s_res_sum += (double)r;
    s_res_n++;
    if (r > s_res_max) s_res_max = r;
}

/* One nominal packet slot: the sources advance; the packet arrives unless
 * dropped, and then runs through the front end. True if it arrived. */
static bool slot(const scene_t *sc)
{
    float drive[2][CSI_NGROUPS];

    s_fe.slot++;
    for (int i = 0; i < sc->nwalker; i++) {
        session_step(sc->walker[i], s_fe.rate_nom, drive[i]);
        if (sc->wgain > 0.0f) {
            for (int j = 0; j < CSI_NGROUPS; j++) drive[i][j] *= sc->wgain;
        }
    }
    if (rng_uniform() < DROP_P) return false;

    int64_t nominal = (int64_t)((double)s_fe.slot * 1e6 / (double)s_fe.rate_nom);
    int64_t jit     = (int64_t)((rng_uniform() * 2.0f - 1.0f) * (float)JITTER_US);
    int64_t t_us    = nominal + jit;
    if (t_us <= s_fe.t_us) t_us = s_fe.t_us + 1;   /* arrival order is kept */
    s_fe.t_us = t_us;

    double tc = (double)(t_us - sc->chest_t0_us) * 1e-6;
    for (int k = 0; k < NSUB; k++) {
        float m = 0.0f;
        int   b = s_room.band[k];
        if (b >= 0) {
            for (int i = 0; i < sc->nwalker; i++) {
                m += s_room.wk[k] * drive[i][b];
            }
        }
        if (sc->chest) m += chest_value(sc->chest, k, tc);
        if (sc->chest2) m += chest_value(sc->chest2, k, tc);
        s_motion[k] = m;
    }
    agc_tick(t_us);
    make_packet(s_motion, s_amp);
    fe_packet(s_amp);
    if (s_agc.on && s_fe.calibrated) agc_residual();
    return true;
}

static void run_empty(float seconds)
{
    scene_t sc;
    memset(&sc, 0, sizeof(sc));
    int n = (int)(seconds * s_fe.rate_nom);
    for (int i = 0; i < n; i++) (void)slot(&sc);
}

/* ================================================================== */
/*  Calibration: PH_WARMUP, PH_A, PH_SELECT, PH_B (M; D for (f)'s shadow) */
/* ================================================================== */
static void fe_calibrate(float rate_nom)
{
    scene_t sc;
    memset(&sc, 0, sizeof(sc));
    memset(&s_fe, 0, sizeof(s_fe));
    s_fe.rate_nom   = rate_nom;
    s_fe.alpha_fast = 0.1f;                       /* main.c's pre-warm-up value */

    /* warm-up: measure the packet rate from arrivals */
    int nslots = (int)((float)CAL_WARMUP_S * rate_nom), got = 0;
    for (int i = 0; i < nslots; i++) got += slot(&sc) ? 1 : 0;
    s_fe.rate       = (float)got / (float)CAL_WARMUP_S;
    s_fe.alpha_fast = 1.0f - expf(-1.0f / (s_fe.rate * TAU_FAST_S));
    s_fe.alpha_slow = 1.0f - expf(-1.0f / (s_fe.rate * TAU_SLOW_S));   /* (f) shadow */

    /* phase A: per-subcarrier statistics of the normalised amplitude */
    static double mean[NSUB], m2[NSUB];
    memset(mean, 0, sizeof(mean));
    memset(m2, 0, sizeof(m2));
    uint32_t n = 0;
    nslots = (int)((float)CAL_A_S * rate_nom);
    for (int i = 0; i < nslots; i++) {
        if (!slot(&sc)) continue;
        n++;
        for (int k = 0; k < NSUB; k++) {
            double x  = (double)s_fe.na[k];
            double d1 = x - mean[k];
            mean[k] += d1 / (double)n;
            m2[k]   += d1 * (x - mean[k]);
        }
    }
    /* select */
    s_fe.nsel = 0;
    for (int k = 0; k < NSUB; k++) {
        s_fe.mu[k]    = (float)mean[k];
        s_fe.sigma[k] = (n > 1) ? sqrtf((float)(m2[k] / (double)(n - 1))) : 0.0f;
        if (s_fe.sigma[k] < SIGMA_FLOOR) s_fe.sigma[k] = SIGMA_FLOOR;
        s_fe.sel[k] = (s_fe.mu[k] >= SELECT_MIN_AMP) ? 1 : 0;
        s_fe.slow[k]  = s_fe.mu[k];      /* (f) shadow: main.c re-seeds at PH_SELECT */
    }

    /* phase B: M's empty-room mean and std, smoothed first as main.c does */
    double mm = 0.0, mq = 0.0, dm = 0.0, dq = 0.0;
    uint32_t bn = 0;
    bool   li = false;
    float  lm = 0.0f, ld = 0.0f;
    nslots = (int)((float)CAL_B_S * rate_nom);
    for (int i = 0; i < nslots; i++) {
        if (!slot(&sc)) continue;
        float sm  = 0.0f, sd = 0.0f;
        int   cnt = 0;
        for (int k = 0; k < NSUB; k++) {
            if (!s_fe.sel[k]) continue;
            sm += fabsf(s_fe.na[k] - s_fe.fast[k]) / s_fe.sigma[k];
            sd += fabsf(s_fe.fast[k] - s_fe.slow[k]) / s_fe.sigma[k];
            cnt++;
        }
        if (cnt == 0) continue;
        float m = sm / (float)cnt, dd = sd / (float)cnt;
        if (!li) { lm = m; ld = dd; li = true; }
        lm += METRIC_SMOOTH * (m - lm);
        ld += METRIC_SMOOTH * (dd - ld);
        bn++;
        double e = (double)lm - mm;
        mm += e / (double)bn;
        mq += e * ((double)lm - mm);
        double e2 = (double)ld - dm;
        dm += e2 / (double)bn;
        dq += e2 * ((double)ld - dm);
    }
    s_fe.m_mean = (float)mm;
    s_fe.m_std  = (bn > 1) ? sqrtf((float)(mq / (double)(bn - 1))) : 0.0f;
    s_fe.d_mean = (float)dm;                                   /* (f) shadow */
    s_fe.d_std  = (bn > 1) ? sqrtf((float)(dq / (double)(bn - 1))) : 0.0f;

    /* analytics_configure() */
    for (int k = 0; k < NSUB; k++) {
        s_fe.inv_sigma[k] = (s_fe.sigma[k] > 0.0f) ? 1.0f / s_fe.sigma[k] : 0.0f;
    }
    s_fe.nsel        = dsp_group_map(s_fe.sel, NSUB, s_fe.map);
    /* mgate_set_rate(keep_floor = false): a new baseline, a new floor */
    mgate_init(&s_fe.gate, s_fe.rate, GATE_RATIO);
    s_fe.calibrated  = true;
    s_fe.live_init   = false;
    s_fe.motion_flag = false;
    s_fe.present     = false;
    s_fe.last_trigger_us = 0;
    s_fe.still       = false;
    s_fe.static_since_us = s_fe.static_last_us = s_fe.freeze_since_us = 0;
    s_fe.static_flag = false;
    s_fe.sh_present  = false;
}

static void print_calibration(void)
{
    float sg[NSUB], scr[NSUB];
    int   n = 0;
    for (int k = 0; k < NSUB; k++) {
        if (s_fe.sel[k]) sg[n++] = s_fe.sigma[k];
    }
    printf("    calibration: %.1f packets/s measured (%.0f nominal), alpha_fast %.4f, "
           "%d/%d subcarriers selected, median sigma %.4f, empty M %.3f +/- %.4f\n",
           (double)s_fe.rate, (double)s_fe.rate_nom, (double)s_fe.alpha_fast, s_fe.nsel,
           NSUB, (double)dsp_median(sg, n, scr), (double)s_fe.m_mean, (double)s_fe.m_std);
}

/* ================================================================== */
/*  Module state - static, as on the target                            */
/* ================================================================== */
static vitals_t    s_vit;
static gait_t      s_gait;
static gait_t      s_gait_dev;      /* same packets, moving = flag && !still (main.c) */
static gait_t      s_gait_flag;     /* same packets, moving = flag alone (before the gate) */
static count_est_t s_cnt_dev;       /* moving = flag && !still (main.c) */
static ncc_model_t s_id_model;
static ncc_model_t s_cnt_model;

/* Motion-flag / gate agreement bookkeeping over whatever a scenario runs. */
typedef struct {
    long pk_moving, flag_moving;    /* truth moving: packets, flagged moving   */
    long gate_moving;               /* ... the gate says MOVING                */
    long hf_n, hf_below;            /* ... hf known, and hf < GATE_RATIO       */
    double hf_sum;
    double grp_sq;                  /* sum of grp^2 over moving packets        */
    long win_truth, win_dev, win_flag;  /* gait windows: truth, device, flag only */
} agree_t;

/* ================================================================== */
/*  (a) recognition                                                    */
/* ================================================================== */

/* One presence episode of one walker. gait state starts fresh - main.c
 * resets it at enrolment start and whenever presence ends. The first
 * second of the flag / gate agreement is skipped (M's smoothing, EMA
 * transient, the gate's 0.3 s hold). Two more gait_t see the same packets
 * with moving = flag && !still (main.c now) and moving = flag alone (main.c
 * before the gate), only to COUNT the windows the device would get (their
 * features are not used). Ends with 5 s of empty room, i.e. presence
 * ending. */
static int walk_windows(const walker_t *w, int nwin, float (*out)[GAIT_NFEAT], agree_t *ag)
{
    session_t s;
    scene_t   sc;
    float     dev_feat[GAIT_NFEAT];
    int       got = 0;

    memset(&sc, 0, sizeof(sc));
    session_start(&s, w, s_fe.rate_nom);
    sc.walker[0] = &s;
    sc.nwalker   = 1;
    gait_reset(&s_gait);
    gait_set_rate(&s_gait_dev, s_gait.rate_hz);
    gait_reset(&s_gait_dev);
    gait_set_rate(&s_gait_flag, s_gait.rate_hz);
    gait_reset(&s_gait_flag);
    for (long i = 0; got < nwin && i < 400000; i++) {
        if (!slot(&sc)) continue;
        const float *grp = s_fe.grp;
        if ((float)i > s_fe.rate_nom) {
            float hf = mgate_ratio(&s_fe.gate);
            ag->pk_moving++;
            if (s_fe.motion_flag) ag->flag_moving++;
            if (!s_fe.still) ag->gate_moving++;
            if (hf >= 0.0f) {
                ag->hf_n++;
                ag->hf_sum += (double)hf;
                if (hf < GATE_RATIO) ag->hf_below++;
            }
            for (int j = 0; j < CSI_NGROUPS; j++) ag->grp_sq += (double)(grp[j] * grp[j]);
        }
        if (gait_push(&s_gait_dev, grp, s_fe.motion_flag && !s_fe.still, dev_feat)) ag->win_dev++;
        if (gait_push(&s_gait_flag, grp, s_fe.motion_flag, dev_feat)) ag->win_flag++;
        if (gait_push(&s_gait, grp, true, out[got])) {
            got++;
            ag->win_truth++;
        }
    }
    run_empty(5.0f);
    return got;
}

/* main.c's id_tally(): a slot needs >= ID_MIN_VOTES votes, strictly more
 * than "unknown", and no tie with another slot. Returns the slot or -1. */
static int tally(const int *votes_cls, int n, int *k_out)
{
    int votes[NCC_MAX_CLASSES] = { 0 };
    int unk = 0;
    for (int i = 0; i < n; i++) {
        if (votes_cls[i] >= 0) votes[votes_cls[i]]++;
        else unk++;
    }
    int  best = -1;
    bool tie  = false;
    for (int c = 0; c < NCC_MAX_CLASSES; c++) {
        if (votes[c] == 0) continue;
        if (best < 0 || votes[c] > votes[best]) { best = c; tie = false; }
        else if (votes[c] == votes[best])      { tie = true; }
    }
    if (best >= 0 && !tie && votes[best] >= ID_MIN_VOTES && votes[best] > unk) {
        *k_out = votes[best];
        return best;
    }
    *k_out = unk;
    return -1;
}

#define ID_SEEDS      3
#define ID_TEST_WALKS 3

static float s_win[ID_ENROLL][GAIT_NFEAT];

typedef struct {
    int     conf[3][3];              /* [truth A/B/C][pred A / B / unknown] */
    int     walk_label[3][3];        /* 7-window vote per walk, same layout */
    double  own_d_sum;
    int     own_d_n;
    agree_t ag;
} rec_t;

/* The whole recognition flow over ID_SEEDS rooms. path_drift selects the
 * walker model (see PATH_TAU_S). Every room: calibrate, enrol A then B
 * with ONE session of ID_ENROLL windows each (main.c's procedure), then
 * ID_TEST_WALKS separate walks of A, B and C, each its own presence
 * episode with a 7-window vote as main.c's id_tally() counts it. */
static void run_recognition(bool path_drift, bool verbose, rec_t *o)
{
    memset(o, 0, sizeof(*o));
    s_path_drift = path_drift;

    for (int seed = 0; seed < ID_SEEDS; seed++) {
        rng_seed(1000u + (uint32_t)seed);
        room_make();
        fe_calibrate(100.0f);
        run_empty(EMPTY_AFTER_CAL);
        if (verbose && seed == 0) print_calibration();
        CHECK(s_fe.nsel == 52, "seed %d: %d subcarriers selected, want the 52 data ones", seed, s_fe.nsel);
        gait_init(&s_gait, s_fe.rate);            /* analytics_configure's rate */
        ncc_init(&s_id_model, GAIT_NFEAT);

        for (int w = 0; w < 2; w++) {
            int ok = 0;
            ncc_set_name(&s_id_model, w, k_walkers[w].name);
            int got = walk_windows(&k_walkers[w], ID_ENROLL, s_win, &o->ag);
            for (int i = 0; i < got; i++) ok += ncc_add(&s_id_model, w, s_win[i]) ? 1 : 0;
            CHECK(got == ID_ENROLL && ok == ID_ENROLL, "seed %d enrol %s: %d windows, %d added",
                  seed, k_walkers[w].name, got, ok);
        }
        CHECK(ncc_trained_classes(&s_id_model, ID_MIN_WIN) == 2, "seed %d: two usable templates", seed);

        int sc_ok = 0, sc_n = 0, c_unk = 0, c_n = 0;
        for (int w = 0; w < 3; w++) {
            for (int walk = 0; walk < ID_TEST_WALKS; walk++) {
                int vote[ID_VOTES];
                int got = walk_windows(&k_walkers[w], ID_VOTES, s_win, &o->ag);
                CHECK(got == ID_VOTES, "seed %d walker %s: %d windows", seed, k_walkers[w].name, got);
                for (int i = 0; i < got; i++) {
                    ncc_result_t r = ncc_classify(&s_id_model, s_win[i], ID_MIN_WIN,
                                                  ID_REJECT, ID_MARGIN);
                    o->conf[w][(r.best >= 0) ? r.best : 2]++;
                    vote[i] = r.best;
                    if (w < 2) {
                        sc_n++;
                        if (r.best == w) sc_ok++;
                        if (r.nearest == w) { o->own_d_sum += (double)r.dist; o->own_d_n++; }
                    } else {
                        c_n++;
                        if (r.best < 0) c_unk++;
                    }
                }
                int k = 0;
                int label = tally(vote, got, &k);
                o->walk_label[w][label >= 0 ? label : 2]++;
            }
        }
        if (verbose) {
            printf("    seed %d: A/B windows correct %d/%d (%.1f%%), C windows unknown %d/%d (%.1f%%)\n",
                   seed, sc_ok, sc_n, 100.0 * sc_ok / (sc_n ? sc_n : 1), c_unk, c_n,
                   100.0 * c_unk / (c_n ? c_n : 1));
        }
    }
    s_path_drift = true;
}

static float rec_ab_correct(const rec_t *o)
{
    int n = o->conf[0][0] + o->conf[0][1] + o->conf[0][2] + o->conf[1][0] + o->conf[1][1] + o->conf[1][2];
    return 100.0f * (float)(o->conf[0][0] + o->conf[1][1]) / (float)(n ? n : 1);
}

static float rec_c_unknown(const rec_t *o)
{
    int n = o->conf[2][0] + o->conf[2][1] + o->conf[2][2];
    return 100.0f * (float)o->conf[2][2] / (float)(n ? n : 1);
}

static void rec_summary(const rec_t *o)
{
    int ab_n = o->conf[0][0] + o->conf[0][1] + o->conf[0][2] + o->conf[1][0] + o->conf[1][1] + o->conf[1][2];
    int c_n  = o->conf[2][0] + o->conf[2][1] + o->conf[2][2];
    printf("    enrolled A/B: %.1f%% correct, %.1f%% unknown, %.1f%% the other name; "
           "mean distance to own template %.2f\n",
           (double)rec_ab_correct(o), 100.0 * (o->conf[0][2] + o->conf[1][2]) / (ab_n ? ab_n : 1),
           100.0 * (o->conf[0][1] + o->conf[1][0]) / (ab_n ? ab_n : 1),
           o->own_d_n ? o->own_d_sum / o->own_d_n : 0.0);
    printf("    never-enrolled C: %.1f%% unknown, %.1f%% called A, %.1f%% called B\n",
           (double)rec_c_unknown(o), 100.0 * o->conf[2][0] / (c_n ? c_n : 1),
           100.0 * o->conf[2][1] / (c_n ? c_n : 1));
    printf("    7-window vote per held-out walk (main.c's rule), label A / B / unknown:\n"
           "        walks of A %d/%d/%d, of B %d/%d/%d, of C %d/%d/%d\n",
           o->walk_label[0][0], o->walk_label[0][1], o->walk_label[0][2],
           o->walk_label[1][0], o->walk_label[1][1], o->walk_label[1][2],
           o->walk_label[2][0], o->walk_label[2][1], o->walk_label[2][2]);
}

static rec_t s_rec;

static void test_recognition(void)
{
    const rec_t *o = &s_rec;

    printf("(a) recognition at 100 Hz: enrol A and B (%d windows, one session each), then %d\n"
           "    held-out walks x %d windows each of A, B and the never-enrolled C;\n"
           "    ncc_classify(min_n %d, reject %.2f, margin %.2f), %d seeds\n",
           ID_ENROLL, ID_TEST_WALKS, ID_VOTES, ID_MIN_WIN, (double)ID_REJECT,
           (double)ID_MARGIN, ID_SEEDS);

    run_recognition(true, true, &s_rec);
    printf("    window confusion, pooled (rows = truth):   pred A  pred B  unknown\n");
    for (int w = 0; w < 3; w++) {
        printf("        %s%s %6d  %6d  %6d\n", k_walkers[w].name,
               w == 2 ? " (never enrolled)       " : "                        ",
               o->conf[w][0], o->conf[w][1], o->conf[w][2]);
    }
    rec_summary(o);
    const agree_t *ag = &o->ag;
    double pkm   = (double)(ag->pk_moving ? ag->pk_moving : 1);
    double hf_mu = ag->hf_n ? ag->hf_sum / (double)ag->hf_n : -1.0;
    printf("    Step 08 motion flag while walking: %.1f%% of packets (gait windows need 80%%); "
           "group signal RMS %.2f noise units\n"
           "    motion gate while walking: MOVING on %.1f%% of packets, hf mean %.1f "
           "(threshold %.1f), hf below it on %.1f%%\n"
           "    gait windows the device gets, of the %ld truth-gated ones: %ld with moving = "
           "flag && !still (main.c), %ld with the flag alone (before the gate)\n",
           100.0 * (double)ag->flag_moving / pkm,
           sqrt(ag->grp_sq / (pkm * (double)CSI_NGROUPS)),
           100.0 * (double)ag->gate_moving / pkm, hf_mu, (double)GATE_RATIO,
           100.0 * (double)ag->hf_below / (double)(ag->hf_n ? ag->hf_n : 1),
           ag->win_truth, ag->win_dev, ag->win_flag);

    float acc = rec_ab_correct(o), c_unk = rec_c_unknown(o);
    CHECK(acc >= 85.0f, "enrolled walkers correct %.1f%%, want >= 85%%", (double)acc);
    CHECK(c_unk >= 50.0f, "never-enrolled walker unknown %.1f%%, want >= 50%%", (double)c_unk);
    CHECK(o->walk_label[0][1] == 0 && o->walk_label[1][0] == 0,
          "a held-out walk was reported as the OTHER enrolled person (A->B %d, B->A %d)",
          o->walk_label[0][1], o->walk_label[1][0]);
    CHECK((double)ag->flag_moving >= 0.8 * (double)ag->pk_moving,
          "Step 08 flags only %ld of %ld walking packets as moving", ag->flag_moving, ag->pk_moving);
    /* What the gate does to walking. NOT "far above the threshold": these
     * synthetic walkers (part of their modulation is the 1.6-2.2 Hz step
     * rate, right at the gate's ~1.6 Hz corner) average hf ~6 - 1.5x the
     * default 4.0 - and dip below it on ~30% of packets, so the gate
     * calls ~81% of walking packets MOVING and the
     * device loses ~23% of the gait windows the flag alone gave it (the
     * price of vetoing a breathing person's windows). With threshold 3.0
     * the same run gives 94% / 250 windows, with 2.5 97% / 263 (build with
     * /DGATE_RATIO=3.0f). How strong REAL walking is in hf is unmeasured.
     * Asserted: hf's mean is above the threshold; the rest are regression
     * guards set just under the values measured on 2026-09-24 (80.8% and
     * 222/287), not targets. */
    CHECK(ag->hf_n > 0 && hf_mu >= (double)GATE_RATIO,
          "walking hf mean %.2f, below the threshold %.1f", hf_mu, (double)GATE_RATIO);
    CHECK((double)ag->gate_moving >= 0.75 * (double)ag->pk_moving,
          "the gate calls only %ld of %ld walking packets MOVING", ag->gate_moving, ag->pk_moving);
    CHECK((double)ag->win_dev >= 0.70 * (double)ag->win_flag,
          "device gait windows %ld (flag && !still) vs %ld (flag alone)", ag->win_dev, ag->win_flag);

    /* The same flow with the path FROZEN for each walk (test_gait's model):
     * printed, and only the "never the wrong name" property asserted - it
     * is the failure mode to know about, not a property of the code. */
    printf("    same, path frozen during each walk (an enrolment that walks ONE route):\n");
    run_recognition(false, false, &s_rec);
    rec_summary(o);
    CHECK(o->conf[0][1] + o->conf[1][0] == 0,
          "frozen path: an enrolled walker was called by the other name (%d windows)",
          o->conf[0][1] + o->conf[1][0]);
}

/* ================================================================== */
/*  (b) one versus two movers                                          */
/* ================================================================== */

/* One presence episode: 1 or 2 walkers moving together, until nblk blocks
 * have been collected. count state starts fresh, as after count_reset.
 * The blocks are main.c's: moving = flag && !still (so feature [4], the
 * duty, is the device's), and only blocks with duty >= COUNT_MIN_DUTY -
 * the ones main.c trains on and classifies - are returned. s_dev_blk /
 * s_dev_blk_ok count all blocks and those that qualified. */
static long s_dev_blk, s_dev_blk_ok;
static long s_blk_cap = 400000;      /* slots per walk; (f) lowers it */

static int walk_blocks(int w1, int w2, int nblk, float (*out)[COUNT_NFEAT])
{
    session_t s1, s2;
    scene_t   sc;
    float     dev_feat[COUNT_NFEAT];
    int       got = 0;

    memset(&sc, 0, sizeof(sc));
    session_start(&s1, &k_walkers[w1], s_fe.rate_nom);
    sc.walker[0] = &s1;
    sc.nwalker   = 1;
    if (w2 >= 0) {
        session_start(&s2, &k_walkers[w2], s_fe.rate_nom);
        sc.walker[1] = &s2;
        sc.nwalker   = 2;
    }
    count_reset(&s_cnt_dev);
    for (long i = 0; got < nblk && i < s_blk_cap; i++) {
        if (!slot(&sc)) continue;
        if (count_push(&s_cnt_dev, s_fe.grp, s_fe.motion_flag && !s_fe.still, dev_feat)) {
            s_dev_blk++;
            if (dev_feat[4] >= COUNT_MIN_DUTY) {
                s_dev_blk_ok++;
                memcpy(out[got], dev_feat, sizeof(dev_feat));
                got++;
            }
        }
    }
    run_empty(5.0f);
    return got;
}

#define CNT_SEEDS   3
#define CNT_TEST    10               /* held-out blocks per class per seed */

static float s_blk[CNT_TRAIN][COUNT_NFEAT];

static void test_count_e2e(void)
{
    int conf[2][2];                  /* [truth one/two][pred one/two]   */
    int info_c[2] = { 0, 0 };        /* C alone: pred one / two          */
    int info_ac[2] = { 0, 0 };       /* A + C:   pred one / two          */
    int ring_ok = 0, ring_n = 0;
    float ma = 0.0f, mb = 0.0f, dot = 0.0f;
    double duty_mu[2] = { 0.0, 0.0 }, duty_sd[2] = { 0.0, 0.0 };   /* info, over seeds */

    memset(conf, 0, sizeof(conf));
    for (int j = 0; j < CSI_NGROUPS; j++) {
        dot += k_walkers[0].mix[j] * k_walkers[1].mix[j];
        ma  += k_walkers[0].mix[j] * k_walkers[0].mix[j];
        mb  += k_walkers[1].mix[j] * k_walkers[1].mix[j];
    }
    printf("(b) one versus two movers at 50 Hz (default ping): train %d + %d blocks\n"
           "    (\"one\" = A or B alone, \"two-plus\" = A and B together), classify %d + %d\n"
           "    held-out blocks per seed with reject 1e9 / margin 1.0, %d seeds.\n"
           "    Every block is main.c's: moving = flag && !still (the duty feature is the\n"
           "    device's), and only blocks with duty >= %.1f are trained on or classified.\n"
           "    Mixing directions of A and B: cos %.2f (nearly orthogonal - the easy case)\n",
           CNT_TRAIN, CNT_TRAIN, CNT_TEST, CNT_TEST, CNT_SEEDS, (double)COUNT_MIN_DUTY,
           (double)(dot / sqrtf(ma * mb)));

    s_dev_blk    = 0;
    s_dev_blk_ok = 0;
    for (int seed = 0; seed < CNT_SEEDS; seed++) {
        rng_seed(2000u + (uint32_t)seed);
        room_make();
        fe_calibrate(50.0f);
        run_empty(EMPTY_AFTER_CAL);
        if (seed == 0) print_calibration();
        count_init(&s_cnt_dev);
        ncc_init(&s_cnt_model, COUNT_NFEAT);
        ncc_set_name(&s_cnt_model, 0, "one");
        ncc_set_name(&s_cnt_model, 1, "two-plus");

        /* training: 4 walks x 5 qualifying blocks per class - walk_blocks
         * returns only blocks main.c would keep (duty >= COUNT_MIN_DUTY) */
        for (int cls = 0; cls < 2; cls++) {
            int added = 0;
            for (int walk = 0; walk < 4; walk++) {
                int w1 = walk & 1;
                int got = walk_blocks(w1, cls ? 1 - w1 : -1, 5, s_blk);
                for (int i = 0; i < got; i++) {
                    if (s_blk[i][4] >= COUNT_MIN_DUTY && ncc_add(&s_cnt_model, cls, s_blk[i])) added++;
                }
            }
            CHECK(added == CNT_TRAIN, "seed %d class %d: %d blocks added", seed, cls, added);
            /* info: the device duty the model learnt (feature [4]) */
            const ncc_class_t *k = &s_cnt_model.cls[cls];
            if (k->n > 1) {
                duty_mu[cls] += (double)k->mean[4] / CNT_SEEDS;
                duty_sd[cls] += sqrt((double)k->m2[4] / (double)(k->n - 1)) / CNT_SEEDS;
            }
        }
        CHECK(ncc_trained_classes(&s_cnt_model, CNT_TRAIN) == 2, "seed %d: count model trained", seed);

        /* held out: "one" = A alone then B alone (5 blocks each walk),
         * "two" = A + B, two walks of 5. Each walk is one presence
         * episode with its own 5-block majority ring, as main.c. */
        for (int cls = 0; cls < 2; cls++) {
            for (int walk = 0; walk < 2; walk++) {
                int got = walk_blocks(walk, cls ? 1 - walk : -1, CNT_TEST / 2, s_blk);
                int twos = 0, nv = 0, est = 1;
                for (int i = 0; i < got; i++) {
                    ncc_result_t r = ncc_classify(&s_cnt_model, s_blk[i], CNT_TRAIN, 1e9f, 1.0f);
                    CHECK(r.best == 0 || r.best == 1, "count classify gave %d", r.best);
                    if (r.best != 0 && r.best != 1) continue;
                    conf[cls][r.best]++;
                    twos += r.best;
                    nv++;                          /* ring of 5, 5 blocks: all */
                    est = (2 * twos > nv) ? 2 : 1;
                }
                ring_n++;
                if (est == cls + 1) ring_ok++;
            }
        }
        /* not asserted: a mover never trained on (C) */
        {
            int got = walk_blocks(2, -1, 5, s_blk);
            for (int i = 0; i < got; i++) {
                ncc_result_t r = ncc_classify(&s_cnt_model, s_blk[i], CNT_TRAIN, 1e9f, 1.0f);
                if (r.best == 0 || r.best == 1) info_c[r.best]++;
            }
            got = walk_blocks(0, 2, 5, s_blk);
            for (int i = 0; i < got; i++) {
                ncc_result_t r = ncc_classify(&s_cnt_model, s_blk[i], CNT_TRAIN, 1e9f, 1.0f);
                if (r.best == 0 || r.best == 1) info_ac[r.best]++;
            }
        }
    }

    int n  = conf[0][0] + conf[0][1] + conf[1][0] + conf[1][1];
    int ok = conf[0][0] + conf[1][1];
    float acc = 100.0f * (float)ok / (float)(n ? n : 1);
    printf("    block confusion, pooled (rows = truth):   pred one  pred two\n");
    printf("        one mover                             %6d    %6d\n", conf[0][0], conf[0][1]);
    printf("        two movers                            %6d    %6d\n", conf[1][0], conf[1][1]);
    printf("    per-block accuracy %d/%d = %.1f%%; 5-block majority per walk correct %d/%d\n",
           ok, n, (double)acc, ring_ok, ring_n);
    printf("    (info, never trained on C) C alone: one %d / two %d;  A + C: one %d / two %d\n",
           info_c[0], info_c[1], info_ac[0], info_ac[1]);
    printf("    device path (moving = flag && !still): %ld of %ld blocks reach duty %.1f\n",
           s_dev_blk_ok, s_dev_blk, (double)COUNT_MIN_DUTY);
    printf("    (info) trained duty, mean / sd over seeds: one %.3f / %.3f, two-plus %.3f / %.3f\n",
           duty_mu[0], duty_sd[0], duty_mu[1], duty_sd[1]);

    CHECK(acc >= 75.0f, "count per-block accuracy %.1f%%, want >= 75%%", (double)acc);
    /* regression guard (measured 210/229 on 2026-09-24 at threshold 4.0,
     * since (b) trains on the device path), see (a) */
    CHECK(s_dev_blk > 0 && (double)s_dev_blk_ok >= 0.85 * (double)s_dev_blk,
          "device path: only %ld of %ld walking blocks reach the count duty", s_dev_blk_ok, s_dev_blk);
}

/* ================================================================== */
/*  (c) vitals                                                         */
/* ================================================================== */
#define BR_TOL   1.0f              /* breathing within this of the truth = "recovered", /min */
#define HR_TOL   3.0f              /* heart rate within this of the truth = "recovered", bpm */

/* What vitals are told "still" by. The front end, Step 08's flag and
 * presence, and the gate run on every packet whichever is chosen. */
typedef enum {
    VG_TRUTH = 0,                  /* ground truth: sitting = still                   */
    VG_GATE,                       /* the motion gate - main.c now, the device path   */
    VG_FLAG,                       /* !Step 08 flag - the device path before the gate */
} vmode_t;

typedef struct {
    vitals_out_t last;
    int   analyses;                /* completed while sitting                        */
    int   br_n, br_ok;             /* ... reporting breathing / within BR_TOL        */
    int   hr_n, hr_ok;             /* ... reporting a heart rate / within HR_TOL     */
    float flagged;                 /* share of sitting packets Step 08 calls moving  */
    int   episodes;                /* ... in this many separate episodes             */
    float gate_moving;             /* share of sitting packets the gate calls MOVING */
    float hf_max;                  /* largest hf while sitting                       */
    float br_err_max, hr_err_max;  /* over analyses that reported a rate             */
} vrun_t;

static bool vstill(vmode_t m, bool sitting)
{
    switch (m) {
    case VG_TRUTH: return sitting;
    case VG_GATE:  return s_fe.still;
    default:       return !s_fe.motion_flag;
    }
}

/* A person walks in (5 s) and sits still for sit_s seconds. The flag /
 * gate / hf statistics skip the first 3 s of sitting: the flag's release,
 * and the gate's decay from walking plus its 1 s hold. */
static vrun_t vitals_run(const chest_t *ch, float sit_s, vmode_t mode,
                         float bpm_br, float bpm_hr)
{
    vrun_t    o;
    session_t s;
    scene_t   sc;
    long      pk = 0, fl = 0, gm = 0;
    bool      prev = false;

    memset(&o, 0, sizeof(o));
    memset(&sc, 0, sizeof(sc));
    vitals_configure(&s_vit, s_fe.sel, NSUB);      /* also resets */

    session_start(&s, &k_walkers[0], s_fe.rate_nom);
    sc.walker[0] = &s;
    sc.nwalker   = 1;
    int n = (int)(5.0f * s_fe.rate_nom);
    for (int i = 0; i < n; i++) {
        if (!slot(&sc)) continue;
        (void)vitals_push(&s_vit, s_fe.t_us, s_fe.na, vstill(mode, false));
    }

    sc.nwalker     = 0;
    sc.chest       = ch;
    sc.chest_t0_us = s_fe.t_us;
    n = (int)(sit_s * s_fe.rate_nom);
    for (int i = 0; i < n; i++) {
        if (!slot(&sc)) continue;
        if ((float)i > 3.0f * s_fe.rate_nom) {
            float hf = mgate_ratio(&s_fe.gate);
            pk++;
            if (s_fe.motion_flag) fl++;
            if (s_fe.motion_flag && !prev) o.episodes++;
            prev = s_fe.motion_flag;
            if (!s_fe.still) gm++;
            if (hf > o.hf_max) o.hf_max = hf;
        }
        if (vitals_push(&s_vit, s_fe.t_us, s_fe.na, vstill(mode, true))) {
            const vitals_out_t *v = vitals_get(&s_vit);
            o.analyses++;
            if (v->br_valid) {
                float e = fabsf(v->br_bpm - bpm_br);
                o.br_n++;
                if (e <= BR_TOL) o.br_ok++;
                if (e > o.br_err_max) o.br_err_max = e;
            }
            if (v->hr_valid) {
                float e = fabsf(v->hr_bpm - bpm_hr);
                o.hr_n++;
                if (e <= HR_TOL) o.hr_ok++;
                if (e > o.hr_err_max) o.hr_err_max = e;
            }
        }
    }
    o.last        = *vitals_get(&s_vit);
    o.flagged     = (float)fl / (float)(pk ? pk : 1);
    o.gate_moving = (float)gm / (float)(pk ? pk : 1);
    return o;
}

static void print_vrun(const char *what, const vrun_t *o)
{
    const vitals_out_t *v = &o->last;
    printf("      %-26s %-9s br %5.2f conf %6.1f%s hr~%6.2f conf %5.1f%s | analyses %2d, br %2d/%2d, "
           "hr %2d/%2d | flag %5.1f%% (%2d ep.), gate moving %5.1f%%, hf max %5.2f\n",
           what, vitals_state_name(v->state), (double)v->br_bpm, (double)v->br_conf,
           v->br_valid ? "*" : " ", (double)v->hr_bpm, (double)v->hr_conf, v->hr_valid ? "*" : " ",
           o->analyses, o->br_n, o->br_ok, o->hr_n, o->hr_ok,
           100.0 * (double)o->flagged, o->episodes, 100.0 * (double)o->gate_moving, (double)o->hf_max);
}

static chest_t s_chest;

static void test_vitals_e2e(void)
{
    const float br = 15.0f, hr = 66.0f;     /* 66: clear of 15/min's harmonics */

    printf("(c) vitals at 50 Hz: walk in 5 s, sit still 60 s; breathing %.0f/min (2nd/3rd\n"
           "    harmonics 0.3/0.15), heart %.0f bpm. Amplitudes in units of the per-packet\n"
           "    noise, per subcarrier x U(0.5,1.5). A real heartbeat moves the chest 10-50x\n"
           "    less than breathing: rows with heart = breathing / 10 or / 20 are the\n"
           "    plausible ones, the first (1/3) is GENEROUS by design.\n"
           "    Columns: final state, breathing and heart (* = valid) at the end; then over the\n"
           "    run: analyses, br reported/recovered (within %.0f/min), hr reported/recovered\n"
           "    (within %.0f bpm); Step 08 flag and gate while sitting (after 3 s), hf max.\n"
           "    device = main.c (still = motion gate); flag = the device path before the gate.\n",
           (double)br, (double)hr, (double)BR_TOL, (double)HR_TOL);

    rng_seed(3000u);
    room_make();
    fe_calibrate(50.0f);
    run_empty(EMPTY_AFTER_CAL);
    print_calibration();
    printf("    gate after %.0f s of empty room: floor %s, hf %.2f, %s\n", (double)EMPTY_AFTER_CAL,
           s_fe.gate.have_empty ? "from the empty room" : "NOT learnt", (double)mgate_ratio(&s_fe.gate),
           s_fe.still ? "STILL" : "MOVING");
    CHECK(s_fe.gate.have_empty && s_fe.still, "gate after the empty room: have_empty %d, still %d",
          (int)s_fe.gate.have_empty, (int)s_fe.still);
    vitals_init(&s_vit, &k_vcfg);

    chest_make(&s_chest, 0.6f, 0.2f, br, hr);
    vrun_t t = vitals_run(&s_chest, 60.0f, VG_TRUTH, br, hr);
    print_vrun("0.6 / 0.20 truth", &t);
    vrun_t f = vitals_run(&s_chest, 60.0f, VG_GATE, br, hr);
    print_vrun("0.6 / 0.20 device", &f);

    CHECK(t.last.state == VIT_OK, "truth-gated: final state %s", vitals_state_name(t.last.state));
    CHECK(t.last.br_valid && fabsf(t.last.br_bpm - br) <= BR_TOL, "truth-gated breathing %.2f/min",
          (double)t.last.br_bpm);
    CHECK(t.last.hr_valid && fabsf(t.last.hr_bpm - hr) <= 2.0f, "truth-gated heart %.2f bpm (valid %d)",
          (double)t.last.hr_bpm, (int)t.last.hr_valid);
    CHECK(t.br_n > 0 && t.br_err_max <= BR_TOL, "a reported breathing rate was %.2f/min off",
          (double)t.br_err_max);
    CHECK(t.hr_n > 0 && t.hr_err_max <= HR_TOL, "a reported heart rate was %.2f bpm off",
          (double)t.hr_err_max);
    CHECK(f.br_n * 2 >= t.br_n, "device path: %d analyses with breathing vs %d truth-gated",
          f.br_n, t.br_n);
    CHECK(f.br_n == 0 || f.br_err_max <= BR_TOL, "device path: a breathing rate was %.2f/min off",
          (double)f.br_err_max);
    CHECK(f.hr_n == 0 || f.hr_err_max <= HR_TOL, "device path: a heart rate was %.2f bpm off",
          (double)f.hr_err_max);

    /* The sweep. "flag" is the old device path: Step 08's fast EMA has its
     * corner at ~0.3 Hz, INSIDE the breathing band, so breathing lifts M
     * and, from ~1 noise-sd per subcarrier, holds the flag on - vitals
     * never ran. "device" is main.c now: the gate looks above ~1.6 Hz. The
     * two breathing-strong rows at heart = breathing / 10 are the reason
     * the gate exists, and are asserted; the rest is printed, apart from
     * two properties that must hold at every size: breathing never trips
     * the gate, and the device path never reports a wrong breathing rate. */
    printf("    sweep, breathing / heart amplitude:\n");
    static const float sweep[][2] = {
        { 0.2f, 0.02f }, { 0.4f, 0.04f }, { 0.6f, 0.06f }, { 1.0f, 0.10f },
        { 2.0f, 0.20f }, { 4.0f, 0.40f }, { 4.0f, 0.20f }, { 0.6f, 0.12f }, { 0.6f, 0.30f },
    };
    for (size_t i = 0; i < sizeof(sweep) / sizeof(sweep[0]); i++) {
        char  lbl[64];
        float ab = sweep[i][0], ah = sweep[i][1];
        chest_make(&s_chest, ab, ah, br, hr);
        vrun_t d = vitals_run(&s_chest, 60.0f, VG_GATE, br, hr);
        snprintf(lbl, sizeof(lbl), "%.1f / %.2f (1/%.0f) device", (double)ab, (double)ah, (double)(ab / ah));
        print_vrun(lbl, &d);
        vrun_t g = vitals_run(&s_chest, 60.0f, VG_FLAG, br, hr);
        snprintf(lbl, sizeof(lbl), "%.1f / %.2f flag", (double)ab, (double)ah);
        print_vrun(lbl, &g);
        vrun_t b = vitals_run(&s_chest, 60.0f, VG_TRUTH, br, hr);
        snprintf(lbl, sizeof(lbl), "%.1f / %.2f truth", (double)ab, (double)ah);
        print_vrun(lbl, &b);

        CHECK(d.br_n == 0 || d.br_err_max <= BR_TOL, "%.1f / %.2f device: a breathing rate %.2f/min off",
              (double)ab, (double)ah, (double)d.br_err_max);
        CHECK(d.gate_moving == 0.0f && d.hf_max < GATE_RATIO,
              "%.1f / %.2f: the gate called %.1f%% of sitting packets MOVING (hf max %.2f)",
              (double)ab, (double)ah, 100.0 * (double)d.gate_moving, (double)d.hf_max);
        if ((ab == 2.0f && ah == 0.20f) || (ab == 4.0f && ah == 0.40f)) {
            CHECK(d.analyses > 0 && d.br_ok > 0 && d.br_ok == d.br_n,
                  "%.1f / %.2f device: %d analyses, breathing %d reported / %d recovered",
                  (double)ab, (double)ah, d.analyses, d.br_n, d.br_ok);
            CHECK(d.br_n * 2 >= b.br_n, "%.1f / %.2f device: %d analyses with breathing vs %d truth-gated",
                  (double)ab, (double)ah, d.br_n, b.br_n);
            CHECK(d.last.br_valid && fabsf(d.last.br_bpm - br) <= BR_TOL,
                  "%.1f / %.2f device: final breathing %.2f/min (valid %d)",
                  (double)ab, (double)ah, (double)d.last.br_bpm, (int)d.last.br_valid);
        }
    }

    /* The gate's documented weakness, shown rather than hidden: the floor
     * forgotten just before the person walks in (as after a boot that
     * reloads a baseline with somebody already walking), so the walk
     * itself becomes the min-statistics floor. Printed only. */
    mgate_set_rate(&s_fe.gate, s_fe.rate, false);
    chest_make(&s_chest, 2.0f, 0.20f, br, hr);
    vrun_t w = vitals_run(&s_chest, 60.0f, VG_GATE, br, hr);
    print_vrun("2.0 / 0.20 dev, no floor", &w);
    printf("        (info: the gate had never seen the room empty - the walk-in was its first data)\n");
}

/* ================================================================== */
/*  (d) empty room: nothing may come out of the noise                  */
/* ================================================================== */
static void test_empty_room(void)
{
    int    analyses = 0, br_n = 0, floor_known = 0;
    long   pk = 0, fl = 0, gpk = 0, gm = 0, hf_n = 0;
    double hf_sum = 0.0;
    float  max_conf = 0.0f, hf_max = 0.0f;

    printf("(d) empty room, 3 seeds x 120 s at 50 Hz, vitals gated by the motion gate (device path)\n");
    for (int seed = 0; seed < 3; seed++) {
        scene_t sc;
        memset(&sc, 0, sizeof(sc));
        rng_seed(4000u + (uint32_t)seed);
        room_make();
        fe_calibrate(50.0f);
        vitals_init(&s_vit, &k_vcfg);
        vitals_configure(&s_vit, s_fe.sel, NSUB);
        int n = (int)(120.0f * s_fe.rate_nom);
        for (int i = 0; i < n; i++) {
            if (!slot(&sc)) continue;
            pk++;
            if (s_fe.motion_flag) fl++;
            /* after the first floor second and the 1 s hold */
            if ((float)i > 3.0f * s_fe.rate_nom) {
                float hf = mgate_ratio(&s_fe.gate);
                gpk++;
                if (!s_fe.still) gm++;
                if (hf >= 0.0f) {
                    hf_n++;
                    hf_sum += (double)hf;
                    if (hf > hf_max) hf_max = hf;
                }
            }
            if (vitals_push(&s_vit, s_fe.t_us, s_fe.na, s_fe.still)) {
                const vitals_out_t *v = vitals_get(&s_vit);
                analyses++;
                if (v->br_valid) br_n++;
                if (v->br_conf > max_conf) max_conf = v->br_conf;
            }
        }
        if (s_fe.gate.have_empty) floor_known++;
    }
    double hf_mu = hf_n ? hf_sum / (double)hf_n : -1.0;
    printf("    %d analyses, %d with breathing (max br_conf %.2f, threshold %.2f); "
           "Step 08 motion flag on %.2f%% of packets\n"
           "    gate: floor learnt from the empty room in %d/3 seeds; after 3 s MOVING on %.2f%% "
           "of packets, hf mean %.2f max %.2f (threshold %.1f)\n",
           analyses, br_n, (double)max_conf, (double)k_vcfg.br_conf_min,
           100.0 * (double)fl / (double)(pk ? pk : 1),
           floor_known, 100.0 * (double)gm / (double)(gpk ? gpk : 1), hf_mu, (double)hf_max,
           (double)GATE_RATIO);
    CHECK(analyses > 0, "no vitals analyses ran in an empty room");
    CHECK(br_n == 0, "%d empty-room analyses reported breathing", br_n);
    CHECK(floor_known == 3, "the gate learnt its floor from the empty room in only %d/3 seeds", floor_known);
    CHECK(hf_n > 0 && hf_mu >= 0.7 && hf_mu <= 1.3, "empty-room hf mean %.2f, want ~1", hf_mu);
    CHECK(hf_max < GATE_RATIO, "empty-room hf reached %.2f", (double)hf_max);
    CHECK(gm == 0, "the gate called %ld empty-room packets MOVING", gm);
}

/* ================================================================== */
/*  (e) whose vitals: main.c's per-packet Step 10 glue                 */
/* ================================================================== */
#define CARRY_US  60000000LL       /* main.c CSI_CARRY_US: 2 x CONFIG_CSI_VITALS_WINDOW_S 30 */
#ifndef E_SEED                     /* overridable (/DE_SEED=5001u): other rooms, same checks */
#define E_SEED    5000u
#endif

/* main.c's Step 10 statics that (a)-(d) have no use for. The names say
 * which main.c static each one mirrors. */
typedef struct {
    bool    present;               /* main.c `present`: AFTER the breathing hold    */
    bool    prev_still;            /* s_prev_still                                  */
    bool    began_present;         /* s_still_began_present                         */
    bool    breath_hold;           /* this packet: the hold asserted PRESENT        */
    int     vote[ID_VOTES];        /* s_vote_cls: slot or PV_LABEL_UNKNOWN          */
    int     vote_n, vote_head;
    int     cnt_vote[CNT_VOTES];   /* s_cnt_vote: 0 = one, 1 = two or more          */
    int     cnt_vote_n, cnt_vote_head;
    int     cnt_est;               /* s_cnt_est: 0 = none this episode, 1 or 2      */
    int     cnt_carry;             /* s_cnt_carry                                   */
    int64_t cnt_absent_us;         /* s_cnt_absent_us                               */
    bool    carry_wait;            /* s_carry_wait                                  */
    /* events, for the scenarios */
    int     starts, ends;          /* presence transitions                          */
    int64_t start_us, end_us;      /* ... the latest of each                        */
    int     hold_starts;           /* starts made by breathing alone (">>> still
                                      person, breathing ... - holding PRESENT")     */
    int     credits;               /* pv_analysis() credited a sample               */
    int     windows;               /* gait windows id_step got (device path)        */
} glue_t;

static glue_t  s_gl;
static pv_t    s_pv;                /* main.c's s_pv, the module under test in (e) */
static act_t   s_act;               /* main.c's s_act (activity.h)                 */
static chest_t s_chest2;            /* e2's second seated person                   */

/* current_count() */
static int gl_count(void)
{
    if (!s_gl.present) return 0;
    return (s_gl.cnt_est > 0) ? s_gl.cnt_est : 1;
}

/* analytics_reset(): false at presence end, true for no-CSI / recal /
 * a new baseline. cnt_votes_clear() drops a carried count too; the
 * presence-end caller re-takes it afterwards, as main.c does. */
static void gl_reset(bool with_vitals)
{
    if (with_vitals) {
        vitals_reset(&s_vit);
        s_gl.prev_still    = false;
        s_gl.began_present = false;
        pv_release(&s_pv);
        mgate_reset(&s_fe.gate, true);
        act_reset(&s_act);
    }
    gait_reset(&s_gait);
    s_gl.vote_n        = 0;
    s_gl.vote_head     = 0;
    count_reset(&s_cnt_dev);
    s_gl.cnt_vote_n    = 0;
    s_gl.cnt_vote_head = 0;
    s_gl.cnt_est       = 0;
    s_gl.cnt_carry     = 0;
    s_gl.carry_wait    = false;
}

/* who_present(): PRESENT as far as the latch is concerned */
static bool gl_who_present(void)
{
    return s_gl.present && !s_gl.carry_wait;
}

/* count_step(), classification branch (no session runs in (e)) */
static void gl_count_step(bool moving)
{
    float feat[COUNT_NFEAT];
    if (!count_push(&s_cnt_dev, s_fe.grp, moving, feat)) return;
    if (!(s_cnt_model.cls[0].n >= (uint32_t)CNT_TRAIN && s_cnt_model.cls[1].n >= (uint32_t)CNT_TRAIN) ||
        !(feat[4] >= COUNT_MIN_DUTY)) {
        return;
    }
    ncc_result_t r = ncc_classify(&s_cnt_model, feat, CNT_TRAIN, 1e9f, 1.0f);
    if (r.best != 0 && r.best != 1) return;

    s_gl.cnt_vote[s_gl.cnt_vote_head] = r.best;
    s_gl.cnt_vote_head = (s_gl.cnt_vote_head + 1) % CNT_VOTES;
    if (s_gl.cnt_vote_n < CNT_VOTES) s_gl.cnt_vote_n++;
    int twos = 0;
    for (int i = 0; i < s_gl.cnt_vote_n; i++) twos += s_gl.cnt_vote[i];
    int est = (2 * twos > s_gl.cnt_vote_n) ? 2 : 1;
    if (est != s_gl.cnt_est) {
        if (est == 2) {                   /* votes cast so far may be two movers */
            s_gl.vote_n    = 0;
            s_gl.vote_head = 0;
        }
        (void)pv_on_count(&s_pv, est);
    }
    s_gl.cnt_est = est;
}

/* id_step(), recognition branch. The ring's first vote_n entries are the
 * votes (it fills from 0), which is all tally() needs. */
static void gl_id_step(bool moving, int count)
{
    float feat[GAIT_NFEAT];
    if (!gait_push(&s_gait, s_fe.grp, moving, feat)) return;
    s_gl.windows++;
    if (count >= 2) return;
    if (ncc_trained_classes(&s_id_model, ID_MIN_WIN) < 1) return;
    ncc_result_t r = ncc_classify(&s_id_model, feat, ID_MIN_WIN, ID_REJECT, ID_MARGIN);
    if (r.nearest < 0) return;

    s_gl.vote[s_gl.vote_head] = (r.best >= 0) ? r.best : PV_LABEL_UNKNOWN;
    s_gl.vote_head = (s_gl.vote_head + 1) % ID_VOTES;
    if (s_gl.vote_n < ID_VOTES) s_gl.vote_n++;

    /* A vote was just cast, so tally()'s -1 is main.c's ID_UNKNOWN, never
     * ID_NONE. Every DECISIVE tally goes to the latch, on every vote. */
    int k = 0;
    int label = tally(s_gl.vote, s_gl.vote_n, &k);
    if (label >= 0 || s_gl.vote_n >= ID_MIN_VOTES) {
        (void)pv_on_label(&s_pv, label, label >= 0 ? s_id_model.cls[label].name : NULL);
    }
}

/* One accepted packet's Step 10 glue, AFTER fe_packet() (which left Step
 * 08's decision for this packet in s_fe.present and gave the gate
 * room_empty = its negation), in main.c's PH_DETECT order:
 *   breathing hold -> pv_packet + carry drop -> presence transition ->
 *   the carry handed back (hold) or given up (MOVING) -> count_step,
 *   id_step (while PRESENT) -> act_push (post-hold presence) ->
 *   pv_analysis (analysis done).
 * s_fe.present is then set to main.c's post-hold `present`, which is what
 * the next packet's Step 08 hold reads. */
static void gl_packet(void)
{
    const int64_t t     = s_fe.t_us;
    const bool    still = s_fe.still;
    bool new_present    = s_fe.present;

    /* CSI_VITALS_HOLDS_PRESENCE */
    if (still && !s_gl.prev_still) s_gl.began_present = new_present;
    s_gl.prev_still = still;
    bool vit_done = vitals_push(&s_vit, t, s_fe.na, still);
    const vitals_out_t *vo = vitals_get(&s_vit);
    s_gl.breath_hold = false;
    if (still && s_gl.began_present && vo->br_valid) {
        s_gl.breath_hold     = true;
        s_fe.last_trigger_us = t;
        new_present          = true;
    }

    /* before the transition; the count carry is non-zero only during a
     * lapse (presence end until handed back or dropped) */
    pv_packet(&s_pv, t, still);
    if (s_gl.cnt_carry > 0 && (!still || t - s_gl.cnt_absent_us > CARRY_US)) {
        s_gl.cnt_carry = 0;
    }

    if (new_present != s_gl.present) {
        s_gl.present = new_present;
        if (new_present) {
            s_gl.starts++;
            s_gl.start_us = t;
            if (s_gl.breath_hold && !s_fe.motion_flag) s_gl.hold_starts++;   /* !trigger */
            s_gl.carry_wait = true;
        } else {
            s_gl.ends++;
            s_gl.end_us = t;
            pv_presence_end(&s_pv, t);
            int     carry = s_gl.carry_wait ? s_gl.cnt_carry     : s_gl.cnt_est;
            int64_t since = s_gl.carry_wait ? s_gl.cnt_absent_us : t;
            gl_reset(false);
            s_gl.cnt_carry     = carry;
            s_gl.cnt_absent_us = since;
        }
    }
    s_fe.present = s_gl.present;

    /* the end of the lapse for what it carried */
    if (s_gl.carry_wait) {
        if (s_gl.breath_hold) {
            s_gl.carry_wait = false;
            pv_presence_start(&s_pv);
            if (s_gl.cnt_carry > 0 && s_gl.cnt_est == 0) {
                s_gl.cnt_est = s_gl.cnt_carry;
                if (s_gl.cnt_est >= 2) (void)pv_on_count(&s_pv, s_gl.cnt_est);
            }
            s_gl.cnt_carry = 0;
        } else if (!still) {
            s_gl.carry_wait = false;
        }
    }

    if (s_gl.present) {
        bool moving = s_fe.motion_flag && !still;
        gl_count_step(moving);
        gl_id_step(moving, gl_count());
    }
    act_push(&s_act, t, s_gl.present, still, mgate_ratio(&s_fe.gate), s_gl.breath_hold);
    if (vit_done && pv_analysis(&s_pv, t, gl_who_present(), gl_count(), vo)) s_gl.credits++;
}

/* report_build()'s vitals rule, per packet (the JSON line samples it
 * every 500 ms; checking every packet is stricter). */
typedef struct {
    long present_n, absent_n;      /* packets                                       */
    long wait_n;                   /* present, the lapse's carry still waiting      */
    long named_n;                  /* present, vitals_of == want                    */
    long other_n;                  /* present, vitals_of set and != want            */
    long held_n;                   /* present, and the breathing hold asserted it   */
    long moving_n;                 /* present, moving = flag && !still              */
    long flag_n, gate_n;           /* present, flag / gate MOVING                   */
    long armed_n;                  /* armed and present                             */
    long leak_n;                   /* ... and NOT "multiple"                        */
    long of_n;                     /* ... and vitals_of != NULL                     */
    const char *want;              /* NULL: no name expected                        */
    bool armed;                    /* e2: count has reached 2 this visit            */
    bool arm_on_count;             /* arm when cnt_est first reads 2                */
} obs_t;

static void obs_packet(obs_t *o)
{
    const vitals_out_t *v = vitals_get(&s_vit);
    int  count = gl_count();
    bool multi = count >= 2 || pv_multiple(&s_pv);
    if (o->arm_on_count && s_gl.present && s_gl.cnt_est >= 2) o->armed = true;
    if (!s_gl.present) {
        o->absent_n++;
        return;
    }
    o->present_n++;
    if (s_gl.carry_wait) o->wait_n++;
    if (s_gl.breath_hold) o->held_n++;
    if (s_fe.motion_flag && !s_fe.still) o->moving_n++;
    if (s_fe.motion_flag) o->flag_n++;
    if (!s_fe.still) o->gate_n++;
    const char *of = NULL;
    if (v->state != VIT_OFF && !multi) of = pv_vitals_of(&s_pv, gl_who_present(), count);
    if (o->want && of && strcmp(of, o->want) == 0) o->named_n++;
    if (of && !(o->want && strcmp(of, o->want) == 0)) o->other_n++;
    if (o->armed) {
        o->armed_n++;
        if (!multi) o->leak_n++;
        if (of) o->of_n++;
    }
}

/* The scene for `seconds` through the front end and the glue. */
static void gl_run(const scene_t *sc, float seconds, obs_t *o)
{
    int n = (int)(seconds * s_fe.rate_nom);
    for (int i = 0; i < n; i++) {
        if (!slot(sc)) continue;
        gl_packet();
        if (o) obs_packet(o);
    }
}

/* Every scenario starts as after analytics_reset(true), then 10 s of empty
 * room: the gate re-learns STILL, the room reads empty. */
static void gl_begin(void)
{
    scene_t sc;
    memset(&sc, 0, sizeof(sc));
    memset(&s_gl, 0, sizeof(s_gl));
    s_gl.present = s_fe.present;
    gl_reset(true);
    gl_run(&sc, 10.0f, NULL);
}

static void print_rec(int s)
{
    char line[200];
    pv_format_rec(line, sizeof(line), &s_pv, s, s_fe.t_us);
    printf("        slot %d (%s): %s\n", s, s_pv.log.rec[s].name[0] ? s_pv.log.rec[s].name : "-", line);
}

/* e1: A (enrolled) walks in alone long enough to be recognised, sits and
 * breathes; Step 08 loses the still person, breathing brings PRESENT back.
 * The name must survive the lapse (the whole point of the latch), and the
 * still spell must land in A's log once it ends. */
static void e1_walk_sit(void)
{
    const float br = 15.0f, hr = 66.0f;
    scene_t   sc;
    session_t s;
    obs_t     o;
    int       k = 0, slot_l = -1;

    gl_begin();
    CHECK(!s_gl.present && pv_attr(&s_pv, NULL) == PV_NONE,
          "e1: before the walk-in: present %d, latch %d", (int)s_gl.present, (int)pv_attr(&s_pv, NULL));

    /* Walk in alone "long enough to be recognised": 30 s, then on until the
     * latch names somebody, 120 s at most. How many gait windows a walk
     * yields on the device path is (a)'s subject, and at 50 Hz it varies a
     * lot from room to room with these synthetic walkers: a window needs
     * 80% of its packets moving = flag && !still, and the gate's hf for
     * walker A sits near its threshold (A's 1.6 Hz steps are at the gate's
     * corner, see (a)). Over rooms 5000..5008 (/DE_SEED, measured on
     * 2026-09-25) the gate called 26-92% of A's walking packets MOVING; in
     * two of the nine (5004, 5007: 31% and 26%) A was never recognised in
     * 120 s and (e) fails right here - a gait-window yield problem, not a
     * latch one; the other seven pass. The walk
     * is not what (e) is about, so beyond "A was recognised" it is printed,
     * not asserted; without a name nothing below it means anything, so e1
     * stops there. */
    memset(&sc, 0, sizeof(sc));
    session_start(&s, &k_walkers[0], s_fe.rate_nom);
    sc.walker[0] = &s;
    sc.nwalker   = 1;
    memset(&o, 0, sizeof(o));
    int   win0   = s_gl.windows;
    float walked = 0.0f;
    do {
        gl_run(&sc, 1.0f, &o);
        walked += 1.0f;
    } while (walked < 30.0f || (pv_attr(&s_pv, NULL) != PV_PERSON && walked < 120.0f));
    pv_attr_t a = pv_attr(&s_pv, &slot_l);
    int label = s_gl.vote_n ? tally(s_gl.vote, s_gl.vote_n, &k) : -2;
    printf("    e1: A walks in alone %.0f s: moving (flag && !still) on %.1f%% of present packets "
           "(flag %.1f%%, gate MOVING %.1f%%),\n"
           "        %d device gait windows; tally %d (%d/%d votes), count %d, latch %d slot %d\n",
           (double)walked, 100.0 * (double)o.moving_n / (double)(o.present_n ? o.present_n : 1),
           100.0 * (double)o.flag_n / (double)(o.present_n ? o.present_n : 1),
           100.0 * (double)o.gate_n / (double)(o.present_n ? o.present_n : 1),
           s_gl.windows - win0, label, k, s_gl.vote_n, gl_count(), (int)a, slot_l);
    CHECK(s_gl.present && a == PV_PERSON && slot_l == 0,
          "e1: A not recognised in %.0f s of walking (present %d, latch %d, slot %d) - see (a) on "
          "device gait windows; the rest of e1 needs a name and is skipped", (double)walked,
          (int)s_gl.present, (int)a, slot_l);
    if (!(s_gl.present && a == PV_PERSON && slot_l == 0)) return;
    CHECK(s_gl.cnt_est == 1, "e1: count after A walked alone %d, want 1 (trained)", s_gl.cnt_est);

    /* sits, breathing 15/min: 90 s, well past the ~20 s the first valid
     * analysis needs and the 5 s Step 08 holds a still person for */
    chest_make(&s_chest, 0.6f, 0.20f, br, hr);
    sc.nwalker     = 0;
    sc.chest       = &s_chest;
    sc.chest_t0_us = s_fe.t_us;
    memset(&o, 0, sizeof(o));
    o.want = "A";
    int ends0 = s_gl.ends, starts0 = s_gl.starts, holds0 = s_gl.hold_starts, credits0 = s_gl.credits;
    gl_run(&sc, 90.0f, &o);
    const char *of_end = pv_vitals_of(&s_pv, gl_who_present(), gl_count());
    printf("    e1: sits 90 s (breathing %.0f/min, heart %.0f): presence ended %d x, came back %d x "
           "(%d by breathing alone), absent %.1f s;\n"
           "        breathing held PRESENT on %ld of %ld present packets (%ld waiting for the hold), "
           "named A on %ld; at the end present %d, count %d, person votes %d, vitals_of %s, pending %u "
           "samples\n",
           (double)br, (double)hr, s_gl.ends - ends0, s_gl.starts - starts0, s_gl.hold_starts - holds0,
           (double)o.absent_n / (double)s_fe.rate, o.held_n, o.present_n, o.wait_n, o.named_n,
           (int)s_gl.present, gl_count(), s_gl.vote_n, of_end ? of_end : "null", (unsigned)s_pv.acc_br_n);
    /* The lapse must happen and be undone, or the carry is not exercised.
     * WHICH detector brings presence back first is the synthetic room's
     * business and is printed, not asserted: usually breathing (~20 s of
     * still data), but a Step 08 flag blip (~1% of sitting packets at this
     * size, see (c)) can come first - edge-case row "Step 08 re-triggers
     * while the gate stays still", latch kept either way. Breathing must
     * then be what holds PRESENT: 90 s later Step 08 alone would have let
     * go many times over. A start by Step 08 alone waits for that hold
     * before the name comes back (s_carry_wait) - vitals_of is null on
     * those packets by design, and A on every other present one. */
    CHECK(s_gl.ends - ends0 >= 1 && s_gl.starts - starts0 >= 1,
          "e1: the lapse / restore was not exercised (ended %d, came back %d)",
          s_gl.ends - ends0, s_gl.starts - starts0);
    CHECK(o.held_n > 0 && s_gl.present, "e1: breathing held PRESENT on %ld packets, present at the end %d",
          o.held_n, (int)s_gl.present);
    CHECK(of_end != NULL && strcmp(of_end, "A") == 0, "e1: vitals_of at the end %s, want A",
          of_end ? of_end : "null");
    CHECK(o.present_n - o.wait_n > 0 && o.named_n == o.present_n - o.wait_n,
          "e1: vitals_of was A on %ld of %ld present packets while A sat (%ld waiting for the hold)",
          o.named_n, o.present_n - o.wait_n, o.wait_n);
    /* edge-case table, row 1: person null after the restore (the vote ring
     * was cleared and a seated person casts no votes - by design), count
     * restored from the carry */
    CHECK(s_gl.vote_n == 0, "e1: %d recognition votes while seated", s_gl.vote_n);
    CHECK(s_gl.cnt_est == 1, "e1: count after the restore %d, want 1 carried over", s_gl.cnt_est);
    CHECK(s_pv.log.rec[0].br_eps == 0 && s_pv.acc_slot == 0 && s_pv.acc_br_n >= PV_EP_MIN,
          "e1: before A moves: %u committed episodes, pending slot %d with %u samples",
          (unsigned)s_pv.log.rec[0].br_eps, (int)s_pv.acc_slot, (unsigned)s_pv.acc_br_n);

    /* Gets up and walks about: the first packet the gate calls MOVING ends
     * the still spell, and its samples become one episode in A's log. At
     * least 5 s, and on until the gate has said MOVING (30 s at most): in
     * rooms where this synthetic walker's hf sits under the gate's
     * threshold (see the walk-in above) the gate can take several seconds
     * to notice - vitals keep analysing, and crediting, meanwhile. */
    int credits_sat = s_gl.credits;
    session_start(&s, &k_walkers[0], s_fe.rate_nom);
    sc.chest     = NULL;
    sc.walker[0] = &s;
    sc.nwalker   = 1;
    memset(&o, 0, sizeof(o));
    float up_s = 0.0f;
    do {
        gl_run(&sc, 1.0f, &o);
        up_s += 1.0f;
    } while (up_s < 5.0f || (o.gate_n == 0 && up_s < 30.0f));
    const pv_rec_t *r = &s_pv.log.rec[0];
    printf("    e1: A gets up and walks %.0f s (gate MOVING on %ld packets); %d analyses credited while "
           "A sat, %d after; the log:\n", (double)up_s, o.gate_n, credits_sat - credits0,
           s_gl.credits - credits_sat);
    for (int i = 0; i < PV_SLOTS; i++) print_rec(i);
    CHECK(strcmp(r->name, "A") == 0 && r->br_eps == 1 && r->samples >= (uint32_t)PV_EP_MIN,
          "e1: A's record: name '%s', %u episodes, %u samples", r->name, (unsigned)r->br_eps,
          (unsigned)r->samples);
    CHECK(r->br_eps == 1 && fabsf(r->br_last - br) <= BR_TOL && fabsf(r->br_mean - br) <= BR_TOL,
          "e1: A's logged breathing %.2f (mean %.2f), want %.0f +/- %.1f", (double)r->br_last,
          (double)r->br_mean, (double)br, (double)BR_TOL);
    CHECK(r->hr_eps <= r->br_eps, "e1: %u heart episodes > %u breathing", (unsigned)r->hr_eps,
          (unsigned)r->br_eps);
    CHECK(s_pv.log.rec[1].br_eps == 0 && s_pv.log.rec[2].br_eps == 0 && s_pv.log.rec[3].br_eps == 0,
          "e1: another slot was credited");
    CHECK(s_pv.acc_slot < 0, "e1: an episode still pending after A moved (slot %d)", (int)s_pv.acc_slot);
    CHECK(pv_log_valid(&s_pv.log, sizeof(s_pv.log)), "e1: the log would be rejected at the next boot");
}

/* e2: A and B walk in together (the trained counter says two), then both
 * sit and breathe at different rates. From the moment count reads 2, no
 * present packet may report a rate or a name - including after the lapse
 * the breathing hold undoes (the count carry and the sticky latch) - and
 * nobody's log may move. */
static void e2_two_sit(void)
{
    scene_t   sc;
    session_t s1, s2;
    obs_t     o;
    pv_log_t  before = s_pv.log;         /* e1's log: e2 must not add to it */
    int       credits0;

    gl_begin();
    credits0 = s_gl.credits;
    memset(&sc, 0, sizeof(sc));
    memset(&o, 0, sizeof(o));
    o.arm_on_count = true;

    /* walk in together, 30 s: ~5 count blocks */
    session_start(&s1, &k_walkers[0], s_fe.rate_nom);
    session_start(&s2, &k_walkers[1], s_fe.rate_nom);
    sc.walker[0] = &s1;
    sc.walker[1] = &s2;
    sc.nwalker   = 2;
    gl_run(&sc, 30.0f, &o);
    printf("    e2: A + B walk in together 30 s: count %d, latch %d, %s\n", gl_count(),
           (int)pv_attr(&s_pv, NULL), o.armed ? "count reached 2" : "count never reached 2");
    CHECK(o.armed && pv_multiple(&s_pv), "e2: two movers: count %d, latch %d", gl_count(),
          (int)pv_attr(&s_pv, NULL));

    /* both sit, 90 s: 15 and 18 breaths/min, similar sizes - the mix
     * vitals may well report one confident rate for */
    chest_make(&s_chest, 0.6f, 0.20f, 15.0f, 66.0f);
    chest_make(&s_chest2, 0.6f, 0.20f, 18.0f, 75.0f);
    sc.nwalker     = 0;
    sc.chest       = &s_chest;
    sc.chest2      = &s_chest2;
    sc.chest_t0_us = s_fe.t_us;
    int     ends0 = s_gl.ends, starts0 = s_gl.starts, holds0 = s_gl.hold_starts;
    long    pres0 = o.present_n, held0 = o.held_n;
    int64_t t0 = s_fe.t_us;
    gl_run(&sc, 90.0f, &o);
    const vitals_out_t *v = vitals_get(&s_vit);
    printf("    e2: both sit 90 s (15 and 18/min): presence ended %d x (last at %.1f s), came back "
           "%d x (last at %.1f s, %d by breathing alone);\n"
           "        breathing held PRESENT on %ld of %ld present packets; vitals %s br %.2f%s; "
           "at the end present %d, count %d, latch %d\n",
           s_gl.ends - ends0, 1e-6 * (double)(s_gl.end_us - t0), s_gl.starts - starts0,
           1e-6 * (double)(s_gl.start_us - t0), s_gl.hold_starts - holds0, o.held_n - held0,
           o.present_n - pres0, vitals_state_name(v->state), (double)v->br_bpm,
           v->br_valid ? "*" : "", (int)s_gl.present, gl_count(), (int)pv_attr(&s_pv, NULL));
    /* Not vacuous. The case that matters is the mix passing for ONE
     * breathing rate, so that the hold keeps PRESENT on a signal that is
     * two people; and presence must have lapsed and come back, or the
     * count carry and the latch's absence rules were never exercised.
     * WHICH detector brings presence back is the synthetic room's business
     * and not asserted: two chests lift Step 08's M more than one, and a
     * flag blip can beat the first valid analysis to it (edge-case row
     * "Step 08 re-triggers while the gate stays still" - the latch and the
     * carry are kept either way). */
    CHECK(s_gl.ends - ends0 >= 1 && s_gl.starts - starts0 >= 1,
          "e2: the lapse / restore was not exercised (ended %d, came back %d)",
          s_gl.ends - ends0, s_gl.starts - starts0);
    CHECK(o.held_n - held0 > 0, "e2: breathing never held PRESENT while they sat - the mix was not "
          "the dangerous case");
    CHECK(s_gl.present && s_gl.cnt_est == 2,
          "e2: after the restore: present %d, count %d - want 2 carried over the lapse",
          (int)s_gl.present, s_gl.cnt_est);

    /* Both get up and leave (5 s). "multiple" is sticky until the room has
     * been empty: here, absent with the gate still for longer than the
     * carry (60 s). The room runs until it has read empty that long (300 s
     * at most) - not for a fixed time, because Step 08 can re-trigger in a
     * synthetic empty room (twice in ~65 s in room 5001 on 2026-09-25), and each
     * re-trigger rightly restarts the carry. */
    session_start(&s1, &k_walkers[0], s_fe.rate_nom);
    session_start(&s2, &k_walkers[1], s_fe.rate_nom);
    sc.chest   = NULL;
    sc.chest2  = NULL;
    sc.nwalker = 2;
    gl_run(&sc, 5.0f, &o);
    sc.nwalker = 0;
    int   ends_l  = s_gl.ends;
    float empty_s = 0.0f;
    do {
        gl_run(&sc, 1.0f, &o);
        empty_s += 1.0f;
    } while ((s_gl.present || s_gl.ends == ends_l || s_fe.t_us - s_gl.end_us <= CARRY_US + 1000000) &&
             empty_s < 300.0f);
    bool quiet = !s_gl.present && s_gl.ends > ends_l && s_fe.t_us - s_gl.end_us > CARRY_US;

    printf("    e2: both leave (5 s); room empty %.0f s (%d presence ends in it). From count 2 on: %ld present\n"
           "        packets, %ld NOT \"multiple\", %ld with a vitals_of; %d analyses credited; "
           "latch after the empty room %d (0 = released)\n",
           (double)empty_s, s_gl.ends - ends_l, o.armed_n, o.leak_n, o.of_n, s_gl.credits - credits0,
           (int)pv_attr(&s_pv, NULL));
    CHECK(o.armed_n > 0, "e2: no present packet after count reached 2");
    CHECK(o.leak_n == 0, "e2: %ld present packets after count 2 were not \"multiple\"", o.leak_n);
    CHECK(o.of_n == 0, "e2: %ld present packets after count 2 named somebody", o.of_n);
    CHECK(s_gl.credits == credits0, "e2: %d analyses credited to somebody", s_gl.credits - credits0);
    CHECK(memcmp(&before, &s_pv.log, sizeof(before)) == 0, "e2: the vitals log changed");
    CHECK(quiet, "e2: the room never read empty for 60 s in %.0f s (present %d)", (double)empty_s,
          (int)s_gl.present);
    CHECK(pv_attr(&s_pv, NULL) == PV_NONE, "e2: latch %d after the room read empty > 60 s, want released",
          (int)pv_attr(&s_pv, NULL));
}

/* e3: A (enrolled) walks in alone and is recognised, then walks OUT. The
 * lapse carries A's name (the breathing hold could still undo it). 15 s
 * later B walks in - 6 s, too short to be recognised - and sits breathing
 * 18/min. B is a new visit: Step 08 reports B first (its flag has no
 * hold) and the gate only later, so the lapse must not end at Step 08's
 * start; the gate's MOVING during B's walk must let A's name and count go.
 * Before s_carry_wait, A's name came back at Step 08's start and B's whole
 * sitting was reported, and logged, as A's. Measured on 2026-09-25 (rooms
 * 5000-5008 at this 15 s gap, plus 5000-5003 at a 30 s gap): Step 08 fired
 * 41-740 ms after B started, the gate 0.4-3.5 s after Step 08 - or, once
 * (5001, 30 s gap), not within the 6 s. So a fixed wait after Step 08's
 * start would be a guess; main.c waits for the breathing hold instead. */
static void e3_walkin_after_lapse(void)
{
    const float gap_s = 15.0f, b_walk_s = 6.0f;
    scene_t   sc;
    session_t s;
    obs_t     o;
    int       slot_l = -1;

    gl_begin();
    memset(&sc, 0, sizeof(sc));
    session_start(&s, &k_walkers[0], s_fe.rate_nom);
    sc.walker[0] = &s;
    sc.nwalker   = 1;
    float walked = 0.0f;
    do {                                   /* as e1: 30 s, then until named, 120 s at most */
        gl_run(&sc, 1.0f, NULL);
        walked += 1.0f;
    } while (walked < 30.0f || (pv_attr(&s_pv, NULL) != PV_PERSON && walked < 120.0f));
    pv_attr_t a = pv_attr(&s_pv, &slot_l);
    CHECK(s_gl.present && a == PV_PERSON && slot_l == 0,
          "e3: A not recognised in %.0f s of walking (present %d, latch %d, slot %d) - see e1; "
          "the rest of e3 needs a name and is skipped", (double)walked, (int)s_gl.present, (int)a, slot_l);
    if (!(s_gl.present && a == PV_PERSON && slot_l == 0)) return;

    /* A walks out: the room goes quiet, Step 08's hold runs out, the lapse
     * carries A (the gate went still while Step 08 still said PRESENT, so
     * this empty-room spell counts as one that began present) */
    sc.nwalker = 0;
    int   ends0  = s_gl.ends;
    float left_s = 0.0f;
    while ((s_gl.present || s_gl.ends == ends0) && left_s < 60.0f) {
        gl_run(&sc, 0.1f, NULL);
        left_s += 0.1f;
    }
    gl_run(&sc, gap_s, NULL);
    printf("    e3: A walked %.0f s and was recognised; walked out, room clear after %.1f s, then %.0f s "
           "empty: present %d, latch %d slot %d, carried count %d\n", (double)walked, (double)left_s,
           (double)gap_s, (int)s_gl.present, (int)pv_attr(&s_pv, &slot_l), slot_l, s_gl.cnt_carry);
    CHECK(!s_gl.present && s_pv.absent && pv_attr(&s_pv, NULL) == PV_PERSON && s_gl.cnt_carry == 1,
          "e3: before B: present %d, pv absent %d, latch %d, carried count %d - want A's name and count "
          "carried by the lapse, or e3 tests nothing", (int)s_gl.present, (int)s_pv.absent,
          (int)pv_attr(&s_pv, NULL), s_gl.cnt_carry);

    pv_log_t before   = s_pv.log;
    int      credits0 = s_gl.credits;

    /* B walks in, 6 s, per packet: when Step 08 and the gate first fire,
     * and when the latch let go of A */
    session_start(&s, &k_walkers[1], s_fe.rate_nom);
    sc.walker[0] = &s;
    sc.nwalker   = 1;
    int64_t t0 = s_fe.t_us, t_pres = 0, t_gate = 0, t_rel = 0;
    int     n  = (int)(b_walk_s * s_fe.rate_nom);
    for (int i = 0; i < n; i++) {
        if (!slot(&sc)) continue;
        gl_packet();
        if (!t_pres && s_gl.present) t_pres = s_fe.t_us;
        if (!t_gate && !s_fe.still) t_gate = s_fe.t_us;
        if (!t_rel && pv_attr(&s_pv, NULL) != PV_PERSON) t_rel = s_fe.t_us;
    }
    printf("    e3: B walks in %.0f s: Step 08 PRESENT after %.0f ms, gate MOVING after %.0f ms, A let go "
           "after %.0f ms (-1 = never); latch %d, carried count %d\n", (double)b_walk_s,
           t_pres ? 1e-3 * (double)(t_pres - t0) : -1.0, t_gate ? 1e-3 * (double)(t_gate - t0) : -1.0,
           t_rel ? 1e-3 * (double)(t_rel - t0) : -1.0, (int)pv_attr(&s_pv, NULL), s_gl.cnt_carry);
    /* What the fix can promise: a walk-in the gate SEES lets the carry go,
     * however much later than Step 08 it sees it. One the gate never calls
     * MOVING is indistinguishable from "Step 08 re-triggers while the gate
     * stays still" (latch kept) - the documented residual, bounded only by
     * the carry. That is the gate's yield on this synthetic walker in this
     * room, as e1's recognition precondition is, and e3 stops there. */
    CHECK(t_pres != 0 && t_gate != 0,
          "e3: Step 08 (%d) or the gate (%d) never saw B walk in - the gate-yield residual; the rest "
          "of e3 is skipped", t_pres != 0, t_gate != 0);
    if (!(t_pres != 0 && t_gate != 0)) return;
    CHECK(t_rel != 0 && t_rel == t_gate, "e3: A's name let go %.0f ms after B started, want at the "
          "gate's first MOVING (%.0f ms)", t_rel ? 1e-3 * (double)(t_rel - t0) : -1.0,
          1e-3 * (double)(t_gate - t0));
    CHECK(s_gl.cnt_carry == 0, "e3: A's count still carried (%d) after the gate saw B move", s_gl.cnt_carry);

    /* B sits, breathing 18/min - the dangerous case is breathing holding
     * PRESENT for B while the latch still says A */
    chest_make(&s_chest, 0.6f, 0.20f, 18.0f, 75.0f);
    sc.nwalker     = 0;
    sc.chest       = &s_chest;
    sc.chest_t0_us = s_fe.t_us;
    memset(&o, 0, sizeof(o));
    o.want = "A";
    gl_run(&sc, 90.0f, &o);
    printf("    e3: B sits 90 s (18/min): breathing held PRESENT on %ld of %ld present packets; vitals_of "
           "A on %ld, another name on %ld; %d analyses credited; latch %d\n", o.held_n, o.present_n,
           o.named_n, o.other_n, s_gl.credits - credits0, (int)pv_attr(&s_pv, NULL));
    CHECK(o.held_n > 0, "e3: breathing never held PRESENT for B - not the dangerous case");
    CHECK(o.named_n == 0, "e3: vitals_of was A on %ld of B's present packets", o.named_n);
    CHECK(s_gl.credits == credits0, "e3: %d of B's analyses credited to somebody", s_gl.credits - credits0);

    /* B gets up: whatever was pending would commit now */
    session_start(&s, &k_walkers[1], s_fe.rate_nom);
    sc.chest     = NULL;
    sc.walker[0] = &s;
    sc.nwalker   = 1;
    gl_run(&sc, 5.0f, NULL);
    CHECK(memcmp(&before, &s_pv.log, sizeof(before)) == 0,
          "e3: the vitals log changed (slot 0 '%s': %u episodes, last %.2f/min)", s_pv.log.rec[0].name,
          (unsigned)s_pv.log.rec[0].br_eps, (double)s_pv.log.rec[0].br_last);
    CHECK(s_pv.acc_slot < 0, "e3: an episode pending for slot %d", (int)s_pv.acc_slot);
}

/* e4: the activity state (activity.h) on packets that came through the
 * front end, the gate, vitals and the breathing hold - the real presence
 * path, so "still" here is PRESENT held by breathing, not a stub. One
 * visit: A walks in, sits breathing (the lapse and restore of e1), fidgets
 * (a weak walker for 1 s in every 4 while seated), walks, and leaves.
 * Per segment, the share of packets whose state matches the segment's
 * ground truth is PRINTED, not asserted: the walking segments depend on
 * the gate's yield for this walker, which in rooms 5004 / 5007 is
 * 26-31% of packets (e1), right at activity's 1/4 duty; and the fidget's
 * hf is whatever this synthetic room makes of it. Asserted only what
 * follows from the rules whatever the room: empty after gl_begin's empty
 * room, still at the end of the seated spell that breathing holds, one
 * episode across that spell's lapse, and empty with the episode ended
 * after the room has been left for longer than the carry. */
#ifndef E4_FGAIN                   /* overridable (-DE4_FGAIN=1.0f -DE4_FGAIN_S="1.0") */
#define E4_FGAIN   0.6f            /* e4's fidget: walker A at this strength, 1 s in 4 */
#define E4_FGAIN_S "0.6"
#endif

typedef struct {
    long n, match, n_late, match_late;  /* packets; ... from 10 s into the segment */
    long st[ACT_NSTATES];
} actobs_t;

static void act_run(const scene_t *sc, float seconds, float t_in, act_state_t truth, actobs_t *ao,
                    obs_t *o)
{
    int n    = (int)(seconds * s_fe.rate_nom);
    int late = (int)((10.0f - t_in) * s_fe.rate_nom);
    for (int i = 0; i < n; i++) {
        if (!slot(sc)) continue;
        gl_packet();
        if (o) obs_packet(o);
        act_state_t a = act_state(&s_act);
        ao->n++;
        ao->st[a]++;
        if (a == truth) ao->match++;
        if (i >= late) {
            ao->n_late++;
            if (a == truth) ao->match_late++;
        }
    }
}

static void print_actobs(const char *what, act_state_t truth, const actobs_t *ao)
{
    printf("        %-30s %-8s %5ld  %5.1f%%  %5.1f%%   %5.1f %5.1f %5.1f %5.1f %5.1f\n", what,
           act_state_name(truth), ao->n, 100.0 * (double)ao->match / (double)(ao->n ? ao->n : 1),
           100.0 * (double)ao->match_late / (double)(ao->n_late ? ao->n_late : 1),
           100.0 * (double)ao->st[ACT_EMPTY] / (double)(ao->n ? ao->n : 1),
           100.0 * (double)ao->st[ACT_STILL] / (double)(ao->n ? ao->n : 1),
           100.0 * (double)ao->st[ACT_RESTLESS] / (double)(ao->n ? ao->n : 1),
           100.0 * (double)ao->st[ACT_MOVING] / (double)(ao->n ? ao->n : 1),
           100.0 * (double)ao->st[ACT_NONE] / (double)(ao->n ? ao->n : 1));
}

/* Empty room, 1 s at a time, until nobody is PRESENT and no episode is
 * open or pending (the 60 s carry), 240 s at most. Returns the seconds. */
static float e4_until_ended(scene_t *sc)
{
    float waited = 0.0f;
    sc->nwalker = 0;
    sc->chest   = NULL;
    sc->chest2  = NULL;
    while ((s_gl.present || s_act.ep_phase != ACT_EP_NONE) && waited < 240.0f) {
        gl_run(sc, 1.0f, NULL);
        waited += 1.0f;
    }
    return waited;
}

static void e4_activity(void)
{
    scene_t   sc;
    session_t s;
    obs_t     o;
    actobs_t  ao_walk, ao_sit, ao_fid, ao_walk2, ao_out;
    uint32_t  ep = 0;

    /* gl_begin's 10 s start as e3 ended - B walking, so PRESENT - and
     * the lapse after it holds an episode pending for the 60 s carry:
     * empty room until that has ended, then 10 s more. */
    gl_begin();
    memset(&sc, 0, sizeof(sc));
    float pre_s = e4_until_ended(&sc);
    gl_run(&sc, 10.0f, NULL);
    CHECK(act_state(&s_act) == ACT_EMPTY && !act_episode_s(&s_act, &ep) && !s_gl.present,
          "e4: %.0f + 10 s of empty room: present %d, activity %s, episode %d - want empty, none",
          (double)pre_s, (int)s_gl.present,
          act_state_name(act_state(&s_act)) ? act_state_name(act_state(&s_act)) : "null",
          (int)act_episode_s(&s_act, &ep));
    uint32_t n0 = s_act.n_episodes;

    memset(&ao_walk, 0, sizeof(ao_walk));
    memset(&ao_sit, 0, sizeof(ao_sit));
    memset(&ao_fid, 0, sizeof(ao_fid));
    memset(&ao_walk2, 0, sizeof(ao_walk2));
    memset(&ao_out, 0, sizeof(ao_out));

    /* walks in, 30 s */
    session_start(&s, &k_walkers[0], s_fe.rate_nom);
    sc.walker[0] = &s;
    sc.nwalker   = 1;
    act_run(&sc, 30.0f, 0.0f, ACT_MOVING, &ao_walk, NULL);
    bool walked_in = s_gl.present;

    /* sits, breathing 15/min, 90 s: e1's chest */
    chest_make(&s_chest, 0.6f, 0.20f, 15.0f, 66.0f);
    sc.nwalker     = 0;
    sc.chest       = &s_chest;
    sc.chest_t0_us = s_fe.t_us;
    memset(&o, 0, sizeof(o));
    int ends0 = s_gl.ends, starts0 = s_gl.starts;
    act_run(&sc, 90.0f, 0.0f, ACT_STILL, &ao_sit, &o);
    act_state_t sit_end = act_state(&s_act);
    bool        sit_ep  = act_episode_s(&s_act, &ep);
    uint32_t    sit_s   = ep;
    printf("    e4: walked in (present %d); sat 90 s: presence ended %d x, came back %d x, breathing "
           "held it on %ld of %ld present packets; at the end present %d, activity %s, episode %u s "
           "(%u ended since the walk-in)\n", (int)walked_in, s_gl.ends - ends0, s_gl.starts - starts0,
           o.held_n, o.present_n, (int)s_gl.present,
           act_state_name(sit_end) ? act_state_name(sit_end) : "null", (unsigned)sit_s,
           (unsigned)(s_act.n_episodes - n0));
    CHECK(s_gl.ends - ends0 >= 1 && s_gl.starts - starts0 >= 1 && o.held_n > 0 && s_gl.present,
          "e4: the breathing-hold lapse was not exercised (ended %d, came back %d, held %ld, present %d)",
          s_gl.ends - ends0, s_gl.starts - starts0, o.held_n, (int)s_gl.present);
    CHECK(sit_end == ACT_STILL, "e4: activity %s at the end of 90 s seated, held PRESENT by breathing - "
          "want still", act_state_name(sit_end) ? act_state_name(sit_end) : "null");
    CHECK(walked_in && s_act.n_episodes == n0 && s_act.ep_phase == ACT_EP_OPEN && sit_ep && sit_s >= 110,
          "e4: the lapse split the episode: %u ended, phase %d, episode %u s (want 0 ended, open, "
          ">= 110 s of the 120 s walk + sit)", (unsigned)(s_act.n_episodes - n0), (int)s_act.ep_phase,
          (unsigned)sit_s);

    /* fidgets while seated, 40 s: a weak walker for 1 s in every 4 */
    session_start(&s, &k_walkers[0], s_fe.rate_nom);
    sc.wgain = E4_FGAIN;
    double fid_hf = 0.0;
    long   fid_n  = 0;
    for (int c = 0; c < 10; c++) {
        sc.nwalker = 1;
        act_run(&sc, 1.0f, 4.0f * (float)c, ACT_RESTLESS, &ao_fid, NULL);
        /* the gate's ratio at the end of each burst second, for the print */
        float hf = mgate_ratio(&s_fe.gate);
        if (hf >= 0.0f) {
            fid_hf += (double)hf;
            fid_n++;
        }
        sc.nwalker = 0;
        act_run(&sc, 3.0f, 4.0f * (float)c + 1.0f, ACT_RESTLESS, &ao_fid, NULL);
    }
    uint32_t fid_eps = s_act.n_episodes - n0;

    /* gets up and walks, 20 s */
    session_start(&s, &k_walkers[0], s_fe.rate_nom);
    sc.wgain   = 0.0f;
    sc.chest   = NULL;
    sc.nwalker = 1;
    act_run(&sc, 20.0f, 0.0f, ACT_MOVING, &ao_walk2, NULL);

    /* leaves: 75 s of empty room, longer than the 60 s carry - and then
     * on until PRESENT has gone and the carry has run out, if that took
     * longer here (a false re-trigger or hold in this synthetic room) */
    sc.nwalker = 0;
    act_run(&sc, 75.0f, 0.0f, ACT_EMPTY, &ao_out, NULL);
    bool  out_present = s_gl.present;
    float out_more_s  = e4_until_ended(&sc);

    printf("    e4: activity per segment, %% of packets (agree = the segment's truth; late = from 10 s "
           "into it):\n"
           "        segment                        truth    pkts   agree    late    empty still restl "
           "movin  null\n");
    print_actobs("walks in (A, 30 s)", ACT_MOVING, &ao_walk);
    print_actobs("sits breathing (90 s)", ACT_STILL, &ao_sit);
    print_actobs("fidgets 1 s in 4 (walker x" E4_FGAIN_S ")", ACT_RESTLESS, &ao_fid);
    print_actobs("walks (20 s)", ACT_MOVING, &ao_walk2);
    print_actobs("left (75 s)", ACT_EMPTY, &ao_out);
    printf("        fidget: hf %.2f at the end of a burst second (mean of %ld; restless counts a second "
           "from a MEAN of %.1f, moving from %.1f)\n", fid_n ? fid_hf / (double)fid_n : -1.0, fid_n,
           (double)ACT_LO, (double)GATE_RATIO);
    printf("        after the 75 s: present %d; %.0f s more until nobody PRESENT and the episode ended\n",
           (int)out_present, (double)out_more_s);
    printf("        episodes since the walk-in: %u ended (%u by the end of the fidgeting); the last "
           "%u s: still %u, restless %u, moving %u, other %u\n", (unsigned)(s_act.n_episodes - n0),
           (unsigned)fid_eps, (unsigned)s_act.last.total_s, (unsigned)s_act.last.st_s[ACT_STILL],
           (unsigned)s_act.last.st_s[ACT_RESTLESS], (unsigned)s_act.last.st_s[ACT_MOVING],
           (unsigned)(s_act.last.st_s[ACT_NONE] + s_act.last.st_s[ACT_EMPTY]));
    CHECK(!s_gl.present && act_state(&s_act) == ACT_EMPTY && s_act.ep_phase == ACT_EP_NONE &&
          s_act.n_episodes > n0, "e4: %.0f s after leaving: present %d, activity %s, episode phase %d, "
          "%u ended - want empty and the episode ended", 75.0 + (double)out_more_s, (int)s_gl.present,
          act_state_name(act_state(&s_act)) ? act_state_name(act_state(&s_act)) : "null",
          (int)s_act.ep_phase, (unsigned)(s_act.n_episodes - n0));
}

static void test_whose_vitals(void)
{
    static agree_t ag;              /* walk_windows' bookkeeping, unused here */

    printf("(e) whose vitals at 50 Hz (default ping), room seed %u: main.c's Step 10 glue - breathing hold,\n"
           "    presence-end clearing, count carry, count_step / id_step decisions, person_vitals.\n"
           "    One room: enrol A and B (%d windows each), train the counter (%d + %d blocks), then\n"
           "    e1  A walks in alone, sits breathing: the name must survive the presence lapse, and\n"
           "        the still spell must become one episode in A's log;\n"
           "    e2  A and B walk in together, both sit: no rate and no name from count 2 on, the\n"
           "        log untouched;\n"
           "    e3  A is recognised and walks out; within the carry B walks in briefly and sits:\n"
           "        B must not inherit A's name or count, and A's log stays untouched;\n"
           "    e4  activity: A walks in, sits breathing, fidgets, walks, leaves - the state per\n"
           "        segment (printed), and one presence episode across the breathing-hold lapse.\n",
           (unsigned)E_SEED, ID_ENROLL, CNT_TRAIN, CNT_TRAIN);

    rng_seed(E_SEED);
    room_make();
    fe_calibrate(50.0f);
    run_empty(EMPTY_AFTER_CAL);
    print_calibration();

    /* enrolment, as (a) */
    gait_init(&s_gait, s_fe.rate);
    ncc_init(&s_id_model, GAIT_NFEAT);
    for (int w = 0; w < 2; w++) {
        int ok = 0;
        ncc_set_name(&s_id_model, w, k_walkers[w].name);
        int got = walk_windows(&k_walkers[w], ID_ENROLL, s_win, &ag);
        for (int i = 0; i < got; i++) ok += ncc_add(&s_id_model, w, s_win[i]) ? 1 : 0;
        CHECK(got == ID_ENROLL && ok == ID_ENROLL, "(e) enrol %s: %d windows, %d added",
              k_walkers[w].name, got, ok);
    }
    /* count training, as (b) */
    count_init(&s_cnt_dev);
    ncc_init(&s_cnt_model, COUNT_NFEAT);
    ncc_set_name(&s_cnt_model, 0, "one");
    ncc_set_name(&s_cnt_model, 1, "two-plus");
    for (int cls = 0; cls < 2; cls++) {
        int added = 0;
        for (int walk = 0; walk < 4; walk++) {
            int w1 = walk & 1;
            int got = walk_blocks(w1, cls ? 1 - w1 : -1, 5, s_blk);
            for (int i = 0; i < got; i++) added += ncc_add(&s_cnt_model, cls, s_blk[i]) ? 1 : 0;
        }
        CHECK(added == CNT_TRAIN, "(e) count class %d: %d blocks added", cls, added);
    }

    /* analytics_configure(): vitals on the selected subcarriers, the
     * latch with main.c's defaults (carry 2 x 30 s, the hold on) */
    const pv_cfg_t  pcfg = { CARRY_US, true };
    const act_cfg_t acfg = { GATE_RATIO, ACT_LO, CARRY_US };
    vitals_init(&s_vit, &k_vcfg);
    vitals_configure(&s_vit, s_fe.sel, NSUB);
    pv_init(&s_pv, &pcfg);
    act_init(&s_act, &acfg);

    e1_walk_sit();
    e2_two_sit();
    e3_walkin_after_lapse();
    e4_activity();
}

/* ================================================================== */
/*  (f) AGC: multiplicative receiver effects (2026-10-30)              */
/* ================================================================== */
#ifndef AGC_SEED                   /* overridable (-DE_SEED=5004u moves both) */
#define AGC_SEED  E_SEED
#endif
#define F_BR      15.0f            /* (f)'s seated person: e1's chest      */
#define F_HR      66.0f

/* MAGNITUDES - ALL ASSUMPTIONS. Neither paper's abstract (the only text in
 * the 2026-10-30 research notes) gives a dB figure, and nothing else in
 * those notes does; Espressif's esp_csi_gain_ctrl (step09 managed
 * component, README) shows the C5 reports a per-packet AGC gain (uint8)
 * and FFT gain (int8), i.e. discrete gain states, and compensates them
 * with ONE scalar per packet - no step size or ripple figure. So:
 *   steps     1-3 dB (the task's "+/-1..3 dB"), mean dwell 10 s, the running
 *             total within +/-3 dB of the calibration gain;
 *   drift     +/-2 dB sine, 240 s period (a slow thermal-like wander);
 *   ripple    +/-1.5 dB smooth across the band + 0.3 dB rms per subcarrier,
 *             fixed (present at calibration);
 *   state     0.5 dB rms smooth per-subcarrier change per 1-dB gain state;
 *   comp      1 dB compression at 60 LSB, i.e. 1.28 x the strongest
 *             nominal tone (47 LSB): ~0.4 dB on it at the calibration gain,
 *             ~1.4 dB after a +3 dB step;
 * and a constant-offset sweep (-6 .. +6 dB) so the break point is measured
 * rather than guessed. */
static const agc_cfg_t k_agc_cfg[] = {
    /* name          offset dwell lim  drift  per    rip   rnd   state comp */
    { "reference",    0.0f,  0.0f, 0.0f, 0.0f,   0.0f, 0.0f, 0.0f, 0.0f,  0.0f },
    { "steps",        0.0f, 10.0f, 3.0f, 0.0f,   0.0f, 0.0f, 0.0f, 0.0f,  0.0f },
    { "drift",        0.0f,  0.0f, 0.0f, 2.0f, 240.0f, 0.0f, 0.0f, 0.0f,  0.0f },
    { "ripple",       0.0f,  0.0f, 0.0f, 0.0f,   0.0f, 1.5f, 0.3f, 0.0f,  0.0f },
    { "comp",         0.0f,  0.0f, 0.0f, 0.0f,   0.0f, 0.0f, 0.0f, 0.0f, 60.0f },
    { "steps+comp",   0.0f, 10.0f, 3.0f, 0.0f,   0.0f, 0.0f, 0.0f, 0.0f, 60.0f },
    { "drift+comp",   0.0f,  0.0f, 0.0f, 2.0f, 240.0f, 0.0f, 0.0f, 0.0f, 60.0f },
    { "steps+state",  0.0f, 10.0f, 3.0f, 0.0f,   0.0f, 0.0f, 0.0f, 0.5f,  0.0f },
    { "all",          0.0f, 10.0f, 3.0f, 2.0f, 240.0f, 1.5f, 0.3f, 0.5f, 60.0f },
};
#define F_NCFG ((int)(sizeof(k_agc_cfg) / sizeof(k_agc_cfg[0])))

/* the AGC process's own PRNG seed, from the room seed (at run time: no
 * constant-expression overflow for MSVC's C4307) */
static uint32_t agc_seed(void)
{
    uint32_t r = AGC_SEED;
    return r * 2654435761u;
}

static bool agc_time_varying(const agc_cfg_t *c)
{
    return c->offset_db != 0.0f || c->step_mean_s > 0.0f || c->drift_db > 0.0f;
}

static bool agc_common_only(const agc_cfg_t *c)
{
    return c->ripple_db == 0.0f && c->ripple_rand_db == 0.0f && c->state_db == 0.0f && c->comp_1db == 0.0f;
}

static void f_res_reset(void)
{
    s_res_sum = 0.0;
    s_res_max = 0.0f;
    s_res_n   = 0;
}

/* f0: the claim itself, on one real packet's amplitudes: dividing by the
 * median cancels any common factor (floats; the median stays >= 1). */
static void f_identity(void)
{
    float na0[NSUB], na1[NSUB], a1[NSUB], scr[NSUB];
    static const float dbs[] = { -6.0f, -3.0f, -1.0f, 1.0f, 3.0f, 6.0f };
    float worst = 0.0f;

    float m0 = dsp_median(s_amp, NSUB, scr);
    if (m0 < 1.0f) m0 = 1.0f;
    for (int k = 0; k < NSUB; k++) na0[k] = s_amp[k] / m0;
    for (size_t i = 0; i < sizeof(dbs) / sizeof(dbs[0]); i++) {
        float g = db_lin(dbs[i]);
        for (int k = 0; k < NSUB; k++) a1[k] = g * s_amp[k];
        float m1 = dsp_median(a1, NSUB, scr);
        if (m1 < 1.0f) m1 = 1.0f;
        for (int k = 0; k < NSUB; k++) {
            na1[k] = a1[k] / m1;
            float e = fabsf(na1[k] - na0[k]) / (na0[k] > 1e-3f ? na0[k] : 1e-3f);
            if (e > worst) worst = e;
        }
    }
    printf("    f0: median normalisation of one packet x {-6,-3,-1,+1,+3,+6} dB: worst relative "
           "change %.2e (median %.1f LSB)\n", (double)worst, (double)m0);
    CHECK(m0 * db_lin(-6.0f) >= 1.0f && worst < 1e-5f,
          "f0: a common gain changed the normalised amplitude by %.2e (median %.1f)", (double)worst,
          (double)m0);
}

/* f1: a constant common offset after calibration, empty room, front end +
 * gate + vitals as (d). With and without compression. */
typedef struct {
    long  n, flag, present, sh_present, statics, gate_n, gate_mv;
    int   an, br, onsets;
    bool  pp;
    double msig_late;
    long  n_late;
    float dsig_max;
} fsweep_t;

static fsweep_t f_sweep_one(float offset_db, float comp, float state)
{
    fsweep_t  o;
    scene_t   sc;
    agc_cfg_t c;
    memset(&o, 0, sizeof(o));
    memset(&sc, 0, sizeof(sc));
    memset(&c, 0, sizeof(c));
    c.name      = "offset";
    c.offset_db = offset_db;
    c.comp_1db  = comp;
    c.state_db  = state;
    rng_seed(AGC_SEED);
    room_make();
    agc_configure(&c, agc_seed());
    fe_calibrate(50.0f);
    run_empty(EMPTY_AFTER_CAL);
    vitals_init(&s_vit, &k_vcfg);
    vitals_configure(&s_vit, s_fe.sel, NSUB);
    agc_start(s_fe.t_us);
    int n = (int)(60.0f * s_fe.rate_nom);
    for (int i = 0; i < n; i++) {
        if (!slot(&sc)) continue;
        o.n++;
        if (s_fe.motion_flag) o.flag++;
        if (s_fe.present) o.present++;
        if (s_fe.present && !o.pp) o.onsets++;
        o.pp = s_fe.present;
        if (s_fe.present || s_fe.sh_present) o.sh_present++;
        if (s_fe.static_flag) o.statics++;
        if (s_fe.d_sigma > o.dsig_max) o.dsig_max = s_fe.d_sigma;
        if ((float)i > 3.0f * s_fe.rate_nom) {
            o.gate_n++;
            if (!s_fe.still) o.gate_mv++;
        }
        if ((float)i >= 30.0f * s_fe.rate_nom) {
            o.n_late++;
            o.msig_late += (double)s_fe.m_sigma;
        }
        if (vitals_push(&s_vit, s_fe.t_us, s_fe.na, s_fe.still)) {
            o.an++;
            if (vitals_get(&s_vit)->br_valid) o.br++;
        }
    }
    agc_off();
    return o;
}

static float pct(long a, long n) { return 100.0f * (float)a / (float)(n ? n : 1); }

#define F_NSWEEP 32
static fsweep_t s_sweep[F_NSWEEP];
static float    s_sweep_row[F_NSWEEP][3];
static int      s_sweep_n;

static void f_sweep(void)
{
    /* rows: compression 1 dB point (0 = off), gain-state ripple, offset */
    static const float rows[][3] = {
        { 0.0f, 0.0f, -6.0f }, { 0.0f, 0.0f, -4.5f }, { 0.0f, 0.0f, -3.0f }, { 0.0f, 0.0f, -1.5f }, { 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.5f }, { 0.0f, 0.0f, 3.0f }, { 0.0f, 0.0f, 6.0f },
        { 60.0f, 0.0f, -6.0f }, { 60.0f, 0.0f, -3.0f }, { 60.0f, 0.0f, -1.0f }, { 60.0f, 0.0f, 0.0f }, { 60.0f, 0.0f, 1.0f },
        { 60.0f, 0.0f, 3.0f }, { 60.0f, 0.0f, 6.0f },
        { 90.0f, 0.0f, -3.0f }, { 90.0f, 0.0f, 1.0f }, { 90.0f, 0.0f, 3.0f },
        { 120.0f, 0.0f, -3.0f }, { 120.0f, 0.0f, 1.0f }, { 120.0f, 0.0f, 3.0f },
        { 0.0f, 0.1f, 1.0f }, { 0.0f, 0.1f, -2.0f }, { 0.0f, 0.2f, 1.0f }, { 0.0f, 0.2f, -2.0f },
        { 0.0f, 0.3f, 1.0f }, { 0.0f, 0.3f, -2.0f }, { 0.0f, 0.5f, 1.0f }, { 0.0f, 0.5f, -2.0f },
    };
    printf("    f1: ONE gain change, held: a constant offset from 5 s after calibration, 60 s of\n"
           "        empty room (front end, gate and vitals as (d)). comp = 1 dB compression point,\n"
           "        LSB (strongest nominal tone 47); state = gain-state ripple, dB rms. m_sigma =\n"
           "        Step 08's motion metric in empty-room std, mean over the last 30 s (the flag sets\n"
           "        at %.1f); +static = present OR the SHADOW D branch (not in the mirror):\n"
           "        comp state offset  m_sigma  flag%%  present%% (onsets)  +static%%  static%%  dsig_max  gate%%  br/an\n",
           (double)THR_MOTION);
    size_t nrow = sizeof(rows) / sizeof(rows[0]);
    if (nrow > F_NSWEEP) nrow = F_NSWEEP;
    for (size_t i = 0; i < nrow; i++) {
        float comp = rows[i][0], st = rows[i][1], off = rows[i][2];
        fsweep_t o = f_sweep_one(off, comp, st);
        float ms = (float)(o.msig_late / (double)(o.n_late ? o.n_late : 1));
        printf("        %4.0f %5.1f %+6.1f  %7.2f  %5.1f  %7.1f (%2d)     %8.1f  %7.1f  %8.1f  %5.1f  %2d/%2d\n",
               (double)comp, (double)st, (double)off, (double)ms, (double)pct(o.flag, o.n),
               (double)pct(o.present, o.n), o.onsets, (double)pct(o.sh_present, o.n),
               (double)pct(o.statics, o.n), (double)o.dsig_max, (double)pct(o.gate_mv, o.gate_n),
               o.br, o.an);
        s_sweep[i] = o;
        s_sweep_row[i][0] = comp;
        s_sweep_row[i][1] = st;
        s_sweep_row[i][2] = off;
    }
    s_sweep_n = (int)nrow;
}

/* f2: one visit through (e)'s glue */
typedef struct {
    long  n, present, sh_present, statics, gate_mv, flag, act_ok, held;
    long  n_late, present_late, sh_late, act_ok_late;
    int   onsets, sh_onsets, an, br, br_ok, hr, credits;
    double msig_sum;
    float  dsig_max;
    bool   pp, psh;
} fseg_t;

static void f_run(const scene_t *sc, float seconds, act_state_t truth, float late_s, fseg_t *g)
{
    int      n    = (int)(seconds * s_fe.rate_nom);
    int      late = (int)(late_s * s_fe.rate_nom);
    int      cr0  = s_gl.credits;
    uint32_t upd  = vitals_get(&s_vit)->updates;
    for (int i = 0; i < n; i++) {
        if (!slot(sc)) continue;
        gl_packet();
        bool p  = s_gl.present;
        bool sh = p || s_fe.sh_present;
        bool ok = act_state(&s_act) == truth;
        g->n++;
        if (p) g->present++;
        if (sh) g->sh_present++;
        if (p && !g->pp) g->onsets++;
        if (sh && !g->psh) g->sh_onsets++;
        g->pp  = p;
        g->psh = sh;
        if (s_fe.static_flag) g->statics++;
        if (!s_fe.still) g->gate_mv++;
        if (s_fe.motion_flag) g->flag++;
        if (s_gl.breath_hold) g->held++;
        if (ok) g->act_ok++;
        if (i >= late) {
            g->n_late++;
            if (p) g->present_late++;
            if (sh) g->sh_late++;
            if (ok) g->act_ok_late++;
        }
        g->msig_sum += (double)s_fe.m_sigma;
        if (s_fe.d_sigma > g->dsig_max) g->dsig_max = s_fe.d_sigma;
        const vitals_out_t *v = vitals_get(&s_vit);
        if (v->updates != upd) {
            upd = v->updates;
            g->an++;
            if (v->br_valid) {
                g->br++;
                if (fabsf(v->br_bpm - F_BR) <= BR_TOL) g->br_ok++;
            }
            if (v->hr_valid) g->hr++;
        }
    }
    g->credits += s_gl.credits - cr0;
}

typedef struct {
    const char *name;
    bool   boot;                   /* time-varying parts from boot, not after calibration */
    float  m_std, d_std;
    int    steps;
    int    steps_cal;              /* of those, taken before calibration ended */
    uint32_t rng_cal;              /* g_rng after calibration + EMPTY_AFTER_CAL: fixed
                                      slot counts, so the same in every visit */
    int    cnt_n[2];               /* count blocks trained per class (want CNT_TRAIN) */
    bool   named;
    float  walk_s;
    fseg_t e, w, s, w2, l;
    double res_mean;
    float  res_max;
} fvisit_t;

/* c = NULL: the same visit with the AGC model off */
static void f_visit(const agc_cfg_t *c, bool boot, fvisit_t *v)
{
    static agree_t ag;
    scene_t   sc;
    session_t s;

    memset(v, 0, sizeof(*v));     /* also zeroes the padding: f-rng memcmp()s segments */
    v->name = c ? c->name : "AGC off";
    v->boot = boot;
    rng_seed(AGC_SEED);
    room_make();
    if (c) agc_configure(c, agc_seed());    /* c NULL: the model off (f-rng) */
    else   agc_off();
    if (boot) agc_start(0);
    fe_calibrate(50.0f);
    v->steps_cal = s_agc.steps;
    if (!boot) agc_start(s_fe.t_us);
    run_empty(EMPTY_AFTER_CAL);
    v->rng_cal = g_rng;
    v->m_std = s_fe.m_std;
    v->d_std = s_fe.d_std;

    /* enrolment and count training as (e), in the perturbed radio */
    gait_init(&s_gait, s_fe.rate);
    ncc_init(&s_id_model, GAIT_NFEAT);
    for (int w = 0; w < 2; w++) {
        ncc_set_name(&s_id_model, w, k_walkers[w].name);
        int got = walk_windows(&k_walkers[w], ID_ENROLL, s_win, &ag);
        for (int i = 0; i < got; i++) (void)ncc_add(&s_id_model, w, s_win[i]);
    }
    count_init(&s_cnt_dev);
    ncc_init(&s_cnt_model, COUNT_NFEAT);
    ncc_set_name(&s_cnt_model, 0, "one");
    ncc_set_name(&s_cnt_model, 1, "two-plus");
    s_blk_cap = (long)(120.0f * s_fe.rate_nom);          /* <= 120 s per training walk */
    for (int cls = 0; cls < 2; cls++) {
        for (int walk = 0; walk < 4; walk++) {
            int w1 = walk & 1;
            int got = walk_blocks(w1, cls ? 1 - w1 : -1, 5, s_blk);
            for (int i = 0; i < got; i++) (void)ncc_add(&s_cnt_model, cls, s_blk[i]);
        }
    }
    s_blk_cap = 400000;
    v->cnt_n[0] = (int)s_cnt_model.cls[0].n;
    v->cnt_n[1] = (int)s_cnt_model.cls[1].n;
    const pv_cfg_t  pcfg = { CARRY_US, true };
    const act_cfg_t acfg = { GATE_RATIO, ACT_LO, CARRY_US };
    vitals_init(&s_vit, &k_vcfg);
    vitals_configure(&s_vit, s_fe.sel, NSUB);
    pv_init(&s_pv, &pcfg);
    act_init(&s_act, &acfg);
    gl_begin();
    f_res_reset();

    memset(&sc, 0, sizeof(sc));
    f_run(&sc, 120.0f, ACT_EMPTY, 0.0f, &v->e);                  /* empty room          */

    session_start(&s, &k_walkers[0], s_fe.rate_nom);              /* A walks in          */
    sc.walker[0] = &s;
    sc.nwalker   = 1;
    do {
        f_run(&sc, 1.0f, ACT_MOVING, v->walk_s < 2.0f ? 2.0f - v->walk_s : 0.0f, &v->w);
        v->walk_s += 1.0f;
    } while (v->walk_s < 30.0f || (pv_attr(&s_pv, NULL) != PV_PERSON && v->walk_s < 120.0f));
    v->named = pv_attr(&s_pv, NULL) == PV_PERSON;

    chest_make(&s_chest, 0.6f, 0.20f, F_BR, F_HR);                /* sits, breathing     */
    sc.nwalker     = 0;
    sc.chest       = &s_chest;
    sc.chest_t0_us = s_fe.t_us;
    f_run(&sc, 90.0f, ACT_STILL, 10.0f, &v->s);

    session_start(&s, &k_walkers[0], s_fe.rate_nom);              /* walks 20 s          */
    sc.chest   = NULL;
    sc.nwalker = 1;
    f_run(&sc, 20.0f, ACT_MOVING, 2.0f, &v->w2);

    sc.nwalker = 0;                                               /* leaves, 75 s        */
    f_run(&sc, 75.0f, ACT_EMPTY, 15.0f, &v->l);

    v->steps    = s_agc.steps;
    v->res_mean = s_res_n ? s_res_sum / (double)s_res_n : 0.0;
    v->res_max  = s_res_max;
    agc_off();
}

static void f_print(const fvisit_t *v)
{
    printf("        %-11s %-4s %4d %5.3f %5.3f %5.2f %5.2f %2d/%2d | %5.1f %2d %5.1f %2d %5.1f %4.1f %2d %2d %5.1f\n",
           v->name, v->boot ? "boot" : "cal", v->steps, (double)v->m_std, (double)v->d_std,
           v->res_mean, (double)v->res_max, v->cnt_n[0], v->cnt_n[1],
           (double)pct(v->e.present, v->e.n), v->e.onsets, (double)pct(v->e.sh_present, v->e.n),
           v->e.sh_onsets, (double)pct(v->e.statics, v->e.n), (double)pct(v->e.gate_mv, v->e.n), v->e.br,
           v->e.credits, (double)pct(v->e.act_ok, v->e.n));
    printf("        %-16s %5.1f %5.1f %3.0f%s | %5.1f %2d/%2d/%2d %2d %5.1f | %5.1f %5.1f %5.1f | %5.1f %5.1f %5.1f %2d %2d\n",
           "", (double)pct(v->w.present_late, v->w.n_late), (double)pct(v->w.act_ok_late, v->w.n_late),
           (double)v->walk_s, v->named ? "+" : "-",
           (double)pct(v->s.present, v->s.n), v->s.an, v->s.br, v->s.br_ok, v->s.credits,
           (double)pct(v->s.act_ok_late, v->s.n_late),
           (double)pct(v->w2.present_late, v->w2.n_late), (double)pct(v->w2.act_ok_late, v->w2.n_late),
           (double)pct(v->w2.sh_late, v->w2.n_late),
           (double)pct(v->l.present_late, v->l.n_late), (double)pct(v->l.sh_late, v->l.n_late),
           (double)pct(v->l.act_ok_late, v->l.n_late), v->l.br, v->l.credits);
}

static fvisit_t s_fv[2 * F_NCFG];

static const agc_cfg_t *f_cfg(const char *name)
{
    for (int j = 0; j < F_NCFG; j++) {
        if (strcmp(k_agc_cfg[j].name, name) == 0) return &k_agc_cfg[j];
    }
    return NULL;
}

/* A miss (a renamed config, an edited sweep row) is a FAILURE and returns
 * an all-zero sentinel - never another row's numbers under this label. */
static const fvisit_t *f_find(int nv, const char *name, bool boot)
{
    static const fvisit_t none = { 0 };
    bool found = false;               /* a variable: no constant condition (MSVC C4127) */
    for (int i = 0; i < nv; i++) {
        if (strcmp(s_fv[i].name, name) == 0 && s_fv[i].boot == boot) return &s_fv[i];
    }
    CHECK(found, "f_find: no f2 visit '%s'/%s (a KNOWN LIMIT line would print zeros)", name,
          boot ? "boot" : "cal");
    return &none;
}

static const fsweep_t *f_row(float comp, float state, float off)
{
    static const fsweep_t none = { 0 };
    bool found = false;
    for (int i = 0; i < s_sweep_n; i++) {
        if (s_sweep_row[i][0] == comp && s_sweep_row[i][1] == state && s_sweep_row[i][2] == off) {
            return &s_sweep[i];
        }
    }
    CHECK(found, "f_row: no f1 sweep row comp %.0f state %.1f offset %+.1f (a KNOWN LIMIT line would"
          " print zeros)", (double)comp, (double)state, (double)off);
    return &none;
}

static float f_msig(const fsweep_t *o) { return (float)(o->msig_late / (double)(o->n_late ? o->n_late : 1)); }

/* What the code provably handles: a COMMON gain divides out (f0, and the
 * float residual in situ); the receiver's fixed properties are part of
 * the room calibration saw; what a common gain leaves in int8 CSI is
 * rounding noise, white and ~8 % larger at -3 dB, which neither the gate
 * nor the breathing detector mistakes for anything; walking stays far
 * above it; and nobody's log can be credited before anybody is named.
 * res_max checks the MODEL (the float twin, normalised inside
 * agc_residual), not fe_packet; fe_packet's own median division is what
 * the br / gate_mv / static checks see (review 2026-11-01: a mutant that
 * froze fe_packet's median while the model is on failed 12 of them, the
 * gate calling MOVING on up to 1098 empty-room packets). The shadow's
 * threshold, sustain, hold and freeze are f-shadow's (scripted). */
static void f_checks(int nv)
{
    const fvisit_t *ref = &s_fv[0];
    CHECK(ref->res_max == 0.0f, "f2 reference: the model with every factor 1 changed na by %.3g sigma",
          (double)ref->res_max);
    CHECK(ref->e.statics == 0 && ref->l.statics == 0 && ref->l.n_late > 0,
          "f2 reference: the shadow static branch fired on %ld empty-room packets (%ld after leaving) - "
          "the shadow is wrong", ref->e.statics, ref->l.statics);
    for (int i = 0; i < s_sweep_n; i++) {
        const fsweep_t *o = &s_sweep[i];
        if (s_sweep_row[i][0] == 0.0f && s_sweep_row[i][1] == 0.0f && fabsf(s_sweep_row[i][2]) <= 3.0f) {
            CHECK(o->br == 0 && o->gate_mv == 0, "f1: common offset %+.1f dB: %d empty-room analyses "
                  "reported breathing, the gate called %ld packets MOVING", (double)s_sweep_row[i][2], o->br,
                  o->gate_mv);
        }
    }
    for (int i = 0; i < nv; i++) {
        const fvisit_t  *v = &s_fv[i];
        const agc_cfg_t *c = f_cfg(v->name);
        const char      *rg = v->boot ? "boot" : "cal";
        CHECK(v->e.credits == 0, "f2 %s/%s: %d credits in the empty room before anybody was named",
              v->name, rg, v->e.credits);
        if (!c) continue;
        if (agc_common_only(c) || !agc_time_varying(c)) {
            CHECK(v->res_max < 1e-3f, "f2 %s/%s: %.3g sigma left after normalisation (common gain or a "
                  "fixed receiver property)", v->name, rg, (double)v->res_max);
        }
        if (agc_common_only(c)) {
            CHECK(v->e.br == 0 && v->e.gate_mv == 0,
                  "f2 %s/%s: empty room: %d analyses reported breathing, gate MOVING on %ld packets",
                  v->name, rg, v->e.br, v->e.gate_mv);
            CHECK(v->w.n_late > 0 && pct(v->w.present_late, v->w.n_late) >= 99.0f &&
                  pct(v->w2.present_late, v->w2.n_late) >= 99.0f,
                  "f2 %s/%s: walking read PRESENT on only %.1f%% (walk-in) / %.1f%% (walk) of packets",
                  v->name, rg, (double)pct(v->w.present_late, v->w.n_late),
                  (double)pct(v->w2.present_late, v->w2.n_late));
        }
    }
}

/* f-cal's verdict for one 'boot' visit, from its own numbers (not fixed
 * text: whether boot-time re-locks deafen the detector, make it see a
 * person in an empty room, or neither, differs between models and rooms). */
static void f_cal_verdict(const fvisit_t *v, const fvisit_t *ref, char *out, size_t n)
{
    float wp   = pct(v->w.present_late, v->w.n_late);
    bool  deaf = v->m_std > 2.0f * ref->m_std && (wp < 95.0f || !v->named);
    bool  fals = pct(v->e.present, v->e.n) > pct(ref->e.present, ref->e.n) + 10.0f;
    snprintf(out, n, "%s%s%s%s", deaf ? "DEAF" : "", deaf && fals ? " + " : "",
             fals ? "FALSE PRESENT" : (deaf ? "" : "neither"), v->named ? "" : ", A not named");
}

/* What it does not handle: printed with this room's numbers, the suite
 * stays green. */
static void f_known_limits(int nv)
{
    const fvisit_t *ref = &s_fv[0];
    const fsweep_t *z = f_row(0.0f, 0.0f, 0.0f), *d15 = f_row(0.0f, 0.0f, -1.5f), *d3 = f_row(0.0f, 0.0f, -3.0f),
                   *d45 = f_row(0.0f, 0.0f, -4.5f), *d6 = f_row(0.0f, 0.0f, -6.0f);
    const fvisit_t *st = f_find(nv, "steps", false);
    printf("    KNOWN LIMIT f-int8 (room %u): a common gain DROP is NOT fully cancelled in int8 CSI -\n"
           "        the rounding noise is a fixed ~0.29 LSB while the signal shrinks, so the relative\n"
           "        noise grows and Step 08's M reads it as motion. Held offset, 60 s empty room:\n"
           "        m_sigma %+.2f / %+.2f / %+.2f / %+.2f / %+.2f at 0 / -1.5 / -3 / -4.5 / -6 dB, flag on\n"
           "        %.1f / %.1f / %.1f / %.1f / %.1f %%, PRESENT %.1f / %.1f / %.1f / %.1f / %.1f %% (%d / %d / %d / %d / %d\n"
           "        onsets). +/-3 dB steps in the visit: empty room PRESENT %.1f%% (%d onsets; reference\n"
           "        %.1f%%, %d), after leaving %.1f%% (reference %.1f%%). (A gain RISE lowers m_sigma: less\n"
           "        sensitive, not false.) Fix (proposed): store the calibration median level in the\n"
           "        baseline and add the rounding term to each sigma_k when the live median is lower,\n"
           "        sigma_k^2 + q^2 (1/med_now^2 - 1/med_cal^2), q^2 ~ 1/12 LSB^2 (the rounding error of\n"
           "        the two components, projected onto the amplitude: (0.29 LSB)^2);\n"
           "        or recalibrate / warn after a sustained median change of > 3 dB. Whether the\n"
           "        board's 12-bit L-LTF words (CSI_LLTF_ONLY=y) shrink q is not modelled here (int8 only).\n",
           (unsigned)AGC_SEED, (double)f_msig(z), (double)f_msig(d15), (double)f_msig(d3), (double)f_msig(d45),
           (double)f_msig(d6), (double)pct(z->flag, z->n), (double)pct(d15->flag, d15->n),
           (double)pct(d3->flag, d3->n), (double)pct(d45->flag, d45->n), (double)pct(d6->flag, d6->n),
           (double)pct(z->present, z->n), (double)pct(d15->present, d15->n), (double)pct(d3->present, d3->n),
           (double)pct(d45->present, d45->n), (double)pct(d6->present, d6->n), z->onsets, d15->onsets,
           d3->onsets, d45->onsets, d6->onsets, (double)pct(st->e.present, st->e.n), st->e.onsets,
           (double)pct(ref->e.present, ref->e.n), ref->e.onsets, (double)pct(st->l.present_late, st->l.n_late),
           (double)pct(ref->l.present_late, ref->l.n_late));

    const fsweep_t *c1 = f_row(60.0f, 0.0f, 1.0f), *cm1 = f_row(60.0f, 0.0f, -1.0f), *c3 = f_row(60.0f, 0.0f, 3.0f),
                   *c90 = f_row(90.0f, 0.0f, 3.0f), *c120 = f_row(120.0f, 0.0f, 3.0f);
    const fvisit_t *sc = f_find(nv, "steps+comp", false), *dc = f_find(nv, "drift+comp", false);
    printf("    KNOWN LIMIT f-comp (room %u): a gain change THROUGH soft compression changes the\n"
           "        SHAPE, which the median cannot remove (strong tones compress more IN THIS PER-TONE\n"
           "        model; a receiver compresses the time-domain composite before the FFT, which also\n"
           "        changes the shape but not in step with tone strength). M sees it for\n"
           "        a fast-EMA time constant; the static (D) branch - SHADOW, not in the mirror's\n"
           "        presence - sees it for the slow EMA's 60 s, and once it holds PRESENT the slow EMA\n"
           "        freezes and D never decays (up to the 600 s freeze cap). 1 dB point 60 LSB, held\n"
           "        offset: static on %.1f / %.1f / %.1f %% of the empty room at -1 / +1 / +3 dB (d_sigma max\n"
           "        %.1f / %.1f / %.1f); +3 dB with the 1 dB point at 90 / 120 LSB: %.1f / %.1f %%. Visit,\n"
           "        empty room: PRESENT %.1f%% in the mirror, %.1f%% with the shadow D (steps+comp), %.1f%% /\n"
           "        %.1f%% (drift+comp); after leaving %.1f%% / %.1f%% and %.1f%% / %.1f%%. Fix (proposed): read\n"
           "        rx_ctrl's AGC and FFT gain per packet (the C5 has them: step09's esp_csi_gain_ctrl,\n"
           "        esp_csi_gain_ctrl.h:18-20) and log them first, to measure real step sizes; then on a\n"
           "        gain change re-anchor D (re-seed the slow EMA to the fast one ~1 s later, as for a\n"
           "        new channel) and skip M for that second; or lock the gain\n"
           "        (esp_csi_gain_ctrl_set_rx_force_gain: \"may cause packet loss\").\n",
           (unsigned)AGC_SEED, (double)pct(cm1->statics, cm1->n), (double)pct(c1->statics, c1->n),
           (double)pct(c3->statics, c3->n), (double)cm1->dsig_max, (double)c1->dsig_max, (double)c3->dsig_max,
           (double)pct(c90->statics, c90->n), (double)pct(c120->statics, c120->n),
           (double)pct(sc->e.present, sc->e.n), (double)pct(sc->e.sh_present, sc->e.n),
           (double)pct(dc->e.present, dc->e.n), (double)pct(dc->e.sh_present, dc->e.n),
           (double)pct(sc->l.present_late, sc->l.n_late), (double)pct(sc->l.sh_late, sc->l.n_late),
           (double)pct(dc->l.present_late, dc->l.n_late), (double)pct(dc->l.sh_late, dc->l.n_late));

    const fsweep_t *s1 = f_row(0.0f, 0.1f, 1.0f), *s2 = f_row(0.0f, 0.1f, -2.0f), *s5 = f_row(0.0f, 0.5f, 1.0f);
    const fvisit_t *ss = f_find(nv, "steps+state", false), *al = f_find(nv, "all", false);
    printf("    KNOWN LIMIT f-state (room %u): a per-subcarrier response that changes with the GAIN\n"
           "        STATE is a shape step at every re-lock. Held offset: the shadow static branch on\n"
           "        %.1f / %.1f %% of the empty room at 0.1 dB rms (+1 / -2 dB), %.1f %% at 0.5 dB rms; the\n"
           "        mirror PRESENT %.1f / %.1f / %.1f %%. Visit with +/-1..3 dB steps and 0.5 dB rms:\n"
           "        empty room PRESENT %.1f%% (%d onsets, reference %.1f%%), %d analyses reported\n"
           "        BREATHING in the empty room (the breathing hold then holds PRESENT: %ld packets),\n"
           "        activity 'empty' on %.1f%% (reference %.1f%%); seated: breathing on %d of %d analyses\n"
           "        (reference %d of %d), %d within %.0f/min; after leaving PRESENT %.1f%%, %d breathing analyses,\n"
           "        %d credits; 'all' combined: empty PRESENT %.1f%% (%.1f%% with D), activity empty\n"
           "        %.1f%%. Fix (proposed): as f-comp, plus on a gain change re-seed the fast EMA and\n"
           "        end the vitals still buffer (treat it as a gap), or keep one baseline per gain state.\n",
           (unsigned)AGC_SEED, (double)pct(s1->statics, s1->n), (double)pct(s2->statics, s2->n),
           (double)pct(s5->statics, s5->n), (double)pct(s1->present, s1->n), (double)pct(s2->present, s2->n),
           (double)pct(s5->present, s5->n), (double)pct(ss->e.present, ss->e.n), ss->e.onsets,
           (double)pct(ref->e.present, ref->e.n), ss->e.br, ss->e.held, (double)pct(ss->e.act_ok, ss->e.n),
           (double)pct(ref->e.act_ok, ref->e.n), ss->s.br, ss->s.an, ref->s.br, ref->s.an, ss->s.br_ok,
           (double)BR_TOL, (double)pct(ss->l.present_late, ss->l.n_late), ss->l.br, ss->l.credits,
           (double)pct(al->e.present, al->e.n), (double)pct(al->e.sh_present, al->e.n),
           (double)pct(al->e.act_ok, al->e.n));

    const fvisit_t *sb = f_find(nv, "steps+state", true), *ab = f_find(nv, "all", true),
                   *cb = f_find(nv, "steps+comp", true);
    char vd[3][48];
    f_cal_verdict(sb, ref, vd[0], sizeof(vd[0]));
    f_cal_verdict(ab, ref, vd[1], sizeof(vd[1]));
    f_cal_verdict(cb, ref, vd[2], sizeof(vd[2]));
    printf("    KNOWN LIMIT f-cal (room %u): when calibration itself sees the re-locks ('boot': %d / %d /\n"
           "        %d gain steps before calibration ended), the shape steps distort the empty-room\n"
           "        statistics. Verdict (DEAF = m_std > 2 x reference AND walk-in PRESENT < 95 %% or A\n"
           "        not named; FALSE PRESENT = empty-room PRESENT > reference + 10 points): %s / %s / %s.\n"
           "        m_std %.3f / %.3f / %.3f, d_std %.3f / %.3f / %.3f (steps+state / all / steps+comp; reference\n"
           "        %.3f, %.3f); count blocks trained %d+%d / %d+%d / %d+%d of %d+%d; walking PRESENT on %.1f / %.1f /\n"
           "        %.1f %% of walk-in packets (reference %.1f), A named %s / %s / %s, seated credits %d / %d / %d\n"
           "        (reference %d). Fix (proposed): restart a calibration phase on a gain-state change\n"
           "        (rx_ctrl, as above), or compute sigma within constant-gain segments.\n",
           (unsigned)AGC_SEED, sb->steps_cal, ab->steps_cal, cb->steps_cal, vd[0], vd[1], vd[2],
           (double)sb->m_std, (double)ab->m_std, (double)cb->m_std, (double)sb->d_std,
           (double)ab->d_std, (double)cb->d_std, (double)ref->m_std, (double)ref->d_std, sb->cnt_n[0],
           sb->cnt_n[1], ab->cnt_n[0], ab->cnt_n[1], cb->cnt_n[0], cb->cnt_n[1], CNT_TRAIN, CNT_TRAIN,
           (double)pct(sb->w.present_late, sb->w.n_late), (double)pct(ab->w.present_late, ab->w.n_late),
           (double)pct(cb->w.present_late, cb->w.n_late), (double)pct(ref->w.present_late, ref->w.n_late),
           sb->named ? "yes" : "no", ab->named ? "yes" : "no", cb->named ? "yes" : "no", sb->s.credits,
           ab->s.credits, cb->s.credits, ref->s.credits);

    printf("    KNOWN LIMIT f-mirror: the static branch here is a SHADOW: it never feeds presence, the\n"
           "        gate's room_empty or the Step 10 glue, so what a device does AFTER a false static\n"
           "        PRESENT (gate floor, breathing hold, latch, activity) is not measured. The numbers\n"
           "        with D are, in f1, Step 08's presence (motion + hold) OR the shadow D; in f2, Step\n"
           "        10's presence (motion + hold + breathing hold, which also refreshes the hold timer\n"
           "        and freezes the shadow's slow EMA) OR the shadow D - in neither is it a device with D\n"
           "        feeding presence. And every magnitude above is an assumption.\n");
}

/* f-shadow: the shadow static branch on a SCRIPTED d_sigma at 50 Hz (no
 * room, no AGC), against main.c's rules: d_sigma >= THR_STATIC (5.0) held
 * for STATIC_SUSTAIN_US (4 s) sets the flag, any packet below clears it,
 * PRESENT holds PRESENCE_HOLD_US (5 s) after the last flagged packet, and
 * the slow EMA freezes while present for at most SLOW_FREEZE_MAX_US. The
 * visits alone could not tell a wrong threshold or a missing hold. */
static void f_shadow_script(void)
{
    static const struct { int t0_ms, t1_ms; float d; } seg[] = {
        {     0, 10000, 0.0f }, { 10000, 13900, 6.0f },   /* 3.9 s above: no flag   */
        { 13900, 20000, 0.0f }, { 20000, 40000, 4.9f },   /* 20 s just below: none  */
        { 40000, 44100, 5.0f },                           /* 4.1 s at it: flag at 44 */
        { 44100, 60000, 0.0f },                           /* hold to 44.08 + 5 s    */
    };
    const int64_t t0_us = 1000000;                        /* 0 means "unset"        */
    int  first_flag = -1, flag_off = -1, sh_last = -1, sh_first = -1;
    memset(&s_fe, 0, sizeof(s_fe));
    s_fe.calibrated = true;
    for (size_t g = 0; g < sizeof(seg) / sizeof(seg[0]); g++) {
        for (int ms = seg[g].t0_ms; ms < seg[g].t1_ms; ms += 20) {
            s_fe.t_us    = t0_us + (int64_t)ms * 1000;
            s_fe.d_sigma = seg[g].d;
            bool was = s_fe.static_flag;
            sh_static(false);
            if (s_fe.static_flag && first_flag < 0) first_flag = ms;
            if (was && !s_fe.static_flag && flag_off < 0) flag_off = ms;
            if (s_fe.sh_present) {
                if (sh_first < 0) sh_first = ms;
                sh_last = ms;
            }
        }
    }
    /* freeze: present from 0 s, for 700 s; then absent; then present again */
    int frz_end = -1;
    bool frz_reset = false, frz_again = false;
    memset(&s_fe, 0, sizeof(s_fe));
    s_fe.calibrated = true;
    s_fe.sh_present = true;
    for (int ms = 0; ms < 700000; ms += 20) {
        s_fe.t_us = t0_us + (int64_t)ms * 1000;
        if (!sh_freeze() && frz_end < 0) frz_end = ms;
    }
    s_fe.sh_present = false;
    s_fe.t_us += 20000;
    frz_reset = !sh_freeze() && s_fe.freeze_since_us == 0;
    s_fe.sh_present = true;
    s_fe.t_us += 20000;
    frz_again = sh_freeze();
    memset(&s_fe, 0, sizeof(s_fe));

    printf("    f-shadow: scripted d_sigma 0 / 6.0 for 3.9 s / 4.9 for 20 s / 5.0 for 4.1 s / 0: static\n"
           "        flag at %.2f s (want 44.00), off at %.2f s (44.10), shadow PRESENT %.2f-%.2f s\n"
           "        (44.00-49.06); slow-EMA freeze ends after %.2f s present (600.00), re-arms %s\n",
           first_flag / 1000.0, flag_off / 1000.0, sh_first / 1000.0, sh_last / 1000.0,
           frz_end / 1000.0, frz_reset && frz_again ? "yes" : "NO");
    CHECK(first_flag == 44000 && flag_off == 44100,
          "f-shadow: static flag set at %d ms / cleared at %d ms, want 44000 / 44100 (threshold %.1f,"
          " sustain %d ms)", first_flag, flag_off, (double)THR_STATIC, STATIC_SUSTAIN_US / 1000);
    CHECK(sh_first == 44000 && sh_last == 49060,
          "f-shadow: shadow PRESENT %d-%d ms, want 44000-49060 (the %d ms hold after the last flagged"
          " packet)", sh_first, sh_last, PRESENCE_HOLD_US / 1000);
    CHECK(frz_end == 600000 && frz_reset && frz_again,
          "f-shadow: slow-EMA freeze ended after %d ms present (want 600000), reset %d, re-armed %d",
          frz_end, (int)frz_reset, (int)frz_again);
}

/* f-rng: the AGC process must draw only from its own PRNG, or every f2
 * row is compared with a reference that saw different noise. Checked two
 * ways: the reference visit with the model OFF must equal the one with it
 * ON at every factor 1 (catches a g_rng draw per packet), and g_rng after
 * calibration + EMPTY_AFTER_CAL (fixed slot counts) must be the same in
 * every visit (catches one per gain step: 'boot' visits step during
 * calibration). */
static void f_rng_checks(int nv)
{
    static fvisit_t off;
    f_visit(NULL, false, &off);
    const fvisit_t *ref = &s_fv[0];
    bool same = memcmp(&off.e, &ref->e, sizeof(fseg_t)) == 0 && memcmp(&off.w, &ref->w, sizeof(fseg_t)) == 0 &&
                memcmp(&off.s, &ref->s, sizeof(fseg_t)) == 0 && memcmp(&off.w2, &ref->w2, sizeof(fseg_t)) == 0 &&
                memcmp(&off.l, &ref->l, sizeof(fseg_t)) == 0 && off.m_std == ref->m_std &&
                off.d_std == ref->d_std && off.cnt_n[0] == ref->cnt_n[0] && off.cnt_n[1] == ref->cnt_n[1] &&
                off.named == ref->named && off.walk_s == ref->walk_s && off.rng_cal == ref->rng_cal;
    int diff = 0, boot_steps = 0;
    for (int i = 0; i < nv; i++) {
        if (s_fv[i].rng_cal != ref->rng_cal) diff++;
        boot_steps += s_fv[i].steps_cal;
    }
    printf("    f-rng: reference visit with the AGC model off %s the one with it on (every factor 1);\n"
           "        g_rng after calibration differs from the reference's in %d of %d visits (%d gain\n"
           "        steps were taken during calibration, 'boot' visits)\n",
           same ? "equals" : "DIFFERS FROM", diff, nv, boot_steps);
    CHECK(same, "f-rng: the reference visit changed when the AGC model was switched off - it draws from"
          " g_rng, or a factor is not exactly 1");
    CHECK(diff == 0 && boot_steps > 0, "f-rng: g_rng after calibration differs in %d of %d visits (%d"
          " gain steps during calibration)", diff, nv, boot_steps);
}

static void test_agc(void)
{
    printf("(f) AGC at 50 Hz, room seed %u: a MULTIPLICATIVE receiver (arXiv 2607.01882, 2605.26836)\n"
           "    in front of the int8 rounding - common gain steps / drift / offset, fixed and\n"
           "    gain-state ripple, soft compression; magnitudes are ASSUMPTIONS (k_agc_cfg).\n",
           (unsigned)AGC_SEED);

    f_shadow_script();

    /* f0 on a calibrated room's packet */
    rng_seed(AGC_SEED);
    room_make();
    fe_calibrate(50.0f);
    f_identity();

    f_sweep();

    printf("    f2: one visit per model through (e)'s glue - enrol A and B, train the counter, then\n"
           "        empty 120 s | A walks in (>= 30 s, until named, <= 120 s) | sits breathing %.0f/min\n"
           "        90 s | walks 20 s | leaves 75 s. 'cal' = steps / drift start after calibration,\n"
           "        'boot' = calibration saw them too. Columns:\n"
           "        line 1: model, regime, steps taken, m_std, d_std, float residual mean / max\n"
           "          (sigma), count blocks trained one/two+ (want %d/%d)\n"
           "          | EMPTY 120 s: present%%, onsets, +static%% (mirror OR shadow D), its onsets,\n"
           "          static%%, gate MOVING%%, analyses reporting breathing, credits, activity=empty%%\n"
           "        line 2: WALK-IN (after 2 s): present%%, activity=moving%%, seconds, named(+)\n"
           "          | SIT: present%%, analyses/br valid/br within %.0f, credits, activity=still%% (after 10 s)\n"
           "          | WALK 20 s (after 2 s): present%%, activity=moving%%, +static%%\n"
           "          | LEFT (after 15 s): present%%, +static%%, activity=empty%%; analyses reporting\n"
           "          breathing, credits (whole 75 s)\n",
           (double)F_BR, CNT_TRAIN, CNT_TRAIN, (double)BR_TOL);
    int nv = 0;
    for (int i = 0; i < F_NCFG; i++) {
        for (int b = 0; b < 2; b++) {
            if (b && !agc_time_varying(&k_agc_cfg[i])) continue;
            f_visit(&k_agc_cfg[i], b != 0, &s_fv[nv]);
            f_print(&s_fv[nv]);
            nv++;
        }
    }

    /* Hard checks: only what the code provably handles. */
    f_rng_checks(nv);
    f_checks(nv);
    f_known_limits(nv);
}

int main(void)
{
    printf("test_e2e: SYNTHETIC CSI through main.c's front end and every Step 10 module.\n"
           "Separable by construction - this is an integration test, not evidence about\n"
           "real people or rooms.\n");
    test_recognition();
    test_count_e2e();
    test_vitals_e2e();
    test_empty_room();
    test_whose_vitals();
    test_agc();
    return test_summary("test_e2e");
}
