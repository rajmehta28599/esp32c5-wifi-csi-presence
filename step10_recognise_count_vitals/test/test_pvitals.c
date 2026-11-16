/*
 * test_pvitals.c - person_vitals.c driven by EVENT SEQUENCES: recognition
 * tallies, count changes, presence lapses, motion-gate decisions and
 * completed vitals analyses, in the order main.c calls them.
 *
 * There is no signal here. Whether the gait classifier recognises anyone,
 * whether the counter sees two movers, whether a breath is found at all -
 * that is test_gait / test_count / test_vitals / test_e2e. This test checks
 * the bookkeeping on top: whose name a rate is reported under, when rates
 * are withheld, and what the per-person log records and persists.
 *
 * (a)-(j) exercise each function against the rules in person_vitals.h.
 * (k) replays every row of the design's edge-case table through a small
 *     mirror of main.c's per-packet order (gate -> pv_packet -> vitals ->
 *     breathing hold -> presence transition -> labels/count -> analysis).
 *     The rows marked "documented residual" are asserted AS THEY ARE, so a
 *     change in that behaviour is noticed and the README updated with it.
 * (l) random event sequences: the latch stays consistent and the log
 *     always passes pv_log_valid (a log that did not would be thrown away
 *     whole at the next boot).
 */

#include <string.h>
#include <stdbool.h>

#include "person_vitals.h"
#include "test_util.h"

#define S_US      1000000LL
#define CARRY_US  (60 * S_US)          /* main.c: 2 x CONFIG_CSI_VITALS_WINDOW_S (30) */
#define T0        (100 * S_US)         /* event clock start - never 0            */
#define FMT_MAX   156                  /* longest pv_format_rec line (person_vitals.c) */

/* The recognition model's slot names in most tests; slot 3 is unused. */
static const char *const k_names[PV_SLOTS] = { "alice", "bob", "carol", "" };

/* NaN and inf from bit patterns, as in test_ncc.c: 0.0f/0.0f or 1e39f
 * would be folded at compile time, which MSVC /WX rejects (C4723/C4756). */
static float f_from_bits(uint32_t b)
{
    float f;
    memcpy(&f, &b, sizeof(f));
    return f;
}

static float nan_f(void) { return f_from_bits(0x7FC00000u); }
static float inf_f(void) { return f_from_bits(0x7F800000u); }

/* ================================================================== */
/*  helpers                                                            */
/* ================================================================== */

static void mk(pv_t *p, bool holds)
{
    pv_cfg_t c;
    c.carry_us       = CARRY_US;
    c.holds_presence = holds;
    pv_init(p, &c);
}

/* A completed analysis as vitals.c reports it: VIT_OK with confident
 * breathing; heart valid only when hr > 0. */
static vitals_out_t vo_ok(float br, float hr, float still_s)
{
    vitals_out_t v;
    memset(&v, 0, sizeof(v));
    v.state    = VIT_OK;
    v.br_valid = true;
    v.br_bpm   = br;
    v.br_conf  = 6.0f;
    v.hr_valid = hr > 0.0f;
    v.hr_bpm   = hr;
    v.hr_conf  = (hr > 0.0f) ? 7.0f : 0.0f;
    v.still_s  = still_s;
    v.updates  = 1;
    return v;
}

/* An analysis while present, count 1 (the only case that can credit). */
static bool an(pv_t *p, int64_t t, float br, float hr, float still_s)
{
    vitals_out_t v = vo_ok(br, hr, still_s);
    return pv_analysis(p, t, true, 1, &v);
}

static bool latch(pv_t *p, int slot)
{
    return pv_on_label(p, slot, k_names[slot]);
}

static bool log_ok(const pv_t *p)
{
    return pv_log_valid(&p->log, sizeof(p->log));
}

static int attr_slot(const pv_t *p)
{
    int s = 99;
    (void)pv_attr(p, &s);
    return s;
}

static bool is_of(const pv_t *p, bool present, int count, const char *want)
{
    const char *of = pv_vitals_of(p, present, count);
    if (!want) return of == NULL;
    return of != NULL && strcmp(of, want) == 0;
}

static bool rec_zero(const pv_rec_t *r)
{
    static const pv_rec_t z;   /* static: zero-filled */
    return memcmp(r, &z, sizeof(z)) == 0;
}

/* One committed still episode: three identical credited analyses (still_s
 * 30, 32, 34), then the gate says moving. Advances *t. */
static void ep(pv_t *p, int64_t *t, float br, float hr)
{
    for (int i = 0; i < 3; i++) {
        *t += 2 * S_US;
        bool ok = an(p, *t, br, hr, 30.0f + 2.0f * (float)i);
        CHECK(ok, "ep(%.1f, %.1f): analysis %d not credited", (double)br, (double)hr, i);
    }
    *t += S_US;
    pv_packet(p, *t, false);
}

/* A valid, populated log image. */
static void good_log(pv_log_t *l)
{
    pv_log_init(l);
    strcpy(l->rec[0].name, "alice");
    l->rec[0].br_eps      = 2;
    l->rec[0].hr_eps      = 1;
    l->rec[0].samples     = 9;
    l->rec[0].br_last     = 14.5f;
    l->rec[0].br_mean     = 15.0f;
    l->rec[0].hr_last_exp = 70.0f;
    l->rec[0].hr_mean_exp = 71.0f;
    strcpy(l->rec[1].name, "Bob_9-x");  /* every character class of the alphabet */
    l->rec[1].br_eps      = 1;
    l->rec[1].samples     = 3;
    l->rec[1].br_last     = 12.0f;
    l->rec[1].br_mean     = 12.0f;
    /* rec[2]: empty */
    strcpy(l->rec[3].name, "zed");      /* named, nothing logged yet */
}

static const char *const k_good_names[PV_SLOTS] = { "alice", "Bob_9-x", "", "zed" };

/* ================================================================== */
/*  (a) the log image                                                  */
/* ================================================================== */
static void test_log_valid(void)
{
    static pv_log_t good, l;
    size_t sz = sizeof(pv_log_t);

    printf("(a) log image: pv_log_init, pv_log_valid (%u bytes)\n", (unsigned)sz);
    CHECK(sz == 168u, "pv_log_t is %u bytes, the header documents ~170 (8 + 4 x 40)", (unsigned)sz);

    pv_log_init(&l);
    CHECK(l.magic == PV_LOG_MAGIC && l.version == PV_LOG_VERSION && l.nslots == PV_SLOTS,
          "pv_log_init header %08x/%u/%u", (unsigned)l.magic, (unsigned)l.version, (unsigned)l.nslots);
    int nonzero = 0;
    for (int s = 0; s < PV_SLOTS; s++) {
        if (!rec_zero(&l.rec[s])) nonzero++;
    }
    CHECK(nonzero == 0, "pv_log_init left %d records non-zero", nonzero);
    CHECK(pv_log_valid(&l, sizeof(l)), "the empty log is rejected");

    good_log(&good);
    CHECK(pv_log_valid(&good, sizeof(good)), "the populated good log is rejected");

    /* size, header */
    CHECK(!pv_log_valid(NULL, sizeof(good)), "NULL accepted");
    CHECK(!pv_log_valid(&good, 0), "size 0 accepted");
    CHECK(!pv_log_valid(&good, sizeof(good) - 1), "size - 1 accepted");
    CHECK(!pv_log_valid(&good, sizeof(good) + 1), "size + 1 accepted");
    CHECK(!pv_log_valid(&good, sizeof(pv_rec_t)), "one record's size accepted");
    l = good; l.magic ^= 1u;
    CHECK(!pv_log_valid(&l, sizeof(l)), "bad magic accepted");
    l = good; l.version = PV_LOG_VERSION + 1;
    CHECK(!pv_log_valid(&l, sizeof(l)), "version + 1 accepted");
    l = good; l.version = 0;
    CHECK(!pv_log_valid(&l, sizeof(l)), "version 0 accepted");
    l = good; l.nslots = PV_SLOTS - 1;
    CHECK(!pv_log_valid(&l, sizeof(l)), "nslots - 1 accepted");
    l = good; l.nslots = PV_SLOTS + 1;
    CHECK(!pv_log_valid(&l, sizeof(l)), "nslots + 1 accepted");

    /* names: terminated inside the field, [A-Za-z0-9_-] only */
    l = good; memset(l.rec[2].name, 'a', PV_NAME_LEN);
    CHECK(!pv_log_valid(&l, sizeof(l)), "unterminated name (in the empty slot) accepted");
    l = good; memset(l.rec[3].name, 'a', PV_NAME_LEN - 1); l.rec[3].name[PV_NAME_LEN - 1] = '\0';
    CHECK(pv_log_valid(&l, sizeof(l)), "a 15-character name rejected");
    {
        static const unsigned char bad[] = { ' ', '.', '"', '\\', '/', '\n', '{', '%', 0x7F, 0x80, 0xFF };
        int accepted = 0;
        for (size_t i = 0; i < sizeof(bad); i++) {
            l = good;
            l.rec[0].name[2] = (char)bad[i];
            if (pv_log_valid(&l, sizeof(l))) {
                accepted++;
                printf("  name byte 0x%02x accepted\n", (unsigned)bad[i]);
            }
        }
        CHECK(accepted == 0, "%d name bytes outside [A-Za-z0-9_-] accepted", accepted);
    }

    /* counts nobody owns */
    l = good; l.rec[2].samples = 1;
    CHECK(!pv_log_valid(&l, sizeof(l)), "samples under an empty name accepted");
    l = good; l.rec[2].br_eps = 1; l.rec[2].br_last = 12.0f; l.rec[2].br_mean = 12.0f;
    CHECK(!pv_log_valid(&l, sizeof(l)), "br_eps under an empty name accepted");
    l = good; l.rec[2].br_eps = 1; l.rec[2].hr_eps = 1; l.rec[2].br_last = 12.0f; l.rec[2].br_mean = 12.0f;
    l.rec[2].hr_last_exp = 70.0f; l.rec[2].hr_mean_exp = 70.0f;
    CHECK(!pv_log_valid(&l, sizeof(l)), "hr_eps under an empty name accepted");

    /* hr_eps <= br_eps */
    l = good; l.rec[0].hr_eps = 3;
    CHECK(!pv_log_valid(&l, sizeof(l)), "hr_eps 3 > br_eps 2 accepted");
    l = good; l.rec[0].hr_eps = 2;
    CHECK(pv_log_valid(&l, sizeof(l)), "hr_eps == br_eps rejected");

    /* every float: finite, in [0, PV_VAL_MAX] */
    {
        const char *fname[4] = { "br_last", "br_mean", "hr_last_exp", "hr_mean_exp" };
        float badv[7];
        badv[0] = nan_f();
        badv[1] = inf_f();
        badv[2] = -inf_f();
        badv[3] = -1.0f;
        badv[4] = -1e-30f;
        badv[5] = 200.01f;
        badv[6] = 1e30f;
        for (int f = 0; f < 4; f++) {
            for (int b = 0; b < 7; b++) {
                l = good;
                float *fp[4] = { &l.rec[0].br_last, &l.rec[0].br_mean,
                                 &l.rec[0].hr_last_exp, &l.rec[0].hr_mean_exp };
                *fp[f] = badv[b];
                CHECK(!pv_log_valid(&l, sizeof(l)), "%s = %g accepted", fname[f], (double)badv[b]);
            }
            l = good;
            {
                float *fq[4] = { &l.rec[0].br_last, &l.rec[0].br_mean,
                                 &l.rec[0].hr_last_exp, &l.rec[0].hr_mean_exp };
                *fq[f] = PV_VAL_MAX;
                CHECK(pv_log_valid(&l, sizeof(l)), "%s = PV_VAL_MAX rejected", fname[f]);
                /* zero where the count says the value is valid */
                *fq[f] = 0.0f;
                CHECK(!pv_log_valid(&l, sizeof(l)), "%s = 0 with its count > 0 accepted", fname[f]);
            }
        }
    }
    /* numbers where nothing is counted are bounded but never read: fine */
    l = good;
    l.rec[1].hr_last_exp = 55.0f;          /* hr_eps 0 */
    l.rec[2].br_mean     = 12.0f;          /* empty record */
    l.rec[3].hr_mean_exp = 80.0f;          /* named, nothing logged */
    CHECK(pv_log_valid(&l, sizeof(l)), "bounded stale numbers under zero counts rejected");
    l.rec[2].br_mean = nan_f();
    CHECK(!pv_log_valid(&l, sizeof(l)), "NaN under a zero count accepted");
}

