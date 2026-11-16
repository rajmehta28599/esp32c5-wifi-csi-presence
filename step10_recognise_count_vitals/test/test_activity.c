/*
 * test_activity.c - activity.c on scripted inputs with known answers.
 *
 * activity.c sees no CSI: it reads main.c's per-packet decisions (present
 * after the breathing hold, the motion gate's still / hf, the hold flag).
 * So these tests script those decisions directly, packet by packet on a
 * microsecond clock, and check the states and episode seconds that must
 * follow from activity.h's rules. test_e2e.c (e) runs the same module on
 * decisions that came through the real front end, gate and vitals.
 *
 * Known answers (the plan's list; each can fail):
 *   1 present, gate still, hf 1.0 for 60 s       -> still from 10 s, not before
 *   2 nobody present                             -> empty from ~5 s, nothing else
 *   3 walking, only 30% of packets gate-MOVING   -> moving on EVERY packet once
 *     in 3 Hz bursts, mean hf below the            settled (a per-packet state
 *     restless band                                would be wrong on 70%)
 *   4 seated, hf 3.0 in 1 s of every 4           -> restless on every packet
 *   5 hysteresis: a 1 s present blip in an empty -> stays empty; a 2 s one does
 *     room; a 1 s spike while still                change it (the hold is 2 s,
 *                                                  not "never"); still stays still
 *   6 the same script at 20, 50 and 100 packets/s -> the same state every second
 *     (a packet-count bin would differ)            and the same episode seconds
 *   7 a 3 s timestamp gap (and one going back)   -> no state right after it, and
 *                                                  nothing from before it leaks
 *   8 episodes: exact seconds, and per label; a lapse the breathing hold
 *     undoes is one episode (also when its restoring second holds a
 *     gate-MOVING packet, and when Step 08 came back first); a walk-in
 *     during a lapse is a new one; the carry limit, also at its edge; no
 *     bridging when the hold is compiled out; act_reset ends an episode.
 *   9 the edges: a second exactly 1/4 gate-MOVING is ACTIVE, 24/100 is not;
 *     mean hf exactly restless_lo is FIDGET, exactly the gate threshold is
 *     ACTIVE; a walk that ends leaves "moving" 5 s later, never through
 *     "restless", also when Step 08's hold and then an empty room follow;
 *     one ACTIVE and one FIDGET second make "restless"
 *  10 the gate's floor unknown (hf -1): an empty room reads empty, but once
 *     someone is PRESENT the state clears to null after the 2 s hold -
 *     "empty" does not stay
 */

#include <string.h>
#include <stdbool.h>

#include "activity.h"
#include "test_util.h"

#define THR      4.0f        /* the gate's threshold (Kconfig 400 / 100)        */
#define LO       2.5f        /* CSI_ACTIVITY_RESTLESS_X100 250 / 100            */
#define CARRY_US 60000000LL  /* main.c CSI_CARRY_US: 2 x CSI_VITALS_WINDOW_S 30 */

typedef enum {
    M_ABSENT,                /* nobody, gate still, hf 1.0                      */
    M_ABSENT_MOVING,         /* nobody PRESENT, gate MOVING (someone passing)   */
    M_QUIET,                 /* present (Step 08), gate still, hf 1.0           */
    M_HELD,                  /* present by the breathing hold, gate still       */
    M_WALK,                  /* present; 3 Hz bursts, 30% of packets MOVING     */
    M_FIDGET,                /* present, gate still; hf 3.0 in 1 s of every 4   */
    M_SPIKE,                 /* present, every packet MOVING, hf 8              */
    M_DUTY25,                /* present, hf 1.0; MOVING on the first 250 ms     */
    M_DUTY24,                /*   ... on the first 240 ms of each second        */
    M_HF_LO,                 /* present, gate still, hf exactly LO              */
    M_HF_THR,                /* present, gate still, hf exactly THR             */
    M_UNK_ABSENT,            /* nobody; gate floor unknown (hf -1, MOVING)      */
    M_UNK_PRESENT,           /* present (Step 08); gate floor unknown           */
} mode_t_;

static int64_t s_t;          /* the packet clock, us                            */
static long    s_seen[ACT_NSTATES];

static void reset_clock(void)
{
    s_t = 0;
}

