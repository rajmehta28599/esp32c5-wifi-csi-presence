#!/usr/bin/env python3
"""
dashboard.py - a live web page that shows HOW Step 10 decides: who is
walking, how many people are moving, and whether someone sits still and
breathes - with every threshold and every vote on screen.

WHAT IT DOES
    Reads the board's serial output (or a recorded log) and serves one page,
    tools/dashboard.html, at http://127.0.0.1:8765/. The page draws the CSI
    waterfall, the motion score against its thresholds, the gait distances
    and vote ring behind "person", the count votes behind "count", and the
    vitals confidence behind "br_bpm" / "hr_bpm_exp", each with a short
    plain-English "How it decides" text that uses the board's own live
    thresholds.

    The page understands four kinds of serial line:
        JSON {...}      the Step 08 / Step 10 status line (2 per second)
        EXPLAIN {...}   Step 10's decision events (CSI_EXPLAIN=y): config,
                        cal, amp, gait, count, vitals, presence
        CMD ...         console command answers (+ their indented lines)
        I (...) tag: .. ordinary log lines, including the "why:" lines
    Without EXPLAIN lines (older firmware, Step 08) it still works from the
    JSON line alone and says what is missing.

USAGE
    python tools\\dashboard.py                      # --port auto: the one Espressif board
    python tools\\dashboard.py --port COM13 --open  # and open the browser
    python tools\\dashboard.py --port COM13 --log session.log
    python tools\\dashboard.py --replay tools\\sample_session.log --speed 4 --open

    --port auto|COMx   auto (default) = the single USB device with Espressif's
                       vendor ID 0x303A (the C5's built-in USB-Serial-JTAG).
                       Zero or several such devices: it keeps looking and
                       says why on the page; pass --port COMx to choose.
                       --port loop:// (a pyserial echo device) tests the
                       page's command path with no board: each command
                       comes back as a line.
    --log FILE         append every line to FILE as "%7.2f <line>" (seconds
                       since this program started), the format
                       serial_capture.py writes - so FILE is --replay input.
    --replay FILE      play a recorded log instead of opening a port, at its
                       recorded pace (or --speed N times faster). Lines
                       starting with "#" are comments and are skipped. Gaps
                       longer than --max-gap seconds (default 3) are
                       shortened to it. Console commands are refused.
    --open             open the page in the default browser
    --http-port N      default 8765; when that is taken (on the bring-up PC
                       another program held it) the next free port up to
                       8774 is used and the URL printed
    --host ADDR        default 127.0.0.1 (this PC only). 0.0.0.0 lets a phone
                       on the same network open the page - and send console
                       commands (enroll / forget / recal) to the board, so
                       only do that on a network you trust.

    Stop with Ctrl+C.

THE PORT - READ THIS BEFORE CHANGING IT
    DTR and RTS are set LOW before the port is opened and never touched
    afterwards. On the C5's USB-Serial-JTAG those two lines drive the chip's
    reset and boot strapping; pulsing them from pyserial on Windows latched
    the chip in reset on 2026-09-25 until RST was pressed (docs/DECISIONS.md
    D-153, D-158; docs/TESTING.md G1). A chip reset re-enumerates the USB
    device, so the port vanishes for a moment: this program reopens it
    automatically and tells the page. Only one program can own a COM port:
    close idf.py monitor, presence_monitor.py and serial_capture.py first
    (TESTING G7).

HTTP INTERFACE (for the page, or for scripts)
    GET  /         dashboard.html (read from disk on every request)
    GET  /events   Server-Sent Events. First an "event: hello" with
                   {"mode": "serial"|"replay", "source", "port", "port_open",
                   "cmd_max", "instance"}, then one message per line:
                   {"seq": n, "t": host epoch seconds, "src": "board"|"host",
                    "line": "<the serial line>"}
                   "host" lines are this program's own notes (port open /
                   lost, replay finished). A new client first gets the last
                   EXPLAIN config and cal lines (so it knows the thresholds
                   even if detection started long ago), then the most recent
                   lines, then the live stream.
    POST /cmd      {"cmd": "status"} -> writes "status\\n" to the board.
                   Printable ASCII only, 1-63 characters (the firmware's
                   console line limit, CON_LINE_MAX 64 in main.c). Answers
                   {"ok": true, "sent": "status"}; the board's CMD reply
                   arrives on /events. Replay mode answers 409 with
                   {"ok": false, "error": "replay mode ..."}.
                   Content-Type must be application/json, and a browser
                   Origin must match the Host, so another web site open in
                   the same browser cannot post commands to the board.

NEEDS
    Python 3.8+ standard library, plus pyserial for a real port (it is in
    the ESP-IDF Python environment; otherwise: pip install pyserial).
    --replay needs no pyserial.
"""

