#!/usr/bin/env python3
"""
kconfig_matrix.py - compile Step 10's main.c once per Kconfig switch
combination and count every warning, without touching the real build.

WHY THIS EXISTS
    Step 10's features (recognition, counting, vitals, the breathing hold,
    the activity state, the console, the JSON line, the human-readable bar
    line, the EXPLAIN lines) are switched in
    menuconfig. A switched-off feature is compiled out with #if, so a normal
    build only ever checks ONE combination. An unused helper, a variable
    that is only read under one #if, or a missing guard shows up only in
    the combination that exposes it: a missing guard as an error, an unused
    helper or variable as a warning. ESP-IDF builds with -Werror but
    exempts unused-function, unused-variable, unused-but-set-variable and
    -Wextra warnings (-Wno-error=...), so those do NOT fail a build; only a
    count of 'warning:' lines catches them, which is what this script does.
    The project rule that main.c builds warning-free in every switch
    combination (docs/DECISIONS.md D-24, D-145; docs/ARCHITECTURE.md §7.3;
    docs/TESTING.md §2.2) is checked with this script.

HOW IT WORKS
    1. Reads <project>/build/compile_commands.json (or <--build-dir>/...,
       the Linux build outside the tree, D-170) and takes the exact
       command ESP-IDF uses for <project>/main/main.c (same gcc, same -O2,
       same warning flags, same include paths).
    2. For each combination, writes a copy of <project>/build/config/
       sdkconfig.h with the switched-off symbols' #define lines removed
       (and a changed CONFIG_CSI_JSON_PERIOD_MS value), into its own
       directory under --out.
    3. Runs the compile command with that directory added as the FIRST -I,
       so the patched sdkconfig.h wins, and -o pointing into --out.
       Nothing is written under build/, so the real build is untouched.
    4. Counts 'warning:' and 'error:' lines in each compiler log.

    A NEGATIVE CONTROL runs after the combinations: it removes
    CONFIG_CSI_PING_INTERVAL_MS, which main.c needs. That compile MUST
    fail. If it does not, the patched header is not the one being read and
    every "OK" is meaningless - the script then counts it as a failure and
    exits 1.

    The patch can only REMOVE or re-value #define lines that the base
    sdkconfig.h already has. So the base build must have every switch ON
    (the Kconfig defaults); a switch that is off in your sdkconfig cannot be
    turned on here, and the script names it before it starts.

THE MATRIX (552 combinations)
    CSI_ID_ENABLE on/off  x  CSI_COUNT_ENABLE on/off
    x  vitals: on with CSI_VITALS_HOLDS_PRESENCE / on without it / off
       (vitals off also removes HOLDS_PRESENCE and MOTION_RATIO_X100,
       which depend on it in Kconfig)
    x  CSI_ACTIVITY_ENABLE on/off - only where it can exist: it depends on
       CSI_VITALS_ENABLE || CSI_ID_ENABLE || CSI_COUNT_ENABLE in Kconfig, so
       with all three off it is removed and "act1" is not generated
       (11 of the 12 id/count/vitals cells x 2, plus that one cell x 1 = 23)
    x  CSI_JSON_PERIOD_MS 500 / 0  x  CSI_HUMAN_READABLE on/off
    x  CSI_CONSOLE_ENABLE on/off  x  CSI_EXPLAIN on/off
    x  CSI_GAIN_LOG on/off - only with CSI_JSON_PERIOD_MS 500: with no JSON
       line main.c compiles the gain code out either way (CSI_HAVE_GAIN)
    = 23 x 12 x 2 (12 = 4 human/console cells x (2 gain with JSON + 1
    without)). The first 276 (CSI_EXPLAIN on is the base build) plus the
    same 276 with CSI_EXPLAIN off. Before CSI_GAIN_LOG (2026-11-03, D-176)
    the matrix had 368 combinations (23 x 8 x 2); before CSI_ACTIVITY_ENABLE (2026-10-16)
    the matrix had 192 combinations (no activity dimension); the 2026-09-25
    verification run's 96, which predate CSI_EXPLAIN, were its "exp0" half.
    NOT varied: CSI_LLTF_ONLY, the LED, the button, forced recalibration,
    CSI_ACTIVITY_RESTLESS_X100 (an int with no 'depends on', read only at
    run time) and CSI_EXPLAIN_AMP_HZ - main.c reads it only in run-time
    conditions (no #if), so 0 cannot compile anything out.

    The switch set is Step 10's. --project lets you point it at a copy of
    Step 10 (or a later step built on it); for a project whose sdkconfig.h
    lacks these symbols the script says which ones are missing, because a
    missing symbol makes that dimension of the matrix a no-op.

LIMITS - what a clean matrix does NOT prove
    * It compiles main.c only (-c), so it cannot catch LINK errors (e.g. a
      module function referenced under a switch whose .c is not built).
      Full builds with switches off cover that; README "Verification
      status" lists the ones done.
    * It says nothing about runtime behaviour.

PREREQUISITES
    * The project has been built once (build/compile_commands.json and
      build/config/sdkconfig.h exist):  .\\build.ps1 step10_recognise_count_vitals
      On Ubuntu: tools/linux_idf.sh build step10_recognise_count_vitals, then
      pass --build-dir ~/esp-build/<abs project path> (DECISIONS D-171).
    * ESP-IDF's environment is active in this shell, so the toolchain the
      compile command names can run:
          & C:\\esp\\v6.0.2\\esp-idf\\export.ps1
      (PowerShell - ESP-IDF refuses Git Bash / MSYS.)

USAGE
    python tools\\kconfig_matrix.py
    python tools\\kconfig_matrix.py --project step10_recognise_count_vitals --jobs 6
    python tools\\kconfig_matrix.py --out D:\\tmp\\kmatrix      # keep the logs somewhere else
    python3 tools/kconfig_matrix.py --build-dir ~/esp-build/mnt/vol1/ESP32C5_CSI/step10_recognise_count_vitals
                                   # Linux: tools/linux_idf.sh builds outside the tree

    Exit code 0 = negative control failed as it must AND every combination
    compiled with 0 warnings / 0 errors. Exit code 1 otherwise. The last
    line reads:  combos: 552, with warnings/errors: <N>
    Each combination's log is <out>/<name>/log.txt.

Standard library only. Origin: the verification scratch script of
2026-09-25 (then hard-wired to D:/ESP32C5_CSI/step10_recognise_count_vitals),
generalised to repo-relative paths with the combinations and checks
unchanged; the CSI_EXPLAIN dimension was added later the same day.
"""

