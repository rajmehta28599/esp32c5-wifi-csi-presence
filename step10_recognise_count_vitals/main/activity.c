/*
 * activity.c - see activity.h for the states, the episode rules and what
 *              none of it can tell.
 *
 * Soft-float budget (the C5 has no FPU): per packet one float compare and
 * one float add (the hf sum); per completed second one division and two
 * compares. Everything else is integer.
 */

#include <string.h>

#include "activity.h"

#define ACT_SEC_US   1000000

/* ring byte */
#define ACT_R_KNOWN    0x01u
#define ACT_R_PRESENT  0x02u
#define ACT_R_ACTIVE   0x04u          /* level ACTIVE                       */
#define ACT_R_FIDGET   0x08u          /* level FIDGET                       */

/* raw_state only: PRESENT, but too few known seconds to say how. Unlike
 * ACT_NONE (a lapse: keep the state) it clears the state. */
#define ACT_RAW_UNSURE ((act_state_t)ACT_NSTATES)

static void bin_clear(act_t *a)
{
    a->n_pkt       = 0;
    a->n_known     = 0;
    a->n_moving    = 0;
    a->hf_sum      = 0.0f;
    a->any_present = false;
    a->any_held    = false;
    a->any_moved   = false;
}

/* The state machine and the ring, not the episode. */
static void classify_restart(act_t *a)
{
    a->started   = false;
    bin_clear(a);
    memset(a->ring, 0, sizeof(a->ring));
    a->ring_head  = 0;
    a->ring_n     = 0;
    a->absent_run = 0;
    a->state      = ACT_NONE;
    a->cand       = ACT_NONE;
    a->cand_n     = 0;
    a->state_s    = 0;
}

void act_init(act_t *a, const act_cfg_t *cfg)
{
    memset(a, 0, sizeof(*a));
    a->cfg = *cfg;
    classify_restart(a);
}

static void ep_add(act_ep_t *e, act_state_t label)
{
    e->total_s++;
    e->st_s[label]++;
}

static void ep_fold(act_ep_t *into, const act_ep_t *from)
{
    into->total_s += from->total_s;
    for (int i = 0; i < ACT_NSTATES; i++) into->st_s[i] += from->st_s[i];
}

/* The open episode ends at its last present second. */
static void ep_close(act_t *a)
{
    a->last = a->cur;
    a->n_episodes++;
    memset(&a->cur, 0, sizeof(a->cur));
    memset(&a->lapse, 0, sizeof(a->lapse));
    memset(&a->wait, 0, sizeof(a->wait));
    a->ep_phase = ACT_EP_NONE;
}

void act_reset(act_t *a)
{
    if (a->ep_phase != ACT_EP_NONE) ep_close(a);
    classify_restart(a);
}

/* Last n ring entries (newest first), counted by mask. */
static int ring_count(const act_t *a, int n, uint8_t mask)
{
    int c = 0;
    if (n > a->ring_n) n = a->ring_n;
    for (int i = 1; i <= n; i++) {
        int j = (a->ring_head - i + ACT_RING) % ACT_RING;
        if (a->ring[j] & mask) c++;
    }
    return c;
}

static act_state_t raw_state(const act_t *a, bool present_now)
{
    if (a->absent_run >= ACT_EMPTY_S) return ACT_EMPTY;
    if (!present_now) return ACT_NONE;             /* a lapse so far: keep the state */
    if (ring_count(a, ACT_WIN_S, ACT_R_ACTIVE) >= ACT_MOVE_MIN_S) return ACT_MOVING;
    /* an ACTIVE second counts only while it is in the "moving" window, so
     * the end of a walk goes to still, not through restless */
    if (ring_count(a, ACT_WIN_S, ACT_R_ACTIVE) + ring_count(a, ACT_QUIET_S, ACT_R_FIDGET) >=
        ACT_FIDGET_MIN_S) {
        return ACT_RESTLESS;
    }
    if (ring_count(a, ACT_QUIET_S, ACT_R_KNOWN) >= ACT_QUIET_S) return ACT_STILL;
    return ACT_RAW_UNSURE;
}

/* The episode bookkeeping for one completed second, with the label the
 * state machine gave it. */