import argparse
import collections
import datetime
import json
import os
import queue
import re
import sys
import threading
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse

HERE = os.path.dirname(os.path.abspath(__file__))
PAGE = os.path.join(HERE, "dashboard.html")

ESPRESSIF_VID = 0x303A     # USB vendor ID of the C5's built-in USB-Serial-JTAG
CMD_MAX = 63               # main.c CON_LINE_MAX (64) minus the terminator
BACKLOG = 4000             # lines replayed to a new page (~4-5 min of output)
CLIENT_QUEUE = 20000       # per-page buffer; the oldest line goes when full
PING_S = 15.0              # SSE keep-alive comment interval

# "%7.2f <line>" - what serial_capture.py, the bring-up listener and --log
# write. The line part may be missing when a tool stripped trailing blanks.
TS_RE = re.compile(r"^\s*(\d+\.\d+)(?: (.*))?$")


class Hub:
    """Fans every line out to the connected pages and keeps what a page
    opened later needs: the latest config / cal EXPLAIN lines and a
    backlog of recent lines."""

    def __init__(self, mode, source, logfile=None, echo=False):
        self.mode = mode
        self.source = source
        self.lock = threading.Lock()
        self.clients = []
        self.backlog = collections.deque(maxlen=BACKLOG)
        self.sticky = {}               # "config" / "cal" -> message
        self.seq = 0
        self.port = None
        self.port_open = False
        self.t0 = time.time()
        self.logfile = logfile
        self.echo = echo

    def hello(self):
        with self.lock:
            # instance: a page that reconnects to a restarted dashboard.py
            # must not skip lines whose seq it has already seen
            return {"mode": self.mode, "source": self.source, "port": self.port,
                    "port_open": self.port_open, "cmd_max": CMD_MAX,
                    "instance": round(self.t0, 3)}

    def publish(self, line, src="board"):
        now = time.time()
        with self.lock:
            self.seq += 1
            msg = {"seq": self.seq, "t": round(now, 3), "src": src, "line": line}
            self.backlog.append(msg)
            if src == "board" and line.startswith("EXPLAIN "):
                # Cheap test on the raw text; the page does the real parse.
                for ev in ("config", "cal"):
                    if ('"ev":"%s"' % ev) in line:
                        self.sticky[ev] = msg
            for q in self.clients:
                put_drop_oldest(q, msg)
            if self.logfile:
                try:
                    tag = "" if src == "board" else "[dashboard] "
                    self.logfile.write("%7.2f %s%s\n" % (now - self.t0, tag, line))
                    self.logfile.flush()
                except OSError:
                    pass
        if self.echo or src == "host":
            print(("  " if src == "board" else "* ") + line, flush=True)

    def note(self, text, **state):
        """A line from this program, not the board: port open / lost,
        replay finished. state updates port / port_open first."""
        with self.lock:
            if "port" in state:
                self.port = state["port"]
            if "port_open" in state:
                self.port_open = state["port_open"]
        self.publish(text, src="host")

    def subscribe(self):
        q = queue.Queue(maxsize=CLIENT_QUEUE)
        with self.lock:
            first = self.backlog[0]["seq"] if self.backlog else self.seq + 1
            # config / cal first, unless they are still in the backlog anyway
            for ev in ("cal", "config"):
                msg = self.sticky.get(ev)
                if msg and msg["seq"] < first:
                    put_drop_oldest(q, msg)
            for msg in self.backlog:
                put_drop_oldest(q, msg)
            self.clients.append(q)
        return q

    def unsubscribe(self, q):
        with self.lock:
            if q in self.clients:
                self.clients.remove(q)


def put_drop_oldest(q, msg):
    """A page that stops reading (a background tab, a sleeping laptop) must
    not grow memory or block the serial reader: drop its oldest line."""
    try:
        q.put_nowait(msg)
    except queue.Full:
        try:
            q.get_nowait()
        except queue.Empty:
            pass
        try:
            q.put_nowait(msg)
        except queue.Full:
            pass


# ---------------------------------------------------------------- serial --