static void packet_of(mode_t_ m, int64_t t, bool *present, bool *still, float *hf, bool *held)
{
    *present = true;
    *still   = true;
    *hf      = 1.0f;
    *held    = false;
    switch (m) {
    case M_ABSENT:
        *present = false;
        break;
    case M_ABSENT_MOVING:
        *present = false;
        *still   = false;
        *hf      = 6.0f;
        break;
    case M_QUIET:
        break;
    case M_HELD:
        *held = true;
        break;
    case M_WALK: {
        bool burst = (t % 333333) < 100000;      /* 3 Hz, 30% duty */
        *still = !burst;
        *hf    = burst ? 5.0f : 1.2f;            /* mean ~2.3: below LO */
        break;
    }
    case M_FIDGET:
        *hf = ((t / 1000000) % 4 == 0) ? 3.0f : 1.0f;
        break;
    case M_SPIKE:
        *still = false;
        *hf    = 8.0f;
        break;
    case M_DUTY25:
        *still = (t % 1000000) >= 250000;        /* at 100/s: 25 of 100 packets */
        break;
    case M_DUTY24:
        *still = (t % 1000000) >= 240000;        /* at 100/s: 24 of 100 packets */
        break;
    case M_HF_LO:
        *hf = LO;                                /* sums and means exactly in float */
        break;
    case M_HF_THR:
        *hf = THR;
        break;
    case M_UNK_ABSENT:
        *present = false;
        *still   = false;
        *hf      = -1.0f;
        break;
    case M_UNK_PRESENT:
        *still = false;
        *hf    = -1.0f;
        break;
    }
}

/* `seconds` of mode m at `rate` packets/s. From settle_s into the segment,
 * the state after every packet is tallied in s_seen. */
static void seg(act_t *a, mode_t_ m, double seconds, double rate, double settle_s)
{
    int64_t period = (int64_t)(1e6 / rate + 0.5);
    int64_t t0 = s_t, t_end = s_t + (int64_t)(seconds * 1e6 + 0.5);
    memset(s_seen, 0, sizeof(s_seen));
    while (s_t < t_end) {
        bool  p, st, h;
        float hf;
        packet_of(m, s_t, &p, &st, &hf, &h);
        act_push(a, s_t, p, st, hf, h);
        if ((double)(s_t - t0) >= settle_s * 1e6) s_seen[act_state(a)]++;
        s_t += period;
    }
}

static long seen_total(void)
{
    long n = 0;
    for (int i = 0; i < ACT_NSTATES; i++) n += s_seen[i];
    return n;
}

static void start(act_t *a, int64_t carry_us)
{
    act_cfg_t cfg = { THR, LO, carry_us };
    act_init(a, &cfg);
    reset_clock();
}

static bool ep_sum_ok(const act_ep_t *e)
{
    uint32_t s = 0;
    for (int i = 0; i < ACT_NSTATES; i++) s += e->st_s[i];
    return s == e->total_s;
}

static const char *nm(act_state_t s)
{
    const char *n = act_state_name(s);
    return n ? n : "null";
}

/* ---- 1 ---- */
static void test_still(void)
{
    static act_t a;
    start(&a, CARRY_US);
    seg(&a, M_QUIET, 9.99, 50.0, 0.0);
    CHECK(s_seen[ACT_NONE] == seen_total(), "1: a state before 10 s of data (%ld of %ld packets)",
          seen_total() - s_seen[ACT_NONE], seen_total());
    seg(&a, M_QUIET, 50.01, 50.0, 0.1);
    CHECK(s_seen[ACT_STILL] == seen_total(), "1: still on %ld of %ld packets after 10 s",
          s_seen[ACT_STILL], seen_total());
    CHECK(act_state(&a) == ACT_STILL && act_state_s(&a) == 51,
          "1: at 60 s: %s for %u s, want still for 51 s", nm(act_state(&a)), (unsigned)act_state_s(&a));
    printf("    1 still: still from 10 s, %u s in it at 60 s\n", (unsigned)act_state_s(&a));
}

/* ---- 2 ---- */
static void test_empty(void)
{
    static act_t a;
    uint32_t     ep = 0;
    start(&a, CARRY_US);
    seg(&a, M_ABSENT, 4.99, 50.0, 0.0);
    CHECK(s_seen[ACT_NONE] == seen_total(), "2: a state before 5 absent seconds");
    seg(&a, M_ABSENT, 25.01, 50.0, 0.1);
    CHECK(s_seen[ACT_EMPTY] == seen_total(), "2: empty on %ld of %ld packets after 5 s",
          s_seen[ACT_EMPTY], seen_total());
    CHECK(!act_episode_s(&a, &ep) && a.n_episodes == 0, "2: an episode in an empty room");
    printf("    2 empty: empty from 5 s, no episode\n");
}

