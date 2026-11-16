#!/usr/bin/env python3
"""
vitals_validate.py - check Step 10's breathing and (experimental) heart-rate
estimates against a reference device you trust.

WHY THIS EXISTS
    Step 10's heart rate is EXPERIMENTAL - the firmware names the JSON key
    hr_bpm_exp so nobody mistakes it for a measurement. A heartbeat moves the
    chest ~0.1-0.5 mm; one Wi-Fi antenna sees that, if at all, buried under
    breathing harmonics and noise. The firmware only reports a value when its
    spectrum shows a clear peak, but "a clear peak" is not "the right number".
    None of the heart-rate results found for this repo's reference sweep
    (docs/REFERENCES.md section 3.4) does it with a plain FFT on one ESP32
    antenna, and this repo has never run on hardware.

    So the only honest way to use hr_bpm_exp is to record it next to a device
    that measures heart rate properly - a chest strap (best) or a finger
    pulse oximeter - and compare. This script does the comparing. The same
    goes for br_bpm, against a respiration belt or a metronome you breathe to.

RECORD
    1. Log the board (host_time is added to every row, see presence_monitor):
           python tools\\presence_monitor.py --port COM12 --csv session.csv
    2. At the same time, log the reference to a CSV with a wall-clock time
       column and a bpm column, for example:
           time,bpm
           2026-09-24T14:03:05+05:30,71
           2026-09-24 14:03:06,72         <- no UTC offset = this PC's local time
       Epoch seconds (or milliseconds) also work, and --ref-time-format takes
       any strptime format your device exports.
    3. Sit or lie still, alone, for at least 5 minutes after the board prints
       its first estimate. Then do it again after climbing some stairs, so
       the reference actually CHANGES (see "reading the result").

RUN
    python tools\\vitals_validate.py --csi session.csv --ref strap.csv --kind hr
    python tools\\vitals_validate.py --csi session.csv --ref belt.csv  --kind br --tolerance 2

    # only the rows the firmware attributed to one enrolled person
    python tools\\vitals_validate.py --csi session.csv --ref strap.csv --kind hr --person raj

    --person keeps only the rows whose vitals_of is that name (case does not
    matter, as for enrol). vitals_of is the firmware's carried-over claim -
    the last person recognised walking in ALONE this visit - not a
    measurement: walk in alone and be recognised before sitting down, or
    there will be no such rows. It needs a CSV recorded with a firmware and
    presence_monitor that know vitals_of.

READING THE RESULT
    * coverage: the share of rows (while PRESENT, with a reference sample
      nearby, and with --person attributed to that person) that carried an
      estimate at all. The firmware stays silent
      rather than guess, so low coverage is expected - but an estimator that
      is right 5% of the time is not much use either.
    * MAE / RMSE / bias / % within tolerance: over the rows that have both.
    * The best-constant line is the real bar. Resting heart rate barely
      moves, so a number that ignores the signal entirely can still score
      "90% within 5 bpm". The script therefore answers two questions
      separately: TRACKING (does the estimate follow the reference's
      changes - correlation, only meaningful if the reference varied) and
      ACCURACY (is its error below always answering the reference's own
      median). A tracker with a steady offset can pass the first and fail
      the second; an estimate that fails both is telling you nothing.
    * Rows are 0.5 s apart and the estimate only changes every ~2 s, so N
      overstates how much independent evidence there is.
    * Timing: the firmware's number describes roughly the last
      CSI_VITALS_WINDOW_S seconds (30 by default) - and the heart rate is
      also the median of its last 5 estimates - so it lags. Reference
      devices average too. Try --offset-s -15 to compare against the middle
      of the window.

Standard library only (csv, datetime, statistics, argparse, bisect).
"""

import argparse
import bisect
import csv
import datetime
import math
import statistics
import sys

EXIT_NO_OVERLAP = 2      # the two recordings never overlap in time
EXIT_NO_ESTIMATES = 3    # they overlap, but the firmware never gave a value
EXIT_NO_PERSON = 4       # --person: no PRESENT row is attributed to that name

KINDS = {
    # --kind -> (CSV column written by presence_monitor, label, unit)
    "hr": ("hr_bpm_exp", "heart rate (EXPERIMENTAL)", "bpm"),
    "br": ("br_bpm", "breathing rate", "breaths/min"),
}


