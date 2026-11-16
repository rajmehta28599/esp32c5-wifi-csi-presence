/*
 * ============================================================================
 *  STEP 10 - EXPERIMENTAL: recognise, count, vitals
 * ============================================================================
 *
 *  Step 08's complete human detector - its behaviour unchanged - plus four
 *  EXPERIMENTAL outputs computed from the same CSI stream:
 *
 *    - WHO is walking: gait-based recognition of up to 4 people enrolled
 *      over the serial console (gait_id.c features, csi_ncc.c classifier)
 *    - HOW MANY are moving: one versus two-or-more, after a training
 *      session in this room (count_est.c features, csi_ncc.c classifier)
 *    - VITALS: the breathing rate of a person sitting or lying still, and
 *      an EXPERIMENTAL heart rate on top of it (vitals.c)
 *    - ACTIVITY: empty / still / restless / moving, and presence-episode
 *      seconds, from presence and the motion gate (activity.c)
 *
 *  Step 08 itself is untouched, and the ROOT project (D:\ESP32C5_CSI) still
 *  builds Step 08. Build this one from its own folder:
 *      .\build.ps1 step10_recognise_count_vitals -Flash
 *
 *  WHAT STEP 08 DOES (all still true here)
 *    - joins your 2.4 GHz WiFi and locks the radio so CSI stays comparable
 *    - pings the gateway to generate a steady stream of received packets
 *    - captures CSI, locks onto one packet shape, extracts amplitude
 *    - normalises away the receiver's automatic gain control
 *    - calibrates itself against an empty room (automatically, on first boot)
 *    - stores that calibration in NVS and reloads it on every boot
 *    - reports PRESENT / EMPTY with a motion level, over serial as JSON
 *      and as a human-readable bar, plus an optional LED
 *    - recalibrates on a button hold, the 'recal' command, or a menuconfig
 *      flag
 *
 *  FIRST BOOT ON A NEW BOARD
 *    There is no baseline in NVS, so it calibrates itself. You get a
 *    countdown (CSI_CAL_LEAVE_ROOM_S) to leave the room, then about 35
 *    seconds of measurement, then it starts detecting. Then enrol people
 *    and train the counter from the console (below) if you want those.
 *
 *  WHAT THIS CAN AND CANNOT DO - please read this before judging the results
 *    Nothing Step 10 adds has been validated on hardware. The host tests in
 *    test/ only show that the arithmetic recovers known answers from
 *    SYNTHETIC signals.
 *    * Presence: as Step 08. A human who MOVES is what single-antenna Wi-Fi
 *      CSI genuinely delivers. A perfectly motionless person is unreliable;
 *      confident breathing can now keep them PRESENT, but only if they were
 *      already PRESENT when they became still (CSI_VITALS_HOLDS_PRESENCE).
 *    * Identity (EXPERIMENTAL): 2-3 enrolled people, ONE AT A TIME,
 *      WALKING, in the room - and with the board where - they enrolled, at
 *      the same ping rate. A published ESP32 result is ~95% for 2 people
 *      and ~74% for 3 (Gaiba 2024, controlled room; abstract only, setup
 *      unverified - docs/REFERENCES.md). A still person cannot be
 *      recognised, a stranger is at best "unknown", and with two or more
 *      people moving it says "multiple" instead of guessing - which needs
 *      a trained counter ('count-train'; untrained, two walkers are
 *      classified as one gait).
 *    * Count (EXPERIMENTAL): 1 versus 2+ MOVING people, and only after
 *      'count-train' in this room. Untrained, "count" is just presence
 *      (0 or 1). People sitting still are not counted.
 *    * Breathing: a person sitting or lying still in the same room, after
 *      ~20 s of stillness. Any motion restarts it - motion as the vitals
 *      motion gate sees it (motion_gate.h, JSON "hf"), not Step 08's flag,
 *      which breathing itself can hold on.
 *    * Activity (EXPERIMENTAL): a coarse state over the last 5-10 s, no
 *      posture - it cannot tell sitting from standing or lying. "still"
 *      lasts only while PRESENT does (for a motionless person: the
 *      breathing hold), and the restless / moving thresholds in hf are
 *      synthetic; a fan or a pet moves it like a person. Not measured.
 *    * Heart rate: EXPERIMENTAL and UNVALIDATED - labelled so in the JSON
 *      key itself (hr_bpm_exp). Log it next to a pulse oximeter or chest
 *      strap (tools/presence_monitor.py, tools/vitals_validate.py) before
 *      believing a single number.
 *    * It also responds to a large dog, a ceiling fan, or curtains moving.
 *      It senses changes in radio propagation, not people specifically.
 *    * Recalibrate after moving the board, moving furniture, or changing
 *      the WiFi channel. Re-enrol and re-train after changing the ping rate
 *      (CSI_PING_INTERVAL_MS; 10 ms is recommended for recognition).
 *
 *  SERIAL CONSOLE (USB-Serial-JTAG). Type into idf.py monitor and press
 *  Enter - typed characters are not echoed - and a line starting "CMD "
 *  answers. tools/presence_monitor.py --send "<command>" does the same.
 *    help                        list the commands
 *    status                      one status block (also: last reset reason,
 *                                uptime, whether a USB host is reading)
 *    people                      enrolment slots: name, windows, ready / collecting,
 *                                and each person's vitals log
 *    explain                     how it decides, with the live thresholds, in
 *                                plain English (+ an EXPLAIN config line)
 *    stats                       activity and presence episodes: the current
 *                                one, the last one, seconds in each state
 *    enroll <slot 0-3> <name>    enrol a person: they walk around ALONE until
 *                                "enrol <name>: done" (name: 1-15 of A-Z a-z 0-9 _ -)
 *    forget <slot> | forget all  delete template(s) and their vitals log, saved to NVS
 *    vitals-reset <slot> | all   delete a person's vitals log (templates kept)
 *    count-train 1               collect blocks with ONE person moving
 *    count-train 2               collect blocks with TWO OR MORE people moving
 *    count-reset                 delete the count model, saved to NVS
 *    stop                        cancel an enrolment / training, discarding it
 *    recal                       recalibrate (same as the 2 s button hold)
 *
 *  JSON OUTPUT (one line every CSI_JSON_PERIOD_MS while detecting; Step 08's
 *  fields first and unchanged; null = not available)
 *    JSON {"t_ms":123456,"state":"detect","present":1,"motion_sd":7.4,
 *          "dev_sd":1.2,"level":148,"rssi":-46,"rate_hz":49.6,"nsub":52,
 *          "count":1,"count_src":"presence",
 *          "person":"raj","person_dist":0.84,"person_votes":"5/7",
 *          "vitals":"ok","br_bpm":14.8,"br_conf":6.2,
 *          "hr_bpm_exp":71.3,"hr_conf":5.4,
 *          "session":"enroll:raj 12/30",
 *          "hf":1.07,"proc_ms_max":8.4,"dropped":0,
 *          "vitals_of":"raj",
 *          "activity":"still","activity_s":42,"episode_s":318,
 *          "agc":36,"fft":-2,"gain_chg":0}
 *    count         0, 1 or 2 (2 = two or more); count_src "presence" | "trained"
 *                  ("trained" only once a block of THIS presence episode has
 *                  been classified by a trained model; until then 1 is
 *                  just presence)
 *    person        null, "unknown", "multiple" or an enrolled name
 *                  ("unknown" only once ID_MIN_VOTES windows have voted -
 *                  before that, and with no name yet, null)
 *    person_dist   mean template distance of the windows behind "person"
 *                  (~1 = typical for an enrolled person; null if none)
 *    person_votes  "k/n": k of the last n gait windows back "person"
 *    vitals        "off" | "no-person" | "multiple" | "moving" | "settling" |
 *                  "no-signal" | "ok". "multiple": two or more people this
 *                  visit (counted moving together, or two names), so one
 *                  rate would mix them - br/hr are null until the room has
 *                  been empty. Needs a trained counter or two names: two
 *                  people who sit down unseen still get one rate.
 *    session       null | "enroll:<name> k/N" | "count-train:1 k/N" | "count-train:2 k/N"
 *    hf            the motion gate's ratio: motion energy above ~1.6 Hz over
 *                  its empty-room level. ~1 = nobody moving (breathing
 *                  barely moves it); at or above CSI_VITALS_MOTION_RATIO_X100
 *                  / 100 = moving. null until the gate knows its floor. For
 *                  tuning that threshold (see its help text).
 *    proc_ms_max   slowest per-packet processing time since the previous
 *                  JSON line, ms - diagnostic
 *    dropped       CSI packets lost to a full queue since boot - diagnostic
 *    vitals_of     null or an enrolled name: whose the breathing / heart rate
 *                  are CLAIMED to be - the last person recognised walking in
 *                  ALONE this visit (nobody can be recognised sitting), so a
 *                  carried-over claim, not a measurement (person_vitals.h).
 *                  Appended after "dropped": new keys go last, none moves.
 *    activity      null | "empty" | "still" | "restless" | "moving"
 *                  (EXPERIMENTAL, activity.h): a coarse state decided over
 *                  the last 5-10 s from presence and the motion gate, held
 *                  2 s before it changes. "still" lasts only while PRESENT
 *                  does - for a motionless person, normally the breathing
 *                  hold. No posture: it cannot tell sitting from lying.
 *                  null until it has enough seconds (also while someone is
 *                  PRESENT before the gate's floor is known), after a CSI
 *                  gap, and always without CSI_ACTIVITY_ENABLE.
 *    activity_s    whole seconds in that state (null with activity null)
 *    episode_s     whole seconds of the current presence episode; a lapse
 *                  that the breathing hold undoes does not end it (the
 *                  value stays put during the lapse). null when there is
 *                  no episode. Appended after vitals_of, in this order.
 *    agc, fft      receiver AGC / FFT gain of the last packet since the
 *                  previous line (CSI_GAIN_LOG, csi_gain.h: two rx_ctrl
 *                  bytes Espressif's gain library reads; NOT a documented
 *                  field, not yet seen on silicon). Logging only.
 *    gain_chg      packets since the previous line whose (agc, fft)
 *                  differed from the packet before. All three null without
 *                  CSI_GAIN_LOG or with no packet. Appended after episode_s.
 *                  Only this (detect) line carries them. The count restarts
 *                  when detection starts (boot or 'recal'), so the first
 *                  line covers detection packets only (plus a change from
 *                  the last calibration packet); the calibration's own
 *                  packets are summarised once in a "gain before
 *                  detection" log line.
 *  The calibration and no_csi JSON lines are exactly Step 08's.
 *
 *  EXPLAIN OUTPUT (CSI_EXPLAIN, default on): WHY it decided, for
 *  tools/dashboard.py. One line per decision event, printed with ONE
 *  printf: "EXPLAIN " + a JSON object whose "ev" says what it is and whose
 *  "t_ms" is the same clock as the JSON line's. Numbers have at most 3
 *  decimals and are clamped to +/-999999 (template distances to 999, as
 *  person_dist); null = unknown, not applicable, or not finite; a feature
 *  compiled out gives null for its whole sub-object. Strings are literals
 *  or enrolled names (JSON-safe, see name_valid). Every key below is
 *  always present for its "ev". Alongside each event except "amp", a
 *  plain-English log line "why: ..." says the same for a person.
 *    "config"   when detection is configured (stored baseline accepted, or
 *               a calibration finished - so also after 'recal'), when a
 *               model changes (enrol / count-train started or done,
 *               'forget', 'count-reset', 'stop'), and on 'explain':
 *               reset (last reset reason), rate_hz (calibrated packets/s),
 *               csi_len, nsub (subcarriers per packet), nsel (selected),
 *               channel, lltf, thr_motion / thr_release / thr_static
 *               (sd units, as JSON motion_sd / dev_sd), static_sustain_ms,
 *               presence_hold_ms, gate_ratio (motion gate, JSON hf),
 *               amp_hz,
 *               id {reject, margin, votes, min_votes, win, hop, min_moving,
 *                   min_windows, slots:[{name|null, n, ready} x4]},
 *               count {votes, win, min_duty, need, ready, n1, n2},
 *               vitals {window_s, min_s, br_conf_min, hr_conf_min,
 *                       holds_presence}
 *    "cal"      baseline in use: src "fresh" (a calibration just finished)
 *               or "stored" (reloaded from NVS at boot), m_mean, m_std,
 *               d_mean, d_std (empty-room motion / deviation metric),
 *               nsub_selected, nsub, csi_len, channel, rate_hz
 *    "amp"      CSI_EXPLAIN_AMP_HZ times a second while detecting: a = the
 *               normalised amplitude (the metrics' own input) of each
 *               selected subcarrier, 2 decimals, at most 64 (every 2nd,
 *               3rd ... selected one beyond that); m (motion_sd), d
 *               (dev_sd), hf, still (motion gate), present (0/1). Skipped
 *               while the console's transmit buffer is full.
 *    "gait"     every completed gait window while PRESENT, voting or not:
 *               win (counter since boot), moving_frac, used, why (null
 *               when used; else "few-moving" | "multiple" | "no-model" |
 *               "session" | "enrol" | "nonfinite"), dist [slot 0..3, null
 *               = slot not usable], nearest (slot), second_dist, best (slot,
 *               -1 = unknown), reason ("ok" | "too-far" | "ambiguous"),
 *               vote (name | "unknown"), ring (the last ID votes, oldest
 *               first: slot digit or "?" = unknown), tally {label (name |
 *               "unknown" | null, as JSON person), k, n}. dist .. vote are
 *               null when not used.
 *    "count"    every completed count block while PRESENT: blk (counter
 *               since boot), duty, used, why (null | "low-duty" |
 *               "no-model" | "session" (a count-train block, or an
 *               enrolment running) | "nonfinite"), feat (the 6 count
 *               features, count_est.h), dist [one, two+] (null until both
 *               classes are trained), vote (1 | 2 | null), ring ("1121",
 *               oldest first), est (0 | 1 | 2 = the count it now reports
 *               from the model; 0 = none this visit, count = presence)
 *    "vitals"   every completed vitals analysis (~every 2 s of still
 *               data): state (as JSON vitals), br, br_conf, hr_exp,
 *               hr_conf (exactly the JSON's rules: withheld -> null),
 *               still_s, vitals_of, multiple (2+ people this visit),
 *               activity, activity_s, episode_s (as the JSON line's)
 *    "presence" every PRESENT / EMPTY change: present, reason ("motion" |
 *               "static" | "breathing-hold" | "hold-expired" | "no-csi" |
 *               "recal"), m, d (null for no-csi / recal)
 *  Worst-case line lengths are worked out at EX_LINE_BOUND below.
 * ============================================================================
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_idf_version.h"

/* ESP-IDF v6.0 renamed the 20 MHz bandwidth enum from WIFI_BW_HT20 to
 * WIFI_BW20. This keeps the project building on both v5.5.x and v6.0.x. */
#if ESP_IDF_VERSION_MAJOR >= 6
#define CSI_WIFI_BW20 WIFI_BW20
#else
#define CSI_WIFI_BW20 WIFI_BW_HT20
#endif
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "ping/ping_sock.h"
#include "lwip/inet.h"

#include "csi_baseline.h"
#include "csi_dsp.h"
#include "csi_ncc.h"
#include "vitals.h"
#include "gait_id.h"
#include "count_est.h"
#include "motion_gate.h"
#include "person_vitals.h"
#include "activity.h"
#include "csi_gain.h"

/* ------------------------------------------------------------------ */
/*  Which Step 10 parts are compiled in. Every feature switch works    */
/*  both ways: code and state of a disabled feature are compiled out  */
/*  entirely, never left behind as unused statics.                     */
/* ------------------------------------------------------------------ */

/* The two NCC models and enrol / train sessions exist only for
 * recognition and counting. */
#if CONFIG_CSI_ID_ENABLE || CONFIG_CSI_COUNT_ENABLE
#define CSI_HAVE_MODELS 1
#else
#define CSI_HAVE_MODELS 0
#endif

/* Whose vitals (person_vitals.h): the latch that attributes a breathing
 * rate to the person recognised walking in, and the per-person vitals log
 * in NVS. It needs a name AND a rate. (The vitals state "multiple" does
 * not need it: two counted movers withhold the rates on their own.) */
#if CONFIG_CSI_ID_ENABLE && CONFIG_CSI_VITALS_ENABLE
#define CSI_HAVE_WHO 1
#else
#define CSI_HAVE_WHO 0
#endif

/* The count carried across a presence lapse that the breathing hold
 * undoes (s_cnt_carry). Without the hold, a lapse is simply the end of
 * the visit and nothing is carried. */
#if CONFIG_CSI_COUNT_ENABLE && CONFIG_CSI_VITALS_ENABLE && CONFIG_CSI_VITALS_HOLDS_PRESENCE
#define CSI_CNT_CARRY 1
#else
#define CSI_CNT_CARRY 0
#endif

/* Anything carried across a lapse (the latch with the hold on, or the
 * count) is handed back only once the breathing hold is what keeps
 * PRESENT - not when Step 08 alone reports somebody again, which is also
 * what a walk-in looks like for the first second or two (s_carry_wait). */
#if (CSI_HAVE_WHO && CONFIG_CSI_VITALS_HOLDS_PRESENCE) || CSI_CNT_CARRY
#define CSI_CARRY_WAIT 1
#else
#define CSI_CARRY_WAIT 0
#endif

/* The longest absence the latch and the count are carried across: two
 * vitals windows (60 s by default). Long enough for a still person's
 * breathing (~20 s of data) to bring PRESENT back; after it the room is
 * taken to have been empty, so a later false hold - a fan passing for
 * breathing - can no longer revive the name of somebody who left. */
#define CSI_CARRY_US ((int64_t)2 * CONFIG_CSI_VITALS_WINDOW_S * 1000000)

/* The group signals and the motion gate (motion_gate.h) computed from
 * them serve all three features: the gate decides "still" for vitals, and
 * keeps a still, breathing person out of the gait and count modules. */
#if CONFIG_CSI_VITALS_ENABLE || CSI_HAVE_MODELS
#define CSI_HAVE_GATE 1
#else
#define CSI_HAVE_GATE 0
#endif

/* The activity state and the presence episodes (activity.h) read the
 * gate's decision and ratio; Kconfig lets CSI_ACTIVITY_ENABLE exist only
 * with one of the switches behind CSI_HAVE_GATE, and this says so again. */
#if defined(CONFIG_CSI_ACTIVITY_ENABLE) && CONFIG_CSI_ACTIVITY_ENABLE && CSI_HAVE_GATE
#define CSI_HAVE_ACTIVITY 1
#else
#define CSI_HAVE_ACTIVITY 0
#endif

/* The gate's threshold is a vitals setting (its Kconfig symbol only
 * exists with CSI_VITALS_ENABLE): vitals is what the gate was built for,
 * and where a wrong threshold does the damage. With vitals off the gate
 * still sorts gait / count packets, at the Kconfig default of 4.0. */
#if CONFIG_CSI_VITALS_ENABLE
#define CSI_GATE_RATIO ((float)CONFIG_CSI_VITALS_MOTION_RATIO_X100 / 100.0f)
#else
#define CSI_GATE_RATIO 4.0f
#endif

/* The console reads the USB-Serial-JTAG port through its driver. The
 * driver's VFS glue (usb_serial_jtag_vfs.c) is only compiled when the
 * console output channel IS USB-Serial-JTAG and VFS I/O is on, so both
 * are required here - otherwise the link would fail. */
#if CONFIG_CSI_CONSOLE_ENABLE && CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG && CONFIG_VFS_SUPPORT_IO
#define CSI_HAVE_CONSOLE 1
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "driver/usb_serial_jtag_select.h"   /* usb_serial_jtag_write_ready() */
#else
#define CSI_HAVE_CONSOLE 0
#endif

/* The EXPLAIN lines and "why:" log lines (CSI_EXPLAIN): event-driven,
 * printed by the pipeline task only. */
#if CONFIG_CSI_EXPLAIN
#define CSI_EXPLAIN 1
#else
#define CSI_EXPLAIN 0
#endif

/* Anything that prints the Step 10 outputs (JSON, bar line, 'status',
 * the EXPLAIN "vitals" event - the only EXPLAIN user of report_build). */
#if (CONFIG_CSI_JSON_PERIOD_MS > 0) || CONFIG_CSI_HUMAN_READABLE || CSI_HAVE_CONSOLE || \
    (CSI_EXPLAIN && CONFIG_CSI_VITALS_ENABLE)
#define CSI_HAVE_REPORT 1
#else
#define CSI_HAVE_REPORT 0
#endif

/* vitals_push()'s "an analysis completed on this packet" is needed by the
 * whose-vitals log and by the EXPLAIN "vitals" event. */
#if CONFIG_CSI_VITALS_ENABLE && (CSI_HAVE_WHO || CSI_EXPLAIN)
#define CSI_VIT_DONE 1
#else
#define CSI_VIT_DONE 0
#endif

static const char *TAG = "csi-human";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

#define TAU_FAST_S     0.5f
#define TAU_SLOW_S    60.0f
#define SIGMA_FLOOR    0.005f

/* Smoothing applied to M and D. THIS MUST BE IDENTICAL in calibration phase B
 * and in the detect path. Calibrating on the RAW metric while thresholding a
 * SMOOTHED one inflates the stored std, so every later reading looks smaller
 * than it should and the detector systematically under-triggers. */
#define METRIC_SMOOTH  0.15f
/* Step 08 used 10. Step 10 can stack a gait window, a count block and a
 * vitals FFT on one packet - a combination whose cost has never been
 * measured on the chip (see proc_ms_max / dropped in the JSON). 16 slots
 * ride out 320 ms of stall at 50 Hz (160 ms at 100 Hz) for +6 x
 * sizeof(csi_raw_t) = +4.7 KB of heap. That only works because the ping
 * task outranks the pipeline (ping_start): pings keep their cadence
 * during a stall, and the replies' CSI waits here. */
#define CSI_QUEUE_LEN 16

#define THR_MOTION   (CONFIG_CSI_MOTION_SIGMA_X100   / 100.0f)
#define THR_RELEASE  (CONFIG_CSI_MOTION_RELEASE_X100 / 100.0f)
#define THR_STATIC   (CONFIG_CSI_STATIC_SIGMA_X100   / 100.0f)

/* Receiver gain logging (D-176): only where a JSON line can carry it. */
#if CONFIG_CSI_GAIN_LOG && CONFIG_CSI_JSON_PERIOD_MS > 0
#define CSI_HAVE_GAIN 1
#else
#define CSI_HAVE_GAIN 0
#endif

/* ------------------------------------------------------------------ */
typedef struct {
    /* Step 10: arrival time, taken in the CSI callback. Vitals bin the
     * signal into 100 ms slots by THIS time, not by when the pipeline got
     * round to dequeuing the packet - a 15 ms gait window or an FFT can
     * delay a few packets, and dequeue times would bunch them together. */
    int64_t  t_us;
    uint16_t len;
    uint8_t  fwi;
    int8_t   rssi;
    uint8_t  channel;
#if CSI_HAVE_GAIN
    bool     gain_ok;  /* rx_ctrl long enough for the two gain bytes       */
    uint8_t  agc;      /* csi_gain.h: not a documented field               */
    int8_t   fft;
#endif
    int8_t   buf[CSI_MAX_CSI_BYTES];
} csi_raw_t;

static QueueHandle_t      s_raw_q;
static EventGroupHandle_t s_wifi_events;
static int                s_retry_count = 0;
static esp_ip4_addr_t     s_gateway;
static volatile uint32_t  s_dropped = 0;
static esp_timer_handle_t s_reconnect_timer;
static bool               s_connected_once = false;
static esp_ping_handle_t  s_ping = NULL;
static esp_ip4_addr_t     s_ping_target;

static csi_baseline_t s_base;
static bool           s_have_baseline = false;

static float s_amp[CSI_MAX_SUBC];
static float s_na[CSI_MAX_SUBC];
static float s_scratch[CSI_MAX_SUBC];
static float s_fast[CSI_MAX_SUBC];
static float s_slow[CSI_MAX_SUBC];

static double s_a_mean[CSI_MAX_SUBC];
static double s_a_m2[CSI_MAX_SUBC];
static uint32_t s_a_n = 0;

static double s_m_mean, s_m_m2, s_d_mean, s_d_m2;
static uint32_t s_b_n = 0;

/* ------------------------------------------------------------------ */
/*  Step 10 state. ALL of it is static: vitals_t alone is ~30 KB and   */
/*  gait_t ~11 KB, against a 12 KB pipeline stack. Only the pipeline   */
/*  task reads or writes any of it - the console task merely posts     */
/*  parsed commands to a queue that the pipeline drains - so there is  */
/*  not a single lock in this file.                                    */
/* ------------------------------------------------------------------ */
#if CSI_HAVE_GATE
static float   s_inv_sigma[CSI_MAX_SUBC];  /* 1 / empty-room sigma[k]            */
static int8_t  s_group_map[CSI_MAX_SUBC];  /* subcarrier -> group, -1 = unused   */
static mgate_t s_gate;                     /* "is anybody moving?" for Step 10   */
#endif
#if CSI_HAVE_ACTIVITY
static act_t   s_act;                      /* activity state + presence episodes */
#endif

/* Pipeline load, for the JSON and 'status' (only this task writes them). */
#if CONFIG_CSI_JSON_PERIOD_MS > 0
static uint32_t s_proc_us_max = 0;         /* slowest packet since the last JSON line */
#endif

/* Receiver gain per packet, for the JSON line only (D-176, csi_gain.h):
 * read in the callback, summarised here, taken by each JSON line. */
#if CSI_HAVE_GAIN
static csi_gain_track_t s_gain;            /* zero = csi_gain_reset()          */

/* Detection starts (boot or 'recal'): the calibration's packets were
 * pushed too, but no calibration line takes them, so without this the
 * first detect line would report every gain change since the last one.
 * Log that summary once and start a new period; the last values stay, so
 * a change across the switch still counts on the first line. */
static void gain_detect_start(void)
{
    csi_gain_track_t g;
    if (csi_gain_take(&s_gain, &g)) {
        ESP_LOGI(TAG, "gain before detection: %" PRIu32 " packets, agc %u-%u, %u%s changes"
                      " (agc %u fft %d last)",
                 g.packets, (unsigned)g.agc_lo, (unsigned)g.agc_hi, (unsigned)g.changes,
                 g.changes == UINT16_MAX ? "+" : "", (unsigned)g.agc, (int)g.fft);
    }
}
#else
static inline void gain_detect_start(void) {}
#endif
static uint32_t s_proc_us_max_all = 0;     /* slowest packet since boot               */

/* Why the chip last reset (esp_reset_reason(), read once in app_main).
 * Printed in the banner AND in 'status' and the EXPLAIN config line: a
 * reset re-enumerates USB-Serial-JTAG, and the host reopens the port only
 * after that, so the banner itself is usually lost. */
static esp_reset_reason_t s_reset_reason = ESP_RST_UNKNOWN;

#if CSI_HAVE_CONSOLE
/* The console's transmit side, as the pipeline sees it (console_tx_check,
 * once a second; see console_install for why output never blocks long).
 * s_con_up is written once in app_main before the pipeline task exists. */
static bool     s_con_up       = false;    /* driver installed: its calls are safe */
static uint32_t s_con_full_s   = 0;        /* 1-s checks that found TX full, total  */
static uint32_t s_con_full_run = 0;        /* consecutive such checks, now          */
static uint32_t s_con_full_max = 0;        /* longest run since boot                */
#endif

#if CSI_EXPLAIN
/* The rate analytics_configure() was given - the calibrated packet rate
 * the EMAs and gait frequencies use. 0 = not configured yet. */
static float s_ex_rate_hz = 0.0f;

/* Defined with the other EXPLAIN code, after the JSON buffer they use;
 * called from the analytics above it. */
static void explain_config(void);
#if CONFIG_CSI_ID_ENABLE
static void explain_gait(const char *why, const float *feat, const ncc_result_t *res);
#endif
#if CONFIG_CSI_COUNT_ENABLE
static void explain_count(const char *why, const float *feat);
#endif
#endif

#if CSI_HAVE_MODELS
_Static_assert(GAIT_NFEAT <= NCC_MAX_FEAT, "gait features do not fit an ncc_model_t");
_Static_assert(COUNT_NFEAT <= NCC_MAX_FEAT, "count features do not fit an ncc_model_t");