class SerialLink:
    """Owns the port: reads lines, reopens after a reset, and writes the
    console commands the page posts (from this thread, between reads, so
    the Serial object is only ever used by one thread)."""

    def __init__(self, hub, port_arg):
        self.hub = hub
        self.port_arg = port_arg
        self.pending = queue.Queue()
        self.is_open = False
        self._last_note = ("", 0.0)

    def _note_limited(self, text):
        """The same 'port unavailable' text at most every 10 s - a reset
        makes the open fail several times in a row."""
        last, when = self._last_note
        if text != last or time.monotonic() - when > 10.0:
            self._last_note = (text, time.monotonic())
            self.hub.note(text, port_open=False)

    def resolve(self, list_ports):
        if self.port_arg.strip().lower() != "auto":
            return self.port_arg, None
        boards = [p for p in list_ports.comports() if p.vid == ESPRESSIF_VID]
        if len(boards) == 1:
            return boards[0].device, None
        if not boards:
            return None, ("--port auto: no Espressif USB device (VID 0x303A) found - "
                          "plug the board in with a data cable, or pass --port COMx")
        names = ", ".join(sorted(p.device for p in boards))
        return None, ("--port auto: %d Espressif devices (%s) - pass --port COMx to pick one"
                      % (len(boards), names))

    def send(self, cmd, timeout=2.0):
        """Called from an HTTP thread. Returns None when written, else an
        error text."""
        if not self.is_open:
            return "the serial port is not open - is the board plugged in? (see the status bar)"
        done = threading.Event()
        box = {}
        self.pending.put((cmd, done, box))
        if not done.wait(timeout):
            return "the serial reader did not write the command within %.0f s" % timeout
        return box.get("error")

    def _fail_pending(self, why):
        while True:
            try:
                _cmd, done, box = self.pending.get_nowait()
            except queue.Empty:
                return
            box["error"] = why
            done.set()

    def run(self):
        import serial                       # only a real port needs pyserial
        from serial.tools import list_ports
        while True:
            port, why = self.resolve(list_ports)
            if port is None:
                self._fail_pending(why)
                self._note_limited(why)
                time.sleep(1.0)
                continue
            try:
                # For "COMx" this is plain serial.Serial; it also accepts
                # pyserial URLs: --port loop:// echoes every write back, a
                # board-free test of the command path.
                ser = serial.serial_for_url(port, do_not_open=True)
                ser.baudrate, ser.timeout = 115200, 0.1
                # BEFORE open(), and never again: see "THE PORT" above.
                ser.dtr = False
                ser.rts = False
                ser.open()
            except (serial.SerialException, OSError, ValueError) as e:
                self._fail_pending("port %s unavailable" % port)
                self._note_limited("port unavailable: %s: %s" % (port, str(e)[:120]))
                time.sleep(0.5)
                continue
            self.is_open = True
            self._last_note = ("", 0.0)
            self.hub.note("port open: %s" % port, port=port, port_open=True)
            buf = b""
            last_rx = time.monotonic()
            try:
                while True:
                    while True:
                        try:
                            cmd, done, box = self.pending.get_nowait()
                        except queue.Empty:
                            break
                        try:
                            ser.write((cmd + "\n").encode("ascii"))
                            ser.flush()
                            self.hub.note(">>> sent: %s" % cmd)
                        except (serial.SerialException, OSError) as e:
                            box["error"] = "write failed: %s" % str(e)[:120]
                            done.set()
                            raise
                        done.set()
                    chunk = ser.read(4096)
                    if chunk:
                        buf += chunk
                        last_rx = time.monotonic()
                        while b"\n" in buf:
                            raw, buf = buf.split(b"\n", 1)
                            self.hub.publish(raw.decode("utf-8", "replace").rstrip("\r"))
                    # A partial line that never ends (a reset mid-line, a
                    # prompt): hand it over after 1 s, or at 4 KB.
                    if buf and (len(buf) > 4096 or time.monotonic() - last_rx > 1.0):
                        self.hub.publish(buf.decode("utf-8", "replace").rstrip("\r"))
                        buf = b""
            except (serial.SerialException, OSError) as e:
                self.is_open = False
                self._fail_pending("the port closed while sending")
                self.hub.note("port lost: %s - reopening (a chip reset re-enumerates USB)"
                              % str(e)[:120], port_open=False)
            finally:
                self.is_open = False
                try:
                    ser.close()
                except Exception:
                    pass
            time.sleep(0.3)


# ---------------------------------------------------------------- replay --

