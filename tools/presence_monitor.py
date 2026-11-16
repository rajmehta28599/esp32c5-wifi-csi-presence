#!/usr/bin/env python3
"""
presence_monitor.py - read the JSON stream from Step 08 / Step 10 and show /
log it, and send console commands to Step 10.

Step 08 prints lines like:
    JSON {"t_ms":123456,"state":"detect","present":1,"motion_sd":7.4,
          "dev_sd":1.2,"level":148,"rssi":-46,"rate_hz":49.6,"nsub":52}

Step 10 appends its own keys to the same line (Step 08's come first,
unchanged, so this tool reads both):
    "count":1,"count_src":"presence",
    "person":"raj","person_dist":0.84,"person_votes":"5/7",
    "vitals":"ok","br_bpm":14.8,"br_conf":6.2,"hr_bpm_exp":71.3,"hr_conf":5.4,
    "session":"enroll:raj 12/30",
    "hf":1.07,"proc_ms_max":8.4,"dropped":0,"vitals_of":"raj",
    "activity":"still","activity_s":42,"episode_s":318,
    "agc":36,"fft":-2,"gain_chg":0
person, person_dist, person_votes, br_bpm, br_conf, hr_bpm_exp, hr_conf,
session, hf and vitals_of can be null - no model, nobody present, no
confident estimate, no noise floor yet, nobody named. hf is the vitals
motion gate's ratio (~1 = nobody moving; at or above
CSI_VITALS_MOTION_RATIO_X100 / 100 = moving) and is shown in the status
line for tuning that threshold. proc_ms_max (slowest packet since the
previous line, ms) and dropped (CSI packets lost since boot) are
diagnostics: check them on the first flash.

vitals is "off", "no-person", "multiple", "moving", "settling", "no-signal"
or "ok". "multiple" means two or more people this visit (counted moving
together, or two names recognised), so br_bpm / hr_bpm_exp are withheld
(null) until the room has been empty. vitals_of is the name the rates are
attributed to: the last person recognised walking in ALONE this visit. It
is a carried-over claim, not a measurement - nobody can be recognised while
sitting still - and it is appended after "dropped" so no older key moved.
The status line shows it as "of=<name>" when it differs from who=.

activity is null, "empty", "still", "restless" or "moving": a coarse state
of the room. activity_s is the whole seconds spent in the current activity
state, and episode_s the whole seconds of the current presence episode
(an integer or null). The three are appended after "vitals_of". A change of activity is printed as
its own line ("*** <time>  activity: <state>"); the status line has no
room for it.

agc and fft are the receiver gain of the last packet since the previous
line and gain_chg the number of packets that changed it (CSI_GAIN_LOG,
DECISIONS D-176): two rx_ctrl bytes that Espressif's gain library reads,
not a documented field and not yet seen on silicon. Logging only - they
are written to the CSV and not shown. All three null without the switch
or with no packet since the previous line. Only the detect line carries
them; the first one after a calibration counts from the switch to
detection.

Ways to use it:

    # live status in the terminal
    python presence_monitor.py --port COM12

    # --port auto (the default) picks the one ESP32 board plugged in, by USB
    # vendor ID - the same table build.ps1 uses
    python presence_monitor.py

    # also write every sample to a CSV for later analysis
    # (overwrites the file; each row gets a host_time wall-clock column)
    python presence_monitor.py --port COM12 --csv session.csv

    # live plot of the motion score with the trigger threshold marked
    python presence_monitor.py --port COM12 --plot

    # Step 10: send console commands, then keep monitoring
    python presence_monitor.py --port COM12 --send "enroll 0 raj"
    python presence_monitor.py --port COM12 --send "people" --send "status"

    # Step 10: delete every person's vitals log (their templates are kept)
    python presence_monitor.py --port COM12 --send "vitals-reset all"

--send exists because only one program can own the COM port: you cannot
type into idf.py monitor and log a CSV here at the same time. Commands are
sent once the firmware is visibly running (its first JSON line), one every
--send-gap seconds, and the board's "CMD ..." answers are printed so you can
see whether each was accepted - including the indented lines that follow
the CMD line of 'status', 'people' and 'help'. Typing the same commands
into idf.py monitor works too.

WHY host_time
    t_ms is the board's clock, milliseconds since boot. A pulse oximeter or
    chest strap logs against wall-clock time. host_time is this PC's clock
    when the line arrived (ISO 8601 with UTC offset), which is what
    tools/vitals_validate.py aligns the two on. Keep the PC's clock synced
    (Windows: Settings -> Time -> Sync now) if the reference device logs on
    a different machine or a phone.

TUNING WORKFLOW
    Run with --plot, then:
      1. Leave the room, watch where the motion trace sits when empty.
         It should hover near 0 sd with occasional spikes of 1-2 sd.
      2. Walk in. The trace should jump well above the threshold line.
      3. If the empty-room trace regularly crosses the line, your threshold
         is too low - raise CSI_MOTION_SIGMA_X100 in menuconfig.
      4. If walking barely moves it, lower the threshold, or recalibrate:
         a baseline taken while the room was NOT empty gives an inflated
         m_std, which flattens everything afterwards.

Heart rate is shown as "hr~72": the "~" is there on purpose. It is the
firmware's EXPERIMENTAL estimate (JSON key hr_bpm_exp) and means nothing
until you have checked it against a reference with tools/vitals_validate.py.
"""

