#!/usr/bin/env python3
"""
presence_monitor.py - read the JSON stream from Step 08 and show / log it.

Step 08 prints lines like:
    JSON {"t_ms":123456,"state":"detect","present":1,"motion_sd":7.4,
          "dev_sd":1.2,"level":148,"rssi":-46,"rate_hz":49.6,"nsub":52}

Three ways to use it:

    # live status in the terminal
    python presence_monitor.py --port COM12

    # also append every sample to a CSV for later analysis
    python presence_monitor.py --port COM12 --csv session.csv

    # live plot of the motion score with the trigger threshold marked
    python presence_monitor.py --port COM12 --plot

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
"""

import argparse
import csv
import json
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial is missing.  pip install pyserial")


def parse_line(raw: bytes):
    try:
        text = raw.decode("utf-8", errors="ignore").strip()
    except Exception:
        return None
    if not text.startswith("JSON "):
        return None
    try:
        return json.loads(text[5:])
    except json.JSONDecodeError:
        return None


def run_text(ser, writer, csv_file):
    last_present = None
    while True:
        rec = parse_line(ser.readline())
        if rec is None:
            continue

        if writer:
            writer.writerow(rec)
            csv_file.flush()

        state = rec.get("state", "?")
        if state != "detect":
            print(f"[{state}] {rec.get('remaining_s', 0)} s remaining ...")
            continue

        present = bool(rec.get("present", 0))
        motion = rec.get("motion_sd", 0.0)
        dev = rec.get("dev_sd", 0.0)
        level = rec.get("level", 0)
        rssi = rec.get("rssi", 0)

        if present != last_present:
            stamp = time.strftime("%H:%M:%S")
            print(f"\n*** {stamp}  {'HUMAN DETECTED' if present else 'room clear'}"
                  f"   (motion {motion:.1f} sd)\n")
            last_present = present

        bar_len = max(0, min(40, int(level * 40 / 200)))
        bar = "#" * bar_len + "." * (40 - bar_len)
        print(f"\r{'PRESENT' if present else 'empty  '} |{bar}| "
              f"motion {motion:6.1f} sd  dev {dev:6.1f} sd  rssi {rssi:4d}",
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
    pres = collections.deque(maxlen=window)

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
            rec = parse_line(ser.readline())
            n += 1
            if rec is None or rec.get("state") != "detect":
                continue
            if writer:
                writer.writerow(rec)
                csv_file.flush()
            motion.append(rec.get("motion_sd", 0.0))
            dev.append(rec.get("dev_sd", 0.0))
            pres.append(bool(rec.get("present", 0)))

        if motion:
            xs = range(len(motion))
            l_m.set_data(xs, list(motion))
            l_d.set_data(range(len(dev)), list(dev))
            ax.set_xlim(0, max(10, len(motion)))
            hi = max(8.0, max(motion) * 1.15)
            ax.set_ylim(-0.5, hi)
            ax.set_title("PRESENT" if (pres and pres[-1]) else "empty")
        return []

    _a = FuncAnimation(fig, update, interval=150, blit=False,
                       cache_frame_data=False)
    plt.tight_layout()
    plt.show()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=921600)
    ap.add_argument("--csv", help="append every sample to this CSV file")
    ap.add_argument("--plot", action="store_true", help="live plot instead of text")
    ap.add_argument("--window", type=int, default=400, help="plot window in samples")
    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=1)
    print(f"listening on {args.port} at {args.baud} ... Ctrl+C to stop")

    csv_file = None
    writer = None
    if args.csv:
        csv_file = open(args.csv, "w", newline="", encoding="utf-8")
        writer = csv.DictWriter(
            csv_file,
            fieldnames=["t_ms", "state", "present", "motion_sd", "dev_sd",
                        "level", "rssi", "rate_hz", "nsub", "remaining_s"],
            extrasaction="ignore",
        )
        writer.writeheader()
        print(f"logging to {args.csv}")

    try:
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