/* ================================================================== */
/*  (b) the latch                                                      */
/* ================================================================== */
static void test_latch(void)
{
    pv_t p;
    int  s = 99;

    printf("(b) the latch: pv_on_label / pv_on_count / pv_release\n");
    mk(&p, true);
    CHECK(pv_attr(&p, &s) == PV_NONE && s == -1, "fresh: attr %d slot %d", (int)pv_attr(&p, NULL), s);
    CHECK(pv_attr(&p, NULL) == PV_NONE, "pv_attr(NULL slot)");
    CHECK(!pv_multiple(&p) && is_of(&p, true, 1, NULL), "fresh: multiple or named");

    /* NONE -> PERSON(alice); idempotent */
    CHECK(latch(&p, 0), "NONE -> alice reported no change");
    CHECK(pv_attr(&p, &s) == PV_PERSON && s == 0, "after alice: attr %d slot %d", (int)pv_attr(&p, NULL), s);
    CHECK(!latch(&p, 0) && !latch(&p, 0), "alice again reported a change");
    CHECK(is_of(&p, true, 1, "alice"), "vitals_of present, count 1");
    CHECK(is_of(&p, true, 0, "alice"), "vitals_of present, count 0");
    CHECK(is_of(&p, false, 1, NULL) && is_of(&p, false, 0, NULL), "named while not present");
    CHECK(is_of(&p, true, 2, NULL) && is_of(&p, true, 3, NULL), "named with count >= 2");
    CHECK(!pv_multiple(&p), "PERSON reads as multiple");

    /* A -> unknown -> A recovers */
    CHECK(pv_on_label(&p, PV_LABEL_UNKNOWN, NULL), "alice -> unknown reported no change");
    CHECK(pv_attr(&p, &s) == PV_UNKNOWN && s == -1, "after unknown: attr %d slot %d", (int)pv_attr(&p, NULL), s);
    CHECK(is_of(&p, true, 1, NULL), "UNKNOWN is named");
    CHECK(!pv_on_label(&p, PV_LABEL_UNKNOWN, "ignored"), "unknown again reported a change");
    CHECK(latch(&p, 0) && pv_attr(&p, NULL) == PV_PERSON && attr_slot(&p) == 0, "A -> unknown -> A did not recover");

    /* A -> unknown -> B: two names */
    (void)pv_on_label(&p, PV_LABEL_UNKNOWN, NULL);
    CHECK(latch(&p, 1), "unknown -> bob (after alice) reported no change");
    CHECK(pv_attr(&p, &s) == PV_MULTIPLE && s == -1 && pv_multiple(&p), "A -> unknown -> B is not MULTIPLE");
    CHECK(is_of(&p, true, 1, NULL) && is_of(&p, true, 0, NULL), "MULTIPLE is named");

    /* MULTIPLE is sticky */
    CHECK(!latch(&p, 0) && !latch(&p, 1) && !latch(&p, 2), "a label moved MULTIPLE");
    CHECK(!pv_on_label(&p, PV_LABEL_UNKNOWN, NULL), "unknown moved MULTIPLE");
    CHECK(!pv_on_count(&p, 2) && !pv_on_count(&p, 1) && !pv_on_count(&p, 0), "a count moved MULTIPLE");
    CHECK(pv_attr(&p, NULL) == PV_MULTIPLE, "MULTIPLE did not stick");

    /* release: NONE, and the first name is forgotten */
    pv_release(&p);
    CHECK(pv_attr(&p, &s) == PV_NONE && s == -1 && !pv_multiple(&p), "release: attr %d", (int)pv_attr(&p, NULL));
    CHECK(latch(&p, 1) && pv_attr(&p, NULL) == PV_PERSON && attr_slot(&p) == 1,
          "after release bob should be the first name, got attr %d", (int)pv_attr(&p, NULL));

    /* A -> B directly */
    CHECK(latch(&p, 0) && pv_multiple(&p), "bob -> alice is not MULTIPLE");

    /* NONE -> UNKNOWN -> B: B is the first NAME */
    pv_release(&p);
    CHECK(pv_on_label(&p, PV_LABEL_UNKNOWN, NULL) && pv_attr(&p, NULL) == PV_UNKNOWN, "NONE -> UNKNOWN");
    CHECK(latch(&p, 1) && pv_attr(&p, NULL) == PV_PERSON && attr_slot(&p) == 1, "NONE -> unknown -> bob");
    CHECK(latch(&p, 2) && pv_multiple(&p), "... -> carol is not MULTIPLE");

    /* counts, from every non-MULTIPLE state */
    for (int start = 0; start < 3; start++) {
        mk(&p, true);
        if (start == 1) (void)pv_on_label(&p, PV_LABEL_UNKNOWN, NULL);
        if (start == 2) (void)latch(&p, 0);
        pv_attr_t before = pv_attr(&p, NULL);
        CHECK(!pv_on_count(&p, 1) && !pv_on_count(&p, 0) && !pv_on_count(&p, -1), "count < 2 reported a change");
        CHECK(pv_attr(&p, NULL) == before, "count < 2 changed the latch (start %d)", start);
        CHECK(pv_on_count(&p, start == 1 ? 3 : 2), "count >= 2 from start %d reported no change", start);
        CHECK(pv_attr(&p, &s) == PV_MULTIPLE && s == -1, "count >= 2 from start %d: attr %d", start,
              (int)pv_attr(&p, NULL));
        CHECK(!pv_on_count(&p, 2), "count 2 again reported a change");
    }

    /* labels that are not decisive tallies, or not slots */
    mk(&p, true);
    (void)latch(&p, 0);
    {
        static const int odd[] = { -2, -3, -100, PV_SLOTS, 99 };
        for (size_t i = 0; i < sizeof(odd) / sizeof(odd[0]); i++) {
            CHECK(!pv_on_label(&p, odd[i], "bob"), "label %d reported a change", odd[i]);
        }
        CHECK(pv_attr(&p, NULL) == PV_PERSON && attr_slot(&p) == 0, "a non-slot label moved the latch");
    }

    /* names that cannot be stored or printed */
    mk(&p, true);
    CHECK(pv_on_label(&p, 1, NULL) && pv_attr(&p, NULL) == PV_UNKNOWN, "unnamed slot did not latch UNKNOWN");
    CHECK(!pv_on_label(&p, 1, NULL), "the same unnamed slot again reported a change");
    CHECK(latch(&p, 0) && pv_multiple(&p), "a named slot after an unnamed one is not two people");
    mk(&p, true);
    CHECK(pv_on_label(&p, 1, "bad name") && pv_attr(&p, NULL) == PV_UNKNOWN && is_of(&p, true, 1, NULL),
          "'bad name' latched as a person");
    mk(&p, true);
    CHECK(pv_on_label(&p, 1, "abcdefghijklmnop") && pv_attr(&p, NULL) == PV_UNKNOWN,
          "a 16-character name latched (it would have to be truncated)");
    mk(&p, true);
    CHECK(pv_on_label(&p, 1, "abcdefghijklmno") && is_of(&p, true, 1, "abcdefghijklmno"),
          "a 15-character name did not latch");
    /* the first slot under a different name: re-enrolled without
     * pv_slot_gone - two people */
    mk(&p, true);
    (void)latch(&p, 0);
    CHECK(pv_on_label(&p, 0, "alicia") && pv_multiple(&p), "slot 0 renamed mid-visit is not MULTIPLE");

    /* NULL safety: nothing crashes, nothing claims success */
    pv_init(NULL, NULL);
    pv_log_init(NULL);
    pv_release(NULL);
    pv_presence_end(NULL, T0);
    pv_presence_start(NULL);
    pv_packet(NULL, T0, false);
    pv_slot_gone(NULL, 0);
    pv_log_clear(NULL, -1);
    pv_saved(NULL, T0, true);
    {
        vitals_out_t v = vo_ok(15.0f, 0.0f, 30.0f);
        int sn = 7;
        CHECK(!pv_on_label(NULL, 0, "alice") && !pv_on_count(NULL, 2), "NULL p: label/count");
        CHECK(!pv_analysis(NULL, T0, true, 1, &v), "NULL p: analysis");
        CHECK(!pv_analysis(&p, T0, true, 1, NULL), "NULL vo: analysis");
        CHECK(pv_attr(NULL, &sn) == PV_NONE && sn == -1, "NULL p: attr");
        CHECK(!pv_multiple(NULL) && pv_vitals_of(NULL, true, 1) == NULL, "NULL p: outputs");
        CHECK(!pv_save_due(NULL, T0, 0), "NULL p: save due");
        CHECK(pv_adopt_log(NULL, NULL, k_names) == 0, "NULL p: adopt");
    }

    /* pv_init(p, NULL): main.c's defaults */
    pv_init(&p, NULL);
    CHECK(p.cfg.carry_us == CARRY_US && p.cfg.holds_presence, "pv_init(NULL cfg): carry %lld holds %d",
          (long long)p.cfg.carry_us, (int)p.cfg.holds_presence);
    CHECK(p.attr == PV_NONE && p.slot == -1 && p.first_slot == -1 && p.acc_slot == -1 && !p.dirty,
          "pv_init state");
}

/* ================================================================== */
/*  (c) presence, the carry, and movement while absent                 */
/* ================================================================== */
static void test_presence(void)
{
    pv_t    p;
    int64_t t = T0;

    printf("(c) presence lapses, the carry (%lld s), movement while absent\n", (long long)(CARRY_US / S_US));

    /* holds_presence: an end keeps the latch; nobody is named while absent */
    mk(&p, true);
    (void)latch(&p, 0);
    pv_presence_end(&p, t);
    CHECK(p.absent && pv_attr(&p, NULL) == PV_PERSON, "hold: presence end dropped the latch");
    CHECK(is_of(&p, false, 0, NULL), "named while absent");
    for (int i = 0; i < 100; i++) pv_packet(&p, t += 20000, true);
    CHECK(pv_attr(&p, NULL) == PV_PERSON, "2 s of still packets while absent released");
    pv_presence_start(&p);
    CHECK(!p.absent && is_of(&p, true, 1, "alice"), "restore did not bring the name back");

    /* without the hold, presence end is a release */
    mk(&p, false);
    (void)latch(&p, 0);
    pv_presence_end(&p, t);
    CHECK(pv_attr(&p, NULL) == PV_NONE && !p.absent, "no hold: presence end kept the latch");
    CHECK(latch(&p, 1) && attr_slot(&p) == 1, "no hold: the first name was not forgotten");

    /* exactly carry_us of absence is kept; one microsecond more releases */
    mk(&p, true);
    (void)latch(&p, 0);
    pv_presence_end(&p, T0);
    pv_packet(&p, T0 + CARRY_US, true);
    CHECK(pv_attr(&p, NULL) == PV_PERSON, "released at exactly carry_us");
    pv_packet(&p, T0 + CARRY_US + 1, true);
    CHECK(pv_attr(&p, NULL) == PV_NONE && !p.absent, "not released after carry_us");

    /* a second end without a start does not restart the carry */
    mk(&p, true);
    (void)latch(&p, 0);
    pv_presence_end(&p, T0);
    pv_presence_end(&p, T0 + 50 * S_US);
    pv_packet(&p, T0 + CARRY_US + 1, true);
    CHECK(pv_attr(&p, NULL) == PV_NONE, "a repeated presence end stretched the carry");

    /* a start clears it; the next end starts a new carry */
    mk(&p, true);
    (void)latch(&p, 0);
    pv_presence_end(&p, T0);
    pv_presence_start(&p);
    pv_presence_end(&p, T0 + 20 * S_US);
    pv_packet(&p, T0 + 20 * S_US + CARRY_US, true);
    CHECK(pv_attr(&p, NULL) == PV_PERSON, "the carry did not restart at the second end");
    pv_packet(&p, T0 + 20 * S_US + CARRY_US + 1, true);
    CHECK(pv_attr(&p, NULL) == PV_NONE, "the second carry did not expire");

    /* while present, still packets never release, however long */
    mk(&p, true);
    (void)latch(&p, 0);
    for (int h = 0; h <= 10; h++) pv_packet(&p, T0 + h * 3600 * S_US, true);
    CHECK(pv_attr(&p, NULL) == PV_PERSON, "10 h of still presence released");
    /* movement while present: the latch is kept */
    pv_packet(&p, T0 + 36001 * S_US, false);
    CHECK(pv_attr(&p, NULL) == PV_PERSON && is_of(&p, true, 1, "alice"), "movement while present released");

    /* movement while absent releases */
    mk(&p, true);
    (void)latch(&p, 0);
    pv_presence_end(&p, T0);
    pv_packet(&p, T0 + S_US, false);
    CHECK(pv_attr(&p, NULL) == PV_NONE && !p.absent, "gate MOVING while absent kept the latch");

    /* a walk-in whose gate and Step 08 fire on the SAME packet: main.c
     * calls pv_packet before the transition, so it is movement while
     * absent - a new visit */
    mk(&p, true);
    (void)latch(&p, 0);
    pv_presence_end(&p, T0);
    pv_packet(&p, T0 + 3 * S_US, false);
    pv_presence_start(&p);
    CHECK(pv_attr(&p, NULL) == PV_NONE && is_of(&p, true, 1, NULL), "same-packet walk-in kept the name");
    /* ...and why the order matters: the other way round keeps it */
    mk(&p, true);
    (void)latch(&p, 0);
    pv_presence_end(&p, T0);
    pv_presence_start(&p);
    pv_packet(&p, T0 + 3 * S_US, false);
    CHECK(pv_attr(&p, NULL) == PV_PERSON, "order check: start-then-packet should keep the latch");

    /* Step 08 re-triggers while the gate stays still: kept */
    mk(&p, true);
    (void)latch(&p, 0);
    pv_presence_end(&p, T0);
    pv_packet(&p, T0 + S_US, true);
    pv_presence_start(&p);
    CHECK(is_of(&p, true, 1, "alice"), "Step 08 re-trigger while still dropped the name");

    /* MULTIPLE is released the same way (the room has been empty) */
    mk(&p, true);
    (void)latch(&p, 0);
    (void)pv_on_count(&p, 2);
    pv_presence_end(&p, T0);
    pv_packet(&p, T0 + CARRY_US + 1, true);
    CHECK(pv_attr(&p, NULL) == PV_NONE, "MULTIPLE survived the carry");

    /* carry_us 0: any absence ends at the next still packet after it */
    {
        pv_cfg_t c;
        c.carry_us       = 0;
        c.holds_presence = true;
        pv_init(&p, &c);
        (void)latch(&p, 0);
        pv_presence_end(&p, T0);
        pv_packet(&p, T0, true);
        CHECK(pv_attr(&p, NULL) == PV_PERSON, "carry 0: released on the end's own packet");
        pv_packet(&p, T0 + 1, true);
        CHECK(pv_attr(&p, NULL) == PV_NONE, "carry 0: not released");
    }
}

