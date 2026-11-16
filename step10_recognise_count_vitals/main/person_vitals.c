/*
 * person_vitals.c - see person_vitals.h for what "vitals_of" claims (and
 *                   cannot), the latch and the log format.
 *
 * Soft-float budget (the C5 has no FPU): pv_packet() runs on every packet
 * and is integer compares only - in the common case (gate still, presence
 * not lapsed) it returns after two. Float work happens once per completed
 * vitals analysis (~every 2 s: a few compares and two adds in
 * pv_analysis) and once per ENDED still episode (one division per rate for
 * the episode mean, one for the running mean, in pv_commit). No libm call.
 *
 * Choices the header leaves to the implementation, and why:
 *   - The samples of one still episode are averaged into ONE value before
 *     they touch the log. Consecutive analyses share ~28 of their 30 s of
 *     data (and hr is already a rolling median), so folding each one into
 *     the mean would weight a long still spell by its length and claim a
 *     precision nobody measured.
 *   - An episode with fewer than PV_EP_MIN samples is DISCARDED, not
 *     committed: the first analyses of a still spell are the least settled
 *     ones, and a 2-sample "episode" would count as much as an hour.
 *   - A pending episode belongs to (acc_slot, acc_name), fixed when its
 *     first sample is credited. At commit, a record whose stored name
 *     differs from acc_name belongs to someone else (the slot was
 *     re-enrolled): it is re-initialised, never merged into.
 *   - Every change of the latch (attr or slot) commits the pending episode
 *     first; pv_slot_gone / pv_log_clear DISCARD it instead (the person's
 *     data is being deleted - committing it first would resurrect it).
 *   - Range tests are written so that NaN fails them: every comparison
 *     with NaN is false, so "v > 0 && v <= PV_VAL_MAX" rejects NaN and both
 *     infinities without an isfinite() call (which promotes to double).
 *   - The log this module writes always passes pv_log_valid(): values are
 *     bounded on the way in, the running mean is clamped between the old
 *     mean and the new value, and counters saturate. Otherwise one bad
 *     number would make main.c discard the WHOLE log at the next boot.
 */

#include "person_vitals.h"

#include <float.h>
#include <stdio.h>
#include <string.h>

/* The log is written to NVS byte for byte: its layout is the format. */
_Static_assert(sizeof(pv_rec_t) == 40, "pv_rec_t is part of the NVS format: 40 bytes, no padding");
_Static_assert(sizeof(pv_log_t) == 8 + PV_SLOTS * sizeof(pv_rec_t),
               "pv_log_t is the NVS blob: no padding");

/* pv_init(p, NULL): what main.c passes with the Kconfig defaults
 * (2 x CONFIG_CSI_VITALS_WINDOW_S 30 s, CONFIG_CSI_VITALS_HOLDS_PRESENCE y). */
#define PV_DEFAULT_CARRY_US  60000000LL

/* ------------------------------------------------------------------ */
/* names                                                               */
/* ------------------------------------------------------------------ */

/* main.c's name_valid() alphabet: what makes a name safe to print inside a
 * JSON string or a log line with no escaping. */
static bool pv_name_char_ok(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-';
}

/* A name the latch may hold: 1..PV_NAME_LEN-1 characters of that
 * alphabet. Too long is rejected rather than truncated - truncation could
 * make two different people the same name. */
static bool pv_name_ok(const char *s)
{
    if (!s || s[0] == '\0') return false;
    for (int i = 0; i < PV_NAME_LEN; i++) {
        if (s[i] == '\0') return true;
        if (!pv_name_char_ok((unsigned char)s[i])) return false;
    }
    return false;                      /* no terminator within PV_NAME_LEN */
}

/* Same name? NULL reads as "" (an unused slot). Both sides are terminated
 * within PV_NAME_LEN wherever this is used, so the bound only guards. */
static bool pv_name_eq(const char *a, const char *b)
{
    return strncmp(a ? a : "", b ? b : "", PV_NAME_LEN) == 0;
}

/* Copy with zero fill (as ncc_set_name): a shorter name written over a
 * longer one must leave no stale bytes in the NVS blob. */
