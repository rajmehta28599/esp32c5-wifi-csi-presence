#!/usr/bin/env python3
"""
session_score.py - score a hardware-session log against the pass marks of
docs/TESTING.md section 4: the Phase 1 table (1a-1d) and the Phase 2 pass marks.

WHY THIS EXISTS
    tools/session_runner.py plays a §4 protocol (phase1, phase2b) and writes
    the schedule into the same log as the board's lines; this script reads
    that log back and says, per segment, what the board reported while the
    room was supposed to be empty, walked through, or sat in, and whether
    each §4 mark is met. It also reads the logs the project already has
    (serial_capture.py, dashboard.py --log, the 2026-09-28 soak.py capture),
    for the resources, restarts and continuity, which need no schedule.
    Before it, the 2026-09-28 soak was summarised by a one-off script
    (D:\\ESP32C5_CSI_logs\\2026-09-28_step10_soak\\soak_summary.py); this is
    the general version, with the marks beside the numbers.

USAGE
    python tools\\session_score.py LOG [--grace S] [--exit-s S]
                                      [--after-calibration]
                                      [--from S] [--to S] [--json OUT]

    python tools\\session_score.py D:\\ESP32C5_CSI_logs\\2026-10-01_p2b\\p2b.log
    python tools\\session_score.py soak10_0928.log --after-calibration
    python tools\\session_score.py capture.log --from 120 --to 900 --json capture_score.json

    LOG                  the log (format auto-detected, INPUT below)
    --grace S            leave the first S seconds of every segment out
                         (default 10; for a session_runner.py --simulate
                         log run with --scale F, 10 x F, so a --scale 0.02
                         smoke test keeps its shape; a live run at another
                         --scale keeps 10: the operator's reaction does
                         not shrink): the operator reacts after the cue.
                         Every presence figure is printed with what is
                         left out AND with nothing left out
    --exit-s S           leave a further S seconds out of an empty
                         segment that follows a walk / enter / minor /
                         still segment (default 9 = CSI_PRESENCE_HOLD_MS
                         5000 + CSI_STATIC_SUSTAIN_MS 4000, the Kconfig
                         defaults; x F for a --simulate log as --grace).
                         Such a gap still holds the walker's exit and the
                         detector's designed hold (session_runner.py
                         NOTES); a firmware built with another hold needs
                         another value, and 0 scores it from cue + grace
    --after-calibration  score only rows after the LAST calibration in the
                         log: after the last JSON row whose state is
                         leave-room / warmup / calib-A / select / calib-B /
                         save (what soak_summary.txt calls "after
                         calibration"). Applies to the rows and to the
                         'status' replies; restarts and continuity are
                         always reported for the whole log
    --from S, --to S     host-time window (seconds, the log's first column)
                         for a log WITHOUT segment markers; refused for a
                         session_runner.py log (its segments are the window)
    --json OUT           also write every result as JSON. Refused if OUT
                         exists (nothing is overwritten)

    Exit code 0 on success; 2 on unreadable input (no such file, nothing
    timestamped in it) or a refused option.

INPUT (auto-detected; the reader duplicates session_runner.py's)
    1. a session_runner.py log: "%7.2f <line>" board lines plus '#' marker
       lines per its OUTPUT grammar (# RUN, # PLAN, # SEGMENT <t>
       start|end <idx> <kind> <label>, # CUE, # SENT, # PORT <t>
       open|lost|reopened|unavailable ..., # BOARD, # WARN, # END). Segment
       kinds: setup, calibration (not scored), empty, walk, enter, minor,
       still. A board row belongs to the segment whose start and end
       markers enclose it in the FILE (the markers are written in order
       with the board lines), so a row and a marker with the same
       timestamp are not confused.
    2. a plain "%7.2f <line>" log: serial_capture.py (with its
       "# <s> port lost: ..." / "# <s> port open: COMn" lines),
       dashboard.py --log (with "[dashboard] port lost/open ..." lines), or
       the 2026-09-25 bring-up listener (bare "port open" / "port
       unavailable: ..." lines, docs/hw_logs/2026-09-25_step10_*.log).
    3. the soak format "%8.2f HH:MM:SS.mmm <line>" with "## port lost" /
       "## port open" / "## sent: CMD" lines (soak.py, 2026-09-28).
    Any other line starting with '#' is a comment (sample_session.log has
    a header of them). A log without SEGMENT markers is scored as ONE
    segment, kind 'unlabelled': resources, restarts, present fraction and
    calibrations seen, and no pass/fail on the presence marks - nobody
    logged what the room was doing.
    Board rows are the JSON lines exactly as the firmware prints them:
    "JSON {...}" at the start of a line (step08 main.c:776, 823, 850;
    step10 main.c:4103, 4238, 4313), searched anywhere in the line in case
    other output ran into it. Only "state":"detect" rows are scored; the
    other states (lock, leave-room, warmup, calib-A, calib-B, no_csi, ...)
    are counted per segment and left out. A row's time is its HOST time
    (the log's first column), never t_ms: in a --simulate log t_ms keeps
    the recorded spacing while host time is compressed.

WHAT IS SCORED, PER SEGMENT
    all kinds    detect rows, other rows by state, present fraction,
                 PRESENT onsets (0 -> 1 between consecutive detect rows)
    walk (1a)    seconds reported empty per walking second; time from the
                 segment start to the first present=1 (INCLUDES the
                 walk-in: the cue marks the start, not the entry)
    empty (1d)   seconds reported PRESENT per empty second; onsets per hour.
                 An empty segment AFTER AN ACTIVITY (the previous segment
                 is walk / enter / minor / still) is scored from the cue
                 + grace + --exit-s; a SETTLED one (first, or after setup
                 / calibration / empty, e.g. empty-start) from the cue +
                 grace. Both are pooled for the 1d verdict and each is
                 also pooled alone; the 1d line names the pool
    enter        time from the segment start to the first present=1
    minor (1b)   present fraction (no mark)
    still (1c)   present fraction; time from the segment start to the
                 first present=0 (the first lapse); for Step 10 rows the
                 coverage and range of br_bpm and hr_bpm_exp (rows with a
                 number). A coverage, never an accuracy: nothing here
                 compares a rate with a reference
    unlabelled   the "all kinds" figures and the calibrations seen
    Segments of one kind are also POOLED (all walk segments = the 1a
    total) and the pool is scored the same way.

ACTIVITY (only when some JSON row carries Step 10's "activity" key)
    Each detect row's "activity" (null, "empty", "still", "restless",
    "moving") is compared with the activity the segment's KIND asks for:
        empty -> empty    walk -> moving    minor -> restless
        still -> still    enter, setup, calibration, unlabelled -> none
    The map is by kind, so it covers phase1 (1a-1c) and, with the same
    kinds, phase2b. Per segment and per column (the grace (+ exit) left
    out | nothing left out): the rows that carry the key, the count of each
    value seen, and the MATCH FRACTION = rows whose value equals the
    expected one / rows that carry the key. null is a value of its own and
    never a match; a non-string value counts as "?"; a detect row without
    the key is counted apart ("rows without the key") and is not in the
    denominator. A segment of a kind with no expected activity shows its
    counts only. The CONFUSION TABLE pools every segment with an expected
    activity: rows = expected, columns = reported. No mark exists for it
    (docs/TESTING.md section 4 has none); the activity state lags each cue
    by the firmware's own window and hold, which the grace and --exit-s
    may not cover, so read both columns. A log in which no row carries
    the key prints none of this, and its report is the same as before
    the activity key existed except for the tool version line.

GAIN (only when some JSON row carries Step 10's "gain_chg" key, D-176)
    Logging only, no mark. Per scored segment (in the SEGMENTS block, one
    figure per column) and pooled per kind, an unlabelled log's single
    segment included (the grace (+ exit) left out): the detect rows whose
    gain_chg is a count (a non-negative integer, as the firmware prints
    it) and those where it is null (switch off, or no packet since the
    previous line), how many had gain_chg > 0 (the receiver gain changed
    during the row) and the packets that changed it, the PRESENT fraction
    of the rows with a change and of the rows without one, and the range
    of agc. Any other gain_chg value (a float, Infinity, NaN, a negative
    number: a corrupt or edited log) is counted as invalid and used for
    nothing else. The first detect line after a calibration starts its
    count at the switch to detection (Step 10 main.c). In an
    empty segment, a PRESENT fraction much higher in the changed rows
    points at the receiver, not a person - a hint, not a proof: a person
    walking in also moves the gain. The bytes are not a documented ESP-IDF
    field (csi_gain.h); first check that agc moves at all.

    THE ROW PERIOD p is the median over the segment of (t[i+4] - t[i]) / 4
    for consecutive detect rows, not the median of single gaps. Reason: the
    host reads the port in chunks, so single gaps are quantised. On the
    2026-09-28 soak, soak.py's 0.2 s reads split them into ~0.4 s and
    ~0.6 s: after its calibration, 1,589 detect rows span 815 s, i.e.
    0.513 s per row, while the median single gap reads 0.60 s and the
    median over 4 gaps 0.507 s. The plain median gap is printed alongside.
    SCORED SECONDS T = detect rows x p, so a rate "seconds reported X per
    second" = rows reported X / detect rows, and the window's coverage =
    T / window length shows rows that never arrived (port lost, output
    dropped). RESOLUTION: the smallest nonzero rate a window can show is
    p / T (one row). When the observed rate is 0 but p / T is above the
    mark, the verdict is "not applicable (0 observed, but T s cannot
    resolve the mark (needs >= p/mark s))" - never 'met'.

RESOURCES AND CONTINUITY
    From Step 10 detect rows: dropped (max, first, last, whether it ever
    increased), proc_ms_max (median, 95th percentile, max), rate_hz
    (min / median / max; the CALIBRATED rate, fixed at calibration:
    step10 main.c:3378). From 'status' replies (step10 main.c:2574-2650):
    free heap at the first and the last reply and the minimum, the live
    rate, the slowest packet since boot, drops since boot, uptime. Also
    "free heap after start-up" boot lines and the firmware's own warnings
    ("N CSI packets dropped in the last minute", main.c:4181; "live CSI
    rate ... differs from the calibrated", main.c:3776, printed when the
    detector starts, which silences the next one for the same excursion;
    "live CSI rate ... is more than 20% off", main.c:4278).
    Restarts: boot banners (" ESP32-C5 Wi-Fi CSI human detector", both
    steps: step08 main.c:1047, step10 main.c:4626), ESP-ROM "rst:" lines,
    and the board's uptime going backwards by more than 2 s, or by more
    than 0.5 s to below 2 s (a restart during the Wi-Fi join, as on
    2026-09-25, never shows a 2 s drop) - one clock read from the ESP log
    timestamps "I (ms)", JSON t_ms and status "up N s". Evidence within
    10 s of host time is one boot, except that a second banner or a second
    uptime drop is always another boot. A boot is a RESTART INSIDE THE LOG
    when board output came before it, when the uptime went back, or when
    the port had been open and was lost within 30 s before it (the
    USB-Serial-JTAG port exists only while the chip runs, and a reset that
    re-enumerates it drops it; not every restart does, TESTING G19; the
    2026-09-25 capture's first banner, after 33 s with no output, is such
    a boot: docs/TESTING.md §5.1). Port losses and
    reopens, silences over 5 s, t_ms jumps over 5 s (rows lost), and the
    firmware's "console: output resumed - for ~N s no host read it".

MARKS (each printed beside its value and origin; verdict 'met' /
       'NOT met' / 'no mark' / 'not applicable (<reason>)')
    walking seconds reported empty <= 0.001 per second - TGbf pre-standard
        target sheet 11-20/1712r2 (docs/COMPARISON.md section 3); TESTING §4 1a.
        IEEE 802.11bf itself sets no bar
    empty seconds reported PRESENT <= 0.0001 per second - the same sheet;
        TESTING §4 1d (an 8 h night). Verdict from the pool of all empty
        segments (after-activity ones from cue + grace + --exit-s); the
        settled and after-activity pools are printed beside it
    dropped stays 0 over a 30-min run - project mark 2026-09-25, TESTING §4
        Phase 2. An increase seen is NOT met however short the log (it
        fails the 30 min that contain it); a shorter run with no increase:
        not applicable, the value still shown
    free heap from 'status' at the end not below the start - project mark,
        TESTING §4 Phase 2. Not applicable across a restart
    live rate >= 90/s at a 10 ms ping - project mark 2026-09-25, TESTING §4
        Phase 2; only when rate_hz is near 100/s
    proc_ms_max - no mark; compared with README "Cost." estimates (~6-10 ms
        per vitals FFT, ~12-32 ms per gait window; README section "Kconfig -
        menuconfig -> Recognise / count / vitals", its "Cost." paragraph)
    1b and 1c - no mark ("none: record it", TESTING §4)

OUTPUT
    A text report on stdout: the log and run header, JSON rows and
    calibrations, one block per segment (a two-column table: with the
    grace (+ exit) left out | with nothing left out), the pools,
    resources, restarts and continuity, then the MARKS list. --json OUT
    writes the same results as one JSON object (keys: tool, log, format,
    labelled, options, columns, two_columns, run, plan, end, board, rows,
    calibrations, calibrated_lines, cutoff, segment_notes, segments (each
    with 'scored' true / false and, when scored, a 'trimmed' figure set -
    grace (+ exit) left out - and, when two_columns, a 'none_left_out' one),
    pools and pools_none_left_out (per kind, plus empty_settled and
    empty_after_activity), resources, scope_text, boots, port,
    port_retries, continuity, marks, notes, written; and, only when a row
    carries "activity": a per-column 'activity' object in each scored
    segment, and a top-level 'activity' with expected_by_kind and one
    confusion table per column; and, only when a row carries "gain_chg": a
    'gain' object in each of a scored segment's column sets ('trimmed' and,
    when two_columns, 'none_left_out') and a top-level 'gain' pooled per
    kind from the 'trimmed' sets, for every kind with a scored segment).

NOTES
    * THE SCHEDULE IS THE GROUND TRUTH ONLY AS FAR AS THE OPERATOR FOLLOWED
      IT (session_runner.py NOTES): a walker who came in late, lingered at
      the door, a pet or a neighbour is not in the log. The report says so.
      Write deviations down with their times (docs/TESTING.md §5.1).
    * A rate here is per second of rows the board sent. A segment whose
      rows did not arrive (port lost, console output dropped) has a low
      coverage; read it before the rate.
    * An empty segment right after an activity is not settled empty room:
      it holds the walker's exit, the 5 s presence hold and the 4 s static
      sustain (session_runner.py NOTES). Scored from the cue + grace only,
      a detector behaving as designed fails 1d on it; hence --exit-s. The
      'nothing left out' column and pool include all of it.
    * Time to the first present=1 is from the segment start (the cue), not
      from the moment the walker entered the room; TESTING §4 1a asks for
      the latter, which needs the written log sheet.
    * The live-rate mark asks for a 60 s mean. JSON rate_hz is the
      calibrated rate, not the live one; the only live figures in a log are
      the 1-s values in 'status' replies, so their mean is used as a proxy
      and the report says so.
    * Log timestamps that jump backwards by more than 0.5 s (two sessions
      appended to one dashboard.py --log file) are shifted so that the
      later session continues after the earlier one, and reported.
    * Python 3.8+ standard library only; it never opens a serial port. The
      log reader duplicates session_runner.py's on purpose (the repo's
      copy-one-file convention): TS_RE, SOAK_RE, MARK_RE and detect_format
      are textually identical to its copies, so a change to the log
      grammar must be made in both files.
"""