/* ================================================================== */
/*  (d) crediting an analysis                                          */
/* ================================================================== */
static void test_credit(void)
{
    pv_t         p;
    vitals_out_t v;
    int64_t      t = T0;

    printf("(d) crediting an analysis\n");
    mk(&p, true);
    CHECK(!an(&p, t, 15.0f, 70.0f, 30.0f), "NONE credited");
    (void)pv_on_label(&p, PV_LABEL_UNKNOWN, NULL);
    CHECK(!an(&p, t, 15.0f, 70.0f, 30.0f), "UNKNOWN credited");
    pv_release(&p);
    (void)latch(&p, 0);
    (void)pv_on_count(&p, 2);
    CHECK(!an(&p, t, 15.0f, 70.0f, 30.0f), "MULTIPLE credited");
    pv_release(&p);
    (void)latch(&p, 0);
    CHECK(p.acc_slot == -1 && p.acc_br_n == 0, "something pending before the first credit");

    /* not present, or count >= 2 passed directly */
    v = vo_ok(15.0f, 70.0f, 30.0f);
    CHECK(!pv_analysis(&p, t, false, 0, &v), "credited while not present");
    CHECK(!pv_analysis(&p, t, false, 1, &v), "credited while not present (count 1)");
    CHECK(!pv_analysis(&p, t, true, 2, &v), "credited with count 2");
    CHECK(!pv_analysis(&p, t, true, 3, &v), "credited with count 3");

    /* the vitals state must be OK with a valid breathing rate */
    {
        static const vitals_state_t states[] = { VIT_OFF, VIT_MOVING, VIT_SETTLING, VIT_NO_SIGNAL };
        for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); i++) {
            v = vo_ok(15.0f, 70.0f, 30.0f);
            v.state = states[i];            /* br_valid left true: inconsistent on purpose */
            CHECK(!pv_analysis(&p, t, true, 1, &v), "credited in state %d", (int)states[i]);
        }
        v = vo_ok(15.0f, 70.0f, 30.0f);
        v.br_valid = false;
        CHECK(!pv_analysis(&p, t, true, 1, &v), "credited with br_valid false");
    }
    /* breathing rate outside (0, PV_VAL_MAX] */
    {
        float bad[8];
        bad[0] = 0.0f;
        bad[1] = -0.0f;
        bad[2] = -1.0f;
        bad[3] = nan_f();
        bad[4] = inf_f();
        bad[5] = -inf_f();
        bad[6] = 200.01f;
        bad[7] = 1e30f;
        for (int i = 0; i < 8; i++) {
            v = vo_ok(bad[i], 70.0f, 30.0f);
            CHECK(!pv_analysis(&p, t, true, 1, &v), "br %g credited", (double)bad[i]);
        }
    }
    CHECK(p.acc_slot == -1 && p.acc_br_n == 0 && p.last_seen_us[0] == 0 && !p.dirty,
          "refused analyses left a trace: acc_slot %d n %u seen %lld dirty %d", (int)p.acc_slot,
          (unsigned)p.acc_br_n, (long long)p.last_seen_us[0], (int)p.dirty);

    /* the bound itself is accepted */
    t += 2 * S_US;
    CHECK(an(&p, t, PV_VAL_MAX, 0.0f, 30.0f), "br == PV_VAL_MAX refused");
    CHECK(p.acc_slot == 0 && strcmp(p.acc_name, "alice") == 0 && p.acc_br_n == 1 && p.acc_hr_n == 0,
          "pending after one credit: slot %d name '%s' n %u/%u", (int)p.acc_slot, p.acc_name,
          (unsigned)p.acc_br_n, (unsigned)p.acc_hr_n);
    CHECK(p.last_seen_us[0] == t, "last_seen %lld, want %lld", (long long)p.last_seen_us[0], (long long)t);

    /* heart: credited only when valid and in range - breathing regardless */
    {
        float bad[6];
        bad[0] = nan_f();
        bad[1] = inf_f();
        bad[2] = 0.0f;
        bad[3] = -5.0f;
        bad[4] = 250.0f;
        bad[5] = -inf_f();
        for (int i = 0; i < 6; i++) {
            v = vo_ok(15.0f, 0.0f, 30.0f);
            v.hr_valid = true;
            v.hr_bpm   = bad[i];
            CHECK(pv_analysis(&p, t, true, 1, &v), "hr %g: breathing refused", (double)bad[i]);
        }
        v = vo_ok(15.0f, 0.0f, 30.0f);
        v.hr_valid = false;
        v.hr_bpm   = 72.0f;                 /* a number without its flag */
        CHECK(pv_analysis(&p, t, true, 1, &v), "hr_valid false: breathing refused");
        CHECK(p.acc_br_n == 8 && p.acc_hr_n == 0, "after bad hr: n %u/%u, want 8/0",
              (unsigned)p.acc_br_n, (unsigned)p.acc_hr_n);
        CHECK(an(&p, t, 15.0f, 72.0f, 30.0f) && p.acc_hr_n == 1, "a valid hr was not counted");
    }
    /* credits alone change no record */
    CHECK(!p.dirty && p.log.rec[0].br_eps == 0 && p.log.rec[0].name[0] == '\0', "a credit wrote the log");

    /* uptime 0 is stored as 1: 0 means "not seen since boot" */
    mk(&p, true);
    (void)latch(&p, 0);
    CHECK(an(&p, 0, 15.0f, 0.0f, 30.0f) && p.last_seen_us[0] == 1, "credit at t=0: last_seen %lld",
          (long long)p.last_seen_us[0]);

    /* an analysis that is not credited does not end the episode */
    mk(&p, true);
    (void)latch(&p, 0);
    t = T0;
    (void)an(&p, t += 2 * S_US, 15.0f, 0.0f, 30.0f);
    (void)an(&p, t += 2 * S_US, 15.0f, 0.0f, 32.0f);
    v = vo_ok(0.0f, 0.0f, 34.0f);
    v.state    = VIT_NO_SIGNAL;
    v.br_valid = false;
    CHECK(!pv_analysis(&p, t += 2 * S_US, true, 1, &v), "NO_SIGNAL credited");
    (void)an(&p, t += 2 * S_US, 15.0f, 0.0f, 36.0f);
    pv_packet(&p, t += S_US, false);
    CHECK(p.log.rec[0].br_eps == 1 && p.log.rec[0].samples == 3u,
          "a NO_SIGNAL analysis split the episode: %u episodes, %u samples",
          (unsigned)p.log.rec[0].br_eps, (unsigned)p.log.rec[0].samples);
}