import argparse
import csv
import datetime
import json
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial is missing.  pip install pyserial")


# Every key either firmware can print. DictWriter is created with
# extrasaction="ignore", so a key missing from this list is dropped
# silently - add new firmware keys here.
CSV_FIELDS = [
    "host_time",
    # Step 08
    "t_ms", "state", "present", "motion_sd", "dev_sd", "level", "rssi",
    "rate_hz", "nsub", "remaining_s",
    # Step 10
    "count", "count_src",
    "person", "person_dist", "person_votes",
    "vitals", "br_bpm", "br_conf", "hr_bpm_exp", "hr_conf",
    "session",
    "hf", "proc_ms_max", "dropped",
    # appended by later firmware (after "dropped", so no older key moved)
    "vitals_of",
    "activity", "activity_s", "episode_s",
    "agc", "fft", "gain_chg",
]

# Presence of any of these marks a Step 10 record (switches the text view
# to the compact layout that has room for them).
STEP10_KEYS = ("count", "person", "vitals", "session", "activity")

# USB vendor IDs that appear on ESP32 devkits - the same table as
# build.ps1's $UsbSerialVids, which is what the IDE tasks' 'auto' means.
BOARD_VIDS = {
    0x303A: "Espressif USB-Serial-JTAG (built into the C5)",
    0x10C4: "Silicon Labs CP210x bridge",
    0x1A86: "WCH CH34x bridge",
    0x0403: "FTDI bridge",
}


def resolve_port(port):
    """'auto' (the default, and what the Antigravity task passes when its
    port prompt is left alone) picks the one connected ESP32 board by USB
    vendor ID. Anything else is used as given."""
    if port.strip().lower() != "auto":
        return port
    from serial.tools import list_ports
    ports = list(list_ports.comports())
    boards = [p for p in ports if p.vid in BOARD_VIDS]
    if len(boards) == 1:
        p = boards[0]
        print(f"--port auto: using {p.device} ({BOARD_VIDS[p.vid]})")
        return p.device
    if not boards:
        present = ", ".join(p.device for p in ports) or "none"
        sys.exit("--port auto: no ESP32 board found (serial ports present: "
                 f"{present}). Plug it in with a data cable, or pass --port COMx.")
    listed = ", ".join(f"{p.device} ({BOARD_VIDS[p.vid]})" for p in boards)
    sys.exit(f"--port auto: {len(boards)} boards found - {listed}. "
             "Pass --port COMx to pick one.")