/* ---- 3 ---- */
static void test_walk_duty(void)
{
    static act_t a;
    start(&a, CARRY_US);
    seg(&a, M_ABSENT, 10.0, 50.0, 0.0);
    seg(&a, M_WALK, 5.0, 50.0, 0.0);
    long first5_moving = s_seen[ACT_MOVING];
    seg(&a, M_WALK, 55.0, 50.0, 0.0);
    CHECK(s_seen[ACT_MOVING] == seen_total(),
          "3: moving on %ld of %ld packets of a walk whose gate says MOVING on 30%% of them "
          "(still %ld, restless %ld, empty %ld)", s_seen[ACT_MOVING], seen_total(),
          s_seen[ACT_STILL], s_seen[ACT_RESTLESS], s_seen[ACT_EMPTY]);
    CHECK(first5_moving > 0, "3: not moving within 5 s of the walk-in");
    printf("    3 walk, 30%% of packets MOVING in 3 Hz bursts: moving on every packet from 5 s on\n");
}

/* ---- 4 ---- */
static void test_fidget(void)
{
    static act_t a;
    start(&a, CARRY_US);
    seg(&a, M_QUIET, 20.0, 50.0, 0.0);
    CHECK(act_state(&a) == ACT_STILL, "4: before the fidgeting %s, want still", nm(act_state(&a)));
    seg(&a, M_FIDGET, 60.0, 50.0, 12.0);
    CHECK(s_seen[ACT_RESTLESS] == seen_total(), "4: restless on %ld of %ld packets (still %ld, moving %ld)",
          s_seen[ACT_RESTLESS], seen_total(), s_seen[ACT_STILL], s_seen[ACT_MOVING]);
    /* and back: 10 quiet seconds clear the window, then the 2 s hold */
    seg(&a, M_QUIET, 14.0, 50.0, 13.0);
    CHECK(s_seen[ACT_STILL] == seen_total(), "4: not still again 13 s after the fidgeting stopped");
    printf("    4 hf 3.0 in 1 s of every 4: restless throughout, still again after it stops\n");
}

/* ---- 5 ---- */
static void test_hysteresis(void)
{
    static act_t a;

    /* (a) a 1 s present blip in an empty room: the raw state is "still"
     * for that one second only */
    start(&a, CARRY_US);
    seg(&a, M_ABSENT, 20.0, 50.0, 0.0);
    CHECK(act_state(&a) == ACT_EMPTY, "5a: %s before the blip", nm(act_state(&a)));
    seg(&a, M_QUIET, 1.0, 50.0, 0.0);
    seg(&a, M_ABSENT, 20.0, 50.0, 0.0);
    CHECK(s_seen[ACT_EMPTY] == seen_total(), "5a: a 1 s blip changed the state (%ld of %ld packets empty)",
          s_seen[ACT_EMPTY], seen_total());

    /* (b) the same blip for 2 s does change it: the hold is ACT_HOLD_S */
    seg(&a, M_QUIET, 2.0, 50.0, 0.0);
    seg(&a, M_ABSENT, 0.5, 50.0, 0.0);
    CHECK(act_state(&a) == ACT_STILL, "5b: after a 2 s blip %s, want still", nm(act_state(&a)));
    seg(&a, M_ABSENT, 10.0, 50.0, 0.0);
    CHECK(act_state(&a) == ACT_EMPTY, "5b: %s 10 s after the blip, want empty", nm(act_state(&a)));

    /* (c) a 1 s spike while still */
    start(&a, CARRY_US);
    seg(&a, M_QUIET, 30.0, 50.0, 0.0);
    seg(&a, M_SPIKE, 1.0, 50.0, 0.0);
    seg(&a, M_QUIET, 30.0, 50.0, 0.0);
    CHECK(s_seen[ACT_STILL] == seen_total(), "5c: a 1 s spike changed still (%ld of %ld packets still)",
          s_seen[ACT_STILL], seen_total());

    /* (d) a 1 s pause in a walk */
    start(&a, CARRY_US);
    seg(&a, M_WALK, 20.0, 50.0, 0.0);
    seg(&a, M_QUIET, 1.0, 50.0, 0.0);
    long dip = s_seen[ACT_MOVING], dip_n = seen_total();
    seg(&a, M_WALK, 10.0, 50.0, 0.0);
    CHECK(dip == dip_n && s_seen[ACT_MOVING] == seen_total(), "5d: a 1 s pause interrupted moving");
    printf("    5 hysteresis: a 1 s blip / spike / pause changes nothing, a 2 s blip does\n");
}

