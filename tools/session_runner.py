#!/usr/bin/env python3
"""
session_runner.py - run a scripted hardware session from docs/TESTING.md §4
(Phase 1 presence, Phase 2b Step 10 resources) while logging the board, and
cue an operator who is away from the laptop with beeps and, optionally,
speech.

WHY THIS EXISTS
    The §4 phases are timed activities: "30 min at 50 Hz: 10 min empty,
    10 min walking, 10 min seated" (§4 Phase 2, step 2b), walking passes
    starting from an empty room (§4 Phase 1, 1a), seated small movements
    (1b) and a seated still person (1c). Scoring them needs to know, for
    every serial line, what the room was doing at that moment. This script
    plays the protocol, cues the operator, and writes the schedule, every
    cue and every command it sent into the same log as the board's lines,
    so that tools/session_score.py can score each segment against the §4
    marks. It also refuses to start without --note: the runs logged so far
    lack the placement and who was in the room (docs/TESTING.md §5.1, row
    2026-09-28 13:51-14:07: "placement not recorded on 2026-09-28", "Who was
    in the room during the `recal` and the rest of the run was not
    recorded").

USAGE
    python tools\\session_runner.py --protocol phase2b --port COM13 ^
        --out D:\\ESP32C5_CSI_logs\\2026-10-01_p2b\\p2b.log ^
        --note "board on shelf 1.0 m high, AP on desk 4.2 m away, line of sight; nobody else in the flat; hotspot 'Testing' ch 6"

    python tools\\session_runner.py --protocol phase1 --dry-run
    python tools\\session_runner.py --protocol phase2b --scale 0.02 --note "smoke test" ^
        --simulate D:\\ESP32C5_CSI_logs\\2026-09-28_step10_soak\\soak10_0928.log --out sim.log

    --protocol phase1|phase2b   the built-in schedule (PROTOCOLS below)
    --port COMx        the board (confirm it first: esptool --port COMx chip_id;
                       docs/TESTING.md G8). Not needed with --simulate/--dry-run
    --out FILE         the log. Refused if FILE exists: raw logs are never
                       overwritten. Its folder must exist. Keep it outside
                       the repo (TESTING §2.5)
    --note TEXT        REQUIRED. Board and AP positions and the distance
                       between them, who is in the room, AP and channel
    --scale F          multiply every duration (segments, countdown lead,
                       status interval) by F; e.g. 0.02 for a smoke test
    --lead-in S        a 'setup' segment first, for the operator to leave
                       the room after starting the run (default 20; 0 = none).
                       Not scored
    --calibrate-first N  an empty 'calibration' segment of N s after the
                       lead-in, for a board that calibrates on boot (after
                       erase-flash, or with no baseline in NVS). Not scored.
                       Step 08/10 defaults calibrate for about 50 s:
                       CSI_LOCK_PACKETS 200 (~4 s at 50/s), CSI_CAL_LEAVE_ROOM_S
                       10, CSI_CAL_WARMUP_S 5, CSI_CAL_PHASE_A_S 15,
                       CSI_CAL_PHASE_B_S 15 (step08 Kconfig.projbuild), plus
                       Wi-Fi association; 90 leaves a margin
    --walk-s, --empty-s, --still-s   override segment lengths (phase1: one
                       pass / the empty gap after a pass / the 1c sit;
                       phase2b: the three 10-min blocks)
    --passes N, --start-empty-s, --enter-s, --minor-s, --gap-s   phase1 only
    --status-every S   Step 10: send 'status' every S s (default 300; 0 = only
                       at the start and the end)
    --console          send 'status' even when the board has not identified
                       itself as Step 10 yet (never to a board identified as
                       Step 08 or Step 09)
    --no-console       never send anything
    --speak            also speak each cue (PowerShell System.Speech)
    --quiet            no beeps
    --sound            beep in --simulate too (it is silent by default)
    --echo             print every board line here (default: only cues, port
                       events, CMD replies and a summary every 30 s)
    --simulate FILE    no board: play a recorded log as the port (OFFLINE MODE)
    --dry-run          print the schedule and exit; opens nothing

    Stop early with Ctrl+C: the open segment is closed and the log ends with
    "# END <t> interrupted". A Ctrl+C in the 3 s tail after the last segment
    (the schedule is done; only the reply to the end 'status' is awaited)
    ends it with "# END <t> completed" and exit code 0.

PROTOCOLS (defaults; every duration x --scale)
    phase2b  docs/TESTING.md §4 Phase 2, step 2b: "30 min at 50 Hz: 10 min
             empty, 10 min walking, 10 min seated ... plus --send status at
             the start and the end". Walk, then sit still: that exercises the
             packet on which a gait window, a count block and a vitals FFT
             land together (§4 Phase 2, Equipment).
                 setup     lead-in     20 s
                 empty     empty-2b    600 s
                 walk      walk-2b     600 s
                 still     seated-2b   600 s      (seated, breathing normally)
             total 1820 s. 'status' at the start, every 300 s, at the end.
    phase1   docs/TESTING.md §4 Phase 1, 1a-1c (1d, the 8 h night, and 1e,
             the other firmware, are separate runs):
                 setup     lead-in             20 s
                 empty     empty-start         120 s  (1a starts "from an
                                                       empty, calibrated room")
                 3x walk   walk-tangential-i   60 s   (1a: across the board-AP
                    empty  empty-after-tangential-i 60 s     line)
                 3x walk   walk-radial-i       60 s   (1a: along the line)
                    empty  empty-after-radial-i 60 s
                 enter     enter-1b            20 s   (walk in, sit down)
                 minor     minor-1b            300 s  (1b: seated, small arm
                                                       and hand movements, 5 min)
                 empty     empty-after-1b      120 s
                 enter     enter-1c            20 s   (1c: "walk in (detected)")
                 still     still-1c            600 s  (1c: sit still, breathe
                                                       normally, 10 min)
                 empty     empty-end           120 s
             total 2040 s. Step 08 has no console, so nothing is sent to it.
             The grid walk itself (0.9 m cells, ~1.2 m/s, 2 s stop at each
             cell edge; §4 Phase 1, "The grid") is the walker's job inside
             each walking segment.

    Segment kinds (closed list): setup, calibration, empty, walk, enter,
    minor, still. session_score.py skips setup and calibration.

OUTPUT (the marker grammar; tools/session_score.py depends on it)
    Every board line:   "%7.2f <line>"  - seconds since the port first
        opened, then the line: exactly what serial_capture.py and
        dashboard.py --log write, so "python tools\\dashboard.py --replay
        FILE" plays the log (it skips lines starting with '#').
    Everything else starts with '#' in column 0 (a board line never does:
    it starts with its timestamp). One regex parses them all:
        ^# (RUN|PLAN|SEGMENT|CUE|SENT|PORT|BOARD|WARN|END) (.*)$
    Untimed header lines, written first:
        # RUN <key> <value>          keys: tool, start_wall (ISO 8601, local
                                     UTC offset, = time 0), port, protocol,
                                     scale, argv, note, console, python,
                                     planned_total_s
        # PLAN <idx> <start_s> <dur_s> <kind> <label>   the schedule as
                                     planned, so an interrupted run still
                                     says what was intended
    Timed lines: the first field after the tag is <t> = "%.2f" seconds on
    the SAME clock as the board lines, taken when the event actually fired:
        # SEGMENT <t> start <idx> <kind> <label>
        # SEGMENT <t> end <idx> <kind> <label>
        # CUE <t> <type> <text>      type: leave|walk|sit|over|countdown|info
                                     (info = printed only: nothing changes
                                     for the operator)
        # SENT <t> <cmd>             a console command written to the board
        # PORT <t> <event> <detail>  event: open|lost|reopened|unavailable
        # BOARD <t> <id> <evidence>  id: step10|step08|step09
        # WARN <t> <text>            e.g. no board output for 10 s
        # END <t> completed|interrupted
                                     completed also when Ctrl+C falls in
                                     the 3 s tail after the last segment
    <idx> is 1-based; <kind> is one word from the list above; <label> is one
    token of [a-z0-9-]. The end marker of a segment and the start marker of
    the next carry the same <t>.

THE PORT
    DTR and RTS are set False BEFORE the port is opened and never touched
    afterwards: pulsing them from pyserial on Windows latched the C5's
    USB-Serial-JTAG in reset until RST was pressed (docs/TESTING.md G1,
    DECISIONS D-153). The port can vanish mid-run (a cable pull, or a reset
    that re-enumerates USB; not every restart does, docs/TESTING.md G19):
    the script then writes "# PORT <t> lost ...", retries
    every 0.3 s, writes "# PORT <t> reopened ..." when it is back, and never
    exits on a port error once the session has started (as
    serial_capture.py does since 2026-09-28 and dashboard.py --log does;
    docs/TESTING.md G19). At START-UP, though, a
    port that stays unavailable for 10 s ends the script before the first
    cue: a busy port (G7: another program owns it) never frees itself, and
    an operator who walked away would perform 30 minutes for nothing.
    Only one program can own the port: close idf.py monitor, dashboard.py,
    presence_monitor.py and serial_capture.py first. Commands: only
    'status', and only to a board identified as Step 10 (its banner
    " Step 10: ...", a "CMD ..." reply, or a JSON line with "proc_ms_max",
    which Step 08's detect line never has: step08 main.c:776-780, as of
    2026-09-28 16:20) or with
    --console. A board identified as Step 08 or Step 09 gets nothing.

OFFLINE MODE (--simulate FILE)
    FILE is a recorded log: this script's own output, a "%7.2f <line>" log
    (serial_capture.py, dashboard.py --log) or a soak-style "%8.2f
    HH:MM:SS.mmm <line>" log (the 2026-09-28 soak.py capture). Its board
    lines are fed as the port, at 1/--scale times their recorded pace, on
    the same code path and host clock as a live run, so the output has the
    same format and all markers. Recorded port losses ("# PORT <t> lost",
    "# <t> port lost: ..." / "# <t> port open: COMn" (serial_capture.py),
    "## port lost", "[dashboard] port lost") make the fake port fail until
    the recorded reopen, which exercises the reopen loop. Commands are
    logged as SENT and nothing answers them; CMD replies present in the
    recording are replayed at their recorded times and are not answers to
    these SENT lines. When the recording ends before
    the schedule, the rest of the run is silent. Board timestamps (t_ms)
    keep their recorded spacing while the host clock is compressed, so
    session_score.py measures the row period from host times.

NOTES
    * THE SCHEDULE IS THE GROUND TRUTH, and it is only as true as the
      operator's obedience: someone who walks in late, lingers at the door,
      or a pet or a neighbour, is not seen by this script. Write deviations
      down with their time (the console prints elapsed seconds) and add
      them to the note in docs/TESTING.md §5.1.
    * Cue times in the log are the times the cues actually fired (within
      ~0.1 s of the plan), not the planned ones; the plan is in the # PLAN
      lines. The operator reacts some seconds later: session_score.py
      --grace (default 10 s) leaves the start of every segment out.
    * An empty segment right after an activity also contains the walker's
      exit, the 5 s presence hold (CSI_PRESENCE_HOLD_MS 5000) and the 4 s
      static sustain (CSI_STATIC_SUSTAIN_MS 4000, step08 Kconfig.projbuild);
      60 s per gap leaves ~40 s of settled empty room after a 10 s grace
      and those ~10 s. The pass and gap lengths are this script's choice,
      not a standard's; override them with --walk-s / --empty-s.
    * Beeps and speech run on their own thread: winsound.Beep blocks, and a
      PowerShell start takes about a second, which must not delay the
      reading of the port (a late read bunches the timestamps).
    * The log's time 0 is when the port first opened; start_wall gives
      the wall clock of that instant, for aligning a written log sheet or
      a phone clock. Keep the PC clock synced (Windows: Settings -> Time ->
      Sync now).
    * The baud rate is ignored by the C5's USB-Serial-JTAG console
      (serial_capture.py NOTES).
    * Python 3.8+ standard library; pyserial for a real port only
      (imported when needed, so --dry-run and --simulate run without it).
      The log reader is duplicated in session_score.py on purpose: both
      tools are standalone (the repo's copy-one-file convention).
"""