import argparse
import concurrent.futures as cf
import itertools
import json
import os
import re
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Every symbol the matrix switches. Used only to warn when a project's
# sdkconfig.h lacks one (that dimension would then change nothing).
SWITCHED = (
    'CONFIG_CSI_ID_ENABLE', 'CONFIG_CSI_COUNT_ENABLE', 'CONFIG_CSI_VITALS_ENABLE',
    'CONFIG_CSI_VITALS_HOLDS_PRESENCE', 'CONFIG_CSI_VITALS_MOTION_RATIO_X100',
    'CONFIG_CSI_JSON_PERIOD_MS', 'CONFIG_CSI_HUMAN_READABLE', 'CONFIG_CSI_CONSOLE_ENABLE',
    'CONFIG_CSI_EXPLAIN', 'CONFIG_CSI_ACTIVITY_ENABLE', 'CONFIG_CSI_GAIN_LOG',
)
NEG_SYMBOL = 'CONFIG_CSI_PING_INTERVAL_MS'


def parse_args():
    ap = argparse.ArgumentParser(
        description="Compile <project>/main/main.c once per Kconfig switch "
                    "combination (Step 10's switches) with ESP-IDF's own "
                    "compile command, and count warnings/errors.")
    ap.add_argument("--project", default="step10_recognise_count_vitals",
                    help="step folder under the repo root (default: "
                         "step10_recognise_count_vitals)")
    ap.add_argument("--out", default=None,
                    help="where the per-combination sdkconfig.h, .obj and "
                         "log.txt go (default: <temp>/kconfig_matrix/<project>; "
                         "never under build/)")
    ap.add_argument("--build-dir", default=None,
                    help="the project's build directory (default: "
                         "<project>/build; on Linux tools/linux_idf.sh builds "
                         "into ~/esp-build<abs project path>)")
    ap.add_argument("--jobs", type=int, default=6,
                    help="parallel compiles (default 6)")
    return ap.parse_args()