static void pv_name_copy(char dst[PV_NAME_LEN], const char *src)
{
    int i = 0;
    if (src) {
        for (; i < PV_NAME_LEN - 1 && src[i] != '\0'; i++) dst[i] = src[i];
    }
    for (; i < PV_NAME_LEN; i++) dst[i] = '\0';
}

/* ------------------------------------------------------------------ */
/* numbers                                                             */
/* ------------------------------------------------------------------ */

/* A rate worth crediting or storing as "valid": (0, PV_VAL_MAX]. False for
 * NaN and both infinities (see the top of the file). */
static bool pv_rate_ok(float v)
{
    return v > 0.0f && v <= PV_VAL_MAX;
}

static uint16_t pv_inc16(uint16_t v)
{
    return (v < UINT16_MAX) ? (uint16_t)(v + 1u) : v;
}

/* Fold one episode value x into a mean over eps_before earlier episodes.
 * One formula for both regimes: k = min(eps_before + 1, PV_MEAN_CAP), so
 * up to PV_MEAN_CAP episodes it is the exact running mean and beyond that
 * an EWMA with alpha 1/PV_MEAN_CAP (a time constant of ~32 episodes) that
 * follows slow drift instead of freezing after a few hundred sittings.
 * The first episode sets the mean outright, so whatever an unused
 * record's float held (pv_log_valid only bounds it) is never averaged in. */
static float pv_fold(float mean, uint16_t eps_before, float x)
{
    if (eps_before == 0) return x;
    int   k = (eps_before >= PV_MEAN_CAP) ? PV_MEAN_CAP : (int)eps_before + 1;
    float m = mean + (x - mean) / (float)k;
    /* A mean of two values lies between them. Rounding can only step
     * outside by an ulp, but an ulp past PV_VAL_MAX would fail
     * pv_log_valid at the next boot and lose everybody's log. */
    float lo = (mean < x) ? mean : x;
    float hi = (mean < x) ? x : mean;
    if (m < lo) m = lo;
    if (m > hi) m = hi;
    return m;
}

/* ------------------------------------------------------------------ */
/* the pending episode                                                 */
/* ------------------------------------------------------------------ */

static void pv_pending_drop(pv_t *p)
{
    p->acc_slot   = -1;
    memset(p->acc_name, 0, sizeof(p->acc_name));
    p->acc_br_n   = 0;
    p->acc_hr_n   = 0;
    p->acc_br_sum = 0.0f;
    p->acc_hr_sum = 0.0f;
}

/* End the pending episode: fold it into its owner's record if it has
 * enough samples, drop it either way. Returns at once when nothing is
 * pending - pv_packet calls this on every moving packet. */
static void pv_commit(pv_t *p)
{
    if (p->acc_slot < 0) return;

    int s = p->acc_slot;
    if (s < PV_SLOTS && p->acc_br_n >= PV_EP_MIN) {
        /* Every sample was checked into (0, PV_VAL_MAX] on the way in, and
         * the float sum of values <= 200 divided by their count cannot
         * round above 200 (200 * n is exact) - the test below only guards
         * against a pv_t someone poked by hand. */
        float br = p->acc_br_sum / (float)p->acc_br_n;
        if (pv_rate_ok(br)) {
            pv_rec_t *r = &p->log.rec[s];
            if (!pv_name_eq(r->name, p->acc_name)) {
                /* "" (never logged, or cleared) or somebody else's record:
                 * start it afresh under this name. */
                memset(r, 0, sizeof(*r));
                pv_name_copy(r->name, p->acc_name);
            }
            r->br_mean = pv_fold(r->br_mean, r->br_eps, br);
            r->br_last = br;
            r->br_eps  = pv_inc16(r->br_eps);
            r->samples = (UINT32_MAX - r->samples < (uint32_t)p->acc_br_n)
                             ? UINT32_MAX : r->samples + p->acc_br_n;

            /* Heart rate only inside a committed breathing episode: it is
             * credited only alongside breathing (acc_hr_n <= acc_br_n), and
             * this keeps hr_eps <= br_eps, which pv_log_valid demands. */
            if (p->acc_hr_n >= PV_EP_MIN) {
                float hr = p->acc_hr_sum / (float)p->acc_hr_n;
                if (pv_rate_ok(hr)) {
                    r->hr_mean_exp = pv_fold(r->hr_mean_exp, r->hr_eps, hr);
                    r->hr_last_exp = hr;
                    r->hr_eps      = pv_inc16(r->hr_eps);
                }
            }
            p->dirty = true;
        }
    }
    pv_pending_drop(p);
}