import argparse
import datetime
import os
import platform
import queue
import re
import subprocess
import sys
import threading
import time

TOOL = "session_runner.py 1"

# ------------------------------------------------------------- defaults --
# Seconds, before --scale. Reasons beside each; docs/TESTING.md §4 is quoted
# where the number comes from it, otherwise the number is this script's.
LEAD_IN_S = 20.0          # time to leave the room after starting the run
COUNTDOWN_S = 10.0        # "in 10 s: ..." before every change of activity
STATUS_EVERY_S = 300.0    # §4 2b asks for start and end; 5-min reads show a trend
TAIL_S = 3.0              # keep reading after the end 'status' for its reply
OPEN_WAIT_S = 10.0        # start-up: a re-enumerating board is back within this
REOPEN_EVERY_S = 0.3      # retry period after a port loss (never gives up)
START_TRIES = 3           # attempts at the start 'status' before giving up
SILENCE_WARN_S = 10.0     # no byte for this long while open -> # WARN (G18)
PARTIAL_LINE_S = 1.0      # hand over a line with no newline after 1 s (a
                          # reset mid-line; as dashboard.py does)
SUMMARY_EVERY_S = 30.0    # console summary period (real seconds)

# phase2b - docs/TESTING.md §4 Phase 2, 2b: "10 min empty, 10 min walking,
# 10 min seated"
P2B_EMPTY_S = 600.0
P2B_WALK_S = 600.0
P2B_SEATED_S = 600.0