def parse_time(text, fmt=None):
    """Seconds since the epoch (float) from an ISO 8601 string, epoch
    seconds/milliseconds, or a strptime format. Naive times are taken as
    this PC's local time. Returns (seconds, was_naive); raises ValueError."""
    text = text.strip()
    if not text:
        raise ValueError("empty time")
    try:
        v = float(text)
    except ValueError:
        pass
    else:
        if not math.isfinite(v):
            raise ValueError(f"not a time: {text!r}")
        # an epoch in milliseconds is ~1.7e12; in seconds ~1.7e9
        return (v / 1000.0 if v > 1e11 else v), False
    if fmt:
        dt = datetime.datetime.strptime(text, fmt)
    else:
        if text.endswith(("Z", "z")):
            text = text[:-1] + "+00:00"
        dt = datetime.datetime.fromisoformat(text)
    naive = dt.tzinfo is None
    if naive:
        dt = dt.astimezone()     # interpret as local time
    return dt.timestamp(), naive


def number(text):
    """A finite float from a CSV cell, or None for blank / null / junk.
    presence_monitor writes a JSON null as an empty cell."""
    if text is None:
        return None
    text = text.strip()
    if not text or text.lower() in ("none", "null", "nan"):
        return None
    try:
        v = float(text)
    except ValueError:
        return None
    return v if math.isfinite(v) else None


def pick_column(fieldnames, wanted, hints, exclude, fallback_index, what):
    """The column named `wanted`, else the first whose name contains one of
    `hints`, else fieldnames[fallback_index]."""
    if wanted:
        if wanted not in fieldnames:
            sys.exit(f"reference CSV has no column {wanted!r} for the {what}; "
                     f"it has: {', '.join(fieldnames)}")
        return wanted
    for name in fieldnames:
        if name == exclude:
            continue
        low = name.strip().lower()
        if any(h in low for h in hints):
            return name
    if len(fieldnames) > fallback_index and fieldnames[fallback_index] != exclude:
        return fieldnames[fallback_index]
    sys.exit(f"cannot tell which reference column is the {what}; "
             f"pass it explicitly (columns: {', '.join(fieldnames)})")


def load_reference(path, time_col, bpm_col, time_fmt):
    """Sorted lists (times, bpm) plus a description for the report."""
    try:
        f = open(path, newline="", encoding="utf-8-sig")
    except OSError as e:
        sys.exit(f"cannot open reference CSV: {e}")
    with f:
        reader = csv.DictReader(f)
        fields = reader.fieldnames or []
        if len(fields) < 2:
            sys.exit(f"{path}: need a header row with a time column and a "
                     f"bpm column (found {fields or 'nothing'})")
        tcol = pick_column(fields, time_col, ("time", "date", "stamp"),
                           None, 0, "time")
        bcol = pick_column(fields, bpm_col,
                           ("bpm", "heart", "pulse", "hr", "resp", "br", "rate"),
                           tcol, 1, "bpm value")
        rows = []
        skipped = 0
        naive_seen = False
        for row in reader:
            bpm = number(row.get(bcol))
            try:
                t, naive = parse_time(row.get(tcol) or "", time_fmt)
            except ValueError:
                skipped += 1
                continue
            # oximeters log 0 or blank while the finger is out - not data
            if bpm is None or bpm <= 0:
                skipped += 1
                continue
            naive_seen = naive_seen or naive
            rows.append((t, bpm))
    if naive_seen:
        print("note: reference times carry no UTC offset - read as this PC's "
              "local time. If the device logged on another machine or a phone "
              "in a different zone, fix its clock or the file first.")
    rows.sort()
    desc = f"time column {tcol!r}, value column {bcol!r}"
    if skipped:
        desc += f" ({skipped} rows skipped: blank, zero or unparseable)"
    return [r[0] for r in rows], [r[1] for r in rows], desc


def nearest(times, t):
    """Index of the element of sorted `times` closest to t (times non-empty)."""
    i = bisect.bisect_left(times, t)
    if i == 0:
        return 0
    if i == len(times):
        return len(times) - 1
    return i if times[i] - t < t - times[i - 1] else i - 1


def fmt_span(times):
    if not times:
        return "none"
    a = datetime.datetime.fromtimestamp(times[0]).astimezone()
    b = datetime.datetime.fromtimestamp(times[-1]).astimezone()
    return (f"{a.isoformat(timespec='seconds')} .. "
            f"{b.isoformat(timespec='seconds')}")