/* ---- 6 ---- */
#define SCRIPT_S 150
/* jitter: +/-2 ms on every arrival and 3% of packets lost, as test_e2e's radio */
static void script_states(double rate, bool jitter, act_state_t out[SCRIPT_S], act_ep_t *ep,
                          uint32_t *n_ep)
{
    static const struct { mode_t_ m; double s; } k_script[] = {
        { M_ABSENT, 10 }, { M_WALK, 20 }, { M_FIDGET, 20 }, { M_HELD, 30 }, { M_WALK, 5 },
        { M_ABSENT, 65 },
    };
    static act_t a;
    int64_t period = (int64_t)(1e6 / rate + 0.5);
    int     k = 0, sec = 0;
    double  until = 0.0;
    start(&a, CARRY_US);
    for (size_t i = 0; i < sizeof(k_script) / sizeof(k_script[0]); i++) {
        until += k_script[i].s;
        while ((double)s_t < until * 1e6) {
            bool    p, st, h;
            float   hf;
            int64_t t = s_t;
            s_t += period;
            k++;
            if (jitter) {
                if (rng_uniform() < 0.03f) continue;
                t += (int64_t)((rng_uniform() * 2.0f - 1.0f) * 2000.0f);
                if (t < 0) t = 0;
            }
            packet_of(k_script[i].m, t, &p, &st, &hf, &h);
            act_push(&a, t, p, st, hf, h);
            /* the state as it stands after the first packet of each whole second */
            if (t >= (int64_t)sec * 1000000 && sec < SCRIPT_S) out[sec++] = act_state(&a);
        }
    }
    *ep   = a.last;
    *n_ep = a.n_episodes;
}

static void test_rate(void)
{
    static act_state_t s50[SCRIPT_S], s100[SCRIPT_S], s20[SCRIPT_S], sj[SCRIPT_S];
    act_ep_t e50, e100, e20, ej;
    uint32_t n50, n100, n20, nj;
    script_states(50.0, false, s50, &e50, &n50);
    script_states(100.0, false, s100, &e100, &n100);
    script_states(20.0, false, s20, &e20, &n20);
    rng_seed(6u);
    script_states(50.0, true, sj, &ej, &nj);
    int d100 = 0, d20 = 0, first = -1;
    for (int i = 0; i < SCRIPT_S; i++) {
        if (s50[i] != s100[i]) { d100++; if (first < 0) first = i; }
        if (s50[i] != s20[i])  { d20++;  if (first < 0) first = i; }
    }
    CHECK(d100 == 0 && d20 == 0, "6: states differ from 50 Hz in %d s at 100 Hz, %d s at 20 Hz (first at %d s)",
          d100, d20, first);
    CHECK(n50 == 1 && n100 == 1 && n20 == 1 && e50.total_s == 75 && e100.total_s == 75 &&
          e20.total_s == 75, "6: episodes %u/%u/%u of %u/%u/%u s, want one of 75 s each",
          (unsigned)n50, (unsigned)n100, (unsigned)n20, (unsigned)e50.total_s,
          (unsigned)e100.total_s, (unsigned)e20.total_s);
    CHECK(memcmp(&e50, &e100, sizeof(e50)) == 0 && memcmp(&e50, &e20, sizeof(e50)) == 0,
          "6: per-state episode seconds differ between rates");
    /* Jitter moves packets across a second's edge and drops some, so a
     * state may change a second earlier or later - not more. */
    int dj = 0;
    for (int i = 0; i < SCRIPT_S; i++) dj += (s50[i] != sj[i]) ? 1 : 0;
    CHECK(dj <= 6 && nj == 1 && ej.total_s >= 74 && ej.total_s <= 76,
          "6: with jitter and drops %d s differ, %u episode(s) of %u s", dj, (unsigned)nj,
          (unsigned)ej.total_s);
    /* the script's states, so a reader sees what was compared */
    CHECK(s50[8] == ACT_EMPTY && s50[25] == ACT_MOVING && s50[45] == ACT_RESTLESS &&
          s50[75] == ACT_STILL && s50[83] == ACT_MOVING && s50[149] == ACT_EMPTY,
          "6: script at 8/25/45/75/83/149 s: %s %s %s %s %s %s", nm(s50[8]), nm(s50[25]),
          nm(s50[45]), nm(s50[75]), nm(s50[83]), nm(s50[149]));
    printf("    6 rate: the same state every second at 20, 50 and 100 packets/s (%d s differ with "
           "jitter + drops); episode 75 s (still %u, restless %u, moving %u, empty %u, none %u)\n", dj,
           (unsigned)e50.st_s[ACT_STILL], (unsigned)e50.st_s[ACT_RESTLESS],
           (unsigned)e50.st_s[ACT_MOVING], (unsigned)e50.st_s[ACT_EMPTY], (unsigned)e50.st_s[ACT_NONE]);
}