/* One enrolment or count-training session at a time, started from the
 * console, fed by the per-packet code, ended by reaching its target,
 * 'stop', or a recalibration. */
typedef enum { SESS_NONE = 0, SESS_ENROLL, SESS_COUNT } sess_kind_t;
static struct {
    sess_kind_t kind;
    int         cls;      /* ID slot 0-3, or count class 0 = one / 1 = two+   */
    int         got;      /* feature vectors added so far                     */
    int         target;
} s_sess;
#endif

#if CONFIG_CSI_VITALS_ENABLE
static vitals_t s_vit;
#if CONFIG_CSI_VITALS_HOLDS_PRESENCE
/* Was PRESENT true when the current still episode began? An episode is a
 * run of packets the motion gate calls still; this is set on its first
 * packet. Only such an episode may use breathing to hold PRESENT, so
 * breathing can never START presence in an empty room. */
static bool s_prev_still = false;
static bool s_still_began_present = false;
#endif
#endif

#if CONFIG_CSI_ID_ENABLE
#define ID_NVS_KEY     "id_model"
#define ID_VOTES       7          /* gait windows voting on "person"          */
#define ID_MIN_VOTES   3          /* a name needs at least this many of them  */
/* A slot is used once it has CSI_ID_MIN_WINDOWS windows - but never more
 * than an enrolment collects, or a fresh enrolment could never be used. */
#if CONFIG_CSI_ID_MIN_WINDOWS > CONFIG_CSI_ID_ENROLL_WINDOWS
#define ID_MIN_WINDOWS CONFIG_CSI_ID_ENROLL_WINDOWS
#else
#define ID_MIN_WINDOWS CONFIG_CSI_ID_MIN_WINDOWS
#endif
/* id_tally() results besides a slot index 0..3 */
#define ID_UNKNOWN     (-1)
#define ID_NONE        (-2)       /* no window classified yet this episode    */
#define ID_MULTIPLE    (-3)       /* only for the change log                  */

static gait_t      s_gait;
static ncc_model_t s_id_model;
static int8_t      s_vote_cls[ID_VOTES];   /* slot, or ID_UNKNOWN            */
static float       s_vote_dist[ID_VOTES];  /* distance to nearest template   */
static int         s_vote_n = 0, s_vote_head = 0;
static int         s_id_logged = ID_NONE;  /* last label written to the log  */
#endif

#if CSI_HAVE_WHO
/* Whose vitals (person_vitals.h): the latch that names the person the
 * breathing / heart rate is attributed to, and the per-person vitals log
 * persisted under PV_NVS_KEY. Like everything above it is touched only by
 * the pipeline task - console commands reach it through s_cmd_q and
 * cmd_execute - so it needs no lock. */
#define PV_NVS_KEY     "vit_log"
_Static_assert(PV_LABEL_UNKNOWN == ID_UNKNOWN, "person_vitals.h and main.c disagree on 'unknown'");
static pv_t s_pv;
#endif

#if CONFIG_CSI_COUNT_ENABLE
#define CNT_NVS_KEY    "cnt_model"
#define CNT_VOTES      5          /* blocks voting on the count               */
#define CNT_FEAT_DUTY  4          /* count_est.h: feature [4] is the duty     */
_Static_assert(CNT_FEAT_DUTY < COUNT_NFEAT, "count_est.h feature order changed");

static count_est_t s_cnt;
static ncc_model_t s_cnt_model;            /* class 0 = one, 1 = two or more */
static uint8_t     s_cnt_vote[CNT_VOTES];
static int         s_cnt_vote_n = 0, s_cnt_vote_head = 0;
static int         s_cnt_est = 0;          /* 0 = none this episode, 1 or 2  */
#endif

#if CSI_CNT_CARRY
/* s_cnt_est across a presence lapse that the breathing hold undoes.
 * Presence end wipes s_cnt_est (analytics_reset), and seated people never
 * vote a count again - count_step needs movers - so without this two
 * people who sat down would come back from the lapse as count 1. Kept
 * only while the room stays still and for at most CSI_CARRY_US - the
 * latch's own absence rules (pv_packet), so count and "multiple" agree.
 * Non-zero only while a lapse lasts: from presence end until it is handed
 * back (see s_carry_wait) or dropped. */
static int     s_cnt_carry     = 0;        /* s_cnt_est at presence end, 0 = none */
static int64_t s_cnt_absent_us = 0;        /* when that was, r.t_us clock          */
#endif

#if CSI_CARRY_WAIT
/* PRESENT again, but the lapse is not over for what it carries: set at
 * every presence start, cleared on the first packet on which the
 * breathing hold is asserting PRESENT (then the latch and the count are
 * handed back), on the first packet the gate calls MOVING (by then both
 * have been let go, as for any movement during a lapse), and at presence
 * end / analytics_reset. Why Step 08's own word is not enough: its flag
 * has no hold, while the gate smooths over 0.5 s and then holds 0.3 s, so
 * on a walk-in Step 08 fires FIRST - in test_e2e's synthetic rooms the
 * gate said MOVING 0.4-3.5 s later (and once not within a 6 s walk), too
 * spread out for a fixed waiting time. Ending the lapse at Step 08's
 * start handed a newcomer the name and count of whoever left before
 * them. The hold cannot be fooled that way: any MOVING packet restarts
 * vitals, and a breathing rate needs ~20 s of still data again, so a hold
 * that reasserts PRESENT means nobody the gate saw has moved since the
 * lapse began. The price: a seated person whom Step 08 re-detects has no
 * name until breathing takes over, and loses it if they fidget first or
 * if that takes past CSI_CARRY_US from the lapse - both safe directions.
 * A walk-in the gate never calls MOVING still inherits the name: nothing
 * here can tell it from a still person (the residual, README). */
static bool    s_carry_wait    = false;
#endif

#if CSI_HAVE_WHO
/* PRESENT as far as the whose-vitals latch is concerned: not while a
 * lapse's carry waits for the breathing hold (above) - nothing is named
 * or credited on the strength of Step 08 alone. Costs nothing in
 * practice: during the wait no breathing rate is valid (a valid one in a
 * still spell that began PRESENT IS the hold, which ends the wait). */
static inline bool who_present(bool present)
{
#if CSI_CARRY_WAIT
    return present && !s_carry_wait;
#else
    return present;
#endif
}
#endif

/* ------------------------------------------------------------------ */
/*  CSI callback - minimal work, runs in the WiFi task                 */
/* ------------------------------------------------------------------ */
static void csi_rx_callback(void *ctx, wifi_csi_info_t *info)
{
    if (!info || !info->buf || info->len == 0 || info->len > CSI_MAX_CSI_BYTES) {
        return;
    }
    csi_raw_t r;
    r.t_us    = esp_timer_get_time();
    r.len     = info->len;
    r.fwi     = info->first_word_invalid ? 1 : 0;
    r.rssi    = info->rx_ctrl.rssi;
    r.channel = info->rx_ctrl.channel;
#if CSI_HAVE_GAIN
    r.gain_ok = csi_gain_read(&info->rx_ctrl, sizeof(info->rx_ctrl), &r.agc, &r.fft);
#endif
    memcpy(r.buf, info->buf, info->len);
    if (xQueueSend(s_raw_q, &r, 0) != pdTRUE) {
        s_dropped++;
    }
}

/* ------------------------------------------------------------------ */
/*  How the CSI bytes are packed                                       */
/*                                                                     */
/*  Default mode captures only HT-LTF / HE-LTF (11n / 11ax ping        */
/*  replies): int8 (imag, real) pairs, 2 bytes per subcarrier.         */
/*  'Capture L-LTF only' forces the L-LTF, which the C5 delivers as    */
/*  12-bit samples in little-endian 16-bit words: 4 bytes per          */
/*  subcarrier, 26 subcarriers in the 104 valid bytes of a 106-byte    */
/*  frame. Read as int8 pairs, those bytes give numbers of plausible   */
/*  size that mean nothing. esp-radar decodes C5 L-LTF the same way;   */
/*  lltf_bit_mode stays at its 12-bit default (see Step 05).           */
/* ------------------------------------------------------------------ */
#if CONFIG_CSI_LLTF_ONLY
#define CSI_BYTES_PER_SUBC 4
#else
#define CSI_BYTES_PER_SUBC 2
#endif

static int csi_nsub(uint16_t len, uint8_t fwi)
{
    int off   = fwi ? 4 : 0;
    int valid = (CSI_BYTES_PER_SUBC == 4) ? (len & ~3) : len;
    int n     = (valid - off) / CSI_BYTES_PER_SUBC;
    return n > 0 ? n : 0;
}

static inline float csi_amp(const int8_t *buf, int off, int k)
{
#if CONFIG_CSI_LLTF_ONLY
    const uint8_t *u = (const uint8_t *)buf + off + 4 * k;
    float im = (float)((int16_t)((uint16_t)(u[0] | (u[1] << 8)) << 4) >> 4);
    float re = (float)((int16_t)((uint16_t)(u[2] | (u[3] << 8)) << 4) >> 4);
#else
    float im = (float)buf[off + 2 * k];
    float re = (float)buf[off + 2 * k + 1];
#endif
    return sqrtf(im * im + re * re);
}

/* ------------------------------------------------------------------ */
static float median_of(const float *src, int n, float *scratch)
{
    if (n <= 0) return 0.0f;
    memcpy(scratch, src, n * sizeof(float));
    for (int i = 1; i < n; i++) {
        float v = scratch[i];
        int j = i - 1;
        while (j >= 0 && scratch[j] > v) { scratch[j + 1] = scratch[j]; j--; }
        scratch[j + 1] = v;
    }
    return (n & 1) ? scratch[n / 2] : 0.5f * (scratch[n / 2 - 1] + scratch[n / 2]);
}

/* esp_reset_reason() as a word, for the banner, 'status' and the EXPLAIN
 * config line. The default: a reason added by a later ESP-IDF. */
static const char *reset_reason_name(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_UNKNOWN:    return "UNKNOWN";
    case ESP_RST_POWERON:    return "POWERON";
    case ESP_RST_EXT:        return "EXT";
    case ESP_RST_SW:         return "SW";
    case ESP_RST_PANIC:      return "PANIC";
    case ESP_RST_INT_WDT:    return "INT_WDT";
    case ESP_RST_TASK_WDT:   return "TASK_WDT";
    case ESP_RST_WDT:        return "WDT";
    case ESP_RST_DEEPSLEEP:  return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:   return "BROWNOUT";
    case ESP_RST_SDIO:       return "SDIO";
    case ESP_RST_USB:        return "USB";
    case ESP_RST_JTAG:       return "JTAG";
    case ESP_RST_EFUSE:      return "EFUSE";
    case ESP_RST_PWR_GLITCH: return "PWR_GLITCH";
    case ESP_RST_CPU_LOCKUP: return "CPU_LOCKUP";
    default:                 return "OTHER";
    }
}

/* ------------------------------------------------------------------ */
/*  NVS                                                                */
/* ------------------------------------------------------------------ */
static esp_err_t baseline_load(csi_baseline_t *b)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CSI_NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t sz = sizeof(*b);
    err = nvs_get_blob(h, CSI_NVS_KEY, b, &sz);
    nvs_close(h);
    if (err != ESP_OK) return err;
    if (sz != sizeof(*b) || b->magic != CSI_BASELINE_MAGIC ||
        b->version != CSI_BASELINE_VERSION) {
        return ESP_ERR_INVALID_VERSION;
    }
    return ESP_OK;
}

static esp_err_t baseline_save(const csi_baseline_t *b)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CSI_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, CSI_NVS_KEY, b, sizeof(*b));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

#if CSI_HAVE_MODELS || CSI_HAVE_CONSOLE
/* A person's name as this firmware accepts it: 1..NCC_NAME_LEN-1
 * characters of [A-Za-z0-9_-]. That is what makes it safe to print inside
 * a JSON string or a log line with no escaping. Names enter only through
 * 'enroll' (checked here) or from NVS (the whole model is discarded if a
 * name fails this), so no other string can ever reach printf. */
