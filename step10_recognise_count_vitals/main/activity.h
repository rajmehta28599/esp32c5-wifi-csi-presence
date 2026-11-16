/*
 * activity.h - a coarse activity state for the room (empty / still /
 *              restless / moving) and presence-episode counters, both
 *              derived from decisions Step 10 already makes. No new signal.
 *
 * INPUTS (once per accepted packet in the detect phase, AFTER main.c has
 * settled this packet's presence, breathing hold included):
 *   present  main.c's `present` - Step 08's decision, possibly held or
 *            restored by the breathing hold (CSI_VITALS_HOLDS_PRESENCE)
 *   still    the motion gate's decision (mgate_push)
 *   hf       the gate's ratio (mgate_ratio): energy above ~1.6 Hz over the
 *            empty room's; < 0 while its floor is unknown
 *   held     the breathing hold asserted PRESENT on this packet
 *
 * WHAT "still" MEANS IN PRACTICE
 *   Step 08 loses a person who stops moving: nothing triggers, and PRESENT
 *   lapses CSI_PRESENCE_HOLD_MS after the last trigger. Unless the breathing
 *   hold brings it back (~20 s of still data later) the room then reads
 *   "empty". So a lasting "still" is, in practice, PRESENT held by the
 *   breathing hold; with CSI_VITALS_HOLDS_PRESENCE off it lasts only as long
 *   as Step 08's own hold (or its static branch) keeps PRESENT. Between the
 *   lapse and the restore the state can read "empty" for a while: that is
 *   what the detector believed at the time, and it is reported as such.
 *
 * ALGORITHM (no FFT, no expf / sinf; per packet a few integer adds and one
 * float add; per second one division and a loop over ACT_RING bytes)
 *   Packets are summed into 1-second bins on their timestamps, never by
 *   packet count, so 50 and 100 packets/s give the same states. Packets
 *   with hf < 0 (gate floor unknown, when the gate also says MOVING by
 *   design) count towards neither motion figure. Each completed second
 *   becomes one byte in a ring:
 *     known    at least one packet had hf >= 0
 *     level    ACTIVE   moving fraction (gate MOVING) >= ACT_MOVE_FRAC, or
 *                       mean hf >= the gate's threshold
 *              FIDGET   not ACTIVE, and mean hf in [restless_lo, gate
 *                       threshold)
 *              QUIET    otherwise
 *     present  any packet of the second was PRESENT
 *   A raw state is derived from the ring every second:
 *     empty     the last ACT_EMPTY_S seconds had no PRESENT packet
 *     (none)    this second had no PRESENT packet, but fewer than
 *               ACT_EMPTY_S in a row: the state stays as it was
 *     moving    >= ACT_MOVE_MIN_S ACTIVE seconds among the last ACT_WIN_S
 *     restless  >= ACT_FIDGET_MIN_S seconds that are FIDGET among the last
 *               ACT_QUIET_S or ACTIVE among the last ACT_WIN_S (an ACTIVE
 *               second leaves with the "moving" window, so a walk that
 *               ends goes to still, not through restless)
 *     still     all of the last ACT_QUIET_S seconds known, and none of the
 *               above
 *     (unsure)  PRESENT, but not all of the last ACT_QUIET_S seconds known
 *               (the gate's floor not learnt yet): no state - unlike the
 *               lapse above it clears the state, so an earlier "empty"
 *               does not stay while someone is PRESENT
 *   HYSTERESIS: a raw state that differs from the current one must hold for
 *   ACT_HOLD_S consecutive seconds before it replaces it ("(unsure)" too,
 *   replacing it with no state); a "(none)" second breaks the run. From no
 *   state at all the first raw state is taken at once (there is nothing to
 *   flicker away from).
 *   Why windows, not packets: at 50 Hz the gate called only 26-31% of a
 *   synthetic walker's packets MOVING in two of nine test_e2e rooms
 *   (test_e2e.c (e), rooms 5004 and 5007), so a per-packet "moving" would
 *   flicker; a duty over a whole second does not.
 *   A gap of more than ACT_GAP_US between packets (or a timestamp going
 *   backwards) discards the ring and the state: the next state needs fresh
 *   seconds. The episode below is NOT ended by such a gap - main.c's no-CSI
 *   watchdog (act_reset via analytics_reset) is what ends one on an outage.
 *
 * PRESENCE EPISODES (whole seconds; per state the seconds of each label)
 *   An episode starts with the first second that has a PRESENT packet and
 *   ends with the last such second. A presence lapse that the breathing
 *   hold undoes does NOT split it - the same rule main.c's count carry and
 *   the whose-vitals latch follow:
 *     lapse   (absent)   pending; any gate-MOVING packet, or a lapse longer
 *                        than cfg.carry_us, ends the episode at its last
 *                        present second (the lapse is not counted).
 *     wait    (PRESENT again, by Step 08 alone)  still pending: a walk-in
 *                        looks like this too, for the first second or two.
 *                        A breathing-hold second bridges the lapse - the
 *                        lapse and wait seconds are added to the episode;
 *                        gate MOVING (or carry_us) ends the old episode and
 *                        starts a new one at the first wait second.
 *   Within one second the hold wins over a gate-MOVING packet, as in
 *   main.c, where the first held packet hands the carry back.
 *   carry_us <= 0 (breathing hold compiled out): every lapse ends the
 *   episode at once.
 *   Seconds are counted from completed 1-second bins, so seconds lost in a
 *   CSI gap are not counted. episode_s is reported only while an episode
 *   is open or pending; during a pending lapse it stays at the last
 *   present second's value.
 *
 * WHAT IT CANNOT DO
 *   - sitting vs standing vs lying: nothing in 26 amplitude tones of one
 *     link supports that;
 *   - the thresholds are synthetic: real walking has never been measured
 *     in hf (DECISIONS D-83), and seated breathing reached hf 2.33 in a
 *     synthetic test, so "restless" and "still" can overlap;
 *   - a fan, a pet or a curtain moves it like a person (Step 08's limits);
 *   - accuracy: not measured.
 *
 * ESP-IDF free; host-tested in test/test_activity.c and (wired as main.c
 * wires it) test/test_e2e.c (e). Memory: act_t is 216 bytes (.bss.s_act in
 * the firmware's linker map, 2026-10-16); declare it static.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#define ACT_WIN_S          5         /* "moving" window, seconds           */