/* Set the latch; any change of attr or slot ends the still episode first
 * (its samples belong to the latch they were credited under). */
static bool pv_set_latch(pv_t *p, pv_attr_t a, int slot)
{
    if (a == p->attr && slot == p->slot) return false;
    pv_commit(p);
    p->attr = a;
    p->slot = (int8_t)slot;
    return true;
}

static void pv_forget_first(pv_t *p)
{
    p->first_slot = -1;
    memset(p->name, 0, sizeof(p->name));
}

/* Slot s's data is being deleted: discard (never commit) its pending
 * episode, zero its record and its "last seen". */
static void pv_forget_rec(pv_t *p, int s)
{
    if (p->acc_slot == s) pv_pending_drop(p);
    memset(&p->log.rec[s], 0, sizeof(p->log.rec[s]));
    p->last_seen_us[s] = 0;
}

/* ------------------------------------------------------------------ */
/* setup and persistence                                               */
/* ------------------------------------------------------------------ */

void pv_log_init(pv_log_t *l)
{
    if (!l) return;
    /* all-zero records, padding-free (asserted above), so two identical
     * logs are identical blobs */
    memset(l, 0, sizeof(*l));
    l->magic   = PV_LOG_MAGIC;
    l->version = PV_LOG_VERSION;
    l->nslots  = PV_SLOTS;
}

void pv_init(pv_t *p, const pv_cfg_t *cfg)
{
    if (!p) return;
    memset(p, 0, sizeof(*p));
    if (cfg) {
        p->cfg = *cfg;
    } else {
        p->cfg.carry_us       = PV_DEFAULT_CARRY_US;
        p->cfg.holds_presence = true;
    }
    pv_log_init(&p->log);
    p->attr       = PV_NONE;
    p->slot       = -1;
    p->first_slot = -1;
    p->acc_slot   = -1;
    /* last_save_us 0 = boot: the first save waits a full min_gap, so a
     * boot loop cannot turn into a flash-write loop. */
}

bool pv_log_valid(const pv_log_t *l, size_t sz)
{
    if (!l || sz != sizeof(pv_log_t)) return false;
    if (l->magic != PV_LOG_MAGIC || l->version != PV_LOG_VERSION || l->nslots != PV_SLOTS) {
        return false;
    }
    for (int s = 0; s < PV_SLOTS; s++) {
        const pv_rec_t *r = &l->rec[s];
        /* 'people' prints the name with %s and so may the JSON: it must be
         * terminated inside the field and need no escaping. */
        if (memchr(r->name, '\0', PV_NAME_LEN) == NULL) return false;
        for (const char *c = r->name; *c != '\0'; c++) {
            if (!pv_name_char_ok((unsigned char)*c)) return false;
        }
        /* numbers nobody owns cannot be attributed to anybody */
        if (r->name[0] == '\0' && (r->br_eps != 0 || r->hr_eps != 0 || r->samples != 0)) {
            return false;
        }
        if (r->hr_eps > r->br_eps) return false;
        /* [0, PV_VAL_MAX], written so NaN and +/-inf fail */
        if (!(r->br_last >= 0.0f && r->br_last <= PV_VAL_MAX) ||
            !(r->br_mean >= 0.0f && r->br_mean <= PV_VAL_MAX) ||
            !(r->hr_last_exp >= 0.0f && r->hr_last_exp <= PV_VAL_MAX) ||
            !(r->hr_mean_exp >= 0.0f && r->hr_mean_exp <= PV_VAL_MAX)) {
            return false;
        }
        /* a counted rate of 0/min was never credited (credits need > 0) */
        if (r->br_eps > 0 && !(r->br_last > 0.0f && r->br_mean > 0.0f)) return false;
        if (r->hr_eps > 0 && !(r->hr_last_exp > 0.0f && r->hr_mean_exp > 0.0f)) return false;
    }
    return true;
}