/* ================================================================== */
/*  (e) still episodes                                                 */
/* ================================================================== */
static void test_episodes(void)
{
    pv_t         p;
    vitals_out_t v;
    int64_t      t = T0;
    pv_rec_t    *r = &p.log.rec[0];

    printf("(e) still episodes: PV_EP_MIN = %d, breathing and heart counted apart, what ends one\n", PV_EP_MIN);

    /* PV_EP_MIN - 1 samples: discarded, not committed */
    mk(&p, true);
    (void)latch(&p, 0);
    (void)an(&p, t += 2 * S_US, 15.0f, 70.0f, 30.0f);
    (void)an(&p, t += 2 * S_US, 15.0f, 70.0f, 32.0f);
    pv_packet(&p, t += S_US, false);
    CHECK(rec_zero(r) && !p.dirty && p.acc_slot == -1, "a 2-sample episode was committed (%u)", (unsigned)r->br_eps);

    /* PV_EP_MIN: committed - episode mean, breathing and heart */
    (void)an(&p, t += 2 * S_US, 14.0f, 69.0f, 30.0f);
    (void)an(&p, t += 2 * S_US, 15.0f, 70.0f, 32.0f);
    (void)an(&p, t += 2 * S_US, 16.0f, 71.0f, 34.0f);
    pv_packet(&p, t += S_US, false);
    CHECK(strcmp(r->name, "alice") == 0 && r->br_eps == 1 && r->samples == 3u && p.dirty,
          "3-sample episode: '%s' %u episodes %u samples dirty %d", r->name, (unsigned)r->br_eps,
          (unsigned)r->samples, (int)p.dirty);
    CHECK_NEAR(r->br_last, 15.0, 0.0, "episode breathing");
    CHECK_NEAR(r->br_mean, 15.0, 0.0, "breathing mean after one episode");
    CHECK(r->hr_eps == 1, "heart episodes %u", (unsigned)r->hr_eps);
    CHECK_NEAR(r->hr_last_exp, 70.0, 0.0, "episode heart");
    CHECK_NEAR(r->hr_mean_exp, 70.0, 0.0, "heart mean after one episode");

    /* 4 breathing samples, 2 heart: breathing committed, heart not */
    (void)an(&p, t += 2 * S_US, 17.0f, 80.0f, 30.0f);
    (void)an(&p, t += 2 * S_US, 17.0f, 80.0f, 32.0f);
    (void)an(&p, t += 2 * S_US, 17.0f, 0.0f, 34.0f);
    (void)an(&p, t += 2 * S_US, 17.0f, 0.0f, 36.0f);
    pv_packet(&p, t += S_US, false);
    CHECK(r->br_eps == 2 && r->samples == 7u && r->hr_eps == 1, "br 4 / hr 2: %u br eps, %u samples, %u hr eps",
          (unsigned)r->br_eps, (unsigned)r->samples, (unsigned)r->hr_eps);
    CHECK_NEAR(r->br_last, 17.0, 0.0, "br_last after the 4-sample episode");
    CHECK_NEAR(r->hr_last_exp, 70.0, 0.0, "hr_last must not move on a 2-sample heart episode");

    /* the vitals buffer restarted: still_s fell */
    mk(&p, true);
    (void)latch(&p, 0);
    (void)an(&p, t += 2 * S_US, 15.0f, 0.0f, 40.0f);
    (void)an(&p, t += 2 * S_US, 15.0f, 0.0f, 42.0f);
    (void)an(&p, t += 2 * S_US, 15.0f, 0.0f, 44.0f);
    CHECK(an(&p, t += 2 * S_US, 18.0f, 0.0f, 20.2f), "the restart analysis itself was not credited");
    CHECK(r->br_eps == 1 && r->samples == 3u && p.acc_br_n == 1, "still_s drop: %u episodes, %u samples, %u pending",
          (unsigned)r->br_eps, (unsigned)r->samples, (unsigned)p.acc_br_n);
    CHECK_NEAR(r->br_last, 15.0, 0.0, "restart: the old spell's value");
    CHECK_NEAR(p.acc_br_sum, 18.0, 0.0, "restart: the new spell starts with its own sample");

    /* ...but a long spell plateaus at the ring length: not a restart */
    mk(&p, true);
    (void)latch(&p, 0);
    {
        static const float ss[] = { 48.0f, 50.0f, 51.2f, 51.2f, 51.2f, 51.2f };
        for (size_t i = 0; i < sizeof(ss) / sizeof(ss[0]); i++) (void)an(&p, t += 2 * S_US, 15.0f, 0.0f, ss[i]);
    }
    CHECK(r->br_eps == 0 && p.acc_br_n == 6, "51.2 s plateau read as a restart: %u episodes, %u pending",
          (unsigned)r->br_eps, (unsigned)p.acc_br_n);

    /* a restart seen by an analysis that is NOT credited still ends it */
    mk(&p, true);
    (void)latch(&p, 0);
    (void)an(&p, t += 2 * S_US, 15.0f, 0.0f, 40.0f);
    (void)an(&p, t += 2 * S_US, 15.0f, 0.0f, 42.0f);
    (void)an(&p, t += 2 * S_US, 15.0f, 0.0f, 44.0f);
    v = vo_ok(15.0f, 0.0f, 20.0f);
    CHECK(!pv_analysis(&p, t += 2 * S_US, false, 0, &v), "credited while absent");
    CHECK(r->br_eps == 1 && p.acc_slot == -1, "uncredited restart kept the episode open");

    /* non-finite / negative still_s is read as 0, never poisons the test */
    mk(&p, true);
    (void)latch(&p, 0);
    (void)an(&p, t += 2 * S_US, 15.0f, 0.0f, 30.0f);
    (void)an(&p, t += 2 * S_US, 15.0f, 0.0f, nan_f());
    CHECK(p.prev_still_s == 0.0f && r->br_eps == 0, "NaN still_s: prev %g", (double)p.prev_still_s);
    (void)an(&p, t += 2 * S_US, 15.0f, 0.0f, 34.0f);
    (void)an(&p, t += 2 * S_US, 15.0f, 0.0f, 36.0f);
    CHECK(p.prev_still_s == 36.0f, "still_s tracking lost after NaN");

    /* every latch change commits */
    {
        const char *what[6] = { "unknown", "a second name", "count 2", "release", "presence end, no hold",
                                "slot gone of another slot" };
        for (int k = 0; k < 6; k++) {
            mk(&p, k != 4);
            (void)latch(&p, 0);
            for (int i = 0; i < 3; i++) (void)an(&p, t += 2 * S_US, 15.0f, 0.0f, 30.0f + 2.0f * (float)i);
            switch (k) {
            case 0: (void)pv_on_label(&p, PV_LABEL_UNKNOWN, NULL); break;
            case 1: (void)latch(&p, 1); break;
            case 2: (void)pv_on_count(&p, 2); break;
            case 3: pv_release(&p); break;
            case 4: pv_presence_end(&p, t); break;
            default: pv_slot_gone(&p, 1); break;
            }
            if (k < 5) {
                CHECK(r->br_eps == 1 && p.acc_slot == -1, "%s did not commit the pending episode", what[k]);
            } else {
                /* deleting somebody ELSE's slot neither commits nor
                 * discards the latched person's episode */
                CHECK(r->br_eps == 0 && p.acc_br_n == 3, "%s touched alice's pending episode", what[k]);
            }
            CHECK(rec_zero(&p.log.rec[1]), "%s: bob's record got alice's samples", what[k]);
        }
    }
    /* ...and things that are not a change do not */
    mk(&p, true);
    (void)latch(&p, 0);
    for (int i = 0; i < 3; i++) (void)an(&p, t += 2 * S_US, 15.0f, 0.0f, 30.0f + 2.0f * (float)i);
    (void)latch(&p, 0);
    (void)pv_on_count(&p, 1);
    pv_packet(&p, t, true);
    pv_presence_start(&p);
    CHECK(r->br_eps == 0 && p.acc_br_n == 3, "a non-change ended the episode");

    /* a lapse the hold undoes does NOT end it: one episode across it */
    mk(&p, true);
    (void)latch(&p, 0);
    (void)an(&p, t += 2 * S_US, 15.0f, 0.0f, 30.0f);
    (void)an(&p, t += 2 * S_US, 15.0f, 0.0f, 32.0f);
    pv_presence_end(&p, t);
    for (int i = 0; i < 100; i++) pv_packet(&p, t += 20000, true);
    v = vo_ok(15.0f, 0.0f, 34.0f);
    CHECK(!pv_analysis(&p, t, false, 0, &v), "credited during the lapse");
    pv_presence_start(&p);
    (void)an(&p, t += 2 * S_US, 15.0f, 0.0f, 36.0f);
    pv_packet(&p, t += S_US, false);
    CHECK(r->br_eps == 1 && r->samples == 3u, "lapse split the episode: %u episodes %u samples",
          (unsigned)r->br_eps, (unsigned)r->samples);

    /* A -> unknown -> A: two episodes */
    mk(&p, true);
    (void)latch(&p, 0);
    for (int i = 0; i < 3; i++) (void)an(&p, t += 2 * S_US, 15.0f, 0.0f, 30.0f + 2.0f * (float)i);
    (void)pv_on_label(&p, PV_LABEL_UNKNOWN, NULL);
    CHECK(!an(&p, t += 2 * S_US, 15.0f, 0.0f, 36.0f), "credited while UNKNOWN");
    (void)latch(&p, 0);
    for (int i = 0; i < 3; i++) (void)an(&p, t += 2 * S_US, 17.0f, 0.0f, 38.0f + 2.0f * (float)i);
    pv_packet(&p, t += S_US, false);
    CHECK(r->br_eps == 2 && r->samples == 6u, "A -> unknown -> A: %u episodes %u samples",
          (unsigned)r->br_eps, (unsigned)r->samples);
    CHECK_NEAR(r->br_mean, 16.0, 0.0, "A -> unknown -> A mean");

    /* ~36 hours of one still spell: the pending counter does not wrap */
    mk(&p, true);
    (void)latch(&p, 0);
    for (int i = 0; i < 3; i++) (void)an(&p, t += 2 * S_US, 15.0f, 0.0f, 51.2f);
    p.acc_br_n   = UINT16_MAX;
    p.acc_br_sum = 15.0f * 65535.0f;
    CHECK(an(&p, t += 2 * S_US, 16.0f, 0.0f, 51.2f), "the 65536th sample was refused");
    CHECK(r->br_eps == 1 && r->samples == 65535u && p.acc_br_n == 1, "saturated spell: %u eps, %u samples, %u pending",
          (unsigned)r->br_eps, (unsigned)r->samples, (unsigned)p.acc_br_n);
    CHECK_NEAR(r->br_last, 15.0, 1e-4, "saturated spell mean");
    CHECK(log_ok(&p), "log invalid after the saturated spell");
}

/* ================================================================== */
/*  (f) the numbers in a record                                        */
/* ================================================================== */
static void test_log_numbers(void)
{
    pv_t      p;
    int64_t   t = T0;
    pv_rec_t *r = &p.log.rec[0];

    printf("(f) running mean, EWMA beyond PV_MEAN_CAP = %d episodes, saturation, re-init\n", PV_MEAN_CAP);

    /* the exact running mean */
    mk(&p, true);
    (void)latch(&p, 0);
    ep(&p, &t, 10.0f, 0.0f);
    ep(&p, &t, 20.0f, 0.0f);
    ep(&p, &t, 30.0f, 0.0f);
    CHECK(r->br_eps == 3 && r->samples == 9u && r->hr_eps == 0, "3 episodes: %u / %u / %u",
          (unsigned)r->br_eps, (unsigned)r->samples, (unsigned)r->hr_eps);
    CHECK_NEAR(r->br_mean, 20.0, 0.0, "mean of 10, 20, 30");
    CHECK_NEAR(r->br_last, 30.0, 0.0, "last of 10, 20, 30");

    /* PV_MEAN_CAP identical episodes, then the mean moves by (x-mean)/CAP */
    mk(&p, true);
    (void)latch(&p, 0);
    for (int i = 0; i < PV_MEAN_CAP; i++) ep(&p, &t, 16.0f, 64.0f);
    CHECK(r->br_eps == PV_MEAN_CAP && r->hr_eps == PV_MEAN_CAP, "after %d: %u/%u episodes", PV_MEAN_CAP,
          (unsigned)r->br_eps, (unsigned)r->hr_eps);
    ep(&p, &t, 48.0f, 96.0f);
    CHECK_NEAR(r->br_mean, 17.0, 0.0, "33rd episode: 16 + (48-16)/32");
    CHECK_NEAR(r->hr_mean_exp, 65.0, 0.0, "33rd episode: 64 + (96-64)/32");
    CHECK_NEAR(r->br_last, 48.0, 0.0, "33rd br_last");
    CHECK_NEAR(r->hr_last_exp, 96.0, 0.0, "33rd hr_last_exp");
    ep(&p, &t, 49.0f, 97.0f);
    CHECK_NEAR(r->br_mean, 18.0, 0.0, "34th episode: 17 + (49-17)/32");
    CHECK_NEAR(r->hr_mean_exp, 66.0, 0.0, "34th episode: 65 + (97-65)/32");
    CHECK(r->br_eps == 34 && r->hr_eps == 34 && r->samples == 102u, "34 episodes: %u/%u, %u samples",
          (unsigned)r->br_eps, (unsigned)r->hr_eps, (unsigned)r->samples);
    /* a long history still follows a change (an exact running mean would
     * sit at 12.86 after this) */
    for (int i = 0; i < 200; i++) ep(&p, &t, 12.0f, 0.0f);
    printf("    after 34 episodes at ~16 and 200 at 12: breathing mean %.3f (exact running mean 12.86)\n",
           (double)r->br_mean);
    CHECK(r->br_mean > 12.0f && r->br_mean < 12.05f, "EWMA did not follow: mean %.4f", (double)r->br_mean);
    CHECK(r->br_eps == 234 && r->hr_eps == 34, "234 episodes: %u / %u", (unsigned)r->br_eps, (unsigned)r->hr_eps);
    CHECK_NEAR(r->hr_mean_exp, 66.0, 0.0, "heart mean moved without heart episodes");

    /* counters saturate, hr_eps <= br_eps survives */
    mk(&p, true);
    (void)latch(&p, 0);
    ep(&p, &t, 15.0f, 70.0f);
    r->br_eps  = UINT16_MAX;
    r->hr_eps  = UINT16_MAX - 1;
    r->samples = UINT32_MAX - 1u;
    CHECK(log_ok(&p), "poked record invalid");
    ep(&p, &t, 15.0f, 70.0f);
    CHECK(r->br_eps == UINT16_MAX && r->hr_eps == UINT16_MAX && r->samples == UINT32_MAX,
          "saturation: %u / %u / %u", (unsigned)r->br_eps, (unsigned)r->hr_eps, (unsigned)r->samples);
    ep(&p, &t, 15.0f, 70.0f);
    CHECK(r->br_eps == UINT16_MAX && r->hr_eps == UINT16_MAX && r->samples == UINT32_MAX,
          "saturation (again): %u / %u / %u", (unsigned)r->br_eps, (unsigned)r->hr_eps, (unsigned)r->samples);
    CHECK_NEAR(r->br_mean, 15.0, 0.0, "saturated mean");
    CHECK(log_ok(&p), "log invalid after saturation");

    /* a record that belongs to somebody else is re-initialised at commit,
     * never merged into */
    mk(&p, true);
    (void)latch(&p, 0);
    strcpy(r->name, "bob");
    r->br_eps      = 5;
    r->hr_eps      = 2;
    r->samples     = 20;
    r->br_last     = 30.0f;
    r->br_mean     = 30.0f;
    r->hr_last_exp = 80.0f;
    r->hr_mean_exp = 80.0f;
    CHECK(log_ok(&p), "bob's record invalid");
    ep(&p, &t, 15.0f, 0.0f);
    CHECK(strcmp(r->name, "alice") == 0 && r->br_eps == 1 && r->samples == 3u && r->hr_eps == 0 &&
          r->hr_last_exp == 0.0f && r->hr_mean_exp == 0.0f,
          "re-init: '%s' %u eps %u samples hr %u/%.1f/%.1f", r->name, (unsigned)r->br_eps,
          (unsigned)r->samples, (unsigned)r->hr_eps, (double)r->hr_last_exp, (double)r->hr_mean_exp);
    CHECK_NEAR(r->br_mean, 15.0, 0.0, "re-init mean (bob's 30 merged in?)");

    /* stale numbers under a zero count (valid, never read) do not leak
     * into the first episode */
    mk(&p, true);
    (void)latch(&p, 1);
    strcpy(p.log.rec[1].name, "bob");
    p.log.rec[1].br_mean     = 150.0f;
    p.log.rec[1].hr_mean_exp = 99.0f;
    p.log.rec[1].hr_last_exp = 88.0f;
    CHECK(log_ok(&p), "stale record invalid");
    ep(&p, &t, 15.0f, 70.0f);
    CHECK_NEAR(p.log.rec[1].br_mean, 15.0, 0.0, "first episode over a stale br_mean");
    CHECK_NEAR(p.log.rec[1].hr_mean_exp, 70.0, 0.0, "first episode over a stale hr_mean_exp");

    /* values at the bound never round past it */
    mk(&p, true);
    (void)latch(&p, 0);
    for (int i = 0; i < 40; i++) {
        float x = (i & 1) ? 199.97f : PV_VAL_MAX;
        ep(&p, &t, x, x);
        if (!log_ok(&p)) break;
    }
    CHECK(log_ok(&p) && r->br_mean <= PV_VAL_MAX && r->hr_mean_exp <= PV_VAL_MAX,
          "values at PV_VAL_MAX broke the log: br_mean %.6f hr_mean %.6f", (double)r->br_mean,
          (double)r->hr_mean_exp);
}

