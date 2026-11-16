/*
 * motion_gate.c - see motion_gate.h for what the gate decides and why
 *                 Step 08's motion flag could not be used for it.
 *
 * Soft-float budget (the C5 has no FPU): per packet ~6 operations per group
 * for the two high-passes and the energy, plus a handful for es, the
 * 1-second sum and the ratio - about 60 in all. expf appears only where a
 * coefficient is (re)computed: at init / set_rate, and once per completed
 * 1-second bin for the floor EMA - never per packet.
 */

#include <math.h>
#include <float.h>
#include <string.h>

#include "motion_gate.h"

#define MGATE_DEFAULT_RATE   50.0f
#define MGATE_DEFAULT_RATIO  4.0f
#define MGATE_BIN_US         1000000

/* Only to keep es / floor finite: an all-zero input (no groups at all)
 * would otherwise divide by zero. With any real noise the floor is many
 * orders of magnitude above this, so it never changes a decision. */
#define MGATE_FLOOR_MIN      1e-9f

/* Portable finiteness test, the same idiom as csi_ncc.c / gait_id.c:
 * false for NaN and +/-inf, and it compiles warning-free on MSVC /W4 and
 * GCC -Wall without isfinite()'s double promotion. */
static bool mg_finite(float v)
{
    return (v == v) && fabsf(v) <= FLT_MAX;
}

static float mg_coef(float rate_hz, float tau_s)
{
    return 1.0f - expf(-1.0f / (rate_hz * tau_s));
}

/* The floor, or a negative value while it is unknown.
 * floor_empty wins as soon as one empty-room second has been seen: it was
 * measured with nobody there, so it cannot contain anybody's motion. The
 * minimum over the last MGATE_MIN_SLOTS seconds is the fallback for a room
 * never seen empty since the floor was last cleared - "the quietest recent
 * second is probably just noise". Its known weakness (motion_gate.h): if
 * every second in the ring had motion in it, the motion becomes the floor
 * and the gate calls it still. One quiet second, or one empty-room second,
 * repairs it. The scan costs MGATE_MIN_SLOTS compares per packet, and only
 * in that fallback case. */
static float mg_floor(const mgate_t *g)
{
    if (g->have_empty) {
        return g->floor_empty;
    }
    if (g->slot_count < MGATE_MIN_VALID) {
        return -1.0f;
    }
    float m = g->slots[0];
    for (int i = 1; i < g->slot_count; i++) {
        if (g->slots[i] < m) m = g->slots[i];
    }
    return m;
}

/* A 1-second interval is complete: feed it to the floor estimators. */
static void mg_close_bin(mgate_t *g)
{
    if (g->sec_n <= 0) return;
    float mean = g->sec_sum / (float)g->sec_n;

    if (g->sec_all_empty) {
        if (!g->have_empty) {
            g->floor_empty = mean;         /* the first empty second seeds it */
            g->have_empty  = true;
        } else {
            /* EMA over 1-second steps. expf once per second is negligible;
             * the header has no field to cache the constant in. */
            float k = 1.0f - expf(-1.0f / MGATE_FLOOR_TAU_S);
            g->floor_empty += k * (mean - g->floor_empty);
        }
    }

    /* Every second goes into the minimum ring, empty room or not: it is
     * the fallback for rooms that are never seen empty. */
    g->slots[g->slot_head] = mean;
    g->slot_head = (g->slot_head + 1) % MGATE_MIN_SLOTS;
    if (g->slot_count < MGATE_MIN_SLOTS) g->slot_count++;
}

void mgate_init(mgate_t *g, float rate_hz, float ratio_thr)
{
    memset(g, 0, sizeof(*g));
    /* The Kconfig range keeps ratio_thr in [1.5, 50]; this only guards a
     * caller passing garbage, so the gate cannot be left with a threshold
     * no ratio ever reaches (NaN) or that every ratio reaches (<= 0). */
    g->ratio_thr = (ratio_thr > 0.0f && ratio_thr <= FLT_MAX) ? ratio_thr : MGATE_DEFAULT_RATIO;
    mgate_set_rate(g, rate_hz, false);
}

void mgate_set_rate(mgate_t *g, float rate_hz, bool keep_floor)
{
    /* A gate that only ever saw set_rate - a zeroed static nobody passed
     * to mgate_init - would have ratio_thr 0: every packet "moving", the
     * gate never STILL, vitals never run, and nothing would say why. */
    if (!(g->ratio_thr > 0.0f && g->ratio_thr <= FLT_MAX)) g->ratio_thr = MGATE_DEFAULT_RATIO;
    g->rate_hz = (rate_hz > 0.0f && rate_hz <= FLT_MAX) ? rate_hz : MGATE_DEFAULT_RATE;
    g->a_hp    = mg_coef(g->rate_hz, MGATE_TAU_S);
    g->a_sm    = mg_coef(g->rate_hz, MGATE_SMOOTH_S);
    mgate_reset(g, keep_floor);
}