def parse_line(raw: bytes):
    """Return (kind, payload): ("json", dict), ("cmd", str) or (None, None).

    "CMD " lines are Step 10's command answers; they are passed up so the
    user sees whether a --send command was accepted. 'status', 'people' and
    'help' continue their answer on lines indented by four spaces - the
    motion gate, pipeline and heap figures are there - so those are passed
    up too. Nothing else the firmware prints starts with spaces (log lines
    start with their level letter, JSON with "JSON ", the bar line with
    PRESENT / empty)."""
    try:
        line = raw.decode("utf-8", errors="ignore").rstrip("\r\n")
    except Exception:
        return None, None
    text = line.strip()
    if text.startswith("CMD "):
        return "cmd", text
    if text and line.startswith("    "):
        return "cmd", line
    if not text.startswith("JSON "):
        return None, None
    try:
        rec = json.loads(text[5:])
    except json.JSONDecodeError:
        return None, None
    if not isinstance(rec, dict):
        return None, None
    rec["host_time"] = datetime.datetime.now().astimezone().isoformat(
        timespec="milliseconds")
    return "json", rec


def num(v, spec, dash="-"):
    """Format a JSON number that may be null (None) or missing."""
    if v is None:
        return dash
    try:
        return format(float(v), spec)
    except (TypeError, ValueError):
        return dash


def text_or_dash(v):
    return "-" if v in (None, "") else str(v)


def step10_summary(rec):
    """'n=1 who=raj br=15 hr~72 hf=1.07' - the fields the firmware's own
    human-readable line shows, plus hf (the vitals motion gate's ratio,
    for tuning CSI_VITALS_MOTION_RATIO_X100); '-' where absent or null.
    'of=<name>' (vitals_of, whose the rates are claimed to be) follows who=
    only when it says something who= does not - typically a seated person,
    who has no "person" (nobody is recognised sitting) but can have one."""
    who = rec.get("person")
    parts = [f"n={text_or_dash(rec.get('count'))}"]
    parts.append(f"who={text_or_dash(who)}")
    of = rec.get("vitals_of")
    if of not in (None, "") and of != who:
        parts.append(f"of={of}")
    parts.append(f"br={num(rec.get('br_bpm'), '.0f')}")
    parts.append(f"hr~{num(rec.get('hr_bpm_exp'), '.0f')}")
    parts.append(f"hf={num(rec.get('hf'), '.2f')}")
    vit = rec.get("vitals")
    if vit not in (None, "ok"):
        parts.append(f"({vit})")
    return " ".join(parts)


class EventLog:
    """Prints one line when something a user cares about changes, so the
    moment it happened survives the \\r-overwritten status line."""

    def __init__(self):
        self.last = {}

    def changed(self, key, value, announce_first=False):
        """True when value differs from the previous record's. The first
        record only counts as a change if announce_first, or if it carries
        a real value (no "person: None" line at start-up)."""
        if key in self.last and self.last[key] == value:
            return False
        first = key not in self.last
        self.last[key] = value
        if not first or announce_first:
            return True
        return value is not None and value != ""

    def check(self, rec):
        stamp = time.strftime("%H:%M:%S")
        present = bool(rec.get("present", 0))
        if self.changed("present", present, announce_first=True):
            print(f"\n*** {stamp}  {'HUMAN DETECTED' if present else 'room clear'}"
                  f"   (motion {num(rec.get('motion_sd'), '.1f')} sd)")
        if "person" in rec and self.changed("person", rec.get("person")):
            who = rec.get("person")
            if who not in (None, ""):
                print(f"\n*** {stamp}  person: {who}"
                      f"  (dist {num(rec.get('person_dist'), '.2f')},"
                      f" votes {text_or_dash(rec.get('person_votes'))})")
        if "count" in rec and self.changed("count", rec.get("count")):
            if present:
                print(f"\n*** {stamp}  count: {text_or_dash(rec.get('count'))}"
                      f"  ({text_or_dash(rec.get('count_src'))})")
        # Tracked on every record, printed only while present: at presence
        # end it always goes null, and "room clear" already says that.
        if "vitals_of" in rec and self.changed("vitals_of", rec.get("vitals_of")):
            if present:
                print(f"\n*** {stamp}  vitals of: {text_or_dash(rec.get('vitals_of'))}")
        if "vitals" in rec and self.changed("vitals", rec.get("vitals")):
            if rec.get("vitals") == "multiple":
                print(f"\n*** {stamp}  vitals: multiple people - rates withheld")
        if "activity" in rec and self.changed("activity", rec.get("activity")):
            act = rec.get("activity")
            if act not in (None, ""):
                ep = rec.get("episode_s")
                print(f"\n*** {stamp}  activity: {act}"
                      + (f"  (episode {ep} s)" if ep is not None else ""))
        if "session" in rec and self.changed("session", rec.get("session")):
            ses = rec.get("session")
            print(f"\n*** {stamp}  session: "
                  f"{ses if ses else 'none (finished, stopped or cancelled)'}")