/* ================================================================== */
/*  (g) pv_slot_gone vs pv_log_clear                                   */
/* ================================================================== */

/* bob has one committed episode; alice (latched now) has one committed and
 * one pending (4 samples); the log has just been saved. */
static void g_setup(pv_t *p, int64_t *t)
{
    mk(p, true);
    *t = T0;
    (void)latch(p, 1);
    ep(p, t, 12.0f, 0.0f);
    pv_release(p);
    (void)latch(p, 0);
    ep(p, t, 15.0f, 70.0f);
    for (int i = 0; i < 4; i++) {
        *t += 2 * S_US;
        (void)an(p, *t, 16.0f, 72.0f, 30.0f + 2.0f * (float)i);
    }
    pv_saved(p, *t, true);
}

static void test_slot_gone_vs_clear(void)
{
    static pv_t p, snap;
    int64_t t = T0;

    printf("(g) pv_slot_gone (template deleted) vs pv_log_clear ('vitals-reset')\n");

    g_setup(&p, &t);
    CHECK(p.log.rec[0].br_eps == 1 && p.log.rec[1].br_eps == 1 && p.acc_br_n == 4 && !p.dirty, "g_setup");

    /* forget alice while alice sits */
    pv_slot_gone(&p, 0);
    CHECK(rec_zero(&p.log.rec[0]) && p.last_seen_us[0] == 0, "slot_gone: alice's record kept");
    CHECK(p.acc_slot == -1 && p.acc_br_n == 0, "slot_gone: alice's pending kept");
    CHECK(pv_attr(&p, NULL) == PV_NONE && p.first_slot == -1 && is_of(&p, true, 1, NULL),
          "slot_gone: latch not released (attr %d)", (int)pv_attr(&p, NULL));
    CHECK(p.dirty, "slot_gone: not dirty");
    CHECK(p.log.rec[1].br_eps == 1 && p.last_seen_us[1] != 0, "slot_gone(0) touched bob");
    CHECK(!an(&p, t += 2 * S_US, 16.0f, 72.0f, 38.0f), "credited after forget");
    pv_packet(&p, t += S_US, false);
    CHECK(rec_zero(&p.log.rec[0]), "forgotten pending came back");

    /* vitals-reset alice while alice sits: the latch is kept */
    g_setup(&p, &t);
    pv_log_clear(&p, 0);
    CHECK(rec_zero(&p.log.rec[0]) && p.last_seen_us[0] == 0, "log_clear: record kept");
    CHECK(p.acc_slot == -1 && p.acc_br_n == 0, "log_clear: pending kept");
    CHECK(pv_attr(&p, NULL) == PV_PERSON && attr_slot(&p) == 0 && is_of(&p, true, 1, "alice"),
          "log_clear moved the latch");
    CHECK(p.dirty && p.log.rec[1].br_eps == 1, "log_clear: dirty %d, bob %u", (int)p.dirty,
          (unsigned)p.log.rec[1].br_eps);
    ep(&p, &t, 14.0f, 0.0f);
    CHECK(strcmp(p.log.rec[0].name, "alice") == 0 && p.log.rec[0].br_eps == 1 && p.log.rec[0].samples == 3u,
          "after log_clear a new episode should start a fresh record: %u eps %u samples",
          (unsigned)p.log.rec[0].br_eps, (unsigned)p.log.rec[0].samples);
    CHECK_NEAR(p.log.rec[0].br_mean, 14.0, 0.0, "fresh record mean");

    /* forget bob while alice sits: alice untouched */
    g_setup(&p, &t);
    pv_slot_gone(&p, 1);
    CHECK(rec_zero(&p.log.rec[1]), "slot_gone(1): bob kept");
    CHECK(pv_attr(&p, NULL) == PV_PERSON && attr_slot(&p) == 0 && p.acc_br_n == 4 && p.log.rec[0].br_eps == 1,
          "slot_gone(1) touched alice");

    /* the first name forgotten while UNKNOWN: a later name is the first */
    mk(&p, true);
    (void)latch(&p, 0);
    (void)pv_on_label(&p, PV_LABEL_UNKNOWN, NULL);
    pv_slot_gone(&p, 0);
    CHECK(pv_attr(&p, NULL) == PV_UNKNOWN && p.first_slot == -1 && p.name[0] == '\0',
          "UNKNOWN: first name not forgotten");
    CHECK(latch(&p, 1) && pv_attr(&p, NULL) == PV_PERSON && attr_slot(&p) == 1, "after forgetting A, B is two names");
    /* ...while MULTIPLE: still MULTIPLE (two people were seen) */
    mk(&p, true);
    (void)latch(&p, 0);
    (void)latch(&p, 1);
    pv_slot_gone(&p, 0);
    CHECK(pv_multiple(&p), "slot_gone released MULTIPLE");

    /* -1 = all */
    g_setup(&p, &t);
    pv_slot_gone(&p, -1);
    CHECK(rec_zero(&p.log.rec[0]) && rec_zero(&p.log.rec[1]) && pv_attr(&p, NULL) == PV_NONE &&
          p.acc_slot == -1 && p.dirty, "slot_gone(-1)");
    g_setup(&p, &t);
    pv_log_clear(&p, -1);
    CHECK(rec_zero(&p.log.rec[0]) && rec_zero(&p.log.rec[1]) && pv_attr(&p, NULL) == PV_PERSON &&
          p.acc_slot == -1 && p.dirty, "log_clear(-1)");

    /* out of range: ignored entirely */
    g_setup(&p, &t);
    snap = p;
    pv_slot_gone(&p, -2);
    pv_slot_gone(&p, PV_SLOTS);
    pv_log_clear(&p, -2);
    pv_log_clear(&p, PV_SLOTS);
    CHECK(memcmp(&p, &snap, sizeof(p)) == 0, "an out-of-range slot changed something");

    /* always dirty, even when the record was already empty (main.c saves
     * at once after forget / vitals-reset anyway) */
    mk(&p, true);
    pv_log_clear(&p, 2);
    CHECK(p.dirty, "log_clear of an empty record: not dirty");
    CHECK(log_ok(&p), "log invalid after (g)");
}

/* ================================================================== */
/*  (h) pv_adopt_log                                                   */
/* ================================================================== */
static void test_adopt(void)
{
    static pv_log_t good, bad;
    static pv_t     p, fresh;
    int             n;

    printf("(h) pv_adopt_log: a record whose slot changed owner is dropped\n");
    good_log(&good);

    mk(&p, true);
    n = pv_adopt_log(&p, &good, k_good_names);
    CHECK(n == 0 && !p.dirty && memcmp(&p.log, &good, sizeof(good)) == 0, "same names: %d cleared", n);

    {
        static const char *const changed[PV_SLOTS] = { "alice", "bob", "", NULL };
        mk(&p, true);
        n = pv_adopt_log(&p, &good, changed);
        CHECK(n == 2 && p.dirty, "Bob_9-x -> bob, zed -> NULL: %d cleared, dirty %d", n, (int)p.dirty);
        CHECK(memcmp(&p.log.rec[0], &good.rec[0], sizeof(pv_rec_t)) == 0, "alice's record changed");
        CHECK(rec_zero(&p.log.rec[1]) && rec_zero(&p.log.rec[3]), "a dropped record kept data");
    }
    {
        /* an empty record facing a newly enrolled name is not a drop */
        static const char *const enrolled[PV_SLOTS] = { "alice", "Bob_9-x", "carol", "zed" };
        mk(&p, true);
        n = pv_adopt_log(&p, &good, enrolled);
        CHECK(n == 0 && !p.dirty, "new name over an empty record counted as a drop: %d", n);
    }
    {
        static const char *const prefix[PV_SLOTS] = { "alic", "Bob_9-x", "", "zed" };
        static const char *const longer[PV_SLOTS] = { "alice2", "Bob_9-x", "", "zed" };
        mk(&p, true);
        n = pv_adopt_log(&p, &good, prefix);
        CHECK(n == 1 && rec_zero(&p.log.rec[0]), "prefix name matched: %d", n);
        mk(&p, true);
        n = pv_adopt_log(&p, &good, longer);
        CHECK(n == 1 && rec_zero(&p.log.rec[0]), "longer name matched: %d", n);
    }
    mk(&p, true);
    n = pv_adopt_log(&p, &good, NULL);
    CHECK(n == 3, "no model (names NULL): %d cleared, want 3", n);

    /* an unusable image is not adopted - nor marked dirty */
    bad = good;
    bad.magic ^= 1u;
    mk(&p, true);
    mk(&fresh, true);
    n = pv_adopt_log(&p, &bad, k_good_names);
    CHECK(n == 0 && !p.dirty && memcmp(&p.log, &fresh.log, sizeof(p.log)) == 0, "a bad image was adopted");
    CHECK(pv_adopt_log(&p, NULL, k_good_names) == 0, "NULL image");

    /* an adopted record carries on */
    mk(&p, true);
    (void)pv_adopt_log(&p, &good, k_good_names);
    (void)latch(&p, 0);
    {
        int64_t t = T0;
        ep(&p, &t, 18.0f, 0.0f);
    }
    CHECK(p.log.rec[0].br_eps == 3 && p.log.rec[0].samples == 12u && p.log.rec[0].hr_eps == 1,
          "adopted + 1 episode: %u eps %u samples %u hr", (unsigned)p.log.rec[0].br_eps,
          (unsigned)p.log.rec[0].samples, (unsigned)p.log.rec[0].hr_eps);
    CHECK_NEAR(p.log.rec[0].br_mean, 16.0, 0.0, "adopted mean 15 (2 eps) + 18 -> 16");
}

/* ================================================================== */
/*  (i) the save backoff                                               */
/* ================================================================== */
static void test_save(void)
{
    pv_t          p;
    const int64_t gap = 300 * S_US;          /* CONFIG_CSI_VITALS_LOG_SAVE_S default */

    printf("(i) pv_save_due / pv_saved: the flash-write backoff\n");
    mk(&p, true);
    CHECK(!pv_save_due(&p, 10 * gap, gap), "clean log due");
    pv_log_clear(&p, 3);                     /* any change */
    CHECK(!pv_save_due(&p, gap - 1, gap), "first save before min_gap after boot");
    CHECK(pv_save_due(&p, gap, gap), "first save not due at min_gap");

    pv_saved(&p, gap, false);                /* the flash failed */
    CHECK(p.dirty && p.last_save_us == gap, "failed save: dirty %d stamp %lld", (int)p.dirty,
          (long long)p.last_save_us);
    CHECK(!pv_save_due(&p, gap + 1, gap), "a failed save retried on the next packet");
    CHECK(!pv_save_due(&p, 2 * gap - 1, gap), "a failed save retried before min_gap");
    CHECK(pv_save_due(&p, 2 * gap, gap), "a failed save never retried");

    pv_saved(&p, 2 * gap, true);
    CHECK(!p.dirty && !pv_save_due(&p, 100 * gap, gap), "saved log still due");

    pv_log_clear(&p, 3);                     /* a change 10 s after the save */
    CHECK(!pv_save_due(&p, 3 * gap - 1, gap), "due before min_gap since the last attempt");
    CHECK(pv_save_due(&p, 3 * gap, gap), "not due at min_gap since the last attempt");
    /* forget / vitals-reset: main.c saves at once and reports it */
    pv_saved(&p, 2 * gap + 20 * S_US, true);
    CHECK(!p.dirty, "forced save left the log dirty");
    pv_log_clear(&p, 3);
    CHECK(pv_save_due(&p, 2 * gap + 20 * S_US, 0), "min_gap 0: not due at once");
}

/* ================================================================== */
/*  (j) pv_format_rec                                                  */
/* ================================================================== */
static void fmt_is(const pv_t *p, int slot, int64_t now, const char *want)
{
    char buf[200];
    int  n = pv_format_rec(buf, sizeof(buf), p, slot, now);
    CHECK(strcmp(buf, want) == 0, "slot %d:\n        got  \"%s\"\n        want \"%s\"", slot, buf, want);
    CHECK(n == (int)strlen(want), "slot %d: returned %d, want %d", slot, n, (int)strlen(want));
}