static void ep_second(act_t *a, int64_t sec_start_us, bool present, bool held, bool moved,
                      act_state_t label)
{
    bool timeout = false;
    if (a->ep_phase == ACT_EP_LAPSE || a->ep_phase == ACT_EP_WAIT) {
        timeout = (sec_start_us + ACT_SEC_US - a->lapse_t0_us > a->cfg.carry_us);
    }

    switch (a->ep_phase) {
    case ACT_EP_NONE:
        break;
    case ACT_EP_OPEN:
        if (present) {
            ep_add(&a->cur, label);
            return;
        }
        if (a->cfg.carry_us <= 0 || moved) {   /* moved: not a still person's lapse */
            ep_close(a);
            return;
        }
        memset(&a->lapse, 0, sizeof(a->lapse));
        ep_add(&a->lapse, label);
        a->lapse_t0_us = sec_start_us;
        a->ep_phase    = ACT_EP_LAPSE;
        return;
    case ACT_EP_LAPSE:
        /* The hold first, as main.c's s_carry_wait hands the carry back on
         * the first held packet: a gate-MOVING packet later in the same
         * second does not undo that. */
        if (present && held && !timeout) {  /* the breathing hold undid the lapse */
            ep_fold(&a->cur, &a->lapse);
            memset(&a->lapse, 0, sizeof(a->lapse));
            ep_add(&a->cur, label);
            a->ep_phase = ACT_EP_OPEN;
            return;
        }
        if (moved || timeout) {
            ep_close(a);                    /* and a present second starts anew below */
            break;
        }
        if (!present) {
            ep_add(&a->lapse, label);
            return;
        }
        memset(&a->wait, 0, sizeof(a->wait));
        ep_add(&a->wait, label);
        a->ep_phase = ACT_EP_WAIT;
        return;
    case ACT_EP_WAIT:
        if (held && !timeout) {             /* the hold first, as above */
            ep_fold(&a->cur, &a->lapse);
            ep_fold(&a->cur, &a->wait);
            memset(&a->lapse, 0, sizeof(a->lapse));
            memset(&a->wait, 0, sizeof(a->wait));
            ep_add(&a->cur, label);
            a->ep_phase = ACT_EP_OPEN;
            return;
        }
        if (moved || timeout) {
            /* somebody new (or too late): the old episode ends at its last
             * present second, the new one began with the wait */
            act_ep_t w = a->wait;
            ep_close(a);
            a->cur      = w;
            a->ep_phase = ACT_EP_OPEN;
            if (present) {
                ep_add(&a->cur, label);
            } else if (a->cfg.carry_us <= 0 || moved) {
                ep_close(a);
            } else {
                ep_add(&a->lapse, label);
                a->lapse_t0_us = sec_start_us;
                a->ep_phase    = ACT_EP_LAPSE;
            }
            return;
        }
        if (!present) {                     /* Step 08's blip is over: still the lapse */
            ep_fold(&a->lapse, &a->wait);
            memset(&a->wait, 0, sizeof(a->wait));
            ep_add(&a->lapse, label);
            a->ep_phase = ACT_EP_LAPSE;
            return;
        }
        ep_add(&a->wait, label);
        return;
    }

    /* ACT_EP_NONE, or an episode just ended */
    if (present) {
        memset(&a->cur, 0, sizeof(a->cur));
        ep_add(&a->cur, label);
        a->ep_phase = ACT_EP_OPEN;
    }
}

static void second_done(act_t *a)
{
    uint8_t r = 0;
    if (a->n_known > 0) {
        float hf_mean = a->hf_sum / (float)a->n_known;
        r |= ACT_R_KNOWN;
        if (a->n_moving * ACT_MOVE_FRAC_DIV >= a->n_known || hf_mean >= a->cfg.move_thr) {
            r |= ACT_R_ACTIVE;
        } else if (hf_mean >= a->cfg.restless_lo) {
            r |= ACT_R_FIDGET;
        }
    }
    if (a->any_present) r |= ACT_R_PRESENT;

    a->ring[a->ring_head] = r;
    a->ring_head = (a->ring_head + 1) % ACT_RING;
    if (a->ring_n < ACT_RING) a->ring_n++;
    a->absent_run = a->any_present ? 0 : a->absent_run + 1;
    if (a->absent_run > 1000000u) a->absent_run = 1000000u;

    act_state_t raw = raw_state(a, a->any_present);
    if (raw == ACT_RAW_UNSURE && a->state == ACT_NONE) raw = ACT_NONE;   /* nothing to clear */
    if (raw == ACT_NONE || raw == a->state) {
        a->cand_n = 0;
    } else if (a->state == ACT_NONE) {
        a->state   = raw;
        a->state_s = 0;
        a->cand_n  = 0;
    } else {
        if (raw == a->cand) {
            a->cand_n++;
        } else {
            a->cand   = raw;
            a->cand_n = 1;
        }
        if (a->cand_n >= ACT_HOLD_S) {
            a->state   = (raw == ACT_RAW_UNSURE) ? ACT_NONE : raw;
            a->state_s = 0;
            a->cand_n  = 0;
        }
    }
    if (a->state != ACT_NONE && a->state_s < 0xFFFFFFFFu) a->state_s++;

    ep_second(a, a->sec_start_us, a->any_present, a->any_held, a->any_moved, a->state);
}

void act_push(act_t *a, int64_t t_us, bool present, bool still, float hf, bool held)
{
    if (a->started && (t_us < a->last_t_us || t_us - a->last_t_us > ACT_GAP_US)) {
        classify_restart(a);                /* the episode carries on */
    }
    if (!a->started) {
        a->started      = true;
        a->sec_start_us = t_us;
    }
    a->last_t_us = t_us;

    /* at most two boundaries: the gap test above bounds the step to 1 s */
    while (t_us - a->sec_start_us >= ACT_SEC_US) {
        if (a->n_pkt > 0) second_done(a);   /* a second without packets is skipped */
        bin_clear(a);
        a->sec_start_us += ACT_SEC_US;
    }

    a->n_pkt++;
    /* hf >= 0 is false for NaN too: such a packet is simply not known */
    if (hf >= 0.0f) {
        a->n_known++;
        a->hf_sum += hf;
        if (!still) a->n_moving++;
    }
    if (present) a->any_present = true;
    if (held)    a->any_held    = true;
    if (!still)  a->any_moved   = true;
}

act_state_t act_state(const act_t *a)
{
    return a->state;
}

uint32_t act_state_s(const act_t *a)
{
    return (a->state == ACT_NONE) ? 0u : a->state_s;
}

const char *act_state_name(act_state_t s)
{
    switch (s) {
    case ACT_EMPTY:    return "empty";
    case ACT_STILL:    return "still";
    case ACT_RESTLESS: return "restless";
    case ACT_MOVING:   return "moving";
    default:           return NULL;
    }
}

bool act_episode_s(const act_t *a, uint32_t *s)
{
    if (a->ep_phase == ACT_EP_NONE) return false;
    *s = a->cur.total_s;
    return true;
}