def main():
    args = parse_args()
    proj = os.path.join(REPO, args.project)
    build = os.path.abspath(os.path.expanduser(args.build_dir)) if args.build_dir \
        else os.path.join(proj, 'build')
    out_root = os.path.abspath(args.out) if args.out else \
        os.path.join(tempfile.gettempdir(), 'kconfig_matrix', args.project)
    if os.path.normcase(out_root).startswith(os.path.normcase(os.path.abspath(build)) + os.sep):
        sys.exit("--out must not be inside the build dir (%s): the real build "
                 "must stay untouched" % build)
    os.makedirs(out_root, exist_ok=True)

    cc_path = os.path.join(build, 'compile_commands.json')
    h_path = os.path.join(build, 'config', 'sdkconfig.h')
    for p in (cc_path, h_path):
        if not os.path.isfile(p):
            sys.exit("%s not found - build the project once first "
                     "(Windows: .\\build.ps1 %s; Linux: tools/linux_idf.sh "
                     "build %s, then pass --build-dir)"
                     % (p, args.project, args.project))

    with open(cc_path, encoding='utf-8') as f:
        cc = json.load(f)
    want = '/' + args.project + '/main/main.c'
    cmd = None
    for e in cc:
        if e['file'].replace('\\', '/').endswith(want):
            cmd = e.get('command')
    if not cmd:
        sys.exit("%s/main/main.c is not in compile_commands.json (or has no "
                 "'command' entry)" % args.project)

    with open(h_path, encoding='utf-8') as f:
        base_h = f.read()
    missing = [s for s in SWITCHED if not re.search(r'#define %s\b' % s, base_h)]
    if missing:
        print("WARNING: not defined in %s's config/sdkconfig.h: %s\n"
              "  Either the project lacks them or they are switched OFF in its "
              "sdkconfig. The matrix can only switch symbols off, so those "
              "dimensions are no-ops: rebuild with them on for a full check."
              % (args.project, ', '.join(missing)))

    m = re.search(r'riscv32-esp-elf-gcc(\.exe)?', cmd)
    if not m:
        sys.exit("compile command does not name riscv32-esp-elf-gcc - is this an ESP32-C5 build?")
    if m.group(1) and sys.platform != 'win32':
        sys.exit("compile command names a Windows .exe compiler: that is the in-tree "
                 "Windows build/. On Linux pass --build-dir ~/esp-build<abs project path>")
    gcc_end = m.end()
    obj_re = re.compile(r' -o \S+main\.c\.obj ')
    if not obj_re.search(cmd):
        sys.exit("compile command has no '-o ...main.c.obj' to redirect")

    def patched(cfg):
        """cfg: dict symbol -> None (remove the #define) or a value string."""
        lines = []
        for ln in base_h.splitlines():
            mm = re.match(r'#define (CONFIG_\w+)\b', ln)
            if mm and mm.group(1) in cfg:
                v = cfg[mm.group(1)]
                if v is None:
                    continue
                ln = '#define %s %s' % (mm.group(1), v)
            lines.append(ln)
        return '\n'.join(lines) + '\n'

    def combo_cfg(idf, vit, holds, cnt, act, json_ms, human, console, explain, gain):
        c = {}
        if not idf:
            c['CONFIG_CSI_ID_ENABLE'] = None
        if not cnt:
            c['CONFIG_CSI_COUNT_ENABLE'] = None
        if not vit:
            c['CONFIG_CSI_VITALS_ENABLE'] = None
            c['CONFIG_CSI_VITALS_HOLDS_PRESENCE'] = None      # depends on VITALS
            c['CONFIG_CSI_VITALS_MOTION_RATIO_X100'] = None   # depends on VITALS
        elif not holds:
            c['CONFIG_CSI_VITALS_HOLDS_PRESENCE'] = None
        # depends on VITALS || ID || COUNT: gone whenever all three are
        if not act or not (idf or cnt or vit):
            c['CONFIG_CSI_ACTIVITY_ENABLE'] = None
        c['CONFIG_CSI_JSON_PERIOD_MS'] = str(json_ms)
        if not human:
            c['CONFIG_CSI_HUMAN_READABLE'] = None
        if not console:
            c['CONFIG_CSI_CONSOLE_ENABLE'] = None
        if not explain:
            c['CONFIG_CSI_EXPLAIN'] = None
        if not gain:
            c['CONFIG_CSI_GAIN_LOG'] = None
        return c

    def name(idf, vit, holds, cnt, act, json_ms, human, console, explain, gain):
        return 'id%d_vit%d_hold%d_cnt%d_act%d_json%d_hr%d_con%d_exp%d_gain%d' % (
            idf, vit, holds, cnt, act, json_ms, human, console, explain, gain)

    combos = []
    for explain in (1, 0):
        for idf, cnt in itertools.product((1, 0), (1, 0)):
            for vit, holds in ((1, 1), (1, 0), (0, 0)):
                # act=1 cannot exist without one of its prerequisites
                acts = (1, 0) if (idf or cnt or vit) else (0,)
                for act in acts:
                    for json_ms, human, console in itertools.product((500, 0), (1, 0), (1, 0)):
                        # CSI_GAIN_LOG does something only with a JSON line
                        for gain in ((1, 0) if json_ms else (1,)):
                            combos.append((idf, vit, holds, cnt, act, json_ms, human, console,
                                           explain, gain))
    assert len(combos) == len(set(combos)) == 552, len(combos)

    def run(c, extra=None, n=None):
        n = n or name(*c)
        d = os.path.join(out_root, n)
        os.makedirs(d, exist_ok=True)
        cfgd = combo_cfg(*c)
        if extra:
            cfgd.update(extra)
        with open(os.path.join(d, 'sdkconfig.h'), 'w', newline='\n') as f:
            f.write(patched(cfgd))
        obj = os.path.join(d, 'main.c.obj')
        x = cmd[:gcc_end] + ' -I' + d.replace('\\', '/') + cmd[gcc_end:]
        x = obj_re.sub(lambda _m: ' -o ' + obj.replace('\\', '/') + ' ', x)
        x += ' -fdiagnostics-color=never'
        r = subprocess.run(x, cwd=build, capture_output=True, shell=(os.name != 'nt'))
        text = (r.stdout + r.stderr).decode('utf-8', 'replace')
        with open(os.path.join(d, 'log.txt'), 'w', encoding='utf-8') as f:
            f.write(text)
        nw = len(re.findall(r'\bwarning:', text))
        ne = len(re.findall(r'\berror:', text))
        return n, r.returncode, nw, ne, text

    print("project : %s" % proj)
    print("out     : %s" % out_root)
    with cf.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as ex:
        res = list(ex.map(run, combos))

    # Negative control: a symbol main.c needs is removed - this MUST fail,
    # proving the patched sdkconfig.h (not build/config's) is the one read.
    neg = run((1, 1, 1, 1, 1, 500, 1, 1, 1, 1), {NEG_SYMBOL: None}, 'negative_control')
    neg_ok = neg[1] != 0 and neg[3] > 0
    print('negative control (%s removed): rc=%d errors=%d -> %s'
          % (NEG_SYMBOL, neg[1], neg[3], 'fails as expected' if neg_ok else 'DID NOT FAIL'))
    bad = 0 if neg_ok else 1
    for n, rc, nw, ne, text in res:
        ok = rc == 0 and nw == 0 and ne == 0
        bad += 0 if ok else 1
        print('%-50s rc=%d warnings=%d errors=%d %s' % (n, rc, nw, ne, 'OK' if ok else 'FAIL'))
        if not ok:
            for ln in text.splitlines():
                if 'warning:' in ln or 'error:' in ln:
                    print('    ' + ln)
    print('combos: %d, with warnings/errors: %d' % (len(res), bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