static void test_format(void)
{
    static pv_t     p;
    static pv_log_t good;
    int64_t         t = T0;

    printf("(j) pv_format_rec\n");
    mk(&p, true);
    fmt_is(&p, 0, T0, "vitals: none logged");

    /* the header's example, field by field (no value sits on a rounding tie) */
    strcpy(p.log.rec[0].name, "alice");
    p.log.rec[0].br_eps      = 3;
    p.log.rec[0].br_last     = 14.8f;
    p.log.rec[0].br_mean     = 15.2f;
    p.log.rec[0].samples     = 41;
    p.log.rec[0].hr_eps      = 2;
    p.log.rec[0].hr_last_exp = 71.3f;
    p.log.rec[0].hr_mean_exp = 72.8f;
    p.last_seen_us[0]        = T0;
    fmt_is(&p, 0, T0 + 412400000LL,
           "vitals: 3 episodes, breathing 14.8/min (mean 15.2, 41 samples), heart ~71/min "
           "(mean ~73, 2 episodes) EXPERIMENTAL, last 412 s ago");
    /* the clock behind last_seen: 0 s, not a wrapped number */
    fmt_is(&p, 0, T0 - 5 * S_US,
           "vitals: 3 episodes, breathing 14.8/min (mean 15.2, 41 samples), heart ~71/min "
           "(mean ~73, 2 episodes) EXPERIMENTAL, last 0 s ago");

    /* through the API: one episode, no heart logged -> no heart clause */
    mk(&p, true);
    (void)latch(&p, 0);
    (void)an(&p, t += 2 * S_US, 14.6f, 0.0f, 30.0f);
    (void)an(&p, t += 2 * S_US, 15.0f, 0.0f, 32.0f);
    (void)an(&p, t += 2 * S_US, 15.7f, 0.0f, 34.0f);
    pv_packet(&p, t + S_US, false);
    fmt_is(&p, 0, t + 7500000LL, "vitals: 1 episode, breathing 15.1/min (mean 15.1, 3 samples), last 7 s ago");
    /* one heart episode: singular */
    mk(&p, true);
    (void)latch(&p, 0);
    (void)an(&p, t += 2 * S_US, 15.0f, 70.0f, 30.0f);
    (void)an(&p, t += 2 * S_US, 15.0f, 70.0f, 32.0f);
    (void)an(&p, t += 2 * S_US, 15.0f, 71.0f, 34.0f);
    pv_packet(&p, t + S_US, false);
    fmt_is(&p, 0, t + 500000LL,
           "vitals: 1 episode, breathing 15.0/min (mean 15.0, 3 samples), heart ~70/min "
           "(mean ~70, 1 episode) EXPERIMENTAL, last 0 s ago");

    /* after a reboot: adopted, not credited since */
    good_log(&good);
    mk(&p, true);
    (void)pv_adopt_log(&p, &good, k_good_names);
    fmt_is(&p, 0, T0,
           "vitals: 2 episodes, breathing 14.5/min (mean 15.0, 9 samples), heart ~70/min "
           "(mean ~71, 1 episode) EXPERIMENTAL, not seen since boot");
    fmt_is(&p, 1, T0, "vitals: 1 episode, breathing 12.0/min (mean 12.0, 3 samples), not seen since boot");
    fmt_is(&p, 2, T0, "vitals: none logged");
    fmt_is(&p, 3, T0, "vitals: none logged");      /* named, nothing logged */
    fmt_is(&p, -1, T0, "vitals: none logged");
    fmt_is(&p, PV_SLOTS, T0, "vitals: none logged");
    fmt_is(NULL, 0, T0, "vitals: none logged");

    /* truncation and the size query */
    {
        const char *full = "vitals: 1 episode, breathing 12.0/min (mean 12.0, 3 samples), not seen since boot";
        char small[12];
        int  n = pv_format_rec(small, sizeof(small), &p, 1, T0);
        CHECK(n == (int)strlen(full) && strlen(small) == sizeof(small) - 1 &&
              strncmp(small, full, sizeof(small) - 1) == 0, "truncated: %d '%s'", n, small);
        n = pv_format_rec(NULL, 0, &p, 1, T0);
        CHECK(n == (int)strlen(full), "size query returned %d", n);
    }

    /* the longest line a valid log can produce */
    mk(&p, true);
    strcpy(p.log.rec[0].name, "alice");
    p.log.rec[0].br_eps      = UINT16_MAX;
    p.log.rec[0].hr_eps      = UINT16_MAX;
    p.log.rec[0].samples     = UINT32_MAX;
    p.log.rec[0].br_last     = PV_VAL_MAX;
    p.log.rec[0].br_mean     = PV_VAL_MAX;
    p.log.rec[0].hr_last_exp = PV_VAL_MAX;
    p.log.rec[0].hr_mean_exp = PV_VAL_MAX;
    p.last_seen_us[0]        = 1;
    CHECK(log_ok(&p), "worst-case record invalid");
    fmt_is(&p, 0, 1 + 5000000000LL * S_US,
           "vitals: 65535 episodes, breathing 200.0/min (mean 200.0, 4294967295 samples), heart ~200/min "
           "(mean ~200, 65535 episodes) EXPERIMENTAL, last 4294967295 s ago");
    {
        char buf[200];
        int  n = pv_format_rec(buf, sizeof(buf), &p, 0, 1 + 5000000000LL * S_US);
        printf("    longest line: %d characters (main.c needs a buffer of at least %d)\n", n, n + 1);
        CHECK(n == FMT_MAX, "longest line %d, person_vitals.c documents %d", n, FMT_MAX);
    }
}

/* ================================================================== */
/*  (k) the edge-case table, driven the way main.c drives the module   */
/* ================================================================== */
#define PKT_US      20000LL             /* 50 Hz pings (CONFIG_CSI_PING_INTERVAL_MS 20) */
#define VIT_MIN_US  (20 * S_US)         /* CONFIG_CSI_VITALS_MIN_S default              */
#define VIT_HOP_US  (2 * S_US)          /* VIT_HOP_BINS 20: an analysis every 2 s        */
#define CNT_KEEP_US (60 * S_US)         /* count carry: 2 x CONFIG_CSI_VITALS_WINDOW_S   */

typedef struct {
    pv_t    pv;
    bool    holds;           /* CONFIG_CSI_VITALS_HOLDS_PRESENCE                */
    int64_t t;               /* packet clock                                    */
    bool    present;
    int     cnt_est;         /* s_cnt_est: 0 = no trained estimate this visit   */
    int     cnt_carry;       /* s_cnt_carry (the count carry across a lapse)    */
    int64_t absent_since;
    int64_t still_since;     /* -1 while the gate says MOVING                   */
    bool    began_present;   /* s_still_began_present                           */
    bool    br_valid;        /* vitals_get()->br_valid right now                */
    int64_t last_an;
    float   br, hr;          /* what the vitals module finds; 0 = nothing       */
    int     analyses, credits;
    /* per-packet observations since drv_stats_reset() */
    long    pres_pk, withheld_pk, named_pk, absent_named, withheld_named;
} drv_t;

/* main.c's current_count(present) */
static int drv_count_now(const drv_t *d)
{
    if (!d->present) return 0;
    return (d->cnt_est > 0) ? d->cnt_est : 1;
}

static void drv_stats_reset(drv_t *d)
{
    d->analyses       = 0;
    d->credits        = 0;
    d->pres_pk        = 0;
    d->withheld_pk    = 0;
    d->named_pk       = 0;
    d->absent_named   = 0;
    d->withheld_named = 0;
}

static void drv_init(drv_t *d, bool holds, float br, float hr)
{
    memset(d, 0, sizeof(*d));
    mk(&d->pv, holds);
    d->holds       = holds;
    d->t           = T0;
    d->still_since = -1;
    d->br          = br;
    d->hr          = hr;
}

/* One packet in PH_DETECT, in main.c's order. still = the motion gate's
 * decision; step08 = Step 08's own decision before the breathing hold. */
static void drv_packet(drv_t *d, bool still, bool step08)
{
    d->t += PKT_US;

    /* the gate, and the vitals buffer that follows it */
    if (!still) {
        d->still_since = -1;
        d->br_valid    = false;               /* vitals_reset on a moving packet */
    } else if (d->still_since < 0) {
        d->still_since   = d->t;
        d->began_present = step08;            /* s_still_began_present = new_present */
        d->last_an       = d->t;
    }

    /* BEFORE the transition: pv_packet and the count-carry rule */
    pv_packet(&d->pv, d->t, still);
    if (!d->present && (!still || d->t - d->absent_since > CNT_KEEP_US)) d->cnt_carry = 0;

    /* vitals_push: an analysis every 2 s once 20 s of still data exist */
    bool         done = false;
    vitals_out_t vo;
    memset(&vo, 0, sizeof(vo));
    if (still && d->t - d->still_since >= VIT_MIN_US && d->t - d->last_an >= VIT_HOP_US) {
        float held = (float)(d->t - d->still_since) / 1e6f;
        d->last_an  = d->t;
        done        = true;
        vo.br_valid = d->br > 0.0f;
        vo.state    = vo.br_valid ? VIT_OK : VIT_NO_SIGNAL;
        vo.br_bpm   = vo.br_valid ? d->br : 0.0f;
        vo.br_conf  = vo.br_valid ? 6.0f : 2.0f;
        vo.hr_valid = vo.br_valid && d->hr > 0.0f;
        vo.hr_bpm   = vo.hr_valid ? d->hr : 0.0f;
        vo.still_s  = (held < 51.2f) ? held : 51.2f;
        d->br_valid = vo.br_valid;
    }

    /* the breathing hold */
    bool newp = step08;
    if (d->holds && still && d->began_present && d->br_valid) newp = true;

    /* the transition block */
    if (newp != d->present) {
        d->present = newp;
        if (newp) {
            pv_presence_start(&d->pv);
            if (d->cnt_carry > 0 && d->cnt_est == 0) d->cnt_est = d->cnt_carry;
            d->cnt_carry = 0;
        } else {
            d->cnt_carry    = d->cnt_est;
            d->absent_since = d->t;
            pv_presence_end(&d->pv, d->t);
            d->cnt_est = 0;                   /* analytics_reset(false) */
        }
    }

    /* (count_step / id_step: the scenarios call drv_count / drv_label) */
    if (done) {
        d->analyses++;
        if (pv_analysis(&d->pv, d->t, d->present, drv_count_now(d), &vo)) d->credits++;
    }

    /* what report_build would say for this packet */
    const char *of       = pv_vitals_of(&d->pv, d->present, drv_count_now(d));
    bool        withheld = drv_count_now(d) >= 2 || pv_multiple(&d->pv);
    if (d->present) {
        d->pres_pk++;
        if (withheld) d->withheld_pk++;
        if (of) d->named_pk++;
        if (withheld && of) d->withheld_named++;
    } else if (of) {
        d->absent_named++;
    }
}

/* the gate says MOVING for secs; step08 = whether Step 08 detects it */
static void drv_move(drv_t *d, float secs, bool step08)
{
    int n = (int)(secs * 50.0f + 0.5f);
    for (int i = 0; i < n; i++) drv_packet(d, false, step08);
}

/* gate STILL for secs; Step 08 keeps detecting for the first step08_s of
 * it (its 5 s hold after the last trigger, or a static-branch re-trigger) */
static void drv_still(drv_t *d, float secs, float step08_s)
{
    int n = (int)(secs * 50.0f + 0.5f);
    for (int i = 0; i < n; i++) drv_packet(d, true, (float)i * 0.02f < step08_s);
}

/* id_step's decisive tally (it only runs while present, count <= 1) */
static bool drv_label(drv_t *d, int slot)
{
    CHECK(d->present && drv_count_now(d) <= 1, "scenario bug: a label while absent or counted 2");
    return pv_on_label(&d->pv, slot, slot >= 0 ? k_names[slot] : NULL);
}

/* count_step's estimate change */
static void drv_count(drv_t *d, int est)
{
    if (est == d->cnt_est) return;
    d->cnt_est = est;
    (void)pv_on_count(&d->pv, est);
}

static bool drv_of(const drv_t *d, const char *want)
{
    return is_of(&d->pv, d->present, drv_count_now(d), want);
}