def run_text(ser, writer, csv_file):
    events = EventLog()
    while True:
        kind, rec = parse_line(ser.readline())
        if kind == "cmd":
            # A new line first to leave the \r status line - but not
            # between a CMD line and its own indented continuation.
            print(rec if rec.startswith(" ") else f"\n{rec}")
            continue
        if kind != "json":
            continue

        if writer:
            writer.writerow(rec)
            csv_file.flush()

        state = rec.get("state", "?")
        if state == "no_csi":
            print("\n[no_csi] no usable CSI - link down or CSI shape changed")
            continue
        if state != "detect":
            print(f"[{state}] {rec.get('remaining_s', 0)} s remaining ...")
            continue

        events.check(rec)

        present = bool(rec.get("present", 0))
        level = rec.get("level") or 0
        rssi = num(rec.get("rssi"), "4.0f", "   -")
        motion = num(rec.get("motion_sd"), "6.1f", "     -")

        if any(k in rec for k in STEP10_KEYS):
            # Step 10: shorter bar so count / person / vitals still fit on
            # one terminal line (a wrapped line breaks the \r overwrite).
            # The pad (56, room for an "of=<name>") blanks out the rest of
            # a longer previous summary; ~117 columns in all.
            bar_len = max(0, min(20, int(level * 20 / 200)))
            bar = "#" * bar_len + "." * (20 - bar_len)
            print(f"\r{'PRESENT' if present else 'empty  '} |{bar}| "
                  f"motion {motion} sd  rssi {rssi} | {step10_summary(rec):<56}",
                  end="", flush=True)
        else:
            dev = num(rec.get("dev_sd"), "6.1f", "     -")
            bar_len = max(0, min(40, int(level * 40 / 200)))
            bar = "#" * bar_len + "." * (40 - bar_len)
            print(f"\r{'PRESENT' if present else 'empty  '} |{bar}| "
                  f"motion {motion} sd  dev {dev} sd  rssi {rssi}",
                  end="", flush=True)


def run_plot(ser, writer, csv_file, window):
    try:
        import collections
        import matplotlib.pyplot as plt
        from matplotlib.animation import FuncAnimation
    except ImportError:
        sys.exit("matplotlib is missing.  pip install matplotlib")

    motion = collections.deque(maxlen=window)
    dev = collections.deque(maxlen=window)
    last = {"rec": None}

    fig, ax = plt.subplots(figsize=(11, 5))
    fig.canvas.manager.set_window_title("ESP32-C5 presence")
    l_m, = ax.plot([], [], label="motion (sd)", lw=1.4)
    l_d, = ax.plot([], [], label="deviation (sd)", lw=1.0, alpha=0.7)
    ax.axhline(5.0, color="r", ls="--", lw=1, label="default trigger (5 sd)")
    ax.set_ylabel("standard deviations above empty room")
    ax.set_xlabel("samples")
    ax.grid(alpha=0.3)
    ax.legend(loc="upper left")

    def update(_f):
        n = 0
        while ser.in_waiting and n < 100:
            kind, rec = parse_line(ser.readline())
            n += 1
            if kind == "cmd":
                print(rec)
                continue
            if kind != "json" or rec.get("state") != "detect":
                continue
            if writer:
                writer.writerow(rec)
                csv_file.flush()
            # null-safe: a JSON null plots as 0 rather than crashing max()
            motion.append(rec.get("motion_sd") or 0.0)
            dev.append(rec.get("dev_sd") or 0.0)
            last["rec"] = rec

        if motion:
            xs = range(len(motion))
            l_m.set_data(xs, list(motion))
            l_d.set_data(range(len(dev)), list(dev))
            ax.set_xlim(0, max(10, len(motion)))
            hi = max(8.0, max(motion) * 1.15)
            ax.set_ylim(-0.5, hi)
            rec = last["rec"]
            title = "PRESENT" if rec and rec.get("present") else "empty"
            if rec and any(k in rec for k in STEP10_KEYS):
                title += "    " + step10_summary(rec)
            ax.set_title(title)
        return []

    _a = FuncAnimation(fig, update, interval=150, blit=False,
                       cache_frame_data=False)
    plt.tight_layout()
    plt.show()


