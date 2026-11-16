#!/usr/bin/env python3
"""
csi_monitor.py - live plot of the CSI amplitude stream from Step 05.

Step 05 prints one line per packet:
    CSI,<seq>,<ms>,<rssi>,<nsub>,<median_x100>,<a0>,<a1>,...,<aN-1>
where each aK is the normalised amplitude x 100.

This script draws two panels:
    top    - the current amplitude across subcarriers (the "channel shape")
    bottom - a waterfall of the last few seconds (time on Y, subcarrier on X)

Watch the waterfall while someone walks past the board. You should see
vertical streaks appear. If the waterfall is a flat, featureless block even
when you wave your arms in front of the antenna, something upstream is wrong
- go back and re-check Steps 03 and 04 before touching the detector.

Usage:
    pip install pyserial matplotlib numpy
    python csi_monitor.py --port COM12
    python csi_monitor.py --port COM12 --baud 921600 --history 300
    python csi_monitor.py        # --port auto (default): the one ESP32 board plugged in
"""

import argparse
import collections
import sys

try:
    import serial
except ImportError:
    sys.exit("pyserial is missing.  pip install pyserial")

try:
    import numpy as np
    import matplotlib.pyplot as plt
    from matplotlib.animation import FuncAnimation
except ImportError:
    sys.exit("matplotlib/numpy are missing.  pip install matplotlib numpy")


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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="auto",
                    help="serial port, e.g. COM12 or /dev/ttyUSB0; 'auto' (default) "
                         "picks the one ESP32 board plugged in")
    ap.add_argument("--baud", type=int, default=921600)
    ap.add_argument("--history", type=int, default=200, help="waterfall rows")
    args = ap.parse_args()

    port = resolve_port(args.port)
    # DTR/RTS de-asserted before opening, as idf.py monitor does: on the
    # C5's USB-Serial-JTAG they drive reset and boot strapping (see
    # presence_monitor.py and docs/DECISIONS.md, hardware bring-up gotchas).
    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = port, args.baud, 1
    ser.dtr = False
    ser.rts = False
    ser.open()
    print(f"listening on {port} at {args.baud} ...")
    print("waiting for the first CSI line (Step 05 must be running)")

    history = collections.deque(maxlen=args.history)
    state = {"nsub": None, "rssi": 0, "median": 0.0, "seq": 0}

    fig, (ax_line, ax_fall) = plt.subplots(
        2, 1, figsize=(11, 8), gridspec_kw={"height_ratios": [1, 2]}
    )
    fig.canvas.manager.set_window_title("ESP32-C5 CSI monitor")

    line, = ax_line.plot([], [], lw=1.2)
    ax_line.set_title("current normalised amplitude per subcarrier")
    ax_line.set_xlabel("subcarrier index")
    ax_line.set_ylabel("amplitude / median")
    ax_line.grid(alpha=0.3)

    img = None
    ax_fall.set_title("waterfall - newest at the bottom")
    ax_fall.set_xlabel("subcarrier index")
    ax_fall.set_ylabel("packets ago")

    def read_available():
        """Drain whatever is waiting, keep the newest frames."""
        got = 0
        while ser.in_waiting and got < 200:
            raw = ser.readline()
            got += 1
            try:
                text = raw.decode("utf-8", errors="ignore").strip()
            except Exception:
                continue
            if not text.startswith("CSI,"):
                continue
            parts = text.split(",")
            if len(parts) < 7:
                continue
            try:
                state["seq"] = int(parts[1])
                state["rssi"] = int(parts[3])
                nsub = int(parts[4])
                state["median"] = int(parts[5]) / 100.0
                amps = [int(v) / 100.0 for v in parts[6:6 + nsub]]
            except ValueError:
                continue
            if len(amps) != nsub or nsub <= 0:
                continue
            if state["nsub"] != nsub:
                state["nsub"] = nsub
                history.clear()
                print(f"CSI shape: {nsub} subcarriers")
            history.append(amps)

    def update(_frame):
        nonlocal img
        read_available()
        if not history:
            return []

        arr = np.array(history, dtype=float)
        cur = arr[-1]
        x = np.arange(len(cur))

        line.set_data(x, cur)
        ax_line.set_xlim(0, len(cur) - 1)
        top = max(2.0, float(arr.max()) * 1.05)
        ax_line.set_ylim(0, top)
        ax_line.set_title(
            f"subcarriers {state['nsub']}   rssi {state['rssi']} dBm   "
            f"median amp {state['median']:.1f}   seq {state['seq']}"
        )

        if img is None:
            img = ax_fall.imshow(
                arr, aspect="auto", origin="upper",
                interpolation="nearest", cmap="viridis",
            )
            fig.colorbar(img, ax=ax_fall, label="amplitude / median")
        else:
            img.set_data(arr)
            img.set_extent((0, arr.shape[1], arr.shape[0], 0))
            lo = float(np.percentile(arr, 2))
            hi = float(np.percentile(arr, 98))
            if hi > lo:
                img.set_clim(lo, hi)
        return []

    _anim = FuncAnimation(fig, update, interval=100, blit=False,
                          cache_frame_data=False)
    try:
        plt.tight_layout()
        plt.show()
    finally:
        ser.close()


if __name__ == "__main__":
    main()