static void test_table(void)
{
    static drv_t d;
    pv_rec_t    *r0 = &d.pv.log.rec[0];
    pv_rec_t    *r1 = &d.pv.log.rec[1];

    printf("(k) the edge-case table, driven in main.c's order (50 Hz packets, analyses every 2 s\n"
           "    from 20 s of stillness, Step 08 hold 5 s, breathing hold on, carry %lld s)\n",
           (long long)(CARRY_US / S_US));

    /* k1: A walks in alone, sits; presence lapses; breathing restores it */
    drv_init(&d, true, 15.2f, 71.4f);
    drv_move(&d, 6.0f, true);
    CHECK(d.present, "k1: not present after walking in");
    CHECK(drv_label(&d, 0), "k1: first decisive tally not reported");
    CHECK(!drv_label(&d, 0) && !drv_label(&d, 0), "k1: later votes reported changes");
    drv_still(&d, 12.0f, 5.0f);
    CHECK(!d.present && pv_attr(&d.pv, NULL) == PV_PERSON && drv_of(&d, NULL),
          "k1 lapse: present %d attr %d", (int)d.present, (int)pv_attr(&d.pv, NULL));
    drv_still(&d, 60.0f, 0.0f);
    CHECK(d.present && drv_of(&d, "alice"), "k1: after the restore present %d, not named alice", (int)d.present);
    CHECK(d.credits == d.analyses && d.credits >= 25, "k1: %d of %d analyses credited", d.credits, d.analyses);
    CHECK(d.absent_named == 0, "k1: named on %ld absent packets", d.absent_named);
    drv_move(&d, 1.0f, true);
    CHECK(r0->br_eps == 1 && r0->hr_eps == 1 && r0->samples == (uint32_t)d.credits,
          "k1 log: %u/%u episodes, %u samples, %d credits", (unsigned)r0->br_eps, (unsigned)r0->hr_eps,
          (unsigned)r0->samples, d.credits);
    CHECK_NEAR(r0->br_mean, 15.2, 1e-3, "k1 breathing mean");
    CHECK_NEAR(r0->hr_mean_exp, 71.4, 1e-3, "k1 heart mean");
    printf("    k1 walk in alone, lapse, breathing restores: %d/%d analyses credited to alice, "
           "episode %.1f/min\n", d.credits, d.analyses, (double)r0->br_mean);

    /* k2: A walks too briefly to be recognised, sits */
    drv_init(&d, true, 15.0f, 0.0f);
    drv_move(&d, 2.0f, true);
    drv_still(&d, 60.0f, 5.0f);
    CHECK(d.present && d.analyses > 0 && d.credits == 0 && pv_attr(&d.pv, NULL) == PV_NONE,
          "k2: present %d, %d credits, attr %d", (int)d.present, d.credits, (int)pv_attr(&d.pv, NULL));
    CHECK(d.named_pk == 0 && d.withheld_pk == 0, "k2: named %ld / withheld %ld packets", d.named_pk,
          d.withheld_pk);

    /* k3: an unknown walker */
    drv_init(&d, true, 15.0f, 0.0f);
    drv_move(&d, 6.0f, true);
    CHECK(pv_on_label(&d.pv, PV_LABEL_UNKNOWN, NULL), "k3: unknown not reported");
    CHECK(!pv_on_label(&d.pv, PV_LABEL_UNKNOWN, NULL), "k3: unknown again reported");
    drv_still(&d, 60.0f, 5.0f);
    CHECK(d.present && d.credits == 0 && pv_attr(&d.pv, NULL) == PV_UNKNOWN && d.named_pk == 0 &&
          d.withheld_pk == 0, "k3: %d credits, attr %d, named %ld, withheld %ld", d.credits,
          (int)pv_attr(&d.pv, NULL), d.named_pk, d.withheld_pk);

    /* k4: A seated, fidgets while present */
    drv_init(&d, true, 15.0f, 0.0f);
    drv_move(&d, 6.0f, true);
    (void)drv_label(&d, 0);
    drv_still(&d, 40.0f, 5.0f);
    {
        int c1 = d.credits;
        drv_move(&d, 1.0f, true);
        CHECK(d.present && r0->br_eps == 1 && r0->samples == (uint32_t)c1 && drv_of(&d, "alice"),
              "k4 fidget: present %d, %u episodes, %u samples of %d", (int)d.present, (unsigned)r0->br_eps,
              (unsigned)r0->samples, c1);
        drv_still(&d, 40.0f, 5.0f);
        CHECK(d.credits > c1 && drv_of(&d, "alice"), "k4: nothing credited after the fidget");
        drv_move(&d, 1.0f, true);
        CHECK(r0->br_eps == 2 && r0->samples == (uint32_t)d.credits, "k4: %u episodes, %u samples of %d",
              (unsigned)r0->br_eps, (unsigned)r0->samples, d.credits);
    }

    /* k5: A seated, fidgets during a lapse -> released for the visit.
     * No breathing is found yet, so nothing holds the presence. */
    for (int k = 0; k < 2; k++) {
        bool loud = (k == 0);                 /* the fidget also re-triggers Step 08 */
        drv_init(&d, true, 0.0f, 0.0f);
        drv_move(&d, 6.0f, true);
        (void)drv_label(&d, 0);
        drv_still(&d, 10.0f, 5.0f);
        CHECK(!d.present && pv_attr(&d.pv, NULL) == PV_PERSON, "k5: no lapse before the fidget");
        drv_move(&d, 0.5f, loud);
        CHECK(pv_attr(&d.pv, NULL) == PV_NONE, "k5 (%s fidget): latch kept", loud ? "loud" : "gentle");
        d.br = 15.0f;                         /* now breathing is found */
        drv_stats_reset(&d);
        drv_still(&d, 60.0f, loud ? 5.0f : 0.0f);
        CHECK(d.credits == 0 && d.named_pk == 0 && d.absent_named == 0,
              "k5 (%s): %d credits, named %ld", loud ? "loud" : "gentle", d.credits, d.named_pk);
        if (loud) CHECK(d.present, "k5 loud: the breathing hold should hold this presence");
        else      CHECK(!d.present, "k5 gentle: a still spell that began absent was held");
    }

    /* k6: lapse, then Step 08 re-triggers while the gate stays still */
    drv_init(&d, true, 0.0f, 0.0f);
    drv_move(&d, 6.0f, true);
    (void)drv_label(&d, 0);
    drv_still(&d, 10.0f, 5.0f);
    CHECK(!d.present, "k6: no lapse");
    drv_still(&d, 1.0f, 1.0f);
    CHECK(d.present && drv_of(&d, "alice"), "k6: re-trigger while still lost the name");

    /* k7: two walk in (count 2), both sit: "multiple" throughout, the
     * count carried across the restore */
    drv_init(&d, true, 15.0f, 70.0f);
    drv_move(&d, 6.0f, true);
    drv_count(&d, 2);
    CHECK(pv_multiple(&d.pv), "k7: count 2 is not MULTIPLE");
    drv_stats_reset(&d);
    drv_still(&d, 60.0f, 5.0f);
    CHECK(d.present && drv_count_now(&d) == 2, "k7: present %d count %d after the restore (carry)",
          (int)d.present, drv_count_now(&d));
    CHECK(d.pres_pk > 0 && d.withheld_pk == d.pres_pk && d.named_pk == 0 && d.withheld_named == 0,
          "k7: %ld of %ld present packets withheld, %ld named", d.withheld_pk, d.pres_pk, d.named_pk);
    CHECK(d.analyses > 0 && d.credits == 0 && rec_zero(r0) && rec_zero(r1), "k7: %d credits", d.credits);

    /* k8: count 2 -> 1 in the same visit: still "multiple" until the room
     * has been empty; then a new visit starts clean */
    drv_init(&d, true, 15.0f, 0.0f);
    drv_move(&d, 6.0f, true);
    drv_count(&d, 2);
    drv_move(&d, 6.0f, true);
    drv_count(&d, 1);
    CHECK(pv_multiple(&d.pv), "k8: count 2 -> 1 released MULTIPLE");
    CHECK(!drv_label(&d, 0) && pv_multiple(&d.pv), "k8: the remaining walker's name broke MULTIPLE");
    drv_stats_reset(&d);
    drv_still(&d, 60.0f, 5.0f);
    CHECK(d.present && d.withheld_pk == d.pres_pk && d.credits == 0,
          "k8: %ld of %ld present packets withheld, %d credits", d.withheld_pk, d.pres_pk, d.credits);
    drv_move(&d, 3.0f, true);                 /* the last one leaves */
    d.br = 0.0f;                              /* an empty room: nothing to find */
    drv_still(&d, 70.0f, 5.0f);
    CHECK(!d.present && pv_attr(&d.pv, NULL) == PV_NONE, "k8: empty for 65 s, attr %d",
          (int)pv_attr(&d.pv, NULL));
    d.br = 15.0f;
    drv_move(&d, 6.0f, true);                 /* a new visit */
    CHECK(drv_label(&d, 0) && pv_attr(&d.pv, NULL) == PV_PERSON, "k8: new visit did not latch alice");
    drv_stats_reset(&d);
    drv_still(&d, 60.0f, 5.0f);
    CHECK(d.credits > 0 && drv_of(&d, "alice"), "k8: new visit: %d credits", d.credits);

    /* k9: A seated, enrolled B walks in and is recognised: two names */
    drv_init(&d, true, 15.0f, 0.0f);
    drv_move(&d, 6.0f, true);
    (void)drv_label(&d, 0);
    drv_still(&d, 40.0f, 5.0f);
    drv_move(&d, 8.0f, true);                 /* B walks in */
    CHECK(r0->br_eps == 1, "k9: alice's episode not committed at B's movement");
    (void)pv_on_label(&d.pv, PV_LABEL_UNKNOWN, NULL);   /* the ring mixes A's and B's votes */
    CHECK(drv_label(&d, 1) && pv_multiple(&d.pv), "k9: bob after alice is not MULTIPLE");
    drv_stats_reset(&d);
    drv_still(&d, 60.0f, 5.0f);
    CHECK(d.withheld_pk == d.pres_pk && d.credits == 0 && r0->br_eps == 1 && rec_zero(r1),
          "k9: %ld/%ld withheld, %d credits, alice %u episodes", d.withheld_pk, d.pres_pk, d.credits,
          (unsigned)r0->br_eps);

    /* k10: A seated, B walks in and sits before being recognised.
     * DOCUMENTED RESIDUAL: invisible - B's breathing mixes into what is
     * credited to A. Asserted as it is, so a change is noticed. */
    drv_init(&d, true, 15.0f, 0.0f);
    drv_move(&d, 6.0f, true);
    (void)drv_label(&d, 0);
    drv_still(&d, 40.0f, 5.0f);
    drv_move(&d, 3.0f, true);                 /* B's walk: too short for a gait window */
    {
        int c1 = d.credits;
        drv_still(&d, 60.0f, 5.0f);
        CHECK(d.credits > c1 && drv_of(&d, "alice") && !pv_multiple(&d.pv),
              "k10: the documented residual changed (credits %d -> %d) - update person_vitals.h/README",
              c1, d.credits);
        printf("    k10 residual (documented): B sat down unrecognised; %d more analyses credited to alice\n",
               d.credits - c1);
    }
    /* ...unless A was in a lapse when B walked in: movement while absent */
    drv_init(&d, true, 0.0f, 0.0f);
    drv_move(&d, 6.0f, true);
    (void)drv_label(&d, 0);
    drv_still(&d, 10.0f, 5.0f);
    drv_move(&d, 3.0f, true);
    CHECK(pv_attr(&d.pv, NULL) == PV_NONE, "k10: B walking in during A's lapse kept A's name");

    /* k11: A walks out; a fan passes for breathing.
     * DOCUMENTED RESIDUAL: A is named for a restore within carry_us of
     * absence; after that the name is released. */
    drv_init(&d, true, 15.0f, 0.0f);
    drv_move(&d, 6.0f, true);
    (void)drv_label(&d, 0);
    drv_still(&d, 30.0f, 5.0f);
    drv_move(&d, 3.0f, true);                 /* alice walks out */
    d.br = 20.0f;                             /* the fan, "confident" at once */
    drv_stats_reset(&d);
    drv_still(&d, 40.0f, 5.0f);
    CHECK(d.present && drv_of(&d, "alice"), "k11: the documented residual changed (present %d, attr %d)",
          (int)d.present, (int)pv_attr(&d.pv, NULL));
    printf("    k11 residual (documented): fan held PRESENT 15 s after alice left; named alice, "
           "%d analyses credited to her\n", d.credits);
    drv_init(&d, true, 15.0f, 0.0f);
    drv_move(&d, 6.0f, true);
    (void)drv_label(&d, 0);
    drv_still(&d, 30.0f, 5.0f);
    drv_move(&d, 3.0f, true);
    d.br = 0.0f;                              /* the fan only "breathes" after 70 s */
    drv_still(&d, 70.0f, 5.0f);
    CHECK(!d.present && pv_attr(&d.pv, NULL) == PV_NONE, "k11: absent 65 s and still latched");
    d.br = 20.0f;
    drv_stats_reset(&d);
    drv_still(&d, 10.0f, 0.0f);
    CHECK(d.present && d.named_pk == 0 && d.credits == 0, "k11: a restore after carry_us named someone");

    /* k12: forget A while A sits */
    drv_init(&d, true, 15.0f, 70.0f);
    drv_move(&d, 6.0f, true);
    (void)drv_label(&d, 0);
    drv_still(&d, 40.0f, 5.0f);
    drv_move(&d, 1.0f, true);
    drv_still(&d, 30.0f, 5.0f);
    pv_saved(&d.pv, d.t, true);
    CHECK(r0->br_eps == 1 && d.pv.acc_br_n > 0, "k12 setup");
    pv_slot_gone(&d.pv, 0);                   /* cmd_forget, then main.c saves at once */
    CHECK(drv_of(&d, NULL) && rec_zero(r0) && d.pv.acc_slot == -1 && d.pv.dirty,
          "k12: forget: named %d, record zero %d, pending %d, dirty %d", (int)!drv_of(&d, NULL),
          (int)rec_zero(r0), (int)d.pv.acc_slot, (int)d.pv.dirty);
    drv_stats_reset(&d);
    drv_still(&d, 20.0f, 0.0f);
    drv_move(&d, 1.0f, true);
    CHECK(d.credits == 0 && rec_zero(r0), "k12: credited after forget");

    /* k13: enroll / count-train starts: released, nobody named during it */
    drv_init(&d, true, 15.0f, 0.0f);
    drv_move(&d, 6.0f, true);
    (void)drv_label(&d, 0);
    drv_still(&d, 40.0f, 5.0f);
    pv_slot_gone(&d.pv, 3);                   /* cmd_enroll into the free slot 3 */
    pv_release(&d.pv);
    CHECK(pv_attr(&d.pv, NULL) == PV_NONE && r0->br_eps == 1,
          "k13 enroll another slot: attr %d, alice's own pending must be committed (%u)",
          (int)pv_attr(&d.pv, NULL), (unsigned)r0->br_eps);
    drv_stats_reset(&d);
    drv_still(&d, 20.0f, 0.0f);               /* no votes during a session */
    CHECK(d.credits == 0 && d.named_pk == 0, "k13: named during the session");
    drv_init(&d, true, 15.0f, 0.0f);
    drv_move(&d, 6.0f, true);
    (void)drv_label(&d, 0);
    drv_still(&d, 40.0f, 5.0f);
    pv_slot_gone(&d.pv, 0);                   /* re-enroll alice's own slot */
    pv_release(&d.pv);
    CHECK(pv_attr(&d.pv, NULL) == PV_NONE && rec_zero(r0), "k13 re-enroll: alice's data kept");
    drv_init(&d, true, 15.0f, 0.0f);
    drv_move(&d, 6.0f, true);
    (void)drv_label(&d, 0);
    drv_still(&d, 40.0f, 5.0f);
    pv_release(&d.pv);                        /* cmd_count_train */
    CHECK(pv_attr(&d.pv, NULL) == PV_NONE && r0->br_eps == 1 && drv_of(&d, NULL), "k13 count-train");

    /* k14: recal / no-CSI / new baseline (analytics_reset(true)) / boot */
    drv_init(&d, true, 15.0f, 0.0f);
    drv_move(&d, 6.0f, true);
    (void)drv_label(&d, 0);
    drv_still(&d, 40.0f, 5.0f);
    pv_release(&d.pv);
    CHECK(pv_attr(&d.pv, NULL) == PV_NONE && r0->br_eps == 1 && !d.pv.absent, "k14 analytics_reset(true)");
    pv_init(&d.pv, NULL);
    CHECK(pv_attr(&d.pv, NULL) == PV_NONE && rec_zero(r0), "k14 boot");

    /* k15: counter untrained or disabled (count never above 1).
     * DOCUMENTED: two unrecognised seated people get one rate, nobody
     * named; "multiple" comes only from two names. */
    drv_init(&d, true, 15.0f, 0.0f);
    drv_move(&d, 6.0f, true);                 /* two people, unrecognised, uncounted */
    drv_stats_reset(&d);
    drv_still(&d, 60.0f, 5.0f);
    CHECK(d.present && d.withheld_pk == 0 && d.named_pk == 0 && d.credits == 0,
          "k15 unrecognised pair: withheld %ld, named %ld, credits %d", d.withheld_pk, d.named_pk, d.credits);
    drv_init(&d, true, 15.0f, 0.0f);
    drv_move(&d, 6.0f, true);
    (void)drv_label(&d, 0);
    drv_move(&d, 6.0f, true);
    CHECK(drv_label(&d, 1) && pv_multiple(&d.pv), "k15: two names are not MULTIPLE");
    drv_stats_reset(&d);
    drv_still(&d, 60.0f, 5.0f);
    CHECK(d.withheld_pk == d.pres_pk && d.credits == 0, "k15 two names: %ld/%ld withheld", d.withheld_pk,
          d.pres_pk);

    CHECK(log_ok(&d.pv), "log invalid after (k)");
}