def replay(hub, path, speed, max_gap, loop):
    """Feed a recorded "%7.2f <line>" log at its own pace (/ speed). A line
    without a timestamp is sent straight after the previous one; a
    timestamp that goes backwards (two sessions appended to one --log
    file) starts a new timeline instead of waiting."""
    while True:
        hub.note("replay start: %s at %gx" % (os.path.basename(path), speed))
        prev = None
        n = 0
        with open(path, encoding="utf-8", errors="replace") as f:
            for raw in f:
                raw = raw.rstrip("\r\n")
                if raw.lstrip().startswith("#") or not raw.strip():
                    continue
                m = TS_RE.match(raw)
                if m:
                    t, line = float(m.group(1)), (m.group(2) or "")
                else:
                    t, line = prev, raw
                if prev is not None and t is not None and t > prev:
                    gap = t - prev
                    if max_gap > 0:
                        gap = min(gap, max_gap)
                    time.sleep(gap / speed)
                if t is not None:
                    prev = t
                if line.strip():
                    hub.publish(line)
                    n += 1
        hub.note("replay finished: %d lines%s" % (n, " - starting again" if loop else ""))
        if not loop:
            return
        time.sleep(3.0)


# ------------------------------------------------------------------ http --

class Handler(BaseHTTPRequestHandler):
    server_version = "csi-dashboard/1"
    hub = None          # set in main()
    link = None         # SerialLink, or None in replay mode

    def log_message(self, fmt, *args):   # keep the terminal for port events
        pass

    def _send_json(self, code, obj):
        body = json.dumps(obj).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = urlparse(self.path).path
        if path in ("/", "/index.html", "/dashboard.html"):
            try:
                with open(PAGE, "rb") as f:
                    body = f.read()
            except OSError as e:
                self._send_json(500, {"ok": False, "error": "cannot read %s: %s" % (PAGE, e)})
                return
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)
        elif path == "/events":
            self._events()
        elif path == "/favicon.ico":
            self.send_response(204)
            self.end_headers()
        else:
            self._send_json(404, {"ok": False, "error": "not found"})

    def _events(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream; charset=utf-8")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("X-Accel-Buffering", "no")
        self.end_headers()
        q = self.hub.subscribe()
        try:
            self.wfile.write(("event: hello\ndata: %s\n\n"
                              % json.dumps(self.hub.hello())).encode("utf-8"))
            self.wfile.flush()
            while True:
                try:
                    msg = q.get(timeout=PING_S)
                except queue.Empty:
                    self.wfile.write(b": ping\n\n")
                    self.wfile.flush()
                    continue
                # Batch whatever else is waiting into one write.
                parts = [msg]
                while len(parts) < 500:
                    try:
                        parts.append(q.get_nowait())
                    except queue.Empty:
                        break
                data = "".join("data: %s\n\n" % json.dumps(m, ensure_ascii=True)
                               for m in parts)
                self.wfile.write(data.encode("ascii"))
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError, OSError):
            pass                       # the page closed or reloaded
        finally:
            self.hub.unsubscribe(q)

    def do_POST(self):
        if urlparse(self.path).path != "/cmd":
            self._send_json(404, {"ok": False, "error": "not found"})
            return
        # A page from another site cannot send application/json without a
        # CORS preflight, which this server never grants; and a browser
        # always names its origin, which must be this server.
        ctype = (self.headers.get("Content-Type") or "").split(";")[0].strip().lower()
        if ctype != "application/json":
            self._send_json(415, {"ok": False, "error": "Content-Type must be application/json"})
            return
        origin = self.headers.get("Origin")
        if origin and urlparse(origin).netloc != (self.headers.get("Host") or ""):
            self._send_json(403, {"ok": False, "error": "cross-origin request refused"})
            return
        try:
            n = int(self.headers.get("Content-Length") or 0)
        except ValueError:
            n = -1
        if n < 0 or n > 1024:
            self._send_json(413, {"ok": False, "error": "body missing or too large"})
            return
        try:
            obj = json.loads(self.rfile.read(n).decode("utf-8") or "null")
        except (UnicodeDecodeError, json.JSONDecodeError):
            obj = None
        cmd = obj.get("cmd") if isinstance(obj, dict) else None
        if not isinstance(cmd, str):
            self._send_json(400, {"ok": False, "error": 'send {"cmd": "<console command>"}'})
            return
        cmd = cmd.strip()
        if not cmd or len(cmd) > CMD_MAX or any(not (0x20 <= ord(c) <= 0x7E) for c in cmd):
            self._send_json(400, {"ok": False,
                                  "error": "a command is 1-%d printable ASCII characters" % CMD_MAX})
            return
        if self.link is None:
            self._send_json(409, {"ok": False,
                                  "error": "replay mode - no board is attached, so '%s' was not "
                                           "sent. Run dashboard.py without --replay to talk to "
                                           "the board." % cmd})
            return
        err = self.link.send(cmd)
        if err:
            self._send_json(503, {"ok": False, "error": err})
        else:
            self._send_json(200, {"ok": True, "sent": cmd})


class Server(ThreadingHTTPServer):
    daemon_threads = True
    # On Windows SO_REUSEADDR lets a second server bind the same port and
    # silently share it; without it a second dashboard fails loudly.
    allow_reuse_address = os.name != "nt"