/* ---- 7 ---- */
static void test_gap(void)
{
    static act_t a;
    uint32_t ep = 0;
    start(&a, CARRY_US);
    seg(&a, M_WALK, 20.0, 50.0, 0.0);
    CHECK(act_state(&a) == ACT_MOVING, "7: %s before the gap", nm(act_state(&a)));
    uint32_t before = a.cur.total_s;
    s_t += 3000000;                              /* no packet for 3 s */
    seg(&a, M_QUIET, 0.02, 50.0, 0.0);
    CHECK(act_state(&a) == ACT_NONE && act_state_s(&a) == 0,
          "7: %s right after a 3 s gap, want null", nm(act_state(&a)));
    seg(&a, M_QUIET, 9.97, 50.0, 0.0);
    CHECK(s_seen[ACT_NONE] == seen_total(),
          "7: a state within 10 s of the gap (moving %ld, restless %ld packets: the walk leaked)",
          s_seen[ACT_MOVING], s_seen[ACT_RESTLESS]);
    seg(&a, M_QUIET, 5.0, 50.0, 0.5);
    CHECK(s_seen[ACT_STILL] == seen_total(), "7: not still 10.5 s after the gap");
    /* the episode carries on across the gap; the 3 s are not counted */
    CHECK(act_episode_s(&a, &ep) && a.n_episodes == 0 && ep == before + 14,
          "7: episode %u s (%u ended), want %u + 14", (unsigned)ep, (unsigned)a.n_episodes,
          (unsigned)before);
    /* a timestamp that goes backwards restarts the same way */
    s_t -= 5000000;
    seg(&a, M_QUIET, 0.02, 50.0, 0.0);
    CHECK(act_state(&a) == ACT_NONE, "7: %s after the clock went back, want null", nm(act_state(&a)));
    printf("    7 gap: null after a 3 s gap, still 10 s later, nothing of the walk leaked; "
           "episode %u s across it\n", (unsigned)ep);
}