void mgate_reset(mgate_t *g, bool keep_floor)
{
    for (int i = 0; i < CSI_NGROUPS; i++) {
        g->l1[i] = 0.0f;
        g->l2[i] = 0.0f;
    }
    g->primed        = false;
    g->es            = 0.0f;
    g->last_t_us     = 0;
    /* A second that straddles the restart is not a clean measurement of
     * anything, so the partial one is dropped even when the floor is kept. */
    g->sec_start_us  = 0;
    g->sec_sum       = 0.0f;
    g->sec_n         = 0;
    g->sec_all_empty = true;
    g->still         = false;         /* MOVING until shown otherwise       */
    g->cand_since_us = 0;

    if (!keep_floor) {
        g->floor_empty = 0.0f;
        g->have_empty  = false;
        for (int i = 0; i < MGATE_MIN_SLOTS; i++) g->slots[i] = 0.0f;
        g->slot_head   = 0;
        g->slot_count  = 0;
    }
}

bool mgate_push(mgate_t *g, int64_t t_us, const float grp[CSI_NGROUPS],
                bool room_empty)
{
    /* A NaN would poison the filter states, es and the floor for good.
     * The packet is skipped entirely - last_t_us included, so a long run
     * of bad packets is seen as the gap it effectively is. */
    for (int i = 0; i < CSI_NGROUPS; i++) {
        if (!mg_finite(grp[i])) return g->still;
    }

    /* Time running backwards (a caller bug, or a clock reset) or a gap
     * longer than MGATE_GAP_US: the filter states describe a signal that
     * no longer continues, so start over - but the floor describes the
     * room, which a short outage does not change. */
    if (g->primed && (t_us < g->last_t_us || t_us - g->last_t_us > MGATE_GAP_US)) {
        mgate_reset(g, true);
    }

    float e = 0.0f;
    if (!g->primed) {
        /* Seed l1 at the signal so d1 starts at 0 instead of stepping by
         * the whole current offset; l2 tracks d1, which is then 0. */
        for (int i = 0; i < CSI_NGROUPS; i++) {
            g->l1[i] = grp[i];
            g->l2[i] = 0.0f;
        }
        /* e is 0 for this packet by construction */
        g->es            = 0.0f;
        g->primed        = true;
        g->sec_start_us  = t_us;
        g->sec_sum       = 0.0f;
        g->sec_n         = 0;
        g->sec_all_empty = true;
    } else {
        float a = g->a_hp;
        for (int i = 0; i < CSI_NGROUPS; i++) {
            g->l1[i] += a * (grp[i] - g->l1[i]);
            float d1 = grp[i] - g->l1[i];
            g->l2[i] += a * (d1 - g->l2[i]);
            float d2 = d1 - g->l2[i];
            e += d2 * d2;
        }
        e *= 1.0f / (float)CSI_NGROUPS;
        g->es += g->a_sm * (e - g->es);
    }
    g->last_t_us = t_us;

    /* 1-second means. The packet that crosses the boundary closes the old
     * interval and opens the next one at its own time: gaps longer than
     * MGATE_GAP_US were handled above, so there is never more than one
     * boundary to catch up on. */
    if (t_us - g->sec_start_us >= MGATE_BIN_US) {
        mg_close_bin(g);
        g->sec_start_us  = t_us;
        g->sec_sum       = 0.0f;
        g->sec_n         = 0;
        g->sec_all_empty = true;
    }
    g->sec_sum += e;
    g->sec_n++;
    if (!room_empty) g->sec_all_empty = false;

    /* Decision. The hold timers run on packet time, so a dropped packet
     * does not stretch them. cand_since_us 0 means "no candidate"; the
     * (practically impossible) t_us == 0 is stored as 1 us. */
    float floor = mg_floor(g);
    if (floor < 0.0f) {
        g->still         = false;         /* unknown floor: vitals just wait */
        g->cand_since_us = 0;
        return false;
    }
    if (floor < MGATE_FLOOR_MIN) floor = MGATE_FLOOR_MIN;

    bool moving_now = (g->es >= g->ratio_thr * floor);   /* ratio >= thr */
    bool opposing   = g->still ? moving_now : !moving_now;
    if (!opposing) {
        g->cand_since_us = 0;
    } else {
        if (g->cand_since_us == 0) g->cand_since_us = (t_us != 0) ? t_us : 1;
        int64_t hold = g->still ? (int64_t)MGATE_MOVE_HOLD_US : (int64_t)MGATE_STILL_HOLD_US;
        if (t_us - g->cand_since_us >= hold) {
            g->still         = !g->still;
            g->cand_since_us = 0;
        }
    }
    return g->still;
}

bool mgate_still(const mgate_t *g)
{
    return g->still;
}

float mgate_ratio(const mgate_t *g)
{
    float floor = mg_floor(g);
    if (floor < 0.0f || !g->primed) return -1.0f;
    if (floor < MGATE_FLOOR_MIN) floor = MGATE_FLOOR_MIN;
    return g->es / floor;
}