def main():
    ap = argparse.ArgumentParser(
        description="Live web dashboard for Step 10: shows how it recognises, "
                    "counts and measures vitals (serial port or a recorded log).")
    src = ap.add_mutually_exclusive_group()
    src.add_argument("--port", default="auto",
                     help="serial port, e.g. COM13; 'auto' (default) = the single "
                          "Espressif (VID 0x303A) USB device")
    src.add_argument("--replay", metavar="FILE",
                     help='play a recorded "%%7.2f <line>" log instead of a port')
    ap.add_argument("--speed", type=float, default=1.0, metavar="N",
                    help="replay N times faster than recorded (default 1)")
    ap.add_argument("--max-gap", type=float, default=3.0, metavar="S",
                    help="replay: shorten silent gaps to S seconds (0 = keep them; default 3)")
    ap.add_argument("--loop", action="store_true", help="replay: start again at the end")
    ap.add_argument("--log", metavar="FILE",
                    help='append every line to FILE as "%%7.2f <line>" (replayable)')
    ap.add_argument("--http-port", type=int, default=None, metavar="N",
                    help="web page port (default 8765; if that is taken, the next free one "
                         "up to 8774 - the URL is printed)")
    ap.add_argument("--host", default="127.0.0.1",
                    help="address to serve on (default 127.0.0.1 = this PC only)")
    ap.add_argument("--open", action="store_true", help="open the page in the browser")
    ap.add_argument("--echo", action="store_true", help="also print every line here")
    args = ap.parse_args()

    if args.speed <= 0:
        ap.error("--speed must be > 0")
    if not os.path.isfile(PAGE):
        sys.exit("dashboard.html not found next to this script (%s)" % PAGE)
    if args.replay and not os.path.isfile(args.replay):
        sys.exit("--replay: no such file: %s" % args.replay)
    if not args.replay:
        try:
            import serial  # noqa: F401  (checked early, used by SerialLink)
        except ImportError:
            sys.exit("pyserial is missing.  pip install pyserial  "
                     "(or use --replay FILE, which does not need it)")

    logfile = None
    if args.log:
        logfile = open(args.log, "a", encoding="utf-8", errors="replace")
        logfile.write("# dashboard.py %s, source %s\n" % (
            datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
            ("replay " + args.replay) if args.replay else ("port " + args.port)))
        logfile.flush()

    mode = "replay" if args.replay else "serial"
    hub = Hub(mode, args.replay or args.port, logfile, args.echo)
    Handler.hub = hub
    # An explicit --http-port is used or fails; the default walks 8765..8774
    # (on the bring-up PC another program already held 8765).
    ports = [args.http_port] if args.http_port else list(range(8765, 8775))
    httpd, err = None, None
    for port in ports:
        try:
            httpd = Server((args.host, port), Handler)
            break
        except OSError as e:
            err = e
    if httpd is None:
        tried = str(ports[0]) if len(ports) == 1 else "%d-%d" % (ports[0], ports[-1])
        sys.exit("cannot serve on %s port %s (%s) - is another dashboard running? "
                 "Pick a free one with --http-port N." % (args.host, tried, err))
    args.http_port = httpd.server_address[1]
    if len(ports) > 1 and args.http_port != ports[0]:
        print("port %d is taken by another program - using %d" % (ports[0], args.http_port), flush=True)

    if args.replay:
        Handler.link = None
        threading.Thread(target=replay, daemon=True,
                         args=(hub, args.replay, args.speed, args.max_gap, args.loop)).start()
    else:
        link = SerialLink(hub, args.port)
        Handler.link = link
        threading.Thread(target=link.run, daemon=True).start()

    shown = "127.0.0.1" if args.host in ("0.0.0.0", "") else args.host
    url = "http://%s:%d/" % (shown, args.http_port)
    print("dashboard: %s   (%s: %s)   Ctrl+C to stop"
          % (url, mode, args.replay or args.port), flush=True)
    if args.host not in ("127.0.0.1", "localhost"):
        print("WARNING: serving on %s - anyone who can reach this PC can open the page "
              "AND send console commands to the board." % args.host, flush=True)
    if args.log:
        print("logging to %s" % args.log, flush=True)
    if args.open:
        threading.Timer(0.5, webbrowser.open, args=(url,)).start()
    try:
        httpd.serve_forever(poll_interval=0.5)
    except KeyboardInterrupt:
        print("\nstopped", flush=True)
    finally:
        httpd.server_close()
        if logfile:
            logfile.close()


if __name__ == "__main__":
    main()