# phase1 - docs/TESTING.md §4 Phase 1
P1_PASSES = 3             # 1a: ">= 3 tangential + 3 radial passes"
P1_WALK_S = 60.0          # one pass: a 6 m side of 0.9 m cells at 1.2 m/s with
                          # a 2 s stop per cell edge (§4 "The grid") is ~20 s
                          # per line; 60 s covers entry plus a few lines
P1_EMPTY_S = 60.0         # after each pass: exit + 5 s hold + 4 s static
                          # sustain (step08 Kconfig) + ~40 s of settled room
P1_START_EMPTY_S = 120.0  # 1a starts "from an empty, calibrated room"
P1_ENTER_S = 20.0         # 1b/1c: walk in, get detected, sit down
P1_MINOR_S = 300.0        # 1b: "seated, 5 min of small arm and hand movements"
P1_STILL_S = 600.0        # 1c: "sit still, breathe normally, 10 min"
P1_GAP_S = 120.0          # empty room after 1b and after 1c

KINDS = ("setup", "calibration", "empty", "walk", "enter", "minor", "still")
OUTSIDE = ("setup", "calibration", "empty")      # the operator is out of the room
SOUND_OF = {"setup": "leave", "calibration": "leave", "empty": "leave",
            "walk": "walk", "enter": "walk", "minor": "sit", "still": "sit"}
SHORT = {"setup": "leave the room", "calibration": "stay out",
         "empty": "leave the room", "walk": "start walking",
         "enter": "walk in and sit down", "minor": "small arm and hand movements",
         "still": "sit still and breathe"}

# Beep patterns: (Hz, ms) tuples. Rising = go, falling = leave, two low =
# sit, a long run = over, one short tick pair = countdown.
PATTERNS = {
    "walk":      ((880, 150), (1175, 150), (1568, 350)),
    "leave":     ((1568, 150), (1175, 150), (880, 350)),
    "sit":       ((523, 450), (523, 450)),
    "over":      ((1047, 180), (1319, 180), (1568, 180), (2093, 700)),
    "countdown": ((1000, 70), (1000, 70)),
}

# ------------------------------------------------------ log reader (dup) --
# TS_RE, SOAK_RE, MARK_RE and detect_format are textually identical to
# session_score.py's copies (duplicated on purpose, see NOTES).
# load_recording recognises the port events of this script, serial_capture.py
# ("# <t> port lost: ..."), soak.py ("## port lost") and dashboard.py --log
# ("[dashboard] port lost"), and continues an appended session after the
# previous one as session_score.py's read_log does. read_log also knows the
# listener's "port lost/open" lines (its HOST_NOTE_RE) and blank soak lines
# (its WALL_ONLY_RE); this reader does not. Keep the two in step.
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


def load_recording(path):
    """[(t, what, text)] with what = 'line' | 'lost' | 'open', in file
    order. t never goes backwards: a timestamp more than 0.5 s back (an
    appended session) continues after the previous one, as session_score.py
    does; a smaller step back is held at the previous t."""
    with open(path, encoding="utf-8", errors="replace") as f:
        lines = [ln.rstrip("\r\n") for ln in f]
    fmt = detect_format(lines)
    items = []
    state = {"prev": 0.0, "off": 0.0}

    def when(t_raw):                  # = session_score.py read_log when()
        t = t_raw + state["off"]
        if t < state["prev"]:
            if state["prev"] - t > 0.5:          # an appended session
                state["off"] += state["prev"] - t
            t = state["prev"]
        state["prev"] = t
        return t

    for raw in lines:
        if raw.startswith("#"):
            m = MARK_RE.match(raw)
            if m and m.group(1) == "PORT":
                p = m.group(2).split(" ", 2)
                if len(p) >= 2:
                    ev = {"lost": "lost", "open": "open", "reopened": "open"}.get(p[1])
                    if ev:
                        items.append((when(float(p[0])), ev, ""))
            elif not m:
                # serial_capture.py note(); = session_score.py CAPTURE_NOTE_RE
                c = re.match(r"^#\s*(\d+\.\d+) port (lost|open)\b", raw)
                if c:
                    items.append((when(float(c.group(1))), c.group(2), ""))
            continue
        m = SOAK_RE.match(raw) if fmt == "soak" else None
        if m:
            t, text = when(float(m.group(1))), m.group(3)
            if text.startswith("## "):
                ev = ("lost" if text.startswith("## port lost") else
                      "open" if text.startswith("## port open") else None)
                if ev:
                    items.append((t, ev, ""))
                continue
        else:
            m = TS_RE.match(raw)
            if m:
                t, text = when(float(m.group(1))), (m.group(2) or "")
                if text.startswith("[dashboard] "):
                    ev = ("lost" if "port lost" in text else
                          "open" if "port open" in text else None)
                    if ev:
                        items.append((t, ev, ""))
                    continue
            elif raw.strip():
                t, text = state["prev"], raw          # a line without a timestamp
            else:
                continue
        items.append((t, "line", text))
    return items, fmt


