#!/usr/bin/env python3
"""
serial_capture.py - record a board's serial output for N seconds, with a
timestamp on every line, without idf.py monitor.

WHY THIS EXISTS
    idf.py monitor is interactive: fine for a person, awkward for a logged
    hardware test or for an AI agent that needs the output in a file. This
    script opens the port, prints and (optionally) saves every line for a
    fixed time, then exits. It was written for the first hardware bring-up
    on 2026-09-25 (Step 01 on the ESP32-C5 at COM13) and is the tool the
    RESULTS LOG in docs/TESTING.md refers to for boot / Step 03 / Step 04 /
    Step 09 captures.

USAGE
    python tools\\serial_capture.py PORT SECONDS [--out FILE] [--send CMD ...]

    python tools\\serial_capture.py COM13 20
    python tools\\serial_capture.py COM13 60 --out step04_geometry.log
    python tools\\serial_capture.py COM13 30 --send status --send people   # Step 10 console

    PORT     the board's COM port (confirm it first: esptool --port COMx chip_id)
    SECONDS  how long to record
    --out    also write the lines to FILE (UTF-8, overwritten)
    --send   a Step 10 console command, sent with a newline. The first is
             sent 4 s after start, then one every 2 s (4 + 2*i s), so the
             firmware has time to be up. Repeat --send for several.
    --reset  DISABLED - see below. Exits with an explanation.

OUTPUT
    Each line is "<seconds since this script started> <line>", e.g.
        "   1.23 I (1312) step01: heartbeat 0  (free heap ...)".
    The timestamp is relative, NOT wall-clock. For anything that must be
    aligned with another device (a chest strap, a written walk log), use
    presence_monitor.py --csv instead: it adds a host_time column.

WHY --reset IS DISABLED (2026-09-25)
    The first version pulsed RTS (with DTR low) to reset the board so the
    boot log was captured from the start. On Windows, driving RTS/DTR from
    pyserial left the C5's built-in USB-Serial-JTAG latched in reset: the
    board stayed silent until the physical RST button was pressed. Reset
    with the ESP-IDF tools instead, which know the USB-Serial-JTAG reset
    sequence: flash with build.ps1 / idf.py (the esptool call ends in
    "--after hard-reset", see the build's "Project build complete" lines),
    or run an esptool command such as the README's port check
    "esptool --port COM13 chip_id", which also ends with esptool's default
    hard reset. Then start this script. For the same reason DTR and RTS
    are both set False BEFORE the port is opened, so opening it does not
    toggle them.

    Because the reset is not captured, the first lines may be mid-boot or
    already the steady state. To see the boot banner, press RST on the
    board right after starting the script. Do not run esptool while this
    script has the port open: only one program can own it
    (docs/TESTING.md G7). Reset with esptool BEFORE starting the script
    instead, and accept that the first lines may be missed.

PORT LOSS (added 2026-09-28)
    The script used to open the port once and exit with a pyserial
    exception when it vanished, so it could not record a restart
    (docs/TESTING.md G19). Now it waits for the port at start, and if the
    port is lost it writes "# <seconds> port lost: <error>", keeps trying
    every 0.3 s and writes "# <seconds> port open: COMn" when it is back.
    Those '#' lines are skipped by dashboard.py --replay. A --send that
    falls due while the port is away is sent once it is back. A --send
    write that cannot complete within 2 s (write_timeout) raises instead
    of blocking, is logged as a port loss, and is sent again after the
    reopen (docs/TESTING.md G20; tested with a fake pyserial only).

NOTES
    * Only one program can own a COM port. Close idf.py monitor,
      presence_monitor.py and any serial terminal first.
    * The baud rate (115200 here) is ignored by the C5's USB-Serial-JTAG
      console, which is what every step's sdkconfig.defaults selects
      (CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y). It would only matter on a
      board whose console goes through a UART bridge chip; README Step 01
      says how to switch to that, and then use the console baud rate
      (CONFIG_ESP_CONSOLE_UART_BAUDRATE, 921600 in Steps 03-10).
    * Needs pyserial (pip install pyserial).
"""
import sys
import time

import serial

USAGE = "usage: python serial_capture.py PORT SECONDS [--out FILE] [--send CMD ...]"

args = sys.argv[1:]
if len(args) < 2 or args[0].startswith('-'):
    sys.exit(USAGE)
port, secs = args[0], float(args[1])
reset = '--reset' in args
out = args[args.index('--out') + 1] if '--out' in args else None
sends = [args[i + 1] for i, a in enumerate(args) if a == '--send']

if reset:
    # Disabled: pulsing RTS from pyserial on Windows left the C5's
    # USB-Serial-JTAG latched in reset (2026-09-25; needed the RST button).
    # Reset with esptool / idf.py instead.
    sys.exit('--reset is disabled; use esptool --after hard-reset')


def open_port():
    """Open the port with DTR and RTS low, set BEFORE open() so opening
    does not toggle them (docs/TESTING.md G1). None if it is not there."""
    p = serial.Serial()
    p.port, p.baudrate, p.timeout = port, 115200, 0.2
    p.write_timeout = 2.0     # a stuck --send raises, not blocks (TESTING G20)
    p.dtr = False
    p.rts = False
    try:
        p.open()
    except (serial.SerialException, OSError):
        return None
    return p


t0 = time.time()
fh = open(out, 'w', encoding='utf-8', errors='replace') if out else None


def note(text):
    """A '#' line: dashboard.py --replay and session_score.py skip it."""
    txt = '# %7.2f %s' % (time.time() - t0, text)
    print(txt, flush=True)
    if fh:
        fh.write(txt + '\n')


s = None
buf = b''
sent = 0
while time.time() - t0 < secs:
    if s is None:
        # First open, or the port vanished (a chip reset that re-enumerates
        # the C5's USB, a cable pull; docs/TESTING.md G19). Keep trying
        # rather than exit, so the boot that follows is recorded.
        s = open_port()
        if s is None:
            time.sleep(0.3)
            continue
        note('port open: %s' % port)
    try:
        if sent < len(sends) and time.time() - t0 > 4 + 2 * sent:
            s.write((sends[sent] + '\n').encode())
            sent += 1
        chunk = s.read(4096)
    except (serial.SerialException, OSError) as e:
        note('port lost: %s' % str(e)[:120])
        try:
            s.close()
        except (serial.SerialException, OSError):
            pass
        s, buf = None, b''
        continue
    if chunk:
        buf += chunk
        while b'\n' in buf:
            line, buf = buf.split(b'\n', 1)
            txt = '%7.2f %s' % (time.time() - t0, line.decode('utf-8', 'replace').rstrip('\r'))
            print(txt, flush=True)
            if fh:
                fh.write(txt + '\n')
if s is not None:
    s.close()
if fh:
    fh.close()