/* ================================================================== */
/*  (l) random event sequences                                         */
/* ================================================================== */

/* NULL if every invariant holds, else what broke. */
static const char *inv_fail(const pv_t *p)
{
    if (p->attr != PV_NONE && p->attr != PV_UNKNOWN && p->attr != PV_PERSON && p->attr != PV_MULTIPLE) {
        return "attr out of range";
    }
    if (p->attr == PV_PERSON) {
        if (p->slot < 0 || p->slot >= PV_SLOTS) return "PERSON without a slot";
        if (p->slot != p->first_slot) return "PERSON is not the first name";
        if (p->name[0] == '\0') return "PERSON without a name";
    } else if (p->slot != -1) {
        return "slot set outside PERSON";
    }
    if (p->first_slot < -1 || p->first_slot >= PV_SLOTS) return "first_slot out of range";
    if (p->first_slot < 0 && p->name[0] != '\0') return "a name without a first slot";
    if (memchr(p->name, '\0', PV_NAME_LEN) == NULL) return "latched name unterminated";
    if (p->acc_slot < -1 || p->acc_slot >= PV_SLOTS) return "acc_slot out of range";
    if (p->acc_slot < 0 && (p->acc_br_n != 0 || p->acc_hr_n != 0)) return "samples with nothing pending";
    if (p->acc_slot >= 0 && p->acc_br_n == 0) return "a pending episode without samples";
    if (p->acc_hr_n > p->acc_br_n) return "more heart than breathing samples pending";
    if (p->acc_slot >= 0 &&
        (p->attr != PV_PERSON || p->acc_slot != p->slot || strcmp(p->acc_name, p->name) != 0)) {
        return "a pending episode that is not the latched person's";
    }
    if (!pv_log_valid(&p->log, sizeof(p->log))) return "the log no longer passes pv_log_valid";
    if ((pv_vitals_of(p, true, 1) != NULL) != (p->attr == PV_PERSON)) return "vitals_of disagrees with the latch";
    if (pv_vitals_of(p, true, 2) != NULL || pv_vitals_of(p, false, 1) != NULL) return "named with count 2 or absent";
    if (pv_multiple(p) != (p->attr == PV_MULTIPLE)) return "pv_multiple disagrees with the latch";
    return NULL;
}

static void test_fuzz(void)
{
    static const char *const fz_names[PV_SLOTS] = { "alice", "bob", "carol", "dave" };
    static const char *const odd_names[]        = { "bad name", "", "abcdefghijklmnop", "alicia", "bob" };
    static const int         labels[]           = { -3, -2, -1, -1, -1, 0, 0, 1, 1, 2, 3, 4 };
    static pv_t              p;
    static pv_log_t          copy;
    float                    vals[10];
    long                     events = 0, credits = 0, commits = 0;

    vals[0] = 15.0f;
    vals[1] = 12.5f;
    vals[2] = 71.0f;
    vals[3] = PV_VAL_MAX;
    vals[4] = 0.0f;
    vals[5] = -1.0f;
    vals[6] = nan_f();
    vals[7] = inf_f();
    vals[8] = 250.0f;
    vals[9] = 0.001f;

    printf("(l) random event sequences: the latch stays consistent, the log stays valid\n");
    for (int c = 0; c < 4; c++) {
        pv_cfg_t cfg;
        cfg.holds_presence = (c & 1) == 0;
        cfg.carry_us       = (c < 2) ? CARRY_US : 5 * S_US;
        pv_init(&p, &cfg);
        rng_seed(7000u + (uint32_t)c);

        int64_t     t       = T0;
        float       still_s = 0.0f;
        const char *why     = NULL;
        const char *ev      = "";
        int         i;
        for (i = 0; i < 60000 && !why; i++) {
            uint32_t r          = rng_u32() % 100u;
            long     eps_before = 0;
            for (int s = 0; s < PV_SLOTS; s++) eps_before += p.log.rec[s].br_eps;
            t += (int64_t)(rng_u32() % 3000000u);
            if (r < 15) {
                /* mostly the same walker voting again, as in a real visit;
                 * otherwise any label, now and then with a bad name */
                int lab = labels[rng_u32() % (sizeof(labels) / sizeof(labels[0]))];
                if (p.first_slot >= 0 && rng_u32() % 10u < 7u) lab = p.first_slot;
                const char *nm = (lab >= 0 && lab < PV_SLOTS) ? fz_names[lab] : NULL;
                if (rng_u32() % 20u == 0) nm = odd_names[rng_u32() % (sizeof(odd_names) / sizeof(odd_names[0]))];
                (void)pv_on_label(&p, lab, nm);
                ev = "label";
            } else if (r < 20) {
                /* mostly one mover, now and then two or three */
                (void)pv_on_count(&p, (rng_u32() % 5u == 0) ? 2 + (int)(rng_u32() % 2u) : (int)(rng_u32() % 2u));
                ev = "count";
            } else if (r < 24) {
                pv_presence_end(&p, t);
                ev = "presence end";
            } else if (r < 28) {
                pv_presence_start(&p);
                ev = "presence start";
            } else if (r < 58) {
                bool still = rng_u32() % 8u != 0;
                pv_packet(&p, t, still);
                if (!still) still_s = 0.0f;
                ev = "packet";
            } else if (r < 80) {
                vitals_out_t v;
                uint32_t     k = rng_u32() % 20u;
                memset(&v, 0, sizeof(v));
                if (k < 14)       still_s = (still_s + 2.0f < 51.2f) ? still_s + 2.0f : 51.2f;
                else if (k == 14) still_s = 20.0f;                       /* buffer restart */
                else if (k == 15) still_s = vals[rng_u32() % 10u];       /* garbage */
                v.state    = (rng_u32() % 8u == 0) ? (vitals_state_t)(rng_u32() % 4u) : VIT_OK;
                v.br_valid = rng_u32() % 8u != 0;
                v.br_bpm   = (rng_u32() % 4u == 0) ? vals[rng_u32() % 10u] : 15.0f;
                v.hr_valid = rng_u32() % 2u != 0;
                v.hr_bpm   = (rng_u32() % 3u == 0) ? vals[rng_u32() % 10u] : 72.0f;
                v.still_s  = still_s;
                bool pres  = rng_u32() % 10u != 0;
                int  cnt   = (rng_u32() % 10u == 0) ? 2 : (int)(rng_u32() % 2u);
                if (pv_analysis(&p, t, pres, cnt, &v)) credits++;
                ev = "analysis";
            } else if (r < 83) {
                pv_slot_gone(&p, (int)(rng_u32() % 6u) - 2);
                ev = "slot gone";
            } else if (r < 86) {
                pv_log_clear(&p, (int)(rng_u32() % 6u) - 2);
                ev = "log clear";
            } else if (r < 89) {
                pv_release(&p);
                ev = "release";
            } else if (r < 94) {
                if (pv_save_due(&p, t, 300 * S_US)) pv_saved(&p, t, rng_u32() % 4u != 0);
                ev = "save";
            } else if (r < 99) {
                char buf[200];
                int  slot = (int)(rng_u32() % 6u) - 1;
                int  n    = pv_format_rec(buf, sizeof(buf), &p, slot, t);
                if (n != (int)strlen(buf) || n > FMT_MAX) why = "pv_format_rec length";
                ev = "format";
            } else {
                const char *nm[PV_SLOTS];
                for (int s = 0; s < PV_SLOTS; s++) {
                    nm[s] = (rng_u32() % 4u == 0) ? odd_names[rng_u32() % 5u] : fz_names[s];
                }
                copy = p.log;
                (void)pv_adopt_log(&p, &copy, nm);
                ev = "adopt";
            }
            long eps_after = 0;
            for (int s = 0; s < PV_SLOTS; s++) eps_after += p.log.rec[s].br_eps;
            if (eps_after > eps_before) commits++;
            if (!why) why = inv_fail(&p);
            events++;
        }
        CHECK(why == NULL, "config %d, event %d (%s): %s", c, i - 1, ev, why ? why : "");
    }
    printf("    %ld events over 4 configurations: %ld credits, %ld episodes committed; invariants\n"
           "    checked after every event\n", events, credits, commits);
    CHECK(credits > 1000 && commits > 50, "the fuzz barely exercised crediting: %ld credits, %ld commits",
          credits, commits);
}

int main(void)
{
    printf("test_pvitals: person_vitals.c on EVENT SEQUENCES (no signal).\n"
           "PV_EP_MIN %d, PV_MEAN_CAP %d, PV_VAL_MAX %.0f, carry %lld s.\n",
           PV_EP_MIN, PV_MEAN_CAP, (double)PV_VAL_MAX, (long long)(CARRY_US / S_US));
    test_log_valid();
    test_latch();
    test_presence();
    test_credit();
    test_episodes();
    test_log_numbers();
    test_slot_gone_vs_clear();
    test_adopt();
    test_save();
    test_format();
    test_table();
    test_fuzz();
    return test_summary("test_pvitals");
}