class SimBoard:
    """A recorded log as the port, played at `speed` x its recorded pace.
    Shared by every handle, so a 'reopen' continues where the loss was."""

    def __init__(self, path, speed):
        self.items, self.fmt = load_recording(path)
        self.speed = speed
        self.i = 0
        self.t_first = self.items[0][0] if self.items else 0.0
        self.t0 = None
        self.down_until = None          # log time the port comes back

    def log_now(self):
        if self.t0 is None:
            self.t0 = time.perf_counter()
        return self.t_first + (time.perf_counter() - self.t0) * self.speed

    def open(self):
        now = self.log_now()
        if self.down_until is not None:
            if now < self.down_until:
                raise OSError("simulated: port absent (recorded port loss)")
            self.down_until = None
        return SimHandle(self)

    def take(self, timeout):
        """Due board lines as bytes; raises OSError at a recorded loss."""
        deadline = time.perf_counter() + timeout
        while True:
            now = self.log_now()
            out = []
            while self.i < len(self.items) and self.items[self.i][0] <= now:
                t, what, text = self.items[self.i]
                self.i += 1
                if what == "line":
                    out.append(text)
                elif what == "lost":
                    nxt = [x[0] for x in self.items[self.i:] if x[1] == "open"]
                    self.down_until = nxt[0] if nxt else t + 1.0
                    if out:           # deliver what came before the loss first
                        self.i -= 1
                        break
                    raise OSError("simulated port loss (recorded at %.2f s)" % t)
            if out:
                return ("\n".join(out) + "\n").encode("utf-8")
            if time.perf_counter() >= deadline:
                return b""
            if self.i < len(self.items):
                wait = (self.items[self.i][0] - now) / self.speed
            else:
                wait = timeout
            time.sleep(max(0.001, min(wait, deadline - time.perf_counter(), 0.05)))


class SimHandle:
    """The subset of serial.Serial the runner uses."""
    timeout = 0.1
    in_waiting = 0

    def __init__(self, board):
        self.board = board
        self.closed = False

    def read(self, _n):
        if self.closed:
            raise OSError("simulated handle closed")
        return self.board.take(self.timeout)

    def write(self, data):
        if self.board.down_until is not None:
            raise OSError("simulated: port absent")
        return len(data)

    def flush(self):
        pass

    def close(self):
        self.closed = True


def live_opener(port):
    import serial                     # only a real port needs pyserial

    def opener():
        s = serial.Serial()
        s.port, s.baudrate, s.timeout = port, 115200, 0.1
        # A write that cannot complete raises instead of blocking for ever
        # (send() then treats it as a port loss). esptool hit "Write
        # timeout" on this board on 2026-09-28 (docs/TESTING.md G20).
        s.write_timeout = 2.0
        # BEFORE open(), and never again: see THE PORT (TESTING G1).
        s.dtr = False
        s.rts = False
        s.open()
        return s
    return opener


# ------------------------------------------------------------ schedule --

def say_dur(s):
    if s >= 60 and abs(s / 60.0 - round(s / 60.0)) < 0.01:
        m = int(round(s / 60.0))
        return "%d minute%s" % (m, "" if m == 1 else "s")
    if s >= 10:
        return "%d seconds" % round(s)
    return "%.1f seconds" % s


def protocol_segments(a):
    """[(kind, label, seconds, cue text with {dur})] before scaling."""
    if a.protocol == "phase2b":
        return [
            ("empty", "empty-2b", a.empty_s or P2B_EMPTY_S,
             "Leave the room and stay out for {dur}."),
            ("walk", "walk-2b", a.walk_s or P2B_WALK_S,
             "Walk in and keep walking around the room for {dur}."),
            ("still", "seated-2b", a.still_s or P2B_SEATED_S,
             "Sit down, stay still and breathe normally for {dur}."),
        ]
    n = a.passes or P1_PASSES
    walk, gap = a.walk_s or P1_WALK_S, a.empty_s or P1_EMPTY_S
    segs = [("empty", "empty-start", a.start_empty_s or P1_START_EMPTY_S,
             "Leave the room and stay out: empty room for {dur}.")]
    for d, how in (("tangential", "across the line from board to access point"),
                   ("radial", "along the line from board to access point")):
        for i in range(1, n + 1):
            segs.append(("walk", "walk-%s-%d" % (d, i), walk,
                         "Start walking: %s pass %d of %d, %s, for {dur}." % (d, i, n, how)))
            segs.append(("empty", "empty-after-%s-%d" % (d, i), gap,
                         "Leave the room and stay out for {dur}."))
    gap2 = a.gap_s or P1_GAP_S
    enter = a.enter_s or P1_ENTER_S
    segs += [
        ("enter", "enter-1b", enter, "Walk in and sit down."),
        ("minor", "minor-1b", a.minor_s or P1_MINOR_S,
         "Stay seated: small arm and hand movements for {dur}."),
        ("empty", "empty-after-1b", gap2, "Leave the room and stay out for {dur}."),
        ("enter", "enter-1c", enter, "Walk in and sit down."),
        ("still", "still-1c", a.still_s or P1_STILL_S,
         "Sit still and breathe normally for {dur}."),
        ("empty", "empty-end", gap2, "Leave the room and stay out for {dur}."),
    ]
    return segs


