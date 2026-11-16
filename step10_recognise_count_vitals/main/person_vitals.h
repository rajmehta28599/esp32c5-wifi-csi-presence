/*
 * person_vitals.h - whose breathing / heart rate is this? Ties Step 10's
 *                   vitals to the person recognised walking in, and keeps
 *                   a small per-person vitals log in NVS.
 *
 * WHAT "vitals_of" MEANS - AND WHAT IT CANNOT MEAN
 *   Recognition (gait_id) needs a person WALKING; vitals need a person
 *   SITTING STILL. The two never happen at the same moment, so the name
 *   attached to a breathing rate is a carried-over claim: "the last person
 *   recognised walking in ALONE during this visit, who has not been
 *   contradicted since". It is not a measurement of whose chest this is.
 *   One 1x1 link cannot separate two seated people's breathing, and the
 *   counter only sees MOVERS, so:
 *     - two people seen moving together (count >= 2), or two different
 *       names in one visit -> MULTIPLE: no name and no rate, until the room
 *       has been empty (sticky on purpose - nobody can tell who stayed);
 *     - someone who walks in and sits down before they have been
 *       recognised is invisible to this, and their breathing mixes into
 *       the latched person's reading. That residual cannot be detected.
 *
 * THE LATCH (an "identity episode" runs from one release to the next and
 * deliberately spans a presence lapse that the breathing hold undoes):
 *   NONE      no decisive tally yet                  vitals_of = null
 *   UNKNOWN   latest decisive tally was "unknown"    vitals_of = null
 *   PERSON(s) latest decisive tally is slot s, the
 *             first slot named this episode          vitals_of = name
 *   MULTIPLE  count >= 2, or a second name           vitals_of = null,
 *             (sticky until release)                 rates withheld
 *   pv_on_label(slot s): NONE/UNKNOWN -> PERSON(s) if s is the first name of
 *     the episode (or equals it); any OTHER slot -> MULTIPLE.
 *   pv_on_label(UNKNOWN): PERSON -> UNKNOWN (the first name is remembered,
 *     so A -> unknown -> A recovers, but A -> unknown -> B is MULTIPLE);
 *     NONE -> UNKNOWN.
 *   pv_on_count(>= 2): -> MULTIPLE.
 *   Release (-> NONE, first name forgotten): pv_release() (no-CSI,
 *     recalibration, new baseline, session start), presence ending while
 *     the breathing hold is compiled out, the motion gate reporting
 *     movement while the room is absent (a new still spell can no longer be
 *     held, so whoever comes next is a new visit), and absence longer than
 *     cfg.carry_us (bounds how long a false hold can name someone who left).
 *
 * THE LOG (per enrolled slot, NVS key "vit_log", ~170 bytes)
 *   Credited once per COMPLETED vitals analysis (~every 2 s), never per
 *   packet, and only while present, count <= 1, latch PERSON and vitals
 *   state OK with a finite breathing rate in (0, PV_VAL_MAX]. Heart rate is
 *   credited only when also valid. Analyses overlap heavily (30 s window,
 *   2 s hop) and hr is already a rolling median, so they are not
 *   independent: the samples of one still EPISODE are averaged into one
 *   value, and that value is folded into the running mean. An episode is
 *   committed only with >= PV_EP_MIN samples (breathing and heart rate
 *   counted separately). The mean becomes an EWMA after PV_MEAN_CAP
 *   episodes, so it follows slow drift. An episode ends when the gate says
 *   moving, when the vitals buffer restarts (still_s drops), or when the
 *   latch changes or is released. Heart-rate fields keep the _exp suffix:
 *   a mean of an unvalidated number is still unvalidated.
 *   "last seen" is uptime in RAM only - there is no wall clock.
 *
 * ESP-IDF free (main.c does the NVS I/O and the saving); host-tested in
 * test/test_pvitals.c. Cost: integer compares per packet; ~10 float
 * operations per completed analysis. RAM: 304 bytes (pv_t, from the firmware's linker map), declare it static.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "csi_ncc.h"                 /* NCC_MAX_CLASSES, NCC_NAME_LEN     */