int pv_adopt_log(pv_t *p, const pv_log_t *l, const char *const names[PV_SLOTS])
{
    /* main.c checks pv_log_valid first; checking again costs nothing at
     * boot and keeps "never a partly trusted log" true for any caller. An
     * unusable image is not adopted, and not marked dirty either: whatever
     * sits in NVS stays there until a real change is saved. */
    if (!p || !l || !pv_log_valid(l, sizeof(*l))) return 0;

    p->log = *l;
    int cleared = 0;
    for (int s = 0; s < PV_SLOTS; s++) {
        pv_rec_t   *r    = &p->log.rec[s];
        const char *want = names ? names[s] : NULL;
        if (pv_name_eq(r->name, want)) continue;
        /* The recognition model no longer holds this person in this slot
         * (forgotten, re-enrolled, or the model failed to load): their
         * numbers must not pass to whoever the slot belongs to now. An
         * already-empty record facing a newly enrolled name is not a drop,
         * and is not counted - main.c logs the count as "dropped". */
        if (r->name[0] != '\0') cleared++;
        memset(r, 0, sizeof(*r));
        p->last_seen_us[s] = 0;
    }
    if (cleared > 0) p->dirty = true;
    return cleared;
}

bool pv_save_due(const pv_t *p, int64_t now_us, int64_t min_gap_us)
{
    return p && p->dirty && now_us - p->last_save_us >= min_gap_us;
}

void pv_saved(pv_t *p, int64_t now_us, bool ok)
{
    if (!p) return;
    p->last_save_us = now_us;
    if (ok) p->dirty = false;
}

/* ------------------------------------------------------------------ */
/* events                                                              */
/* ------------------------------------------------------------------ */

void pv_release(pv_t *p)
{
    if (!p) return;
    pv_commit(p);
    p->attr = PV_NONE;
    p->slot = -1;
    pv_forget_first(p);
    p->absent          = false;
    p->absent_since_us = 0;
    /* Nothing is pending now, so no episode can straddle this point and
     * there is no restart to detect across it. */
    p->prev_still_s    = 0.0f;
}

bool pv_on_label(pv_t *p, int label, const char *name)
{
    if (!p) return false;
    /* main.c's ID_NONE (-2) / ID_MULTIPLE (-3) are not decisive tallies,
     * and a slot past the log cannot be anybody's. */
    if (label < PV_LABEL_UNKNOWN || label >= PV_SLOTS) return false;
    if (p->attr == PV_MULTIPLE) return false;   /* sticky until release */

    if (label == PV_LABEL_UNKNOWN) {
        /* PERSON -> UNKNOWN keeps first_slot: A -> unknown -> A recovers,
         * A -> unknown -> B is two names. */
        return pv_set_latch(p, PV_UNKNOWN, -1);
    }

    /* A slot whose name could not be stored or printed reads as UNNAMED
     * (main.c never passes one: names are checked at enroll and at load).
     * It cannot be named, so it latches UNKNOWN - but its slot is still
     * remembered, so a different person after it is still two people. */
    const char *nm   = pv_name_ok(name) ? name : "";
    pv_attr_t   a    = (nm[0] != '\0') ? PV_PERSON : PV_UNKNOWN;
    int         slot = (nm[0] != '\0') ? label : -1;

    if (p->first_slot < 0) {
        p->first_slot = (int8_t)label;
        pv_name_copy(p->name, nm);
        return pv_set_latch(p, a, slot);
    }
    if (p->first_slot == label && pv_name_eq(p->name, nm)) {
        return pv_set_latch(p, a, slot);
    }
    /* A second slot this visit - or the first slot under a different name,
     * which means it was re-enrolled without pv_slot_gone: two people
     * either way, so no name and no rate until the room has been empty. */
    return pv_set_latch(p, PV_MULTIPLE, -1);
}