def build_schedule(a):
    """(segments, total_s, events). Every duration is multiplied by
    --scale. A segment is a dict; events are (t, order, what, arg)."""
    body = []
    if a.lead_in > 0:
        body.append(("setup", "lead-in", a.lead_in,
                     "Leave the room now. The first segment starts in {dur}."))
    if a.calibrate_first > 0:
        body.append(("calibration", "calibration", a.calibrate_first,
                     "Stay out of the room: the board calibrates for {dur}."))
    body += protocol_segments(a)
    segs, t = [], 0.0
    for i, (kind, label, dur, cue) in enumerate(body, 1):
        dur *= a.scale
        segs.append({"idx": i, "kind": kind, "label": label, "start": t,
                     "dur": dur, "cue": cue.format(dur=say_dur(dur))})
        t += dur
    total = t
    for i, s in enumerate(segs):
        prev = segs[i - 1]["kind"] if i else None
        s["sound"] = ("info" if prev in OUTSIDE and s["kind"] in OUTSIDE
                      else SOUND_OF[s["kind"]])

    ev = []
    for i, s in enumerate(segs):
        if i:
            ev.append((s["start"], 0, "seg_end", i - 1))
        ev.append((s["start"], 1, "seg_start", i))
        if i and s["sound"] != "info":
            lead = min(COUNTDOWN_S * a.scale, segs[i - 1]["dur"] / 2.0)
            ev.append((s["start"] - lead, 2, "countdown", (i, lead)))
    if segs:
        lead = min(COUNTDOWN_S * a.scale, segs[-1]["dur"] / 2.0)
        ev.append((total - lead, 2, "countdown", (None, lead)))
        ev.append((total, 0, "seg_end", len(segs) - 1))
    every = a.status_every * a.scale
    if every > 0:
        k = 1
        while k * every < total - 1e-6:
            ev.append((k * every, 3, "status", "periodic"))
            k += 1
    ev.append((total, 9, "end", None))
    ev.sort(key=lambda e: (e[0], e[1]))
    return segs, total, ev