#include "vitals.h"                  /* vitals_out_t, VIT_OK              */

#define PV_SLOTS         NCC_MAX_CLASSES
#define PV_NAME_LEN      NCC_NAME_LEN

#define PV_LOG_MAGIC     0x314C5650u /* "PVL1" little-endian               */
#define PV_LOG_VERSION   1

#define PV_LABEL_UNKNOWN (-1)        /* == main.c ID_UNKNOWN              */
#define PV_EP_MIN        3           /* samples to commit an episode      */
#define PV_MEAN_CAP      32          /* episodes; EWMA beyond             */
#define PV_VAL_MAX       200.0f      /* sanity bound, per minute          */

typedef enum {
    PV_NONE = 0,
    PV_UNKNOWN,
    PV_PERSON,
    PV_MULTIPLE,
} pv_attr_t;

/* One slot's log record - part of the NVS format, 40 bytes, no padding. */
typedef struct {
    char     name[PV_NAME_LEN];      /* owner; "" = empty record          */
    uint16_t br_eps;                 /* episodes folded into br_mean      */
    uint16_t hr_eps;                 /* episodes folded into hr_mean_exp;
                                        always <= br_eps                  */
    uint32_t samples;                /* analyses behind the committed
                                        breathing episodes                */
    float    br_last, br_mean;       /* breaths/min, valid iff br_eps > 0 */
    float    hr_last_exp;            /* EXPERIMENTAL beats/min,           */
    float    hr_mean_exp;            /*   valid iff hr_eps > 0            */
} pv_rec_t;

/* The NVS blob. */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t nslots;                 /* == PV_SLOTS                       */
    pv_rec_t rec[PV_SLOTS];
} pv_log_t;

typedef struct {
    int64_t carry_us;                /* absence longer than this releases
                                        the latch (main: 2 x vitals window) */
    bool    holds_presence;          /* CONFIG_CSI_VITALS_HOLDS_PRESENCE:
                                        false -> presence end releases     */
} pv_cfg_t;

typedef struct {
    pv_cfg_t  cfg;
    pv_log_t  log;                   /* the persisted image                */

    /* latch */
    pv_attr_t attr;
    int8_t    slot;                  /* PV_PERSON: its slot, else -1      */
    int8_t    first_slot;            /* first slot named this episode, -1 */
    char      name[PV_NAME_LEN];     /* name of first_slot ("" if none)   */

    /* carried across a presence lapse */
    bool      absent;                /* presence ended, latch still held  */
    int64_t   absent_since_us;

    /* the pending (uncommitted) still episode */
    int8_t    acc_slot;              /* -1 = nothing pending              */
    char      acc_name[PV_NAME_LEN];
    uint16_t  acc_br_n, acc_hr_n;
    float     acc_br_sum, acc_hr_sum;
    float     prev_still_s;          /* detects a vitals buffer restart   */

    /* persistence bookkeeping (RAM only) */
    int64_t   last_seen_us[PV_SLOTS];/* uptime of the last credit, 0 = not
                                        since boot                         */
    bool      dirty;                 /* log changed since the last save   */
    int64_t   last_save_us;          /* last save ATTEMPT                 */
} pv_t;

/* ---- setup and persistence ---------------------------------------- */

/* Empty log, latch NONE, nothing pending. */
void pv_init(pv_t *p, const pv_cfg_t *cfg);

/* An empty, valid log image. */
void pv_log_init(pv_log_t *l);

/* Is a blob read from NVS usable? sz is the size NVS reported. Checks the
 * size, magic, version, nslots; every name NUL-terminated within
 * PV_NAME_LEN and [A-Za-z0-9_-]* ; an empty name has all-zero counts;
 * hr_eps <= br_eps; every float finite and in [0, PV_VAL_MAX], and non-zero
 * where its count says it is valid. Any failure -> false (main.c then
 * starts from an empty log, never a partly trusted one). */
bool pv_log_valid(const pv_log_t *l, size_t sz);