/* ---- 8 ---- */
static void test_episodes(void)
{
    static act_t a;
    uint32_t ep = 0;

    /* exact seconds; frozen during the lapse; ended by the carry limit */
    start(&a, CARRY_US);
    CHECK(!act_episode_s(&a, &ep), "8a: an episode before any packet");
    seg(&a, M_ABSENT, 10.0, 50.0, 0.0);
    seg(&a, M_WALK, 30.0, 50.0, 0.0);
    CHECK(act_episode_s(&a, &ep) && ep == 29, "8a: episode_s %u after 30 s present, want 29 "
          "(the 30th second completes with the next packet)", (unsigned)ep);
    seg(&a, M_ABSENT, 20.0, 50.0, 0.0);
    CHECK(act_episode_s(&a, &ep) && ep == 30 && a.ep_phase == ACT_EP_LAPSE,
          "8a: during the lapse episode_s %u phase %d, want 30 (frozen), lapse", (unsigned)ep,
          (int)a.ep_phase);
    seg(&a, M_ABSENT, 50.0, 50.0, 0.0);
    CHECK(!act_episode_s(&a, &ep) && a.n_episodes == 1 && a.last.total_s == 30 && ep_sum_ok(&a.last),
          "8a: after 70 s absent: %u ended, last %u s (want 1 of 30 s)", (unsigned)a.n_episodes,
          (unsigned)a.last.total_s);
    /* labelled by the state, not the raw state: the first two walk seconds
     * are the 2 s hold, still reported "empty" */
    CHECK(a.last.st_s[ACT_EMPTY] == 2 && a.last.st_s[ACT_MOVING] == 28,
          "8a: labels empty %u moving %u, want 2 and 28", (unsigned)a.last.st_s[ACT_EMPTY],
          (unsigned)a.last.st_s[ACT_MOVING]);
    printf("    8a 30 s present: episode 30 s (empty %u, moving %u, other %u), ended by the 60 s carry\n",
           (unsigned)a.last.st_s[ACT_EMPTY], (unsigned)a.last.st_s[ACT_MOVING],
           (unsigned)(a.last.total_s - a.last.st_s[ACT_EMPTY] - a.last.st_s[ACT_MOVING]));

    /* Step 08's hold lapses, breathing restores it: one episode */
    start(&a, CARRY_US);
    seg(&a, M_ABSENT, 10.0, 50.0, 0.0);
    seg(&a, M_WALK, 15.0, 50.0, 0.0);
    seg(&a, M_QUIET, 5.0, 50.0, 0.0);
    seg(&a, M_ABSENT, 10.0, 50.0, 0.0);
    seg(&a, M_HELD, 20.0, 50.0, 0.0);
    CHECK(a.n_episodes == 0 && a.ep_phase == ACT_EP_OPEN && act_episode_s(&a, &ep) && ep == 49,
          "8b: a lapse the hold undid: %u ended, phase %d, episode_s %u (want 0, open, 49)",
          (unsigned)a.n_episodes, (int)a.ep_phase, (unsigned)ep);
    seg(&a, M_ABSENT_MOVING, 1.0, 50.0, 0.0);    /* gets up and leaves */
    seg(&a, M_ABSENT, 10.0, 50.0, 0.0);
    CHECK(a.n_episodes == 1 && a.last.total_s == 50 && ep_sum_ok(&a.last) && a.last.st_s[ACT_STILL] > 0,
          "8b: %u ended, last %u s, still %u s (want 1 of 50 s, with still seconds)",
          (unsigned)a.n_episodes, (unsigned)a.last.total_s, (unsigned)a.last.st_s[ACT_STILL]);
    printf("    8b walk 15 + sit 5, lapse 10, held 20: one episode of %u s (still %u, moving %u, "
           "empty %u)\n", (unsigned)a.last.total_s, (unsigned)a.last.st_s[ACT_STILL],
           (unsigned)a.last.st_s[ACT_MOVING], (unsigned)a.last.st_s[ACT_EMPTY]);

    /* a walk-in during the lapse: Step 08 first, then the gate */
    start(&a, CARRY_US);
    seg(&a, M_ABSENT, 10.0, 50.0, 0.0);
    seg(&a, M_WALK, 20.0, 50.0, 0.0);
    seg(&a, M_ABSENT, 10.0, 50.0, 0.0);
    seg(&a, M_QUIET, 2.0, 50.0, 0.0);
    CHECK(a.ep_phase == ACT_EP_WAIT, "8c: phase %d after Step 08 alone came back, want wait", (int)a.ep_phase);
    seg(&a, M_WALK, 18.0, 50.0, 0.0);
    CHECK(a.n_episodes == 1 && a.last.total_s == 20 && act_episode_s(&a, &ep) && ep == 19,
          "8c: %u ended (last %u s), now %u s - want 1 of 20 s, then a new one of 19",
          (unsigned)a.n_episodes, (unsigned)a.last.total_s, (unsigned)ep);

    /* a Step 08 blip during the lapse, then the hold: still one episode */
    start(&a, CARRY_US);
    seg(&a, M_ABSENT, 10.0, 50.0, 0.0);
    seg(&a, M_WALK, 20.0, 50.0, 0.0);
    seg(&a, M_ABSENT, 10.0, 50.0, 0.0);
    seg(&a, M_QUIET, 1.0, 50.0, 0.0);
    seg(&a, M_ABSENT, 4.0, 50.0, 0.0);
    seg(&a, M_HELD, 15.0, 50.0, 0.0);
    CHECK(a.n_episodes == 0 && a.ep_phase == ACT_EP_OPEN && act_episode_s(&a, &ep) && ep == 49,
          "8d: %u ended, phase %d, episode_s %u (want 0, open, 49)", (unsigned)a.n_episodes,
          (int)a.ep_phase, (unsigned)ep);

    /* Step 08 comes back first (wait), then the hold: one episode, with
     * the lapse and the wait seconds in it - main.c's usual restore */
    start(&a, CARRY_US);
    seg(&a, M_WALK, 20.0, 50.0, 0.0);
    seg(&a, M_ABSENT, 10.0, 50.0, 0.0);
    seg(&a, M_QUIET, 2.0, 50.0, 0.0);
    seg(&a, M_HELD, 10.0, 50.0, 0.0);
    CHECK(a.n_episodes == 0 && a.ep_phase == ACT_EP_OPEN && act_episode_s(&a, &ep) && ep == 41 &&
          ep_sum_ok(&a.cur), "8i: wait then hold: %u ended, phase %d, episode_s %u (want 0, open, "
          "41 = 20 + 10 + 2 + 9)", (unsigned)a.n_episodes, (int)a.ep_phase, (unsigned)ep);
    seg(&a, M_ABSENT_MOVING, 1.0, 50.0, 0.0);    /* gets up and leaves */
    seg(&a, M_ABSENT, 1.0, 50.0, 0.0);
    CHECK(a.n_episodes == 1 && a.last.total_s == 42 && ep_sum_ok(&a.last),
          "8i: %u ended, last %u s, want 1 of 42", (unsigned)a.n_episodes, (unsigned)a.last.total_s);

    /* the restoring second also holds one gate-MOVING packet: the hold
     * wins, as main.c's s_carry_wait does on the first held packet */
    start(&a, CARRY_US);
    seg(&a, M_WALK, 20.0, 50.0, 0.0);
    seg(&a, M_ABSENT, 10.0, 50.0, 0.0);
    seg(&a, M_HELD, 0.5, 50.0, 0.0);
    seg(&a, M_SPIKE, 0.02, 50.0, 0.0);
    seg(&a, M_HELD, 10.48, 50.0, 0.0);
    CHECK(a.n_episodes == 0 && a.ep_phase == ACT_EP_OPEN && act_episode_s(&a, &ep) && ep == 40,
          "8h: hold + one MOVING packet in the restoring second: %u ended, phase %d, episode_s %u "
          "(want 0, open, 40)", (unsigned)a.n_episodes, (int)a.ep_phase, (unsigned)ep);

    /* a lapse longer than the carry is not bridged, even by the hold */
    start(&a, CARRY_US);
    seg(&a, M_WALK, 20.0, 50.0, 0.0);
    seg(&a, M_ABSENT, 61.0, 50.0, 0.0);
    seg(&a, M_HELD, 5.0, 50.0, 0.0);
    CHECK(a.n_episodes == 1 && a.last.total_s == 20 && act_episode_s(&a, &ep) && ep == 4,
          "8e: %u ended (last %u s), now %u s - want 1 of 20, then 4", (unsigned)a.n_episodes,
          (unsigned)a.last.total_s, (unsigned)ep);

    /* the edge: the restoring second must end within carry_us of the
     * lapse's start - a 59 s lapse is bridged, a 60 s one is not */
    start(&a, CARRY_US);
    seg(&a, M_WALK, 20.0, 50.0, 0.0);
    seg(&a, M_ABSENT, 59.0, 50.0, 0.0);
    seg(&a, M_HELD, 5.0, 50.0, 0.0);
    CHECK(a.n_episodes == 0 && act_episode_s(&a, &ep) && ep == 83,
          "8j: 59 s lapse: %u ended, episode_s %u - want 0, 83 = 20 + 59 + 4", (unsigned)a.n_episodes,
          (unsigned)ep);
    start(&a, CARRY_US);
    seg(&a, M_WALK, 20.0, 50.0, 0.0);
    seg(&a, M_ABSENT, 60.0, 50.0, 0.0);
    seg(&a, M_HELD, 5.0, 50.0, 0.0);
    CHECK(a.n_episodes == 1 && a.last.total_s == 20 && act_episode_s(&a, &ep) && ep == 4,
          "8j: 60 s lapse: %u ended (last %u s), now %u s - want 1 of 20, then 4", (unsigned)a.n_episodes,
          (unsigned)a.last.total_s, (unsigned)ep);

    /* no bridging when the hold is compiled out (carry 0) */
    start(&a, 0);
    seg(&a, M_WALK, 20.0, 50.0, 0.0);
    seg(&a, M_ABSENT, 5.0, 50.0, 0.0);
    seg(&a, M_HELD, 10.0, 50.0, 0.0);
    CHECK(a.n_episodes == 1 && a.last.total_s == 20, "8f: carry 0: %u ended (last %u s), want 1 of 20",
          (unsigned)a.n_episodes, (unsigned)a.last.total_s);

    /* act_reset (no-CSI, recalibration) ends it and clears the state */
    start(&a, CARRY_US);
    seg(&a, M_QUIET, 20.0, 50.0, 0.0);
    act_reset(&a);
    CHECK(a.n_episodes == 1 && a.last.total_s == 19 && !act_episode_s(&a, &ep) &&
          act_state(&a) == ACT_NONE && ep_sum_ok(&a.last),
          "8g: after act_reset: %u ended (last %u s), state %s", (unsigned)a.n_episodes,
          (unsigned)a.last.total_s, nm(act_state(&a)));
    printf("    8c-j walk-in during a lapse = new episode; blip then hold = one; Step 08 then hold "
           "= one; hold + a MOVING packet = one; carry limit (59 s bridged, 60 s not); carry 0; "
           "act_reset ends it\n");
}