bool pv_on_count(pv_t *p, int count)
{
    /* count < 2 changes nothing: seated people are never counted (count
     * blocks need movement), so "one mover now" cannot say the second
     * person left. */
    if (!p || count < 2) return false;
    return pv_set_latch(p, PV_MULTIPLE, -1);
}

void pv_presence_end(pv_t *p, int64_t now_us)
{
    if (!p) return;
    if (!p->cfg.holds_presence) {
        /* nothing can restore this presence, so this visit is over */
        pv_release(p);
        return;
    }
    /* The latch AND the pending episode carry on: the breathing hold may
     * restore PRESENT within this same still spell (Step 08's hold is 5 s,
     * a breathing estimate needs ~20 s, so a seated person nearly always
     * lapses first). Nothing is credited while absent (pv_analysis needs
     * present). A second end without a start keeps the first time, so it
     * cannot stretch the carry. */
    if (!p->absent) {
        p->absent          = true;
        p->absent_since_us = now_us;
    }
}

void pv_presence_start(pv_t *p)
{
    if (!p) return;
    p->absent          = false;
    p->absent_since_us = 0;
}

void pv_packet(pv_t *p, int64_t now_us, bool still)
{
    if (!p) return;
    if (still && !p->absent) return;              /* every packet: 2 compares */

    if (!still) {
        /* Movement ends the still episode (vitals_push drops its buffer on
         * the same packet), whoever is in the room. */
        pv_commit(p);
        p->prev_still_s = 0.0f;
        /* Movement while the room is absent: a new still spell can no
         * longer be held by the breathing hold, so whoever is there when
         * presence comes back is a new visit. "Absent" lasts until
         * pv_presence_start, which main.c calls only once the breathing
         * hold asserts PRESENT - a walk-in's Step 08 start comes before
         * the gate sees it move, and must not end the lapse. */
        if (p->absent) pv_release(p);
        return;
    }

    /* still while absent: bound how long a false hold (a fan or curtain
     * passing for breathing after the person left) can name them */
    if (now_us - p->absent_since_us > p->cfg.carry_us) pv_release(p);
}

bool pv_analysis(pv_t *p, int64_t now_us, bool present, int count,
                 const vitals_out_t *vo)
{
    if (!p || !vo) return false;

    /* A vitals buffer restart (still_s fell) begins a new still spell, so
     * it ends the episode even when this analysis is not credited. Strict
     * '<': still_s plateaus at the ring length (51.2 s) in a long spell.
     * A restart that lands on EXACTLY the previous value (both at the
     * first analysis of their buffer) is missed here - but the only
     * restart pv_packet has not already seen is a packet gap, and the
     * motion gate restarts MOVING on any gap over MGATE_GAP_US (1 s),
     * which is shorter than the vitals' own gap limit (~1.1 s), so
     * pv_packet(still=false) has ended the episode first. */
    float ss = vo->still_s;
    if (!(ss >= 0.0f && ss <= FLT_MAX)) ss = 0.0f;
    if (ss < p->prev_still_s) pv_commit(p);
    p->prev_still_s = ss;

    if (!present || count > 1 || p->attr != PV_PERSON) return false;
    if (vo->state != VIT_OK || !vo->br_valid || !pv_rate_ok(vo->br_bpm)) return false;
    int s = p->slot;
    if (s < 0 || s >= PV_SLOTS) return false;

    /* Defensive: the latch changing already committed (pv_set_latch). */
    if (p->acc_slot >= 0 && (p->acc_slot != s || !pv_name_eq(p->acc_name, p->name))) {
        pv_commit(p);
    }
    /* ~36 hours of one unbroken still spell: close it as an episode rather
     * than wrap the counter. */
    if (p->acc_slot >= 0 && p->acc_br_n == UINT16_MAX) pv_commit(p);
    if (p->acc_slot < 0) {
        p->acc_slot = (int8_t)s;
        pv_name_copy(p->acc_name, p->name);
    }

    p->acc_br_sum += vo->br_bpm;
    p->acc_br_n++;
    if (vo->hr_valid && pv_rate_ok(vo->hr_bpm)) {
        p->acc_hr_sum += vo->hr_bpm;
        p->acc_hr_n++;
    }
    p->last_seen_us[s] = (now_us != 0) ? now_us : 1;   /* 0 means "not since boot" */
    return true;
}