static bool name_valid(const char *s)
{
    int n = 0;
    for (; s[n] != '\0'; n++) {
        char ch = s[n];
        bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                  (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
        if (!ok || n >= NCC_NAME_LEN - 1) return false;
    }
    return n > 0;
}
#endif

#if CSI_HAVE_MODELS
/* ncc_valid() checks the statistics and that every name is terminated;
 * this adds the printability rule above, and that no class holds data
 * without a name. */
static bool model_names_ok(const ncc_model_t *m)
{
    for (int c = 0; c < NCC_MAX_CLASSES; c++) {
        const ncc_class_t *k = &m->cls[c];
        if (k->name[0] == '\0') {
            if (k->n != 0) return false;
        } else if (!name_valid(k->name)) {
            return false;
        }
    }
    return true;
}

/* Load a classifier model - the same pattern as baseline_load. Anything
 * that is not exactly a model of this firmware (size, magic, version,
 * feature count, a non-finite statistic, an unprintable name) leaves an
 * EMPTY model, never a half-trusted one. */
static esp_err_t model_load(const char *key, ncc_model_t *m, int nfeat)
{
    ncc_init(m, nfeat);
    nvs_handle_t h;
    esp_err_t err = nvs_open(CSI_NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t sz = sizeof(*m);
    err = nvs_get_blob(h, key, m, &sz);
    nvs_close(h);
    if (err == ESP_OK &&
        (sz != sizeof(*m) || !ncc_valid(m, nfeat) || !model_names_ok(m))) {
        err = ESP_ERR_INVALID_VERSION;
    }
    if (err != ESP_OK) {
        ncc_init(m, nfeat);
    }
    return err;
}

static esp_err_t model_save(const char *key, const ncc_model_t *m)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CSI_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, key, m, sizeof(*m));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
#endif

#if CSI_HAVE_WHO
/* The per-person vitals log - the same pattern as model_load: anything
 * pv_log_valid() rejects (size, magic, version, a bad name, a non-finite
 * or out-of-range number) leaves an EMPTY log, never a partly trusted one.
 * A stored blob larger than *l fails in nvs_get_blob itself
 * (ESP_ERR_NVS_INVALID_LENGTH) and ends up the same way. */
static esp_err_t vit_log_load(pv_log_t *l)
{
    pv_log_init(l);
    nvs_handle_t h;
    esp_err_t err = nvs_open(CSI_NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t sz = sizeof(*l);
    err = nvs_get_blob(h, PV_NVS_KEY, l, &sz);
    nvs_close(h);
    if (err == ESP_OK && !pv_log_valid(l, sz)) {
        err = ESP_ERR_INVALID_VERSION;
    }
    if (err != ESP_OK) {
        pv_log_init(l);
    }
    return err;
}

/* Write s_pv's log (~170 bytes) and tell s_pv how it went. pv_saved()
 * stamps every ATTEMPT, so a failing flash backs off for
 * CSI_VITALS_LOG_SAVE_S instead of being retried on every packet; dirty
 * clears only on success. NVS skips the write when an identical blob is
 * already stored, so a save that changes nothing costs no flash wear. */
static esp_err_t vit_log_save(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CSI_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, PV_NVS_KEY, &s_pv.log, sizeof(s_pv.log));
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    pv_saved(&s_pv, esp_timer_get_time(), err == ESP_OK);
    return err;
}
#endif

/* ------------------------------------------------------------------ */
/*  LED + button                                                       */
/* ------------------------------------------------------------------ */
static void led_init(void)
{
#if CONFIG_CSI_LED_GPIO >= 0
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << CONFIG_CSI_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT, .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE, .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    ESP_LOGI(TAG, "presence LED on GPIO %d", CONFIG_CSI_LED_GPIO);
#endif
}

static void led_set(bool on)
{
#if CONFIG_CSI_LED_GPIO >= 0
#if CONFIG_CSI_LED_ACTIVE_LOW
    gpio_set_level(CONFIG_CSI_LED_GPIO, on ? 0 : 1);
#else
    gpio_set_level(CONFIG_CSI_LED_GPIO, on ? 1 : 0);
#endif
#else
    (void)on;
#endif
}

static void button_init(void)
{
#if CONFIG_CSI_BUTTON_GPIO >= 0
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << CONFIG_CSI_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE, .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    ESP_LOGI(TAG, "recalibrate button on GPIO %d (hold 2 s)", CONFIG_CSI_BUTTON_GPIO);
#endif
}

/* Returns true once, after the button has been held for 2 seconds. */
static bool button_held_2s(void)
{
#if CONFIG_CSI_BUTTON_GPIO >= 0
    static int64_t down_since = 0;
    static bool    fired = false;

    if (gpio_get_level(CONFIG_CSI_BUTTON_GPIO) == 0) {   /* active low */
        int64_t now = esp_timer_get_time();
        if (down_since == 0) {
            down_since = now;
        } else if (!fired && (now - down_since) > 2000000) {
            fired = true;
            return true;
        }
    } else {
        down_since = 0;
        fired = false;
    }
#endif
    return false;
}

/* ------------------------------------------------------------------ */
/*  Shape auto-lock                                                    */
/* ------------------------------------------------------------------ */
#define MAX_SHAPES 12
static uint16_t s_shape_len[MAX_SHAPES];
static uint32_t s_shape_cnt[MAX_SHAPES];
static int      s_shape_n = 0, s_observed = 0;
static uint16_t s_locked_len = 0;
static int      s_locked_nsub = 0;
static uint8_t  s_locked_fwi = 0, s_locked_ch = 0;

static void shape_observe(const csi_raw_t *r)
{
    for (int i = 0; i < s_shape_n; i++) {
        if (s_shape_len[i] == r->len) { s_shape_cnt[i]++; return; }
    }
    if (s_shape_n < MAX_SHAPES) {
        s_shape_len[s_shape_n] = r->len;
        s_shape_cnt[s_shape_n] = 1;
        s_shape_n++;
    }
}

static float shape_lock(const csi_raw_t *r)
{
    int best = 0;
    uint32_t total = 0;
    for (int i = 0; i < s_shape_n; i++) {
        if (s_shape_cnt[i] > s_shape_cnt[best]) best = i;
        total += s_shape_cnt[i];
    }
    s_locked_len  = s_shape_len[best];
    s_locked_fwi  = r->fwi;
    s_locked_ch   = r->channel;
    s_locked_nsub = csi_nsub(s_locked_len, s_locked_fwi);
    if (s_locked_nsub > CSI_MAX_SUBC) s_locked_nsub = CSI_MAX_SUBC;
    return total ? (100.0f * s_shape_cnt[best] / total) : 0.0f;
}

/* ------------------------------------------------------------------ */
/*  Main pipeline                                                      */
/* ------------------------------------------------------------------ */
typedef enum {
    PH_LOCK = 0,      /* work out the CSI shape                        */
    PH_LEAVE,         /* countdown so the user can leave the room      */
    PH_WARMUP,        /* settle the EMAs, measure the packet rate      */
    PH_A,             /* per-subcarrier noise -> sigma[k]              */
    PH_SELECT,        /* choose usable subcarriers                     */
    PH_B,             /* empty-room noise floor of M and D             */
    PH_SAVE,          /* write the baseline to NVS                     */
    PH_SETTLE,        /* warm the EMAs before detecting                */
    PH_DETECT,        /* the actual job                                */
} phase_t;

static const char *phase_name(phase_t p)
{
    switch (p) {
        case PH_LOCK:   return "lock";
        case PH_LEAVE:  return "leave-room";
        case PH_WARMUP: return "warmup";
        case PH_A:      return "calib-A";
        case PH_SELECT: return "select";
        case PH_B:      return "calib-B";
        case PH_SAVE:   return "save";
        case PH_SETTLE: return "settle";
        case PH_DETECT: return "detect";
        default:        return "?";
    }
}

/* What the pipeline task knows about its own state that 'status' and
 * 'recal' need. Built by the pipeline once per iteration and read only by
 * the commands it executes itself, in the same task. */
typedef struct {
    phase_t phase;
    bool    present;
    bool    no_csi;          /* the no-CSI watchdog fired: presence UNKNOWN */
    int64_t last_usable_us;  /* esp_timer time of the last accepted packet  */
    float   rate_hz;         /* the rate the EMAs and gait frequencies use  */
    bool    rate_measured;   /* rate_hz is a calibration's (stored or just
                                measured), not 1000 / CSI_PING_INTERVAL_MS  */
    float   live_hz;         /* usable CSI packets per second, last second  */
} pipe_view_t;

/* ================================================================== */
/*  Step 10 analytics                                                  */
/*                                                                     */
/*  Everything below runs in the pipeline task, per accepted packet    */
/*  in PH_DETECT, after Step 08 has made its presence decision. Its    */
/*  only influence on that decision is the breathing hold              */
/*  (CSI_VITALS_HOLDS_PRESENCE); everything else just adds outputs.    */
/* ================================================================== */

#if CONFIG_CSI_ID_ENABLE
static void id_votes_clear(void)
{
    s_vote_n    = 0;
    s_vote_head = 0;
    s_id_logged = ID_NONE;
}

/* The label the vote ring supports right now:
 *   ID_NONE     no window classified yet in this presence episode
 *   0..3        the slot with the most votes, if it has at least
 *               ID_MIN_VOTES of them, strictly more than "unknown", and no
 *               other slot ties it
 *   ID_UNKNOWN  anything else
 * *k = the votes behind that label, *dist = their mean distance (-1 when
 * k == 0, e.g. "unknown" only because no name has enough votes yet). */
static int id_tally(int *k, float *dist)
{
    int   votes[NCC_MAX_CLASSES] = { 0 };
    float dsum[NCC_MAX_CLASSES]  = { 0 };
    int   unk = 0;
    float unk_d = 0.0f;

    *k    = 0;
    *dist = -1.0f;
    if (s_vote_n == 0) return ID_NONE;

    for (int i = 0; i < s_vote_n; i++) {
        int c = s_vote_cls[i];
        if (c >= 0 && c < NCC_MAX_CLASSES) {
            votes[c]++;
            dsum[c] += s_vote_dist[i];
        } else {
            unk++;
            unk_d += s_vote_dist[i];
        }
    }

    int  best = -1;
    bool tie  = false;
    for (int c = 0; c < NCC_MAX_CLASSES; c++) {
        if (votes[c] == 0) continue;
        if (best < 0 || votes[c] > votes[best]) {
            best = c;
            tie  = false;
        } else if (votes[c] == votes[best]) {
            tie = true;
        }
    }
    if (best >= 0 && !tie && votes[best] >= ID_MIN_VOTES && votes[best] > unk) {
        *k    = votes[best];
        *dist = dsum[best] / (float)votes[best];
        return best;
    }
    *k = unk;
    if (unk > 0) *dist = unk_d / (float)unk;
    return ID_UNKNOWN;
}
#endif

#if CONFIG_CSI_COUNT_ENABLE
static void cnt_votes_clear(void)
{
    s_cnt_vote_n    = 0;
    s_cnt_vote_head = 0;
    s_cnt_est       = 0;
#if CSI_CNT_CARRY
    /* Every caller that drops the estimate drops a carried one too: no
     * stale count may come back after a session start, 'count-reset', a
     * cancelled count session, no-CSI or a recalibration. (Presence end
     * re-takes the carry right after analytics_reset(false).) */
    s_cnt_carry     = 0;
#endif
}

/* Both count classes trained. Tested per class rather than as
 * "ncc_trained_classes() == 2", which would also be satisfied by the
 * unused classes 2 and 3 of a model that somehow had data there. */
static bool count_model_ready(void)
{
    return s_cnt_model.cls[0].n >= (uint32_t)CONFIG_CSI_COUNT_TRAIN_WINDOWS &&
           s_cnt_model.cls[1].n >= (uint32_t)CONFIG_CSI_COUNT_TRAIN_WINDOWS;
}
#endif

/* What is thrown away, and when (these are the rules the JSON relies on):
 *   presence ends        -> gait + count accumulators, both vote rings.
 *                           NOT vitals: a still person whose PRESENT lapsed
 *                           after the hold time must keep accumulating the
 *                           ~20 s of breathing that can bring it back.
 *                           NOT the motion gate either: it describes the
 *                           room, whoever Step 08 thinks is in it.
 *                           NOT the whose-vitals latch (pv_presence_end
 *                           decides) nor the count carried over a lapse
 *                           (the caller re-takes it) - both follow the
 *                           breathing hold, which can undo the lapse.
 *                           The wait for that hold (s_carry_wait) ends:
 *                           the caller has read it first.
 *   no-CSI, recalibration -> all of the above AND vitals AND the motion
 *                           gate's filters and decision (with_vitals) AND
 *                           the latch and any carried count AND the
 *                           activity state, ending any presence episode
 *                           (which presence end alone does not). The gate's
 *                           noise floor is KEPT here - an outage does not
 *                           change the room. Callers that change the units
 *                           of grp (a new baseline) clear it.
 *   Models and the vitals log are never touched here; sessions are
 *   handled by the callers. */
static void analytics_reset(bool with_vitals)
{
#if CONFIG_CSI_VITALS_ENABLE
    if (with_vitals) {
        vitals_reset(&s_vit);
#if CONFIG_CSI_VITALS_HOLDS_PRESENCE
        s_prev_still          = false;
        s_still_began_present = false;
#endif
#if CSI_HAVE_WHO
        pv_release(&s_pv);                /* commits a pending episode first */
#endif
    }
#endif
#if CSI_HAVE_GATE
    if (with_vitals) {
        mgate_reset(&s_gate, true);       /* MOVING again, like s_prev_still */
    }
#endif
#if CSI_HAVE_ACTIVITY
    /* Not at presence end: an episode spans a lapse the breathing hold
     * undoes (activity.h). Here it ends, and the state starts again. */
    if (with_vitals) {
        act_reset(&s_act);
    }
#endif
#if !CSI_HAVE_GATE
    (void)with_vitals;
#endif
#if CSI_CARRY_WAIT
    s_carry_wait = false;
#endif
#if CONFIG_CSI_ID_ENABLE
    gait_reset(&s_gait);
    id_votes_clear();
#endif
#if CONFIG_CSI_COUNT_ENABLE
    count_reset(&s_cnt);
    cnt_votes_clear();
#endif
}

#if CSI_HAVE_MODELS
/* A model learnt on a different CSI shape or channel describes a
 * different radio channel entirely. It is still used - the user may have
 * reasons - but they are told. */
static void model_shape_check(const ncc_model_t *m, const char *what, const char *redo)
{
    if (m->csi_len == 0 || ncc_trained_classes(m, 1) == 0) return;
    if (m->csi_len != s_locked_len || m->channel != s_locked_ch) {
        ESP_LOGW(TAG, "%s model was trained on %u-byte CSI, channel %u; now %u bytes, "
                      "channel %u - expect poor results: %s",
                 what, m->csi_len, m->channel, s_locked_len, s_locked_ch, redo);
    }
}

/* A session has just filled class cls, and the model's CSI shape and
 * channel are about to be stamped with the current ones. They are stored
 * once per MODEL, not per class: if the OTHER trained classes were learnt
 * on a different shape or channel, the new stamp hides that from every
 * later boot's model_shape_check - so say it now, while it can be acted
 * on. (A per-class record would need NCC_VERSION 2.) */
static void model_mix_check(const ncc_model_t *m, int cls, const char *what, const char *redo)
{
    bool others = false;
    for (int c = 0; c < NCC_MAX_CLASSES; c++) {
        if (c != cls && m->cls[c].n > 0) others = true;
    }
    if (others && m->csi_len != 0 &&
        (m->csi_len != s_locked_len || m->channel != s_locked_ch)) {
        ESP_LOGW(TAG, "%s model: its other classes were trained on %u-byte CSI, channel %u; this one "
                      "on %u bytes, channel %u - it now mixes two radio channels: %s",
                 what, m->csi_len, m->channel, s_locked_len, s_locked_ch, redo);
    }
}

/* "enroll:raj 12/30", "count-train:2 7/20", or "" when idle. The name is
 * one that passed name_valid(), so the result is JSON-safe. buf holds 64:
 * more than the longest possible result (47 with 11-digit ints). */
static void session_describe(char *buf, size_t len)
{
    buf[0] = '\0';
#if CONFIG_CSI_ID_ENABLE
    if (s_sess.kind == SESS_ENROLL) {
        snprintf(buf, len, "enroll:%s %d/%d", s_id_model.cls[s_sess.cls].name,
                 s_sess.got, s_sess.target);
    }
#endif
#if CONFIG_CSI_COUNT_ENABLE
    if (s_sess.kind == SESS_COUNT) {
        snprintf(buf, len, "count-train:%d %d/%d", s_sess.cls + 1, s_sess.got, s_sess.target);
    }
#endif
}

/* Cancel the active session and DISCARD its data: the class it was
 * filling is cleared. The model is then saved so NVS matches RAM -
 * otherwise a reboot would resurrect the template this session had
 * already wiped in RAM when it started. Returns the save result. */
static esp_err_t session_cancel(void)
{
    esp_err_t err = ESP_OK;
#if CONFIG_CSI_ID_ENABLE
    if (s_sess.kind == SESS_ENROLL) {
        ncc_clear_class(&s_id_model, s_sess.cls);
        err = model_save(ID_NVS_KEY, &s_id_model);
        id_votes_clear();
#if CSI_HAVE_WHO
        pv_slot_gone(&s_pv, s_sess.cls);  /* the slot is empty again */
#endif
    }
#endif
#if CONFIG_CSI_COUNT_ENABLE
    if (s_sess.kind == SESS_COUNT) {
        ncc_clear_class(&s_cnt_model, s_sess.cls);
        err = model_save(CNT_NVS_KEY, &s_cnt_model);
        cnt_votes_clear();
#if CONFIG_CSI_ID_ENABLE
        /* The half-built gait window holds training data - maybe two
         * people - that must not be classified once the session is over. */
        gait_reset(&s_gait);
#endif
    }
#endif
    s_sess.kind = SESS_NONE;
    return err;
}
#endif

/* After a stored baseline is accepted (PH_LOCK) or a fresh one is saved
 * (PH_SAVE), before the first PH_DETECT packet: derive everything that
 * depends on the baseline. rate_hz is the MEASURED packet rate (stored in
 * the baseline, or just measured in PH_WARMUP) - gait frequencies come
 * from it, never from an assumed 50 Hz. */
static void analytics_configure(float rate_hz)
{
    int nsub = s_locked_nsub;
#if CSI_HAVE_GATE
    for (int k = 0; k < nsub; k++) {
        /* sigma is floored at SIGMA_FLOOR when measured; the test only
         * keeps a corrupted blob from dividing by zero. */
        s_inv_sigma[k] = (s_base.sigma[k] > 0.0f) ? 1.0f / s_base.sigma[k] : 0.0f;
    }
    int nsel = dsp_group_map(s_base.selected, nsub, s_group_map);
    ESP_LOGI(TAG, "step 10: %d selected subcarriers in %d groups", nsel, CSI_NGROUPS);
    /* keep_floor = false on BOTH paths here. After a fresh calibration the
     * group signals are in units of the NEW sigma, so an old floor would
     * be in the wrong units. After a baseline reload at boot there is no
     * floor yet anyway. (The no-CSI reset, which does not change units,
     * keeps it - see analytics_reset.) The room is normally empty for a
     * while after a calibration, which is where the floor is learnt. */
    mgate_set_rate(&s_gate, rate_hz, false);
    ESP_LOGI(TAG, "step 10: motion gate at %.1f packets/s, moving at hf >= %.2f",
             rate_hz, CSI_GATE_RATIO);
#endif
#if CONFIG_CSI_VITALS_ENABLE
    vitals_configure(&s_vit, s_base.selected, nsub);
    ESP_LOGI(TAG, "step 10: vitals track %d subcarriers", s_vit.nch);
#endif
#if CONFIG_CSI_ID_ENABLE
    gait_set_rate(&s_gait, rate_hz);
    ESP_LOGI(TAG, "step 10: gait frequencies from the measured %.1f packets/s", rate_hz);
    model_shape_check(&s_id_model, "recognition", "'forget all' and re-enrol");
#else
    (void)rate_hz;
#endif
#if CONFIG_CSI_COUNT_ENABLE
    model_shape_check(&s_cnt_model, "count", "'count-reset' and retrain");
#endif
    analytics_reset(true);
    (void)nsub;
#if CSI_EXPLAIN
    /* Both paths into detection pass here (a stored baseline accepted, a
     * calibration saved - including after 'recal'), so this is where the
     * dashboard learns the thresholds and the models in force. */
    s_ex_rate_hz = rate_hz;
    explain_config();
#endif
}

/* Recalibration (button or 'recal'): an enrolment or count training would
 * straddle two baselines, so it is cancelled; all buffered analytics go.
 * The stored templates and count model are KEPT - but they were learnt
 * against the old baseline's noise levels, so the user is told. */
static void analytics_on_recal(void)
{
#if CSI_HAVE_MODELS
    if (s_sess.kind != SESS_NONE) {
        char what[64];
        session_describe(what, sizeof(what));
        esp_err_t err = session_cancel();
        ESP_LOGW(TAG, "session %s cancelled by the recalibration, its data discarded (NVS: %s)",
                 what, esp_err_to_name(err));
    }
    ESP_LOGW(TAG, "templates and count model kept - re-enrol / retrain if results get worse");
#endif
    analytics_reset(true);
#if CSI_HAVE_GATE
    /* The floor was measured in units of the old baseline's sigma. (PH_SAVE
     * clears it again via analytics_configure; this makes the recal path
     * right on its own.) */
    mgate_reset(&s_gate, false);
#endif
}

/* Follow the Wi-Fi channel of accepted packets. shape_lock() records it
 * once, at boot, but the board rejoins a router that came back on another
 * channel without rebooting (wifi_init_sta pins no channel), and with the
 * same HT20 length its packets are still accepted. Without this, 'status',
 * PH_SAVE and every model saved afterwards would carry the boot-time
 * channel, and the next boot would call fresh templates stale. A channel
 * is adopted only after CH_CONFIRM accepted packets in a row on it, so a
 * stray frame caught during a reconnect scan cannot flip it. Returns true
 * when it adopts one; the caller decides whether to recalibrate (D-168). */
#define CH_CONFIRM 100
static bool channel_follow(uint8_t ch)
{
    static uint8_t cand   = 0;
    static int     cand_n = 0;

    if (ch == 0 || ch == s_locked_ch) {
        cand_n = 0;
        return false;
    }
    if (ch != cand) {
        cand   = ch;
        cand_n = 0;
    }
    if (++cand_n < CH_CONFIRM) return false;
    cand_n = 0;
    ESP_LOGW(TAG, "Wi-Fi channel changed %u -> %u", s_locked_ch, ch);
    s_locked_ch = ch;
#if CONFIG_CSI_ID_ENABLE
    model_shape_check(&s_id_model, "recognition", "'forget all' and re-enrol");
#endif
#if CONFIG_CSI_COUNT_ENABLE
    model_shape_check(&s_cnt_model, "count", "'count-reset' and retrain");
#endif
    return true;
}

#if CSI_HAVE_REPORT || CONFIG_CSI_ID_ENABLE
/* 0 = nobody present; 1 = someone; 2 = two or more (trained model only).
 * With a trained model but no classified block yet in this presence
 * episode it stays 1 - claiming a second person needs evidence. */
static int current_count(bool present)
{
    if (!present) return 0;
#if CONFIG_CSI_COUNT_ENABLE
    if (s_cnt_est > 0) return s_cnt_est;
#endif
    return 1;
}
#endif

#if CONFIG_CSI_COUNT_ENABLE
/* One packet of the count estimator (called only while PRESENT). */
static void count_step(const float grp[CSI_NGROUPS], bool moving)
{
    float feat[COUNT_NFEAT];
    if (!count_push(&s_cnt, grp, moving, feat)) return;
    float duty = feat[CNT_FEAT_DUTY];

    /* ---- training session: every block with enough movement ---- */
    if (s_sess.kind == SESS_COUNT) {
        int cls = s_sess.cls;
        if (!(duty >= COUNT_MIN_DUTY)) {
            ESP_LOGI(TAG, "count-train:%d: block skipped - moving only %.0f%% of it (needs %.0f%%)",
                     cls + 1, 100.0f * duty, 100.0f * COUNT_MIN_DUTY);
#if CSI_EXPLAIN
            explain_count("low-duty", feat);
#endif
            return;
        }
        if (!ncc_add(&s_cnt_model, cls, feat)) {
            ESP_LOGW(TAG, "count-train:%d: block rejected (non-finite features)", cls + 1);
#if CSI_EXPLAIN
            explain_count("nonfinite", feat);
#endif
            return;
        }
        s_sess.got++;
        ESP_LOGI(TAG, "count-train:%d: %d/%d blocks", cls + 1, s_sess.got, s_sess.target);
#if CSI_EXPLAIN
        explain_count("session", feat);
#endif
        if (s_sess.got >= s_sess.target) {
            model_mix_check(&s_cnt_model, cls, "count", "'count-reset' and retrain both");
            s_cnt_model.csi_len = s_locked_len;
            s_cnt_model.channel = s_locked_ch;
            esp_err_t err = model_save(CNT_NVS_KEY, &s_cnt_model);
            s_sess.kind = SESS_NONE;
#if CONFIG_CSI_ID_ENABLE
            /* The half-built gait window holds training data (maybe two
             * people), and s_cnt_est is 0 until the next block: a window
             * completing before it would be classified as one person. */
            gait_reset(&s_gait);
#endif
            ESP_LOGW(TAG, ">>> count-train:%d done (%d blocks), saved: %s - %s",
                     cls + 1, s_sess.got, esp_err_to_name(err),
                     count_model_ready() ? "count model ready" :
                     (cls == 0 ? "now 'count-train 2' with two or more people moving"
                               : "now 'count-train 1' with one person moving"));
#if CSI_EXPLAIN
            explain_config();                     /* the count model changed */
#endif
        }
        return;
    }
    if (s_sess.kind != SESS_NONE) {               /* enrolment running */
#if CSI_EXPLAIN
        explain_count("session", feat);
#endif
        return;
    }

    /* ---- classification: a trained model, and enough movement ----
     * A block where people barely moved carries no information about how
     * many there are, so it neither votes nor changes the estimate. */
    if (!count_model_ready()) {
#if CSI_EXPLAIN
        explain_count("no-model", feat);
#endif
        return;
    }
    if (!(duty >= COUNT_MIN_DUTY)) {
#if CSI_EXPLAIN
        explain_count("low-duty", feat);
#endif
        return;
    }

    /* reject 1e9, margin 1: always the nearest class. Deciding between
     * exactly two trained classes, "neither" is not a useful answer; the
     * 5-block majority below is the protection against one odd block. */
    ncc_result_t res = ncc_classify(&s_cnt_model, feat, (uint32_t)CONFIG_CSI_COUNT_TRAIN_WINDOWS,
                                    1e9f, 1.0f);
    if (res.best != 0 && res.best != 1) {
#if CSI_EXPLAIN
        explain_count("nonfinite", feat);
#endif
        return;
    }

    s_cnt_vote[s_cnt_vote_head] = (uint8_t)res.best;
    s_cnt_vote_head = (s_cnt_vote_head + 1) % CNT_VOTES;
    if (s_cnt_vote_n < CNT_VOTES) s_cnt_vote_n++;

    int twos = 0;
    for (int i = 0; i < s_cnt_vote_n; i++) {
        twos += s_cnt_vote[i];
    }
    /* "two or more" needs a strict majority; a tie stays at one. */
    int est = (2 * twos > s_cnt_vote_n) ? 2 : 1;
    if (est != s_cnt_est) {
        ESP_LOGW(TAG, ">>> count: %s (%d of the last %d blocks say two or more)",
                 est == 2 ? "two or more moving" : "one moving", twos, s_cnt_vote_n);
#if CONFIG_CSI_ID_ENABLE
        /* The count lags by about 3 blocks, so the recognition votes cast
         * until now may be two movers read as one - and id_step casts no
         * more while count is 2. Drop them, or they would name somebody
         * again the moment count is back at 1 (possibly a person who has
         * left). s_id_logged is kept, so "multiple" is still logged once;
         * once count is 1 again "person" is null until fresh windows vote. */
        if (est == 2) {
            s_vote_n    = 0;
            s_vote_head = 0;
        }
#endif
#if CSI_HAVE_WHO
        /* Two movers: no rate can be one person's now, nor after one of
         * them leaves - nobody can tell who stayed. The latch goes to
         * MULTIPLE and stays there until the room has been empty. */
        if (pv_on_count(&s_pv, est) && est >= 2) {
            ESP_LOGW(TAG, ">>> vitals of: nobody (2+ people moving) - rates withheld until "
                          "the room has been empty");
        }
#endif
    }
    s_cnt_est = est;
#if CSI_EXPLAIN
    explain_count(NULL, feat);                    /* after the vote: ring and est are this block's */
#endif
}
#endif

#if CONFIG_CSI_ID_ENABLE
/* One packet of recognition / enrolment (called only while PRESENT).
 * count is this packet's current_count(). */
static void id_step(const float grp[CSI_NGROUPS], bool moving, int count)
{
    float feat[GAIT_NFEAT];
    if (!gait_push(&s_gait, grp, moving, feat)) {
#if CSI_EXPLAIN
        /* gait_push() is false both between windows and for a window that
         * completed but did not qualify. Its public state tells them
         * apart: the hop counter restarts on EVERY completed window,
         * qualifying or not (gait_id.c), and a full ring rules out the
         * zeroed state right after a gait_reset(). A window that was
         * moving enough and still failed had non-finite features. */
        if (s_gait.count == GAIT_NWIN && s_gait.since_window == 0) {
            int n_moving = 0;
            for (int t = 0; t < GAIT_NWIN; t++) n_moving += s_gait.moving[t];
            explain_gait((float)n_moving < GAIT_MIN_MOVING * (float)GAIT_NWIN ? "few-moving"
                                                                               : "nonfinite",
                         NULL, NULL);
        }
#endif
        return;
    }

    /* ---- enrolment session: every window that was moving enough ---- */
    if (s_sess.kind == SESS_ENROLL) {
        int         slot = s_sess.cls;
        const char *name = s_id_model.cls[slot].name;
        if (!ncc_add(&s_id_model, slot, feat)) {
            ESP_LOGW(TAG, "enrol %s: window rejected (non-finite features)", name);
#if CSI_EXPLAIN
            explain_gait("nonfinite", NULL, NULL);
#endif
            return;
        }
        s_sess.got++;
        ESP_LOGI(TAG, "enrol %s: %d/%d windows", name, s_sess.got, s_sess.target);
#if CSI_EXPLAIN
        explain_gait("enrol", NULL, NULL);
#endif
        if (s_sess.got >= s_sess.target) {
            model_mix_check(&s_id_model, slot, "recognition", "'forget all' and re-enrol everyone");
            s_id_model.csi_len = s_locked_len;
            s_id_model.channel = s_locked_ch;
            esp_err_t err = model_save(ID_NVS_KEY, &s_id_model);
            s_sess.kind = SESS_NONE;
            ESP_LOGW(TAG, ">>> enrol %s: done - slot %d, %d windows, saved: %s",
                     name, slot, s_sess.got, esp_err_to_name(err));
#if CSI_EXPLAIN
            explain_config();                     /* a slot became ready */
#endif
        }
        return;
    }
    /* count training: two people may be moving, never classify that */
    if (s_sess.kind != SESS_NONE) {
#if CSI_EXPLAIN
        explain_gait("session", NULL, NULL);
#endif
        return;
    }

    /* Two or more movers: their gaits are superimposed in one signal, and
     * a classifier would confidently name somebody. It is not asked. */
    if (count >= 2) {
        if (s_id_logged != ID_MULTIPLE) {
            s_id_logged = ID_MULTIPLE;
            ESP_LOGW(TAG, ">>> recognised: multiple people moving - not classifying");
        }
#if CSI_EXPLAIN
        explain_gait("multiple", NULL, NULL);
#endif
        return;
    }
    if (ncc_trained_classes(&s_id_model, (uint32_t)ID_MIN_WINDOWS) < 1) {
#if CSI_EXPLAIN
        explain_gait("no-model", NULL, NULL);
#endif
        return;
    }

    ncc_result_t res = ncc_classify(&s_id_model, feat, (uint32_t)ID_MIN_WINDOWS,
                                    (float)CONFIG_CSI_ID_REJECT_X100 / 100.0f,
                                    (float)CONFIG_CSI_ID_MARGIN_X100 / 100.0f);
    if (res.nearest < 0) {                /* no usable template / NaN input */
#if CSI_EXPLAIN
        explain_gait("nonfinite", NULL, NULL);
#endif
        return;
    }

    /* Distances beyond a few units only mean "far"; the cap keeps the
     * printed mean finite and short (ncc returns 1e30 for "infinitely"). */
    s_vote_cls[s_vote_head]  = (int8_t)(res.best >= 0 ? res.best : ID_UNKNOWN);
    s_vote_dist[s_vote_head] = (res.dist < 999.0f) ? res.dist : 999.0f;
    s_vote_head = (s_vote_head + 1) % ID_VOTES;
    if (s_vote_n < ID_VOTES) s_vote_n++;

    /* Log changes of the reported label - but "unknown" only once enough
     * windows exist that a name COULD have been reported; otherwise every
     * episode would open with a meaningless "unknown". */
    int   k = 0;
    float d = -1.0f;
    int   label = id_tally(&k, &d);
    if (label != s_id_logged && (label >= 0 || s_vote_n >= ID_MIN_VOTES)) {
        s_id_logged = label;
        if (label >= 0) {
            ESP_LOGW(TAG, ">>> recognised: %s (dist %.2f, %d/%d windows)",
                     s_id_model.cls[label].name, d, k, s_vote_n);
        } else {
            ESP_LOGW(TAG, ">>> recognised: unknown - no enrolled person has a clear majority "
                          "(%d/%d windows matched nobody)", k, s_vote_n);
        }
    }
#if CSI_EXPLAIN
    explain_gait(NULL, feat, &res);       /* after the vote: ring and tally are this window's */
#endif

#if CSI_HAVE_WHO
    /* Whose vitals: every DECISIVE tally (the same test as the change log
     * above and report_build's "person") goes to the latch - on every vote,
     * not only when the printed label changes, because the latch keeps
     * state the label does not (the visit's first name: A -> unknown -> A
     * recovers, A -> unknown -> B is two people). count <= 1 here: with
     * two movers this function returned above without voting. */
    if (label >= 0 || (label == ID_UNKNOWN && s_vote_n >= ID_MIN_VOTES)) {
        if (pv_on_label(&s_pv, label, label >= 0 ? s_id_model.cls[label].name : NULL)) {
            int       ps = -1;
            pv_attr_t a  = pv_attr(&s_pv, &ps);
            if (a == PV_PERSON && ps >= 0 && ps < NCC_MAX_CLASSES) {
                ESP_LOGW(TAG, ">>> vitals of: %s", s_id_model.cls[ps].name);
            } else if (a == PV_MULTIPLE) {
                ESP_LOGW(TAG, ">>> vitals of: nobody (2 names this visit)");
            } else {
                ESP_LOGW(TAG, ">>> vitals of: nobody (unknown walker)");
            }
        }
    }
#endif
}
#endif

#if CSI_HAVE_REPORT
/* The Step 10 outputs as they stand, for the JSON line, the bar line and
 * 'status' - one definition, so the three can never disagree. */
typedef struct {
    int         count;          /* 0, 1 or 2 (= two or more)                 */
    const char *count_src;      /* "presence" | "trained"                    */
    const char *person;         /* NULL = null; else JSON-safe               */
    float       person_dist;    /* < 0 = null                                */
    int         votes_k, votes_n;   /* votes_n == 0 = null                  */
    const char *vitals;         /* "off" | "no-person" | "multiple" |
                                   vitals state name                         */
    bool        br_ok, brc_ok, hr_ok, hrc_ok;
    float       br_bpm, br_conf, hr_bpm, hr_conf;
    char        session[64];    /* "" = null                                 */
    float       hf;             /* motion gate ratio; < 0 = null             */
    const char *vitals_of;      /* whose the rates are claimed to be; NULL =
                                   null (always, without CSI_HAVE_WHO); else
                                   an enrolled name, so JSON-safe            */
    const char *activity;       /* act_state_name(); NULL = null (always,
                                   without CSI_HAVE_ACTIVITY)                */
    uint32_t    activity_s;     /* seconds in it; only with activity         */
    bool        episode_ok;     /* false = episode_s null                    */
    uint32_t    episode_s;
} report_t;

#if CONFIG_CSI_VITALS_ENABLE
/* "multiple": two or more people this visit, so one breathing rate would
 * be a mix of them and naming one of them a guess - no rate, no
 * vitals_of. Two people seen moving together (count 2 - frozen at 2
 * while they sit, and carried across a lapse the breathing hold undoes),
 * two different names this visit (the latch, sticky until the room has
 * been empty), or a 'count-train 2' session, whose whole premise is two
 * or more people. Blind spot: a second person who sits down before being
 * counted or recognised (person_vitals.h). One definition for the JSON
 * (report_build) and the EXPLAIN "vitals" event. */
static bool vitals_multi(int count)
{
    bool multi = (count >= 2);
#if CSI_HAVE_WHO
    if (pv_multiple(&s_pv)) multi = true;
#endif
#if CONFIG_CSI_COUNT_ENABLE
    if (s_sess.kind == SESS_COUNT && s_sess.cls == 1) multi = true;
#endif
    return multi;
}
#endif

static void report_build(report_t *o, bool present)
{
    memset(o, 0, sizeof(*o));
    o->count       = current_count(present);
    o->count_src   = "presence";
    o->person      = NULL;
    o->person_dist = -1.0f;
    o->vitals      = "off";
    o->hf          = -1.0f;
    o->vitals_of   = NULL;
    o->activity    = NULL;

#if CSI_HAVE_GATE
    /* Room-level, so reported whether or not anybody is PRESENT: in an
     * empty room it is what shows the floor is sane (hf ~1). */
    o->hf = mgate_ratio(&s_gate);
#endif

#if CONFIG_CSI_COUNT_ENABLE
    if (present && s_cnt_est > 0) o->count_src = "trained";
#endif

#if CONFIG_CSI_ID_ENABLE
    /* null: not present, or no usable template. "multiple" wins over the
     * votes: they may be from before the second person arrived. */
    if (present && ncc_trained_classes(&s_id_model, (uint32_t)ID_MIN_WINDOWS) > 0) {
        if (o->count >= 2) {
            o->person = "multiple";
        } else {
            int   k = 0;
            float d = -1.0f;
            int   label = id_tally(&k, &d);
            /* "unknown" only once a name COULD have been reported - the
             * same rule as id_step's change log. Before that (the first
             * ID_MIN_VOTES - 1 windows of every visit, which cannot yet
             * name anybody) person stays null, not a zero-vote "unknown". */
            if (label >= 0 || (label == ID_UNKNOWN && s_vote_n >= ID_MIN_VOTES)) {
                o->person      = (label >= 0) ? s_id_model.cls[label].name : "unknown";
                o->person_dist = d;
                o->votes_k     = k;
                o->votes_n     = s_vote_n;
            }
        }
    }
#endif

#if CONFIG_CSI_VITALS_ENABLE
    /* "multiple": two or more people this visit (vitals_multi). */
    bool multi = vitals_multi(o->count);

    /* Only while PRESENT. Vitals keep running while absent, so that a
     * still person's breathing can bring PRESENT back, but a rate is never
     * reported for a room the detector calls empty. Precedence: "off"
     * (not configured yet - before the first baseline), "no-person",
     * "multiple", then vitals' own state. */
    const vitals_out_t *v = vitals_get(&s_vit);
    if (v->state == VIT_OFF) {
        o->vitals = "off";
    } else if (!present) {
        o->vitals = "no-person";
    } else if (multi) {
        o->vitals = "multiple";              /* all four *_ok stay false */
    } else {
        bool analysed = (v->state == VIT_OK || v->state == VIT_NO_SIGNAL);
        o->vitals  = vitals_state_name(v->state);
        o->br_ok   = v->br_valid;
        o->br_bpm  = v->br_bpm;
        o->brc_ok  = analysed;               /* conf even when below threshold */
        o->br_conf = v->br_conf;
        o->hr_ok   = v->hr_valid;            /* hr_valid implies br_valid      */
        o->hr_bpm  = v->hr_bpm;
        /* hr_conf only WITH a heart rate. vitals.c also fills it when the
         * peak is withheld - on a band edge, or as the shoulder of a
         * notched breathing harmonic, where it can read 30+ - and
         * "hr_conf":34 beside "hr_bpm_exp":null would read as a strong
         * heartbeat that was somehow not reported. */
        o->hrc_ok  = v->hr_valid;
        o->hr_conf = v->hr_conf;
#if CSI_HAVE_WHO
        /* The last person recognised walking in ALONE this visit - a
         * carried-over claim, not a measurement (person_vitals.h). Only
         * beside rates that are reported at all, and not while a lapse's
         * carry still waits for the breathing hold (who_present). */
        o->vitals_of = pv_vitals_of(&s_pv, who_present(present), o->count);
#endif
    }
#endif

#if CSI_HAVE_MODELS
    session_describe(o->session, sizeof(o->session));
#endif

#if CSI_HAVE_ACTIVITY
    /* Room-level like hf, and not gated on `present`: "empty" is one of
     * its states, and a pending lapse keeps its episode (activity.h). */
    o->activity   = act_state_name(act_state(&s_act));
    o->activity_s = act_state_s(&s_act);
    o->episode_ok = act_episode_s(&s_act, &o->episode_s);
#endif
}
#endif

/* Users: the bar line, the "why:" lines (explain_presence always), and
 * 'status' - whose every fmt_num call sits in a block that needs the
 * motion gate (recognition, vitals or the gate itself), hence
 * CSI_HAVE_GATE: with all three features off and the bar line and
 * CSI_EXPLAIN off, nothing would call it. */
#if CONFIG_CSI_HUMAN_READABLE || (CSI_HAVE_CONSOLE && CSI_HAVE_GATE) || CSI_EXPLAIN
/* A number, or "-" when there is none (or it is not finite). */
static void fmt_num(char *buf, size_t len, bool ok, int decimals, float v)
{
    if (ok && isfinite(v)) {
        snprintf(buf, len, "%.*f", decimals, v);
    } else {
        snprintf(buf, len, "-");
    }
}
#endif

#if CONFIG_CSI_JSON_PERIOD_MS > 0 || CSI_EXPLAIN
/* The detect-phase JSON line is assembled here and printed with ONE
 * printf, so an acknowledgement from the console task cannot land in the
 * middle of it. 1024 bytes: the example at the top of this file is 457,
 * and no line can exceed 862 + NUL - the bound with all ten floats at
 * -FLT_MAX (43 characters as %.2f, 42 as %.1f; proc_ms_max is at most 9),
 * 10-digit t_ms and dropped, a 15-character name in both person and
 * vitals_of (the latter is 30 of the 862), the longest session
 * ("enroll:<15> 200/200"), the activity keys at their longest (69:
 * "restless" and two 10-digit second counts; json_activity), and the gain
 * keys (38, json_gain): 900 + NUL since D-176. Overflow is still checked - a truncated line
 * would be invalid JSON, so it is not printed. The EXPLAIN lines are built
 * in the same buffer (same task, one line at a time; their bound is at
 * EX_LINE_BOUND). */
static char   s_json[1024];
static size_t s_json_len;
static bool   s_json_over;

static void json_begin(void)
{
    s_json[0]   = '\0';
    s_json_len  = 0;
    s_json_over = false;
}

static char  *json_end(void)  { return s_json + s_json_len; }
static size_t json_room(void) { return sizeof(s_json) - s_json_len; }

/* Account for n characters just written at json_end() by snprintf. */
static void json_adv(int n)
{
    if (n < 0 || (size_t)n >= json_room()) {
        s_json_over = true;
        s_json_len  = sizeof(s_json) - 1;
    } else {
        s_json_len += (size_t)n;
    }
}

/* ,"key":"s" - or ,"key":null when s is NULL. s is NOT escaped: callers
 * pass literals, vitals_state_name(), or names that passed name_valid(). */
static void json_str(const char *key, const char *s)
{
    if (s) {
        json_adv(snprintf(json_end(), json_room(), ",\"%s\":\"%s\"", key, s));
    } else {
        json_adv(snprintf(json_end(), json_room(), ",\"%s\":null", key));
    }
}

static void json_print(void)
{
    if (s_json_over) {
        /* The start of the line says which one it was ("JSON {..." or
         * "EXPLAIN {"ev":"..."). */
        ESP_LOGE(TAG, "output line longer than %u bytes - not printed (%.28s...)",
                 (unsigned)sizeof(s_json), s_json);
        return;
    }
    printf("%s\n", s_json);
}
#endif

#if CONFIG_CSI_JSON_PERIOD_MS > 0 || (CSI_EXPLAIN && CONFIG_CSI_VITALS_ENABLE)
/* ,"activity":"still","activity_s":42,"episode_s":318 - each null when
 * there is none (all three without CSI_ACTIVITY_ENABLE). The last keys of
 * the JSON line and of the EXPLAIN "vitals" event. At most 69 characters:
 * "restless" and two 10-digit seconds. */
static void json_activity(const report_t *o)
{
    json_str("activity", o->activity);
    if (o->activity) {
        json_adv(snprintf(json_end(), json_room(), ",\"activity_s\":%" PRIu32, o->activity_s));
    } else {
        json_str("activity_s", NULL);
    }
    if (o->episode_ok) {
        json_adv(snprintf(json_end(), json_room(), ",\"episode_s\":%" PRIu32, o->episode_s));
    } else {
        json_str("episode_s", NULL);
    }
}
#endif

#if CONFIG_CSI_JSON_PERIOD_MS > 0
/* ,"key":1.23 - or ,"key":null, also for a NaN or an infinity: printf
 * would write "nan" / "inf", which is not JSON, and one such token makes
 * the host drop the whole line. (IDF does not build with -ffast-math, so
 * isfinite() is a real test here.) */
static void json_num(const char *key, bool ok, float v, int decimals)
{
    if (ok && isfinite(v)) {
        json_adv(snprintf(json_end(), json_room(), ",\"%s\":%.*f", key, decimals, v));
    } else {
        json_adv(snprintf(json_end(), json_room(), ",\"%s\":null", key));
    }
}

/* ,"agc":36,"fft":-2,"gain_chg":0 - the receiver gain of the last packet
 * since the previous detect line and how many packets changed it (D-176,
 * csi_gain.h); all three null without CSI_GAIN_LOG or with no packet
 * since the previous line. The first line after calibration starts at the
 * switch to detection (gain_detect_start). Appended after episode_s. At
 * most 38 characters ("agc":255, "fft":-128, "gain_chg":65535, or three
 * nulls). */
static void json_gain(void)
{
#if CSI_HAVE_GAIN
    csi_gain_track_t g;
    if (csi_gain_take(&s_gain, &g)) {
        json_adv(snprintf(json_end(), json_room(), ",\"agc\":%u,\"fft\":%d,\"gain_chg\":%u",
                          (unsigned)g.agc, (int)g.fft, (unsigned)g.changes));
        return;
    }
#endif
    json_str("agc", NULL);
    json_str("fft", NULL);
    json_str("gain_chg", NULL);
}

/* The Step 10 keys, then the diagnostics, then keys added since
 * (vitals_of; activity, activity_s, episode_s; agc, fft, gain_chg), then
 * the closing brace.
 * The rule is that no key ever moves: the diagnostics were last when they
 * were added, and every newer key is appended after "dropped" rather than
 * slotted in beside its relatives - so a host that reads fields by
 * position, or a CSV with an old header, still lines up. vitals_of is
 * written in every build (null when CSI_ID_ENABLE or CSI_VITALS_ENABLE is
 * off), and so are the activity keys (all three null without
 * CSI_ACTIVITY_ENABLE), so the key set never depends on the build either.
 * proc_us_max: slowest packet since the previous line - the packet that
 * prints this line is still being processed, so it counts towards the
 * next one. dropped: s_dropped, read once by the caller. */
static void json_step10(const report_t *o, uint32_t proc_us_max, uint32_t dropped)
{
    json_adv(snprintf(json_end(), json_room(), ",\"count\":%d,\"count_src\":\"%s\"",
                      o->count, o->count_src));
    json_str("person", o->person);
    json_num("person_dist", o->person_dist >= 0.0f, o->person_dist, 2);
    if (o->votes_n > 0) {
        json_adv(snprintf(json_end(), json_room(), ",\"person_votes\":\"%d/%d\"",
                          o->votes_k, o->votes_n));
    } else {
        json_str("person_votes", NULL);
    }
    json_str("vitals", o->vitals);
    json_num("br_bpm", o->br_ok, o->br_bpm, 1);
    json_num("br_conf", o->brc_ok, o->br_conf, 1);
    json_num("hr_bpm_exp", o->hr_ok, o->hr_bpm, 1);
    json_num("hr_conf", o->hrc_ok, o->hr_conf, 1);
    json_str("session", o->session[0] ? o->session : NULL);
    json_num("hf", o->hf >= 0.0f, o->hf, 2);
    json_adv(snprintf(json_end(), json_room(), ",\"proc_ms_max\":%.1f,\"dropped\":%" PRIu32,
                      (float)proc_us_max / 1000.0f, dropped));
    json_str("vitals_of", o->vitals_of);
    json_activity(o);
    json_gain();
    json_adv(snprintf(json_end(), json_room(), "}"));
}
#endif

#if CSI_EXPLAIN
/* ================================================================== */
/*  EXPLAIN lines and "why:" log lines (CSI_EXPLAIN)                   */
/*                                                                     */
/*  All of it runs in the pipeline task, once per decision event - a   */
/*  gait window, a count block, a vitals analysis, a presence change,  */
/*  a calibration, a model change, 'explain' - never per packet, except */
/*  the "amp" line, rate-limited to CSI_EXPLAIN_AMP_HZ. The protocol is */
/*  documented in the header comment at the top of this file.          */
/*                                                                     */
/*  Each EXPLAIN line is built in s_json and printed with ONE printf   */
/*  (json_print), so nothing can land in the middle of it; a line that */
/*  would not fit is not printed at all.                               */
/*                                                                     */
/*  EX_LINE_BOUND - the longest each line can be, counted (2026-09-25) */
/*  from the format strings below with every number at its widest      */
/*  (ex_val clamps: "-999999.999"), 10-digit t_ms and counters, every  */
/*  Kconfig int at its range limit, 15-character names, and the "\n":  */
/*    config    896  (its four slots with 15-character names: 223)     */
/*    amp       821  (64 values at the clamp; 629 at the largest       */
/*                    amplitude a 12-bit sample can give, na <= 2896.31) */
/*    gait 305, vitals 272, count 243, cal 221, presence 112           */
/*  Every line fits the 1024-byte buffer, and the longest is 22% of    */
/*  the 4 KB console transmit ring (console_install). Typical at 26    */
/*  subcarriers (L-LTF): amp ~230, gait ~280, count ~200, config ~560. */
/*  Add a key -> redo this count: a line that does not fit is dropped  */
/*  whole (json_print logs it), never printed truncated.               */
/* ================================================================== */
#define EX_NUM_MAX   999999.0f      /* every number is clamped to +/- this   */
#define EX_DIST_MAX  999.0f         /* template distances, as person_dist    */
#define EX_AMP_MAX   64             /* values per "amp" line                 */

/* "EXPLAIN {"ev":"<ev>","t_ms":<ms>" - t_ms on the JSON line's clock. */
static void ex_begin(const char *ev)
{
    json_begin();
    json_adv(snprintf(json_end(), json_room(), "EXPLAIN {\"ev\":\"%s\",\"t_ms\":%" PRIu32,
                      ev, (uint32_t)(esp_timer_get_time() / 1000)));
}

/* pre, then v with dec (0-3) decimals - or pre + "null" when !ok or v is
 * not finite. Integer arithmetic only: one float multiply and one
 * conversion, then an integer printf. Never the %f path: picolibc
 * promotes a float to double and runs dtoa, and on this FPU-less core that
 * is what makes printing a float expensive - the amp line prints up to 64
 * of them. Rounds half away from zero; clamped to +/-EX_NUM_MAX, so a
 * number is at most 11 characters ("-999999.999"; the scaled value,
 * <= 999999000, fits int32). */
static void ex_val(const char *pre, bool ok, float v, int dec)
{
    static const int32_t k_scale[4] = { 1, 10, 100, 1000 };
    if (!ok || !isfinite(v)) {
        json_adv(snprintf(json_end(), json_room(), "%snull", pre));
        return;
    }
    if (dec < 0) dec = 0;
    if (dec > 3) dec = 3;
    if (v > EX_NUM_MAX)  v = EX_NUM_MAX;
    if (v < -EX_NUM_MAX) v = -EX_NUM_MAX;
    float       f    = v * (float)k_scale[dec];
    int32_t     q    = (int32_t)(f + (f >= 0.0f ? 0.5f : -0.5f));
    uint32_t    u    = (q < 0) ? (uint32_t)(-q) : (uint32_t)q;
    const char *sign = (q < 0) ? "-" : "";
    if (dec == 0) {
        json_adv(snprintf(json_end(), json_room(), "%s%s%" PRIu32, pre, sign, u));
    } else {
        uint32_t sc = (uint32_t)k_scale[dec];
        json_adv(snprintf(json_end(), json_room(), "%s%s%" PRIu32 ".%0*" PRIu32,
                          pre, sign, u / sc, dec, u % sc));
    }
}

/* ,"key":<number> | ,"key":null */
static void ex_num(const char *key, bool ok, float v, int dec)
{
    json_adv(snprintf(json_end(), json_room(), ",\"%s\":", key));
    ex_val("", ok, v, dec);
}

static void ex_bool(const char *key, bool b)
{
    json_adv(snprintf(json_end(), json_room(), ",\"%s\":%s", key, b ? "true" : "false"));
}

static void ex_end(void)
{
    json_adv(snprintf(json_end(), json_room(), "}"));
    json_print();
}

/* The "why:" line under construction: pieces are appended with why_cat
 * and the whole line is logged once (why_log). Static - the pipeline task
 * is the only writer, and 320 bytes stay off its stack. The longest line
 * (a gait window with two 15-character names and an "unknown" tally) is
 * ~250; truncation would only shorten a log line, so it is clipped. */
static char   s_why[320];
static size_t s_why_len;

static void why_cat(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void why_cat(const char *fmt, ...)
{
    if (s_why_len >= sizeof(s_why) - 1) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(s_why + s_why_len, sizeof(s_why) - s_why_len, fmt, ap);
    va_end(ap);
    if (n > 0) {
        s_why_len += (size_t)n;
        if (s_why_len > sizeof(s_why) - 1) s_why_len = sizeof(s_why) - 1;
    }
}

static void why_start(void)
{
    s_why[0]  = '\0';
    s_why_len = 0;
}

static void why_log(void)
{
    ESP_LOGI(TAG, "why: %s", s_why);
}

/* ---- "config": the live thresholds and the models in force ---- */
static void explain_config(void)
{
    ex_begin("config");
    json_str("reset", reset_reason_name(s_reset_reason));
    ex_num("rate_hz", s_ex_rate_hz > 0.0f, s_ex_rate_hz, 1);
    if (s_locked_len > 0) {
        json_adv(snprintf(json_end(), json_room(),
                          ",\"csi_len\":%u,\"nsub\":%d,\"nsel\":%u,\"channel\":%u",
                          (unsigned)s_locked_len, s_locked_nsub, (unsigned)s_base.n_selected,
                          (unsigned)s_locked_ch));
    } else {                                      /* 'explain' before the shape lock */
        json_adv(snprintf(json_end(), json_room(),
                          ",\"csi_len\":null,\"nsub\":null,\"nsel\":null,\"channel\":null"));
    }
#if CONFIG_CSI_LLTF_ONLY
    ex_bool("lltf", true);
#else
    ex_bool("lltf", false);
#endif
    ex_num("thr_motion", true, THR_MOTION, 2);
    ex_num("thr_release", true, THR_RELEASE, 2);
    ex_num("thr_static", true, THR_STATIC, 2);
    json_adv(snprintf(json_end(), json_room(), ",\"static_sustain_ms\":%d,\"presence_hold_ms\":%d",
                      CONFIG_CSI_STATIC_SUSTAIN_MS, CONFIG_CSI_PRESENCE_HOLD_MS));
#if CSI_HAVE_GATE
    ex_num("gate_ratio", true, CSI_GATE_RATIO, 2);
#else
    json_str("gate_ratio", NULL);
#endif
    json_adv(snprintf(json_end(), json_room(), ",\"amp_hz\":%d", CONFIG_CSI_EXPLAIN_AMP_HZ));

#if CONFIG_CSI_ID_ENABLE
    json_adv(snprintf(json_end(), json_room(), ",\"id\":{\"reject\":"));
    ex_val("", true, (float)CONFIG_CSI_ID_REJECT_X100 / 100.0f, 2);
    ex_num("margin", true, (float)CONFIG_CSI_ID_MARGIN_X100 / 100.0f, 2);
    json_adv(snprintf(json_end(), json_room(), ",\"votes\":%d,\"min_votes\":%d,\"win\":%d,\"hop\":%d",
                      ID_VOTES, ID_MIN_VOTES, GAIT_NWIN, GAIT_HOP));
    ex_num("min_moving", true, GAIT_MIN_MOVING, 2);
    json_adv(snprintf(json_end(), json_room(), ",\"min_windows\":%d,\"slots\":[", ID_MIN_WINDOWS));
    for (int s = 0; s < NCC_MAX_CLASSES; s++) {
        const ncc_class_t *k = &s_id_model.cls[s];
        if (k->name[0] != '\0') {
            json_adv(snprintf(json_end(), json_room(), "%s{\"name\":\"%s\"", s ? "," : "", k->name));
        } else {
            json_adv(snprintf(json_end(), json_room(), "%s{\"name\":null", s ? "," : ""));
        }
        json_adv(snprintf(json_end(), json_room(), ",\"n\":%" PRIu32 ",\"ready\":%s}",
                          k->n, k->n >= (uint32_t)ID_MIN_WINDOWS ? "true" : "false"));
    }
    json_adv(snprintf(json_end(), json_room(), "]}"));
#else
    json_str("id", NULL);
#endif

#if CONFIG_CSI_COUNT_ENABLE
    json_adv(snprintf(json_end(), json_room(), ",\"count\":{\"votes\":%d,\"win\":%d,\"min_duty\":",
                      CNT_VOTES, COUNT_NWIN));
    ex_val("", true, COUNT_MIN_DUTY, 2);
    json_adv(snprintf(json_end(), json_room(),
                      ",\"need\":%d,\"ready\":%s,\"n1\":%" PRIu32 ",\"n2\":%" PRIu32 "}",
                      CONFIG_CSI_COUNT_TRAIN_WINDOWS, count_model_ready() ? "true" : "false",
                      s_cnt_model.cls[0].n, s_cnt_model.cls[1].n));
#else
    json_str("count", NULL);
#endif

#if CONFIG_CSI_VITALS_ENABLE
    /* s_vit.cfg: the values in force, after vitals_init's clamping */
    json_adv(snprintf(json_end(), json_room(), ",\"vitals\":{\"window_s\":%d,\"min_s\":%d,\"br_conf_min\":",
                      s_vit.cfg.window_s, s_vit.cfg.min_s));
    ex_val("", true, s_vit.cfg.br_conf_min, 2);
    ex_num("hr_conf_min", true, s_vit.cfg.hr_conf_min, 2);
#if CONFIG_CSI_VITALS_HOLDS_PRESENCE
    ex_bool("holds_presence", true);
#else
    ex_bool("holds_presence", false);
#endif
    json_adv(snprintf(json_end(), json_room(), "}"));
#else
    json_str("vitals", NULL);
#endif
    ex_end();
}

/* ---- "cal": the empty-room baseline in use ---- */
static void explain_cal(const char *src)
{
    ex_begin("cal");
    json_str("src", src);
    ex_num("m_mean", true, s_base.m_mean, 3);
    ex_num("m_std", true, s_base.m_std, 3);
    ex_num("d_mean", true, s_base.d_mean, 3);
    ex_num("d_std", true, s_base.d_std, 3);
    json_adv(snprintf(json_end(), json_room(),
                      ",\"nsub_selected\":%u,\"nsub\":%u,\"csi_len\":%u,\"channel\":%u",
                      (unsigned)s_base.n_selected, (unsigned)s_base.nsub,
                      (unsigned)s_base.csi_len, (unsigned)s_base.channel));
    ex_num("rate_hz", true, s_base.rate_hz, 1);
    ex_end();

    why_start();
    why_cat("baseline (%s) - empty room: motion metric %.3f +/- %.3f, deviation %.3f +/- %.3f; "
            "PRESENT above motion %.3f (%.1f sd) or deviation %.3f (%.1f sd, for %d ms)",
            src, s_base.m_mean, s_base.m_std, s_base.d_mean, s_base.d_std,
            s_base.m_mean + THR_MOTION * s_base.m_std, THR_MOTION,
            s_base.d_mean + THR_STATIC * s_base.d_std, THR_STATIC, CONFIG_CSI_STATIC_SUSTAIN_MS);
    why_log();
}

/* ---- "amp": the CSI waterfall (rate-limited by the caller) ---- */
static void explain_amp(float m, float d, bool present)
{
    int nsel   = s_base.n_selected;
    int stride = (nsel + EX_AMP_MAX - 1) / EX_AMP_MAX;   /* 1 up to 64 selected */
    if (stride < 1) stride = 1;

    ex_begin("amp");
    json_adv(snprintf(json_end(), json_room(), ",\"a\":["));
    int j = 0, n = 0;
    for (int k = 0; k < s_locked_nsub && n < EX_AMP_MAX; k++) {
        if (!s_base.selected[k]) continue;
        if ((j++ % stride) != 0) continue;
        ex_val(n++ ? "," : "", true, s_na[k], 2);
    }
    json_adv(snprintf(json_end(), json_room(), "]"));
    ex_num("m", true, m, 2);
    ex_num("d", true, d, 2);
#if CSI_HAVE_GATE
    float hf = mgate_ratio(&s_gate);
    ex_num("hf", hf >= 0.0f, hf, 2);
    ex_bool("still", mgate_still(&s_gate));
#else
    json_str("hf", NULL);
    json_str("still", NULL);
#endif
    json_adv(snprintf(json_end(), json_room(), ",\"present\":%d", present ? 1 : 0));
    ex_end();
}

#if CONFIG_CSI_ID_ENABLE
static const char *slot_name(int s)
{
    return (s >= 0 && s < NCC_MAX_CLASSES && s_id_model.cls[s].name[0] != '\0')
           ? s_id_model.cls[s].name : "?";
}

/* ---- "gait": one completed gait window ----
 * why == NULL: the window was classified (feat, res valid) and has just
 * voted; otherwise why says why not. Called AFTER the vote, so ring and
 * tally include it. The window's packets are still in s_gait's ring
 * (gait_push slides it, never clears it), so moving_frac is recounted
 * here - 256 byte adds, once per window. */
static void explain_gait(const char *why, const float *feat, const ncc_result_t *res)
{
    static uint32_t win = 0;
    win++;

    int n_moving = 0;
    for (int t = 0; t < GAIT_NWIN; t++) n_moving += s_gait.moving[t];
    float frac   = (float)n_moving / (float)GAIT_NWIN;
    float reject = (float)CONFIG_CSI_ID_REJECT_X100 / 100.0f;
    float margin = (float)CONFIG_CSI_ID_MARGIN_X100 / 100.0f;
    bool  used   = (why == NULL && feat != NULL && res != NULL);

    float       dist[NCC_MAX_CLASSES];
    int         second = -1;                      /* runner-up slot */
    const char *reason = NULL, *vote = NULL;
    for (int c = 0; c < NCC_MAX_CLASSES; c++) dist[c] = NCC_NO_DIST;
    if (used) {
        ncc_distances(&s_id_model, feat, (uint32_t)ID_MIN_WINDOWS, dist);
        for (int c = 0; c < NCC_MAX_CLASSES; c++) {
            if (c == res->nearest || dist[c] < 0.0f) continue;
            if (second < 0 || dist[c] < dist[second]) second = c;
        }
        reason = (res->best >= 0) ? "ok" : (!(res->dist <= reject) ? "too-far" : "ambiguous");
        vote   = (res->best >= 0) ? s_id_model.cls[res->best].name : "unknown";
    }

    /* the vote ring, oldest first: slot digit, '?' = unknown */
    char ring[ID_VOTES + 1];
    int  oldest = (s_vote_n < ID_VOTES) ? 0 : s_vote_head;
    for (int i = 0; i < s_vote_n; i++) {
        int c = s_vote_cls[(oldest + i) % ID_VOTES];
        ring[i] = (c >= 0 && c < NCC_MAX_CLASSES) ? (char)('0' + c) : '?';
    }
    ring[s_vote_n] = '\0';

    /* the tally, with report_build's rule for "unknown" */
    int         k     = 0;
    float       kd    = -1.0f;
    int         label = id_tally(&k, &kd);
    const char *lname = (label >= 0) ? s_id_model.cls[label].name :
                        ((label == ID_UNKNOWN && s_vote_n >= ID_MIN_VOTES) ? "unknown" : NULL);

    ex_begin("gait");
    json_adv(snprintf(json_end(), json_room(), ",\"win\":%" PRIu32, win));
    ex_num("moving_frac", true, frac, 2);
    ex_bool("used", used);
    json_str("why", why);
    if (used) {
        json_adv(snprintf(json_end(), json_room(), ",\"dist\":["));
        for (int c = 0; c < NCC_MAX_CLASSES; c++) {
            ex_val(c ? "," : "", dist[c] >= 0.0f, dist[c] < EX_DIST_MAX ? dist[c] : EX_DIST_MAX, 2);
        }
        json_adv(snprintf(json_end(), json_room(), "],\"nearest\":%d", res->nearest));
        float d2 = (second >= 0) ? dist[second] : 0.0f;
        ex_num("second_dist", second >= 0, d2 < EX_DIST_MAX ? d2 : EX_DIST_MAX, 2);
        json_adv(snprintf(json_end(), json_room(), ",\"best\":%d", res->best));
    } else {
        json_adv(snprintf(json_end(), json_room(),
                          ",\"dist\":null,\"nearest\":null,\"second_dist\":null,\"best\":null"));
    }
    json_str("reason", reason);
    json_str("vote", vote);
    json_str("ring", ring);
    if (lname) {
        json_adv(snprintf(json_end(), json_room(), ",\"tally\":{\"label\":\"%s\"", lname));
    } else {
        json_adv(snprintf(json_end(), json_room(), ",\"tally\":{\"label\":null"));
    }
    json_adv(snprintf(json_end(), json_room(), ",\"k\":%d,\"n\":%d}", k, s_vote_n));
    ex_end();

    /* ---- the same, in words ---- */
    why_start();
    why_cat("gait window %" PRIu32 " - ", win);
    if (!used) {
        if (strcmp(why, "few-moving") == 0) {
            why_cat("only %.0f%% of packets moving (needs %.0f%%) -> %s", 100.0f * frac,
                    100.0f * GAIT_MIN_MOVING,
                    s_sess.kind == SESS_ENROLL ? "not added to the template" : "no vote");
        } else if (strcmp(why, "enrol") == 0) {
            why_cat("%.0f%% moving -> added to %s's template (%d/%d), not classified", 100.0f * frac,
                    slot_name(s_sess.cls), s_sess.got, s_sess.target);
        } else if (strcmp(why, "session") == 0) {
            why_cat("count training running -> not classified");
        } else if (strcmp(why, "multiple") == 0) {
            why_cat("%.0f%% moving, but count says 2+ people moving -> not classified (person = multiple)",
                    100.0f * frac);
        } else if (strcmp(why, "no-model") == 0) {
            why_cat("%.0f%% moving, but nobody is enrolled yet ('enroll <slot> <name>') -> no vote",
                    100.0f * frac);
        } else {
            why_cat("%.0f%% moving, but its features are not finite -> no vote", 100.0f * frac);
        }
        why_log();
        return;
    }
    why_cat("%.0f%% moving; nearest %s %.2f", 100.0f * frac, slot_name(res->nearest), res->dist);
    if (second >= 0) {
        why_cat(", next %s %.2f", slot_name(second), dist[second]);
    } else {
        why_cat(", no other enrolled person");
    }
    if (res->best >= 0) {
        if (second >= 0 && margin > 1.0f && res->dist > 0.0f) {
            why_cat(" (reject > %.2f, margin needs %.2f, got %.2f) -> vote %s", reject, margin,
                    dist[second] / res->dist, vote);
        } else {
            why_cat(" (reject > %.2f) -> vote %s", reject, vote);
        }
    } else if (!(res->dist <= reject)) {
        why_cat(" (%.2f > reject %.2f: nobody enrolled walks like this) -> vote unknown",
                res->dist, reject);
    } else {
        why_cat(" (margin needs %.2f, got %.2f: too close to call) -> vote unknown", margin,
                (second >= 0 && res->dist > 0.0f) ? dist[second] / res->dist : 0.0f);
    }
    if (label >= 0) {
        why_cat("; votes %s %d of %d -> person %s", lname, k, s_vote_n, lname);
    } else if (lname) {
        /* id_tally: a name needs >= ID_MIN_VOTES, more than "unknown", no tie */
        why_cat("; no name leads the last %d votes clearly (%d unknown; a name needs %d, more "
                "than unknown, no tie) -> person unknown", s_vote_n, k, ID_MIN_VOTES);
    } else {
        why_cat("; %d vote%s so far (a name needs %d) -> person not decided yet",
                s_vote_n, s_vote_n == 1 ? "" : "s", ID_MIN_VOTES);
    }
    why_log();
}
#endif

#if CONFIG_CSI_COUNT_ENABLE
/* ---- "count": one completed count block ----
 * why == NULL: the block voted (called after the vote, so ring and est
 * include it); otherwise why says why not. */
static void explain_count(const char *why, const float *feat)
{
    static uint32_t blk = 0;
    blk++;

    float duty  = feat[CNT_FEAT_DUTY];
    bool  used  = (why == NULL);
    bool  ready = count_model_ready();
    float dist[NCC_MAX_CLASSES];                  /* ncc_distances fills all four */
    int   nd    = ncc_distances(&s_cnt_model, feat, (uint32_t)CONFIG_CSI_COUNT_TRAIN_WINDOWS, dist);
    if (!ready) nd = 0;                           /* one class alone decides nothing */
    int   vote  = used ? (int)s_cnt_vote[(s_cnt_vote_head + CNT_VOTES - 1) % CNT_VOTES] + 1 : 0;

    char ring[CNT_VOTES + 1];
    int  oldest = (s_cnt_vote_n < CNT_VOTES) ? 0 : s_cnt_vote_head;
    int  twos   = 0;
    for (int i = 0; i < s_cnt_vote_n; i++) {
        int v = s_cnt_vote[(oldest + i) % CNT_VOTES];
        ring[i] = v ? '2' : '1';
        twos += v;
    }
    ring[s_cnt_vote_n] = '\0';

    ex_begin("count");
    json_adv(snprintf(json_end(), json_room(), ",\"blk\":%" PRIu32, blk));
    ex_num("duty", true, duty, 2);
    ex_bool("used", used);
    json_str("why", why);
    json_adv(snprintf(json_end(), json_room(), ",\"feat\":["));
    for (int f = 0; f < COUNT_NFEAT; f++) {
        ex_val(f ? "," : "", true, feat[f], 3);
    }
    json_adv(snprintf(json_end(), json_room(), "]"));
    if (nd > 0) {
        ex_val(",\"dist\":[", dist[0] >= 0.0f, dist[0] < EX_DIST_MAX ? dist[0] : EX_DIST_MAX, 2);
        ex_val(",", dist[1] >= 0.0f, dist[1] < EX_DIST_MAX ? dist[1] : EX_DIST_MAX, 2);
        json_adv(snprintf(json_end(), json_room(), "]"));
    } else {
        json_str("dist", NULL);
    }
    if (used) {
        json_adv(snprintf(json_end(), json_room(), ",\"vote\":%d", vote));
    } else {
        json_str("vote", NULL);
    }
    json_str("ring", ring);
    json_adv(snprintf(json_end(), json_room(), ",\"est\":%d", s_cnt_est));
    ex_end();

    /* ---- the same, in words ---- */
    const char *now = (s_cnt_est >= 2) ? "2+" : (s_cnt_est == 1 ? "1" : "1 (= presence)");
    why_start();
    why_cat("count block %" PRIu32 " - ", blk);
    if (used) {
        why_cat("movement in %.0f%% of packets; one-mover %.2f, two+ %.2f -> vote %s; "
                "2+ in %d of the last %d (needs a majority) -> count %s",
                100.0f * duty, dist[0], dist[1], vote == 2 ? "2+" : "1", twos, s_cnt_vote_n,
                s_cnt_est >= 2 ? "2+" : "1");
    } else if (strcmp(why, "session") == 0 && s_sess.kind == SESS_COUNT) {
        why_cat("count-train:%d: block %d/%d (%.0f%% moving) -> training data, not classified",
                s_sess.cls + 1, s_sess.got, s_sess.target, 100.0f * duty);
    } else if (strcmp(why, "session") == 0) {
        why_cat("enrolment running -> not classified");
    } else if (strcmp(why, "low-duty") == 0) {
        why_cat("movement in only %.0f%% of packets (needs %.0f%%) -> %s", 100.0f * duty,
                100.0f * COUNT_MIN_DUTY,
                s_sess.kind == SESS_COUNT ? "block skipped, not training data" : "no vote");
        if (s_sess.kind != SESS_COUNT) why_cat("; count stays %s", now);
    } else if (strcmp(why, "no-model") == 0) {
        why_cat("no count model yet (one %" PRIu32 "/%d, two+ %" PRIu32 "/%d blocks: 'count-train 1', "
                "then 'count-train 2') -> count follows presence (1)",
                s_cnt_model.cls[0].n, CONFIG_CSI_COUNT_TRAIN_WINDOWS,
                s_cnt_model.cls[1].n, CONFIG_CSI_COUNT_TRAIN_WINDOWS);
    } else {
        why_cat("its features are not finite -> no vote; count stays %s", now);
    }
    why_log();
}
#endif

#if CONFIG_CSI_VITALS_ENABLE
/* ---- "vitals": one completed analysis, as the JSON now reports it ---- */
static void explain_vitals(bool present)
{
    const vitals_out_t *v = vitals_get(&s_vit);
    report_t            rep;
    report_build(&rep, present);
    bool multi = vitals_multi(rep.count);

    ex_begin("vitals");
    json_str("state", rep.vitals);
    ex_num("br", rep.br_ok, rep.br_bpm, 1);
    ex_num("br_conf", rep.brc_ok, rep.br_conf, 1);
    ex_num("hr_exp", rep.hr_ok, rep.hr_bpm, 1);
    ex_num("hr_conf", rep.hrc_ok, rep.hr_conf, 1);   /* only WITH a heart rate (D-131) */
    ex_num("still_s", true, v->still_s, 1);
    json_str("vitals_of", rep.vitals_of);
    ex_bool("multiple", multi);
    json_activity(&rep);                             /* appended later: last */
    ex_end();

    /* ---- the same, in words ---- */
    float brmin = s_vit.cfg.br_conf_min, hrmin = s_vit.cfg.hr_conf_min;
    why_start();
    why_cat("vitals - ");
    if (strcmp(rep.vitals, "no-person") == 0) {
        why_cat("analysed %.0f s of still data, but nobody is PRESENT -> nothing reported", v->still_s);
    } else if (strcmp(rep.vitals, "multiple") == 0) {
        why_cat("2+ people this visit (counted moving together, two names, or count-train 2) -> "
                "rates withheld: one rate would mix them");
    } else if (v->br_valid) {
        why_cat("breathing %.1f/min (conf %.1f >= %.1f)", v->br_bpm, v->br_conf, brmin);
        if (v->hr_valid) {
            why_cat(", heart ~%.0f/min EXPERIMENTAL (conf %.1f >= %.1f)", v->hr_bpm, v->hr_conf, hrmin);
        } else if (v->hr_conf < hrmin) {
            why_cat(", heart: none (conf %.1f < %.1f)", v->hr_conf, hrmin);
        } else {
            /* A strong number here is NOT a heartbeat that went unreported:
             * vitals.c withholds a peak on the band edge or on the shoulder
             * of a notched breathing harmonic (D-131), so no number. */
            why_cat(", heart: none (its peak sits on a band edge or a breathing harmonic)");
        }
        why_cat(" over %.0f s of still data", v->still_s);
    } else if (v->br_conf < brmin) {
        why_cat("no clear breathing peak (conf %.1f < %.1f) in %.0f s of still data -> no rate",
                v->br_conf, brmin, v->still_s);
    } else {
        why_cat("the breathing peak is on the band edge (outside ~7.6-28.7/min) -> no rate");
    }
#if CSI_HAVE_WHO
    if (rep.br_ok && rep.vitals_of) {
        why_cat(", of %s (the last person recognised walking in alone)", rep.vitals_of);
    } else if (rep.br_ok) {
        why_cat(", of nobody named");
    }
#endif
    why_log();
}
#endif

/* ---- "presence": every PRESENT / EMPTY change ----
 * have_md: m and d (JSON motion_sd, dev_sd) are this packet's; bpm is the
 * breathing rate behind a "breathing-hold". */
static void explain_presence(bool present, const char *reason, bool have_md, float m, float d,
                             float bpm)
{
    ex_begin("presence");
    json_adv(snprintf(json_end(), json_room(), ",\"present\":%d", present ? 1 : 0));
    json_str("reason", reason);
    ex_num("m", have_md, m, 2);
    ex_num("d", have_md, d, 2);
    ex_end();

    char ms[16], ds[16];
    fmt_num(ms, sizeof(ms), have_md, 1, m);
    fmt_num(ds, sizeof(ds), have_md, 1, d);
    if (strcmp(reason, "motion") == 0) {
        ESP_LOGI(TAG, "why: PRESENT - motion %s sd >= threshold %.1f (deviation %s sd)",
                 ms, THR_MOTION, ds);
    } else if (strcmp(reason, "static") == 0) {
        ESP_LOGI(TAG, "why: PRESENT - deviation %s sd >= %.1f for %d ms (motion only %s sd): "
                      "something in the room changed and stayed - a still person, or moved furniture",
                 ds, THR_STATIC, CONFIG_CSI_STATIC_SUSTAIN_MS, ms);
    } else if (strcmp(reason, "breathing-hold") == 0) {
        ESP_LOGI(TAG, "why: PRESENT - a still person breathing %.1f/min: breathing may hold a still "
                      "spell that began while PRESENT (motion %s sd)", bpm, ms);
    } else if (strcmp(reason, "hold-expired") == 0) {
        ESP_LOGI(TAG, "why: EMPTY - nothing triggered for %d ms (motion %s sd, released below %.1f; "
                      "deviation %s sd, needs %.1f for %d ms)",
                 CONFIG_CSI_PRESENCE_HOLD_MS, ms, THR_RELEASE, ds, THR_STATIC,
                 CONFIG_CSI_STATIC_SUSTAIN_MS);
    } else if (strcmp(reason, "no-csi") == 0) {
        ESP_LOGI(TAG, "why: EMPTY - no usable CSI for %d ms: presence unknown, reported as clear",
                 CONFIG_CSI_NO_CSI_TIMEOUT_MS);
    } else {
        ESP_LOGI(TAG, "why: EMPTY - recalibration started (leave the room)");
    }
}
#endif /* CSI_EXPLAIN */

/* ================================================================== */
/*  Step 10 console                                                    */
/*                                                                     */
/*  A low-priority task reads lines from USB-Serial-JTAG, parses them  */
/*  into a cmd_t and posts it to s_cmd_q. The pipeline task drains     */
/*  that queue once per loop iteration - after every packet and on    */
/*  every 100 ms receive timeout - and executes the commands itself.   */
/*  So every change to models, sessions and state happens in ONE task. */
/* ================================================================== */
#if CSI_HAVE_CONSOLE
#define CON_LINE_MAX    64
#define CON_QUEUE_LEN   4

typedef enum {
    CMD_STATUS = 0,
    CMD_PEOPLE,
    CMD_ENROLL,
    CMD_FORGET,
    CMD_COUNT_TRAIN,
    CMD_COUNT_RESET,
    CMD_STOP,
    CMD_RECAL,
    CMD_VITALS_RESET,
    CMD_EXPLAIN,
    CMD_STATS,
} cmd_verb_t;

typedef struct {
    cmd_verb_t verb;
    int        arg;                  /* slot 0-3 (-1 = all), or count-train 1 / 2 */
    char       name[NCC_NAME_LEN];   /* enroll: passed name_valid()               */
} cmd_t;

static QueueHandle_t s_cmd_q = NULL;

static char ascii_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* Case-insensitive match of a typed word against a lower-case literal. */
static bool word_is(const char *w, const char *lit)
{
    for (; *w != '\0' && *lit != '\0'; w++, lit++) {
        if (ascii_lower(*w) != *lit) return false;
    }
    return *w == '\0' && *lit == '\0';
}

/* ---- executed in the PIPELINE task ---- */

#if CSI_HAVE_MODELS
/* Sessions need a calibrated, detecting board, and only one runs at a time. */
static bool session_can_start(const char *what, phase_t phase)
{
    if (phase != PH_DETECT) {
        printf("CMD %s: not detecting yet (phase %s) - wait for calibration to finish\n",
               what, phase_name(phase));
        return false;
    }
    if (s_sess.kind != SESS_NONE) {
        char d[64];
        session_describe(d, sizeof(d));
        printf("CMD %s: session %s is running - 'stop' it first\n", what, d);
        return false;
    }
    return true;
}
#endif

#if CONFIG_CSI_ID_ENABLE
static bool names_equal(const char *a, const char *b)
{
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        if (ascii_lower(*a) != ascii_lower(*b)) return false;
    }
    return *a == '\0' && *b == '\0';
}
#endif

static void cmd_status(const pipe_view_t *v)
{
    bool     detecting = (v->phase == PH_DETECT);
    report_t rep;
    /* During a CSI outage the watchdog has already made present false. */
    report_build(&rep, detecting && v->present);

    /* The same states the JSON reports: during a CSI outage presence is
     * UNKNOWN (JSON "no_csi"), not "empty". */
    char pres[64];
    if (!detecting) {
        snprintf(pres, sizeof(pres), "not detecting yet");
    } else if (v->no_csi) {
        snprintf(pres, sizeof(pres), "NO USABLE CSI for %.0f s - presence unknown",
                 (double)(esp_timer_get_time() - v->last_usable_us) / 1e6);
    } else {
        snprintf(pres, sizeof(pres), "%s", v->present ? "PRESENT" : "empty");
    }
    /* live = usable packets in the last second; the other figure is the
     * rate the EMAs and gait frequencies assume - a calibration's, or the
     * ping interval's until one has been measured. */
    printf("CMD status: phase %s, %s, %.1f CSI packets/s live (%s %.1f/s), %u/%d subcarriers used, "
           "channel %u, free heap %" PRIu32 " bytes\n",
           phase_name(v->phase), pres, v->live_hz,
           v->rate_measured ? "calibrated" : "not measured yet, nominal", v->rate_hz,
           s_base.n_selected, s_locked_nsub, s_locked_ch, esp_get_free_heap_size());

#if CONFIG_CSI_COUNT_ENABLE
    printf("    count %d (%s); count model: one %" PRIu32 "/%d, two+ %" PRIu32 "/%d blocks - %s\n",
           rep.count, rep.count_src,
           s_cnt_model.cls[0].n, CONFIG_CSI_COUNT_TRAIN_WINDOWS,
           s_cnt_model.cls[1].n, CONFIG_CSI_COUNT_TRAIN_WINDOWS,
           count_model_ready() ? "trained" : "untrained, count = presence");
#else
    printf("    count %d (%s); counting disabled (CSI_COUNT_ENABLE)\n", rep.count, rep.count_src);
#endif

#if CONFIG_CSI_ID_ENABLE
    {
        char dist[48];
        fmt_num(dist, sizeof(dist), rep.person_dist >= 0.0f, 2, rep.person_dist);
        printf("    person %s (dist %s, %d/%d windows); %d of %d slots ready\n",
               rep.person ? rep.person : "-", dist, rep.votes_k, rep.votes_n,
               ncc_trained_classes(&s_id_model, (uint32_t)ID_MIN_WINDOWS), NCC_MAX_CLASSES);
    }
#else
    printf("    recognition disabled (CSI_ID_ENABLE)\n");
#endif

#if CONFIG_CSI_VITALS_ENABLE
    {
        char br[48], brc[48], hr[48], hrc[48];
        fmt_num(br, sizeof(br), rep.br_ok, 1, rep.br_bpm);
        fmt_num(brc, sizeof(brc), rep.brc_ok, 1, rep.br_conf);
        fmt_num(hr, sizeof(hr), rep.hr_ok, 1, rep.hr_bpm);
        fmt_num(hrc, sizeof(hrc), rep.hrc_ok, 1, rep.hr_conf);
        /* " (of raj)": whose the rates are claimed to be (JSON vitals_of). */
        char of[32] = "";
#if CSI_HAVE_WHO
        snprintf(of, sizeof(of), " (of %s)", rep.vitals_of ? rep.vitals_of : "-");
#endif
        printf("    vitals %s%s: breathing %s/min (conf %s), heart ~%s/min EXPERIMENTAL "
               "(conf %s), %.0f s still data\n",
               rep.vitals, of, br, brc, hr, hrc, vitals_get(&s_vit)->still_s);
        if (strcmp(rep.vitals, "multiple") == 0) {
            printf("    vitals withheld: 2+ people this visit - one rate would mix them\n");
        }
    }
#else
    printf("    vitals disabled (CSI_VITALS_ENABLE)\n");
#endif

#if CSI_HAVE_GATE
    {
        char hf[48];
        fmt_num(hf, sizeof(hf), rep.hf >= 0.0f, 2, rep.hf);
        printf("    motion gate %s: hf %s (moving at >= %.2f), floor from %s\n",
               mgate_still(&s_gate) ? "STILL" : "MOVING", hf, CSI_GATE_RATIO,
               s_gate.have_empty ? "the empty room" :
               (s_gate.slot_count >= MGATE_MIN_VALID ? "the quietest recent second (room not seen empty)"
                                                     : "nothing yet - not known"));
    }
#endif

#if CSI_HAVE_ACTIVITY
    if (rep.episode_ok) {
        printf("    activity %s for %" PRIu32 " s; presence episode %" PRIu32 " s ('stats')\n",
               rep.activity ? rep.activity : "-", rep.activity_s, rep.episode_s);
    } else {
        printf("    activity %s for %" PRIu32 " s; no presence episode ('stats')\n",
               rep.activity ? rep.activity : "-", rep.activity_s);
    }
#endif

    printf("    session %s\n", rep.session[0] ? rep.session : "none");
    /* One plain read of s_dropped: see the once-a-minute check in
     * pipeline_task for why that is safe without a lock. */
    uint32_t dropped = s_dropped;
    printf("    pipeline: slowest packet %.1f ms since boot, %" PRIu32 " CSI packets dropped "
           "since boot (queue of %d)\n",
           (float)s_proc_us_max_all / 1000.0f, dropped, CSI_QUEUE_LEN);
    /* After an unexplained silence these two say whether the board
     * restarted (and why), or only its output was lost (console_tx_check). */
    printf("    board: up %" PRIu32 " s, last reset %s (%d); console: USB host %s, transmit buffer "
           "found full at %" PRIu32 " of the 1-s checks (longest run %" PRIu32 " s)\n",
           (uint32_t)(esp_timer_get_time() / 1000000), reset_reason_name(s_reset_reason),
           (int)s_reset_reason, usb_serial_jtag_is_connected() ? "connected" : "not seen",
           s_con_full_s, s_con_full_max);
}

static void cmd_people(const pipe_view_t *v)
{
#if CONFIG_CSI_ID_ENABLE
    printf("CMD people: %d of %d slots ready (a slot needs %d gait windows, enrolment collects %d)\n",
           ncc_trained_classes(&s_id_model, (uint32_t)ID_MIN_WINDOWS), NCC_MAX_CLASSES,
           ID_MIN_WINDOWS, CONFIG_CSI_ID_ENROLL_WINDOWS);
#if CSI_HAVE_WHO
    int64_t now_us = esp_timer_get_time();   /* the clock last_seen_us is on */
#endif
    for (int s = 0; s < NCC_MAX_CLASSES; s++) {
        const ncc_class_t *k = &s_id_model.cls[s];
        if (k->name[0] == '\0' && k->n == 0) {
            printf("    slot %d: empty\n", s);
            continue;
        }
        const char *st;
        if (s_sess.kind == SESS_ENROLL && s_sess.cls == s) st = "collecting";
        else if (k->n >= (uint32_t)ID_MIN_WINDOWS)         st = "ready";
        else                                               st = "incomplete";
        printf("    slot %d: %s, %" PRIu32 " windows, %s\n",
               s, k->name[0] ? k->name : "-", k->n, st);
#if CSI_HAVE_WHO
        /* Indented, so presence_monitor forwards it with the rest. */
        char rec[256];
        pv_format_rec(rec, sizeof(rec), &s_pv, s, now_us);
        printf("        %s\n", rec);
#endif
    }
    if (s_id_model.csi_len != 0) {
        printf("    enrolled on %u-byte CSI, channel %u\n", s_id_model.csi_len, s_id_model.channel);
    }
#if CSI_HAVE_WHO
    /* What the JSON's vitals / vitals_of say at this moment. */
    report_t rep;
    report_build(&rep, v->phase == PH_DETECT && v->present);
    printf("    vitals now: %s\n",
           strcmp(rep.vitals, "multiple") == 0 ? "multiple (2+ this visit)" :
           (rep.vitals_of ? rep.vitals_of : "nobody named"));
#else
    (void)v;
#endif
#else
    (void)v;
    printf("CMD people: recognition is disabled (CSI_ID_ENABLE)\n");
#endif
}

static void cmd_enroll(const cmd_t *c, phase_t phase)
{
#if CONFIG_CSI_ID_ENABLE
    int slot = c->arg;
    if (!session_can_start("enroll", phase)) return;
    /* Two templates for one person would split the votes and trip the
     * margin test against each other: refuse a duplicate name. */
    for (int s = 0; s < NCC_MAX_CLASSES; s++) {
        if (s != slot && s_id_model.cls[s].name[0] != '\0' &&
            names_equal(s_id_model.cls[s].name, c->name)) {
            printf("CMD enroll: '%s' is already in slot %d - 'forget %d' first, or enrol into slot %d\n",
                   c->name, s, s, s);
            return;
        }
    }
    char old[NCC_NAME_LEN];
    memcpy(old, s_id_model.cls[slot].name, sizeof(old));
    bool replacing = (s_id_model.cls[slot].n > 0);

    ncc_clear_class(&s_id_model, slot);
    ncc_set_name(&s_id_model, slot, c->name);
#if CSI_HAVE_WHO
    /* The slot now belongs to this enrolment: the previous owner's vitals
     * log must not become the new person's (it is cleared - also when the
     * same person re-enrols), and nobody is attributed during the session.
     * Saved NOW, like 'forget': at boot a stored record is kept whenever
     * its name matches its slot's template, so if the new template (saved
     * when the enrolment completes, ~40-130 s from now) reached flash
     * before the cleared log did, a reset in between would hand the old
     * owner's log to a new person who re-used the name. */
    pv_slot_gone(&s_pv, slot);
    pv_release(&s_pv);
    esp_err_t verr = vit_log_save();
#endif
    gait_reset(&s_gait);                  /* first window from this session only */
    /* Nothing is classified while a session runs, so no earlier estimate
     * may linger in the outputs either. */
    id_votes_clear();
#if CONFIG_CSI_COUNT_ENABLE
    cnt_votes_clear();
#endif
    s_sess.kind   = SESS_ENROLL;
    s_sess.cls    = slot;
    s_sess.got    = 0;
    s_sess.target = CONFIG_CSI_ID_ENROLL_WINDOWS;

    /* "Different routes": csi_ncc measures distance in units of the spread
     * seen DURING enrolment. One fixed route gives a template so narrow
     * that later walks of the same person read as "unknown" - in the host
     * e2e test (test/test_e2e.c), 79% of them. */
    printf("CMD enroll %d %s: started%s%s - %s walks around the room ALONE, at a normal pace, "
           "by several different routes, until 'enrol %s: done' (%d windows)\n",
           slot, c->name, replacing ? ", replacing " : "", replacing ? old : "",
           c->name, c->name, s_sess.target);
#if CSI_HAVE_WHO
    if (verr != ESP_OK) {
        /* Indented, so presence_monitor forwards it with the answer. */
        printf("    vitals log NVS %s: slot %d's old log is cleared in RAM only - "
               "retried with the next periodic save\n", esp_err_to_name(verr), slot);
    }
#endif
#if CSI_EXPLAIN
    explain_config();                     /* the slot is named, with 0 windows */
#endif
#else
    (void)c;
    (void)phase;
    printf("CMD enroll: recognition is disabled (CSI_ID_ENABLE)\n");
#endif
}

static void cmd_forget(const cmd_t *c)
{
#if CONFIG_CSI_ID_ENABLE
    if (s_sess.kind == SESS_ENROLL) {
        printf("CMD forget: an enrolment is running - 'stop' it first\n");
        return;
    }
    int  slot = c->arg;                   /* -1 = all */
    char old[NCC_NAME_LEN] = "";
    if (slot < 0) {
        ncc_init(&s_id_model, GAIT_NFEAT);
    } else {
        memcpy(old, s_id_model.cls[slot].name, sizeof(old));
        ncc_clear_class(&s_id_model, slot);
    }
    id_votes_clear();
    esp_err_t err = model_save(ID_NVS_KEY, &s_id_model);
#if CSI_HAVE_WHO
    /* Their vitals log goes with them (a pending episode is discarded,
     * never committed), and the latch lets go if it named them. Saved NOW
     * rather than at the next periodic save: 'forget' is a privacy
     * request. (NVS is not encrypted by default, so this is a delete, not
     * a secure wipe.) */
    pv_slot_gone(&s_pv, slot);
    esp_err_t   verr = vit_log_save();
    const char *vsep = "; vitals log NVS ";
    const char *vres = esp_err_to_name(verr);
#else
    const char *vsep = "";
    const char *vres = "";
#endif
    if (slot < 0) {
        printf("CMD forget all: every template deleted, NVS %s%s%s\n",
               esp_err_to_name(err), vsep, vres);
    } else {
        printf("CMD forget %d: slot %d (%s) deleted, NVS %s%s%s\n",
               slot, slot, old[0] ? old : "empty", esp_err_to_name(err), vsep, vres);
    }
#if CSI_EXPLAIN
    explain_config();
#endif
#else
    (void)c;
    printf("CMD forget: recognition is disabled (CSI_ID_ENABLE)\n");
#endif
}

/* 'vitals-reset <slot> | all': a person's vitals log only - their
 * template, and the latch, are kept (pv_log_clear). Saved at once, like
 * 'forget'. Allowed during a session: it touches no template. */
static void cmd_vitals_reset(const cmd_t *c)
{
#if CSI_HAVE_WHO
    int slot = c->arg;                    /* -1 = all */
    pv_log_clear(&s_pv, slot);
    esp_err_t err = vit_log_save();
    if (slot < 0) {
        printf("CMD vitals-reset all: vitals log cleared, NVS %s\n", esp_err_to_name(err));
    } else {
        const char *name = s_id_model.cls[slot].name;
        printf("CMD vitals-reset %d: vitals log of slot %d (%s) cleared, NVS %s\n",
               slot, slot, name[0] ? name : "empty", esp_err_to_name(err));
    }
#else
    (void)c;
    printf("CMD vitals-reset: needs CSI_ID_ENABLE and CSI_VITALS_ENABLE\n");
#endif
}

static void cmd_count_train(const cmd_t *c, phase_t phase)
{
#if CONFIG_CSI_COUNT_ENABLE
    if (!session_can_start("count-train", phase)) return;
    int  cls       = c->arg - 1;          /* 1 -> class 0 "one", 2 -> class 1 "two+" */
    bool replacing = (s_cnt_model.cls[cls].n > 0);

    ncc_clear_class(&s_cnt_model, cls);
    ncc_set_name(&s_cnt_model, cls, cls == 0 ? "one" : "two-plus");
    count_reset(&s_cnt);                  /* first block from this session only */
    cnt_votes_clear();
#if CONFIG_CSI_ID_ENABLE
    id_votes_clear();                     /* see cmd_enroll */
#endif
#if CSI_HAVE_WHO
    /* Nobody's vitals are attributed during a session (class 2 is two or
     * more people by definition - report_build withholds its rates). */
    pv_release(&s_pv);
#endif
    s_sess.kind   = SESS_COUNT;
    s_sess.cls    = cls;
    s_sess.got    = 0;
    s_sess.target = CONFIG_CSI_COUNT_TRAIN_WINDOWS;

    printf("CMD count-train %d: started%s - %s keep moving around the room until "
           "'count-train:%d done' (%d blocks of >= %.0f%% movement)\n",
           c->arg, replacing ? ", replacing the previous training" : "",
           cls == 0 ? "ONE person should" : "TWO OR MORE people should",
           c->arg, s_sess.target, 100.0f * COUNT_MIN_DUTY);
#if CSI_EXPLAIN
    explain_config();                     /* the class being trained is empty now */
#endif
#else
    (void)c;
    (void)phase;
    printf("CMD count-train: counting is disabled (CSI_COUNT_ENABLE)\n");
#endif
}

static void cmd_count_reset(void)
{
#if CONFIG_CSI_COUNT_ENABLE
    if (s_sess.kind == SESS_COUNT) {
        printf("CMD count-reset: count training is running - 'stop' it first\n");
        return;
    }
    ncc_init(&s_cnt_model, COUNT_NFEAT);
    cnt_votes_clear();
    esp_err_t err = model_save(CNT_NVS_KEY, &s_cnt_model);
    printf("CMD count-reset: count model deleted, NVS %s - 'count' is presence only until retrained\n",
           esp_err_to_name(err));
#if CSI_EXPLAIN
    explain_config();
#endif
#else
    printf("CMD count-reset: counting is disabled (CSI_COUNT_ENABLE)\n");
#endif
}

static void cmd_stop(void)
{
#if CSI_HAVE_MODELS
    if (s_sess.kind == SESS_NONE) {
        printf("CMD stop: no session running\n");
        return;
    }
    char what[64];
    session_describe(what, sizeof(what));
    esp_err_t err = session_cancel();
    printf("CMD stop: %s cancelled, its data discarded (NVS %s)\n", what, esp_err_to_name(err));
#if CSI_EXPLAIN
    explain_config();                     /* the cancelled class is empty again */
#endif
#else
    printf("CMD stop: no session running\n");
#endif
}

/* 'explain': how it decides, in words, with the settings in force - the
 * same numbers as the EXPLAIN config line, which follows when CSI_EXPLAIN
 * is on. Continuation lines are indented (presence_monitor forwards them). */
static void cmd_explain(const pipe_view_t *v)
{
    printf("CMD explain: how this board decides, with its live settings%s\n",
           CSI_EXPLAIN ? " (an EXPLAIN config line follows)" : "");
    if (v->phase != PH_DETECT) {
        printf("    not detecting yet (phase %s): the calibration numbers apply once it is\n",
               phase_name(v->phase));
    }
    printf("    PRESENT: motion >= %.2f sd (until it falls below %.2f), or deviation >= %.2f sd "
           "for %d ms; held %d ms after the last trigger\n",
           THR_MOTION, THR_RELEASE, THR_STATIC, CONFIG_CSI_STATIC_SUSTAIN_MS,
           CONFIG_CSI_PRESENCE_HOLD_MS);
    printf("    sd = standard deviations of the empty room: motion %.3f +/- %.3f, deviation "
           "%.3f +/- %.3f; %u of %d subcarriers, %.1f packets/s (%s)\n",
           s_base.m_mean, s_base.m_std, s_base.d_mean, s_base.d_std, s_base.n_selected,
           s_locked_nsub, v->rate_hz, v->rate_measured ? "calibrated" : "nominal");
#if CSI_HAVE_GATE
    printf("    moving (for who / how many / vitals): Step 08's motion flag AND the motion gate, "
           "hf >= %.2f (energy above ~1.6 Hz over the empty room's)\n", CSI_GATE_RATIO);
#endif
#if CONFIG_CSI_ID_ENABLE
    printf("    WHO: a %d-packet gait window every %d packets, used if >= %.0f%% moving; it votes for "
           "the nearest enrolled person\n", GAIT_NWIN, GAIT_HOP, 100.0f * GAIT_MIN_MOVING);
    printf("         if within %.2f and the runner-up is >= %.2fx farther, else 'unknown'; a name "
           "needs %d of the last %d votes; %d of %d slots ready\n",
           (float)CONFIG_CSI_ID_REJECT_X100 / 100.0f, (float)CONFIG_CSI_ID_MARGIN_X100 / 100.0f,
           ID_MIN_VOTES, ID_VOTES, ncc_trained_classes(&s_id_model, (uint32_t)ID_MIN_WINDOWS),
           NCC_MAX_CLASSES);
#else
    printf("    WHO: recognition disabled (CSI_ID_ENABLE)\n");
#endif
#if CONFIG_CSI_COUNT_ENABLE
    printf("    HOW MANY: a %d-packet block, used if >= %.0f%% moving, votes one or 2+ (the nearer "
           "trained class); 2+ needs a majority of the last %d\n",
           COUNT_NWIN, 100.0f * COUNT_MIN_DUTY, CNT_VOTES);
    printf("         model: one %" PRIu32 "/%d, two+ %" PRIu32 "/%d blocks - %s\n",
           s_cnt_model.cls[0].n, CONFIG_CSI_COUNT_TRAIN_WINDOWS,
           s_cnt_model.cls[1].n, CONFIG_CSI_COUNT_TRAIN_WINDOWS,
           count_model_ready() ? "trained" : "untrained, count = presence (0 or 1)");
#else
    printf("    HOW MANY: counting disabled (CSI_COUNT_ENABLE)\n");
#endif
#if CONFIG_CSI_VITALS_ENABLE
#if CONFIG_CSI_VITALS_HOLDS_PRESENCE
    const char *holds = "yes";
#else
    const char *holds = "no";
#endif
    printf("    VITALS: while the gate says still; first estimate after %d s, then every 2 s over "
           "%d s; breathing at conf >= %.1f, heart (EXPERIMENTAL) at >= %.1f; "
           "breathing holds PRESENT: %s\n",
           s_vit.cfg.min_s, s_vit.cfg.window_s, s_vit.cfg.br_conf_min, s_vit.cfg.hr_conf_min,
           holds);
#else
    printf("    VITALS: disabled (CSI_VITALS_ENABLE)\n");
#endif
#if CSI_HAVE_ACTIVITY
    printf("    ACTIVITY (EXPERIMENTAL): per second, moving if the gate saw motion in >= 1/%d of it "
           "or mean hf >= %.2f in %d of the last %d s; restless if %d of the last %d s had mean hf "
           ">= %.2f (or motion, in the last %d s); still after %d quiet s; empty after %d s not "
           "PRESENT; held %d s\n",
           ACT_MOVE_FRAC_DIV, s_act.cfg.move_thr, ACT_MOVE_MIN_S, ACT_WIN_S,
           ACT_FIDGET_MIN_S, ACT_QUIET_S, s_act.cfg.restless_lo, ACT_WIN_S, ACT_QUIET_S, ACT_EMPTY_S,
           ACT_HOLD_S);
#else
    printf("    ACTIVITY: disabled (CSI_ACTIVITY_ENABLE)\n");
#endif
#if CSI_EXPLAIN
    explain_config();
#else
    printf("    (EXPLAIN lines and 'why:' lines are off: CSI_EXPLAIN)\n");
#endif
}

#if CSI_HAVE_ACTIVITY
/* "<total> s (still a, restless b, moving c, other d)" - other = seconds
 * labelled empty or nothing inside the episode (its first seconds, a
 * bridged lapse). */
static void stats_ep(char *buf, size_t len, const act_ep_t *e)
{
    uint32_t other = e->st_s[ACT_NONE] + e->st_s[ACT_EMPTY];
    snprintf(buf, len, "%" PRIu32 " s (still %" PRIu32 ", restless %" PRIu32 ", moving %" PRIu32
             ", other %" PRIu32 ")", e->total_s, e->st_s[ACT_STILL], e->st_s[ACT_RESTLESS],
             e->st_s[ACT_MOVING], other);
}
#endif

/* 'stats': the activity state and the presence episodes (activity.h).
 * Continuation lines are indented (presence_monitor forwards them). */
static void cmd_stats(void)
{
#if CSI_HAVE_ACTIVITY
    const char *st = act_state_name(act_state(&s_act));
    char        ep[112];
    printf("CMD stats: activity %s for %" PRIu32 " s (EXPERIMENTAL, not validated)\n",
           st ? st : "- (not enough data yet)", act_state_s(&s_act));
    stats_ep(ep, sizeof(ep), &s_act.cur);
    switch (s_act.ep_phase) {
    case ACT_EP_OPEN:
        printf("    this episode: %s\n", ep);
        break;
    case ACT_EP_LAPSE:
        printf("    this episode: %s - PRESENT lapsed; the breathing hold may still undo it\n", ep);
        break;
    case ACT_EP_WAIT:
        printf("    this episode: %s - PRESENT again, waiting: the breathing hold continues it, "
               "movement starts a new one\n", ep);
        break;
    default:
        printf("    no presence episode now\n");
        break;
    }
    if (s_act.n_episodes > 0) {
        stats_ep(ep, sizeof(ep), &s_act.last);
        printf("    last episode: %s; %" PRIu32 " ended since boot (RAM only, never saved)\n",
               ep, s_act.n_episodes);
    } else {
        printf("    no episode has ended since boot\n");
    }
#else
    printf("CMD stats: activity is disabled (CSI_ACTIVITY_ENABLE)\n");
#endif
}

/* Returns true when a recalibration was accepted. */
static bool cmd_execute(const cmd_t *c, const pipe_view_t *v)
{
    switch (c->verb) {
    case CMD_STATUS:      cmd_status(v);                       break;
    case CMD_PEOPLE:      cmd_people(v);                       break;
    case CMD_ENROLL:      cmd_enroll(c, v->phase);             break;
    case CMD_FORGET:      cmd_forget(c);                       break;
    case CMD_VITALS_RESET: cmd_vitals_reset(c);                break;
    case CMD_COUNT_TRAIN: cmd_count_train(c, v->phase);        break;
    case CMD_COUNT_RESET: cmd_count_reset();                   break;
    case CMD_STOP:        cmd_stop();                          break;
    case CMD_EXPLAIN:     cmd_explain(v);                      break;
    case CMD_STATS:       cmd_stats();                         break;
    case CMD_RECAL:
        /* Like the button, not during a CSI outage: the calibration would
         * sit in "leave-room" beside the no_csi lines, and after a CSI
         * shape change - which only the boot-time lock re-learns - it
         * could never finish (every packet fails the shape test). */
        if (v->no_csi) {
            printf("CMD recal: refused - no usable CSI (link down or CSI shape changed); "
                   "wait for the link to return, or reboot if the CSI shape changed\n");
            return false;
        }
        /* Like the button: only while detecting, so a half-finished
         * calibration is never interrupted by a stray command. */
        if (v->phase != PH_DETECT) {
            printf("CMD recal: ignored - not detecting yet (phase %s)\n", phase_name(v->phase));
            return false;
        }
        printf("CMD recal: recalibrating - LEAVE THE ROOM\n");
        return true;
    }
    return false;
}

/* ---- executed in the CONSOLE task: parsing only, no shared state ---- */

static void console_help(void)
{
    printf("CMD help: type a command, then Enter (typed characters are not echoed)\n"
           "    status                      one status block\n"
           "    people                      enrolment slots (name, windows, ready) + vitals logs\n"
           "    explain                     how it decides, with the live thresholds\n"
           "    stats                       activity state and presence episodes (seconds per state)\n"
           "    enroll <slot 0-%d> <name>    enrol a person: they walk around ALONE until 'done'\n"
           "                                (name: 1-%d characters of A-Z a-z 0-9 _ -)\n"
           "    forget <slot> | forget all  delete template(s) and vitals log(s), saved to NVS\n"
           "    vitals-reset <slot> | all   delete a person's vitals log (templates kept)\n"
           "    count-train 1               collect blocks with ONE person moving\n"
           "    count-train 2               collect blocks with TWO OR MORE people moving\n"
           "    count-reset                 delete the count model, saved to NVS\n"
           "    stop                        cancel an enrolment / training, discarding its data\n"
           "    recal                       recalibrate (same as the 2 s button hold) - LEAVE THE ROOM\n",
           NCC_MAX_CLASSES - 1, NCC_NAME_LEN - 1);
}

/* Split line in place at spaces / tabs. Returns the word count, or
 * max + 1 if there are more than max words. */
static int split_words(char *line, char **w, int max)
{
    int   n = 0;
    char *p = line;
    for (;;) {
        while (*p == ' ' || *p == '\t') {
            *p = '\0';
            p++;
        }
        if (*p == '\0') return n;
        if (n == max) return max + 1;
        w[n++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t') p++;
    }
}

/* Decimal digits only, lo <= value <= hi. */
static bool parse_int(const char *s, int lo, int hi, int *out)
{
    int v = 0;
    if (*s == '\0') return false;
    for (; *s != '\0'; s++) {
        if (*s < '0' || *s > '9') return false;
        v = v * 10 + (*s - '0');
        if (v > hi) return false;
    }
    if (v < lo) return false;
    *out = v;
    return true;
}

/* Parse one line. Errors are answered here; a valid command is posted
 * to the pipeline, which answers it. Typed text is never echoed back
 * (only validated names are), per the "no unvalidated strings" rule. */
static void console_handle_line(char *line)
{
    char *w[3];
    int   nw = split_words(line, w, 3);
    cmd_t c;

    if (nw == 0) return;                   /* empty line, e.g. the LF of CRLF */
    if (nw > 3) {
        printf("CMD error: too many words - type 'help'\n");
        return;
    }
    memset(&c, 0, sizeof(c));
    c.arg = -1;

    if (word_is(w[0], "help") || word_is(w[0], "?")) {
        console_help();
        return;
    } else if (word_is(w[0], "status") && nw == 1) {
        c.verb = CMD_STATUS;
    } else if (word_is(w[0], "people") && nw == 1) {
        c.verb = CMD_PEOPLE;
    } else if (word_is(w[0], "explain") && nw == 1) {
        c.verb = CMD_EXPLAIN;
    } else if (word_is(w[0], "stats") && nw == 1) {
        c.verb = CMD_STATS;
    } else if (word_is(w[0], "enroll") || word_is(w[0], "enrol")) {
        if (nw != 3 || !parse_int(w[1], 0, NCC_MAX_CLASSES - 1, &c.arg)) {
            printf("CMD error: usage: enroll <slot 0-%d> <name>\n", NCC_MAX_CLASSES - 1);
            return;
        }
        if (!name_valid(w[2])) {
            printf("CMD error: a name is 1-%d characters of A-Z a-z 0-9 _ -\n", NCC_NAME_LEN - 1);
            return;
        }
        /* These are JSON "person" values with a meaning of their own. */
        if (word_is(w[2], "unknown") || word_is(w[2], "multiple") || word_is(w[2], "null")) {
            printf("CMD error: that name is reserved - pick another\n");
            return;
        }
        int i = 0;
        for (; i < NCC_NAME_LEN - 1 && w[2][i] != '\0'; i++) {
            c.name[i] = w[2][i];
        }
        c.name[i] = '\0';
        c.verb = CMD_ENROLL;
    } else if (word_is(w[0], "forget")) {
        if (nw == 2 && word_is(w[1], "all")) {
            c.arg = -1;
        } else if (nw != 2 || !parse_int(w[1], 0, NCC_MAX_CLASSES - 1, &c.arg)) {
            printf("CMD error: usage: forget <slot 0-%d> | forget all\n", NCC_MAX_CLASSES - 1);
            return;
        }
        c.verb = CMD_FORGET;
    } else if (word_is(w[0], "vitals-reset")) {
        if (nw == 2 && word_is(w[1], "all")) {
            c.arg = -1;
        } else if (nw != 2 || !parse_int(w[1], 0, NCC_MAX_CLASSES - 1, &c.arg)) {
            printf("CMD error: usage: vitals-reset <slot 0-%d> | vitals-reset all\n",
                   NCC_MAX_CLASSES - 1);
            return;
        }
        c.verb = CMD_VITALS_RESET;
    } else if (word_is(w[0], "count-train")) {
        if (nw != 2 || !parse_int(w[1], 1, 2, &c.arg)) {
            printf("CMD error: usage: count-train 1 | count-train 2\n");
            return;
        }
        c.verb = CMD_COUNT_TRAIN;
    } else if (word_is(w[0], "count-reset") && nw == 1) {
        c.verb = CMD_COUNT_RESET;
    } else if (word_is(w[0], "stop") && nw == 1) {
        c.verb = CMD_STOP;
    } else if (word_is(w[0], "recal") && nw == 1) {
        c.verb = CMD_RECAL;
    } else {
        printf("CMD error: unknown command or wrong arguments - type 'help'\n");
        return;
    }

    if (xQueueSend(s_cmd_q, &c, 0) != pdTRUE) {
        printf("CMD busy: earlier commands have not run yet - try again\n");
    }
}

static void console_task(void *arg)
{
    char    line[CON_LINE_MAX];
    int     len      = 0;
    bool    too_long = false;
    uint8_t buf[32];

    for (;;) {
        int n = usb_serial_jtag_read_bytes(buf, sizeof(buf), portMAX_DELAY);
        for (int i = 0; i < n; i++) {
            uint8_t ch = buf[i];
            /* '\r' or '\n' ends a line, so CR, LF and CRLF terminals all
             * work (the LF of a CRLF is an empty line, ignored). */
            if (ch == '\r' || ch == '\n') {
                if (too_long) {
                    printf("CMD error: line longer than %d characters - ignored\n", CON_LINE_MAX - 1);
                } else if (len > 0) {
                    line[len] = '\0';
                    console_handle_line(line);
                }
                len      = 0;
                too_long = false;
            } else if (ch == 0x08 || ch == 0x7F) {         /* backspace / DEL */
                if (len > 0 && !too_long) len--;
            } else if (ch < 0x20 || ch > 0x7E) {
                /* other control bytes and non-ASCII: dropped */
            } else if (len < CON_LINE_MAX - 1) {
                line[len++] = (char)ch;
            } else {
                too_long = true;
            }
        }
    }
}
#endif /* CSI_HAVE_CONSOLE */

/* Install the USB-Serial-JTAG driver and move stdout onto it. Called at
 * the very start of app_main, before any other task of ours prints:
 * between the two calls below printf still writes the FIFO directly
 * while the driver's ISR also owns it. The console TASK starts later,
 * once the pipeline that executes the commands exists.
 *
 * CAN CONSOLE OUTPUT BLOCK THE PIPELINE? (checked 2026-09-25 against the
 * ESP-IDF v6.0.2 sources, C:\esp\v6.0.2\esp-idf\components\...; the
 * pipeline prints the JSON, bar, EXPLAIN and log lines itself.)
 *   printf -> picolibc stdout: ONE line-buffered FILE shared by every task
 *     (esp_libc/src/picolibc/picolibc_init.c:141-149) -> /dev/console,
 *     whose write returns `size` whatever the port did
 *     (esp_stdio/stdio_vfs.c:82-89), so stdout never enters an error
 *     state and dropped output is silent.
 *   -> usb_serial_jtag_write (esp_driver_usb_serial_jtag/src/
 *     usb_serial_jtag_vfs.c:182-206): returns -1 AT ONCE when
 *     usb_serial_jtag_is_connected() is false (:184-187) - no USB SOF for
 *     ~3 ticks (usb_serial_jtag_connection_monitor.c:26-27, 38-73: a
 *     charger, or a host gone). Otherwise, per character, the driver path
 *     usbjtag_tx_char_via_driver (vfs.c:709-725) tries the TX ring without
 *     waiting; if full, ONE wait of TX_FLUSH_TIMEOUT_US = 50 ms (vfs.c:77;
 *     50 ticks at our 1000 Hz); if that fails too it sets
 *     tx_tried_blocking and drops every later byte WITHOUT waiting until
 *     one gets through. usb_serial_jtag_write_bytes bounds both of its
 *     waits by that timeout (usb_serial_jtag.c:265-289).
 *   So a write can NOT block indefinitely: the worst is ~50 ms once per
 *   "ring just became full" episode (CSI_QUEUE_LEN rides out 320 ms), and
 *   0 ms with no host. The pipeline keeps running with no host.
 *   What CAN happen: the ring is emptied only by the ISR, on
 *   SERIAL_IN_EMPTY - i.e. when the host has PICKED UP the previous packet
 *   (usb_serial_jtag.c:67-141). A host that is attached (SOF) but not
 *   reading - port closed, or a monitor that did not really re-open after
 *   COM13 re-enumerated - leaves the ring full, and everything printed is
 *   dropped, 'status' answers included, while typed input still arrives
 *   (RX is pushed by the host). That matches the 2026-09-25 "accepted
 *   input, printed nothing" silence better than a blocked pipeline. Not
 *   settled from source: whether the USJ hardware can lose a pending IN
 *   packet on a bus reset so that SERIAL_IN_EMPTY never comes and the
 *   ring stays full for good - that needs a hardware repro.
 *   Changes made: the ring is 4 KB (below); the optional high-rate lines
 *   (bar, EXPLAIN amp) are skipped while it is full (console_tx_room);
 *   console_tx_check logs when output resumes after a stall and 'status'
 *   reports the stall history, the USB connection and the reset reason
 *   (a reset re-enumerates USB, so its banner is usually lost). */
static void console_install(void)
{
#if CSI_HAVE_CONSOLE
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    /* TX 4 KB instead of 256 B: printf goes through this ring from the
     * PIPELINE task, and a full ring blocks the writer for up to 50 ms
     * before dropping (see above) - a CSI stall. The worst burst on one
     * packet (a gait window, a count block and a vitals analysis, each
     * with its EXPLAIN and "why:" line, plus the JSON, bar and amp lines)
     * is ~2.5 KB; 2 KB was enough before the EXPLAIN lines. +2 KB of
     * heap. RX must exceed 64 bytes. */
    cfg.tx_buffer_size = 4096;
    cfg.rx_buffer_size = 256;
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "console: USB-Serial-JTAG driver install failed (%s) - no console",
                 esp_err_to_name(err));
        return;
    }
    usb_serial_jtag_vfs_use_driver();
    s_con_up = true;
    s_cmd_q = xQueueCreate(CON_QUEUE_LEN, sizeof(cmd_t));
    if (!s_cmd_q) {
        ESP_LOGE(TAG, "console: queue alloc failed - no console");
    }
#elif CONFIG_CSI_CONSOLE_ENABLE
    ESP_LOGW(TAG, "console: needs 'Channel for console output' = USB Serial/JTAG "
                  "(ESP-STDIO menu) - enrol / train commands unavailable");
#endif
}