import argparse
import bisect
import datetime
import json
import os
import re
import statistics
import sys

TOOL = "session_score.py 5"   # 2: columns 'trimmed' / 'none_left_out', --exit-s; 3: activity; 4: gain
#                              5: gain pooled for every scored kind (unlabelled too), per-segment gain
#                                 lines, rows_numeric / rows_null / rows_invalid (was rows_with_key)

# ------------------------------------------------------ log reader (dup) --
# TS_RE, SOAK_RE, MARK_RE and detect_format are copied verbatim from
# tools/session_runner.py (its "log reader (dup)" block); keep them
# textually identical (NOTES). TS_RE is also tools/dashboard.py's TS_RE.
TS_RE = re.compile(r"^\s*(\d+\.\d+)(?: (.*))?$")      # = tools/dashboard.py TS_RE
SOAK_RE = re.compile(r"^\s*(\d+\.\d+) (\d\d:\d\d:\d\d\.\d{3}) (.*)$")
MARK_RE = re.compile(r"^# (RUN|PLAN|SEGMENT|CUE|SENT|PORT|BOARD|WARN|END) (.*)$")


def detect_format(lines):
    """'soak' when >= 90 % of the first 200 timestamped lines carry a
    HH:MM:SS.mmm wall-clock field, else 'plain'."""
    n = soak = 0
    for raw in lines:
        if raw.startswith("#") or not TS_RE.match(raw):
            continue
        n += 1
        soak += 1 if SOAK_RE.match(raw) else 0
        if n >= 200:
            break
    return "soak" if n and soak * 10 >= n * 9 else "plain"


# The port-event lines of the other tools (session_runner.py recognises
# the same events in load_recording; this reader also knows the listener's).
CAPTURE_NOTE_RE = re.compile(r"^#\s*(\d+\.\d+) port (lost|open)\b:?\s*(.*)$")  # serial_capture.py note()
HOST_NOTE_RE = re.compile(r"^(port open|port lost|port unavailable|listener start|listener end)\b:?\s*(.*)$")
WALL_ONLY_RE = re.compile(r"^\d\d:\d\d:\d\d\.\d{3}$")

# ------------------------------------------------- board line patterns --
JSON_TAG = "JSON {"
ESPLOG_RE = re.compile(r"^[IWEDV] \((\d+)\) ")               # "I (12345) tag: ..."
ROM_RST_RE = re.compile(r"^rst:0x[0-9a-fA-F]+ \(([^)]*)\)")  # ESP-ROM reset line
BANNER = " ESP32-C5 Wi-Fi CSI human detector"                # step08 main.c:1047, step10 main.c:4626
STEP10_BANNER = " Step 10: "                                  # step10 main.c:4627
LIVE_RE = re.compile(r"([\d.]+) CSI packets/s live \((calibrated|not measured yet, nominal) ([\d.]+)/s\)")
HEAP_RE = re.compile(r"free heap (\d+) bytes")
PHASE_RE = re.compile(r"CMD status: phase (\S+), ([^,]*),")
PIPE_RE = re.compile(r"^\s+pipeline: slowest packet ([\d.]+) ms since boot, (\d+) CSI packets dropped since boot")
UPB_RE = re.compile(r"^\s+board: up (\d+) s, last reset (\S+) \((-?\d+)\)")
STARTUP_HEAP_RE = re.compile(r"free heap after start-up: (\d+) bytes")
DROP_WARN_RE = re.compile(r"(\d+) CSI packets dropped in the last minute")
# step10 main.c:3776 (PH_SETTLE, sets rchk_warned so 4278 stays silent for
# the same excursion) and main.c:4278 (PH_DETECT, every 10 s)
RATE_WARN_RE = re.compile(r"live CSI rate ([\d.]+)/s (?:is more than 20% off|differs from) the calibrated ([\d.]+)/s")
RATE_BACK_RE = re.compile(r"live CSI rate back to ([\d.]+)/s")
CALIBRATED_RE = re.compile(r" CALIBRATED \(([^)]*)\)")
RESUMED_RE = re.compile(r"console: output resumed - for ~(\d+) s no host read it")
CRASH_RE = re.compile(r"Guru Meditation|abort\(\) was called|Backtrace:|assert failed|"
                      r"Stack smashing|panic'ed|Rebooting\.\.\.")

CAL_STATES = ("leave-room", "warmup", "calib-A", "select", "calib-B", "save")
SCORED_KINDS = ("walk", "empty", "enter", "minor", "still")
UNSCORED_KINDS = ("setup", "calibration")

UPTIME_BACK_MS = 2000      # the board clock going back by more than this = a boot
BOOT_CLUSTER_S = 10.0      # restart evidence this close in host time = one boot
SILENCE_S = 5.0            # no board line for longer than this = a silence
TMS_JUMP_MS = 5000         # consecutive JSON rows further apart = rows lost
PERIOD_SPAN = 4            # row period: median of (t[i+k] - t[i]) / k
APPEND_BACK_S = 0.5        # a host clock this far backwards = an appended session
GRACE_S = 10.0             # session_runner.py NOTES: "--grace (default 10 s)"
EXIT_S = 9.0               # CSI_PRESENCE_HOLD_MS 5000 + CSI_STATIC_SUSTAIN_MS 4000 (defaults:
                           # step08 Kconfig.projbuild:106-114, step10 :122-130; session_runner.py NOTES)
ACTIVITY_KINDS = ("walk", "enter", "minor", "still")   # someone was in the room
# Step 10 "activity" key: the value each segment kind asks for (enter is a
# transition, setup / calibration are not scored: no expectation)
EXPECTED_ACTIVITY = {"empty": "empty", "walk": "moving", "minor": "restless", "still": "still"}
ACTIVITY_VALUES = ("empty", "still", "restless", "moving", "null")   # table columns; "?" when seen
ORIGIN_ACTIVITY = ("no mark: docs/TESTING.md section 4 sets none for the activity state "
                   "(as of 2026-10-16); the expected value is the segment kind's cue "
                   "(empty -> empty, walk -> moving, minor -> restless, still -> still)")

MARK_WALK = 0.001
MARK_EMPTY = 0.0001
MARK_RUN_S = 1800.0
ORIGIN_WALK = ("TGbf pre-standard target sheet 11-20/1712r2, 'miss <= 0.001' per second "
               "(docs/COMPARISON.md section 3; docs/TESTING.md section 4 Phase 1, 1a). IEEE 802.11bf "
               "itself sets no bar")
ORIGIN_EMPTY = ("TGbf pre-standard target sheet 11-20/1712r2, 'false alarm <= 0.0001' per "
                "second (docs/COMPARISON.md section 3; docs/TESTING.md section 4 1d, an 8 h night)")
ORIGIN_DROP = "project mark 2026-09-25 (docs/TESTING.md section 4 Phase 2)"
ORIGIN_HEAP = "project mark (docs/TESTING.md section 4 Phase 2): a cheap leak check"
ORIGIN_RATE = ("project mark 2026-09-25 (docs/TESTING.md section 4 Phase 2): 60 s mean >= 90/s at "
               "a 10 ms ping, and within 20 % of the calibrated rate")
ORIGIN_PROC = ("no mark (docs/TESTING.md section 4 Phase 2): compare with README 'Cost.' estimates, "
               "~6-10 ms per vitals FFT and ~12-32 ms per gait window (README section 'Kconfig - menuconfig -> "
               "Recognise / count / vitals', its 'Cost.' paragraph; "
               "estimates, not measurements)")
ORIGIN_1B = "no bar exists (docs/TESTING.md section 4 1b; COMPARISON section 3): none, record it"
ORIGIN_1C = ("none, record it (docs/TESTING.md section 4 1c); README calls still-person detection "
             "unreliable")
GROUND_TRUTH = ("The schedule is the ground truth only as far as the operator followed it: a "
                "walker who came in late or lingered, a pet or a neighbour is not in the log "
                "(session_runner.py NOTES).")


def num(v):
    """A finite JSON number (not a bool)."""
    return isinstance(v, (int, float)) and not isinstance(v, bool) and v == v \
        and v not in (float("inf"), float("-inf"))


def rnd(x, d=4):
    return None if x is None else round(x, d)