void pv_slot_gone(pv_t *p, int slot)
{
    if (!p || slot < -1 || slot >= PV_SLOTS) return;
    for (int s = 0; s < PV_SLOTS; s++) {
        if (slot >= 0 && s != slot) continue;
        /* discard first: pv_release below commits, and this person's
         * pending samples must not be resurrected into a record */
        pv_forget_rec(p, s);
        if (p->attr == PV_PERSON && p->slot == s) {
            pv_release(p);
        } else if (p->first_slot == s) {
            /* UNKNOWN keeps being UNKNOWN, MULTIPLE stays MULTIPLE (two
             * people were still seen); only the name is gone. */
            pv_forget_first(p);
        }
    }
    p->dirty = true;
}

void pv_log_clear(pv_t *p, int slot)
{
    if (!p || slot < -1 || slot >= PV_SLOTS) return;
    for (int s = 0; s < PV_SLOTS; s++) {
        if (slot >= 0 && s != slot) continue;
        pv_forget_rec(p, s);
    }
    p->dirty = true;
}

/* ------------------------------------------------------------------ */
/* outputs                                                             */
/* ------------------------------------------------------------------ */

pv_attr_t pv_attr(const pv_t *p, int *slot)
{
    if (!p) {
        if (slot) *slot = -1;
        return PV_NONE;
    }
    if (slot) *slot = p->slot;
    return p->attr;
}

bool pv_multiple(const pv_t *p)
{
    return p && p->attr == PV_MULTIPLE;
}

const char *pv_vitals_of(const pv_t *p, bool present, int count)
{
    if (!p || !present || count > 1 || p->attr != PV_PERSON || p->name[0] == '\0') {
        return NULL;
    }
    return p->name;
}

/* At most 156 characters (+ NUL) for a valid log:
 *   "vitals: 65535 episodes, breathing 200.0/min (mean 200.0, 4294967295
 *    samples), heart ~200/min (mean ~200, 65535 episodes) EXPERIMENTAL,
 *    last 4294967295 s ago"
 * The heart clause is left out while no heart episode is logged. */
int pv_format_rec(char *buf, size_t len, const pv_t *p, int slot, int64_t now_us)
{
    if (!p || slot < 0 || slot >= PV_SLOTS || p->log.rec[slot].br_eps == 0) {
        return snprintf(buf, len, "vitals: none logged");
    }
    const pv_rec_t *r = &p->log.rec[slot];

    char hr[80] = "";
    if (r->hr_eps > 0) {
        snprintf(hr, sizeof(hr), ", heart ~%.0f/min (mean ~%.0f, %u episode%s) EXPERIMENTAL",
                 r->hr_last_exp, r->hr_mean_exp, (unsigned)r->hr_eps, r->hr_eps == 1 ? "" : "s");
    }

    /* uptime only - there is no wall clock (no SNTP) */
    char seen[40];
    if (p->last_seen_us[slot] == 0) {
        snprintf(seen, sizeof(seen), "not seen since boot");
    } else {
        int64_t d = now_us - p->last_seen_us[slot];
        if (d < 0) d = 0;
        int64_t sec = d / 1000000;
        if (sec > 4294967295LL) sec = 4294967295LL;
        snprintf(seen, sizeof(seen), "last %lu s ago", (unsigned long)sec);
    }

    return snprintf(buf, len, "vitals: %u episode%s, breathing %.1f/min (mean %.1f, %lu sample%s)%s, %s",
                    (unsigned)r->br_eps, r->br_eps == 1 ? "" : "s", r->br_last, r->br_mean,
                    (unsigned long)r->samples, r->samples == 1u ? "" : "s", hr, seen);
}