#define ACT_MOVE_MIN_S     2         /* ACTIVE seconds in it for "moving"  */
#define ACT_QUIET_S        10        /* "still" / "restless" window        */
#define ACT_FIDGET_MIN_S   2         /* non-quiet seconds for "restless"   */
#define ACT_EMPTY_S        5         /* absent seconds for "empty"         */
#define ACT_HOLD_S         2         /* hysteresis, seconds                */
#define ACT_MOVE_FRAC_DIV  4         /* moving fraction >= 1/4 = ACTIVE    */
#define ACT_GAP_US         1000000
#define ACT_RING           ACT_QUIET_S

typedef enum {
    ACT_NONE = 0,                    /* no state yet: JSON null            */
    ACT_EMPTY,
    ACT_STILL,
    ACT_RESTLESS,
    ACT_MOVING,
    ACT_NSTATES
} act_state_t;

typedef struct {
    float   move_thr;                /* the gate's threshold (hf)          */
    float   restless_lo;             /* hf from which a second is FIDGET   */
    int64_t carry_us;                /* longest lapse an episode bridges; 0 = none */
} act_cfg_t;

/* One presence episode: whole seconds, and the seconds spent under each
 * label (st_s[ACT_NONE] and st_s[ACT_EMPTY] are the seconds no state or
 * "empty" was reported inside it: the hysteresis at the start, a bridged
 * lapse). The st_s add up to total_s. */
typedef struct {
    uint32_t total_s;
    uint32_t st_s[ACT_NSTATES];
} act_ep_t;

typedef enum {
    ACT_EP_NONE = 0,                 /* no episode                         */
    ACT_EP_OPEN,                     /* present                            */
    ACT_EP_LAPSE,                    /* absent, may still be bridged       */
    ACT_EP_WAIT                      /* present by Step 08 alone after a lapse */
} act_ep_phase_t;

typedef struct {
    act_cfg_t cfg;

    /* the current 1-second bin */
    bool     started;
    int64_t  sec_start_us;
    int64_t  last_t_us;
    uint32_t n_pkt, n_known, n_moving;
    float    hf_sum;
    bool     any_present, any_held, any_moved;

    /* completed seconds */
    uint8_t  ring[ACT_RING];         /* ACT_R_* bits                      */
    int      ring_head, ring_n;
    uint32_t absent_run;             /* seconds in a row without PRESENT  */

    act_state_t state, cand;
    int         cand_n;
    uint32_t    state_s;             /* seconds since `state` was taken   */

    /* episodes */
    act_ep_phase_t ep_phase;
    act_ep_t       cur, lapse, wait;
    int64_t        lapse_t0_us;      /* start of the lapse's first second */
    act_ep_t       last;             /* the last episode that ended       */
    uint32_t       n_episodes;       /* ended since boot                  */
} act_t;

void act_init(act_t *a, const act_cfg_t *cfg);

/* No-CSI, recalibration, new baseline: an open or pending episode ends
 * (at its last present second) and the state and its seconds start again
 * from nothing. The configuration is kept. */
void act_reset(act_t *a);

/* Once per accepted packet in the detect phase, after presence is final. */
void act_push(act_t *a, int64_t t_us, bool present, bool still, float hf, bool held);

act_state_t act_state(const act_t *a);

/* Seconds since the current state was taken (0 with ACT_NONE). */
uint32_t act_state_s(const act_t *a);

/* "empty" / "still" / "restless" / "moving", NULL for ACT_NONE (JSON null). */
const char *act_state_name(act_state_t s);

/* The current episode's seconds; false (JSON null) when there is none. */
bool act_episode_s(const act_t *a, uint32_t *s);