/* Adopt a loaded log. Every record whose name differs from names[s] (the
 * recognition model's slot names; "" or NULL = unused slot) is cleared.
 * Returns the number of records cleared (dirty is set if > 0). */
int pv_adopt_log(pv_t *p, const pv_log_t *l, const char *const names[PV_SLOTS]);

/* Should main.c save now? dirty and at least min_gap_us since the last
 * attempt (counted from boot for the first one). */
bool pv_save_due(const pv_t *p, int64_t now_us, int64_t min_gap_us);

/* After a save attempt: always stamps last_save_us (so a failing flash
 * backs off instead of retrying every packet); clears dirty only if ok. */
void pv_saved(pv_t *p, int64_t now_us, bool ok);

/* ---- events, all from the pipeline task ---------------------------- */

/* Hard boundary: commit the pending episode, latch NONE, first name
 * forgotten, absence carry dropped. */
void pv_release(pv_t *p);

/* Recognition produced a DECISIVE tally (a slot >= 0, or PV_LABEL_UNKNOWN
 * once enough votes exist) while count <= 1. name = that slot's name
 * (ignored for PV_LABEL_UNKNOWN). Idempotent; call after every vote.
 * Returns true if attr or slot changed. */
bool pv_on_label(pv_t *p, int label, const char *name);

/* The counter's estimate changed. >= 2 -> MULTIPLE (commits first).
 * Returns true if attr changed. */
bool pv_on_count(pv_t *p, int count);

/* Presence ended (any cause) / the lapse is over. An end keeps the latch
 * when holds_presence (the breathing hold may restore it), else releases.
 * main.c calls pv_presence_start when the breathing hold asserts PRESENT,
 * not at Step 08's own start: on a walk-in Step 08 fires 0.4-3.5 s before
 * the gate (test_e2e), so the lapse must last until then for pv_packet to
 * see the newcomer's movement "while absent". */
void pv_presence_end(pv_t *p, int64_t now_us);
void pv_presence_start(pv_t *p);

/* Every packet in PH_DETECT, with the motion gate's decision, BEFORE
 * main.c's presence transition for this packet (movement on the packet
 * that ends a lapse still counts as movement while absent). !still ends
 * the pending episode; while absent, !still or an absence longer than
 * cfg.carry_us releases. Integer work only. */
void pv_packet(pv_t *p, int64_t now_us, bool still);

/* A vitals analysis just completed (vitals_push returned true). present
 * and count are AFTER this packet's transition. Credits the pending
 * episode per the rules above. Returns true if a sample was credited. */
bool pv_analysis(pv_t *p, int64_t now_us, bool present, int count,
                 const vitals_out_t *vo);

/* Slot s's template was deleted or replaced (forget, enroll start, enroll
 * cancelled); s = -1 for all. Discards (never commits) a pending episode
 * for it, clears its record, and releases the latch if it points there
 * (or forgets it as the episode's first name). Sets dirty. */
void pv_slot_gone(pv_t *p, int slot);

/* Clear slot s's log record only (-1 = all): the 'vitals-reset' command.
 * Discards a pending episode for it; the latch and the templates are
 * kept. Sets dirty. */
void pv_log_clear(pv_t *p, int slot);

/* ---- outputs -------------------------------------------------------- */

pv_attr_t pv_attr(const pv_t *p, int *slot);   /* slot may be NULL     */

/* True when rates must be withheld because of this visit's latch. */
bool pv_multiple(const pv_t *p);

/* The name to report as "vitals_of", or NULL: present, count <= 1 and
 * latch PERSON. */
const char *pv_vitals_of(const pv_t *p, bool present, int count);

/* One human-readable line describing slot s's record, for 'people', e.g.
 *   "vitals: 3 episodes, breathing 14.8/min (mean 15.2, 41 samples), heart ~71/min (mean ~73, 2 episodes) EXPERIMENTAL, last 412 s ago"
 *   "vitals: none logged"
 * "last ... ago" becomes "not seen since boot" when last_seen_us is 0.
 * Returns snprintf's result. */
int pv_format_rec(char *buf, size_t len, const pv_t *p, int slot, int64_t now_us);