def pct(q, vals):
    """Nearest-rank percentile of a sorted list."""
    if not vals:
        return None
    k = max(0, min(len(vals) - 1, int(-(-q * len(vals) // 1)) - 1))
    return vals[k]


# ----------------------------------------------------------------- read --

class Row:
    __slots__ = ("t", "n", "j", "state", "present")

    def __init__(self, t, n, j):
        self.t, self.n, self.j = t, n, j
        self.state = j.get("state") if isinstance(j.get("state"), str) else "?"
        p = j.get("present")
        self.present = p if (p in (0, 1) and not isinstance(p, bool)) else None


class Parsed:
    def __init__(self):
        self.lines = 0
        self.fmt = "plain"
        self.run = {}                  # # RUN key -> value
        self.plan = []                 # # PLAN rows
        self.markers = 0
        self.bad_markers = []
        self.comments = 0
        self.first_comment = None
        self.seg_marks = []            # (t, n, 'start'|'end', idx, kind, label)
        self.cues = 0
        self.sent = []                 # (t, n, cmd, source)
        self.port = []                 # (t, n, event, detail, source)
        self.host_notes = []           # (t, n, text)
        self.board_ids = []            # (t, n, id, evidence) - # BOARD markers and our own
        self.warns = []                # (t, n, text) - # WARN
        self.end = None                # (t, result)
        self.nboard = 0
        self.board_t = []              # (t, n) of every board line
        self.untimed = 0
        self.first_t = None
        self.last_t = None
        self.appended = []             # (at t, raw t) host clock went backwards
        self.rows = []
        self.unparsable = []           # (t, n)
        self.uptime = []               # (t, n, ms, source)
        self.status = []               # dicts
        self.startup_heap = []         # (t, n, bytes)
        self.banners = []              # (t, n, step)
        self.rom = []                  # (t, n, text)
        self.calibrated = []           # (t, n, err)
        self.recal = []                # (t, n, how)
        self.drop_warns = []           # (t, n, text)
        self.rate_warns = []           # (t, n, text)
        self.crash = []                # (t, n, text)
        self.resumed = []              # (t, n, seconds)
        self._id = None

    # -- helpers
    def identify(self, t, n, text):
        """The same evidence session_runner.py's identify() uses."""
        who = ev = None
        if STEP10_BANNER in text:
            who, ev = "step10", "banner line: " + text.strip()[:80]
        elif text.startswith("CMD "):
            who, ev = "step10", "console reply: " + text[:60]
        elif text.startswith(JSON_TAG):
            if '"proc_ms_max"' in text:
                who, ev = "step10", 'JSON line carries "proc_ms_max"'
            elif ('"state":"detect"' in text and '"count"' not in text
                  and re.search(r'"nsub":\d+\}\s*$', text)):
                who, ev = "step08", 'JSON detect line ends at "nsub" (no Step 10 keys)'
        elif " | jitter " in text and " | wander " in text:
            who, ev = "step09", "esp_wifi_sensing state line"
        if who and who != self._id:
            self._id = who
            self.board_ids.append((t, n, who, ev, "this tool"))

    def board_line(self, t, n, text):
        self.nboard += 1
        self.board_t.append((t, n))
        self.identify(t, n, text)
        k = text.find(JSON_TAG)
        dec = json.JSONDecoder()
        while k >= 0:
            try:
                obj, end = dec.raw_decode(text, k + len(JSON_TAG) - 1)
            except ValueError:
                obj, end = None, k + len(JSON_TAG)
            if isinstance(obj, dict):
                r = Row(t, n, obj)
                self.rows.append(r)
                if num(obj.get("t_ms")):
                    self.uptime.append((t, n, int(obj["t_ms"]), "JSON t_ms"))
            else:
                self.unparsable.append((t, n))
            k = text.find(JSON_TAG, end)
        m = ESPLOG_RE.match(text)
        if m:
            self.uptime.append((t, n, int(m.group(1)), "log timestamp"))
        if "CMD status:" in text:
            st = {"t": t, "n": n, "heap": None, "live": None, "cal": None, "cal_kind": None,
                  "phase": None, "presence": None, "slowest_ms": None, "dropped_boot": None,
                  "up_s": None, "reset": None}
            m = HEAP_RE.search(text)
            if m:
                st["heap"] = int(m.group(1))
            m = LIVE_RE.search(text)
            if m:
                st["live"], st["cal_kind"], st["cal"] = float(m.group(1)), m.group(2), float(m.group(3))
            m = PHASE_RE.search(text)
            if m:
                st["phase"], st["presence"] = m.group(1), m.group(2).strip()
            self.status.append(st)
        m = PIPE_RE.match(text)
        if m and self.status and self.status[-1]["slowest_ms"] is None:
            self.status[-1]["slowest_ms"] = float(m.group(1))
            self.status[-1]["dropped_boot"] = int(m.group(2))
        m = UPB_RE.match(text)
        if m:
            self.uptime.append((t, n, int(m.group(1)) * 1000, "status uptime"))
            if self.status and self.status[-1]["up_s"] is None:
                self.status[-1]["up_s"] = int(m.group(1))
                self.status[-1]["reset"] = "%s (%s)" % (m.group(2), m.group(3))
        m = STARTUP_HEAP_RE.search(text)
        if m:
            self.startup_heap.append((t, n, int(m.group(1))))
        if BANNER in text:
            self.banners.append((t, n, text.strip()[:80]))
        s = text.strip()
        if s.startswith("ESP-ROM:") or ROM_RST_RE.match(s):
            self.rom.append((t, n, s[:80]))
        m = CALIBRATED_RE.search(text)
        if m:
            self.calibrated.append((t, n, m.group(1)))
        if text.startswith("CMD recal:"):
            self.recal.append((t, n, "console 'recal' reply"))
        if DROP_WARN_RE.search(text):
            self.drop_warns.append((t, n, s[:160]))
        if RATE_WARN_RE.search(text) or RATE_BACK_RE.search(text):
            self.rate_warns.append((t, n, s[:160]))
        if CRASH_RE.search(text):
            self.crash.append((t, n, s[:160]))
        m = RESUMED_RE.search(text)
        if m:
            self.resumed.append((t, n, int(m.group(1))))


def read_log(path):
    """Parse a log of any of the three formats into a Parsed."""
    with open(path, encoding="utf-8", errors="replace") as f:
        lines = [ln.rstrip("\r\n") for ln in f]
    L = Parsed()
    L.lines = len(lines)
    L.fmt = detect_format(lines)
    state = {"prev": None, "off": 0.0}

    def when(t_raw):
        t = t_raw + state["off"]
        prev = state["prev"]
        if prev is not None and t < prev:
            if prev - t > APPEND_BACK_S:
                L.appended.append((prev, t_raw))
                state["off"] += prev - t
            t = prev
        state["prev"] = t
        if L.first_t is None:
            L.first_t = t
        L.last_t = t
        return t

    for n, raw in enumerate(lines, 1):
        if raw.startswith("#"):
            m = MARK_RE.match(raw)
            if m:
                L.markers += 1
                tag, rest = m.group(1), m.group(2)
                if tag == "RUN":
                    k, _, v = rest.partition(" ")
                    L.run[k] = v
                    continue
                if tag == "PLAN":
                    p = rest.split(" ")
                    try:
                        L.plan.append({"idx": int(p[0]), "start": float(p[1]), "dur": float(p[2]),
                                       "kind": p[3], "label": " ".join(p[4:])})
                    except (ValueError, IndexError):
                        L.bad_markers.append((n, raw[:100]))
                    continue
                p = rest.split(" ", 1)
                try:
                    t = when(float(p[0]))
                except ValueError:
                    L.bad_markers.append((n, raw[:100]))
                    continue
                body = p[1] if len(p) > 1 else ""
                if tag == "SEGMENT":
                    q = body.split(" ")
                    try:                     # validate first: a bad marker is never used
                        if q[0] not in ("start", "end"):
                            raise ValueError
                        L.seg_marks.append((t, n, q[0], int(q[1]), q[2], " ".join(q[3:])))
                    except (ValueError, IndexError):
                        L.bad_markers.append((n, raw[:100]))
                elif tag == "PORT":
                    ev, _, detail = body.partition(" ")
                    L.port.append((t, n, ev, detail, "# PORT"))
                elif tag == "SENT":
                    L.sent.append((t, n, body, "# SENT"))
                    if body.strip() == "recal":
                        L.recal.append((t, n, "'recal' sent"))
                elif tag == "BOARD":
                    who, _, ev = body.partition(" ")
                    L.board_ids.append((t, n, who, ev, "# BOARD"))
                elif tag == "WARN":
                    L.warns.append((t, n, body))
                elif tag == "CUE":
                    L.cues += 1
                elif tag == "END":
                    L.end = (t, body)
                continue
            m = CAPTURE_NOTE_RE.match(raw)
            if m:
                L.port.append((when(float(m.group(1))), n, m.group(2), m.group(3), "serial_capture"))
                continue
            L.comments += 1
            if L.first_comment is None and raw.strip("# "):
                L.first_comment = (n, raw[:110])
            continue
        m = SOAK_RE.match(raw) if L.fmt == "soak" else None
        if m:
            t, text = when(float(m.group(1))), m.group(3)
            if text.startswith("## "):
                note = text[3:]
                if note.startswith("port lost"):
                    L.port.append((t, n, "lost", note[9:].lstrip(": "), "## soak"))
                elif note.startswith("port open"):
                    L.port.append((t, n, "open", "", "## soak"))
                elif note.startswith("port unavailable"):
                    L.port.append((t, n, "unavailable", note[16:].lstrip(": "), "## soak"))
                elif note.startswith("sent: "):
                    L.sent.append((t, n, note[6:], "## soak"))
                    if note[6:].strip() == "recal":
                        L.recal.append((t, n, "'recal' sent"))
                else:
                    L.host_notes.append((t, n, note))
                continue
        else:
            m = TS_RE.match(raw)
            if m:
                t, text = when(float(m.group(1))), (m.group(2) or "")
                if L.fmt == "soak" and WALL_ONLY_RE.match(text):
                    text = ""
                if text.startswith("[dashboard] "):
                    note = text[12:]
                    for ev in ("port lost", "port open", "port unavailable"):
                        if note.startswith(ev):
                            L.port.append((t, n, ev[5:], note[len(ev):].lstrip(": "), "[dashboard]"))
                            break
                    else:
                        if note.startswith(">>> sent: "):
                            L.sent.append((t, n, note[10:], "[dashboard]"))
                        else:
                            L.host_notes.append((t, n, note))
                    continue
                h = HOST_NOTE_RE.match(text)
                if h:
                    if h.group(1).startswith("port "):
                        L.port.append((t, n, h.group(1)[5:], h.group(2), "listener"))
                    else:
                        L.host_notes.append((t, n, text))
                    continue
            elif raw.strip():
                t = state["prev"] if state["prev"] is not None else 0.0
                text = raw                    # a line without a timestamp
                L.untimed += 1
            else:
                continue
        L.board_line(t, n, text)
    return L


# ------------------------------------------------------------- analysis --

def row_period(det):
    """(p, how, plain median single gap) for consecutive detect rows."""
    if len(det) < 2:
        return None, None, None
    gaps = [b.t - a.t for a, b in zip(det, det[1:])]
    plain = statistics.median(gaps)
    k = min(PERIOD_SPAN, len(det) - 1)
    spans = [(det[i + k].t - det[i].t) / k for i in range(len(det) - k)]
    p = statistics.median(spans)
    if p > 0:
        return p, "median of %d-row spans / %d" % (k, k), plain
    span = det[-1].t - det[0].t
    if span > 0:
        return span / (len(det) - 1), "mean gap (timestamps too coarse for a median)", plain
    return None, None, plain


def runs(det, value):
    """Longest run of consecutive detect rows with present == value."""
    best = cur = 0
    for r in det:
        cur = cur + 1 if r.present == value else 0
        best = max(best, cur)
    return best


def figures(det, other, window_s, p_log):
    n = len(det)
    p, how, plain = row_period(det)
    if p is None and n and p_log:
        p, how = p_log, "the log's (fewer than 2 rows here)"
    n1 = sum(1 for r in det if r.present == 1)
    n0 = n - n1
    T = n * p if (p and n) else None
    onsets = sum(1 for a, b in zip(det, det[1:]) if a.present == 0 and b.present == 1)
    lapses = sum(1 for a, b in zip(det, det[1:]) if a.present == 1 and b.present == 0)
    states = {}
    for r in other:
        states[r.state] = states.get(r.state, 0) + 1
    return {
        "detect_rows": n, "other_rows": states,
        "period_s": rnd(p, 6), "period_how": how, "median_single_gap_s": rnd(plain),
        "scored_s": rnd(T, 3), "window_s": rnd(window_s, 2),
        "coverage": rnd(T / window_s, 3) if (T is not None and window_s and window_s > 0) else None,
        "present_rows": n1, "empty_rows": n0,
        "present_fraction": rnd(n1 / n) if n else None,
        "onsets": onsets, "lapses": lapses,
        "onsets_per_h": rnd(onsets / (T / 3600.0), 2) if T else None,
        "longest_present_s": rnd(runs(det, 1) * p, 2) if p else None,
        "longest_empty_s": rnd(runs(det, 0) * p, 2) if p else None,
        "first_t": rnd(det[0].t, 2) if det else None, "last_t": rnd(det[-1].t, 2) if det else None,
    }


def verdict(rate, res, n, p, T, mark):
    """The verdict for 'seconds reported X per second' <= mark. res = the
    smallest nonzero rate the window can show, p / T (one row)."""
    if not n:
        return "not applicable (no detect rows in the scored window)"
    if rate is None or not p or not T:
        return "not applicable (row period unknown: fewer than 2 rows)"
    if rate > mark:
        return "NOT met"
    if rate == 0 and res > mark:
        return ("not applicable (0 observed, but %.1f s cannot resolve the mark "
                "(needs >= %.0f s))" % (T, p / mark))
    return "met"


def rate_verdict(bad_rows, n, p, T, mark):
    """(rate, resolution, verdict) for one window. With T = n x p the rate
    bad x p / T is bad / n and one row is 1 / n."""
    if not n or not p or not T:
        return None, None, verdict(None, None, n, p, T, mark)
    rate, res = bad_rows / float(n), 1.0 / n
    return rate, res, verdict(rate, res, n, p, T, mark)


def vitals_cover(det):
    out = {}
    have = [r for r in det if "br_bpm" in r.j or "hr_bpm_exp" in r.j]
    if not have:
        return None
    for key in ("br_bpm", "hr_bpm_exp"):
        v = [r.j[key] for r in have if num(r.j.get(key))]
        out[key] = {"rows_with_number": len(v), "rows": len(have),
                    "coverage": rnd(len(v) / len(have), 3),
                    "min": rnd(min(v), 1) if v else None, "max": rnd(max(v), 1) if v else None}
    vs = {}
    for r in have:
        k = r.j.get("vitals") if isinstance(r.j.get("vitals"), str) else "-"
        vs[k] = vs.get(k, 0) + 1
    out["vitals_states"] = vs
    return out


def act_value(r):
    """A row's "activity" as a table column: the string, "null" for null,
    "?" for any other type; None when the row has no such key."""
    if "activity" not in r.j:
        return None
    v = r.j["activity"]
    if v is None:
        return "null"
    return v if isinstance(v, str) else "?"


def activity_figures(det, kind):
    """Counts of each "activity" value in the detect rows and, for a kind
    with an expected value, the match fraction (rows equal to it / rows
    carrying the key)."""
    counts = {}
    without = 0
    for r in det:
        v = act_value(r)
        if v is None:
            without += 1
            continue
        counts[v] = counts.get(v, 0) + 1
    n = sum(counts.values())
    exp = EXPECTED_ACTIVITY.get(kind)
    out = {"expected": exp, "rows_with_key": n, "rows_without_key": without, "counts": counts}
    if exp is not None:
        k = counts.get(exp, 0)
        out.update(match_rows=k, match_fraction=rnd(k / n) if n else None)
    return out


def activity_confusion(segs_scored, col):
    """Expected x reported row counts, pooled over the segments with an
    expected activity, for one column."""
    table, cols = {}, list(ACTIVITY_VALUES)
    for s in segs_scored:
        f = s.get(col, {}).get("activity") if s.get("scored") else None
        if not f or f["expected"] is None:
            continue
        row = table.setdefault(f["expected"], {})
        for v, c in f["counts"].items():
            row[v] = row.get(v, 0) + c
            if v not in cols:
                cols.append(v)
    rows = [e for e in ("empty", "still", "restless", "moving") if e in table]
    n = sum(sum(table[e].values()) for e in rows)
    hit = sum(table[e].get(e, 0) for e in rows)
    return {"column": col, "rows": rows, "columns": cols,
            "table": {e: {c: table[e].get(c, 0) for c in cols} for e in rows},
            "per_expected": {e: {"rows": sum(table[e].values()), "match_rows": table[e].get(e, 0),
                                 "match_fraction": rnd(table[e].get(e, 0) / sum(table[e].values()))
                                 if sum(table[e].values()) else None} for e in rows},
            "total_rows": n, "match_rows": hit, "match_fraction": rnd(hit / n) if n else None}


def gain_count(v):
    """A gain_chg / agc value as the firmware prints it ('%u'): a
    non-negative int. json.loads also yields floats, Infinity and NaN;
    none of those is a count."""
    return isinstance(v, int) and not isinstance(v, bool) and v >= 0


def gain_figures(det):
    """Detect rows whose "gain_chg" is a count: how many changed the gain,
    and the PRESENT fraction with and without a change. Rows with the key
    null, or with any other value, are only counted."""
    keyed = [r for r in det if "gain_chg" in r.j]
    rows = [r for r in keyed if gain_count(r.j["gain_chg"])]
    null = sum(1 for r in keyed if r.j["gain_chg"] is None)
    chg = [r for r in rows if r.j["gain_chg"] > 0]
    same = [r for r in rows if r.j["gain_chg"] == 0]
    agc = [r.j["agc"] for r in rows if gain_count(r.j.get("agc"))]
    return {"rows_numeric": len(rows), "rows_null": null,
            "rows_invalid": len(keyed) - len(rows) - null, "rows_changed": len(chg),
            "packets_changed": int(sum(r.j["gain_chg"] for r in chg)),
            "present_changed": sum(1 for r in chg if r.present == 1),
            "present_unchanged": sum(1 for r in same if r.present == 1),
            "rows_unchanged": len(same),
            "agc_min": min(agc) if agc else None, "agc_max": max(agc) if agc else None}


def gain_pool(segs_scored, kind):
    """gain_figures summed over the scored segments of one kind ('trimmed')."""
    F = [s["trimmed"]["gain"] for s in segs_scored
         if s.get("scored") and s["kind"] == kind and "gain" in s.get("trimmed", {})]
    if not F:
        return None
    out = {k: sum(f[k] for f in F) for k in ("rows_numeric", "rows_null", "rows_invalid",
                                            "rows_changed", "packets_changed",
                                            "present_changed", "present_unchanged", "rows_unchanged")}
    lo = [f["agc_min"] for f in F if f["agc_min"] is not None]
    hi = [f["agc_max"] for f in F if f["agc_max"] is not None]
    out.update(segments=len(F), agc_min=min(lo) if lo else None, agc_max=max(hi) if hi else None,
               present_fraction_changed=rnd(out["present_changed"] / out["rows_changed"])
               if out["rows_changed"] else None,
               present_fraction_unchanged=rnd(out["present_unchanged"] / out["rows_unchanged"])
               if out["rows_unchanged"] else None)
    return out


def cal_episodes(rows):
    """Runs of consecutive calibration-state rows (JSON order)."""
    eps, cur = [], None
    for r in rows:
        if r.state in CAL_STATES:
            if cur is None:
                cur = {"start_t": r.t, "start_n": r.n, "states": []}
            cur["end_t"], cur["end_n"] = r.t, r.n
            if r.state not in cur["states"]:
                cur["states"].append(r.state)
        elif cur is not None:
            eps.append(cur)
            cur = None
    if cur is not None:
        eps.append(cur)
    return eps


def build_segments(L, a, cut_n, cut_t):
    """Segment dicts: idx, kind, label, start, end, n0, n1 (line span)."""
    segs, open_s, notes = [], None, []
    for (t, n, se, idx, kind, label) in L.seg_marks:
        if se == "start":
            if open_s is not None:
                open_s.update(end=t, n1=n, closed="by the next start")
                segs.append(open_s)
                notes.append("segment %d had no end marker; closed at the next start" % open_s["idx"])
            open_s = {"idx": idx, "kind": kind, "label": label, "start": t, "n0": n, "closed": "marker"}
        else:
            if open_s is not None and open_s["idx"] == idx:
                open_s.update(end=t, n1=n)
                segs.append(open_s)
                open_s = None
            else:
                notes.append("an end marker for segment %d without its start (line %d)" % (idx, n))
    if open_s is not None:
        open_s.update(end=L.last_t, n1=L.lines + 1, closed="log end (no end marker)")
        segs.append(open_s)
        notes.append("segment %d has no end marker: scored to the end of the log" % open_s["idx"])
    if segs:
        return segs, notes, True
    start = L.first_t if L.first_t is not None else 0.0
    if a.from_s is not None:
        start = max(start, a.from_s)
    if cut_t is not None and cut_t > start:
        start = cut_t
    end = L.last_t if L.last_t is not None else start
    if a.to_s is not None:
        end = min(end, a.to_s)
    return [{"idx": 1, "kind": "unlabelled", "label": "unlabelled", "start": start, "end": end,
             "n0": (cut_n or 0), "n1": L.lines + 1, "closed": "whole log"}], notes, False


def in_seg(r, s, labelled):
    if not (s["n0"] < r.n < s["n1"]):
        return False
    if labelled:
        return True
    return s["start"] <= r.t <= s["end"]


def after_activity(segs):
    """Tag each empty segment that follows a walk / enter / minor / still
    segment (marker order): it still holds the walker's exit and the
    detector's designed hold (session_runner.py NOTES). The first segment,
    and one after setup / calibration / empty, is 'settled'."""
    prev = None
    for s in segs:
        s["after_activity"] = None
        if s["kind"] == "empty" and prev is not None and prev["kind"] in ACTIVITY_KINDS:
            s["after_activity"] = "#%d %s %s" % (prev["idx"], prev["kind"], prev["label"])
        prev = s


def score_segment(s, rows, L, a, labelled, p_log, cut_n, port, two_cols):
    """Columns: 'trimmed' leaves out the grace (plus --exit-s for an empty
    segment after an activity); 'none_left_out' (when two_cols) leaves out
    nothing."""
    seg_rows = [r for r in rows if in_seg(r, s, labelled) and r.n > cut_n]
    out = {"idx": s["idx"], "kind": s["kind"], "label": s["label"],
           "start": rnd(s["start"], 2), "end": rnd(s["end"], 2),
           "length_s": rnd(s["end"] - s["start"], 2), "closed": s["closed"],
           "after_activity": s.get("after_activity"),
           "rows_before_cutoff": sum(1 for r in rows if in_seg(r, s, labelled) and r.n <= cut_n)}
    if s["kind"] in UNSCORED_KINDS:
        out["scored"] = False
        out["json_rows"] = len(seg_rows)
        return out
    out["scored"] = True
    ex = a.exit_s if s.get("after_activity") else 0.0
    cols = [("trimmed", a.grace, ex)] + ([("none_left_out", 0.0, 0.0)] if two_cols else [])
    for col, g, e in cols:
        w0 = s["start"] + g + e
        win = [r for r in seg_rows if r.t >= w0]
        det = [r for r in win if r.state == "detect" and r.present is not None]
        other = [r for r in win if not (r.state == "detect" and r.present is not None)]
        f = figures(det, other, max(0.0, s["end"] - w0), p_log)
        if s["kind"] == "walk":
            rate, res, v = rate_verdict(f["empty_rows"], f["detect_rows"], f["period_s"],
                                        f["scored_s"], MARK_WALK)
            f.update(empty_s_per_walking_s=rnd(rate, 6), resolution_per_s=rnd(res, 6), verdict=v)
        elif s["kind"] == "empty":
            rate, res, v = rate_verdict(f["present_rows"], f["detect_rows"], f["period_s"],
                                        f["scored_s"], MARK_EMPTY)
            f.update(present_s_per_empty_s=rnd(rate, 6), resolution_per_s=rnd(res, 6), verdict=v)
        if s["kind"] == "still" and col == "trimmed":
            f["vitals"] = vitals_cover(det)
        if a.activity_in_log:
            f["activity"] = activity_figures(det, s["kind"])
        if a.gain_in_log:
            f["gain"] = gain_figures(det)
        f["unparsable_json"] = sum(1 for (t, n) in L.unparsable
                                   if s["n0"] < n < s["n1"] and n > cut_n and t >= w0
                                   and (labelled or s["start"] <= t <= s["end"]))
        f.update(grace_s=rnd(g, 3), exit_s=rnd(e, 3), left_out_s=rnd(g + e, 3))
        out[col] = f
    # latencies from the segment start (the grace does not apply)
    det_all = [r for r in seg_rows if r.state == "detect" and r.present is not None]
    # the state carried in: labelled segments only (an unlabelled window
    # after --after-calibration starts at a calibration, which resets it)
    before = [r for r in rows if r.n < s["n0"] and r.state == "detect" and r.present is not None]
    out["present_before_start"] = before[-1].present if (before and labelled) else None
    out["detect_rows_no_grace"] = len(det_all)
    if s["kind"] in ("walk", "enter", "unlabelled"):
        first = next((r for r in det_all if r.present == 1), None)
        out["first_present_after_s"] = rnd(first.t - s["start"], 2) if first else None
    if s["kind"] == "still":
        first = next((r for r in det_all if r.present == 0), None)
        out["first_lapse_after_s"] = rnd(first.t - s["start"], 2) if first else None
        out["present_at_first_row"] = det_all[0].present if det_all else None
    if s["kind"] == "unlabelled":
        out["calibrations"] = [e for e in cal_episodes(seg_rows)]
    out["port_events"] = [(p["t"], p["event"]) for p in port
                          if s["start"] <= p["t"] <= s["end"]]
    return out


def pool(segs_scored, kind, col, sub=None):
    """Pool one column ('trimmed' / 'none_left_out') of every segment of a
    kind; sub = 'settled' / 'after_activity' pools only those empties."""
    parts = [s[col] for s in segs_scored if s["kind"] == kind and s.get("scored") and col in s
             and (sub is None or (sub == "after_activity") == bool(s.get("after_activity")))]
    if not parts:
        return None
    n = sum(p["detect_rows"] for p in parts)
    T = sum(p["detect_rows"] * p["period_s"] for p in parts if p["period_s"])
    W = sum(p["window_s"] for p in parts if p["window_s"])
    n1 = sum(p["present_rows"] for p in parts)
    n0 = sum(p["empty_rows"] for p in parts)
    bad1 = sum(p["present_rows"] * p["period_s"] for p in parts if p["period_s"])
    bad0 = sum(p["empty_rows"] * p["period_s"] for p in parts if p["period_s"])
    onsets = sum(p["onsets"] for p in parts)
    pp = T / n if (n and T) else None
    out = {"kind": kind, "column": col, "subset": sub or "all", "segments": len(parts),
           "after_activity_segments": sum(1 for s in segs_scored if s["kind"] == kind
                                          and s.get("scored") and col in s
                                          and s.get("after_activity")
                                          and (sub is None or sub == "after_activity")),
           "detect_rows": n, "scored_s": rnd(T, 2), "window_s": rnd(W, 2),
           "coverage": rnd(T / W, 3) if (T and W) else None,
           "period_s": rnd(pp), "present_rows": n1, "empty_rows": n0,
           "present_fraction": rnd(n1 / n) if n else None, "onsets": onsets,
           "onsets_per_h": rnd(onsets / (T / 3600.0), 2) if T else None}
    # pooled: rate = sum(bad rows x p) / sum(T) over the segments; one row
    # of the pool = the mean period / T
    res = pp / T if (pp and T) else None
    if kind == "walk":
        rate = bad0 / T if T else None
        out.update(empty_s_per_walking_s=rnd(rate, 6), resolution_per_s=rnd(res, 6),
                   verdict=verdict(rate, res, n, pp, T, MARK_WALK))
    elif kind == "empty":
        rate = bad1 / T if T else None
        out.update(present_s_per_empty_s=rnd(rate, 6), resolution_per_s=rnd(res, 6),
                   verdict=verdict(rate, res, n, pp, T, MARK_EMPTY))
    return out


def restarts(L, port):
    """Boots seen in the log. Evidence: a banner, an ESP-ROM line, the
    board uptime going back by more than UPTIME_BACK_MS, or back by more
    than 0.5 s to an early-boot value (< 2 s: a restart during the Wi-Fi
    join, as on 2026-09-25, never shows a 2 s drop). One boot = evidence
    within BOOT_CLUSTER_S, but a second banner or a second uptime drop is
    always a new boot, and a ROM line after a banner starts one."""
    ev = []
    for (t, n, text) in L.banners:
        ev.append((t, n, "boot banner", "banner"))
    for (t, n, text) in L.rom:
        ev.append((t, n, "ROM line '%s'" % text[:40], "rom"))
    prev = None
    for (t, n, ms, src) in L.uptime:
        if prev is not None and (ms < prev[2] - UPTIME_BACK_MS
                                 or (ms < 2000 and ms < prev[2] - 500)):
            ev.append((t, n, "board uptime went back %d -> %d ms (%s -> %s)"
                       % (prev[2], ms, prev[3], src), "uptime"))
        prev = (t, n, ms, src)
    ev.sort(key=lambda e: e[1])
    up_n = [n for (t, n, ms, src) in L.uptime]

    def readings_between(n_a, n_b):
        """Is there an uptime reading on a line strictly between n_a and n_b?"""
        i = bisect.bisect_right(up_n, n_a)
        return i < len(up_n) and up_n[i] < n_b

    clusters = []
    for e in ev:
        c = clusters[-1] if clusters else None
        # An uptime drop belongs to the banner's boot when the banner line
        # itself (or the ROM lines before it) carries it; it is a new boot
        # when that boot has already printed later uptimes.
        new = (c is None or e[0] - c["t_last"] > BOOT_CLUSTER_S
               or (e[3] == "banner" and "banner" in c["kinds"])
               or (e[3] == "uptime" and ("uptime" in c["kinds"] or
                                         ("banner" in c["kinds"]
                                          and readings_between(c["banner_n"], e[1]))))
               or (e[3] == "rom" and ("banner" in c["kinds"] or "uptime" in c["kinds"])))
        if new:
            c = {"t": e[0], "n": e[1], "t_last": e[0], "evidence": [], "kinds": [],
                 "banner_n": None}
            clusters.append(c)
        c["evidence"].append(e[2])
        c["t_last"] = e[0]
        c["kinds"].append(e[3])
        if e[3] == "banner" and c["banner_n"] is None:
            c["banner_n"] = e[1]
    out = []
    lost_t = [p["t"] for p in port if p["event"].startswith("lost")]
    for c in clusters:
        # earlier board output: a line more than 1 s (host) before this
        # boot's first evidence - the '=====' line printed with the banner
        # is part of the same boot
        earlier = any(n < c["n"] and t < c["t"] - 1.0 for (t, n) in L.board_t)
        losses = [t for t in lost_t if t <= c["t"] and c["t"] - t <= 30.0]
        # a port that was open and then lost just before the boot: the
        # USB-Serial-JTAG port exists while the chip runs, and a reset that
        # re-enumerates it drops it (not every restart does, TESTING G19), so
        # the chip was running before this boot even if it printed nothing
        opens = [p["t"] for p in port if p["event"] in ("open", "reopened")
                 and losses and p["t"] < losses[-1]]
        opened_before = bool(opens)
        restart = ("uptime" in c["kinds"]) or earlier or opened_before
        out.append({"t": rnd(c["t"], 2), "evidence": c["evidence"],
                    "restart_inside_log": restart,
                    "earlier_board_output": earlier,
                    "after_port_loss": opened_before,
                    "port_open_since_s": rnd(opens[-1], 2) if opens else None,
                    "port_loss_before_s": rnd(c["t"] - losses[-1], 2) if losses else None})
    return out


def port_story(L):
    """Losses and reopens from the port events, in order."""
    state, out, retries, loss_t = None, [], 0, None
    for (t, n, ev, detail, src) in L.port:
        if ev in ("open", "reopened"):
            if state is False:
                out.append({"t": rnd(t, 2), "event": "reopened", "down_s": rnd(t - loss_t, 2)
                            if loss_t is not None else None, "source": src})
            elif state is None:
                out.append({"t": rnd(t, 2), "event": "open", "source": src})
            state = True
        elif ev == "lost" or (ev == "unavailable" and state is True):
            out.append({"t": rnd(t, 2), "event": "lost" if ev == "lost" else "lost (first 'unavailable')",
                        "detail": detail[:100], "source": src})
            state, loss_t = False, t
        elif ev == "unavailable":
            retries += 1
    return out, retries


def continuity(L, cut_n):
    out = {"silences": [], "tms_jumps": [], "resumed": []}
    prev = None
    if L.board_t and L.first_t is not None and L.board_t[0][0] - L.first_t > SILENCE_S:
        out["silences"].append({"from": rnd(L.first_t, 2), "to": rnd(L.board_t[0][0], 2),
                                "s": rnd(L.board_t[0][0] - L.first_t, 2), "from_start": True})
    for (t, n) in L.board_t:
        if prev is not None and t - prev[0] > SILENCE_S:
            out["silences"].append({"from": rnd(prev[0], 2), "to": rnd(t, 2),
                                    "s": rnd(t - prev[0], 2)})
        prev = (t, n)
    if L.last_t is not None and prev is not None and L.last_t - prev[0] > SILENCE_S:
        out["silences"].append({"from": rnd(prev[0], 2), "to": rnd(L.last_t, 2),
                                "s": rnd(L.last_t - prev[0], 2), "to_end": True})
    rows = [r for r in L.rows if num(r.j.get("t_ms"))]
    for a, b in zip(rows, rows[1:]):
        d = b.j["t_ms"] - a.j["t_ms"]
        if d > TMS_JUMP_MS:
            out["tms_jumps"].append({"at": rnd(b.t, 2), "board_s": rnd(d / 1000.0, 2),
                                     "host_s": rnd(b.t - a.t, 2)})
    out["resumed"] = [{"t": rnd(t, 2), "lost_s": s} for (t, n, s) in L.resumed]
    return out


def resources(L, scope):
    det = [r for r in L.rows if r.state == "detect" and scope(r.t, r.n)]
    out = {}
    d = [(r.t, r.j["dropped"]) for r in det if num(r.j.get("dropped"))]
    if d:
        inc = [(rnd(b[0], 2), a[1], b[1]) for a, b in zip(d, d[1:]) if b[1] > a[1]]
        out["dropped"] = {"rows": len(d), "first": d[0][1], "last": d[-1][1],
                          "max": max(v for _, v in d), "increases": len(inc),
                          "first_increases": inc[:5],
                          "span_s": rnd(d[-1][0] - d[0][0], 2)}
    pm = sorted(r.j["proc_ms_max"] for r in det if num(r.j.get("proc_ms_max")))
    if pm:
        at = max((r for r in det if num(r.j.get("proc_ms_max"))), key=lambda r: r.j["proc_ms_max"])
        out["proc_ms_max"] = {"rows": len(pm), "median": rnd(statistics.median(pm), 2),
                              "p95": rnd(pct(0.95, pm), 2), "max": rnd(pm[-1], 2),
                              "max_at": rnd(at.t, 2), "over_10ms_rows": sum(1 for v in pm if v > 10.0)}
    rh = sorted(r.j["rate_hz"] for r in det if num(r.j.get("rate_hz")))
    if rh:
        out["rate_hz"] = {"rows": len(rh), "min": rh[0], "median": rnd(statistics.median(rh), 2),
                          "max": rh[-1]}
    st = [s for s in L.status if scope(s["t"], s["n"])]
    out["status"] = [{k: (rnd(v, 2) if isinstance(v, float) else v) for k, v in s.items()}
                     for s in st]
    heaps = [s["heap"] for s in st if s["heap"] is not None]
    if heaps:
        out["status_heap"] = {"first": heaps[0], "last": heaps[-1], "min": min(heaps), "n": len(heaps)}
    sl = [s["slowest_ms"] for s in st if s["slowest_ms"] is not None]
    if sl:
        out["status_slowest_ms"] = {"max": max(sl), "last": sl[-1]}
    out["status_uptimes_s"] = [s["up_s"] for s in st if s["up_s"] is not None]
    out["startup_heap"] = [{"t": rnd(t, 2), "bytes": b} for (t, n, b) in L.startup_heap if scope(t, n)]
    out["drop_warnings"] = [{"t": rnd(t, 2), "line": x} for (t, n, x) in L.drop_warns if scope(t, n)]
    out["rate_warnings"] = [{"t": rnd(t, 2), "line": x} for (t, n, x) in L.rate_warns if scope(t, n)]
    out["crash_lines"] = [{"t": rnd(t, 2), "line": x} for (t, n, x) in L.crash]
    return out, det


# ---------------------------------------------------------------- marks --

def over(f):
    """' over T of W s (coverage C %)' for a figures or pool dict."""
    if f.get("scored_s") is None:
        return ""
    if f.get("coverage") is None:
        return " over %.1f s" % f["scored_s"]
    return " over %.1f of %.1f s (coverage %.0f %%)" % (f["scored_s"], f["window_s"],
                                                         100 * f["coverage"])


def marks(L, segs, pools, pools0, res, det_rows, boots, cut_n, labelled):
    M = []

    def add(name, value, origin, verdict, note=None):
        m = {"mark": name, "value": value, "origin": origin, "verdict": verdict}
        if note:
            m["note"] = note
        M.append(m)

    for kind, name, origin, key in (
            ("walk", "walking seconds reported empty <= 0.001 per second (1a)", ORIGIN_WALK,
             "empty_s_per_walking_s"),
            ("empty", "empty seconds reported PRESENT <= 0.0001 per second (1d)", ORIGIN_EMPTY,
             "present_s_per_empty_s")):
        ss = [s for s in segs if s["kind"] == kind]
        if not ss:
            why = ("the log has no SEGMENT markers: nobody logged what the room was doing"
                   if not labelled else "no %s segment in this log" % kind)
            add(name, "-", origin, "not applicable (%s)" % why)
            continue
        pl = pools.get(kind)
        p0 = pools0.get(kind) if pools0 else None
        g, ex = pools["_grace"], pools["_exit"]
        na = sum(1 for s in ss if s.get("after_activity"))
        # which pool the verdict comes from, and what was left out of it
        if kind == "empty" and na:
            which = ("pool: all %d empty segment%s, %d settled (from the cue + %g s grace) and %d "
                     "after an activity (from the cue + %g s grace + %g s exit, --exit-s)"
                     % (len(ss), "" if len(ss) == 1 else "s", len(ss) - na, g, na, g, ex))
        else:
            which = "pool: all %d %s segment%s (from the cue + %g s grace)" % (
                len(ss), kind, "" if len(ss) == 1 else "s", g)
        note = None
        if kind == "empty" and na:
            note = ("An empty segment after an activity holds the walker's exit, the 5 s presence "
                    "hold (CSI_PRESENCE_HOLD_MS 5000) and the 4 s static sustain "
                    "(CSI_STATIC_SUSTAIN_MS 4000) (session_runner.py NOTES; Kconfig defaults, step08 "
                    "Kconfig.projbuild:106-114, step10 :122-130): it is scored from the cue + grace + --exit-s "
                    "(%g s here). --exit-s 0 scores it from the cue + grace; a firmware built "
                    "with another hold needs another --exit-s. The 'nothing left out' figure "
                    "includes the walk-in, the exit and the hold." % ex)
        also = ""
        if p0 is not None and p0["detect_rows"]:
            also = "; with nothing left out: %s /s%s (%d rows): %s" % (
                fmt_rate(p0[key]), over(p0), p0["detect_rows"], p0["verdict"])
        subs = ""
        if kind == "empty" and na:
            for sub, label in (("settled", "settled alone"), ("after_activity", "after an activity alone")):
                sp = pools.get("empty_" + sub)
                if sp is None:
                    subs += "; %s: no such segment" % label
                elif not sp["detect_rows"]:
                    subs += "; %s: no detect rows" % label
                else:
                    subs += "; %s: %s /s%s (%d rows): %s" % (label, fmt_rate(sp[key]), over(sp),
                                                             sp["detect_rows"], sp["verdict"])
        if pl is None or not pl["detect_rows"]:
            add(name, "%s; no detect rows in the scored windows%s%s" % (which, subs, also), origin,
                "not applicable (no detect rows in the scored windows)", note)
            continue
        add(name, "%s; %s /s%s (%d detect rows; one row = %s /s)%s%s"
            % (which, fmt_rate(pl[key]), over(pl), pl["detect_rows"],
               fmt_rate(pl["resolution_per_s"]), subs, also), origin, pl["verdict"], note)

    dr = res.get("dropped")
    if not dr:
        add("dropped stays 0 over a 30-min run (Phase 2)", "-", ORIGIN_DROP,
            "not applicable (no \"dropped\" key in the detect rows: not Step 10, or no detect rows)")
    else:
        val = ("max %d, first %d, last %d, %s over %.1f min of rows" %
               (dr["max"], dr["first"], dr["last"],
                "never increased" if not dr["increases"] else "increased %d time(s)" % dr["increases"],
                dr["span_s"] / 60.0))
        # an observed increase fails the 30-min run that contains it, however
        # short the log; only an unresolved 'met' becomes not applicable
        if dr["increases"]:
            v = "NOT met"
        elif dr["span_s"] < MARK_RUN_S:
            v = "not applicable (run of %.1f min is shorter than the mark's 30 min)" % (dr["span_s"] / 60.0)
        else:
            v = "met" if dr["max"] == 0 else "NOT met"
        if boots and any(b["restart_inside_log"] for b in boots):
            val += "; a restart resets the counter (see RESTARTS)"
        add("dropped stays 0 over a 30-min run (Phase 2)", val, ORIGIN_DROP, v)

    st = [s for s in res["status"] if s.get("heap") is not None]
    if len(st) < 2:
        add("free heap from 'status' at the end not below the start (Phase 2)",
            "%d status repl%s with a heap figure" % (len(st), "y" if len(st) == 1 else "ies"),
            ORIGIN_HEAP, "not applicable (needs a 'status' reply at the start and at the end)")
    else:
        a, b = st[0], st[-1]
        val = "start %d B (at %.2f s, up %s s), end %d B (at %.2f s, up %s s), minimum %d B" % (
            a["heap"], a["t"], a["up_s"], b["heap"], b["t"], b["up_s"], min(s["heap"] for s in st))
        back = (a["up_s"] is not None and b["up_s"] is not None and b["up_s"] < a["up_s"])
        boot_between = any(a["t"] < bt["t"] <= b["t"] for bt in boots or [] if bt["restart_inside_log"])
        cals = [e for e in cal_episodes([r for r in L.rows if r.n > cut_n])
                if a["n"] < e["start_n"] < b["n"]]
        if back or boot_between:
            v = "not applicable (the board restarted between the first and the last 'status')"
        else:
            v = "met" if b["heap"] >= a["heap"] else "NOT met"
        if cals:
            after = [s for s in st if s["n"] > cals[-1]["end_n"]]
            val += ("; a calibration ran between them (%.2f-%.2f s), so the start value predates it"
                    % (cals[0]["start_t"], cals[-1]["end_t"]))
            if after:
                val += "; from the first reply after it: %d -> %d B" % (after[0]["heap"], after[-1]["heap"])
        add("free heap from 'status' at the end not below the start (Phase 2)", val, ORIGIN_HEAP, v)

    rh = res.get("rate_hz")
    lives = [s for s in res["status"] if s.get("live") is not None]
    if not rh:
        add("live rate >= 90/s at a 10 ms ping (Phase 2, 2c)", "-", ORIGIN_RATE,
            "not applicable (no rate_hz in the detect rows)")
    elif rh["median"] < 75.0:
        add("live rate >= 90/s at a 10 ms ping (Phase 2, 2c)",
            "rate_hz (calibrated) median %.1f/s; status live %s" %
            (rh["median"], ", ".join("%.1f" % s["live"] for s in lives) or "none"),
            ORIGIN_RATE, "not applicable (rate_hz %.1f/s: not a 10 ms ping; the mark is for ~100/s)"
            % rh["median"])
    elif not lives:
        add("live rate >= 90/s at a 10 ms ping (Phase 2, 2c)",
            "rate_hz (calibrated) median %.1f/s" % rh["median"], ORIGIN_RATE,
            "not applicable (no 'status' reply: the live rate is only in 'status')")
    else:
        mean = sum(s["live"] for s in lives) / len(lives)
        off = [s for s in lives if s["cal"] and abs(s["live"] - s["cal"]) > 0.2 * s["cal"]]
        warns = [w for w in res["rate_warnings"]
                 if "more than 20%" in w["line"] or "differs from the calibrated" in w["line"]]
        val = ("proxy: mean of %d one-second live figures from 'status' = %.1f/s (the mark's 60 s "
               "mean is not in the log); %d of them more than 20 %% off the calibrated rate; "
               "%d firmware rate warning(s) (step10 main.c:3776, 4278); rate_hz median %.1f/s"
               % (len(lives), mean, len(off), len(warns), rh["median"]))
        v = "met" if (mean >= 90.0 and not off and not warns) else "NOT met"
        add("live rate >= 90/s at a 10 ms ping (Phase 2, 2c)", val, ORIGIN_RATE, v)

    pm = res.get("proc_ms_max")
    sl = res.get("status_slowest_ms")
    val = "-"
    if pm:
        val = "median %.2f ms, 95th percentile %.2f ms, max %.2f ms (at %.2f s) over %d rows" % (
            pm["median"], pm["p95"], pm["max"], pm["max_at"], pm["rows"])
    if sl:
        val += "; 'status' slowest packet since boot %.1f ms" % sl["max"]
    add("proc_ms_max", val, ORIGIN_PROC, "no mark")

    for kind, name, origin in (("minor", "1b minor motion: fraction of time PRESENT", ORIGIN_1B),
                               ("still", "1c still person: fraction PRESENT, time to first lapse",
                                ORIGIN_1C)):
        ss = [s for s in segs if s["kind"] == kind and s.get("scored")]
        if not ss:
            continue
        parts = []
        for s in ss:
            f = s.get("trimmed", {})
            if not f.get("detect_rows"):
                parts.append("#%d %s: no detect rows (grace %g s)" % (s["idx"], s["label"],
                                                                      pools["_grace"]))
                continue
            txt = "#%d %s: %s%s" % (s["idx"], s["label"], fmt_frac(f.get("present_fraction")),
                                    over(f))
            if kind == "still":
                fl = s.get("first_lapse_after_s")
                txt += ", first lapse %s" % ("%.2f s after the start" % fl if fl is not None
                                             else "none")
            parts.append(txt)
        add(name, "; ".join(parts), origin, "no mark")
    return M


# --------------------------------------------------------------- report --

def fmt_frac(x):
    return "-" if x is None else "%.3f (%.1f %%)" % (x, 100.0 * x)


def fmt_rate(x):
    if x is None:
        return "-"
    if x == 0:
        return "0"
    return "%.2g" % x if x < 0.01 else "%.4f" % x


def fmt_states(d):
    return ", ".join("%s %d" % (k, v) for k, v in sorted(d.items())) or "none"


def fmt_s(x):
    return "-" if x is None else "%.2f s" % x


class Out:
    def __init__(self):
        self.lines = []

    def __call__(self, s=""):
        self.lines.append(s)


def report(L, a, R):
    o = Out()
    o("%s: %s" % (TOOL, os.path.abspath(a.log)))
    o("  format: %s; %d lines: %d board lines, %d markers, %d other '#' lines%s"
      % (R["format"], L.lines, L.nboard, L.markers, L.comments,
         (", %d untimed" % L.untimed) if L.untimed else ""))
    if L.first_comment:
        o("  first '#' comment (line %d): %s" % L.first_comment)
    o("  host time %s .. %s (%.1f min)" % (fmt_s(L.first_t), fmt_s(L.last_t),
                                          ((L.last_t or 0) - (L.first_t or 0)) / 60.0))
    if L.appended:
        o("  WARNING: the host clock went backwards %d time(s) (appended sessions?); later lines "
          "were shifted to continue: %s" % (len(L.appended), ", ".join(
              "at %.2f s (raw %.2f)" % x for x in L.appended[:5])))
    if L.bad_markers:
        o("  WARNING: %d marker line(s) did not parse, first: line %d %s"
          % (len(L.bad_markers), L.bad_markers[0][0], L.bad_markers[0][1]))
    if L.run:
        o("  run: protocol %s, scale %s, start %s, planned %s s, tool %s"
          % (L.run.get("protocol", "-"), L.run.get("scale", "-"), L.run.get("start_wall", "-"),
             L.run.get("planned_total_s", "-"), L.run.get("tool", "-")))
        o("  port: %s" % L.run.get("port", "-"))
        o("  note: %s" % L.run.get("note", "(none)"))
    if L.end:
        o("  end: %s at %.2f s" % (L.end[1], L.end[0]))
    elif R["labelled"]:
        o("  end: no '# END' marker (the runner did not finish writing the log)")
    ids = []
    for (t, n, who, ev, src) in L.board_ids:
        if not ids or ids[-1][0] != who:
            ids.append((who, "%s at %.2f s (%s)" % (who, t, ev)))
    o("  board: %s" % ("; ".join(x[1] for x in ids) if ids else "not identified"))
    if R["labelled"]:
        o("  " + GROUND_TRUTH)
    else:
        o("  No SEGMENT markers: one 'unlabelled' segment; no pass/fail on the presence marks.")
    o("  options: grace %g s (%s)%s%s%s%s" % (
        a.grace, a.grace_how,
        (", exit %g s after an activity (%s)" % (a.exit_s, a.exit_how)) if R["labelled"] else "",
        ", --after-calibration" if a.after_calibration else "",
        (", --from %g" % a.from_s) if a.from_s is not None else "",
        (", --to %g" % a.to_s) if a.to_s is not None else ""))

    o("")
    o("JSON ROWS")
    st = {}
    for r in L.rows:
        st[r.state] = st.get(r.state, 0) + 1
    o("  %d parsed (%s); %d unparsable%s" % (
        len(L.rows), fmt_states(st), len(L.unparsable),
        (" (first at %.2f s, line %d)" % L.unparsable[0]) if L.unparsable else ""))
    eps = R["calibrations"]
    if eps:
        for e in eps:
            o("  calibration %.2f .. %.2f s (%s)%s" % (
                e["start_t"], e["end_t"], " -> ".join(e["states"]),
                "" if "calib-B" in e["states"] else "  - did not reach calib-B"))
    else:
        o("  no calibration rows in the log")
    for (t, n, err) in L.calibrated:
        o("  'CALIBRATED (%s)' at %.2f s" % (err, t))
    for (t, n, how) in L.recal:
        o("  recalibration asked: %s at %.2f s" % (how, t))
    c = R["cutoff"]
    if a.after_calibration:
        if c["n"]:
            o("  --after-calibration: scoring rows after the last calibration row (%s at %.2f s, "
              "line %d); 'status' replies after it too" % (c["state"], c["t"], c["n"]))
        else:
            o("  --after-calibration: no calibration row in the log, so nothing is cut "
              "(the baseline predates the log)")

    o("")
    two = R["two_columns"]
    left = "grace %g s left out at each start" % a.grace
    if a.exit_s > 0 and any(s.get("after_activity") for s in R["segments"]):
        left += ", plus exit %g s at an empty segment after an activity" % a.exit_s
    o("SEGMENTS (%s; %s)" % (left, "each figure: with that left out | with nothing left out"
                             if two else "one column: nothing left out"))
    for n in R["segment_notes"]:
        o("  NOTE: %s" % n)
    for s in R["segments"]:
        head = "  #%d %s %s  %.2f .. %.2f s (%.2f s)" % (s["idx"], s["kind"], s["label"],
                                                       s["start"], s["end"], s["length_s"])
        if not s["scored"]:
            o(head + ": not scored (%d JSON rows)" % s["json_rows"])
            continue
        o(head)
        if s.get("after_activity") and s["trimmed"]["exit_s"]:
            o("     after an activity (%s): the first column leaves out the %g s grace + %g s "
              "exit (the walker's exit and the designed hold; --exit-s)"
              % (s["after_activity"], s["trimmed"]["grace_s"], s["trimmed"]["exit_s"]))
        elif s.get("after_activity"):
            o("     after an activity (%s): --exit-s 0, so the walker's exit and the designed "
              "hold are scored" % s["after_activity"])
        if s["rows_before_cutoff"]:
            o("     %d rows before the --after-calibration cutoff left out" % s["rows_before_cutoff"])
        F = [s["trimmed"]] + ([s["none_left_out"]] if two else [])

        def line(label, fn):
            o("     %-38s %s" % (label, " | ".join(fn(f) for f in F)))
        line("detect rows", lambda f: str(f["detect_rows"]))
        line("other rows (left out)", lambda f: fmt_states(f["other_rows"]))
        if any(f["unparsable_json"] for f in F):
            line("unparsable JSON lines", lambda f: str(f["unparsable_json"]))
        line("row period (%s)" % (F[0]["period_how"] or "-"),
             lambda f: "-" if f["period_s"] is None else "%.3f s" % f["period_s"])
        line("  plain median single gap", lambda f: "-" if f["median_single_gap_s"] is None
             else "%.3f s" % f["median_single_gap_s"])
        line("scored s = rows x period (coverage)", lambda f: "-" if f["scored_s"] is None else
             "%.1f of %.1f s (%s)" % (f["scored_s"], f["window_s"],
                                      "-" if f["coverage"] is None else "%.0f %%" % (100 * f["coverage"])))
        line("present fraction (PRESENT rows)", lambda f: "%s; %d" % (
            fmt_frac(f["present_fraction"]), f["present_rows"]))
        line("PRESENT onsets (per hour)", lambda f: "%d (%s)" % (
            f["onsets"], "-" if f["onsets_per_h"] is None else "%.1f/h" % f["onsets_per_h"]))
        line("longest run PRESENT / empty", lambda f: "%s / %s" % (fmt_s(f["longest_present_s"]),
                                                                    fmt_s(f["longest_empty_s"])))
        if s["kind"] == "walk":
            line("empty s per walking s (1 row =)", lambda f: "%s (%s)" % (
                fmt_rate(f["empty_s_per_walking_s"]), fmt_rate(f["resolution_per_s"])))
            line("  mark <= 0.001/s (TGbf, 1a)", lambda f: f["verdict"])
        if s["kind"] == "empty":
            line("PRESENT s per empty s (1 row =)", lambda f: "%s (%s)" % (
                fmt_rate(f["present_s_per_empty_s"]), fmt_rate(f["resolution_per_s"])))
            line("  mark <= 0.0001/s (TGbf, 1d)", lambda f: f["verdict"])
        if "activity" in F[0]:
            fa = [f["activity"] for f in F]
            if fa[0]["expected"] is not None:
                line("activity match, expected %s" % fa[0]["expected"], lambda f: "%s (%d of %d)" % (
                    fmt_frac(f["activity"]["match_fraction"]), f["activity"]["match_rows"],
                    f["activity"]["rows_with_key"]))
            line("activity rows (no expected value)" if fa[0]["expected"] is None
                 else "activity rows", lambda f: fmt_states(f["activity"]["counts"])
                 if f["activity"]["counts"] else "no row carries the key")
            if any(x["rows_without_key"] for x in fa):
                line("  detect rows without the key", lambda f: str(f["activity"]["rows_without_key"]))
        if "gain" in F[0]:
            line("gain_chg rows: numeric / null", lambda f: "%d / %d%s" % (
                f["gain"]["rows_numeric"], f["gain"]["rows_null"],
                ", %d invalid" % f["gain"]["rows_invalid"] if f["gain"]["rows_invalid"] else ""))
            line("  gain changed: rows (packets); agc", lambda f: "%d (%d); %s" % (
                f["gain"]["rows_changed"], f["gain"]["packets_changed"],
                "-" if f["gain"]["agc_min"] is None
                else "%g-%g" % (f["gain"]["agc_min"], f["gain"]["agc_max"])))
            line("  PRESENT: changed rows / the rest", lambda f: "%s / %s" % (
                fmt_frac(f["gain"]["present_changed"] / f["gain"]["rows_changed"]
                         if f["gain"]["rows_changed"] else None),
                fmt_frac(f["gain"]["present_unchanged"] / f["gain"]["rows_unchanged"]
                         if f["gain"]["rows_unchanged"] else None)))
        if s.get("present_before_start") is not None:
            o("     last detect row before the start: %s" %
              ("PRESENT (carried in)" if s["present_before_start"] else "empty"))
        none = not s["detect_rows_no_grace"]
        if "first_present_after_s" in s:
            v = s["first_present_after_s"]
            o("     first present=1: %s%s" % (
                "no detect rows in the segment" if none else
                ("%s after the segment start" % fmt_s(v)) if v is not None else "never",
                "" if none else " (includes the walk-in; no grace)"
                if s["kind"] in ("walk", "enter") else " (no grace)"))
        if "first_lapse_after_s" in s:
            v = s["first_lapse_after_s"]
            if none:
                o("     first present=0 (first lapse): no detect rows in the segment")
            else:
                o("     first present=0 (first lapse): %s (no grace); first row %s" % (
                    "none" if v is None else "%s after the segment start" % fmt_s(v),
                    {None: "-", 1: "PRESENT", 0: "empty"}[s["present_at_first_row"]]))
            vit = F[0].get("vitals")
            if not F[0]["detect_rows"]:
                o("     vitals: no detect rows in the scored window")
            elif vit is None:
                o("     vitals: no br_bpm / hr_bpm_exp keys (not Step 10 rows)")
            else:
                for key in ("br_bpm", "hr_bpm_exp"):
                    x = vit[key]
                    o("     %s: a number in %d of %d rows (%.0f %%)%s - coverage only, no accuracy"
                      % (key, x["rows_with_number"], x["rows"], 100 * x["coverage"],
                         (", %.1f .. %.1f" % (x["min"], x["max"])) if x["min"] is not None else ""))
                o("     vitals states: %s" % fmt_states(vit["vitals_states"]))
        if s["kind"] == "minor":
            o("     mark: none (1b: 'none: record it')")
        if s["kind"] == "still":
            o("     mark: none (1c: 'none: record it')")
        if s["kind"] == "unlabelled":
            o("     calibrations in the window: %d" % len(s.get("calibrations", [])))
        if s["port_events"]:
            o("     port events: %s" % ", ".join("%s at %.2f s" % (e, t) for t, e in s["port_events"]))
        if s["kind"] not in SCORED_KINDS and s["kind"] != "unlabelled":
            o("     NOTE: kind '%s' is not in session_runner.py's list; scored without a mark" % s["kind"])

    P, P0 = R["pools"], R["pools_none_left_out"]
    multi = [k for k in SCORED_KINDS if P.get(k) and P[k]["segments"] > 1]
    if P.get("empty") and P["empty"]["after_activity_segments"] and "empty" not in multi:
        multi.append("empty")
    if multi:
        o("")
        o("POOLS (every segment of a kind; %s%s)" % (
            left, "; 'nothing left out' beneath" if two else ""))

        def pool_lines(name, p, p0):
            if p is None:
                return
            o("  %-24s %d segment%s, %d detect rows, %s s scored%s, present fraction %s, onsets %d"
              % (name, p["segments"], "" if p["segments"] == 1 else "s", p["detect_rows"],
                 p["scored_s"], "" if p["coverage"] is None else " of %s s (coverage %.0f %%)"
                 % (p["window_s"], 100 * p["coverage"]), fmt_frac(p["present_fraction"]), p["onsets"]))
            for q, how in ((p, ""), (p0, "nothing left out: ")):
                if q is None or q.get("verdict") is None:
                    continue
                if q["kind"] == "walk":
                    o("        %sempty s per walking s %s (1 row = %s): %s" % (
                        how, fmt_rate(q["empty_s_per_walking_s"]), fmt_rate(q["resolution_per_s"]),
                        q["verdict"]))
                else:
                    o("        %sPRESENT s per empty s %s (1 row = %s), %s onsets/h: %s" % (
                        how, fmt_rate(q["present_s_per_empty_s"]), fmt_rate(q["resolution_per_s"]),
                        "-" if q["onsets_per_h"] is None else "%.1f" % q["onsets_per_h"], q["verdict"]))
        for k in multi:
            split = k == "empty" and P["empty"]["after_activity_segments"]
            pool_lines(k + " (all)" if split else k, P[k], P0.get(k))
            if split:
                pool_lines("empty settled", P.get("empty_settled"), P0.get("empty_settled"))
                pool_lines("empty after an activity", P.get("empty_after_activity"),
                           P0.get("empty_after_activity"))

    A = R.get("activity")
    if A:
        o("")
        o("ACTIVITY (detect rows; expected value from the segment kind: %s; no mark)" % ", ".join(
            "%s -> %s" % kv for kv in EXPECTED_ACTIVITY.items()))
        for col, how in (("trimmed", left), ("none_left_out", "nothing left out")):
            c = A["confusion"].get(col)
            if c is None:
                continue
            if not c["rows"]:
                o("  %s: no row carries the key in a segment with an expected activity" % how)
                continue
            o("  %s: %s of %d rows match (%d)" % (how, fmt_frac(c["match_fraction"]), c["total_rows"],
                                                 c["match_rows"]))
            o("    %-18s %s  %8s" % ("expected \\ seen", " ".join("%8s" % v for v in c["columns"]),
                                      "match"))
            for e in c["rows"]:
                pe = c["per_expected"][e]
                o("    %-18s %s  %8s" % (e, " ".join("%8d" % c["table"][e][v] for v in c["columns"]),
                                          fmt_frac(pe["match_fraction"])))
        o("  " + ORIGIN_ACTIVITY)

    G = R.get("gain")
    if G is not None:
        o("")
        o("GAIN (Step 10 agc / fft / gain_chg, D-176; %s; logging only, no mark)" % left)
        if not any(g["rows_numeric"] for g in G.values()):
            o("  no scored segment has a detect row with a numeric gain_chg")
        for k, g in G.items():
            o("  %-10s %d rows with a numeric gain_chg (%d null%s) in %d segment(s); gain changed in "
              "%d rows (%d packets); PRESENT %s of changed rows, %s of the rest; agc %s" % (
                  k, g["rows_numeric"], g["rows_null"],
                  ", %d invalid" % g["rows_invalid"] if g["rows_invalid"] else "", g["segments"],
                  g["rows_changed"], g["packets_changed"],
                  fmt_frac(g["present_fraction_changed"]), fmt_frac(g["present_fraction_unchanged"]),
                  "-" if g["agc_min"] is None else "%g-%g" % (g["agc_min"], g["agc_max"])))
        o("  the gain bytes are not a documented field (csi_gain.h): check that agc moves at all")

    X = R["resources"]
    o("")
    o("RESOURCES (%s)" % R["scope_text"])
    d = X.get("dropped")
    o("  dropped: %s" % ("no \"dropped\" key in the detect rows" if not d else
                         "max %d, first %d, last %d, %s (%d rows over %.1f min)" % (
                             d["max"], d["first"], d["last"],
                             "never increased" if not d["increases"] else
                             "increased %d time(s), first (t, from, to) %s" % (d["increases"], d["first_increases"][:3]),
                             d["rows"], d["span_s"] / 60.0)))
    pm = X.get("proc_ms_max")
    o("  proc_ms_max: %s" % ("none" if not pm else
                             "median %.2f, 95th percentile %.2f, max %.2f ms (at %.2f s), %d rows > 10 ms, "
                             "%d rows" % (pm["median"], pm["p95"], pm["max"], pm["max_at"],
                                          pm["over_10ms_rows"], pm["rows"])))
    rh = X.get("rate_hz")
    o("  rate_hz (the calibrated rate): %s" % ("none" if not rh else "min %.1f, median %.1f, max %.1f (%d rows)"
                                               % (rh["min"], rh["median"], rh["max"], rh["rows"])))
    if X["status"]:
        o("  'status' replies: %d" % len(X["status"]))
        for s in X["status"]:
            o("    %8.2f s  heap %s B, %s, live %s/s (%s %s/s), slowest %s ms, dropped since boot %s, up %s s, "
              "last reset %s" % (s["t"], s["heap"], s["presence"] or "-", s["live"], s["cal_kind"] or "-",
                                 s["cal"], s["slowest_ms"], s["dropped_boot"], s["up_s"], s["reset"]))
        h = X.get("status_heap")
        if h:
            o("  free heap from 'status': first %d, last %d, minimum %d B (%d replies)"
              % (h["first"], h["last"], h["min"], h["n"]))
    else:
        o("  'status' replies: none")
    for x in X["startup_heap"]:
        o("  'free heap after start-up: %d bytes' at %.2f s" % (x["bytes"], x["t"]))
    o("  firmware warnings: %d 'dropped in the last minute', %d live-rate" %
      (len(X["drop_warnings"]), len(X["rate_warnings"])))
    for w in (X["drop_warnings"] + X["rate_warnings"])[:6]:
        o("    %8.2f s %s" % (w["t"], w["line"]))

    C = R["continuity"]
    o("")
    o("RESTARTS AND CONTINUITY (whole log)")
    boots = R["boots"]
    if not boots:
        up = [u for u in L.uptime]
        o("  no boot banner, no ESP-ROM line, board uptime never went back (by more than %.0f s, "
          "or to an early-boot value) in %d readings (%s .. %s): 0 restarts seen" % (
              UPTIME_BACK_MS / 1000.0, len(up), ("%.1f s" % (up[0][2] / 1000.0)) if up else "-",
              ("%.1f s" % (max(u[2] for u in up) / 1000.0)) if up else "-"))
    else:
        nr = sum(1 for b in boots if b["restart_inside_log"])
        o("  %d boot(s) seen, %d of them after earlier board output, with the uptime going back, "
          "or after a loss of a port that was open (restarts inside the log):" % (len(boots), nr))
        for b in boots:
            loss = ""
            if b["port_loss_before_s"] is not None:
                loss = "; port lost %.2f s before" % b["port_loss_before_s"]
                if b["after_port_loss"]:
                    loss += (" (open since %.2f s: the chip ran before this boot, a restart)"
                             % b["port_open_since_s"])
            o("    %8.2f s  %s%s%s" % (
                b["t"], "; ".join(b["evidence"]),
                "" if b["earlier_board_output"] else "; the first board output in the log", loss))
    ups = X["status_uptimes_s"]
    if ups:
        o("  'status' uptimes: %s s (%s)" % (", ".join(str(u) for u in ups),
                                              "never backwards" if all(b >= a2 for a2, b in zip(ups, ups[1:]))
                                              else "WENT BACKWARDS"))
    ps = R["port"]
    o("  port: %s%s" % ("; ".join("%s at %.2f s%s" % (p["event"], p["t"],
                                                     (" (down %.2f s)" % p["down_s"]) if p.get("down_s") is not None else "")
                                  for p in ps[:12]) or "no port events",
                        (" ... %d events" % len(ps)) if len(ps) > 12 else ""))
    if R["port_retries"]:
        o("        %d further 'unavailable' retries while the port was away" % R["port_retries"])
    for w in L.warns[:8]:
        o("  runner WARN at %.2f s: %s" % (w[0], w[2]))
    sil = C["silences"]
    o("  silences over %.0f s between board lines: %d%s" % (SILENCE_S, len(sil), "" if not sil else ": " + ", ".join(
        "%.2f..%.2f s (%.1f s%s)" % (x["from"], x["to"], x["s"], ", to the end" if x.get("to_end")
                                      else ", from the log start" if x.get("from_start") else "")
        for x in sil[:8])))
    tj = C["tms_jumps"]
    o("  JSON t_ms jumps over %.0f s (rows lost): %d%s" % (TMS_JUMP_MS / 1000.0, len(tj), "" if not tj else ": " + ", ".join(
        "at %.2f s: board +%.1f s, host +%.2f s" % (x["at"], x["board_s"], x["host_s"]) for x in tj[:8])))
    for x in C["resumed"]:
        o("  firmware at %.2f s: console output resumed after ~%d s nobody read it (lines dropped)"
          % (x["t"], x["lost_s"]))
    for x in X["crash_lines"][:6]:
        o("  CRASH? %8.2f s %s" % (x["t"], x["line"]))

    o("")
    o("MARKS (verdict: met / NOT met / no mark / not applicable (reason))")
    for m in R["marks"]:
        o("  %s" % m["mark"])
        o("      value:   %s" % m["value"])
        o("      origin:  %s" % m["origin"])
        if m.get("note"):
            o("      note:    %s" % m["note"])
        o("      verdict: %s" % m["verdict"])
    if R["labelled"]:
        o("")
        o(GROUND_TRUTH)
    return o.lines


# ----------------------------------------------------------------- main --

def main():
    try:
        sys.stdout.reconfigure(errors="replace")   # U+FFFD on a cp1252 console
    except (AttributeError, ValueError):
        pass
    ap = argparse.ArgumentParser(
        description="Score a hardware-session log against docs/TESTING.md section 4 pass marks "
                    "(see the module docstring).")
    ap.add_argument("log", help="session_runner.py, serial_capture.py, dashboard.py --log or soak.py log")
    ap.add_argument("--grace", type=float, default=None, metavar="S",
                    help="seconds left out at each segment start (default %g; for a "
                         "session_runner.py --simulate log run with --scale F, %g x F)"
                         % (GRACE_S, GRACE_S))
    ap.add_argument("--exit-s", dest="exit_s", type=float, default=None, metavar="S",
                    help="further seconds left out of an empty segment that follows a walk, "
                         "enter, minor or still segment: the walker's exit and the designed hold "
                         "(default %g = CSI_PRESENCE_HOLD_MS 5000 + CSI_STATIC_SUSTAIN_MS 4000; "
                         "for a --simulate log run with --scale F, %g x F)" % (EXIT_S, EXIT_S))
    ap.add_argument("--after-calibration", action="store_true",
                    help="score only rows after the last calibration row in the log")
    ap.add_argument("--from", dest="from_s", type=float, metavar="S",
                    help="host-time window start (logs without SEGMENT markers)")
    ap.add_argument("--to", dest="to_s", type=float, metavar="S",
                    help="host-time window end (logs without SEGMENT markers)")
    ap.add_argument("--json", metavar="OUT", help="also write the results as JSON (never overwrites)")
    a = ap.parse_args()
    if a.grace is not None and a.grace < 0:
        ap.error("--grace must be >= 0")
    if a.exit_s is not None and a.exit_s < 0:
        ap.error("--exit-s must be >= 0")
    if a.from_s is not None and a.to_s is not None and a.from_s >= a.to_s:
        ap.error("--from must be below --to")
    if a.json and os.path.exists(a.json):
        ap.error("--json %s exists; nothing is overwritten - choose another name" % a.json)
    if not os.path.isfile(a.log):
        print("session_score.py: cannot read %s: no such file" % a.log, file=sys.stderr)
        return 2
    try:
        L = read_log(a.log)
    except OSError as e:
        print("session_score.py: cannot read %s: %s" % (a.log, e), file=sys.stderr)
        return 2
    if L.first_t is None:
        print("session_score.py: %s has no timestamped line - not a session log" % a.log,
              file=sys.stderr)
        return 2
    labelled = bool(L.seg_marks)
    a.activity_in_log = any("activity" in r.j for r in L.rows)
    a.gain_in_log = any("gain_chg" in r.j for r in L.rows)
    # The defaults shrink with --scale only for a --simulate replay (its
    # "# RUN port simulate <path>", session_runner.py main()), where the
    # recorded board, hold included, is played faster. On a real port the
    # operator's reaction and the firmware's hold do not shrink with it.
    try:
        sc = float(L.run.get("scale", ""))
    except ValueError:
        sc = None
    replay = L.run.get("port", "").startswith("simulate ")
    odd_scale = bool(sc and sc > 0 and sc != 1.0)

    def default_s(given, base, what):
        if given is not None:
            return given, "given"
        if odd_scale and replay:
            return base * sc, "default %g s x the --simulate replay's --scale %g" % (base, sc)
        if odd_scale:
            return base, ("default; not scaled: a live run at --scale %g, and %s does not shrink "
                          "with it" % (sc, what))
        return base, "default"
    a.grace, a.grace_how = default_s(a.grace, GRACE_S, "the operator's reaction")
    a.exit_s, a.exit_how = default_s(a.exit_s, EXIT_S, "the firmware's hold")
    if labelled and (a.from_s is not None or a.to_s is not None):
        ap.error("--from/--to are for logs without SEGMENT markers; this log has segments")

    # --after-calibration cutoff: the last calibration-state row, by file line
    cut_n, cut_t, cut_state = 0, None, None
    if a.after_calibration:
        cal = [r for r in L.rows if r.state in CAL_STATES]
        if cal:
            cut_n, cut_t, cut_state = cal[-1].n, cal[-1].t, cal[-1].state

    def scope(t, n):
        if n <= cut_n:
            return False
        if a.from_s is not None and t < a.from_s:
            return False
        if a.to_s is not None and t > a.to_s:
            return False
        return True

    port, retries = port_story(L)
    segs, seg_notes, _ = build_segments(L, a, cut_n, cut_t)
    det_all = [r for r in L.rows if r.state == "detect" and r.present is not None and scope(r.t, r.n)]
    p_log = row_period(det_all)[0]
    after_activity(segs)
    # a second column (nothing left out) whenever the first leaves anything out
    two_cols = a.grace > 0 or (a.exit_s > 0 and any(s["after_activity"] for s in segs))
    scored = [score_segment(s, L.rows, L, a, labelled, p_log, cut_n, port, two_cols) for s in segs]

    def pools_of(col):
        P = {"_grace": a.grace if col == "trimmed" else 0.0,
             "_exit": a.exit_s if col == "trimmed" else 0.0, "_column": col}
        for k in SCORED_KINDS:
            pl = pool(scored, k, col)
            if pl:
                P[k] = pl
        if P.get("empty"):
            for sub in ("settled", "after_activity"):
                pl = pool(scored, "empty", col, sub)
                if pl:
                    P["empty_" + sub] = pl
        return P
    pools = pools_of("trimmed")
    pools0 = pools_of("none_left_out") if two_cols else {}
    res, det_rows = resources(L, scope)
    boots = restarts(L, port)
    cont = continuity(L, cut_n)
    M = marks(L, scored, pools, pools0, res, det_rows, boots, cut_n, labelled)
    act = None
    if a.activity_in_log:
        act = {"expected_by_kind": EXPECTED_ACTIVITY,
               "confusion": {col: activity_confusion(scored, col)
                             for col in (["trimmed"] + (["none_left_out"] if two_cols else []))}}
        c = act["confusion"]["trimmed"]
        M.append({"mark": "activity state matches the segment's cue (Step 10 'activity')",
                  "value": ("%s of %d rows (%d) in segments with an expected activity"
                            % (fmt_frac(c["match_fraction"]), c["total_rows"], c["match_rows"])
                            if c["total_rows"] else "no row with the key in a segment with an "
                            "expected activity"),
                  "origin": ORIGIN_ACTIVITY, "verdict": "no mark"})
    scope_text = []
    if a.after_calibration:
        scope_text.append("after the last calibration row" if cut_n else "whole log: no calibration row")
    if a.from_s is not None or a.to_s is not None:
        scope_text.append("host time %s .. %s" % ("-" if a.from_s is None else "%g s" % a.from_s,
                                                  "-" if a.to_s is None else "%g s" % a.to_s))
    fmt = ("session_runner.py log (\"%7.2f <line>\" + '#' markers)" if (labelled or L.run) else
           "soak.py log (\"%8.2f HH:MM:SS.mmm <line>\")" if L.fmt == "soak" else
           "plain \"%7.2f <line>\" log (serial_capture.py / dashboard.py --log / listener)")
    R = {
        "tool": TOOL, "log": os.path.abspath(a.log), "format": fmt, "labelled": labelled,
        "options": {"grace": a.grace, "grace_how": a.grace_how,
                    "exit_s": a.exit_s, "exit_how": a.exit_how,
                    "after_calibration": a.after_calibration,
                    "from": a.from_s, "to": a.to_s},
        "columns": {"trimmed": "the grace left out at each segment start, plus exit_s at an "
                              "empty segment that follows an activity",
                    "none_left_out": "nothing left out (present only when 'trimmed' leaves "
                                     "something out)"},
        "two_columns": two_cols,
        "run": L.run, "plan": L.plan,
        "end": None if L.end is None else {"t": rnd(L.end[0], 2), "result": L.end[1]},
        "board": [{"t": rnd(t, 2), "id": who, "evidence": ev, "source": src}
                  for (t, n, who, ev, src) in L.board_ids],
        "rows": {"parsed": len(L.rows), "unparsable": len(L.unparsable),
                 "states": {}, "detect_in_scope": len(det_all)},
        "calibrations": cal_episodes(L.rows),
        "calibrated_lines": [{"t": rnd(t, 2), "result": e} for (t, n, e) in L.calibrated],
        "cutoff": {"n": cut_n, "t": rnd(cut_t, 2), "state": cut_state},
        "segment_notes": seg_notes, "segments": scored,
        "pools": pools, "pools_none_left_out": pools0,
        "resources": res, "scope_text": ", ".join(scope_text) or "whole log",
        "boots": boots, "port": port, "port_retries": retries, "continuity": cont,
        "marks": M,
        "notes": [GROUND_TRUTH if labelled else
                  "no SEGMENT markers: one unlabelled segment, no pass/fail on presence marks"],
    }
    if act is not None:
        R["activity"] = act
    if a.gain_in_log:
        # every kind with a scored segment: an unlabelled log is one
        # 'unlabelled' segment, and a kind outside session_runner.py's list
        # is scored too (without a mark)
        kinds = list(SCORED_KINDS) + ["unlabelled"]
        kinds += [s["kind"] for s in scored if s.get("scored") and s["kind"] not in kinds]
        kinds = list(dict.fromkeys(kinds))
        R["gain"] = {k: g for k, g in ((k, gain_pool(scored, k)) for k in kinds) if g}
    for r in L.rows:
        R["rows"]["states"][r.state] = R["rows"]["states"].get(r.state, 0) + 1
    for ln in report(L, a, R):
        print(ln)
    if a.json:
        try:
            with open(a.json, "x", encoding="utf-8") as f:
                R["written"] = datetime.datetime.now().astimezone().isoformat(timespec="seconds")
                json.dump(R, f, indent=1, default=str)
                f.write("\n")
        except FileExistsError:
            print("session_score.py: --json %s exists; not overwritten" % a.json, file=sys.stderr)
            return 2
        except OSError as e:
            print("session_score.py: cannot write %s: %s" % (a.json, e), file=sys.stderr)
            return 2
        print("")
        print("JSON written: %s" % os.path.abspath(a.json))
    return 0


if __name__ == "__main__":
    sys.exit(main())