def send_commands(ser, writer, csv_file, commands, wait_s, gap_s):
    """Wait until the firmware is running (first JSON line, any state), then
    write each command + newline, gap_s apart.

    Waiting matters: opening the port can reset some boards, and a command
    written while the chip is still booting is simply lost. Enrolment and
    count training also need the detector running (state "detect"); if it
    is still calibrating, the board answers with a CMD line saying so."""
    print(f"waiting up to {wait_s:.0f} s for the firmware before sending ...")
    deadline = time.monotonic() + wait_s
    seen = False
    while time.monotonic() < deadline:
        kind, rec = parse_line(ser.readline())
        if kind == "cmd":
            print(rec)
        elif kind == "json":
            if writer:
                writer.writerow(rec)
                csv_file.flush()
            print(f"firmware is up (state: {rec.get('state', '?')})")
            seen = True
            break
    if not seen:
        print("no JSON seen yet - sending anyway (check the port and the step)")

    for cmd in commands:
        cmd = cmd.strip()
        if not cmd:
            continue
        ser.write((cmd + "\n").encode("utf-8"))
        ser.flush()
        print(f">>> sent: {cmd}")
        time.sleep(gap_s)


def main():
    ap = argparse.ArgumentParser(
        description="Show / log the JSON stream from Step 08 or Step 10, "
                    "and send Step 10 console commands.")
    ap.add_argument("--port", default="auto",
                    help="serial port, e.g. COM12; 'auto' (default) picks the "
                         "one ESP32 board plugged in")
    ap.add_argument("--baud", type=int, default=921600)
    ap.add_argument("--csv", help="write every sample to this CSV file "
                                  "(overwritten; adds a host_time column)")
    ap.add_argument("--plot", action="store_true", help="live plot instead of text")
    ap.add_argument("--window", type=int, default=400, help="plot window in samples")
    ap.add_argument("--send", action="append", default=[], metavar="CMD",
                    help='Step 10 console command to send after opening the port, '
                         'e.g. --send "enroll 0 raj". Repeatable; sent in order.')
    ap.add_argument("--send-wait", type=float, default=10.0, metavar="S",
                    help="seconds to wait for the firmware's first JSON line "
                         "before sending (default 10)")
    ap.add_argument("--send-gap", type=float, default=0.3, metavar="S",
                    help="seconds between commands (default 0.3)")
    args = ap.parse_args()

    port = resolve_port(args.port)
    # Open with DTR and RTS de-asserted, as idf.py monitor does. On the C5's
    # USB-Serial-JTAG those two lines drive the chip's reset and boot
    # strapping; pyserial's default (both asserted on open) can hold or
    # reset the chip, and a pulse on them from pyserial under Windows once
    # latched it in reset until the RST button was pressed
    # (docs/DECISIONS.md, hardware bring-up gotchas).
    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = port, args.baud, 1
    ser.dtr = False
    ser.rts = False
    ser.open()
    print(f"listening on {port} at {args.baud} ... Ctrl+C to stop")

    csv_file = None
    writer = None
    if args.csv:
        csv_file = open(args.csv, "w", newline="", encoding="utf-8")
        writer = csv.DictWriter(csv_file, fieldnames=CSV_FIELDS,
                                extrasaction="ignore")
        writer.writeheader()
        print(f"logging to {args.csv}")

    try:
        if args.send:
            send_commands(ser, writer, csv_file, args.send,
                          args.send_wait, args.send_gap)
        if args.plot:
            run_plot(ser, writer, csv_file, args.window)
        else:
            run_text(ser, writer, csv_file)
    except KeyboardInterrupt:
        print("\nstopped")
    finally:
        ser.close()
        if csv_file:
            csv_file.close()


if __name__ == "__main__":
    main()