/* ---- 9 ---- */
static void test_edges(void)
{
    static act_t a;

    /* moving fraction: 25 of 100 packets is ACTIVE, 24 is not (hf 1.0) */
    start(&a, CARRY_US);
    seg(&a, M_DUTY25, 20.0, 100.0, 3.0);
    CHECK(s_seen[ACT_MOVING] == seen_total(), "9: 25%% MOVING: moving on %ld of %ld packets",
          s_seen[ACT_MOVING], seen_total());
    start(&a, CARRY_US);
    seg(&a, M_DUTY24, 20.0, 100.0, 10.1);
    CHECK(s_seen[ACT_STILL] == seen_total(), "9: 24%% MOVING: still on %ld of %ld packets (moving %ld)",
          s_seen[ACT_STILL], seen_total(), s_seen[ACT_MOVING]);

    /* mean hf exactly restless_lo: FIDGET; exactly the threshold: ACTIVE */
    start(&a, CARRY_US);
    seg(&a, M_HF_LO, 20.0, 100.0, 3.0);
    CHECK(s_seen[ACT_RESTLESS] == seen_total(), "9: hf = LO: restless on %ld of %ld packets (still %ld)",
          s_seen[ACT_RESTLESS], seen_total(), s_seen[ACT_STILL]);
    start(&a, CARRY_US);
    seg(&a, M_HF_THR, 20.0, 100.0, 3.0);
    CHECK(s_seen[ACT_MOVING] == seen_total(), "9: hf = THR: moving on %ld of %ld packets (restless %ld)",
          s_seen[ACT_MOVING], seen_total(), s_seen[ACT_RESTLESS]);

    /* a walk that ends: the last ACTIVE second leaves the 5 s window, then
     * the 2 s hold - moving until 5 s after the walk, then still */
    start(&a, CARRY_US);
    seg(&a, M_ABSENT, 10.0, 50.0, 0.0);
    seg(&a, M_WALK, 15.0, 50.0, 0.0);
    seg(&a, M_QUIET, 4.98, 50.0, 0.0);
    long restless = s_seen[ACT_RESTLESS];
    act_state_t at5 = act_state(&a);
    seg(&a, M_QUIET, 20.0, 50.0, 0.04);
    restless += s_seen[ACT_RESTLESS];
    CHECK(at5 == ACT_MOVING && s_seen[ACT_STILL] == seen_total() && restless == 0,
          "9: walk then sit: %s at 4.98 s, then still on %ld of %ld packets, restless on %ld - want "
          "moving, all, 0", nm(at5), s_seen[ACT_STILL], seen_total(), restless);

    /* the same walk, then Step 08's 5 s hold with nobody moving, then nobody */
    start(&a, CARRY_US);
    seg(&a, M_ABSENT, 10.0, 50.0, 0.0);
    seg(&a, M_WALK, 15.0, 50.0, 0.0);
    seg(&a, M_QUIET, 5.0, 50.0, 0.0);
    restless = s_seen[ACT_RESTLESS];
    seg(&a, M_ABSENT, 10.0, 50.0, 0.0);
    restless += s_seen[ACT_RESTLESS];
    CHECK(restless == 0 && act_state(&a) == ACT_EMPTY, "9: walk, hold, out: restless on %ld packets, "
          "%s at the end - want 0, empty", restless, nm(act_state(&a)));
    /* one ACTIVE second and one FIDGET second: restless while the ACTIVE
     * one is in the 5 s window */
    start(&a, CARRY_US);
    seg(&a, M_QUIET, 20.0, 50.0, 0.0);
    seg(&a, M_SPIKE, 1.0, 50.0, 0.0);
    seg(&a, M_HF_LO, 1.0, 50.0, 0.0);
    seg(&a, M_QUIET, 2.02, 50.0, 0.0);
    CHECK(act_state(&a) == ACT_RESTLESS, "9: one ACTIVE + one FIDGET second: %s, want restless",
          nm(act_state(&a)));
    printf("    9 edges: 25%% MOVING = moving, 24%% = still; hf = LO restless, hf = THR moving; "
           "a walk ends in still or empty, never restless; ACTIVE + FIDGET = restless\n");
}