def mmss(t):
    return "%d:%05.2f" % (int(t // 60), t - 60 * int(t // 60))


def print_schedule(a, segs, total, events):
    print("protocol %s, scale %g: %d segments, total %.2f s (%s)"
          % (a.protocol, a.scale, len(segs), total, mmss(total)))
    print("  #  kind         label                          start      dur_s  cue")
    for s in segs:
        print("%3d  %-11s  %-28s %9s %9.2f  [%s] %s"
              % (s["idx"], s["kind"], s["label"], mmss(s["start"]), s["dur"],
                 s["sound"], s["cue"]))
    cds = ["%.2f" % e[0] for e in events if e[2] == "countdown"]
    sts = ["%.2f" % e[0] for e in events if e[2] == "status"]
    print("countdown cues at (s): %s" % (", ".join(cds) or "none"))
    if a.no_console:
        print("'status': never (--no-console)")
    else:
        print("'status' (Step 10 only%s): at the start (once the board is "
              "identified), at %s, and at the end (%.2f s)"
              % (", or --console" if not a.console else "; --console given",
                 ", ".join(sts) or "no periodic times", total))
    kinds = {}
    for s in segs:
        kinds[s["kind"]] = kinds.get(s["kind"], 0.0) + s["dur"]
    print("time per kind: " + ", ".join("%s %.2f s" % (k, v) for k, v in kinds.items()))
    print("the log ends about %.0f s after the last segment (reply to the end 'status')"
          % TAIL_S)


# ---------------------------------------------------------------- cues --

class Announcer:
    """Beeps (winsound) and speech (PowerShell System.Speech) on a daemon
    thread, so neither ever delays the port reader. A backlog of more than
    two cues drops the oldest: a stale cue is worse than none."""

    def __init__(self, beep, speak):
        self.beep, self.speak = beep, speak
        self.q = queue.Queue(maxsize=2)
        self.winsound = None
        if beep:
            try:
                import winsound
                self.winsound = winsound
            except ImportError:           # not Windows: the terminal bell
                self.winsound = None
        if beep or speak:
            threading.Thread(target=self._run, daemon=True).start()

    def cue(self, sound, text):
        if not (self.beep or self.speak) or sound == "info":
            return
        while True:
            try:
                self.q.put_nowait((sound, text))
                return
            except queue.Full:
                try:
                    self.q.get_nowait()
                except queue.Empty:
                    pass

    def _run(self):
        while True:
            sound, text = self.q.get()
            if self.speak:
                self._say(text)
            if self.beep:
                try:
                    if self.winsound:
                        for hz, ms in PATTERNS.get(sound, ()):
                            self.winsound.Beep(hz, ms)
                    else:
                        sys.stdout.write("\a")
                        sys.stdout.flush()
                except Exception:
                    pass

    @staticmethod
    def _say(text):
        """Starts PowerShell and returns at once; it speaks ~1 s later."""
        ps = ("Add-Type -AssemblyName System.Speech; "
              "(New-Object System.Speech.Synthesis.SpeechSynthesizer).Speak('%s')"
              % text.replace("'", "''"))
        flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
        try:
            subprocess.Popen(["powershell", "-NoProfile", "-NonInteractive",
                              "-Command", ps], stdin=subprocess.DEVNULL,
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                             creationflags=flags)
        except Exception:
            pass


# -------------------------------------------------------------- session --

class Session:
    def __init__(self, a, segs, total, events, opener, port_name, fh, announcer):
        self.a, self.segs, self.total, self.events = a, segs, total, events
        self.opener, self.port_name, self.fh, self.ann = opener, port_name, fh, announcer
        self.t0 = None
        self.ser = None
        self.buf = b""
        self.last_rx = None
        self.silent_warned = False
        self.next_try = 0.0
        self.last_unavail = ("", -1e9)
        self.board = None
        self.pending_start_status = not a.no_console
        self.start_fails = 0            # failed writes of the start 'status'
        self.open_seg = None
        self.boundary_t = None          # <t> of the last seg_end (see fire)
        self.lines = 0
        self.json_rows = 0
        self.last_present = None
        self.last_state = None
        self.cur_seg = None
        self.in_cmd_block = False

    # -- output
    def rel(self):
        return time.perf_counter() - self.t0

    def mark(self, tag, text, echo=True, t=None):
        line = "# %s %.2f %s" % (tag, self.rel() if t is None else t, text)
        self.fh.write(line + "\n")
        if echo:
            print(line, flush=True)

    def board_line(self, text):
        self.fh.write("%7.2f %s\n" % (self.rel(), text))
        self.lines += 1
        self.identify(text)
        if text.startswith("JSON {"):
            self.json_rows += 1
            m = re.search(r'"state":"([^"]*)","present":(\d)', text)
            if m:
                self.last_state, self.last_present = m.group(1), int(m.group(2))
        if self.a.echo:
            print("  " + text, flush=True)
        elif text.startswith("CMD "):
            self.in_cmd_block = True
            print("  " + text, flush=True)
        elif self.in_cmd_block and text.startswith("    "):
            print("  " + text, flush=True)
        else:
            self.in_cmd_block = False

    def identify(self, text):
        who = ev = None
        if " Step 10: " in text:
            who, ev = "step10", "banner line: " + text.strip()[:80]
        elif text.startswith("CMD "):
            who, ev = "step10", "console reply: " + text[:60]
        elif text.startswith("JSON {"):
            if '"proc_ms_max"' in text:
                who, ev = "step10", 'JSON line carries "proc_ms_max"'
            elif ('"state":"detect"' in text and '"count"' not in text
                  and re.search(r'"nsub":\d+\}\s*$', text)):
                who, ev = "step08", 'JSON detect line ends at "nsub" (no Step 10 keys)'
        elif " | jitter " in text and " | wander " in text:
            who, ev = "step09", "esp_wifi_sensing state line"
        if who and who != self.board:
            self.board = who
            self.mark("BOARD", "%s %s" % (who, ev))

    # -- port
    def may_send(self):
        if self.a.no_console or self.ser is None:
            return False
        if self.board == "step10":
            return True
        if self.board in ("step08", "step09"):
            return False
        return self.a.console

    def send(self, cmd, why):
        """True when the command was written, False when the port failed."""
        try:
            self.ser.write((cmd + "\n").encode("ascii"))
            self.ser.flush()
        except Exception as e:
            self.port_lost(e)
            self.mark("WARN", "'%s' (%s) not sent: the port failed while writing" % (cmd, why))
            return False
        self.mark("SENT", cmd, echo=False)
        print("# SENT %.2f %s   (%s)" % (self.rel(), cmd, why), flush=True)
        return True

    def flush_partial(self):
        if self.buf:
            self.board_line(self.buf.decode("utf-8", "replace").rstrip("\r"))
            self.buf = b""

    def port_lost(self, e):
        self.flush_partial()
        self.mark("PORT", "lost %s - retrying every %.1f s (port gone or write failed; TESTING G19)"
                  % (str(e)[:120].replace("\n", " "), REOPEN_EVERY_S))
        try:
            self.ser.close()
        except Exception:
            pass
        self.ser = None
        self.next_try = self.rel() + REOPEN_EVERY_S

    def try_reopen(self):
        try:
            self.ser = self.opener()
        except Exception as e:
            self.next_try = self.rel() + REOPEN_EVERY_S
            text = str(e)[:120].replace("\n", " ")
            last, when = self.last_unavail
            if text != last or self.rel() - when > 10.0:
                self.last_unavail = (text, self.rel())
                self.mark("PORT", "unavailable %s" % text)
            return
        self.last_unavail = ("", -1e9)
        self.last_rx = time.perf_counter()
        self.mark("PORT", "reopened %s" % self.port_name)

    # -- schedule
    def fire(self, what, arg):
        if what == "seg_end":
            # A boundary's end and start marks carry one <t>, taken here:
            # seg_end and the next seg_start share a scheduled time and are
            # adjacent in the sorted events, so nothing is written between
            # them and marker times still never go backwards.
            s = self.segs[arg]
            self.boundary_t = self.rel()
            self.mark("SEGMENT", "end %d %s %s" % (s["idx"], s["kind"], s["label"]),
                      echo=False, t=self.boundary_t)
            self.open_seg = None
        elif what == "seg_start":
            s = self.segs[arg]
            t = self.boundary_t if self.boundary_t is not None else self.rel()
            self.boundary_t = None
            self.mark("SEGMENT", "start %d %s %s" % (s["idx"], s["kind"], s["label"]),
                      echo=False, t=t)
            self.open_seg = s
            self.cur_seg = s
            self.cue(s["sound"], s["cue"], banner="segment %d/%d %s %s (%s)"
                     % (s["idx"], len(self.segs), s["kind"], s["label"], say_dur(s["dur"])))
        elif what == "countdown":
            i, lead = arg
            nxt = SHORT[self.segs[i]["kind"]] if i is not None else "session over"
            self.cue("countdown", "In %s: %s." % (say_dur(lead), nxt))
        elif what == "status":
            self.status(arg)
        elif what == "end":
            self.cue("over", "Session over. You can come back in.",
                     banner="all %d segments done" % len(self.segs))
            self.status("end")

    def cue(self, sound, text, banner=None):
        self.mark("CUE", "%s %s" % (sound, text), echo=False)
        if banner is None:
            print("# CUE %.2f %s %s" % (self.rel(), sound, text), flush=True)
        else:
            bar = "=" * 72
            print("%s\n[%8.2f s] %s\n    %s\n%s" % (bar, self.rel(), banner, text.upper()
                                                   if sound != "info" else text, bar), flush=True)
        self.ann.cue(sound, text)

    def status(self, why):
        if self.a.no_console:
            return
        if self.ser is None:
            if self.board == "step10" or self.a.console:
                self.mark("WARN", "'status' (%s) not sent: the port is closed" % why)
            return
        if self.may_send():
            # A failed start write stays pending and is retried after the
            # reopen (TESTING §4 2b's leak check needs the start value),
            # but only START_TRIES times: a board that keeps refusing input
            # (each try can block for write_timeout) must not starve the
            # reading of the port for the whole run.
            ok = self.send("status", why)
            if why == "start":
                if ok:
                    self.pending_start_status = False
                else:
                    self.start_fails += 1
                    if self.start_fails >= START_TRIES:
                        self.pending_start_status = False
                        self.mark("WARN", "'status' (start) failed %d times; not retried. "
                                  "The heap leak check will lack its start value"
                                  % self.start_fails)

    def summary(self):
        s = self.cur_seg
        left = (s["start"] + s["dur"] - self.rel()) if s else 0.0
        pres = {None: "-", 0: "empty", 1: "PRESENT"}.get(self.last_present, "?")
        print("[%8.2f s] %s, %.0f s left | board %s | port %s | %d lines, %d JSON | last: %s %s"
              % (self.rel(), ("segment %d %s" % (s["idx"], s["label"])) if s else "-",
                 max(0.0, left), self.board or "not identified yet",
                 "open" if self.ser else "CLOSED", self.lines, self.json_rows,
                 self.last_state or "-", pres), flush=True)

    # -- main loop
    def run(self, first_handle):
        self.ser = first_handle
        self.t0 = time.perf_counter()
        wall = datetime.datetime.now().astimezone()
        self.header(wall)
        self.mark("PORT", "open %s" % self.port_name)
        self.last_rx = time.perf_counter()
        next_summary = self.rel() + (5.0 if self.a.simulate else SUMMARY_EVERY_S)
        ev_i, tail_until, result = 0, None, "interrupted"
        try:
            while True:
                now = self.rel()
                while ev_i < len(self.events) and self.events[ev_i][0] <= now:
                    _t, _o, what, arg = self.events[ev_i]
                    ev_i += 1
                    self.fire(what, arg)
                    if what == "end":
                        tail_until = self.rel() + TAIL_S
                if tail_until is not None and now >= tail_until:
                    result = "completed"
                    break
                if self.pending_start_status and self.may_send():
                    self.status("start")
                if now >= next_summary:
                    self.summary()
                    next_summary = now + (5.0 if self.a.simulate else SUMMARY_EVERY_S)
                if self.ser is None:
                    if now >= self.next_try:
                        self.try_reopen()
                    else:
                        time.sleep(0.05)
                    continue
                try:
                    n = self.ser.in_waiting
                    chunk = self.ser.read(n if n else 1)
                except Exception as e:
                    self.port_lost(e)
                    continue
                t = time.perf_counter()
                if chunk:
                    self.buf += chunk
                    self.last_rx = t
                    if self.silent_warned:
                        self.silent_warned = False
                        self.mark("WARN", "board output resumed")
                    while b"\n" in self.buf:
                        raw, self.buf = self.buf.split(b"\n", 1)
                        self.board_line(raw.decode("utf-8", "replace").rstrip("\r"))
                elif self.buf and (len(self.buf) > 4096 or t - self.last_rx > PARTIAL_LINE_S):
                    self.flush_partial()
                if (not self.silent_warned and t - self.last_rx > SILENCE_WARN_S):
                    self.silent_warned = True
                    self.mark("WARN", "no board output for %.0f s with the port open "
                              "(docs/TESTING.md G18)" % SILENCE_WARN_S)
        except KeyboardInterrupt:
            print("\nCtrl+C: closing the log", flush=True)
            if tail_until is not None:
                result = "completed"   # the schedule finished; only the wait for
                                       # the end 'status' reply was cut
        finally:
            self.flush_partial()
            if result != "completed" and self.open_seg is not None:
                s = self.open_seg
                self.mark("SEGMENT", "end %d %s %s" % (s["idx"], s["kind"], s["label"]))
            self.mark("END", result)
            if self.ser is not None:
                try:
                    self.ser.close()
                except Exception:
                    pass
        return result

    def header(self, wall):
        a = self.a
        w = lambda k, v: self.fh.write("# RUN %s %s\n" % (k, str(v).replace("\n", " / ")))
        w("tool", TOOL)
        w("start_wall", wall.isoformat(timespec="milliseconds"))
        w("port", ("simulate %s" % os.path.abspath(a.simulate)) if a.simulate else a.port)
        w("protocol", a.protocol)
        w("scale", "%g" % a.scale)
        w("argv", subprocess.list2cmdline(sys.argv))
        w("note", a.note.strip())
        w("console", "off" if a.no_console else ("forced" if a.console else "auto"))
        w("python", platform.python_version())
        w("planned_total_s", "%.2f" % self.total)
        for s in self.segs:
            self.fh.write("# PLAN %d %.2f %.2f %s %s\n"
                          % (s["idx"], s["start"], s["dur"], s["kind"], s["label"]))


# ----------------------------------------------------------------- main --

def parse_args():
    ap = argparse.ArgumentParser(
        description="Run a docs/TESTING.md §4 session (phase1 / phase2b) while "
                    "logging the board, with cues for the operator. See the "
                    "module docstring for the log grammar.")
    ap.add_argument("--protocol", required=True, choices=("phase1", "phase2b"))
    ap.add_argument("--port", help="the board's COM port, e.g. COM13")
    ap.add_argument("--out", metavar="FILE", help="the log to write (must not exist)")
    ap.add_argument("--note", help="REQUIRED: placement (board and AP positions, distance), "
                                   "who is in the room, AP and channel")
    ap.add_argument("--scale", type=float, default=1.0, metavar="F",
                    help="multiply every duration by F (default 1)")
    ap.add_argument("--lead-in", type=float, default=LEAD_IN_S, metavar="S",
                    help="unscored 'setup' segment first (default %g s; 0 = none)" % LEAD_IN_S)
    ap.add_argument("--calibrate-first", type=float, default=0.0, metavar="N",
                    help="unscored empty 'calibration' segment of N s (board calibrating on boot)")
    ap.add_argument("--walk-s", type=float, metavar="S",
                    help="phase1: one pass (default %g); phase2b: walking block (default %g)"
                         % (P1_WALK_S, P2B_WALK_S))
    ap.add_argument("--empty-s", type=float, metavar="S",
                    help="phase1: empty gap after a pass (default %g); phase2b: empty block "
                         "(default %g)" % (P1_EMPTY_S, P2B_EMPTY_S))
    ap.add_argument("--still-s", type=float, metavar="S",
                    help="phase1: 1c sit (default %g); phase2b: seated block (default %g)"
                         % (P1_STILL_S, P2B_SEATED_S))
    ap.add_argument("--passes", type=int, metavar="N",
                    help="phase1: passes per direction (default %d)" % P1_PASSES)
    ap.add_argument("--start-empty-s", type=float, metavar="S",
                    help="phase1: first empty segment (default %g)" % P1_START_EMPTY_S)
    ap.add_argument("--enter-s", type=float, metavar="S",
                    help="phase1: walk-in before 1b and 1c (default %g)" % P1_ENTER_S)
    ap.add_argument("--minor-s", type=float, metavar="S",
                    help="phase1: 1b seated small movements (default %g)" % P1_MINOR_S)
    ap.add_argument("--gap-s", type=float, metavar="S",
                    help="phase1: empty after 1b and after 1c (default %g)" % P1_GAP_S)
    ap.add_argument("--status-every", type=float, default=STATUS_EVERY_S, metavar="S",
                    help="Step 10: 'status' every S s (default %g; 0 = start and end only)"
                         % STATUS_EVERY_S)
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--console", action="store_true",
                   help="send 'status' before the board is identified as Step 10")
    g.add_argument("--no-console", action="store_true", help="never send anything")
    ap.add_argument("--speak", action="store_true", help="also speak the cues")
    ap.add_argument("--quiet", action="store_true", help="no beeps")
    ap.add_argument("--sound", action="store_true", help="beep in --simulate too")
    ap.add_argument("--echo", action="store_true", help="print every board line")
    ap.add_argument("--simulate", metavar="FILE", help="play a recorded log as the port")
    ap.add_argument("--dry-run", action="store_true", help="print the schedule and exit")
    a = ap.parse_args()

    if a.scale <= 0:
        ap.error("--scale must be > 0")
    if a.lead_in < 0 or a.calibrate_first < 0 or a.status_every < 0:
        ap.error("--lead-in, --calibrate-first and --status-every must be >= 0")
    for name in ("walk_s", "empty_s", "still_s", "start_empty_s", "enter_s", "minor_s", "gap_s"):
        v = getattr(a, name)
        if v is not None and v <= 0:
            ap.error("--%s must be > 0" % name.replace("_", "-"))
    if a.passes is not None and a.passes < 1:
        ap.error("--passes must be >= 1")
    if a.protocol == "phase2b":
        extra = [n for n in ("passes", "start_empty_s", "enter_s", "minor_s", "gap_s")
                 if getattr(a, n) is not None]
        if extra:
            ap.error("phase1-only option(s) with phase2b: "
                     + ", ".join("--" + n.replace("_", "-") for n in extra))
    if a.dry_run:
        return a
    if not a.note or not a.note.strip():
        ap.error("--note is required: write the placement (board and AP positions and the "
                 "distance between them), who is in the room, and the AP and channel. e.g. "
                 "--note \"board on shelf 1.0 m high N wall, AP on desk S wall, 4.2 m apart, "
                 "line of sight; only the operator in the flat; hotspot 'Testing' ch 6\"")
    if not a.out:
        ap.error("--out FILE is required (outside the repo: docs/TESTING.md §2.5)")
    if os.path.exists(a.out):
        ap.error("--out %s exists; raw logs are never overwritten - choose another name" % a.out)
    d = os.path.dirname(os.path.abspath(a.out))
    if not os.path.isdir(d):
        ap.error("--out: folder %s does not exist - create it first" % d)
    if not a.simulate and not a.port:
        ap.error("--port COMx is required (or --simulate FILE / --dry-run)")
    if a.simulate and not os.path.isfile(a.simulate):
        ap.error("--simulate: no such file: %s" % a.simulate)
    return a


def main():
    try:
        sys.stdout.reconfigure(errors="replace")   # U+FFFD on a cp1252 console
    except (AttributeError, ValueError):
        pass
    a = parse_args()
    segs, total, events = build_schedule(a)
    if a.dry_run:
        print_schedule(a, segs, total, events)
        if not a.note:
            print("(a real run also needs --note: placement, who is in the room, AP and channel)")
        return 0

    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out_abs = os.path.abspath(a.out)
    if os.path.normcase(out_abs).startswith(os.path.normcase(repo) + os.sep):
        print("WARNING: --out is inside the repo; raw logs belong outside it "
              "(docs/TESTING.md §2.5, §5)", flush=True)

    if a.simulate:
        sim = SimBoard(a.simulate, 1.0 / a.scale)
        opener, port_name = sim.open, "simulate:" + os.path.basename(a.simulate)
        print("simulate: %s (%s format, %d items) at %gx its recorded pace"
              % (a.simulate, sim.fmt, len(sim.items), 1.0 / a.scale), flush=True)
    else:
        try:
            opener = live_opener(a.port)
        except ImportError:
            sys.exit("pyserial is missing: pip install pyserial (or run export.ps1 first)")
        port_name = a.port

    # Start-up: bounded retry, then give up BEFORE any cue (see THE PORT).
    deadline = time.perf_counter() + OPEN_WAIT_S
    while True:
        try:
            handle = opener()
            break
        except Exception as e:
            if time.perf_counter() >= deadline:
                sys.exit("cannot open %s for %.0f s: %s\nNothing was recorded. Is another "
                         "program holding the port (docs/TESTING.md G7), or is it the wrong "
                         "port (G8)?" % (port_name, OPEN_WAIT_S, e))
            time.sleep(REOPEN_EVERY_S)

    announcer = Announcer(beep=not a.quiet and (a.sound or not a.simulate), speak=a.speak)
    print_schedule(a, segs, total, events)
    print("note: %s" % a.note.strip())
    print("writing %s - Ctrl+C stops early" % out_abs, flush=True)
    try:                   # "x": never truncate a file created since parse_args
        fh = open(a.out, "x", encoding="utf-8", errors="replace", buffering=1)
    except FileExistsError:
        sys.exit("--out %s appeared meanwhile; not overwritten" % a.out)
    with fh:
        sess = Session(a, segs, total, events, opener, port_name, fh, announcer)
        result = sess.run(handle)
    print("%s: %d board lines, %d JSON lines, board %s -> %s"
          % (result, sess.lines, sess.json_rows, sess.board or "not identified", out_abs),
          flush=True)
    time.sleep(0.5 if (a.speak or not a.quiet) else 0.0)   # let the last cue play
    return 0 if result == "completed" else 130


if __name__ == "__main__":
    sys.exit(main())