#if CONFIG_CSI_HUMAN_READABLE || CSI_EXPLAIN
/* False while the console's transmit ring is completely full - nothing has
 * been read by a host for a while (see console_install). The optional
 * high-rate lines (the 4 Hz bar line, the EXPLAIN amp stream) are then
 * skipped rather than formatted and dropped byte by byte, and they cannot
 * each cost the one 50 ms wait of a host that drains at a trickle. The
 * JSON line and the event lines are always attempted. */
static bool console_tx_room(void)
{
#if CSI_HAVE_CONSOLE
    return !s_con_up || usb_serial_jtag_write_ready();
#else
    return true;                   /* ROM / no-driver console: nothing to ask */
#endif
}
#endif

#if CSI_HAVE_CONSOLE
/* Once a second, from the pipeline: is anything draining the transmit
 * ring? Full at two or more checks in a row = no host reading (port
 * closed, a monitor that did not re-open after a USB re-enumeration, or a
 * stalled driver - console_install). When output flows again, say how
 * long it was lost, so a silent spell can be told from a stopped board:
 * t_ms on the next JSON line shows the board kept running. */
static void console_tx_check(void)
{
    if (!s_con_up) return;
    if (!usb_serial_jtag_write_ready()) {
        s_con_full_s++;
        s_con_full_run++;
        if (s_con_full_run > s_con_full_max) s_con_full_max = s_con_full_run;
        return;
    }
    if (s_con_full_run >= 2) {
        ESP_LOGW(TAG, "console: output resumed - for ~%" PRIu32 " s no host read it (port closed, "
                      "or USB re-enumerated) and what was printed then was dropped; the detector "
                      "kept running (reset reason: %s)",
                 s_con_full_run, reset_reason_name(s_reset_reason));
    }
    s_con_full_run = 0;
}
#endif