/* ---- 10 ---- */
static void test_unknown_floor(void)
{
    static act_t a;
    uint32_t ep = 0;
    start(&a, CARRY_US);
    seg(&a, M_UNK_ABSENT, 30.0, 50.0, 0.0);
    CHECK(act_state(&a) == ACT_EMPTY, "10: %s in an empty room with the floor unknown, want empty",
          nm(act_state(&a)));
    seg(&a, M_UNK_PRESENT, 1.98, 50.0, 0.0);     /* the 2 s hold: empty still */
    CHECK(act_state(&a) == ACT_EMPTY, "10: %s within the 2 s hold, want empty", nm(act_state(&a)));
    seg(&a, M_UNK_PRESENT, 58.02, 50.0, 0.04);   /* then nothing */
    CHECK(s_seen[ACT_NONE] == seen_total() && act_state_s(&a) == 0 && act_episode_s(&a, &ep) && ep == 59,
          "10: PRESENT, floor unknown: null on %ld of %ld packets (empty %ld), state_s %u, episode %u s - "
          "want all, 0, 59", s_seen[ACT_NONE], seen_total(), s_seen[ACT_EMPTY],
          (unsigned)act_state_s(&a), (unsigned)ep);
    seg(&a, M_QUIET, 10.5, 50.0, 0.0);           /* the floor is known: 10 known seconds */
    CHECK(act_state(&a) == ACT_STILL, "10: %s 10.5 s after the floor became known, want still",
          nm(act_state(&a)));
    printf("    10 floor unknown: empty while nobody is there, null (not empty) once someone is\n");
}

int main(void)
{
    printf("test_activity: scripted per-packet decisions (present, gate still / hf, hold) with\n"
           "known answers - activity.h's rules, not evidence about real rooms.\n");
    test_still();
    test_empty();
    test_walk_duty();
    test_fidget();
    test_hysteresis();
    test_rate();
    test_gap();
    test_episodes();
    test_edges();
    test_unknown_floor();
    return test_summary("test_activity");
}