def main():
    ap = argparse.ArgumentParser(
        description="Compare Step 10's br_bpm / hr_bpm_exp (from a "
                    "presence_monitor.py --csv log) against a reference "
                    "device, aligned on wall-clock time.")
    ap.add_argument("--csi", required=True, metavar="SESSION_CSV",
                    help="CSV written by presence_monitor.py --csv")
    ap.add_argument("--ref", required=True, metavar="REF_CSV",
                    help="reference CSV: a time column and a bpm column")
    ap.add_argument("--ref-time-col", metavar="NAME",
                    help="reference time column (default: first column whose "
                         "name contains time/date/stamp, else the first)")
    ap.add_argument("--ref-bpm-col", metavar="NAME",
                    help="reference value column (default: first column whose "
                         "name contains bpm/heart/pulse/hr/resp/br/rate, else "
                         "the second)")
    ap.add_argument("--ref-time-format", metavar="STRPTIME",
                    help='strptime format for the reference times, e.g. '
                         '"%%d/%%m/%%Y %%H:%%M:%%S" (default: ISO 8601 or '
                         'epoch seconds/ms)')
    ap.add_argument("--kind", required=True, choices=sorted(KINDS),
                    help="hr = hr_bpm_exp (experimental), br = br_bpm")
    ap.add_argument("--tolerance", type=float, default=5.0,
                    help="an estimate within this many bpm counts as a hit "
                         "(default 5)")
    ap.add_argument("--max-skew-s", type=float, default=2.0,
                    help="pair a board row with the nearest reference sample "
                         "only if it is at most this many seconds away "
                         "(default 2)")
    ap.add_argument("--offset-s", type=float, default=0.0,
                    help="seconds added to the board's host_time before "
                         "matching (default 0; -15 centres a 30 s window)")
    ap.add_argument("--person", metavar="NAME",
                    help="keep only rows whose vitals_of is NAME (case-"
                         "insensitive): the rates the firmware attributed to "
                         "that enrolled person")
    args = ap.parse_args()

    if args.tolerance <= 0 or args.max_skew_s <= 0:
        sys.exit("--tolerance and --max-skew-s must be positive")
    person = None
    if args.person is not None:
        person = args.person.strip()
        if not person:
            sys.exit("--person needs a name")

    col, label, unit = KINDS[args.kind]
    ref_t, ref_v, ref_desc = load_reference(
        args.ref, args.ref_time_col, args.ref_bpm_col, args.ref_time_format)
    if not ref_t:
        sys.exit(f"{args.ref}: no usable reference samples ({ref_desc})")

    try:
        f = open(args.csi, newline="", encoding="utf-8-sig")
    except OSError as e:
        sys.exit(f"cannot open session CSV: {e}")
    with f:
        reader = csv.DictReader(f)
        fields = reader.fieldnames or []
        missing = [c for c in ("host_time", "present", col) if c not in fields]
        if missing:
            sys.exit(f"{args.csi} has no {', '.join(missing)} column - record "
                     f"it with the Step 10 firmware and an up-to-date "
                     f"tools/presence_monitor.py --csv")
        # Checked only for --person, so older recordings still validate.
        if person is not None and "vitals_of" not in fields:
            sys.exit(f"{args.csi}: CSV predates vitals_of - re-record with the "
                     f"current presence_monitor (and firmware) to use --person")
        rows_detect = 0
        rows_present = 0
        rows_person = 0      # present rows attributed to --person
        board_t = []
        matched = 0          # present rows with a reference sample in reach
        pairs = []           # (estimate, reference)
        for row in reader:
            state = (row.get("state") or "detect").strip()
            if state != "detect":
                continue
            rows_detect += 1
            try:
                t, _ = parse_time(row.get("host_time") or "")
            except ValueError:
                continue
            t += args.offset_s
            board_t.append(t)
            if (row.get("present") or "").strip().lower() not in ("1", "true"):
                continue
            rows_present += 1
            if person is not None:
                # presence_monitor writes a JSON null as an empty cell
                of = (row.get("vitals_of") or "").strip()
                if of.casefold() != person.casefold():
                    continue
                rows_person += 1
            i = nearest(ref_t, t)
            if abs(ref_t[i] - t) > args.max_skew_s:
                continue
            matched += 1
            est = number(row.get(col))
            if est is not None:
                pairs.append((est, ref_v[i]))

    board_t.sort()
    print(f"vitals_validate: {label} ({col}) vs {args.ref}"
          + (f", attributed to {person!r} only" if person is not None else ""))
    print(f"  reference    : {len(ref_t)} samples, {ref_desc}")
    print(f"                 {fmt_span(ref_t)}")
    print(f"  board rows   : {rows_detect} in detect, {rows_present} PRESENT, "
          + (f"{rows_person} with vitals_of = {person!r}, "
             if person is not None else "")
          + f"{matched} within {args.max_skew_s:g} s of a reference sample")
    print(f"                 {fmt_span(board_t)}"
          + (f"  (shifted {args.offset_s:+g} s)" if args.offset_s else ""))

    if person is not None and rows_person == 0:
        sys.stdout.flush()   # keep the report above the error message
        print(f"\nNO ROWS FOR {person!r}: no PRESENT row has vitals_of = "
              f"{person!r}. The firmware names whose vitals these are only "
              f"after recognising that person walking in ALONE during the "
              f"same visit, and never while two or more people were seen "
              f"(vitals \"multiple\"). Check the spelling against 'people', "
              f"and walk in alone before sitting down.",
              file=sys.stderr)
        sys.exit(EXIT_NO_PERSON)

    if matched == 0:
        sys.stdout.flush()   # keep the report above the error message
        print(f"\nNO OVERLAP: no PRESENT row of {args.csi} lies within "
              f"{args.max_skew_s:g} s of any reference sample. Check that "
              f"both recordings cover the same minutes (compare the two "
              f"spans above), that the reference clock is right, and that "
              f"somebody was actually detected (present = 1).",
              file=sys.stderr)
        sys.exit(EXIT_NO_OVERLAP)

    coverage = 100.0 * len(pairs) / matched
    print(f"  coverage     : {coverage:.1f}% of those {matched} rows carried "
          f"an estimate ({len(pairs)})")
    if not pairs:
        sys.stdout.flush()
        print(f"\nNO ESTIMATES: the recordings overlap, but the firmware "
              f"reported no {col} while PRESENT. It stays silent while you "
              f"move, for the first CSI_VITALS_MIN_S seconds of stillness, "
              f"and whenever its spectrum shows no confident peak"
              + (" (heart rate additionally needs confident breathing)"
                 if args.kind == "hr" else "") + ".",
              file=sys.stderr)
        sys.exit(EXIT_NO_ESTIMATES)

    err = [e - r for e, r in pairs]
    refs = [r for _, r in pairs]
    ests = [e for e, _ in pairs]
    n = len(pairs)
    mae = statistics.fmean(abs(x) for x in err)
    rmse = math.sqrt(statistics.fmean(x * x for x in err))
    bias = statistics.fmean(err)
    hits = 100.0 * sum(1 for x in err if abs(x) <= args.tolerance) / n

    within_label = f"within +/-{args.tolerance:g}"
    print(f"  pairs (N)    : {n}   (rows are 0.5 s apart and the estimate "
          f"changes every ~2 s, so this overstates the independent evidence)")
    print(f"  MAE          : {mae:.2f} {unit}")
    print(f"  RMSE         : {rmse:.2f} {unit}")
    print(f"  bias         : {bias:+.2f} {unit} (estimate minus reference)")
    print(f"  {within_label:<13}: {hits:.1f}%")

    corr = None
    if n >= 3:
        try:
            corr = statistics.correlation(ests, refs)
        except statistics.StatisticsError:
            corr = None      # one of the two is constant
    print("  correlation  : "
          + (f"{corr:.2f}" if corr is not None else "n/a (a series is constant)"))

    # The best constant answer: the reference's own median. It is an oracle
    # (no estimator knows that median in advance), which makes it a hard bar
    # - and the right one, because resting rates barely move and a constant
    # can score well on "% within tolerance" without seeing anything.
    const = statistics.median(refs)
    c_mae = statistics.fmean(abs(const - r) for r in refs)
    c_hits = 100.0 * sum(1 for r in refs
                         if abs(const - r) <= args.tolerance) / n
    ref_sd = statistics.pstdev(refs)
    print(f"  best constant: always {const:.1f} (the reference median, an "
          f"oracle): MAE {c_mae:.2f} {unit}, {c_hits:.1f}% {within_label}")
    print(f"  reference sd : {ref_sd:.2f} {unit} over the paired rows")

    # Two separate questions: does it FOLLOW the reference (correlation),
    # and is it ACCURATE (error against the best constant)? A tracker with
    # a steady offset passes the first and can fail the second.
    print()
    if ref_sd < 3.0:
        print(f"CAUTION: the reference barely varied (sd {ref_sd:.1f} {unit}), "
              f"so neither correlation nor accuracy can tell a tracker from "
              f"a constant here. Record a session where the rate changes "
              f"(e.g. sit down straight after climbing stairs).")
    follows = corr is not None and corr >= 0.5 and ref_sd >= 3.0
    if follows:
        print(f"TRACKING: yes - correlation {corr:.2f}; the estimate follows "
              f"the reference's changes on this recording.")
    else:
        print(f"TRACKING: no convincing evidence - correlation "
              + (f"{corr:.2f}" if corr is not None else "n/a")
              + " (0.5 or more over a varying reference would be).")
    if mae < c_mae:
        print(f"ACCURACY: beats the best constant guess "
              f"(MAE {mae:.2f} vs {c_mae:.2f} {unit}).")
    else:
        print(f"ACCURACY: does NOT beat the best constant guess "
              f"(MAE {mae:.2f} vs {c_mae:.2f} {unit})"
              + (f"; the {bias:+.2f} {unit} bias is part of that"
                 if abs(bias) >= 0.5 * mae else "") + ".")
    print(f"One session, one room, one person - repeat it before trusting "
          f"{col}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
