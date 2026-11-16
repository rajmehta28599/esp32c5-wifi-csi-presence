/*
 * motion_gate.h - "is anybody moving?" for the vitals analysis, decided on
 *                 the HIGH-frequency part of the group signals only.
 *
 * WHY NOT STEP 08's MOTION FLAG
 *   Step 08's fast EMA (TAU_FAST_S = 0.5 s) has its corner at ~0.32 Hz -
 *   inside the 0.1-0.5 Hz breathing band. So about 60% of a 15/min breath
 *   survives into |na - fast| and lifts the motion metric M. In test_e2e,
 *   a synthetic chest whose breathing was strong enough to lift a
 *   physiologically sized heartbeat (1/10 of breathing) out of the noise
 *   held Step 08's flag on 100% of the time, so vitals - gated on that
 *   flag - never analysed anything. The measurement was switched off by the
 *   very signal it measures. Step 08's detection is left exactly as it is;
 *   this gate is used only for vitals (and to keep a still, breathing person
 *   from feeding the gait and count modules).
 *
 * ALGORITHM (per packet, on the CSI_NGROUPS group signals from
 * dsp_group_dev(), which are already high-passed at ~0.32 Hz):
 *   Two cascaded first-order high-passes with time constant MGATE_TAU_S
 *   (corner ~1.6 Hz):
 *       l1 += a*(grp - l1);   d1 = grp - l1;
 *       l2 += a*(d1  - l2);   d2 = d1  - l2;         a = 1 - exp(-1/(rate*tau))
 *   e  = mean over groups of d2^2
 *   es = EMA of e with time constant MGATE_SMOOTH_S.
 *   A second-order high-pass at 1.6 Hz passes about (0.25/1.6)^2 ~ 2.5% of a
 *   0.25 Hz breath's amplitude (0.06% of its power), while walking - Doppler
 *   in the 2-20 Hz range - passes almost entirely.
 *
 * NOISE FLOOR (so no threshold in absolute units is needed):
 *   e is averaged over each 1-second interval.
 *   - While room_empty is true (main.c passes Step 08's !present), the
 *     1-second means feed floor_empty, an EMA with time constant
 *     MGATE_FLOOR_TAU_S.
 *   - Always, the last MGATE_MIN_SLOTS 1-second means are kept in a ring;
 *     floor_min is their minimum (minimum statistics - a fallback for a
 *     room that has never been seen empty since boot).
 *   floor = floor_empty if any empty-room second has been seen, else
 *   floor_min once MGATE_MIN_VALID slots exist, else unknown.
 *   Known weakness of the fallback: it assumes the quietest recent second
 *   was noise. If every second in the ring contained motion - somebody
 *   moving from the moment the gate started (then STILL after ~4 s), or
 *   without one quiet second for 2 minutes - that motion BECOMES the
 *   floor and the gate calls it still (vitals then run on it, gait/count
 *   get no "moving" packets). One quiet second or one empty-room second
 *   repairs it. After a fresh calibration the room is normally empty for
 *   a while, so floor_empty is the usual case; the fallback matters after
 *   a boot that reloads a stored baseline with somebody in the room.
 *
 * DECISION, with hysteresis in time:
 *   ratio = es / floor.
 *   MOVING -> STILL after ratio < thr continuously for MGATE_STILL_HOLD_US;
 *   STILL -> MOVING after ratio >= thr continuously for MGATE_MOVE_HOLD_US.
 *   Unknown floor -> MOVING (vitals simply waits). A time gap longer than
 *   MGATE_GAP_US restarts the filters and the decision (floor kept).
 *
 * ESP-IDF free; host-tested in test/test_gate.c. Cost per packet: ~40
 * soft-float operations. Memory: < 1 KB.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "csi_dsp.h"

#define MGATE_TAU_S          0.10f
#define MGATE_SMOOTH_S       0.50f
#define MGATE_FLOOR_TAU_S    20.0f
#define MGATE_MIN_SLOTS      120      /* 2 minutes of 1-second means      */
#define MGATE_MIN_VALID      3
#define MGATE_STILL_HOLD_US  1000000
#define MGATE_MOVE_HOLD_US   300000
#define MGATE_GAP_US         1000000

typedef struct {
    float    rate_hz;
    float    ratio_thr;
    float    a_hp;                    /* high-pass EMA coefficient         */
    float    a_sm;                    /* es smoothing coefficient          */
    float    l1[CSI_NGROUPS];
    float    l2[CSI_NGROUPS];
    bool     primed;                  /* filters seeded                    */
    float    es;
    int64_t  last_t_us;

    /* 1-second means */
    int64_t  sec_start_us;
    float    sec_sum;
    int      sec_n;
    bool     sec_all_empty;           /* every packet this second empty    */

    float    floor_empty;
    bool     have_empty;
    float    slots[MGATE_MIN_SLOTS];
    int      slot_head;
    int      slot_count;

    bool     still;
    int64_t  cand_since_us;           /* start of the opposing condition, 0 = none */
} mgate_t;

/* ratio_thr: es/floor at or above which it counts as motion (Kconfig
 * CSI_VITALS_MOTION_RATIO_X100 / 100). rate_hz <= 0 -> 50.
 * ratio_thr <= 0 or not finite -> 4.0 (only a guard against garbage). */
void mgate_init(mgate_t *g, float rate_hz, float ratio_thr);

/* New packet rate (after calibration or a baseline load): recomputes the
 * coefficients and restarts the filters and decision; the floor is kept
 * only if keep_floor. The threshold is mgate_init's - but a gate never
 * passed to mgate_init (a zeroed static) gets 4.0 here rather than 0,
 * which would call every packet motion. */
void mgate_set_rate(mgate_t *g, float rate_hz, bool keep_floor);

/* Restart filters and decision (state MOVING). Floor kept if keep_floor. */
void mgate_reset(mgate_t *g, bool keep_floor);

/* Once per accepted packet in the detect phase. Returns true while STILL. */
bool mgate_push(mgate_t *g, int64_t t_us, const float grp[CSI_NGROUPS],
                bool room_empty);

bool  mgate_still(const mgate_t *g);

/* es / floor, or -1.0f while the floor is unknown (and before the first
 * packet after a restart, when es means nothing yet) - for JSON / tuning.
 * A floor that is 0 (all-zero input) is clamped to 1e-9 only so that this
 * stays finite. Non-finite grp input is skipped by mgate_push entirely. */
float mgate_ratio(const mgate_t *g);