/* Once per pipeline iteration. Returns true if 'recal' was accepted. */
static bool console_drain(const pipe_view_t *v)
{
#if CSI_HAVE_CONSOLE
    bool  recal = false;
    cmd_t c;
    if (!s_cmd_q) return false;
    while (xQueueReceive(s_cmd_q, &c, 0) == pdTRUE) {
        if (cmd_execute(&c, v)) recal = true;
    }
    return recal;
#else
    (void)v;
    return false;
#endif
}

/* ------------------------------------------------------------------ */
/*  Main pipeline task                                                 */
/* ------------------------------------------------------------------ */
static void pipeline_task(void *arg)
{
    csi_raw_t r;
    phase_t   phase = PH_LOCK;
    int64_t   phase_start = esp_timer_get_time();
    uint32_t  phase_packets = 0;

    float alpha_fast = 0.1f, alpha_slow = 0.001f;
    float rate_hz    = 1000.0f / CONFIG_CSI_PING_INTERVAL_MS;
    bool  rate_measured = false;        /* Step 10: rate_hz no longer nominal */
    bool  ema_init   = false;

    float live_m = 0.0f, live_d = 0.0f;
    bool  live_init = false;

    bool     motion_flag = false, present = false;
    int64_t  static_since = 0, last_trigger = 0;
    int64_t  present_since = 0;         /* start of the current PRESENT episode */
    bool     freeze_expired_logged = false;
    uint32_t accepted = 0, wrong_shape = 0;

    /* no-CSI watchdog: without it a dropped link or a CSI shape change
     * freezes the last decision - possibly PRESENT, LED on - forever. */
    int64_t  last_accepted = esp_timer_get_time();
    bool     no_csi = false, reseed_fast = false;
    /* channel_follow adopted a new Wi-Fi channel: recalibrate (D-168) */
    bool     chan_recal = false;

    int64_t next_json  = esp_timer_get_time() + 1000000;
    int64_t next_human = esp_timer_get_time() + 250000;
    int64_t next_log   = esp_timer_get_time() + 1000000;
    /* Only read when their output is compiled in; this keeps a build
     * with JSON or the bar line switched off warning-free. */
    (void)next_json;
    (void)next_human;
#if CSI_EXPLAIN
    /* the EXPLAIN amp stream; the divisor guard only keeps a disabled
     * stream (CSI_EXPLAIN_AMP_HZ = 0) from dividing by zero */
    int64_t next_amp = 0;
    const int64_t amp_period_us =
        1000000LL / (CONFIG_CSI_EXPLAIN_AMP_HZ > 0 ? CONFIG_CSI_EXPLAIN_AMP_HZ : 1);
#endif

    /* Step 10: dropped-packet check, once a minute */
    int64_t  next_drop_check = esp_timer_get_time() + 60000000;
    uint32_t drops_seen      = 0;

    /* Step 10: the LIVE packet rate. rate_hz is fixed at calibration and is
     * what the EMAs and gait frequencies assume; these follow what actually
     * arrives. "Usable" = accepted + s_observed: every packet the lock
     * observed (s_observed stops growing once the shape is locked), then
     * every packet of the locked shape. */
    float    live_hz     = 0.0f;        /* last second, for 'status'           */
    uint32_t live_n0     = 0;
    int64_t  live_t0     = esp_timer_get_time();
    uint32_t rchk_n0     = 0;           /* 10 s live-rate check in PH_DETECT   */
    int64_t  rchk_t0     = 0;           /* 0 = not running                     */
    bool     rchk_warned = false;       /* warned; re-armed once back in range */

    while (1) {
        /* Step 10: when this iteration's packet was dequeued (0 = no packet).
         * Declared before the first 'goto periodic', which reads it. */
        int64_t t_work0 = 0;

        /* ---- Step 10: console commands ----
         * Executed HERE, in this task, once per iteration (after every
         * packet and on every 100 ms receive timeout), so all model and
         * state mutation happens in one task. 'recal' comes back as a flag
         * and takes exactly the button's path below. */
        pipe_view_t view = {
            .phase = phase, .present = present, .no_csi = no_csi,
            .last_usable_us = last_accepted, .rate_hz = rate_hz,
            .rate_measured = rate_measured, .live_hz = live_hz,
        };
        bool recal_req = console_drain(&view);

        /* ---- recalibrate on a long button press (or 'recal') ----
         * phase and no_csi are tested FIRST so the hold timer only advances
         * while we are actually detecting. Otherwise a hold that crosses 2 s
         * during a calibration phase would latch 'fired' and be silently
         * swallowed, and no later press would produce another event until
         * release. Not during a CSI outage (Step 10): a recalibration then
         * waits in "leave-room" beside the no_csi lines, and after a CSI
         * shape change it could never finish - 'recal' refuses it too.
         * A Wi-Fi channel change (chan_recal, set by channel_follow's
         * caller below) takes the same path in any phase past LOCK; during
         * an outage it waits until CSI flows again. */
        bool chan_now = chan_recal && !no_csi;
        if (chan_now) chan_recal = false;
        if (!no_csi && (chan_now ||
                        (phase == PH_DETECT && (recal_req || button_held_2s())))) {
            ESP_LOGW(TAG, "%s - recalibrating. LEAVE THE ROOM.",
                     chan_now ? "Wi-Fi channel changed" :
                     recal_req ? "'recal' command" : "button held");
#if CSI_EXPLAIN
            if (present) explain_presence(false, "recal", false, 0.0f, 0.0f, 0.0f);
#endif
            present      = false;
            motion_flag  = false;
            static_since = 0;
            last_trigger = 0;
            led_set(false);
            analytics_on_recal();
            /* Re-seed the EMAs from the next packet. Carrying s_fast/s_slow
             * over from the old room state would make phase A's statistics
             * start from a warm-but-wrong reference and phase B measure
             * deviation against a pre-recalibration baseline. */
            ema_init  = false;
            live_init = false;
            phase = PH_LEAVE;
            phase_start = esp_timer_get_time();
            phase_packets = 0;
        }

        if (xQueueReceive(s_raw_q, &r, pdMS_TO_TICKS(100)) != pdTRUE) {
            goto periodic;
        }
        /* Step 10: the cost of a gait window + a count block + a vitals
         * FFT landing on one packet has never been measured on the chip.
         * Timed from here to 'periodic:' - everything this packet costs,
         * printing included; JSON proc_ms_max and 'status' report it. */
        t_work0 = esp_timer_get_time();
#if CSI_HAVE_GAIN
        /* every packet, in every phase: the JSON line reports the gain of
         * the packets since the previous line. Feeds nothing else. */
        if (r.gain_ok) csi_gain_push(&s_gain, r.agc, r.fft);
#endif

        /* ================= PH_LOCK ================= */
        if (phase == PH_LOCK) {
            shape_observe(&r);
            if (++s_observed >= CONFIG_CSI_LOCK_PACKETS) {
                float share = shape_lock(&r);
                ESP_LOGW(TAG, "CSI shape locked: %u bytes, %d subcarriers, fwi=%u, ch=%u (%.1f%%)",
                         s_locked_len, s_locked_nsub, s_locked_fwi, s_locked_ch, share);
                if (share < 90.0f) {
                    ESP_LOGE(TAG, "Only %.1f%% dominant - enable 'Capture L-LTF only'.", share);
                }

                /* Does the stored baseline still apply? A baseline pictures
                 * one channel's subcarriers, so one from another channel is
                 * stale even when the shape matches (D-168; channel 0 on
                 * either side = not known, accepted). A change after lock:
                 * channel_follow. */
                bool shape_ok = s_have_baseline &&
                                s_base.csi_len == s_locked_len &&
                                s_base.nsub    == (uint16_t)s_locked_nsub &&
                                s_base.fwi     == s_locked_fwi &&
                                s_base.n_selected >= 8 &&
                                s_base.m_std > 0.0f;
                bool same_ch  = s_base.channel == 0 || s_locked_ch == 0 ||
                                s_base.channel == s_locked_ch;
                bool usable   = shape_ok && same_ch;

#if CONFIG_CSI_FORCE_RECALIBRATE
                if (usable) {
                    ESP_LOGW(TAG, "CSI_FORCE_RECALIBRATE is set - ignoring stored baseline.");
                }
                usable = false;
#endif
                if (usable) {
                    rate_hz    = s_base.rate_hz;
                    rate_measured = true;
                    alpha_fast = 1.0f - expf(-1.0f / (rate_hz * TAU_FAST_S));
                    alpha_slow = 1.0f - expf(-1.0f / (rate_hz * TAU_SLOW_S));
                    ESP_LOGW(TAG, "baseline OK (%u subcarriers, %.1f Hz) - settling then detecting",
                             s_base.n_selected, rate_hz);
#if CSI_EXPLAIN
                    explain_cal("stored");          /* then "config", from analytics_configure */
#endif
                    analytics_configure(rate_hz);
                    phase = PH_SETTLE;
                } else {
                    if (!s_have_baseline) {
                        ESP_LOGW(TAG, "no stored baseline - calibrating");
                    } else if (shape_ok && !same_ch) {
                        ESP_LOGW(TAG, "stored baseline was calibrated on channel %u, now channel %u - "
                                      "recalibrating", s_base.channel, s_locked_ch);
                    } else if (shape_ok) {
                        /* Only reachable with CONFIG_CSI_FORCE_RECALIBRATE. */
                        ESP_LOGW(TAG, "recalibrating (CSI_FORCE_RECALIBRATE)");
                    } else {
                        ESP_LOGW(TAG, "stored baseline does not match this CSI shape - recalibrating");
                    }
                    phase = PH_LEAVE;
                }
                phase_start = esp_timer_get_time();
                phase_packets = 0;
            }
            goto periodic;
        }

        /* Every phase past LOCK requires the locked shape. */
        if (r.len != s_locked_len) {
            wrong_shape++;
            goto periodic;
        }
        accepted++;
        phase_packets++;
        last_accepted = esp_timer_get_time();
        if (no_csi) {
            no_csi      = false;
            reseed_fast = true;     /* s_fast is stale after the gap */
            live_init   = false;
            ESP_LOGW(TAG, "CSI flowing again - detecting");
        }
        /* Step 10: keep s_locked_ch the channel packets actually arrive on.
         * A new channel makes the baseline stale (D-168): a calibration in
         * progress starts again, and detection recalibrates, except with a
         * baseline whose stored channel is 0 (not known), which is kept.
         * (A nonzero stored channel equals the old s_locked_ch, unless the
         * channel at lock was 0; then adopting the baseline's own channel
         * keeps it.) Acted on at the
         * top of the next iteration, on the 'recal' path; PH_SAVE waits
         * for that. */
        if (channel_follow(r.channel)) {
            bool calibrating = phase != PH_SETTLE && phase != PH_DETECT;
            if (calibrating || (s_base.channel != 0 && s_base.channel != s_locked_ch)) {
                chan_recal = true;
            }
        }

        /* ---- amplitude + normalise ---- */
        {
            int off  = s_locked_fwi ? 4 : 0;
            int nsub = s_locked_nsub;
            for (int k = 0; k < nsub; k++) {
                s_amp[k] = csi_amp(r.buf, off, k);
            }
            float med = median_of(s_amp, nsub, s_scratch);
            if (med < 1.0f) med = 1.0f;
            for (int k = 0; k < nsub; k++) {
                s_na[k] = s_amp[k] / med;
            }

            /* Seeding: the fast EMA always starts from the first packet.
             * The slow EMA - the empty-room reference the static branch
             * compares against - starts from the STORED empty-room mean
             * when a baseline was just accepted (PH_SETTLE). Seeding it
             * from the first packet meant a boot with someone in the room
             * took that person as "empty", and even an empty boot left D
             * inflated for minutes while a one-packet seed decayed on a
             * 60 s time constant. A fresh calibration re-seeds it from the
             * new mu in PH_SELECT, before phase B measures d_mean/d_std. */
            if (!ema_init) {
                for (int k = 0; k < nsub; k++) {
                    s_fast[k] = s_na[k];
                    s_slow[k] = (phase == PH_SETTLE) ? s_base.mu[k] : s_na[k];
                }
                ema_init = true;
            } else if (reseed_fast) {
                for (int k = 0; k < nsub; k++) { s_fast[k] = s_na[k]; }
                reseed_fast = false;
            } else {
                /* Freeze the slow reference while PRESENT. If it keeps
                 * adapting, it absorbs a person within about a minute (a
                 * still person then reads "clear") and reports their
                 * DEPARTURE as a change as large as their arrival -
                 * PRESENT for 1-2 minutes in an empty room. The cap stops
                 * a genuine change made while someone was present (a moved
                 * chair) from holding PRESENT forever. Residual: a still
                 * person who never crosses a threshold is still absorbed,
                 * on the 60 s time constant. */
                int64_t now_us = esp_timer_get_time();
                bool freeze_slow = (phase == PH_DETECT) && present &&
                    (CONFIG_CSI_SLOW_FREEZE_MAX_S == 0 ||
                     (now_us - present_since) < (int64_t)CONFIG_CSI_SLOW_FREEZE_MAX_S * 1000000);
                if (phase == PH_DETECT && present && !freeze_slow && !freeze_expired_logged) {
                    freeze_expired_logged = true;
                    ESP_LOGW(TAG, "PRESENT for over %d s - letting the empty-room reference adapt again",
                             CONFIG_CSI_SLOW_FREEZE_MAX_S);
                }
                for (int k = 0; k < nsub; k++) {
                    s_fast[k] += alpha_fast * (s_na[k] - s_fast[k]);
                    if (!freeze_slow) {
                        s_slow[k] += alpha_slow * (s_na[k] - s_slow[k]);
                    }
                }
            }
        }

        float elapsed = (esp_timer_get_time() - phase_start) / 1e6f;
        int   nsub    = s_locked_nsub;

        switch (phase) {

        /* ================= PH_LEAVE ================= */
        case PH_LEAVE:
            if (elapsed >= CONFIG_CSI_CAL_LEAVE_ROOM_S) {
                phase = PH_WARMUP;
                phase_start = esp_timer_get_time();
                phase_packets = 0;
                ESP_LOGW(TAG, "calibration starting - stay out of the room");
            }
            break;

        /* ================= PH_WARMUP ================= */
        case PH_WARMUP:
            if (elapsed >= CONFIG_CSI_CAL_WARMUP_S) {
                rate_hz = phase_packets / elapsed;
                if (rate_hz < 1.0f) rate_hz = 1.0f;
                rate_measured = true;
                alpha_fast = 1.0f - expf(-1.0f / (rate_hz * TAU_FAST_S));
                alpha_slow = 1.0f - expf(-1.0f / (rate_hz * TAU_SLOW_S));
                ESP_LOGI(TAG, "measured %.1f CSI packets/s", rate_hz);

                memset(s_a_mean, 0, sizeof(s_a_mean));
                memset(s_a_m2, 0, sizeof(s_a_m2));
                s_a_n = 0;
                phase = PH_A;
                phase_start = esp_timer_get_time();
                phase_packets = 0;
            }
            break;

        /* ================= PH_A ================= */
        case PH_A:
            s_a_n++;
            for (int k = 0; k < nsub; k++) {
                double x  = s_na[k];
                double d1 = x - s_a_mean[k];
                s_a_mean[k] += d1 / s_a_n;
                s_a_m2[k]   += d1 * (x - s_a_mean[k]);
            }
            if (elapsed >= CONFIG_CSI_CAL_PHASE_A_S) {
                phase = PH_SELECT;
            }
            break;

        /* ================= PH_SELECT ================= */
        case PH_SELECT: {
            int   n_sel   = 0;
            float min_amp = CONFIG_CSI_SELECT_MIN_AMP / 100.0f;
            for (int k = 0; k < nsub; k++) {
                s_base.mu[k]    = (float)s_a_mean[k];
                s_base.sigma[k] = (s_a_n > 1) ? sqrtf((float)(s_a_m2[k] / (s_a_n - 1))) : 0.0f;
                if (s_base.sigma[k] < SIGMA_FLOOR) s_base.sigma[k] = SIGMA_FLOOR;
                s_base.selected[k] = (s_base.mu[k] >= min_amp) ? 1 : 0;
                if (s_base.selected[k]) n_sel++;
            }
            s_base.n_selected = n_sel;

            /* Re-seed the slow EMA at the empty-room mean before phase B.
             * Until now it has been dragging along whatever it saw first -
             * often the user, still walking out - and on a 60 s time
             * constant that leftover would inflate D through phase B and
             * bake a too-high d_mean/d_std into the baseline. */
            for (int k = 0; k < nsub; k++) {
                s_slow[k] = s_base.mu[k];
            }
            ESP_LOGI(TAG, "using %d of %d subcarriers", n_sel, nsub);
            if (n_sel < 8) {
                ESP_LOGE(TAG, "only %d usable subcarriers - lower CSI_SELECT_MIN_AMP", n_sel);
            }
            s_m_mean = s_m_m2 = s_d_mean = s_d_m2 = 0;
            s_b_n = 0;
            phase = PH_B;
            phase_start = esp_timer_get_time();
            phase_packets = 0;
            break;
        }

        /* ================= PH_B ================= */
        case PH_B: {
            float sm = 0, sd = 0;
            int   cnt = 0;
            for (int k = 0; k < nsub; k++) {
                if (!s_base.selected[k]) continue;
                float sg = s_base.sigma[k];
                sm += fabsf(s_na[k]   - s_fast[k]) / sg;
                sd += fabsf(s_fast[k] - s_slow[k]) / sg;
                cnt++;
            }
            if (cnt > 0) {
                float M = sm / cnt, D = sd / cnt;
                /* smooth first, then accumulate - see METRIC_SMOOTH above */
                if (!live_init) { live_m = M; live_d = D; live_init = true; }
                live_m += METRIC_SMOOTH * (M - live_m);
                live_d += METRIC_SMOOTH * (D - live_d);
                s_b_n++;
                double e1 = live_m - s_m_mean;
                s_m_mean += e1 / s_b_n; s_m_m2 += e1 * (live_m - s_m_mean);
                double e2 = live_d - s_d_mean;
                s_d_mean += e2 / s_b_n; s_d_m2 += e2 * (live_d - s_d_mean);
            }
            if (elapsed >= CONFIG_CSI_CAL_PHASE_B_S) {
                phase = PH_SAVE;
            }
            break;
        }

        /* ================= PH_SAVE ================= */
        case PH_SAVE: {
            /* The channel changed on this very packet: the statistics are
             * from the old channel. Save nothing; the recalibration at the
             * top of the next iteration starts again (D-168). */
            if (chan_recal) break;
            s_base.magic   = CSI_BASELINE_MAGIC;
            s_base.version = CSI_BASELINE_VERSION;
            s_base.csi_len = s_locked_len;
            s_base.nsub    = nsub;
            s_base.fwi     = s_locked_fwi;
            s_base.channel = s_locked_ch;
            s_base.rate_hz = rate_hz;
            s_base.m_mean  = (float)s_m_mean;
            s_base.m_std   = (s_b_n > 1) ? sqrtf((float)(s_m_m2 / (s_b_n - 1))) : 0.0f;
            s_base.d_mean  = (float)s_d_mean;
            s_base.d_std   = (s_b_n > 1) ? sqrtf((float)(s_d_m2 / (s_b_n - 1))) : 0.0f;

            esp_err_t err = baseline_save(&s_base);
            s_have_baseline = (err == ESP_OK);

            ESP_LOGW(TAG, "=================================================");
            ESP_LOGW(TAG, " CALIBRATED (%s)", esp_err_to_name(err));
            ESP_LOGW(TAG, "   subcarriers %u/%u, %.1f Hz, channel %u",
                     s_base.n_selected, s_base.nsub, s_base.rate_hz, s_base.channel);
            ESP_LOGW(TAG, "   empty M %.4f +/- %.4f   D %.4f +/- %.4f",
                     s_base.m_mean, s_base.m_std, s_base.d_mean, s_base.d_std);
            ESP_LOGW(TAG, "   trigger at M > %.4f, D > %.4f sustained %d ms",
                     s_base.m_mean + THR_MOTION * s_base.m_std,
                     s_base.d_mean + THR_STATIC * s_base.d_std,
                     CONFIG_CSI_STATIC_SUSTAIN_MS);
            ESP_LOGW(TAG, " DETECTING NOW - walk in and watch it fire.");
            ESP_LOGW(TAG, "=================================================");

            if (s_base.m_std <= 0.0f) {
                ESP_LOGE(TAG, "m_std is zero - calibration did not see enough packets.");
            }

#if CSI_EXPLAIN
            explain_cal("fresh");                   /* then "config", from analytics_configure */
#endif
            /* the freshly measured rate, not the old baseline's */
            analytics_configure(rate_hz);

            live_init = false;
            phase = PH_DETECT;
            phase_start = esp_timer_get_time();
            gain_detect_start();
            break;
        }

        /* ================= PH_SETTLE ================= */
        case PH_SETTLE:
            if (elapsed >= CONFIG_CSI_CAL_WARMUP_S) {
                /* Step 10: the stored rate drives the EMAs AND every gait
                 * frequency, and a stored baseline does not notice that the
                 * ping interval changed since. Check it against the rate
                 * seen during this settle period - a warning only. */
                float settle_hz = (float)phase_packets / elapsed;
                if (fabsf(settle_hz - rate_hz) > 0.2f * rate_hz) {
                    ESP_LOGW(TAG, "live CSI rate %.1f/s differs from the calibrated %.1f/s - "
                                  "recalibrate ('recal'), and re-enrol if CSI_PING_INTERVAL_MS changed",
                             settle_hz, rate_hz);
                    rchk_warned = true;     /* the PH_DETECT check: same excursion */
                }
                ESP_LOGW(TAG, "detecting");
                live_init = false;
                phase = PH_DETECT;
                phase_start = esp_timer_get_time();
                gain_detect_start();
            }
            break;

        /* ================= PH_DETECT ================= */
        case PH_DETECT: {
            float sm = 0, sd = 0;
            int   cnt = 0;
            for (int k = 0; k < nsub; k++) {
                if (!s_base.selected[k]) continue;
                float sg = s_base.sigma[k];
                sm += fabsf(s_na[k]   - s_fast[k]) / sg;
                sd += fabsf(s_fast[k] - s_slow[k]) / sg;
                cnt++;
            }
            if (cnt == 0) break;

            float M = sm / cnt, D = sd / cnt;
            if (!live_init) { live_m = M; live_d = D; live_init = true; }
            live_m += METRIC_SMOOTH * (M - live_m);
            live_d += METRIC_SMOOTH * (D - live_d);

            float m_sigma = (s_base.m_std > 0) ? (live_m - s_base.m_mean) / s_base.m_std : 0.0f;
            float d_sigma = (s_base.d_std > 0) ? (live_d - s_base.d_mean) / s_base.d_std : 0.0f;

            int64_t now = esp_timer_get_time();

            /* motion branch, hysteresis */
            if (!motion_flag && m_sigma >= THR_MOTION)      motion_flag = true;
            else if (motion_flag && m_sigma < THR_RELEASE)  motion_flag = false;

            /* static branch, must be sustained */
            bool static_flag = false;
            if (d_sigma >= THR_STATIC) {
                if (static_since == 0) static_since = now;
                else if ((now - static_since) / 1000 >= CONFIG_CSI_STATIC_SUSTAIN_MS) static_flag = true;
            } else {
                static_since = 0;
            }

            bool trigger = motion_flag || static_flag;
            if (trigger) last_trigger = now;

            bool new_present = trigger ||
                (present && last_trigger &&
                 (now - last_trigger) / 1000 < CONFIG_CSI_PRESENCE_HOLD_MS);

            /* ---- Step 10: group signals and the motion gate ----
             * Once per packet, whatever the presence state: the gate needs
             * a continuous signal, and gait / count read grp below.
             *
             * Why not Step 08's motion_flag for "still": its 0.5 s fast EMA
             * has its corner (~0.32 Hz) INSIDE the breathing band, so a
             * breathing chest lifts M - in test_e2e, breathing strong enough
             * to carry a heartbeat 1/10 its size held the flag on 100% of
             * the time and vitals never ran. The gate looks only above
             * ~1.6 Hz (motion_gate.h). Step 08's own detection is untouched.
             *
             * room_empty = Step 08's decision for THIS packet: new_present
             * as computed just above, BEFORE the breathing hold below. The
             * hold is derived from the gate's own output (still -> vitals ->
             * br_valid -> hold), and feeding it back here would close a
             * loop. In practice little changes: while breathing holds
             * PRESENT it also refreshes last_trigger, so Step 08's own hold
             * keeps new_present true. What does count as "empty" is a still
             * person Step 08 has lost and breathing has not (yet) brought
             * back - harmless for the floor, because breathing barely
             * registers in the gate (test_gate: hf <= ~1.6 for a 15/min
             * breath 8x the group noise). The floor only learns from
             * seconds in which EVERY packet was "empty". */
#if CSI_HAVE_GATE
            float grp[CSI_NGROUPS];
            dsp_group_dev(s_na, s_fast, s_inv_sigma, s_group_map, nsub, grp);
            bool still = mgate_push(&s_gate, r.t_us, grp, !new_present);
#endif

            /* ---- Step 10: vitals, and the breathing hold ----
             * "still" is the gate's decision; no_csi is false here, since
             * this packet was just accepted. vitals_push() drops its own
             * buffer on any moving packet and on a gap of more than 1 s. */
            bool  breath_hold = false;
            float hold_bpm    = 0.0f;
#if CSI_VIT_DONE
            bool  vit_done    = false;      /* an analysis completed on this packet */
#endif
#if CONFIG_CSI_VITALS_ENABLE
            {
#if CONFIG_CSI_VITALS_HOLDS_PRESENCE
                /* First gate-still packet after a gate-moving one: remember
                 * whether this still episode began PRESENT. Only such an
                 * episode may be held by breathing - it can extend or
                 * restore a presence, never start one in a room that was
                 * empty when it went quiet. (After boot, a recalibration or
                 * a CSI gap the gate restarts MOVING and s_prev_still is
                 * false, so the rule applies there too.) Someone who walks
                 * in too gently for the gate to call it motion does not
                 * start a new episode: breathing cannot hold them - the
                 * safe direction to fail in. */
                if (still && !s_prev_still) {
                    s_still_began_present = new_present;
                }
                s_prev_still = still;
#endif
#if CSI_VIT_DONE
                vit_done = vitals_push(&s_vit, r.t_us, s_na, still);
#else
                vitals_push(&s_vit, r.t_us, s_na, still);
#endif
#if CONFIG_CSI_VITALS_HOLDS_PRESENCE
                const vitals_out_t *vo = vitals_get(&s_vit);
                if (still && s_still_began_present && vo->br_valid) {
                    /* Refreshing last_trigger keeps the ordinary hold alive;
                     * forcing new_present re-asserts a presence that lapsed
                     * during THIS still episode (breathing needs ~20 s of
                     * data, Step 08's hold is 5 s). */
                    breath_hold  = true;
                    hold_bpm     = vo->br_bpm;
                    last_trigger = now;
                    new_present  = true;
                }
#endif
            }
#endif

            /* ---- Step 10: what a presence lapse may carry ----
             * A lapse the breathing hold may undo carries the whose-vitals
             * latch (pv's "absent") and the count (s_cnt_carry). It lasts
             * from presence end until the hold hands them back - NOT merely
             * until Step 08 reports somebody again (s_carry_wait: on a
             * walk-in Step 08 fires 0.4-3.5 s before the gate does).
             * Movement while it lasts means a new still spell could no
             * longer be held (see the breathing hold above): whoever sits
             * next is a new visit - so the latch is released (inside
             * pv_packet) and the carried count is dropped. So is anything
             * carried longer than CSI_CARRY_US, which bounds how long a
             * false hold could revive either. Evaluated BEFORE this
             * packet's transition, with the gate's decision for it.
             * All the times here are r.t_us, the packets' arrival clock that
             * vitals_push bins by (esp_timer, like `now`). */
#if CSI_HAVE_WHO
            pv_packet(&s_pv, r.t_us, still);
#endif
#if CSI_CNT_CARRY
            if (s_cnt_carry > 0 &&                /* non-zero only during a lapse */
                (!still || r.t_us - s_cnt_absent_us > CSI_CARRY_US)) {
                s_cnt_carry = 0;
            }
#endif

            if (new_present != present) {
                present = new_present;
                if (present) {
                    present_since = now;
                    freeze_expired_logged = false;
                }
                led_set(present);
                if (present && breath_hold && !trigger) {
                    ESP_LOGW(TAG, ">>> still person, breathing %.1f/min - holding PRESENT", hold_bpm);
                } else {
                    ESP_LOGW(TAG, ">>> %s  (motion %.1f sd, deviation %.1f sd)",
                             present ? "HUMAN DETECTED" : "room clear", m_sigma, d_sigma);
                }
#if CSI_EXPLAIN
                /* Which branch decided: the same tests as new_present. A
                 * start with a trigger names the trigger even when the
                 * breathing hold was also true. */
                explain_presence(present,
                                 !present ? "hold-expired" :
                                 (breath_hold && !trigger) ? "breathing-hold" :
                                 motion_flag ? "motion" : "static",
                                 true, m_sigma, d_sigma, hold_bpm);
#endif
                if (present) {
                    /* Step 10: a new presence episode - possibly a lapse
                     * the breathing hold is undoing, possibly somebody
                     * new walking in. What the lapse carried is handed
                     * back below, once the hold shows which. */
#if CSI_CARRY_WAIT
                    s_carry_wait = true;
#elif CSI_HAVE_WHO
                    pv_presence_start(&s_pv);     /* nothing is carried without the hold */
#endif
                } else {
                    /* Step 10: a presence episode is over - gait, count and
                     * both vote rings start again with the next one (vitals
                     * carry on, see analytics_reset). The whose-vitals latch
                     * is kept if the breathing hold may yet undo this lapse
                     * (pv_presence_end decides), and so is the count, here.
                     * An episode that ends while still waiting never ended
                     * the lapse before it: that lapse's carry and its clock
                     * stand, as pv_presence_end keeps its first time.
                     * (Nothing was counted during the wait - the gate was
                     * still throughout, so no block had movers.) */
#if CSI_HAVE_WHO
                    pv_presence_end(&s_pv, r.t_us);
#endif
#if CSI_CNT_CARRY
                    /* read first: analytics_reset clears all of these */
                    int     cnt_carry = s_carry_wait ? s_cnt_carry     : s_cnt_est;
                    int64_t cnt_since = s_carry_wait ? s_cnt_absent_us : r.t_us;
#endif
                    analytics_reset(false);
#if CSI_CNT_CARRY
                    s_cnt_carry     = cnt_carry;
                    s_cnt_absent_us = cnt_since;
#endif
                }
            }

#if CSI_CARRY_WAIT
            /* ---- Step 10: the end of a lapse, for what it carried ----
             * The latch and the count come back on the first PRESENT packet
             * on which the breathing hold asserts presence - the start
             * packet itself when breathing is what restored it. Such a hold
             * means the still spell that began PRESENT, before the lapse,
             * is unbroken: nobody the gate could see has moved since. A
             * start by Step 08 alone (a walk-in, or a seated person's
             * breathing tripping its flag) waits: the gate calling MOVING
             * first ends the wait with both already let go (above), as
             * during the lapse. The vote ring stays empty either way (a
             * seated person casts no votes, so "person" stays null - by
             * design). */
            if (s_carry_wait) {
                if (breath_hold) {
                    s_carry_wait = false;
#if CSI_HAVE_WHO
                    pv_presence_start(&s_pv);
#endif
#if CSI_CNT_CARRY
                    /* Without this the count latch, wiped when presence
                     * ended, would read 1 ("presence") after the restore
                     * while vitals still said "multiple" - two seated people
                     * never vote a count again (count_step needs movers). */
                    if (s_cnt_carry > 0 && s_cnt_est == 0) {
                        s_cnt_est = s_cnt_carry;
                        ESP_LOGI(TAG, "count %s carried over the presence lapse",
                                 s_cnt_est >= 2 ? "two or more" : "one");
#if CSI_HAVE_WHO
                        /* count_step made the latch MULTIPLE when the 2
                         * was first voted, and nothing since released it
                         * without also dropping this carry - so this only
                         * restates it. Never called for 1. */
                        if (s_cnt_est >= 2) pv_on_count(&s_pv, s_cnt_est);
#endif
                    }
                    s_cnt_carry = 0;
#endif
                } else if (!still) {
                    s_carry_wait = false;         /* both were let go above */
                }
            }
#endif

            /* ---- Step 10: count, then recognition - only while PRESENT ----
             * Blocks and windows therefore start with the episode, and an
             * empty room costs nothing. Count first: recognition must know
             * whether this might be two people.
             *
             * "moving" needs BOTH detectors: Step 08's flag AND the gate.
             * A still person breathing hard trips the flag (the reason the
             * gate exists); without the gate's veto their breathing would
             * fill gait windows and count blocks as if it were walking -
             * enrolling a template of somebody sitting, or voting a count.
             * While the gate's floor is still unknown it reports MOVING, so
             * this falls back to the flag alone. */
#if CSI_HAVE_MODELS
            if (present) {
                bool moving = motion_flag && !still;
#if CONFIG_CSI_COUNT_ENABLE
                count_step(grp, moving);
#endif
#if CONFIG_CSI_ID_ENABLE
                id_step(grp, moving, current_count(present));
#endif
            }
#endif

#if CSI_HAVE_ACTIVITY
            /* ---- Step 10: activity state and presence episodes ----
             * With THIS packet's final presence - after the breathing hold
             * and the transition above - because "still" lasts only as
             * long as PRESENT does, and for a motionless person that is
             * the hold's doing (activity.h). breath_hold tells the episode
             * counter a lapse was undone, not ended. Before the vitals
             * crediting and its EXPLAIN event, which reports the state. */
            act_push(&s_act, r.t_us, present, still, mgate_ratio(&s_gate), breath_hold);
#endif

#if CSI_HAVE_WHO
            /* ---- Step 10: credit the named person's vitals log ----
             * Once per COMPLETED analysis (~every 2 s), never per packet.
             * Here, after this packet's presence transition, count and
             * recognition, so present, count and the latch are all this
             * packet's (person_vitals.h has the crediting rules). Nothing
             * is credited while a lapse's carry waits (who_present). */
            if (vit_done) {
                pv_analysis(&s_pv, r.t_us, who_present(present), current_count(present),
                            vitals_get(&s_vit));
            }
#endif
#if CSI_EXPLAIN && CONFIG_CSI_VITALS_ENABLE
            /* After the crediting above, so vitals_of is this packet's. */
            if (vit_done) explain_vitals(present);
#endif

            int level = (int)(m_sigma * 100.0f / THR_MOTION);
            if (level < 0)   level = 0;
            if (level > 999) level = 999;

#if CSI_EXPLAIN
            /* The one per-packet EXPLAIN line, rate-limited (and 0 = off). */
            if (CONFIG_CSI_EXPLAIN_AMP_HZ > 0 && now >= next_amp) {
                next_amp = now + amp_period_us;
                if (console_tx_room()) {
                    explain_amp(m_sigma, d_sigma, present);
                }
            }
#endif

#if CONFIG_CSI_JSON_PERIOD_MS > 0
            if (now >= next_json) {
                next_json = now + CONFIG_CSI_JSON_PERIOD_MS * 1000LL;
                report_t rep;
                report_build(&rep, present);
                json_begin();
                /* Step 08's keys in Step 08's order and format - the same
                 * bytes for any finite value - but the floats go through
                 * json_num, so a NaN or inf prints null (here only; Step
                 * 08's own file is unchanged). */
                json_adv(snprintf(json_end(), json_room(),
                         "JSON {\"t_ms\":%" PRIu32 ",\"state\":\"detect\",\"present\":%d",
                         (uint32_t)(now / 1000), present ? 1 : 0));
                json_num("motion_sd", true, m_sigma, 2);
                json_num("dev_sd", true, d_sigma, 2);
                json_adv(snprintf(json_end(), json_room(), ",\"level\":%d,\"rssi\":%d",
                                  level, r.rssi));
                json_num("rate_hz", true, rate_hz, 1);
                json_adv(snprintf(json_end(), json_room(), ",\"nsub\":%u", s_base.n_selected));
                /* s_dropped: one plain read, see the once-a-minute check */
                uint32_t dropped = s_dropped;
                json_step10(&rep, s_proc_us_max, dropped);
                json_print();
                s_proc_us_max = 0;
            }
#endif

#if CONFIG_CSI_HUMAN_READABLE
            /* skipped while no host is reading (console_tx_room) */
            if (now >= next_human && console_tx_room()) {
                next_human = now + 250000;
                char bar[41];
                int fill = (int)(m_sigma * 40.0f / (THR_MOTION * 2.0f));
                if (fill < 0)  fill = 0;
                if (fill > 40) fill = 40;
                memset(bar, '#', fill);
                memset(bar + fill, '.', 40 - fill);
                bar[40] = 0;

                /* Step 10 tail: n=count ("2+" = two or more), who=person,
                 * br=breaths/min, hr~=beats/min - the "~" flags the heart
                 * rate as experimental. "-" = not available. of=vitals_of,
                 * the name the rates are attributed to - only when it says
                 * something who= does not: a seated person has no "person"
                 * (nobody can be recognised sitting), yet can have one. */
                report_t rep;
                char     br_s[16], hr_s[16];
                report_build(&rep, present);
                fmt_num(br_s, sizeof(br_s), rep.br_ok, 0, rep.br_bpm);
                fmt_num(hr_s, sizeof(hr_s), rep.hr_ok, 0, rep.hr_bpm);
                bool of = rep.vitals_of && (!rep.person || strcmp(rep.vitals_of, rep.person) != 0);
                printf("%-8s |%s| motion %6.1f sd  dev %6.1f sd  level %3d%%  rssi %d"
                       "  n=%s who=%s%s%s br=%s hr~%s%s%s%s\n",
                       present ? "PRESENT" : "empty", bar, m_sigma, d_sigma, level, r.rssi,
                       rep.count >= 2 ? "2+" : (rep.count == 1 ? "1" : "0"),
                       rep.person ? rep.person : "-",
                       of ? " of=" : "", of ? rep.vitals_of : "", br_s, hr_s,
                       rep.session[0] ? "  [" : "", rep.session, rep.session[0] ? "]" : "");
            }
#endif
            break;
        }

        default:
            break;
        }

periodic:
        /* ---- Step 10: this packet's processing time ---- */
        if (t_work0 != 0) {
            int64_t  dt = esp_timer_get_time() - t_work0;
            uint32_t us = (dt < 0) ? 0u : (dt > (int64_t)UINT32_MAX ? UINT32_MAX : (uint32_t)dt);
#if CONFIG_CSI_JSON_PERIOD_MS > 0
            if (us > s_proc_us_max) s_proc_us_max = us;
#endif
            if (us > s_proc_us_max_all) s_proc_us_max_all = us;
        }

        /* ---- Step 10: were packets dropped in the last minute? ----
         * s_dropped is incremented by the Wi-Fi task (csi_rx_callback) and
         * read here. It is a volatile, naturally aligned uint32_t, and on
         * this 32-bit core a 32-bit load is one instruction: the read can
         * never see half an update, so neither a lock nor a critical
         * section in the CSI callback is needed. It is read ONCE into a
         * local, so the difference and the total printed agree. */
        if (esp_timer_get_time() >= next_drop_check) {
            next_drop_check = esp_timer_get_time() + 60000000;
            uint32_t dropped = s_dropped;
            if (dropped != drops_seen) {
                ESP_LOGW(TAG, "%" PRIu32 " CSI packets dropped in the last minute (%" PRIu32 " since boot) - "
                              "the pipeline fell behind its %d-packet queue; slowest packet %.1f ms",
                         dropped - drops_seen, dropped, CSI_QUEUE_LEN,
                         (float)s_proc_us_max_all / 1000.0f);
                drops_seen = dropped;
            }
        }

        /* ---- no-CSI watchdog (detect phase only) ---- */
        if (phase == PH_DETECT && !no_csi &&
            (esp_timer_get_time() - last_accepted) / 1000 >= CONFIG_CSI_NO_CSI_TIMEOUT_MS) {
            no_csi       = true;
            motion_flag  = false;
            static_since = 0;
            last_trigger = 0;
            if (present) {
                present = false;
                led_set(false);
#if CSI_EXPLAIN
                explain_presence(false, "no-csi", false, 0.0f, 0.0f, 0.0f);
#endif
            }
            /* Step 10: after a gap nothing buffered can be trusted - the
             * breathing buffer would bridge a hole, a gait window would
             * splice two moments. Models and any session are kept. */
            analytics_reset(true);
            ESP_LOGW(TAG, ">>> no usable CSI for %d ms - link down or CSI shape changed. "
                          "Presence UNKNOWN, reporting clear.", CONFIG_CSI_NO_CSI_TIMEOUT_MS);
        }

#if CSI_HAVE_WHO
        /* ---- Step 10: the per-person vitals log to NVS ----
         * Only if it changed, and at most once per CSI_VITALS_LOG_SAVE_S
         * (flash wear - see its help text); the price is up to that many
         * seconds of log lost on a power cut. Here, after this packet's
         * timing was taken, so a slow flash write is not blamed on the
         * packet (proc_ms_max): one over 50 ms is logged here instead, and
         * CSI_QUEUE_LEN rides it out. A failed save is retried after the
         * same interval, never on the next packet (pv_saved stamps every
         * attempt). */
        if (pv_save_due(&s_pv, esp_timer_get_time(),
                        (int64_t)CONFIG_CSI_VITALS_LOG_SAVE_S * 1000000)) {
            int64_t   t_save0 = esp_timer_get_time();
            esp_err_t verr    = vit_log_save();
            int64_t   save_us = esp_timer_get_time() - t_save0;
            if (verr != ESP_OK) {
                ESP_LOGW(TAG, "vitals log save failed (%s) - next attempt in %d s",
                         esp_err_to_name(verr), CONFIG_CSI_VITALS_LOG_SAVE_S);
            } else if (save_us > 50000) {
                ESP_LOGW(TAG, "vitals log save took %d ms (the CSI queue holds %d packets)",
                         (int)(save_us / 1000), CSI_QUEUE_LEN);
            }
        }
#endif
#if CONFIG_CSI_JSON_PERIOD_MS > 0
        if (no_csi && esp_timer_get_time() >= next_json) {
            next_json = esp_timer_get_time() + CONFIG_CSI_JSON_PERIOD_MS * 1000LL;
            printf("JSON {\"t_ms\":%" PRIu32 ",\"state\":\"no_csi\",\"present\":0}\n",
                   (uint32_t)(esp_timer_get_time() / 1000));
        }
#endif

        if (esp_timer_get_time() >= next_log) {
            next_log = esp_timer_get_time() + 1000000;

#if CSI_HAVE_CONSOLE
            console_tx_check();         /* is a host reading our output? */
#endif

            /* ---- Step 10: live packet rate ---- */
            {
                int64_t  t_now  = esp_timer_get_time();
                uint32_t usable = accepted + (uint32_t)s_observed;
                live_hz = (float)(usable - live_n0) * 1e6f / (float)(t_now - live_t0);
                live_n0 = usable;
                live_t0 = t_now;

                /* The stored rate drives the EMAs and every gait frequency,
                 * and PH_SETTLE only compares it with the live rate once, at
                 * boot. Check again every 10 s while detecting: a router that
                 * answers slower, or a pipeline that overflows its queue,
                 * shows up here. Warned once per excursion, re-armed when
                 * the rate is back in range. A CSI outage is reported by
                 * the watchdog instead, so it restarts the window; so does
                 * a recalibration, which also re-measures the rate. */
                if (phase != PH_DETECT) {
                    rchk_t0     = 0;
                    rchk_warned = false;
                } else if (no_csi) {
                    rchk_t0 = 0;
                } else if (rchk_t0 == 0) {
                    rchk_t0 = t_now;
                    rchk_n0 = usable;
                } else if (t_now - rchk_t0 >= 10000000) {
                    float hz  = (float)(usable - rchk_n0) * 1e6f / (float)(t_now - rchk_t0);
                    bool  off = fabsf(hz - rate_hz) > 0.2f * rate_hz;
                    if (off && !rchk_warned) {
                        ESP_LOGW(TAG, "live CSI rate %.1f/s is more than 20%% off the calibrated %.1f/s "
                                      "(%" PRIu32 " dropped since boot) - gait and count assume the "
                                      "calibrated rate: check the router, or 'recal' if the ping "
                                      "interval changed",
                                 hz, rate_hz, (uint32_t)s_dropped);
                        rchk_warned = true;
                    } else if (!off && rchk_warned) {
                        ESP_LOGW(TAG, "live CSI rate back to %.1f/s (calibrated %.1f/s)", hz, rate_hz);
                        rchk_warned = false;
                    }
                    rchk_t0 = t_now;
                    rchk_n0 = usable;
                }
            }

            if (phase != PH_DETECT) {
                float el = (esp_timer_get_time() - phase_start) / 1e6f;
                int remain = 0;
                switch (phase) {
                    case PH_LOCK:   remain = 0; break;
                    case PH_LEAVE:  remain = CONFIG_CSI_CAL_LEAVE_ROOM_S - (int)el; break;
                    case PH_WARMUP: remain = CONFIG_CSI_CAL_WARMUP_S     - (int)el; break;
                    case PH_A:      remain = CONFIG_CSI_CAL_PHASE_A_S    - (int)el; break;
                    case PH_B:      remain = CONFIG_CSI_CAL_PHASE_B_S    - (int)el; break;
                    case PH_SETTLE: remain = CONFIG_CSI_CAL_WARMUP_S     - (int)el; break;
                    default: break;
                }
                if (phase == PH_LOCK) {
                    ESP_LOGI(TAG, "[%s] %d/%d packets",
                             phase_name(phase), s_observed, CONFIG_CSI_LOCK_PACKETS);
                } else {
                    ESP_LOGI(TAG, "[%s] %d s left, %" PRIu32 " packets",
                             phase_name(phase), remain > 0 ? remain : 0, phase_packets);
                }
#if CONFIG_CSI_JSON_PERIOD_MS > 0
                printf("JSON {\"t_ms\":%" PRIu32 ",\"state\":\"%s\",\"present\":0,"
                       "\"remaining_s\":%d}\n",
                       (uint32_t)(esp_timer_get_time() / 1000),
                       phase_name(phase), remain > 0 ? remain : 0);
#endif
            } else if (wrong_shape > accepted && accepted > 0) {
                ESP_LOGW(TAG, "most packets have the wrong CSI shape (%" PRIu32 " vs %" PRIu32 ")",
                         wrong_shape, accepted);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/*  CSI enable / ping / WiFi                                           */
/* ------------------------------------------------------------------ */
static void csi_start(void)
{
#if CONFIG_CSI_LLTF_ONLY
    wifi_csi_config_t csi_cfg = {
        .enable = 1, .acquire_csi_legacy = 1, .acquire_csi_force_lltf = 1,
        .acquire_csi_ht20 = 0, .acquire_csi_ht40 = 0, .acquire_csi_vht = 0,
        .acquire_csi_su = 0, .acquire_csi_mu = 0, .acquire_csi_dcm = 0,
        .acquire_csi_beamformed = 0, .acquire_csi_he_stbc_mode = 0,
        .val_scale_cfg = 0, .dump_ack_en = 1, .reserved = 0,
    };
    ESP_LOGI(TAG, "CSI: L-LTF only (12-bit samples)");
#else
    /* HT-LTF / HE-LTF only, both plain int8. Legacy (11g) frames and ACKs
     * are left out: on the C5 their L-LTF is 12-bit packed, and mixing
     * them in gave a ~50/50 two-shape stream in which the lock often
     * picked the 106-byte ACK shape. Trade-off: a router that answers
     * pings with 11g frames now yields no CSI - Step 03 shows that, and
     * 'Capture L-LTF only' is the fallback. */
    wifi_csi_config_t csi_cfg = {
        .enable = 1, .acquire_csi_legacy = 0, .acquire_csi_force_lltf = 0,
        .acquire_csi_ht20 = 1, .acquire_csi_ht40 = 0, .acquire_csi_vht = 0,
        .acquire_csi_su = 1, .acquire_csi_mu = 1, .acquire_csi_dcm = 0,
        .acquire_csi_beamformed = 0, .acquire_csi_he_stbc_mode = 2,
        .val_scale_cfg = 0, .dump_ack_en = 0, .reserved = 0,
    };
    ESP_LOGI(TAG, "CSI: HT/HE-LTF, auto-lock");
#endif
    ESP_ERROR_CHECK(esp_wifi_set_csi_config(&csi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(&csi_rx_callback, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_csi(true));
}

static void ping_noop(esp_ping_handle_t hdl, void *args) { }

/* Called on every GOT_IP: resume the session, or re-aim it if the gateway
 * changed. Errors are logged, not ESP_ERROR_CHECKed - this runs from the
 * event handler, and aborting there on a transient failure would reboot. */
static void ping_start(esp_ip4_addr_t target)
{
    if (s_ping) {
        if (s_ping_target.addr == target.addr) {
            esp_ping_start(s_ping);
            ESP_LOGI(TAG, "ping resumed");
            return;
        }
        esp_ping_stop(s_ping);
        esp_ping_delete_session(s_ping);    /* its task exits by itself */
        s_ping = NULL;
    }

    ip_addr_t target_addr = { 0 };
    target_addr.type = IPADDR_TYPE_V4;
    target_addr.u_addr.ip4.addr = target.addr;

    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    cfg.target_addr = target_addr;
    cfg.count       = ESP_PING_COUNT_INFINITE;
    cfg.interval_ms = CONFIG_CSI_PING_INTERVAL_MS;
    cfg.timeout_ms  = CONFIG_CSI_PING_INTERVAL_MS * 2;
    cfg.data_size   = 32;
    /* Step 10: above csi_pipeline (4). esp_ping keeps ONE echo in flight
     * (ping_sock.c: send, block for the reply, vTaskDelayUntil), so a ping
     * task below the pipeline stops sending while a gait window or count
     * block runs: the stream pauses instead of queueing, the samples gait
     * treats as evenly spaced arrive as a gap and a burst, and 'dropped'
     * can never count an overload. Above it, pings keep their cadence,
     * CSI_QUEUE_LEN absorbs the stall, and 'dropped' means what the JSON
     * says. The task blocks almost all the time, so this costs the
     * pipeline two short wake-ups per ping. */
    cfg.task_prio   = 5;

    esp_ping_callbacks_t cbs = {
        .on_ping_success = ping_noop, .on_ping_timeout = ping_noop,
        .on_ping_end = ping_noop, .cb_args = NULL,
    };
    esp_err_t err = esp_ping_new_session(&cfg, &cbs, &s_ping);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ping session failed: %s", esp_err_to_name(err));
        s_ping = NULL;
        return;
    }
    s_ping_target = target;
    esp_ping_start(s_ping);
    ESP_LOGI(TAG, "ping -> " IPSTR " every %d ms", IP2STR(&target), CONFIG_CSI_PING_INTERVAL_MS);
}

/* ------------------------------------------------------------------ */
/*  Reconnection policy                                                */
/*  First join: CONFIG_CSI_WIFI_MAX_RETRY attempts, then WIFI_FAIL_BIT */
/*  so app_main can report bad credentials and reboot.                 */
/*  After the first join: never give up. A router reboot or firmware   */
/*  update is off the air for 1-3 minutes, and a board that stops      */
/*  retrying stays offline until someone power-cycles it. Retries back */
/*  off 1, 2, 4 ... 30 s and run from a timer, because a vTaskDelay in */
/*  this handler stalls every other event on the default loop. The     */
/*  ping pauses while the link is down (no send-error flood) and is    */
/*  resumed or re-aimed on the next GOT_IP.                            */
/* ------------------------------------------------------------------ */
static void reconnect_cb(void *arg)
{
    esp_wifi_connect();
}

static void schedule_reconnect(uint32_t delay_ms)
{
    esp_timer_stop(s_reconnect_timer);          /* harmless if idle */
    esp_timer_start_once(s_reconnect_timer, (uint64_t)delay_ms * 1000);
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)data;
        if (s_ping) {
            esp_ping_stop(s_ping);
        }
        if (s_connected_once) {
            uint32_t delay_s = 1u << (s_retry_count < 5 ? s_retry_count : 5);
            if (delay_s > 30) delay_s = 30;
            s_retry_count++;
            ESP_LOGW(TAG, "link lost (reason %d), reconnect attempt %d in %" PRIu32 " s",
                     d->reason, s_retry_count, delay_s);
            schedule_reconnect(delay_s * 1000);
        } else if (s_retry_count < CONFIG_CSI_WIFI_MAX_RETRY) {
            s_retry_count++;
            ESP_LOGW(TAG, "disconnected (reason %d), retry %d/%d",
                     d->reason, s_retry_count, CONFIG_CSI_WIFI_MAX_RETRY);
            schedule_reconnect(1000);
        } else {
            xEventGroupSetBits(s_wifi_events, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        s_gateway = e->ip_info.gw;
        ESP_LOGI(TAG, "got IP " IPSTR ", gateway " IPSTR,
                 IP2STR(&e->ip_info.ip), IP2STR(&e->ip_info.gw));
        s_retry_count = 0;
        s_connected_once = true;
        ping_start(s_gateway);
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void)
{
    s_wifi_events = xEventGroupCreate();

    const esp_timer_create_args_t rt = {
        .callback = reconnect_cb, .name = "wifi_reconnect",
    };
    ESP_ERROR_CHECK(esp_timer_create(&rt, &s_reconnect_timer));

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    wifi_config_t wc = { 0 };
    strncpy((char *)wc.sta.ssid, CONFIG_CSI_WIFI_SSID, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, CONFIG_CSI_WIFI_PASSWORD, sizeof(wc.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Keep CSI comparable: 2.4 GHz, 20 MHz, radio always awake. */
    esp_wifi_set_band_mode(WIFI_BAND_MODE_2G_ONLY);
    esp_wifi_set_bandwidth(WIFI_IF_STA, CSI_WIFI_BW20);
    esp_wifi_set_ps(WIFI_PS_NONE);
}

/* ------------------------------------------------------------------ */
/*  Step 10: module state and stored models, before the pipeline runs. */
/* ------------------------------------------------------------------ */
static void analytics_init(void)
{
#if CSI_HAVE_GATE
    /* The threshold is set HERE and only here (mgate_set_rate keeps it).
     * The rate is a placeholder, as for gait_init: analytics_configure()
     * sets the MEASURED rate, and clears the floor, before the first
     * packet reaches mgate_push(). */
    mgate_init(&s_gate, 1000.0f / (float)CONFIG_CSI_PING_INTERVAL_MS, CSI_GATE_RATIO);
    ESP_LOGI(TAG, "motion gate: moving at hf >= %.2f, %u bytes static",
             CSI_GATE_RATIO, (unsigned)sizeof(s_gate));
#endif

#if CSI_HAVE_ACTIVITY
    /* The episode carry is the whose-vitals latch's: the longest lapse the
     * breathing hold is taken to undo. Without the hold nothing undoes a
     * lapse, so every lapse ends the episode. */
    {
        act_cfg_t acfg = {
            .move_thr    = CSI_GATE_RATIO,
            .restless_lo = (float)CONFIG_CSI_ACTIVITY_RESTLESS_X100 / 100.0f,
#if defined(CONFIG_CSI_VITALS_HOLDS_PRESENCE) && CONFIG_CSI_VITALS_HOLDS_PRESENCE
            .carry_us    = CSI_CARRY_US,
#else
            .carry_us    = 0,
#endif
        };
        act_init(&s_act, &acfg);
        ESP_LOGI(TAG, "activity: restless at hf >= %.2f, moving at >= %.2f, %u bytes static",
                 acfg.restless_lo, acfg.move_thr, (unsigned)sizeof(s_act));
    }
#endif

#if CONFIG_CSI_VITALS_ENABLE
    vitals_cfg_t vcfg = {
        .window_s    = CONFIG_CSI_VITALS_WINDOW_S,
        .min_s       = CONFIG_CSI_VITALS_MIN_S,
        .br_conf_min = (float)CONFIG_CSI_VITALS_BR_CONF_X100 / 100.0f,
        .hr_conf_min = (float)CONFIG_CSI_VITALS_HR_CONF_X100 / 100.0f,
    };
    vitals_init(&s_vit, &vcfg);
    ESP_LOGI(TAG, "vitals: breathing + EXPERIMENTAL heart rate, %u bytes static", (unsigned)sizeof(s_vit));
#endif

#if CONFIG_CSI_ID_ENABLE
    /* Placeholder rate only: analytics_configure() sets the MEASURED rate
     * before the first packet reaches gait_push(). */
    gait_init(&s_gait, 1000.0f / (float)CONFIG_CSI_PING_INTERVAL_MS);
    esp_err_t err = model_load(ID_NVS_KEY, &s_id_model, GAIT_NFEAT);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "recognition model found (%u-byte blob): %d of %d slots ready",
                 (unsigned)sizeof(s_id_model),
                 ncc_trained_classes(&s_id_model, (uint32_t)ID_MIN_WINDOWS), NCC_MAX_CLASSES);
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "no recognition model yet - enrol people with 'enroll <slot> <name>'");
    } else {
        ESP_LOGW(TAG, "recognition model in NVS unusable (%s, expected a %u-byte blob) - "
                      "starting empty, re-enrol", esp_err_to_name(err), (unsigned)sizeof(s_id_model));
    }
#endif

#if CSI_HAVE_WHO
    /* Whose vitals: the latch and the per-person vitals log. AFTER the
     * recognition model, because every loaded record is checked against
     * the name its slot holds NOW: a slot that was re-enrolled or
     * forgotten without the log being saved, or a model that failed to
     * load, must never hand one person's history to somebody else. */
    {
        pv_cfg_t pcfg = {
            .carry_us       = CSI_CARRY_US,
#if defined(CONFIG_CSI_VITALS_HOLDS_PRESENCE) && CONFIG_CSI_VITALS_HOLDS_PRESENCE
            .holds_presence = true,
#else
            .holds_presence = false,       /* presence end releases the latch */
#endif
        };
        pv_init(&s_pv, &pcfg);

        pv_log_t  loaded;                  /* ~170 bytes; this is app_main's stack */
        esp_err_t verr = vit_log_load(&loaded);
        if (verr == ESP_OK) {
            const char *names[PV_SLOTS];
            for (int s = 0; s < PV_SLOTS; s++) {
                names[s] = s_id_model.cls[s].name;
            }
            int dropped = pv_adopt_log(&s_pv, &loaded, names);
            int logged  = 0;
            for (int s = 0; s < PV_SLOTS; s++) {
                if (s_pv.log.rec[s].br_eps > 0) logged++;
            }
            ESP_LOGI(TAG, "vitals log found (%u-byte blob): %d of %d slots have logged vitals",
                     (unsigned)sizeof(loaded), logged, PV_SLOTS);
            if (dropped > 0) {
                /* Saved at the next periodic save (pv_adopt_log set dirty). */
                ESP_LOGW(TAG, "vitals log: %d record(s) dropped - their slot now holds another "
                              "name, or none", dropped);
            }
        } else if (verr == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "no vitals log yet - it fills as recognised people sit still");
        } else {
            /* Not erased: it is simply overwritten by the next save. */
            ESP_LOGW(TAG, "vitals log in NVS unusable (%s, expected a %u-byte blob) - "
                          "starting empty", esp_err_to_name(verr), (unsigned)sizeof(loaded));
        }
    }
#endif

#if CONFIG_CSI_COUNT_ENABLE
    count_init(&s_cnt);
    esp_err_t cerr = model_load(CNT_NVS_KEY, &s_cnt_model, COUNT_NFEAT);
    if (cerr == ESP_OK) {
        ESP_LOGI(TAG, "count model found (%u-byte blob): %s", (unsigned)sizeof(s_cnt_model),
                 count_model_ready() ? "trained" : "not fully trained - count = presence");
    } else if (cerr == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "no count model yet - 'count-train 1', then 'count-train 2'");
    } else {
        ESP_LOGW(TAG, "count model in NVS unusable (%s, expected a %u-byte blob) - "
                      "starting empty, retrain", esp_err_to_name(cerr), (unsigned)sizeof(s_cnt_model));
    }
#endif
}

/* ------------------------------------------------------------------ */
void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* First, while nothing else of ours is printing: see console_install. */
    console_install();

    ESP_LOGI(TAG, "=========================================");
    ESP_LOGI(TAG, " ESP32-C5 Wi-Fi CSI human detector");
    ESP_LOGI(TAG, " Step 10: + recognise / count / vitals (EXPERIMENTAL)");
    ESP_LOGI(TAG, "=========================================");

    /* Why this boot happened. Also in 'status' and the EXPLAIN config
     * line: the reset re-enumerated USB, so a host usually misses this. */
    s_reset_reason = esp_reset_reason();
    switch (s_reset_reason) {
    case ESP_RST_PANIC: case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT: case ESP_RST_WDT:
    case ESP_RST_BROWNOUT: case ESP_RST_PWR_GLITCH: case ESP_RST_CPU_LOCKUP:
        ESP_LOGW(TAG, " last reset: %s (%d) - a crash, watchdog or power problem",
                 reset_reason_name(s_reset_reason), (int)s_reset_reason);
        break;
    case ESP_RST_USB:
        ESP_LOGW(TAG, " last reset: %s (%d) - reset by the host over USB-Serial-JTAG "
                      "(esptool / idf.py, or a tool pulsing DTR/RTS - docs/DECISIONS.md D-153)",
                 reset_reason_name(s_reset_reason), (int)s_reset_reason);
        break;
    default:
        ESP_LOGI(TAG, " last reset: %s (%d)", reset_reason_name(s_reset_reason),
                 (int)s_reset_reason);
        break;
    }

    ret = baseline_load(&s_base);
    s_have_baseline = (ret == ESP_OK);
    if (s_have_baseline) {
        ESP_LOGI(TAG, "baseline found: %u bytes CSI, %u/%u subcarriers, ch %u, %.1f Hz",
                 s_base.csi_len, s_base.n_selected, s_base.nsub,
                 s_base.channel, s_base.rate_hz);
    } else {
        ESP_LOGW(TAG, "no baseline (%s) - will calibrate automatically",
                 esp_err_to_name(ret));
    }

    analytics_init();

    led_init();
    led_set(false);
    button_init();

    s_raw_q = xQueueCreate(CSI_QUEUE_LEN, sizeof(csi_raw_t));
    if (!s_raw_q) {
        ESP_LOGE(TAG, "queue alloc failed");
        return;
    }

    wifi_init_sta();

    EventBits_t bits = xEventGroupWaitBits(s_wifi_events,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, portMAX_DELAY);
    if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "WiFi failed. Check SSID/password (must be 2.4 GHz).");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }

    csi_start();
    /* The ping was already started by the GOT_IP handler. */

    xTaskCreate(pipeline_task, "csi_pipeline", 12288, NULL, 4, NULL);

#if CSI_HAVE_CONSOLE
    /* Priority 1: below the pipeline (4) and the ping task (5), so typing
     * can never cost a CSI packet. Started only now that the pipeline -
     * which executes the commands - exists. */
    if (s_cmd_q) {
        if (xTaskCreate(console_task, "csi_console", 4096, NULL, 1, NULL) == pdPASS) {
            ESP_LOGI(TAG, "console ready - type 'help' and Enter in idf.py monitor");
        } else {
            ESP_LOGE(TAG, "console task create failed - no console");
        }
    }
#endif
    ESP_LOGI(TAG, "free heap after start-up: %" PRIu32 " bytes", esp_get_free_heap_size());
}
