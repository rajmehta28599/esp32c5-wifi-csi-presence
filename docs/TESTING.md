# Testing: what has been checked, how to check it, and the hardware plan

This file covers how this project is tested and what the tests do and do
not show. It is for anyone, human or AI, who picks the project up. Keep it
up to date: every run of any kind goes into the **Results log** (§5), with a
date.

Rules this file follows (please keep them):

- **Every claim has a source**: a `file:line`, a document section, or a dated
  measurement. Line numbers are as of 2026-09-25 and will drift, so section
  names are given as well. The `test_e2e.c` citations in §0-§3 were
  re-mapped on 2026-11-01 and say so; the `test_vitals.c` citations added
  on 2026-11-01 (§3.1-§3.3, (9)-(12b)) are as of that date, and those in
  §3.1-§3.3 were re-mapped on 2026-11-03 (after (6b) moved everything below
  line 652) and say so.
- **Nothing is called "validated"** unless a measurement in §5 says so.
  Recognition, counting, heart rate and the activity state are experimental.
  No accuracy of any kind has been measured yet. An offline replay of
  someone else's recordings (§5.3) is not a measurement of this firmware on
  the C5.
- **Pass marks are labelled by origin.** A mark is either an accepted bar
  from [`COMPARISON.md` §3](COMPARISON.md#3-standards-the-bar-each-output-would-have-to-meet),
  or a **project mark**: a number this project picked on 2026-09-25 because no
  standard sets one. Project marks can be changed, but log the change in §5.

Related documents: [`../README.md`](../README.md) is the tutorial and holds
the *Verification status* list at the top. [`COMPARISON.md`](COMPARISON.md)
compares this project with prior art and sets out the standards (§3).
[`REFERENCES.md`](REFERENCES.md) is the annotated literature list, with C5
CSI facts in §1 and a prioritised to-do list in §6.
[`ARCHITECTURE.md`](ARCHITECTURE.md) explains how the code runs and how to
change it safely; [`DECISIONS.md`](DECISIONS.md) records why each choice
was made (D-numbers). README is cited here by section heading, because its
line numbers move.

---

## 0. Where things stand (2026-11-03)

| Level | Status | Evidence |
|---|---|---|
| Host unit tests (Step 10) | **10 suites, 11,705 checks, 0 failures** on Ubuntu with gcc `-Werror` (2026-11-03, after D-175 (F2: linear fill of empty vitals bins) and D-176 (receiver gain logging) and their review: `vitals` 2,509, `gain` 22 (new suite), `e2e` 287; 11,673 in 9 suites on 2026-11-01). The run also prints **12 KNOWN LIMIT lines** (vitals 7, e2e 5): shortcomings the tests measure and print instead of failing (§3.1; §3.3 items 10 and 11); D-175 took the three (12b) silence lines out, and those rows now `CHECK` a rate bound. On MSVC the last run is 8 suites, 11,453 checks (2026-09-28); `run_tests.ps1` lists the activity and gain suites but has **not been run** since, so MSVC has compiled neither of them, nor the test code of 2026-10-30 and 2026-11-03. No host test exercises `main.c` itself: it is not host-compiled | §5.2 rows 2026-11-01 and 2026-11-03 |
| End-to-end mirror (`test_e2e`) | passes on the default room (seed 5000, 287 checks); **fails e1 in 2 of 9 synthetic rooms** (5004 and 5007: `273 checks, 1 failures`, the same e1 failure as on 2026-09-25; the other seven 287 / 0; swept 2026-11-01, and again with F2 on 2026-11-03 with the same result). Since 2026-10-30, scenario (f) also runs an **assumed** multiplicative AGC receiver and a shadow of Step 08's static (D) branch that feeds nothing; (f) passes in all nine rooms | §2.1; §3.3 items 2 and 11; §5.2 rows 2026-11-01 and 2026-11-03 |
| Offline replay of real recordings (**not** the C5) | 2026-11-01: a host port of Step 08's state machine, kept outside the repo and linked with Step 10's unchanged `csi_dsp.c`, `motion_gate.c` and `activity.c`, run on 6 public scripted trials of ESP32-S3 node-to-node CSI (one adult, two rooms, 56 links × 3 packet modes = 1,008 runs). On the median link PRESENT covered 1.00 of the seated and walking time, 0.03-0.10 of the first empty phase and 0.08-0.16 of the second; 20-50 onsets per hour in the empty phases; 11-17 % of runs stayed PRESENT through the second empty phase. **Not a C5 accuracy figure**: different chip, sounding, tone set and packet timing | §5.3; §3.3 item 12; DECISIONS D-174 |
| Kconfig switch matrix (`main.c`) | **552 combinations** (23 × 12 × 2: the `CSI_GAIN_LOG` dimension, varied only where a JSON line exists, §2.2), 0 warnings, and the negative control fails as it must (2026-11-03, Ubuntu v6.1 build, before and after that day's review). 368 on 2026-10-16 (no gain switch); the 192 of 2026-09-28 (Windows) had no activity switch. Not yet run on Windows | §5.2 |
| Scripted §4 sessions (`tools/session_runner.py`, `tools/session_score.py`) | tools only, tested offline on 2026-09-28: dry runs, a replayed 2026-09-28 soak log and a fake pyserial module; the scorer also scored the recorded 2026-09-28 soak and 2026-09-25 boot logs. On 2026-10-16 the scorer gained activity scoring (version 3), checked on a synthetic log and on the 10 earlier logs, whose scores did not change. On 2026-11-03 it gained a GAIN block for the D-176 keys (version 4, then version 5 after that day's review; logging only, no mark), checked on synthetic logs and on the recorded logs, whose output did not change except the version line. **Never run on the C5**, no §4 session results | §5.2, §2.7; DECISIONS D-169, D-172, D-176 |
| Firmware build, Step 10 | Windows v6.0.2: 0 warnings, 0 errors, 0xef360, 53 % of the partition free (2026-09-28, after D-168 and its refinements). **Flashed on 2026-09-28 at 19:00**; the image it replaced, 0xef240, was built before D-167 and D-168, also 0 warnings. Ubuntu v6.1 with the activity state (D-172): **0xf56d0**, 52 % free, 0 warnings (2026-10-16), **not flashed**, so the board's 0xef360 has no `activity` keys. With F2 and gain logging (D-175, D-176): **0xf5a90**, 52 % free, 0 warnings (2026-11-03, after the review; 0xf59b0 before it), **not flashed**, so the board's image has none of the gain keys either (`agc`, `fft`, `gain_chg`, appended after `episode_s` on the detect line). No Windows v6.0.2 build of the activity or gain sources yet | §5.2; §5.1 row 2026-09-28 (19:00) |
| Firmware build, Steps 01 / 03 / 04 | Step 01 rebuilt 2026-09-25 (warning count not recorded, §5.1 row 0a). Steps 03 / 04: 0 `warning:` lines, rebuilt 2026-09-25 with the user's laptop-hotspot Wi-Fi credentials | §5 |
| Firmware build, Step 08 / root | 0 warnings each, 2026-09-28 after D-168 and its refinements. Step 08: 0xdb340, built with `CSI_LLTF_ONLY` = y (its `build/config/sdkconfig.h` says so), the capture this laptop hotspot needs. Root: 0xdb350 with its own `sdkconfig`, `CSI_LLTF_ONLY` off. On Ubuntu v6.1 both are 0xe0600, 0 warnings, unchanged on 2026-10-16 (Step 08's `main.c` untouched since 2026-09-28). Neither has been flashed or run (the board has Step 10's 0xef360 since 2026-09-28 19:00). Not rebuilt on 2026-11-03: their sources did not change (D-175 and D-176 are Step 10 only) | §5.2 |
| Firmware build, Steps 02 / 05 / 06 / 07 / 09 | **no build of the current sources**: last built 2026-09-07; their `main.c` changed on 2026-09-22 | `build/*.bin` and `main/main.c` file dates, checked 2026-09-25. Rebuild before relying on them, and log it in §5.2 |
| Hardware | Step 01 (2026-09-25). Steps 03 and 04 on a laptop hotspot (2026-09-25). **Step 10**: several boots on 2026-09-25 with **restarts nobody explained**; on 2026-09-28 the D-164/D-165 image ran a **15-min soak at 50 Hz with no restart** (its numbers: §5.1 row 2026-09-28 13:51–14:07). At 19:00 on 2026-09-28 the D-168 image 0xef360 was flashed, with no `Write timeout`, and booted with the hotspot off: its designed restart after the failed join read `last reset: SW (3)`, and COM13 stayed open across it. **The D-168 channel check has not run on the board** (no Wi-Fi join yet; §5.1 rows 2026-09-28 18:57 to 19:01:33). **Step 08 has never run**. A board on `/dev/ttyACM0` (USB 303a:1001, serial `38:44:BE:BF:9D:9C`, 2026-11-03; the session's own report, no port listing saved) is neither recorded board; it was not touched (§5.2 row 2026-11-03, DECISIONS D-152) | §5.1, §5.2 |
| CSI on hardware | yes, 106-byte 12-bit L-LTF at about 50/s on channel 1 (2026-09-25) and channel 6 (2026-09-28), laptop hotspot only; no HT/HE CSI yet | §5.1 |
| Accuracy of anything | **not measured**. No run so far recorded who was in the room. The 2026-11-01 replay scored a port of Step 08 on another group's ESP32-S3 recordings: evidence about the arithmetic on real channels, not an accuracy of this firmware or board | COMPARISON.md §1 ("Not measured yet"); §5.1; §5.3 |

---

## 1. Test levels: what each one shows and what it does not

| # | Level | What runs | What a pass shows | What a pass does NOT show |
|---|---|---|---|---|
| L1 | **Host unit tests** (`step10_recognise_count_vitals/test/test_{dsp,ncc,vitals,gait,count,gate,pvitals,activity,gain}.c`) | The nine analytics modules (`activity.c` since 2026-10-16, `csi_gain.c` since 2026-11-03), compiled with MSVC `/std:c11 /W4 /WX` or gcc `-Werror` on the PC and fed **synthetic** signals (for `activity`, scripted per-packet decisions; for `gain`, raw header bytes) whose answers are known (run_tests.ps1:1-25) | The arithmetic is right: the FFT matches a direct DFT, known breathing and heart rates come back, and synthetic walkers and 1-vs-2 movers separate. The `vitals_of` bookkeeping follows its rules, and the code builds warning-free under a second compiler. Since 2026-10-30 `test_vitals` also measures heart-rate failure modes and prints the ones `vitals.c` has today as KNOWN LIMIT lines (§3.3 item 10). Since 2026-11-03 `test_gain` checks which `rx_ctrl` bytes `csi_gain.c` reads and how it counts gain changes | That any of it works on a real person, room or C5 (test_util.h:4-7). That bytes 22 and 23 **are** the receiver gain on a C5 (test_gain.c:1-8). Real CSI noise is neither white nor independent, and real walkers are not separable by construction (test_e2e.c:106-113 on 2026-11-01) |
| L2 | **End-to-end mirror** (`test_e2e.c`) | int8 CSI → a **copy** of `main.c`'s front end (amplitude, AGC normalisation, calibration, EMAs, Step 08's motion flag and presence, the motion gate) → every module → a copy of `main.c`'s per-packet Step 10 decisions; since 2026-10-30 also (f): an assumed multiplicative AGC receiver in front of the int8 rounding and a shadow of Step 08's static branch that feeds nothing (test_e2e.c:1-121 on 2026-11-01) | The modules fit together: shapes, units, scaling, window and block timing at a *measured* packet rate, and the order of the Step 10 glue (test_e2e.c:100-105 on 2026-11-01). (f) shows how the mirrored front end reacts to the assumed receiver, not how a real receiver behaves | That `main.c` itself behaves this way, because it is not compiled on the PC (run_tests.ps1:17). Several paths are not modelled at all (§3.3) |
| L3 | **Kconfig switch matrix** (`tools/kconfig_matrix.py`) | `main.c` compiled 552 times (368 before `CSI_GAIN_LOG`, 192 before `CSI_ACTIVITY_ENABLE`, 96 before `CSI_EXPLAIN` was added to the matrix, §2.2) with the firmware's own gcc command line, each time with a patched `sdkconfig.h` | No switch combination of recognition, counting, vitals, the hold, the activity state, JSON, bar line, console, `CSI_EXPLAIN` or the gain log produces a warning or error in `main.c` | Link errors, because it compiles with `-c` only. Full builds with switches off cover linking (README *Verification status — please read*). It shows nothing about runtime. `CSI_LLTF_ONLY`, the LED, the button and forced recalibration are not varied |
| L4 | **Firmware build** (`build.ps1 <step>`) | ESP-IDF v6.0.2, target `esp32c5`, `-O2`, with warnings counted | The image builds and fits the partition. For Step 10, the static RAM figures come from the linker map | Anything about runtime heap, timing, CSI or accuracy |
| L5 | **Hardware** (§4) | The real C5, router and room | Only what the specific phase measured, in that room, with those people, on that date | Anything beyond that session. One good session is a result, not a validation |

**The order matters.** L1–L4 can all be green while L5 fails outright. For
example, Step 03 reads 0 CSI packets per second if the router answers pings
with 11g frames (step03_csi_hello/main/main.c:19-25). No host test can see
that.

---

## 2. Exact commands

All commands are for **PowerShell** on Windows, run from `D:\ESP32C5_CSI`.
ESP-IDF refuses Git Bash / MSYS (README *Setting up your shell*).

### 2.0 Before you call a change safe

Run what matches your change. Every row assumes the rows above it where
they apply. `build.ps1` changes directory, so call it by its full path (G3).

| You changed | Run (PowerShell) | Pass |
|---|---|---|
| anything in `step10_recognise_count_vitals` | `& D:\ESP32C5_CSI\build.ps1 step10_recognise_count_vitals`, then `powershell -ExecutionPolicy Bypass -File D:\ESP32C5_CSI\step10_recognise_count_vitals\test\run_tests.ps1` | `Project build complete` and 0 `warning:` lines (§2.3); `ALL HOST TESTS PASSED`, exit code 0 |
| an `#if`, a guard macro (`main.c` block "Which Step 10 parts are compiled in", lines 239-344 on 2026-09-28, and `CSI_HAVE_GAIN` at `:423-428` on 2026-11-03; grep `#define CSI_HAVE_`), a Kconfig symbol, or code compiled only under a switch (Step 10) | also `python tools\kconfig_matrix.py`, after a Step 10 build with every switch on (§2.2) | `combos: 552, with warnings/errors: 0`, exit code 0 (368 before 2026-11-03, 192 before 2026-10-16) |
| Step 10's per-packet order, or a default that `test_e2e.c` copies (test_e2e.c:20-21, 138-168 on 2026-11-01) | update `gl_packet()` and the constants in `test_e2e.c` in the same change, then rerun the host tests. `test_pvitals.c` (k) is a second mirror that already predates D-108 (§3.3 item 3); update it too if you rely on it | e2e passes at seed 5000. Seeds 5004 and 5007 fail e1 as §3.3 records (273 checks, 1 failure each on 2026-11-01 and 2026-11-03); any other change in a seed sweep (§2.1) is new. A changed KNOWN LIMIT line (§3.1) is also a change: say why |
| `step08_final_human_detector` | build Step 08 (`& D:\ESP32C5_CSI\build.ps1 step08_final_human_detector`); build the root: `& C:\esp\v6.0.2\esp-idf\export.ps1; Set-Location D:\ESP32C5_CSI; idf.py build` (run `idf.py set-target esp32c5` first only if `D:\ESP32C5_CSI\sdkconfig` is missing, DECISIONS D-08). `build.ps1` cannot build the root: it exits with "No step given" (build.ps1:201). Then follow ARCHITECTURE §7.7 | 0 `warning:` lines in both |
| boilerplate copied across steps (ARCHITECTURE §7.6) or `csi_baseline.h` | build every step that holds a copy, calling `build.ps1` by full path (G3). For `csi_baseline.h`, first `diff` the four copies (Steps 06, 07, 08, 10) | 0 `warning:` lines in each |
| `tools/*.py` | `python -m py_compile tools\<name>.py`, then a run on a real capture; `session_runner.py` and `session_score.py` can also be checked offline (§2.7) | no error |
| an `EXPLAIN` event or key (`main.c`), `tools/dashboard.py`, `tools/dashboard.html` or `tools/sample_session.log` | update ARCHITECTURE §3.7 and the parser in `dashboard.html` in the same change (ARCHITECTURE §3.7, *Changing it*); `python -m py_compile tools\dashboard.py`; the replay checks of §2.6. A `main.c` change also needs the rows above | the §2.6 replay checks pass |
| always | add a §5.2 row if a number in README *Verification status — please read* changes. Say "builds and passes host tests", never "works": L1–L4 prove nothing about hardware (§1) | – |

### 2.1 Host tests (L1 + L2)

These need Visual Studio 2022 with the C++ workload (run_tests.ps1:55-62 on 2026-11-03).

```powershell
powershell -ExecutionPolicy Bypass -File step10_recognise_count_vitals\test\run_tests.ps1
powershell -ExecutionPolicy Bypass -File step10_recognise_count_vitals\test\run_tests.ps1 -Only vitals
#   -Only takes one of: dsp | ncc | vitals | gait | count | gate | pvitals | activity | gain | e2e
```

The suites run in that order; `activity` (D-172, 2026-10-16) is registered
in both runners (`run_tests.ps1` `$tests`, `run_tests.sh` `names`/`mods`),
and `e2e` now also links `activity.c`. `gain` (D-176, 2026-11-03) is
registered the same way, between `activity` and `e2e` (`run_tests.ps1:49-50`,
`run_tests.sh:62`, `:74-75`); it links only `csi_gain.c`. Under MSVC neither
new suite has **been run yet** (§5.2, 2026-10-16 and 2026-11-03).

- Each suite prints `test_<name>: <N> checks, <F> failures -> PASS|FAIL`
  (test_util.h, `test_summary`). The run ends with `ALL HOST TESTS PASSED`
  and exit code 0, or `FAILED: <names>` and exit code 1 (run_tests.ps1:88-93 on 2026-11-03).
- A **compile warning is a failure** (`/W4 /WX`), the same standard the
  firmware build is held to.
- Binaries go to `step10_recognise_count_vitals\test\bin\`, which is
  gitignored.
- **To add a suite**, add `test_<name>.c` and an entry in the `$tests` map in
  `run_tests.ps1` that lists the module `.c` files it links against
  (run_tests.ps1:38-53 on 2026-11-03), and the same entry in `run_tests.sh`'s `mods` table.

**On Ubuntu** (gcc; DECISIONS D-171), the same suites and order:

```bash
bash step10_recognise_count_vitals/test/run_tests.sh             # gcc -std=c11 -O2 -Wall -Wextra -Werror
bash step10_recognise_count_vitals/test/run_tests.sh --only e2e  # one suite; --out DIR, --no-werror
```

Binaries go to `${TMPDIR:-/tmp}/esp32c5_host_tests`, never into the NTFS
tree; an `--out` inside `test/` is refused. A gcc warning is a failure, as
on MSVC. First run 2026-10-16 (§5.2): 11,453 checks, the Windows counts.
After the activity state the same day: 9 suites, 11,515 checks (§5.2).
After the heart-rate and AGC tests of 2026-10-30 and their review: 9
suites, 11,673 checks and 15 KNOWN LIMIT lines (2026-11-01, §5.2). After
F2 and gain logging (D-175, D-176) and their review: 10 suites, 11,705
checks and 12 KNOWN LIMIT lines (2026-11-03, §5.2).

**Other synthetic rooms (e2e seed sweep).** `test_e2e` scenario (e) uses room
seed `E_SEED`, 5000 by default. It can be overridden with `/DE_SEED`
(test_e2e.c:1597-1599 on 2026-11-01; scenario (f)'s `AGC_SEED` follows it,
:2501-2503), but neither runner has an option for that. This
recipe uses the same flags and module list as `run_tests.ps1`. It was run on
2026-09-25 for seeds 5004 and 5007 and gave the expected e1 failures (§3.3):

```powershell
$vsi = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer'
$env:PATH = "$vsi;$env:PATH"
$vs = & "$vsi\vswhere.exe" -latest -products * -property installationPath
$t = 'D:\ESP32C5_CSI\step10_recognise_count_vitals\test'
$m = 'D:\ESP32C5_CSI\step10_recognise_count_vitals\main'
$seed = '5004u'                                   # 5000u..5008u were swept on 2026-09-25
$o = Join-Path $env:TEMP "e2e_seed_$seed"; New-Item -ItemType Directory -Force $o | Out-Null
$mods = 'csi_dsp.c','csi_ncc.c','vitals.c','gait_id.c','count_est.c','motion_gate.c','person_vitals.c','activity.c' | ForEach-Object { "`"$m\$_`"" }
$cl = "cl /nologo /std:c11 /W4 /WX /wd4996 /O2 /fp:precise /D_CRT_SECURE_NO_WARNINGS /DE_SEED=$seed " +
      "/I`"$m`" /I`"$t`" `"$t\test_e2e.c`" $($mods -join ' ') /Fe:`"$o\test_e2e.exe`" /Fo:`"$o\\`""
cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && $cl"
& "$o\test_e2e.exe"
```

`/DGATE_RATIO=3.0f` can be added the same way to study the motion-gate
threshold (test_e2e.c:157-159 on 2026-11-01).

The `$mods` list gained `activity.c` on 2026-10-16: `test_e2e.c` calls
`act_push` since then, so without it the link fails. A 9-room sweep
(5000-5008) was run on Ubuntu that day, but **before** the review fixes of
D-172 (`e2e_seeds_5000-5008.txt`, 18:15, in
`~/ESP32C5_CSI_logs/2026-10-16_activity/`): rooms 5004 and 5007 still fail
e1 (`test_e2e: 228 checks, 1 failures`), the other seven pass. Its
activity figures predate the fixes; no sweep of the final sources was run.

**On Ubuntu** the same sweep is a loop over `gcc -std=c11 -O2 -Wall -Wextra
-Werror -DE_SEED=<n>u` with `run_tests.sh`'s module list
(`~/ESP32C5_CSI_logs/2026-10-30_step1/agc/seeds.sh`, outside the repo). Run
on the final sources of 2026-11-01: `287 checks, 0 failures` in rooms 5000-5003,
5005, 5006 and 5008; rooms 5004 and 5007 `273 checks, 1 failures`, only e1
(`test_e2e.c:1923`, gate MOVING 30.7 % and 26.0 %, the 2026-09-25 figures);
(f) passes in all nine (`after_review/seeds_summary.txt`, `after_review/seeds/`).
Re-run with F2 on 2026-11-03 (first pass; the review that followed changed
only comments in `vitals.c`, and the gain code is not in the mirror): the
same, 287 / 0 in seven rooms, `273 checks, 1 failures` (only e1) in 5004
and 5007 (`~/ESP32C5_CSI_logs/2026-11-03_impl/seeds/`).

### 2.2 Kconfig switch matrix (L3)

```powershell
& C:\esp\v6.0.2\esp-idf\export.ps1           # the toolchain must be on PATH
python tools\kconfig_matrix.py                # default --project step10_recognise_count_vitals
python tools\kconfig_matrix.py --jobs 6 --out D:\tmp\kmatrix
```

- Build the project once first, because the script needs
  `build\compile_commands.json` and `build\config\sdkconfig.h`. That build
  must have every switch **on** (the defaults). The script can only switch
  things off, and it names any switch that is missing.
- Nothing is written under `build\`. By default the logs go to
  `%TEMP%\kconfig_matrix\<project>\<combination>\log.txt`.
- The script checks itself: its negative control removes
  `CONFIG_CSI_PING_INTERVAL_MS`, and that compile **must fail**. If it does
  not, the patched header is not the one being read, and the run fails.
- Result on 2026-09-28, before `CSI_ACTIVITY_ENABLE` (368 since 2026-10-16 and 552 since 2026-11-03, below):
  `negative control (CONFIG_CSI_PING_INTERVAL_MS removed): rc=1 errors=3 -> fails as expected`
  and `combos: 192, with warnings/errors: 0`, exit code 0.
- The explain work of 2026-09-25 (`CSI_EXPLAIN`, DECISIONS D-161) added
  `CSI_EXPLAIN` on/off to the matrix (DECISIONS D-145; the script's
  docstring), which doubles it to 192 combinations. The first logged
  run of the updated script is 2026-09-28 (§5.2): 192, 0 warnings. The 96
  of 2026-09-25 is the `CSI_EXPLAIN`-off half.
- **Since 2026-10-16: 368 combinations** (the script's docstring, *THE
  MATRIX*). The activity state (D-172) added `CSI_ACTIVITY_ENABLE` on/off,
  but only where Kconfig lets it exist: it depends on `CSI_VITALS_ENABLE ||
  CSI_ID_ENABLE || CSI_COUNT_ENABLE`, so the one id/count/vitals cell with
  all three off has no `act1` variant. 11 cells × 2 + 1 = 23, × 8 (JSON ×
  bar line × console) × 2 (`CSI_EXPLAIN`) = 368. Expected:
  `combos: 368, with warnings/errors: 0` and the same negative-control line.
  `CSI_ACTIVITY_RESTLESS_X100` is an int read only at run time and is not
  varied. First run 2026-10-16 on Ubuntu (§5.2); not yet run on Windows.
- **Since 2026-11-03: 552 combinations** (the script's docstring, *THE
  MATRIX*). Gain logging (D-176) added `CSI_GAIN_LOG` on/off, but only with
  `CSI_JSON_PERIOD_MS` 500: with no JSON line `main.c` compiles the gain
  code out either way (`CSI_HAVE_GAIN`). So each of the 4 bar-line ×
  console cells has 2 gain variants with JSON and 1 without: 23 × 12 × 2
  (`CSI_EXPLAIN`) = 552. Expected: `combos: 552, with warnings/errors: 0`
  and the same negative-control line. The 184 `gain0` and 184 `json0`
  combinations compile the gain-off stub. First run 2026-11-03 on Ubuntu,
  before and after that day's review (§5.2); not yet run on Windows.

**On Ubuntu** the build lives outside the tree (D-170), so pass it:

```bash
bash tools/linux_idf.sh build step10_recognise_count_vitals
python3 tools/kconfig_matrix.py --build-dir ~/esp-build/mnt/vol1/ESP32C5_CSI/step10_recognise_count_vitals --out /tmp/kmatrix
```

The compile command in that `compile_commands.json` names the compiler by
absolute path. First run 2026-10-16: 192, 0 warnings, negative control
fails (§5.2); after the activity state the same day: 368, 0 warnings;
after gain logging (2026-11-03): 552, 0 warnings.

### 2.3 Firmware build (L4)

```powershell
.\build.ps1 step10_recognise_count_vitals          # activate IDF, set-target esp32c5 if needed, build
.\build.ps1 step03_csi_hello -Clean                # deletes build\ AND sdkconfig first
.\build.ps1 -Doctor                                # toolchain, ports, which steps are built
```

- **Success check:** the output contains `Project build complete` and a
  `<name>.bin binary size 0x… bytes. Smallest app partition is 0x200000 bytes.`
  line. Also count `warning:` lines. The target is 0.
- **A warning does not fail the build.** The IDF compiles with `-Werror` but
  exempts unused-function, unused-variable, unused-but-set-variable,
  deprecated-declarations and `-Wextra` warnings (`-Wno-error=...`; see
  Step 10's `build\compile_commands.json`). They still print, so counting
  `warning:` lines is the only check that enforces the 0-warnings rule.
- **Do not judge by the exit code when the output is redirected.** See
  gotcha G2 in §6.
- For Steps 01–09, run `build.ps1` once **before** `idf.py menuconfig`.
  Otherwise menuconfig configures the classic ESP32. Step 10's
  `sdkconfig.defaults` names the target itself (README *Step 10 — recognise, count, vitals
  (experimental)*).
- Wi-Fi SSID and password are set per step in `menuconfig` and end up in that
  step's `sdkconfig`. That file is **gitignored** because it holds real
  credentials (.gitignore, *Generated config*). Never commit it.

### 2.4 Flashing and confirming the port

```powershell
& C:\esp\v6.0.2\esp-idf\export.ps1
esptool --port COM13 chip_id                       # must say ESP32-C5 before you flash anything
.\build.ps1 step03_csi_hello -Flash -Port COM13    # build + flash + idf.py monitor (Ctrl+] quits)
.\build.ps1 step03_csi_hello -Monitor -Port COM13  # build (if needed) + monitor, no flash
```

`-Monitor` still runs `set-target` (when there is no `sdkconfig`) and
`idf.py build` before monitoring (build.ps1:236-254). `-Flash` runs
`idf.py flash monitor`, which never returns in an automated call. Agents
and scripts: use the next block instead (DECISIONS D-156).

**Flash and read without `idf.py monitor`** (scripts and AI agents):

```powershell
& C:\esp\v6.0.2\esp-idf\export.ps1                  # in EVERY automated call: shell state does not persist (D-156)
esptool --port COM13 chip_id                         # must report ESP32-C5
& D:\ESP32C5_CSI\build.ps1 step03_csi_hello          # build only (no -Flash)
Set-Location D:\ESP32C5_CSI\step03_csi_hello
idf.py -p COM13 flash                                # ends with esptool's hard reset, then exits
Set-Location D:\ESP32C5_CSI
python tools\serial_capture.py COM13 60 --out <path outside the repo>\step03.log
```

The board resets at the end of `flash`, before `serial_capture.py` opens the
port, so the boot lines are usually missed. To capture them, start
`serial_capture.py` first, then press the board's RST button. Do not run
esptool while the script holds the port (G7). For Step 10,
`--send status` prints the pipeline line and free heap at any time
(Phase 2b).

**Never, on this Windows setup:**

- pulse RTS/DTR from your own pyserial code, or use
  `serial_capture.py --reset` (disabled for that reason) (G1);
- run `build.ps1 -Flash` from a non-interactive call (G14);
- judge a redirected `build.ps1` run by its exit code (G2);
- call `.\build.ps1` twice by relative path (G3);
- run `idf.py` from Git Bash (G4);
- keep two programs on one COM port (G7);
- flash COM3 or COM4 (G8);
- run `erase-flash` unless you mean to wipe the calibration, the models and
  the vitals log (DECISIONS D-117).

- On 2026-09-25 the C5 showed up as **COM13** (MAC `30:ed:a0:e4:50:b8`).
  COM numbers change with the USB socket and the cable, so run `chip_id` each
  time (README *Find your board's COM port*). COM3 and COM4 on this machine
  are Bluetooth and are never the board (README *If flashing fails*).
- The port can also be identified without resetting the chip: pyserial's
  `list_ports` does not open the port, whereas `esptool chip_id` ends with a
  hard reset via RTS (G20). For example,
  `python -c "from serial.tools import list_ports; [print(p.device, p.vid, p.pid, p.serial_number) for p in list_ports.comports()]"`
  prints each port's device, VID, PID and serial number. pyserial holds VID
  and PID as integers, so they print in decimal: 0x303a is 12346, 0x1001 is
  4097. On 2026-09-28 pyserial's `list_ports` showed COM13 with VID 0x303a,
  PID 0x1001 and the board's MAC, `30:ED:A0:E4:50:B8`, as its serial number
  (§5.1 row 18:57).
- A plain `flash` keeps NVS, so calibration, templates, the count model and
  the vitals log survive it. `idf.py erase-flash` deletes all of them
  (README *Kconfig — `menuconfig` → Recognise / count / vitals*).
- **On Ubuntu (ESP-IDF v6.1) only:** `idf.py flash` now writes only the
  regions that changed when `*_flashed.bin` files from an earlier flash are
  in the build directory, verifies, and falls back to a full flash if the
  verification fails; Espressif recommends `idf.py flash -a` (`--all`) for
  "brand-new, empty, or erased chips" (v6.1
  `docs/en/migration-guides/release-6.x/6.1/tools.rst`, read 2026-10-16).
  After an `erase-flash`, or on a new board, use
  `tools/linux_idf.sh idf <step> -p /dev/ttyACM<n> flash -a` (the `idf`
  subcommand passes its arguments through and, unlike `flash`, picks no
  port; `tools/linux_idf.sh`, the `case` at its end). See G22. Windows stays on
  v6.0.2, where `idf.py flash` writes everything. Nothing has been flashed
  from Linux yet.
- Resetting the board: see gotcha G1 in §6. Use esptool, idf.py or the RST
  button, never RTS/DTR from your own script.

### 2.5 Capturing serial output

| Tool | Use it for | Command |
|---|---|---|
| `idf.py monitor` | interactive use (Ctrl+] quits) | via `build.ps1 … -Monitor` |
| `tools/serial_capture.py` | a fixed-length, timestamped log of **any** step, e.g. boot, Step 03 rate, Step 04 histogram, Step 09 lines. Timestamps are seconds since start, not wall-clock. Since 2026-09-28 it reopens the port after a port loss and marks the gap with `#` lines (G19; the reopen tested with a fake pyserial only: the script's first C5 runs since that change, 2026-09-28, lost no port, §5.1), and a stuck `--send` write times out after 2 s | `python tools\serial_capture.py COM13 60 --out step04.log` |
| `tools/presence_monitor.py` | Step 08 / Step 10 JSON: live status, `--plot`, `--csv` with a wall-clock `host_time` column, and `--send` for Step 10 console commands | `python tools\presence_monitor.py --port COM13 --csv session.csv` |
| `tools/csi_monitor.py` | Step 05's amplitude waterfall | `python tools\csi_monitor.py --port COM13` |
| `tools/vitals_validate.py` | comparing Step 10's `br_bpm` / `hr_bpm_exp` from a `--csv` log with a reference CSV | `python tools\vitals_validate.py --csi session.csv --ref strap.csv --kind hr` |
| `tools/dashboard.py` | Step 10's live dashboard in the browser: how it recognises, counts and measures, from the `EXPLAIN` lines, plus console buttons. `--log` keeps a timestamped raw log; `--replay` plays one back (§2.6) | `python tools\dashboard.py --port auto --open` |
| `tools/session_runner.py` | a scripted §4 session (Phase 1 1a–1c or Phase 2b): cues the operator and logs the board with the schedule as `#` lines (§2.7) | `python tools\session_runner.py --protocol phase2b --port COM13 --note "<placement; who is in the room; AP and channel>" --out <new file outside the repo>` |
| `tools/session_score.py` | scoring a `session_runner.py`, `serial_capture.py` or `dashboard.py --log` log against the §4 marks: per segment when the log holds the schedule, plus resources and restarts (§2.7) | `python tools\session_score.py <log>` |

- **Only one program can own a COM port.** Close `idf.py monitor` before
  running any of these tools (README *Console commands*, README *Step 05*).
  That includes esptool: do not reset with esptool while `serial_capture.py`
  is running. Reset first and accept that the first lines may be missed, or
  press RST after starting the script.
- **DTR/RTS (G1).** `serial_capture.py`, `presence_monitor.py` and
  `csi_monitor.py` all set DTR and RTS False before they open the port, as
  `idf.py monitor` does. The two monitors were changed to this on
  2026-09-25; before that they used pyserial's defaults, which assert both
  lines. Neither monitor has been run on the C5 yet. `dashboard.py` does
  the same (`ser.dtr = False`, `ser.rts = False` before `open()`, read on
  2026-09-25; DECISIONS D-163) and has not been run on the C5 either. If
  the board goes silent after you start one, press RST.
- `serial_capture.py --send CMD` sends Step 10 console commands at 4 s, 6 s,
  8 s and so on after start. `presence_monitor.py --send CMD` waits for the
  first JSON line (up to `--send-wait`, 10 s by default), then sends them 0.3 s apart (`--send-gap`). Typed console
  characters are **not echoed** (README *Console commands*).
- For anything that must line up in time with another device or a written
  log (Phases 3–5), use `presence_monitor.py --csv`. Its `host_time` is this
  PC's wall clock.
- The file names in these examples (`step04.log`, `session.csv`) are
  relative. For a real session, give a path outside the repo (§5, *Raw
  logs*).
- Python needs: `pip install pyserial matplotlib numpy` (README *What you
  need*). `vitals_validate.py` uses the standard library only.
  `dashboard.py` needs only pyserial, which the ESP-IDF Python environment
  already has after `export.ps1`, and not even that for `--replay`.

### 2.6 The live dashboard: replay, live use, smoke test

`tools/dashboard.py` serves `tools/dashboard.html` at
`http://127.0.0.1:8765/` and feeds it the serial lines (DECISIONS D-163).
The explain protocol it reads is ARCHITECTURE §3.7. README *See how it
decides: the live dashboard and the why lines* explains the panels. **None
of this section has been run on the C5 yet**, and nothing here is a result:
results go to §5.

**Replay: no board needed.** Do this first, and after any change to the
dashboard or the protocol (§2.0). Replay needs Python 3.8+ only, no
pyserial. Console commands are refused in replay (HTTP 409).

```powershell
Set-Location D:\ESP32C5_CSI
python -m py_compile tools\dashboard.py
python tools\dashboard.py --replay tools\sample_session.log --open             # recorded pace
python tools\dashboard.py --replay tools\sample_session.log --speed 4 --open   # 4x
```

Silences longer than 3 s in the log are shortened to 3 s (`--max-gap`),
and `--loop` starts again at the end.

Read the first line of `tools\sample_session.log` before you look at the
page. If it starts `# SYNTHETIC`, the values are made up: the replay shows
that the page draws, and says nothing about the board or the room.

| # | Replay check | Pass (project marks, 2026-09-25) |
|---|---|---|
| R1 | open the page | every panel draws; no errors in the browser's developer console |
| R2 | read each panel's "How it decides" text | the thresholds in it are the ones in the log's `config` line, not hard-coded defaults |
| R3 | let the whole log play | gait, count, vitals and presence events appear in the event log with their `why:` text; the waterfall scrolls |
| R4 | press "Record" | a file of the session's lines downloads |
| R5 | replay a log with the explain lines removed (commands below) | the page says `explain lines not received - flash Step 10 with CSI_EXPLAIN` and still shows the JSON values |

```powershell
# R5: a copy of the sample without EXPLAIN and why: lines, as older firmware or Step 08 would print it.
# ascii, not utf8: PowerShell 5.1 writes a byte-order mark with utf8. The firmware prints ASCII only.
Get-Content tools\sample_session.log | Where-Object { $_ -notmatch 'EXPLAIN |why: ' } |
    Set-Content -Encoding ascii "$env:TEMP\no_explain.log"
python tools\dashboard.py --replay "$env:TEMP\no_explain.log" --open
```

**Live.** The board runs Step 10 built with `CSI_EXPLAIN=y` (the default).
Close `idf.py monitor`, `presence_monitor.py` and `serial_capture.py`
first: only one program can own the port (G7). The dashboard's console
panel replaces typing into the monitor.

```powershell
& C:\esp\v6.0.2\esp-idf\export.ps1
Set-Location D:\ESP32C5_CSI
python tools\dashboard.py --port auto --open
python tools\dashboard.py --port COM13 --log D:\ESP32C5_CSI_logs\2026-09-25_dashboard\session.log --open
```

`--port auto` picks the single Espressif device (USB vendor ID `0x303A`).
With none or several, it keeps looking and says why on the page; with both
C5 boards plugged in, name the port. Keep `--log` files outside the repo
(§5, *Raw logs*). A `--log` file is in the `%7.2f <line>` format and can be
replayed later with `--replay`. The page is served to this PC only; do not
use `--host 0.0.0.0` on a network you do not trust, because the console
buttons can `forget` people and `recal` (DECISIONS D-163).

**Smoke test (hardware).** Project marks, set on 2026-09-25; never run.
Record the calibration type too: empty room, or the still-room workaround
below.

| # | Do | Pass |
|---|---|---|
| S1 | start the dashboard with `--log`, then press RST on the board | the page shows the board's lines again without restarting `dashboard.py`. If the boot lines came through (they are often missed after a reset, §2.4), one of them gives the reset reason, e.g. `POWERON` or `SW` (DECISIONS D-165); check the `--log` file |
| S2 | wait for detection, or press `explain` | a `config` event arrives, and the "How it decides" texts show its thresholds |
| S3 | 30 s with nobody moving, then one person walks between the board and the AP | the waterfall scrolls at about `CSI_EXPLAIN_AMP_HZ` rows per second and the walk visibly disturbs it; the motion trace crosses the motion line; a `presence` event with reason `"motion"`; the link panel's packet rate matches the JSON `rate_hz`, and `dropped` stays 0 |
| S4 | keep walking for 30 s or more | a `gait` event about every 2.6 s at 50 packets/s (one per 128 packets), and a `count` event about every 5.1 s (one per 256). With no models, `used` is false with `why` `"no-model"` |
| S5 | one person sits still for 40 s | after about 20 s of stillness, a `vitals` event about every 2 s |
| S6 | press `status`, `people` and `help` | each `CMD` reply appears in the console panel |
| S7 | note `dropped`, close `dashboard.py` for 5 minutes with the board left running and the cable in, then start it again | JSON lines resume at once, `status` answers, and `dropped` has not grown. This is the no-host case of DECISIONS D-164 |

**The still-room calibration workaround** (DECISIONS D-166). This is a
**workaround** for a room that cannot be emptied, **not** the calibration
procedure, which needs an empty room. Use it only to bring the board up and
watch the dashboard.

1. Everyone who will stay in the room sits down and keeps still: no typing,
   no gestures, no phone in hand.
2. Send `recal` (dashboard console button). Ignore the "leave the room"
   prompts, and stay still until the `CALIBRATED` banner: 45 s at the
   defaults (10 s countdown, 5 s warm-up, 15 s phase A, 15 s phase B). A
   boot with no stored baseline first spends about 4 s locking the CSI
   shape.
3. Expect movement to be detected, against a threshold that is higher than
   an empty room would give. Expect the seated people **not** to be
   detected while they sit still: the reference contains them.
4. Label every result from this baseline "still-room baseline" (§5.1, *Room
   / setup* column). The baseline stays in NVS, and Step 08 would load it
   too, so run `recal` in an empty room as soon as one is available.

### 2.7 Scripted sessions: `session_runner.py` and `session_score.py`

`tools/session_runner.py` plays a §4 protocol while it logs the board
(DECISIONS D-169). Each cue is printed and beeped (`--quiet`: no beeps),
with `--speak` also spoken, and a 10 s countdown comes before each change
of activity (none between the lead-in, a `--calibrate-first` segment and
the first empty segment: the operator is already out of the room).
It writes the schedule, every cue and every command it sent into the same
log as the board's lines, as `#` marker lines (its docstring, OUTPUT). Board
lines keep the `%7.2f <line>` format of `serial_capture.py` and
`dashboard.py --log`, so `dashboard.py --replay` plays the log.
`tools/session_score.py` reads the log back and scores each segment
against the §4 marks. **Neither has been run on the C5; everything below
except a live run has been run offline on 2026-09-28 (§5.2).**

| `--protocol` | Segments at the defaults | Total |
|---|---|---|
| `phase1` | §4 Phase 1, 1a–1c (1d and 1e are separate runs): 20 s lead-in; 120 s empty; 3 tangential and 3 radial walking passes of 60 s, each followed by a 60 s empty gap; 20 s walk-in and 300 s seated small movements (1b); 120 s empty; 20 s walk-in and 600 s sitting still (1c); 120 s empty | 20 segments, 2040 s (34 min) |
| `phase2b` | §4 Phase 2, 2b: 20 s lead-in; 600 s empty; 600 s walking; 600 s seated | 4 segments, 1820 s |

The lead-in is for the operator to leave the room; it is not scored. The
lengths can be changed (`--lead-in`, `--walk-s`, `--empty-s`, `--still-s`;
for phase1 also `--passes`, `--start-empty-s`, `--enter-s`, `--minor-s`,
`--gap-s`). A board with no baseline in NVS calibrates at boot: add
`--calibrate-first N`, an empty segment of N s that is not scored (its
docstring: calibration takes about 50 s at the defaults, and 90 leaves a
margin).

```powershell
Set-Location D:\ESP32C5_CSI

# Dry run: prints the schedule and opens nothing (needs no --note, --out or --port)
python tools\session_runner.py --protocol phase1 --dry-run
python tools\session_runner.py --protocol phase2b --dry-run

# Offline smoke test: the 2026-09-28 soak played as the port at 50x its recorded pace, then scored.
# --out: a new file in a folder that already exists, outside the repo.
python tools\session_runner.py --protocol phase2b --simulate D:\ESP32C5_CSI_logs\2026-09-28_step10_soak\soak10_0928.log --scale 0.02 --quiet --note "smoke test" --out <new file>
python tools\session_score.py <that file>
# Expected in this smoke test: the 15-min soak lasts 18 s at 50x and the schedule 36.4 s, so the
# board goes silent during the walk block, the runner prints '# WARN ... no board output for 10 s
# ... (G18)', and the scorer shows 'no detect rows' for seated-2b. The verdicts mean nothing: the
# soak's room activity was never recorded.

# Live Phase 2b on the board (about 30 min)
& C:\esp\v6.0.2\esp-idf\export.ps1                  # esptool, and pyserial for the runner
esptool --port COM13 chip_id                         # must report ESP32-C5 (G8); then close every other program on the port (G7)
New-Item -ItemType Directory D:\ESP32C5_CSI_logs\<YYYY-MM-DD>_p2b     # --out refuses a missing folder
python tools\session_runner.py --protocol phase2b --port COM13 --out D:\ESP32C5_CSI_logs\<YYYY-MM-DD>_p2b\p2b.log --note "<board and AP positions and the distance between them; who is in the room; AP and channel>"

# Scoring
python tools\session_score.py D:\ESP32C5_CSI_logs\<YYYY-MM-DD>_p2b\p2b.log --json D:\ESP32C5_CSI_logs\<YYYY-MM-DD>_p2b\p2b_score.json
python tools\session_score.py <serial_capture.py or dashboard.py --log file> --after-calibration   # no schedule: one 'unlabelled' segment
```

- **The schedule is the ground truth only as far as the operator followed
  it.** A walker who came in late or lingered at the door, a pet or a
  neighbour is not in the log. Write each deviation down with its time
  (the runner prints elapsed seconds) and put it in the §5.1 row.
- **`--note` is required** for a live or simulated run: board and AP
  positions and the distance between them, who is in the room, AP and
  channel. The runs logged so far lack them (§5.1, row 2026-09-28
  13:51–14:07). `--out` refuses a file that exists and a folder that does
  not; keep it outside the repo (§5, *Raw logs*).
- **What it sends.** Only `status`: at the start, every 300 s
  (`--status-every`) and at the end. Only a board that has identified
  itself as Step 10 gets it (the ` Step 10: ` banner, a `CMD` reply, or a
  JSON line with `proc_ms_max`), or, with `--console`, a board not yet
  identified. A board identified as Step 08 or Step 09 gets nothing (Step
  08 has no console); `--no-console` sends nothing at all.
- **The port.** DTR and RTS are set False before `open()` and never
  touched (G1). Once the session has started, a port loss is logged as
  `# PORT <t> lost ...`, the port is retried every 0.3 s, and the script
  does not exit (G19). At start-up, a port that stays unavailable for 10 s
  ends the script before the first cue. A write times out after 2 s and
  counts as a port loss (G20); the start `status` is tried at most 3 times
  in all (`START_TRIES`, session_runner.py:238), each retry after a reopen.
  Ctrl+C stops the run; the log then ends
  `# END <t> interrupted` (`completed` if it falls in the 3 s tail after
  the last segment).
- **What the scorer leaves out.** `--grace` (default 10 s) leaves the
  start of every segment out, because the operator reacts after the cue.
  `--exit-s` (default 9 s = `CSI_PRESENCE_HOLD_MS` 5000 +
  `CSI_STATIC_SUSTAIN_MS` 4000, the Kconfig defaults: step10
  Kconfig.projbuild:122-130, step08 :106-114, on 2026-09-28) leaves a
  further 9 s out of an empty segment that follows an activity, since it
  still holds the walker's exit and the detector's designed hold. Both are
  stated figures, not measured ones; a firmware built with another hold
  needs another `--exit-s`. Every presence figure is printed both with and
  without what is left out.
- **Resolution.** A rate is rows reported X / detect rows, so one row is
  the smallest nonzero rate a window can show. When 0 is observed but one
  row is above the mark, the verdict is
  `not applicable (... needs >= p/mark s)`, never `met` (session_score.py
  docstring, THE ROW PERIOD). At the defaults this matters. The phase1
  walk pool is 6 × 60 s, less the 10 s grace each: 300 s. At the
  2026-09-28 soak's 0.513 s per detect row at 50 Hz (same docstring) that
  is about 585 rows, so one row is about 0.0017 per second, above the 1a
  mark of 0.001 (the score of the `--simulate` phase1 run reads the same
  `1 row = 0.0017`: `score_final_sim_p1.txt` in
  `D:\ESP32C5_CSI_logs\2026-09-28_session_tools\`). A 0 there can only
  read `not applicable`. The 1d mark of 0.0001 needs about 5,100 scored
  empty seconds (0.513 / 0.0001), which neither protocol holds at its
  defaults. An 8 h night holds them, but the scorer gives presence
  verdicts only to a log with `SEGMENT` markers, and the runner has no 1d
  protocol.
- **A scored figure is a result for that session only**, in that room,
  with those people (§1, L5). Never write "validated" on the strength of
  it. For 1c the scorer counts the rows that carry `br_bpm` or
  `hr_bpm_exp` (coverage); nothing in it compares a rate with a reference.
- **Activity (Step 10 `activity` key, scorer version 3, 2026-10-16;
  DECISIONS D-172).** Only when some JSON row carries the key, each detect
  row's `activity` is compared with the value the segment's kind asks
  for: empty → `empty`, walk → `moving`, minor → `restless`, still →
  `still`; enter, setup, calibration and unlabelled segments have no
  expected value and show counts only (session_score.py docstring,
  ACTIVITY). Per segment and per column (grace (+ exit) left out | nothing
  left out) it prints the count of each value and the **match fraction**
  (matching rows / rows that carry the key; `null` is never a match; rows
  without the key are counted apart). A **confusion table** pools the
  segments with an expected value: rows = expected, columns = reported.
  The verdict is always `no mark`: §4 sets none. The state lags each cue by
  its own 5-10 s window and 2 s hold (activity.h), which the 10 s grace
  may not cover, so read both columns. A log with no `activity` key
  scores exactly as before (checked on 10 older logs, §5.2 2026-10-16).
  The expected value is the cue, not an observed truth: a person who
  fidgets during a "still" segment counts as an error of the firmware.
- **Gain (Step 10 `agc` / `fft` / `gain_chg`; scorer version 4 on
  2026-11-03, version 5 after that day's review; DECISIONS D-176).** Only
  when some JSON row carries `gain_chg`. Per scored segment (in the
  SEGMENTS block, one figure per column) and pooled per kind, `unlabelled`
  included (grace (+ exit) left out): the detect rows with a numeric
  `gain_chg`, the `null` ones (switch off, or no packet since the previous
  line) and any invalid ones (not a non-negative integer: a float,
  Infinity, NaN, a negative number; used for nothing else); how many rows
  had `gain_chg` > 0 and the packets behind them; the PRESENT fraction of
  the rows with a change and of the rest; the `agc` range (session_score.py
  docstring, GAIN). In `--json`, `gain` sits in each column set with
  `rows_numeric`, `rows_null` and `rows_invalid` (version 4's
  `rows_with_key` is gone). No mark. In an empty segment a PRESENT fraction
  much higher in the changed rows points at the receiver, not a person: a
  hint, not a proof, since a person walking in also moves the gain. A log
  with no gain key scores as version 4 did except the version line
  (checked on the seven `docs/hw_logs` logs and `tools/sample_session.log`,
  §5.2 2026-11-03). Before reading any of it, do the first-capture check
  of §4 (*Keep for every session*).
- **Log the run** in §5.1 with the `--note` text, the deviations and the
  raw-log path, and keep the scorer's `--json` file beside the raw log.

---

## 3. How the synthetic tests work, and their known weaknesses

### 3.1 Mechanics

- **Deterministic.** A seeded xorshift32 PRNG (test_util.h, `rng_seed`,
  `rng_u32`) makes every run see identical "noise". So a pass or fail is
  reproducible, but one seed is one sample of a synthetic world.
- **Counting checks.** `CHECK(cond, …)` counts every assertion and prints
  `FAIL file:line` for each one that fails (test_util.h). "11,453 checks" (2026-09-28),
  "11,515 checks" (2026-10-16), "11,673 checks" (2026-11-01) and "11,705
  checks" (2026-11-03) are counts of assertions, not of independent experiments.
- **KNOWN LIMIT lines (since 2026-10-30).** Where a test measures a failure
  the code has today, it prints `KNOWN LIMIT: …` with the numbers instead of
  a failing `CHECK` (test_vitals.c:776-785 and `known_limit` :791-794 on 2026-11-03; test_e2e.c scenario (f), `test_agc`).
  They are neither checks nor failures, so a green run carries them: read
  them. `test_vitals` ends with `test_vitals: N KNOWN LIMIT line(s) above -
  printed, not failures` (test_vitals.c:1337 on 2026-11-03); `test_e2e`'s five are found
  by grep. Where a failure is rare (a few % of runs), the case runs 50-100
  seeds and `CHECK`s a run-count or rate bound instead of "none", because a
  check of "none" on 3-5 seeds passes or fails by seed luck
  (test_vitals.c:891-901 on 2026-11-03). The (12b) silence rows have
  checked such a rate since 2026-11-03 (at most 2 wrong heart values per
  1,000 analyses, test_vitals.c:1305) and print a KNOWN LIMIT line only if
  a wrong value appears (:1308-1312). The 12 KNOWN LIMIT lines of
  2026-11-03 (15 on 2026-11-01) are listed in §3.3 items 10 and 11.
- **Mirrored constants.** `test_e2e.c` copies `main.c` / Kconfig defaults
  such as `TAU_FAST_S`, thresholds, `ID_*`, `CNT_*`, `GATE_RATIO` and
  `CARRY_US` "as of 2026-09-24; if those change, change them here"
  (test_e2e.c:20-21, 138-168, 1596 on 2026-11-01). Since 2026-10-30 it also
  copies the static branch's `TAU_SLOW_S`, `THR_STATIC`,
  `STATIC_SUSTAIN_US` and `SLOW_FREEZE_MAX_US` (:162-166), used only by
  (f)'s shadow.

### 3.2 What each suite feeds in

| Suite | Signal | Main checks | Source |
|---|---|---|---|
| `dsp` | seeded random and constructed vectors | FFT vs a direct DFT, detrend, Hann, parabolic interpolation, median, group map/dev, Jacobi, power iteration | test_dsp.c:1-5 |
| `ncc` | Gaussian clusters | Welford statistics, standardisation, rejection by distance and margin, NaN/inf hygiene, NVS-blob sanity. "Separable data" by design | test_ncc.c:1-10 |
| `vitals` | 52 subcarriers at ~50 Hz, ±3 ms jitter, 10 % loss; each subcarrier is `1 + a_br·sin + harmonics + a_hr·sin + 0.01·gauss + drift`. Since 2026-10-30, (9)-(12b): no chest (white noise; 0.8-2 Hz noise per subcarrier or shared by all; an "empty room" fluctuation shared by all, band-passed to 0.1-2 Hz), breathing-only waveforms rich in harmonics (4 shapes, 8-25/min, rates that wander ±4-8 %), 15/min breathing plus heart-band noise, 72 bpm at irregular 40-100 Hz packet rates and a 40 Hz stream stamped as 50 Hz, and a weak or absent heart with 0.3-0.9 s link silences. Since 2026-11-03, (6b): packets 0.65 s apart with a distinct value per subcarrier (D-175) | known breathing and heart rates recovered; quiet on noise; no heart rate where there is none and the true rate or nothing where there is; rare failures checked as run-count bounds over 50-100 seeds, and the (12b) silence rows as a rate bound of 2 per 1,000 analyses (D-175); (6b) the exact filled ring values per channel, each toward its own subcarrier's value, and an adjacent bin; 7 KNOWN LIMIT lines (§3.3 item 10) | test_vitals.c:1-19, 653-713, 776-1316 (on 2026-11-03) |
| `gait` | clean periodic "walks" and three synthetic walkers | feature definitions, window timing, the ≥ `GAIT_MIN_MOVING` gate, separability, degenerate input, rates other than 100 Hz | test_gait.c:1-13 |
| `count` | synthetic group signals, 1 vs 2 sources | eigenvalue features vs full Jacobi, block timing and duty, scale invariance, one-pass vs two-pass covariance | test_count.c:1-19 |
| `gate` | group signals in units of group noise; breathing fed **unfiltered** (harsher than the device) | slow vs fast modulation, learns its floor only in an empty room, survives gaps and NaN | test_gate.c:1-23 |
| `pvitals` | **no signal**: event sequences. (a)–(j) call `person_vitals.c` directly; (k) drives them through `drv_packet()`, a small mirror of the per-packet order **as it was before D-108** (no `s_carry_wait`, no `who_present`, `pv_packet` before the vitals step; §3.3 item 3) | every `vitals_of` latch rule, the edge-case table (documented residuals asserted as they are), 240,000 random events (4 configurations × 60,000, test_pvitals.c:1735-1747) checking invariants and `pv_log_valid` | test_pvitals.c:1-21 |
| `activity` (2026-10-16) | **no signal**: per-packet decisions (present after the hold, gate still, `hf`, hold flag) scripted on a microsecond clock | the activity.h rules with known answers: still after 10 s, empty after ~5 s, moving with only 30 % of packets gate-MOVING, restless at `hf` 3.0 1 s in 4, the 2 s hysteresis, the same states at 20 / 50 / 100 packets/s, timestamp gaps, episode seconds (a lapse the hold undoes is one episode; a walk-in during a lapse is a new one; the carry limit), threshold edges, the gate's floor unknown | test_activity.c:1-40 |
| `gain` (2026-11-03) | **no CSI**: 64-byte `rx_ctrl` headers (bytes 22 and 23 set, byte 28 a channel), 2,000 random headers, scripted and 200 random gain sequences | `csi_gain_read` takes byte 23 as the uint8 agc and byte 22 as the int8 fft, never byte 28, and refuses a short header; the per-row summary (a change counted against the packet before, across rows; lo/hi; the last values kept over an empty row; 0 changes at constant gain; saturation at 65,535); 22 checks. It cannot show that those bytes **are** the receiver gain on a C5 (D-176) | test_gain.c:1-8 |
| `e2e` | int8 CSI of 64 subcarriers, 3 % dropped, ±2 ms jitter, AGC wander, through the mirrored front end | (a) recognition at 100 Hz, (b) 1 vs 2 movers at 50 Hz, (c) vitals at 50 Hz (truth, gate and flag paths), (d) empty room, (e) whose vitals: e1 recognised walker sits, e2 two sit, e3 walk-in after a named person left, e4 (2026-10-16) the activity state over a walk-in, sit, fidget, walk, leave visit: it **prints** the agreement per segment and asserts only what is certain (still at the end of the seated spell, one episode across the breathing-hold lapse, the episode ended after leaving); (f) (2026-10-30) the same front end and (e)'s glue under an **assumed** multiplicative receiver applied before the int8 rounding (common gain steps, drift and offset; fixed and gain-state per-subcarrier ripple; Rapp compression), with an unperturbed twin of every packet and a shadow of Step 08's static (D) branch that feeds nothing: f0 normalisation, f1 one held offset in an empty room, f2 one visit per model, f-shadow (scripted `d_sigma`) and f-rng (the AGC model's own PRNG). It asserts the invariants (f0; the reference visit; a common gain leaves no residual after normalisation; no visit credits vitals in the empty room; for a common gain, no empty-room breathing or gate MOVING, and walking stays PRESENT) and prints 5 KNOWN LIMIT lines (§3.3 item 11) | test_e2e.c:1-121 (on 2026-11-01), `e4_activity`, `test_agc` (:3189), `k_agc_cfg` (:2524) |

### 3.3 Known weaknesses: read these before trusting a green run

1. **Separable by construction.** The walkers are built to differ, the two
   movers in (b) disturb nearly orthogonal groups, and the chest in (c) has a
   **sine** heartbeat on **white** noise (test_e2e.c:106-113 on 2026-11-01; README *Heart rate:
   what the synthetic test shows*). The recognition and counting accuracies
   the tests print are about the arithmetic, not about recognising or
   counting people (README *How it answers*, *Training the counter*).

2. **e1 fails in 2 of 9 synthetic rooms at the device default of 50 Hz.**
   On 2026-09-25, e2e was re-run for rooms 5000–5008. In rooms 5004 and 5007
   the walker is never recognised within 120 s, because the motion gate calls
   only 31 % and 26 % of the walking packets MOVING. Too few gait windows
   reach the 80 %-moving bar (test_e2e.c:1889-1902 on 2026-11-01; README *Verification
   status — please read*). At 100 Hz it is also 7 of 9, but different rooms
   fail for different reasons: an empty-room false trigger in 5002, and two
   seated chests that kept Step 08 on for the whole run in 5003 (README
   *Verification status — please read*). **The 100 Hz figures and room
   numbers are a 2026-09-25 session measurement that cannot be reproduced
   from the repo as it is:** scenario (e) is hard-coded to 50 Hz
   (`fe_calibrate(50.0f)` in `test_whose_vitals`, test_e2e.c:2453 on 2026-11-01), and no
   `/D` switch changes that. Re-running it needs that line edited to
   `fe_calibrate(100.0f)`, or a new `E_RATE` override. **So raising the
   packet rate is not a proven fix.** Both 50 Hz failures were reproduced
   on 2026-09-25 with the §2.1 recipe, exactly as written:
   - room 5004: `e1: A walks in alone 120 s: moving (flag && !still) on 26.5% of present packets (flag 75.6%, gate MOVING 30.7%)`
   - room 5007: `… on 20.5% of present packets (flag 64.6%, gate MOVING 26.0%)`
   - both: `FAIL … test_e2e.c:1609 … e1: A not recognised in 120 s of walking`
     and `test_e2e: 223 checks, 1 failures -> FAIL`.
   - re-run on Ubuntu on 2026-11-01 with the final sources (§2.1): the same
     two rooms, the same gate figures (30.7 % and 26.0 %), the same CHECK
     (now `test_e2e.c:1923`), `273 checks, 1 failures` each. Scenario (f)
     passes in both.

   This is a gait-window yield weakness. It suggests that recognition
   through the device path is fragile even on synthetic data.

3. **The mirrors can drift from `main.c`.** `main.c` is never compiled on
   the PC. `test_e2e.c` holds a hand copy of its front end and per-packet
   order (`gl_packet`) and of its constants (test_e2e.c:20-21 on 2026-11-01). If
   `main.c` changes and the copy does not, e2e stays green while testing old
   behaviour. Nothing checks this automatically. The last comparison was a
   manual read during the 2026-09-25 verification. **Whenever you change the
   per-packet order or a default in `main.c`, update `test_e2e.c` in the same
   change.**

   **A second mirror has already drifted.** `test_pvitals.c` section (k)
   drives its scenarios through `drv_packet()` (test_pvitals.c:1287-1345),
   which still follows the rule D-108 replaced (D-109): it calls
   `pv_presence_start` and hands back the carried count at **every**
   presence start, has no `s_carry_wait` and no `who_present` (neither name
   occurs in the file), and calls `pv_packet` before the vitals step instead
   of after the breathing hold as `main.c:2786-2857` does. Its check "Step 08
   re-triggers while the gate stays still: kept" (test_pvitals.c:490-495)
   asserts the pre-D-108 hand-back. Read (k) as a unit-level test of
   `person_vitals.c` that predates D-108, not as a replay of `main.c`'s
   current order. The current order, including the carry wait, is exercised
   only by `test_e2e.c`'s `gl_packet()` (scenario (e); test_e2e.c:1729-1796
   on 2026-11-01, `act_push` at :1794). Bringing `drv_packet()` up to `main.c:2786-2988` is an open
   item.

4. **Not modelled in e2e** (test_e2e.c:24-31, 42-43, 67-74 on 2026-11-01): Step 08's static (D)
   presence branch as an input to presence (since 2026-10-30 scenario (f)
   computes a **shadow** of it that feeds nothing, so what a device does
   after a false static PRESENT is still unmeasured: item 11, f-mirror);
   `report_build`'s other fields and the JSON text; the
   no-CSI watchdog and recalibration; sessions and the console (enrolment and
   count training are run offline); `forget` / `vitals-reset`; the NVS save.
   (a)–(d) also leave out the breathing hold and `person_vitals`.

5. **Mixed rates.** Recognition was tested at 100 Hz, counting and vitals at
   50 Hz (README *Before recognising anyone: raise the packet rate*). The device default is 50 Hz
   (`CSI_PING_INTERVAL_MS` 20, Kconfig.projbuild:17-19).

6. **A green suite has already missed a real bug.** In the 2026-09-25
   review, a person who walked in within 60 s of a named person leaving
   inherited that person's name and count. Their breathing went into the
   other person's log. The suite was green, because the mirror had no such
   scenario. It was fixed in `main.c`, and scenario **e3** was added. With the
   old rule restored in a copy, e3 fails (2026-09-25 session measurement; the
   copy is not in the repo). One gap remains, and it is documented: a walk-in
   the gate never calls MOVING still inherits the name (README *Walking in,
   unseen by the gate*, a bullet of *Whose vitals: `vitals_of` and the
   per-person vitals log*).

7. **Crediting while walking.** Until the gate notices a walk, vitals keep
   analysing, and those analyses are credited to the seated person. In the
   2026-09-25 sweep this was 1 extra sample in room 5000, and 7 during a 29 s
   walk in one 100 Hz room. The logged rate stayed within ±1 (2026-09-25
   session measurement, not reproducible from repo files alone).

8. **Mutation check (pvitals).** 15 rules in `person_vitals.c` were broken
   one at a time. The tests caught 14. The one they missed has no observable
   effect: `slot_gone` commits instead of discarding just before the record is
   zeroed (2026-09-25 session measurement; the mutants are not in the repo).
   **Mutation check (activity), 2026-10-16:** 38 mutants of `activity.c`
   (comparisons, thresholds, windows, the hold, the episode rules), each
   run against `test_activity` (plus an unmutated baseline run, which
   passes); 34 were caught. Four survive
   (`mutations_list.txt` gives each edit): `gap_ge` (the gap limit doubled
   to 2 s), `emptysec_count` (a second closed even with no packet in it),
   `hf_gt0` (`hf >= 0` made `hf > 0`) and `wait_held_notimeout` (a hold
   second bridges a wait even past the carry limit) (`mutations.txt` in
   `~/ESP32C5_CSI_logs/2026-10-16_activity/after_review/`, outside the
   repo; the mutants themselves are not in the repo).
   **Mutation check (vitals), 2026-10-30 and 2026-11-01:** seven single-rule
   mutants of `vitals.c` (no breathing gate before the heart search, no
   harmonic notch, no shoulder test, timing from packet counts at an assumed
   50 Hz, no heart threshold, breathing threshold 2, heart threshold 3) each
   fail `test_vitals` (4 to 57 failures; `hr/mutations.txt`). After the
   review, `VIT_NOTCH_KMAX` 5 and 7 and a 0.03 Hz notch half-width fail 2
   checks each (`after_review/harness/vitals_mut_*.log`).
   **Mutation check (the e2e mirror), 2026-11-01:** six mutants of
   `test_e2e.c`'s own mirror and harness (shadow `THR_STATIC` 3.0, no static
   hold, `fe_packet`'s median frozen, an f1 row looked up wrongly, a `g_rng`
   draw per tick, one per gain step) fail 1 to 12 checks each
   (`after_review/harness/e2e_mutants/`). Both folders are under
   `~/ESP32C5_CSI_logs/2026-10-30_step1/`, outside the repo; the mutants are
   not in the repo. `dsp`, `ncc`, `gait`, `count` and `gate` have had no
   mutation check.

9. **Heart rate needed strong breathing even in simulation.** At a
   heart-to-breathing ratio of 1/10, heart rate came through only when
   breathing was at least 2 noise units. At 1/20 it was reported in a quarter
   of the analyses, and 1/50 was never tried. Real ratios are 1/10 to 1/50
   (README *Heart rate: what the synthetic test shows*).

10. **Heart rate (and breathing) from noise, harmonics and link silences:
    7 KNOWN LIMIT lines** (10 until 2026-11-03; `test_vitals` (9)-(12b),
    added 2026-10-30 after the research sweep, reviewed 2026-11-01; figures
    from that day's `~/ESP32C5_CSI_logs/2026-10-30_step1/after_review/run_tests_full.log`, unchanged in the
    2026-11-03 run except the silences). Every amplitude is an assumption,
    not a measured room (test_vitals.c:884-890 on 2026-11-03).
    - **A fluctuation shared by all subcarriers** passes the confidence
      tests: the combined spectrum then has the statistics of one noise
      spectrum, not of 12 (test_vitals.c:884-888 on 2026-11-03). An "empty room" with a
      shared 0.1-2 Hz fluctuation of 0.02 gave a heart rate (49.8-89.3 bpm)
      in 16 of 100 analyses (4 of 5 runs) and breathing (8.96-27.99/min) in
      34 of 100; at 0.005, breathing in 148 of 2,000 analyses
      (8.75-28.60/min) and a heart rate in 7 of 100 runs (checked against a
      bound of 15). Breathing from an empty room matters twice: with the
      breathing hold (D-90) it also keeps PRESENT on.
    - **Breathing plus heart-band noise shared by all subcarriers**: at
      shared 0.005 a heart rate (63.3-102.0 bpm) in 51 of 1,000 analyses (12
      of 50 runs); at 0.02, in 44 of 100 (5 of 5 runs, 68.9-109.0 bpm). The
      same noise drawn per subcarrier ("own") gave none.
    - **Phase-modulated breathing** ("phase 2.5 rad", shape 3): breathing
      read up to 2.01 × the true rate in 19 of 60 runs at 8-12/min (the 2nd
      harmonic outweighs the fundamental on many subcarriers), and withheld
      in 480 of 800 analyses at 14.65-15/min (2nd harmonic on the band's top
      bin, so no heart search ran). Its phantom heart rate is a rate bound
      (at most 5 % of analyses): 200-run sweeps gave 3 runs at 12/min
      (59.9-60.0 bpm) and 2 at 22/min (test_vitals.c:975-980 on 2026-11-03).
    - **Link silences of 0.3-0.9 s** (12b). Until 2026-11-03 an empty
      100 ms bin repeated the previous bin (DECISIONS D-85), freezing a
      breathing swing into a step whose spectrum leaks into the low heart
      band. At the shipped seed base that gave 6 and 12 analyses more than
      3 bpm off the true 72 with a weak heart (×0.50, ×0.25; 49.8-52.5 and
      49.8-60.9 bpm), and 23 of 3,997 analyses reporting 49.2-56.2 bpm with
      no heart: three KNOWN LIMIT lines on 2026-11-01. Without silences
      every case was right or withheld. **Since D-175 (2026-11-03)** the
      empty bins are filled by a straight line toward the packet that ends
      the silence (vitals.c gap loop; (6b) pins the fill, §3.2). That
      **reduces** the phantom; it does not remove it: over 52 seed bases
      (1700-50700 step 1000, plus 1750 and 1770) D-175 gives 1 wrong value
      in 332,374 silence analyses (52.4 bpm with no heart, base 1770), the
      repeat loop 889 (2.7 per 1,000; up to 14.2 per 1,000 at one base)
      (`~/ESP32C5_CSI_logs/2026-11-03_impl/after_review/sweep12b/SUMMARY.txt`).
      So the three silence rows now `CHECK` a rate, at most 2 wrong values
      per 1,000 analyses (test_vitals.c:1305 on 2026-11-03), and print a
      KNOWN LIMIT line only if a wrong value appears (:1308-1312); at the
      shipped base none does, so the three lines are gone. The seed base is
      part of the test: at 14 of the 52 bases the repeat loop also stays under
      the bound, which is why (6b), not this row, pins the fill
      (test_vitals.c:1242-1258). What remains: the rare phantom, and the
      fill's own risk, a line toward one packet's value (D-175).
    - Printed, not a KNOWN LIMIT: a 40 Hz stream stamped as 50 Hz reads
      16.27/min and 90 bpm (true 13/min and 72 bpm), what a caller passing
      packet-count timestamps would get. `main.c` stamps arrival times
      (D-85).
    - Clean in these tests: white noise at four levels and per-subcarrier
      heart-band noise (no breathing, no heart rate), shapes 1, 2 and 4 (no
      phantom heart, breathing within ±1/min), the irregular 40-100 Hz rates
      (72 bpm or withheld).

11. **AGC: a multiplicative receiver breaks what additive noise cannot**
    (`test_e2e` (f), added 2026-10-30 after the research sweep, which cites
    arXiv 2607.01882 and 2605.26836: AGC "compresses the channel estimate
    multiplicatively", and AWGN-trained classifiers "remain near random
    decision" on real CSI). (a)-(e) add noise additively, so they cannot see
    this. The receiver magnitudes are **assumptions** (`k_agc_cfg`), the
    papers give none, and the static branch is a shadow (f-mirror). Room
    5000, from `after_review/run_tests_full.log`:
    - **f-int8**: a common gain DROP is not fully cancelled in int8 CSI: the
      rounding noise stays ~0.29 LSB while the signal shrinks. Held offset,
      60 s empty: `m_sigma` −0.02 / +0.86 / +1.69 / +3.27 / +5.65 at 0 / −1.5 /
      −3 / −4.5 / −6 dB, PRESENT 8.6 / 46.1 / 30.8 / 99.6 / 99.6 %. ±3 dB steps
      in a visit: no false PRESENT. Whether the board's 12-bit L-LTF words
      shrink the effect is not modelled (int8 only).
    - **f-comp**: a gain change through soft compression changes the shape,
      which the median cannot remove. With the 1 dB point at 60 LSB the
      shadow static branch is on 92.6 / 93.1 % of the empty room at +1 / +3 dB.
      In a visit (steps + compression): empty-room PRESENT 18.5 % in the
      mirror, 79.4 % with the shadow D; after leaving 8.6 % / 58.8 %. Once D
      holds PRESENT the slow EMA freezes, so D does not decay until the
      600 s cap (D-70).
    - **f-state**: a per-subcarrier response that changes with the gain state
      is a shape step at every re-lock. Steps of ±1-3 dB with 0.5 dB rms:
      empty-room PRESENT 91.8 % (5 onsets; reference 0.0 %), 15 analyses
      reported breathing in the empty room (the breathing hold then held
      PRESENT on 1,451 packets), activity `empty` on 1.7 % (reference 100 %);
      seated breathing on 28 of 35 analyses.
    - **f-cal**: gain steps during calibration ("boot") distort the empty-room
      statistics. Verdicts at room 5000: steps+state and all DEAF and A never
      named, steps+comp FALSE PRESENT; across rooms 5000-5008 the verdict
      varies (steps+comp "neither" in five rooms; every model FALSE PRESENT
      at 5004; `after_review/seeds/`).
    - **f-mirror**: the shadow never feeds presence, the gate's
      `room_empty` or the glue, so what a device does after a false static
      PRESENT (gate floor, breathing hold, latch, activity) is not measured.
    - The fixes the test prints are proposals (DECISIONS D-174). The
      first step they share, logging the receiver gain per packet, exists
      since 2026-11-03 as a diagnostic (D-176: JSON `agc`, `fft`,
      `gain_chg` on the detect line; `test_gain`, §3.2), built but **never
      run on the board**. Nothing the detector decides reads it, so (f)'s
      numbers are unchanged, and AGC handling itself is still a proposal
      (§4, *Keep for every session*).

12. **Real recordings put Step 08's thresholds inside the empty-room tail,
    show the seated person carried by D, and show freeze-cap stuck runs**
    (offline replay of 2026-11-01, §5.3; ESP32-S3 recordings, not the C5,
    so read every number with §5.3's caveats; medians over 336 link runs per
    mode, `raw` / `first` / `mean`, default thresholds):
    - **Thresholds inside the tail.** The per-link 99th percentile of
      `m_sigma` in the first empty phase is ≥ 5 on 93 / 138 / 110 runs, and
      of `d_sigma` on 132 / 148 / 135; the median link's `m_sigma` p99 is
      3.66 sd (raw IQR 2.64-5.23). Phase B's 15 s understates the spread of
      the next 4 minutes (an autocorrelated metric with few independent
      samples, a median 54 packets in the burst modes, and a room that is
      not stationary). The 4 s sustain (D-68) keeps D's tail from
      triggering. Raising both thresholds to 8 sd cut empty-room PRESENT to
      a median 0-5 % but dropped the seated lower quartile to 0.55 / 0.42 /
      0.93.
    - **Seated is carried by D.** Seated time: static flag on a median 100 %,
      motion flag 3-11 %; AUC of `d_sigma` 1.00, of `m_sigma` 0.60 / 0.64 /
      0.67 (walking 0.96 / 1.00 / 1.00). Without the D branch this data says
      a seated person would be missed. P(seated) < 0.5 on 43 / 57 / 31 runs,
      27 of the 43 raw ones in the residence.
    - **Freeze-cap stuck runs.** 52 / 37 / 56 runs stayed PRESENT on > 90 % of
      the second empty phase, every one held by the static flag (median
      `d_sigma` 12.4 sd raw); 43 of the 52 raw ones are laboratory trials
      T0009-T0011, clustered on links touching three nodes. The slow EMA is
      frozen while PRESENT and the cap is 600 s (D-70), longer than any
      trial's remaining time, so no trial shows the end. At 8 sd: 37 / 33 /
      42: a reference problem, not a threshold one. This data cannot say which
      cap is better.
    - **D drifts.** In runs that do not stick, `d_sigma` sits a median 0.45 /
      0.42 / 0.67 sd higher in the second empty phase than in the first
      (AUC 0.62 / 0.62 / 0.64).
    - **Activity blanks on gaps.** The activity state discards its ring on a
      gap > 1 s; residence links had a median 84 (T0003) and 54 (T0004)
      such gaps per 750 s, laboratory links 2-3 (recounted 2026-11-01,
      `~/ESP32C5_CSI_logs/2026-11-01_docs/gap_recount.txt`).
      Seated → `still` 61-68 % in the laboratory, 19-23 % in the residence,
      with no state 52-57 % of the residence's seated time.

---

## 4. Hardware test plan

Do the phases **in order**. Each phase assumes the previous ones passed, and
a failure early on makes every later number meaningless.

**Keep for every session:**

- **Date and time**, board MAC and port, firmware (step, `git rev-parse
  --short HEAD`, and whether the tree was dirty), and non-default Kconfig
  values.
- **Access point:** router or laptop hotspot, model if known, channel, and
  RSSI.
- **Room sketch:** size, board and AP positions, the distance between them,
  furniture, fans, HVAC, pets and doors.
- **Raw logs**, kept outside the repo. Put their paths in §5.
- **Assertions:** leave them **on** until the code has run on hardware
  (README *Build settings*).
- **Placement:** follow README *Getting good results — physical placement*.
  Board and router on opposite sides of the area, 1–6 m apart, board taped
  down.
- **After a move:** recalibrate after moving anything. For Step 10, also
  re-enrol and retrain after moving the board or changing the channel or
  ping rate (README *Enrolling a person*, step 7).
- **Baseline:** when it was taken, and whether the room was empty or the
  still-room workaround was used (§2.6, DECISIONS D-166). A still-room
  baseline stays in NVS until the next calibration, so it can silently
  carry over into a later session.
- **Receiver gain per packet (logging since 2026-11-03, DECISIONS D-176;
  never run on the board).** A Step 10 image built from the 2026-11-03
  sources with `CSI_GAIN_LOG` on (the default) and JSON on adds `agc`,
  `fft` (the last packet's receiver AGC and FFT gain since the previous
  detect line) and `gain_chg` (packets that changed it) to every detect
  JSON line, and logs the calibration's packets once as `gain before
  detection: ...`. The board's 0xef360 (2026-09-28) has none of this, so
  flash a newer Step 10 first and log it in §5.1. Why it matters:
  `test_e2e` (f) shows, with **assumed** magnitudes, that gain steps
  through compression, or a per-subcarrier response that changes with the
  gain state, give false static PRESENT and empty-room breathing (§3.3
  item 11); real step sizes on the C5 are unknown, and every AGC fix
  proposed so far (D-174) needs them first. **First-capture sanity check**
  (csi_gain.h:22-25), before trusting any gain figure: `agc` should move
  with distance or RSSI (for example, two captures at different board-AP
  distances, read beside `rssi`) and stay nearly constant on a still link;
  a value that never changes, or one that equals the channel (6 on the
  2026-09-28 hotspot, §5.1), means bytes 22-23 are not the gain on this
  chip, and the keys must be ignored. The byte positions come from
  Espressif's `esp_csi_gain_ctrl` binary, not a documented field (D-176).
  Then score the log: `session_score.py` (version 5) prints a GAIN block
  per segment and per kind, with no mark (§2.7). The library route itself,
  `esp_csi_gain_ctrl_get_rx_gain(rx_ctrl, &agc_gain, &fft_gain)`
  (`step09_espressif_sensing/managed_components/espressif__esp_csi_gain_ctrl/include/esp_csi_gain_ctrl.h:112-118`),
  was not taken: it ships no ESP-IDF 6.1 library (DECISIONS D-173, D-176).

### Phase 0: bring-up (boot, Wi-Fi, CSI arrives, CSI shape, rate)

**Equipment:** the C5, a data USB cable, the 2.4 GHz AP the later phases will
use. The AP matters: CSI shape and rate depend on the PHY and behaviour of
the AP (README *Step 04*). A laptop hotspot is fine for bring-up. Repeat 0c
and 0d on the real router before Phase 1.

| Check | Firmware | Do | Pass mark | Origin of the mark |
|---|---|---|---|---|
| 0a Boot | Step 01 | flash, capture 20 s | banner, then a `heartbeat N (free heap …)` line every second (step01_hello/main/main.c:8, 54) | the step's own definition. **Done 2026-09-25**, §5 |
| 0b Wi-Fi | Step 02 | build it first (its current source has no recorded build, §0), flash with SSID/password, capture 30 s | `band mode 2.4GHz-only`, `bandwidth HT20` and `power save OFF` all `ESP_OK`, got an IP, `link alive` lines. Record channel and RSSI | README *Step 02* expected output |
| 0c CSI arrives | Step 03 | capture ≥ 60 s | the counter rises, and the **60 s mean rate is ≥ 45/s** at the default 20 ms ping, i.e. within 10 % of 1000 / 20 = 50/s. Record the `last len` values, and the `heap N B` value on the rate lines (minimum over the 60 s; step03_csi_hello/main/main.c:320-327): that is the Wi-Fi + CSI free-heap figure DECISIONS D-17 waits for | **project mark** (2026-09-25). README *Step 03* "What to check" says the rate should be "close to `1000 / CSI_PING_INTERVAL_MS` — about 50/s". One reply per 20 ms ping gives about 50/s, so a single lost reply takes a line below 50, and README's own example line reads 47/s: judge the 60 s mean, with 10 % tolerance. The 90–290/s bracket in DECISIONS D-44 is a third-party figure for other traffic patterns, not a target for this firmware (D-35) |
| 0d CSI shape | Step 04 | empty room, ≥ 30 s, capture the histogram | every `bytes` value in **{106, 114, 228, 234, 468, 490}**. A value outside that set is the thing to investigate. Note the dominant share: ≥ ~90 % → leave `CSI_LLTF_ONLY` off, otherwise turn it on | REFERENCES.md §1.1 table and §6 item 1; README *Step 04*, "The decision this step gives you" |
| 0e 12-bit L-LTF | Step 04 | read the `odd hi-nibble 0 / F / other` columns | **106-byte** rows show `other` near 0 %, meaning 12-bit samples in 16-bit words, 26 subcarriers, not 53. HT/HE rows show `other` well above 0 % (plain int8). A very weak int8 signal can also read near 0 %, so check RSSI | step04_csi_geometry/main/main.c:88-100, 360-363; the C5 format is from esp-idf #18118 (COMPARISON.md §4). This is **the** C5 CSI hazard |

- If 0c reads **0/s while associated**, the AP answers pings with 11g
  frames. Step 03 deliberately does not capture those, because their 12-bit
  L-LTF would mix two layouts (step03_csi_hello/main/main.c:19-25). Use
  `CSI_LLTF_ONLY` from Step 05 on.
- If 0c reads **~10/s**, ping is failing. **Single digits** mean a marginal
  link (README *Step 03* table).
- If 0d finds the lengths differ from what REFERENCES §1.1 predicts, stop.
  Later steps lock onto the dominant shape (README *Step 05*).
- Also run README *Step 05 → See it with your own eyes*
  (`csi_monitor.py`). Waving a hand must tear streaks through the waterfall.
  A flat waterfall means stop.

### Phase 1: presence benchmark (Step 08 against Step 09 and ESPectre)

**Goal.** Measure Step 08's moving-person detection, its still-person
retention and its empty-room false alarms. Measure the same things for
Espressif's `esp_wifi_sensing` (Step 09) and ESPectre. Use the **same board,
position, AP, channel and room**, and flash them one after another (README
*Step 09 — the benchmark*).

**Equipment:** tape measure, floor tape for a grid, a stopwatch or phone
clock synced to the PC, a written log sheet, and one walker. Record the
walker's height, weight and clothing. IEC 63180 defines a reference test
person for PIR detectors; here that is only recorded.

**Before comparing, check the inputs match.** esp-radar and ESPectre choose
their own CSI configuration, and ESPectre switches the C5 to 8-bit mode
(COMPARISON.md §4). If one firmware scores HE frames and another L-LTF, the
comparison is silently meaningless (README *Step 09 — the benchmark*). Record each
firmware's CSI length and mode. If they differ, label the comparison
"different inputs".

**The grid.** This follows IEC 63180 / NEMA WD 7 in style. Neither is
followed exactly: both are written for PIR, ultrasonic or microwave sensors,
and neither sets a pass/fail line (COMPARISON.md §3, *Presence test
method*).

- Mark the area between board and AP in cells **no larger than 0.9 m ×
  0.9 m** (3 ft × 3 ft).
- Walk at about **1.2 m/s** (4 ± 0.5 ft/s), with a **2 s stop at the edge of
  each cell**.
- These three numbers are NEMA WD 7 as quoted by Cooper Lighting
  (<https://www.cooperlighting.com/global/controls-wiring/wd7-occupancy-sensor-testing>)
  and Electronic Design, 2012
  (<https://www.electronicdesign.com/technologies/power/alternative-energy/article/21194550/how-to-test-occupancy-sensors>).
  The standard itself was not read; the fact check was done on 2026-09-25.
- IEC 63180 separates **major motion** (walking into or within an area) from
  **minor motion** (small movements of a person or a part of one). That split
  maps onto README's "moving: works / still: unreliable".
- Walk **tangential** passes (across the board–AP line) and **radial** passes
  (along it). IEC 63180 has a separate test grid for each of these two walk
  directions.
- The IEC 63180 terms and grid types come from its official preview (scope,
  definitions, table of contents):
  <https://cdn.standards.iteh.ai/samples/101399/6e494c4306994c8aa87359b149d0b374/IEC-63180-2020.pdf>.
  Its catalogue page is listed in COMPARISON.md §6.

| Test | Procedure | Record | Pass mark | Origin |
|---|---|---|---|---|
| 1a Major motion | ≥ 3 tangential + 3 radial passes over the grid per firmware, starting from an empty, calibrated room | per cell: detected yes/no, and the time from entering the room to `present`=1 (Step 08 JSON / Step 09 `ACTIVE`). Also the fraction of walking seconds reported empty | walking seconds reported empty **≤ 0.001 per second**, i.e. ≤ 0.1 % of walking time after entry | TGbf pre-standard target sheet 11-20/1712r2: "miss ≤ 0.001" per second (COMPARISON.md §3). IEEE 802.11bf itself sets **no** bar |
| 1b Minor motion | seated, 5 min of small arm and hand movements | fraction of time PRESENT | none: record it | no bar exists (COMPARISON.md §3) |
| 1c Still person | walk in (detected), sit still, breathe normally, 10 min. For Step 10, run once with `CSI_VITALS_HOLDS_PRESENCE` on and once off | fraction of time PRESENT, time until the first lapse | none: record it. README calls still-person detection unreliable (README *Read this before you start*, table) | – |
| 1d Empty room overnight | ≥ 8 h, room empty and closed. Note fans, HVAC, pets and neighbours. `presence_monitor.py --csv` | PRESENT onsets per hour; fraction of empty seconds reported PRESENT | **≤ 0.0001 per second**, i.e. ≤ 2.9 PRESENT-seconds in an 8 h night | TGbf target "false alarm ≤ 0.0001" per second (COMPARISON.md §3) |
| 1e Same room, all three | repeat 1a–1d with Step 09 and with ESPectre | as above, plus each firmware's version or commit and its settings | project mark: Step 08 is **not worse than Step 09** on 1a and 1d in the same room | project mark (2026-09-25). ESPectre's published C5 F1 (98.9 % Lightweight / 99.5 % High-Accuracy in its 2026-10-07 report; 97.2 % / 99.9 % when this row was written) is its own host replay of HT20/HT-LTF recordings, not a run on the chip (COMPARISON.md §1, §5); compare on these recordings, not on its README |

**Notes:**

- Step 09 prints one line per second with the state (`ACTIVE` / `DEBOUNCE` /
  `inactive`), jitter, wander and heap (step09_espressif_sensing/main/main.c:274-285).
  Capture it with `serial_capture.py`. Its timestamps are relative, so write
  down the wall-clock start time.
- Step 09 trains for 30 s after a 10 s leave-the-room delay
  (step09_espressif_sensing/main/main.c:52-57). Step 08 calibrates for about
  45 s (README *The 30-second version*). The room must be empty for both.
- Flashing another firmware may leave or overwrite NVS. After switching back
  to Step 08 or 10, check the boot log for the loaded baseline, or
  recalibrate (`recal` or the 2 s button press).
- ESPectre: <https://github.com/francescopace/espectre>. It targets the C5
  and runs on the chip (REFERENCES.md §2.6). Install it following its own
  documentation. Record its detector (`lightweight` or `high_accuracy`) and
  its version.
- `tools/session_runner.py --protocol phase1` plays 1a–1c (not 1d or 1e)
  with cues, and `tools/session_score.py` scores the log against the 1a and
  1d marks (§2.7). The scorer reads Step 08 and Step 10 JSON `detect` rows
  only, not Step 09's state lines. The grid walk inside each pass is the
  walker's job.
- **Step 10's activity state** (D-172, built 2026-10-16, never run on the
  board) is scored in the same phase1 run when the board runs Step 10: the
  scorer compares each row's `activity` with the segment's cue (empty →
  `empty`, 1a walk → `moving`, 1b minor → `restless`, 1c still → `still`)
  and prints a match fraction per segment and a confusion table (§2.7).
  **There is no mark**, and none is proposed here. Read 1c with the hold in
  mind: a lasting `still` is in practice PRESENT held by the breathing hold
  (activity.h), so with `CSI_VITALS_HOLDS_PRESENCE` off it rarely appears.
  The thresholds are synthetic (D-83), so 1b `restless` versus 1c `still`
  is the comparison most likely to disagree.
- **A reference sensor would make the truth better than the cue sheet**
  (an option, not a plan; nothing is bought or built, D-173). The cue
  schedule is the only truth today (§2.7). A 60 GHz mmWave module that
  reports presence, breathing and heart rate by itself, such as the
  DFRobot C1001 (SEN0623) in the How To Electronics build of 2026-10-08
  (COMPARISON.md §7), logged beside the C5, could give an independent
  occupancy record for 1a-1d and a breathing reference for Phase 3. Its own
  accuracy is unpublished (the article publishes none), its chest range is
  about 0.4-1.5 m, and its serial protocol is not publicly documented, so it
  would be a second opinion, not ground truth. Phase 3's chest strap stays
  the heart-rate reference (COMPARISON.md §3).

### Phase 2: Step 10 resources (heap, per-packet time, drops, 100 Hz)

**Goal.** Measure the numbers README says have never been measured: free
heap, per-packet processing time, and dropped packets (README *Verification
status — please read*; README **Cost.**, the paragraph of README *Kconfig —
`menuconfig` → Recognise / count / vitals* that starts with that word, cited
below as README **Cost.**). Then check that 100 Hz is sustainable, because
recognition needs it.

**Equipment:** the board, the AP, and one person to exercise the worst
packet. That is a packet on which a gait window, a count block and a vitals
FFT land together, and it has never been timed (README **Cost.**). So walk, then
sit still.

| Step | Do | Record |
|---|---|---|
| 2a | Flash Step 10 with the defaults (20 ms ping). Capture the boot log | `free heap after start-up: N bytes` (main.c:3575) |
| 2b | 30 min at 50 Hz: 10 min empty, 10 min walking, 10 min seated. `presence_monitor.py --csv` plus `--send status` at the start and the end, or `session_runner.py --protocol phase2b` (§2.7), which sends `status` at the start, every 5 min and at the end | per JSON line: `rate_hz`, `proc_ms_max`, `dropped`. From `status`: the `pipeline:` line with the slowest packet and drops since boot (main.c:1800), and free heap |
| 2c | Set `CSI_PING_INTERVAL_MS` = 10, reflash, `recal` (README *Before recognising anyone*), repeat 2b | same, at 100 Hz |
| 2d | Watch for the firmware's own warnings | `N CSI packets dropped in the last minute …` (main.c:3082); `live CSI rate … more than 20% off the calibrated …` (main.c:3172) |

| Pass mark | Origin |
|---|---|
| `dropped` stays **0** for each 30 min run | project mark (2026-09-25). A drop means the pipeline fell behind its 16-packet queue, which covers 320 ms at 50 Hz and 160 ms at 100 Hz (README **Cost.**) |
| live rate **≥ 90/s** (60 s mean) at a 10 ms ping, i.e. within 10 % of 1000 / 10 = 100/s, and within 20 % of the calibrated rate | **project mark** (2026-09-25). README *Before recognising anyone: raise the packet rate* says gait needs ≥ 100 packets/s and the rate line "should read ~100/s", but one reply per 10 ms ping gives about 100/s, so a single lost reply fails a bare 100. 20 %: the firmware's own warning threshold (main.c:3172; README **Cost.**) |
| free heap from `status` at the end is not below the value at the start | project mark: a cheap leak check. Record the minimum seen |
| `proc_ms_max`: **no mark**. Record the maximum and the typical value, and compare them with the header estimates of ~6–10 ms per vitals FFT and ~12–32 ms per gait window (README **Cost.**) | estimates, not measurements |

**If `dropped` climbs at 100 Hz**, go back to a 20 ms ping or switch off a
Step 10 feature (README *Kconfig — `menuconfig` → Recognise / count / vitals*,
after the live-rate warning example). Phases 4–5 then run at 50 Hz, and
recognition is expected to suffer.

### Phase 3: vitals (breathing and experimental heart rate) against references

**Goal.** Put numbers on `br_bpm` and `hr_bpm_exp` against a trusted
reference. **Not a medical device.** `hr_bpm_exp` stays labelled experimental
whatever this phase shows. The FDA General Wellness guidance requires
validation before a displayed value may "mimic those used clinically", and
forbids clinical-accuracy claims (COMPARISON.md §3).

**Equipment:**

- **Polar H10 chest strap.** It is the reference COMPARISON.md §3
  recommends: 99.6 % beat-interval signal quality against raw ECG. Apple
  Watch-class wrist heart rate is too loose (same row). You also need an app
  that exports time-stamped heart rate to CSV. None is prescribed here;
  record which one you used.
- A **metronome** (a phone app is fine) for paced breathing.
- The PC running `presence_monitor.py --csv`.
- **Clocks synced** between the phone and the PC. `vitals_validate.py` pairs
  rows within ±2 s (`--max-skew-s`).

**Protocol** (README *Validating heart rate — do this before you believe it*):

1. One person, **alone**, walks in and sits still. Wait for the first
   estimate, which needs about 20 s of stillness (README *Breathing and heart
   rate, in six lines*).
2. **Breathing, 3a:** paced breathing at **10, 15 and 20 breaths/min**, at
   least 5 min per rate after the first estimate. All three rates lie inside
   the reportable band of about 7.6–28.7/min (README *Breathing and heart rate, in six lines*, item 4). Make the
   reference CSV yourself: rows of `time,bpm` holding the metronome rate for
   each block.
3. **Heart rate, 3b:** at least 5 min seated rest with the H10, then again
   straight after climbing stairs, so the reference actually **changes**
   (README *Validating heart rate — do this before you believe it*, step 3).
4. Run the comparison:
   ```powershell
   python tools\vitals_validate.py --csi session.csv --ref metronome.csv --kind br --tolerance 2
   python tools\vitals_validate.py --csi session.csv --ref h10.csv --kind hr
   python tools\vitals_validate.py --csi session.csv --ref h10.csv --kind hr --offset-s -15   # centre of the 30 s window
   ```

**Metrics:**

| Metric | Computed by `vitals_validate.py`? |
|---|---|
| coverage, MAE, RMSE, bias, % within tolerance, correlation, best-constant baseline, TRACKING / ACCURACY verdicts | yes (vitals_validate.py:323-433) |
| **MAPE** | **no**: compute it offline from the paired rows, or extend the script. COMPARISON.md §3: "script has no MAPE or EC13 rate" |
| **Bland-Altman LoA** (bias ± 1.96 × SD of the differences) | **no**: the script prints the bias, not the SD of the differences. COMPARISON.md §3: "script has no LoA" |
| **EC13 pass rate** (share of pairs within max(5 bpm, 10 % of the reference)) | **no**. The script's "% within ±5 bpm" is a lower bound on it whenever the reference is ≥ 50 bpm, because 10 % is then at least 5 bpm |

The script does not export the paired rows. Adding a `--pairs out.csv`
option, plus MAPE, LoA and the EC13 rate, is an **open work item** that
should be done before Phase 3 is scored.

**Bars and pass marks:**

| Output | Bar | Origin |
|---|---|---|
| Heart rate | each reading within **±10 % or ±5 bpm, whichever is larger** (EC13). Report the pass rate | ANSI/AAMI EC13:2002, which covers ECG devices only and is superseded, but papers borrow it (COMPARISON.md §3) |
| Heart rate | **MAPE ≤ 10 %** (consumer bar, measured during activity) | ANSI/CTA-2065, 2018 edition (COMPARISON.md §3) |
| Heart rate | the script prints `ACCURACY: beats the best constant guess` **and** `TRACKING: yes` on the session with stairs | project mark (2026-09-25), from the script's own design (vitals_validate.py:56-63) |
| Breathing | Bland-Altman **LoA within ±3 breaths/min**, at each metronome rate | clinical convention (Toften 2024), COMPARISON.md §3. No ISO/IEC bar was found |
| Both | report coverage next to accuracy. A silent estimator that is right 5 % of the time is not much use (vitals_validate.py:50-54) | – |

Rows are 0.5 s apart while the estimate changes about every 2 s, so N
overstates the independent evidence (vitals_validate.py:64-65). One session,
one person and one room is a result for that session only. **Do not describe
heart rate as validated** on the strength of it.

### Phase 4: recognition (who)

**Preconditions:**

- Phase 2 passed at 100 Hz: `CSI_PING_INTERVAL_MS` = 10, then `recal`
  (README *Before recognising anyone: raise the packet rate*).
- If more than one person will be in the room at once, train the counter
  first. Without that, two walkers are classified as one (README *How it answers*).

**Equipment:**

- **2–3 enrolled walkers and 1 walker who is never enrolled.** The right
  answer for that person is `"unknown"` (README *Validating heart rate — do this before you
  believe it*, last paragraph).
- A written log with wall-clock times.
- `presence_monitor.py --csv`.
- There is no scoring script, so scoring is by hand (README *Validating heart rate*, last paragraph;
  COMPARISON.md §3: "no scoring script").

**Enrolment.** Enrol each person exactly as README *Enrolling a person*
says:

- The walker is alone, at normal pace, taking several different routes.
- Collect 30 windows.
- Budget about 65 s of continuous walking at 100 Hz.
- Record how long it took. The pass rate of real walking windows has never
  been measured (README *Enrolling a person*, **How long it takes.**).

**Protocol** (project design, 2026-09-25):

- Each walker makes **≥ 10 visits** in randomised order, one person at a
  time.
- A visit is: walk in, keep walking for 60 s on a route that is **not** an
  enrolment route, then leave.
- Wait until the JSON shows `present`:0 for ≥ 10 s before the next visit.
  The votes are cleared when presence ends (README *How it answers*).
- Log the walker's name and the start and end times of every visit.

**Scoring:**

- Take the last non-null `person` of each visit.
- Sort it into one of four outcomes: own name / other name / `"unknown"` /
  `null`, where `null` means no decision.

| Metric | Definition |
|---|---|
| **FNIR** | enrolled visits that did not end in the walker's own name ÷ enrolled visits |
| **FPIR** | never-enrolled visits that ended in any enrolled name ÷ never-enrolled visits |
| **Confusion matrix** | rows: true walker (A, B, C, stranger). Columns: A, B, C, `unknown`, `null` |
| time to first decision | from the start of the visit to the first non-null `person` |

**Bar:** **there is none.** ISO/IEC 19795-1:2021 sets no threshold. It says
an open-set system should report FPIR and FNIR (COMPARISON.md §3).

For context only:

- WifiU measured FAR 8.05 % and FRR 9.54 % with an Intel 5300 (COMPARISON.md
  §3).
- The best published ESP32 figure is 95 % for 2 people and 74 % for 3, in a
  controlled room with a closed set (README *Read this before you start*, table; REFERENCES.md §3.4).

The minimum sanity check is that the result beats chance: with k enrolled
people, a guess is right 1/k of the time. Report N next to every figure.

### Phase 5: counting (0 / 1 / 2+ movers)

**Preconditions:**

- `count-train 1` and `count-train 2` are complete, at the ping rate the test
  will use (README *Training the counter*).
- Until the counter is trained, `count` is 1 whenever anyone is present
  (`count_src:"presence"`, README *Training the counter*). An untrained run tests
  nothing.

**Protocol** (project design, 2026-09-25):

- Script blocks of **0, 1 and 2 people moving**, each ≥ 2 min, each repeated
  ≥ 5 times in randomised order.
- Keep a written log with wall-clock start and end times.
- `presence_monitor.py --csv`.

**Scoring:**

- Compare `count` in each JSON row (every 0.5 s by default,
  `CSI_JSON_PERIOD_MS` 500) with the log.
- **Exclude the first 30 s after each change at 50 Hz, or 15 s at 100 Hz.**
  This is a project choice. `count` is the majority of the last 5 blocks of
  256 packets, so a change needs about 3 new blocks: roughly 15 s at 50 Hz
  plus block alignment (README *Training the counter*).
- Report:
  - a confusion matrix: truth {0, 1, 2} × reported {0, 1, 2};
  - exact-count accuracy;
  - per-class recall;
  - the `count_src` mix.

**Bar:** **none found** (COMPARISON.md §3, *Counting*). For context only:

- Wi-Monitor: 84.6 % exact for 0–3 people with 1 ESP32 pair and a host DNN.
- Wi-CaL with one link: median error 0.55–0.82 for 0–10 people.

Both are in COMPARISON.md §2B.

**Known limits to keep in mind when reading the numbers:**

- People sitting still are invisible to the counter.
- The counter only separates 1 from 2+ (README *What each feature does*).

---

## 5. Results log

**This section is the record of what has been verified.** README
*Verification status — please read*, ARCHITECTURE §1, TESTING §0 and the
DECISIONS header summarise it, and they have diverged before. Add a row for
**every** hardware session and for every software check that changes a
number in README *Verification status*. Newest at the bottom. In the same
commit as a new row:

1. Update TESTING §0, ARCHITECTURE §1 and README *Verification status* to
   the new date, and point them at the §5 row instead of copying its
   numbers.
2. For each DECISIONS entry the result bears on, add the row's date to its
   Evidence. Change its Status from `unverified on hardware` to `active`
   only if the row shows that behaviour observed on the C5. A result that
   contradicts an entry gets a new entry that supersedes it; never delete
   one.
3. If the result contradicts a document, add a DECISIONS Appendix A row.
4. Never write "validated" for recognition, counting, heart rate or the activity state (D-172) on the
   strength of one session.

**Raw logs** go outside the repo, for example
`D:\ESP32C5_CSI_logs\<YYYY-MM-DD>_<phase>\`: `.gitignore` has no `logs/`
entry, so a log folder inside the repo would be picked up by `git add .`.
Put the path in the *Raw log* column. Never commit `sdkconfig`, because it
holds Wi-Fi credentials.

### 5.1 Hardware

| Date | Phase | Board / port | Firmware | AP / channel | Room / setup | Measured | Result | vs bar | Raw log |
|---|---|---|---|---|---|---|---|---|---|
| 2026-09-25 | 0a | ESP32-C5, COM13, MAC `30:ed:a0:e4:50:b8` | Step 01 (`step01_hello`), HEAD 8c60baf; `step01_hello` unmodified, other folders have uncommitted changes | n/a (no Wi-Fi in Step 01) | desk | boot, heartbeat, free heap | **boots; heartbeat every second; free heap 338,196 B** | Step 01 pass (README *Step 01* shows `free heap 340000` as an illustration only) | session console capture (not kept in the repo) |
| 2026-09-25 | 0c–0e prep | – | Steps 03 / 04 **built only** with the laptop-hotspot credentials: `step03_csi_hello.bin` 0xd8e20 (58 % free), `step04_csi_geometry.bin` 0xd97a0 (58 % free), 0 `warning:` lines each | laptop hotspot | – | – | **CSI capture pending**: not flashed or run yet | – | build logs of the session |
| 2026-09-25 | 0b | same board; module **ESP32-C5-WROOM-1-N8R4** (8 MB flash, 4 MB PSRAM; firmware uses 4 MB flash, no PSRAM) | Step 03 | Windows 11 Mobile Hotspot "Testing", 2.4 GHz, laptop at 192.168.137.1 | board next to the laptop | association | first boot: `disconnected (reason 201)` x10 = AP not found - **the Windows hotspot had switched itself off** (it does after minutes with no client). After turning it on: associated, **channel 1**, IP 192.168.137.5 | – | [hw_logs/2026-09-25_step03_hotspot_off.log](hw_logs/2026-09-25_step03_hotspot_off.log) |
| 2026-09-25 | 0c | same | Step 03 (HT/HE capture only, legacy off) | same, ch 1 | same | CSI packets | **0 CSI/s** although the laptop sent echo replies at ~50/s (`netstat -s -p icmp`: +252 replies in 5 s) - the hotspot answers with legacy 11g frames only, which Step 03 does not capture | fail for Steps 03, 05-07 on this AP (HT/HE only); not a board fault | [hw_logs/2026-09-25_step03_hotspot_ch1.log](hw_logs/2026-09-25_step03_hotspot_ch1.log) |
| 2026-09-25 | 0d / 0e | same | Step 04 (captures every PHY, `dump_ack_en` = 1) | same, ch 1 | same | shape histogram, rate, drops, 12-bit test | **100.0 % of 2,163 packets = 106 B, 11g/11a (L-LTF)**; **~50.2 packets/s** (1,762 in 35.1 s); **0 dropped**; RSSI -39..-26 dBm; **odd-byte high nibble 0 in 100.0 %** -> **12-bit L-LTF packing confirmed on silicon** (26 subcarriers per 106 B frame, not 53) | 106 is one of the six legal lengths; rate >= 45/s project mark: **pass**; 12-bit hypothesis (DECISIONS: L-LTF 12-bit decode) **confirmed** | [hw_logs/2026-09-25_step04_hotspot_ch1.log](hw_logs/2026-09-25_step04_hotspot_ch1.log) |
| 2026-09-25 (capture file 19:24; logged 2026-09-28) | stability (no §4 check) / 2a | same | Step 10 with `CSI_LLTF_ONLY` = y, a build from **before** D-165 (its banner has no `last reset` line); whether D-164 or `CSI_EXPLAIN` was in it is not shown by the capture; the exact image was not recorded | same, ch 1; associates as `phy: ax`, RSSI -42, -42 and -41 dBm (three joins) | not recorded for this capture (row 0b, earlier that day: board next to the laptop) | boot, heap, shape lock, uptime | Boots, IP about 2.2 s after reset. **`free heap after start-up: 89,708` / `89,720` B** (two boots). One boot printed 51 `ping_sock: send error=0` lines, one every 40 ms from 2,280 to 4,280 ms of uptime (IP at 2,158 ms); the next boot printed none. Shape lock **106 B, 26 subcarriers, fwi 0, ch 1, 100.0 %** after 200 packets, then `no stored baseline - calibrating`. **COM13 vanished 4 times between 33 s and 65 s of the capture.** The first three losses were each followed by a fresh boot banner (at 33.98 s, 36.14 s and 48.78 s): once after 33 s with no output at all, once under 2 s after a reset (associated, no IP yet), and once in the leave-room countdown (uptime about 12 s, `remaining_s` 8). The fourth (64.67 s, uptime about 16 s, `remaining_s` 2) is followed only by the port reopening at 68.27 s, where the file ends. **No panic, abort, watchdog or brownout text** in the capture. That firmware did not print the reset reason, so the **cause is not known**. A manual RST press or a reset from the host is not ruled out, because nothing records what was done at the desk. Read from the ESP-IDF v6.0.2 source on 2026-09-28: a software restart (`esp_restart`) and the panic restart both reset the CPU and a fixed list of peripherals that does not include USB-Serial-JTAG, which keeps running (`components/esp_system/port/soc/esp32c5/system_internal.c`: `esp_system_reset_modules_on_exit`, `esp_restart_noos_inner`; `port/panic_handler.c`: `panic_restart`, whose system-reset branch is for the classic ESP32 only). So a port that **vanishes** points away from an ordinary firmware crash, towards a chip-level reset (RST pin, power loss, brownout, a watchdog stage that resets the whole system) or a USB drop on the host side. Not established | Phase 2a: start-up heap 89,708 / 89,720 B recorded, on a non-default build (`CSI_LLTF_ONLY` = y; §4 2a asks for the defaults); no mark. Stability (no §4 check or pass mark defined): **not established** | [hw_logs/2026-09-25_step10_boot_hotspot_ch1.log](hw_logs/2026-09-25_step10_boot_hotspot_ch1.log) |
| 2026-09-28 | – (state check) | same | whatever was on the board: its banner matches the pre-D-165 Step 10 above (no `last reset` line) | hotspot **off** (WinRT `TetheringOperationalState` = Off) | placement not recorded on 2026-09-28 (on 2026-09-25: board next to the laptop, row 0b) | 25 s passive listen | `baseline found: 106 bytes CSI, 26/26 subcarriers, ch 1, 50.2 Hz`. So **a Step 10 calibration did complete and was saved to NVS** at some point after the capture above; no log of that run was kept, and whether the room was empty then is not known. With the hotspot off: `disconnected (reason 201), retry N/10` every 3.4 s (D-159). The capture opened about 2.4 s after a boot (banner at 314 ms of uptime). With the hotspot off, Step 10 sets `WIFI_FAIL_BIT` at the 11th failed join (about 37.5 s) and calls `esp_restart()` 5 s later (D-165), a cycle of about 43 s that is consistent with this boot; no restart falls inside the 25 s | – | [hw_logs/2026-09-28_step10_oldfw_hotspot_off.log](hw_logs/2026-09-28_step10_oldfw_hotspot_off.log) |
| 2026-09-28 (12:01–13:52) | 0 (flashing) | same; `esptool chip_id`: ESP32-C5 rev v1.0, BASE MAC `30:ed:a0:e4:50:b8` | Step 10 image 0xef240 (D-164 and D-165 in, D-167 not yet), 0 warnings (§5.2) | hotspot turned back on with WinRT `StartTetheringAsync` (D-159) | placement not recorded on 2026-09-28 | flashing | (1) `esptool --port COM13 chip_id` succeeded and ended with `Hard resetting via RTS pin`. (2) About a minute later, `idf.py -p COM13 flash` (the pre-D-165 Step 10, with no `last reset` line, was last seen running in the listen before `chip_id`; whether it was running at this moment was not checked): **`Write timeout` at `Connecting...`**. The board then printed nothing in a 10 s listen, and one further `chip_id` also got `Write timeout`. esptool drove DTR/RTS both times (`chip_id`'s RTS hard reset; the flash's `--before=default-reset`). No other program did: `serial_capture.py` holds both low and never toggles them. Whether esptool's reset latched the chip (the D-153 mechanism) is not established. (3) The user held BOOT and tapped RST. Then `esptool --before no-reset --after hard-reset write-flash @flash_args` (bootloader, partition table and app) wrote all three images and verified their hashes (`Hash of data verified` ×3, then `Hard resetting via RTS pin`), **but the board then printed nothing**: 25 s of silence, `status` got no answer, and a further listen from 13:19:46 to 13:29:47 also got nothing. That is consistent with the app not having started, since a running D-164 image with a reader attached prints within seconds (retry lines, JSON rows or `console: output resumed`), but it was not observed directly. It printed its banner only after RST was pressed (`last reset: POWERON (1)`) | the D-158 recovery was needed. n = 1 | `D:\ESP32C5_CSI_logs\2026-09-28_step10_soak\flash10_0928.log`, `flash10_0928b.log` (outside the repo) |
| 2026-09-28 (13:51–14:07) | stability (no §4 check) / 2a / 2b (partial, 50 Hz) | same | same image 0xef240; `CSI_LLTF_ONLY` = y (not the default), `CSI_EXPLAIN` = y, 20 ms ping | hotspot "Testing", 2.4 GHz, **channel 6** (channel 1 on 2026-09-25; the board logged `wifi:new:<6,0>, old:<1,0>`). JSON `rssi` -66 to -25 dBm (-56 to -25 in the 900 s file; -66 in `listen_1352.log`) | placement not recorded on 2026-09-28. **Who was in the room during the `recal` and the rest of the run was not recorded**, so treat the new baseline as a still-room or occupied one (D-166) | boot, reset reason, stability, heap, rate, per-packet time, drops, calibration; 900 s capture from a script that reopens the port (G19) with `status` at +5, +120, +300, +600 and +880 s and `recal` at +40 s | **Boot:** `last reset: POWERON (1)` (the RST press). It loaded the stored channel-1 baseline (`ch 1`; saved at an unrecorded time after the 2026-09-25 capture, row above) and detected with it on channel 6: the firmware had no check for that, so nothing was said (D-167). **Before the `recal`** (channel-1 baseline): PRESENT in all 81 detect rows of the soak and all 30 of `listen_1352.log`; `dev_sd` 4.96–20.64 sd in the soak (20.04 over the 80 rows that parse) and -1.58 to 16.75 in `listen_1352.log`; `rssi` -26 to -33 dBm from uptime 74 s up to the `recal`, -27 in the last row before it and in the first row after calibration (`dev_sd` -1.04 there). **Console (D-164):** `output resumed - for ~14 s no host read it ... the detector kept running`, and ~29 s the second time. **Stability:** no restart and no port loss in the 900 s. Uptime ran continuously from 68 s to 943 s (`board: up` lines). **Calibration:** leave-room 10 s, warm-up 5 s, phases A and B 15 s each, `CALIBRATED (ESP_OK)`, 26/26 subcarriers, 50.2 packets/s, channel 6. Calibration statistics (the firmware labels them `empty`; occupancy not recorded): M 0.6051 ± 0.2334, D 0.9861 ± 0.3685. This **overwrote that channel-1 baseline in NVS**. **Resources:** free heap 94,248 B at 21 s and 68 s, then 94,132 B at every later `status` (183–943 s). Live rate 49.7–52.6/s in the five `status` reads of the soak (calibrated 50.2). **0 CSI packets dropped** since boot. Slowest packet since boot **52.2 ms** (`status` `pipeline:` line), set between about 33.5 s and 62.7 s of uptime, inside the ~29 s when no host read the console. The timing includes printing, so this is consistent with (not shown to be) the one ~50 ms console-write wait described at `console_install` (D-164), and it is not a measured gait-window cost. The slowest packet in any captured JSON row is 24.3 ms (`listen_1352.log`); over the soak's 1,670 detect rows (1,669 parse) `proc_ms_max` has median 2.3 ms, max 20.8 ms. **Outputs, with no ground truth:** after calibration, `present` = 1 in 544 of 1,587 rows (34.3 %; `soak_summary.py`'s 86 s cutoff leaves out the first two post-calibration rows, both 0), 30 onsets; `br_bpm` in 213 rows (7.6–10.5/min; 7.6 is the bottom of the reportable band); `hr_bpm_exp` in 9 rows (52.7–57.4). These say nothing about accuracy | Stability (no §4 check or pass mark defined): no restart in one 15-min run on the D-164/D-165 image. The 2026-09-25 restarts are neither explained nor reproduced. **Phase 2a: not recorded**: the `free heap after start-up` line fell in the dropped boot output; only the `status` free heap was captured (94,248 B at 21 s), on a non-default build. **Phase 2b: not met**: `dropped` was 0 for 15 min at 50 Hz, but the mark asks for 0 drops over one 30-min run (10 min empty, 10 walking, 10 seated); the run was incomplete and the activity neither controlled nor recorded. **Heap leak check (end not below start): not met as written**: 94,132 B at the end is 116 B below 94,248 B at the start; the drop falls between the 68 s and 183 s reads, which contain the `recal` (uptime 103 s), and the value stayed flat from 183 s to 943 s. Minimum seen 94,132 B. **Per-packet time:** no mark; see Result, and compare the header estimates of ~6–10 ms per vitals FFT and ~12–32 ms per gait window. Phase 2c (100 Hz) not run | excerpt: [hw_logs/2026-09-28_step10_soak_hotspot_ch6_excerpt.log](hw_logs/2026-09-28_step10_soak_hotspot_ch6_excerpt.log). Full capture, `listen_1352.log` (boot banner, `baseline found ... ch 1`, the `status` at 21 s), `soak.py`, `soak_summary.py` and its output: `D:\ESP32C5_CSI_logs\2026-09-28_step10_soak\` (outside the repo) |
| 2026-09-28 (18:57) | – (state check) | same; COM13 identified with pyserial `list_ports`, which does not open the port: VID 0x303a, PID 0x1001, serial number `30:ED:A0:E4:50:B8` (the BASE MAC); COM3 and COM4 are Bluetooth. No `esptool chip_id`: its RTS hard reset preceded the G20 `Write timeout` earlier that day | Step 10 0xef240 (flashed 2026-09-28, row 12:01–13:52; built before D-167 and D-168) | hotspot "Testing" **off** throughout this session (not turned on) | board next to the laptop (< 1 m; the user's answer, about 19:05 on 2026-09-28). The user said other people are usually present in this office (D-166); who was present during this capture was not recorded | port check; 20 s listen, `python tools\serial_capture.py COM13 20 --send status`, no reset of any kind. The first run on the C5 of `serial_capture.py` with its reopen loop and write timeout (§5.2) | Banner (uptime 317–324 ms) with `last reset: SW (3)` and `baseline found: 106 bytes CSI, 26/26 subcarriers, ch 6, 50.2 Hz`, then `disconnected (reason 201), retry 1/10` to `7/10` about every 3.4 s (AP not found, D-159). `retry 2/10` at uptime 6,751 ms came 2.26 s into the capture, so the capture opened about 4.5 s after a boot. The image was in its self-restart cycle with the hotspot off (D-165; row 2026-09-28 state check above). **The `status` sent at 4 s got no reply**; the write raised no error (no `port lost` line). In the Step 10 source the console task is created only after the Wi-Fi join: `app_main` calls `wifi_init_sta()` at step10 main.c:4673, waits for `WIFI_CONNECTED_BIT` or `WIFI_FAIL_BIT` at 4675–4682 with `esp_restart()` on failure, and creates the console task at 4689–4700. Its `usb_serial_jtag_read_bytes` (main.c:3165) is the only console read in that file, so with no join nothing reads the console. That ordering predates D-167 and D-168 and is not part of them | no §4 mark applies | `D:\ESP32C5_CSI_logs\2026-09-28_flash_d168\preflight_listen.log` (outside the repo) |
| 2026-09-28 (19:00) | 0 (flashing) | same; esptool: `Chip type: ESP32-C5 (revision v1.0)`, `BASE MAC: 30:ed:a0:e4:50:b8` | Step 10 **0xef360** (979,808 B), D-168 and its four refinements: the third build in the §5.2 row on the D-168 refinements (`build10_0928h.log`; `.bin` and `.elf` dated 2026-09-28 18:20; the flash's `ninja all` step compiled nothing). ELF SHA-256 `8bcc0f492e10fbc0cd0ed1a5d90a3ff28bd5a5e3734d3008748e32e5f893c8e1` | hotspot off | same | flashing, 19:00:15–19:00:24 | `idf.py -p COM13 flash` from `step10_recognise_count_vitals` after `export.ps1`, **with no `esptool chip_id` before it**; the 0xef240 image was last seen at 18:57 in its `reason 201` cycle (row above), and its state at 19:00:15 was not observed. esptool v5.3.1, `--before=default-reset --after=hard-reset`: `Connecting...`, then the chip type and BASE MAC above; wrote 23,232 B at 0x00002000 (bootloader), 3,072 B at 0x00008000 (partition table) and 979,808 B = 0xef360 at 0x00010000 (app, 4.2 s); `Hash of data verified` ×3; `Hard resetting via RTS pin...`; `Done`. **No `Write timeout`.** The app then ran without an RST press (next row; inferred from its uptime). The stored channel-6 baseline was still in NVS after this plain `flash` (next row) | no §4 mark applies (Phase 0 has no flashing check). n = 1: whether skipping `chip_id` is why there was no `Write timeout` is not established (G20) | `D:\ESP32C5_CSI_logs\2026-09-28_flash_d168\flash10_0928h.log` (outside the repo) |
| 2026-09-28 (19:00:33–19:01:33) | stability (no §4 check) | same | 0xef360 (row above). The new boot printed `ELF file SHA256: 8bcc0f492...`, the first 9 hex digits of that ELF's SHA-256, and `App version: 8c60baf-dirty`. Its `Compile time: Sep 25 2026 20:08:16` does not identify the image: the app description is not recompiled when only `main.c` changes | hotspot off | same | boot, reset reason, a restart while `serial_capture.py` holds the port; 60 s capture, no `--send` | At 1.03 s: `W (10061) csi-human: disconnected (reason 201), retry 3/10`, so an app was running at about 10 s of uptime, about 9 s after the flash ended. Its banner came before the capture opened, so that it started after the flash's hard reset without an RST press is **inferred** from that uptime (the 13:19 recovery flash, row 12:01–13:52, needed RST). `retry 10/10` at uptime 33,953 ms (25.00 s); `E (37367) csi-human: WiFi failed. Check SSID/password (must be 2.4 GHz).` at 28.30 s; 5 s later (`wifi:flush txq` at 42,367 ms) the ROM banner `ESP-ROM:esp32c5-eco2-20250121` and `rst:0xc (SW_CPU),boot:0x18 (SPI_FAST_FLASH_BOOT)` at 33.44 s. That is the firmware's own `esp_restart()` after `WIFI_FAIL_BIT` (step10 main.c:4678–4681; the D-165 cycle), **observed on the C5**. The new boot: `ESP-IDF: v6.0.2`, `flash io: qio`, `Detected size(8192k) larger than the size in the binary image header(4096k)`, the Step 10 banner with `last reset: SW (3)` and `baseline found: 106 bytes CSI, 26/26 subcarriers, ch 6, 50.2 Hz`, then `no recognition model yet`, `no vitals log yet`, `no count model yet`, and `retry 1/10` to `7/10` up to 57.05 s. **No `port lost` line anywhere: COM13 stayed open across the software restart, and the capture recorded the whole reboot** (G19). So `serial_capture.py`'s reopen loop was not exercised, and nor was its write timeout (no write in this capture). **Not observed:** the D-168 channel check (no Wi-Fi join, so no shape lock); the `free heap after start-up` line (printed only after the join, step10 main.c:4701); any CSI; accuracy | no §4 mark applies. Stability (no §4 check or pass mark defined): one `esp_restart()` in the 60 s, the designed cycle with the AP absent. **Phase 2a: not recorded** (no join, so no start-up heap line) | [hw_logs/2026-09-28_step10_d168_flash_boot_hotspot_off.log](hw_logs/2026-09-28_step10_d168_flash_boot_hotspot_off.log) (a full copy of `boot_after_flash.log`, 122 lines); `D:\ESP32C5_CSI_logs\2026-09-28_flash_d168\boot_after_flash.log` (outside the repo) |

### 5.2 Software checks

| Date | Check | Result | Source |
|---|---|---|---|
| 2026-09-25 | `run_tests.ps1`, all 8 suites | 11,435 checks, 0 failures: dsp 5,980, ncc 731, vitals 2,386, gait 108, count 592, gate 97, pvitals 1,304, e2e 237 | README *Verification status — please read* |
| 2026-09-25 | e2e rooms 5000–5008 at 50 Hz | e1 passes in 7 of 9 rooms; 5004 and 5007 fail (walker never recognised) | README *Verification status — please read*; test_e2e.c:1571-1585 |
| 2026-09-25 | e2e rooms 5004 and 5007 re-run with the §2.1 recipe | both `test_e2e: 223 checks, 1 failures -> FAIL` (e1; gate MOVING 30.7 % and 26.0 %) | this file, §3.3 |
| 2026-09-25 | `tools/kconfig_matrix.py` (repo copy, first run) | negative control `rc=1 errors=3 -> fails as expected`; `combos: 96, with warnings/errors: 0`; exit code 0 | this file, §2.2 |
| 2026-09-25 | Step 10 firmware build, full rebuild (1,089 steps) after the `person_vitals.h` and `main.c` comment edits and the hotspot credentials | 0 warnings, 0 errors, 0xea3d0 bytes, 54 % free | build log |
| 2026-09-25 | build status of every project, from `build/*.bin` and `main/main.c` file dates (no build run) | built after the 2026-09-22 edits: Steps 01, 03, 04 (2026-09-25), 08 (2026-09-23), root (2026-09-24), 10 (2026-09-25). **Steps 02, 05, 06, 07, 09: last built 2026-09-07, before their `main.c` changed on 2026-09-22** | `stat` of the files; §0 |
| 2026-09-25 | `run_tests.ps1 -Only e2e`, default seed 5000 (documentation review) | `test_e2e: 237 checks, 0 failures -> PASS`. Path-frozen case: `enrolled A/B: 20.6% correct, 79.4% unknown`, walks `A 1/0/8, B 0/3/6` (14 of 18 walks unknown). Settles DECISIONS A-24 in favour of README's 79 % | console output of that run |
| 2026-09-25 (found 2026-09-28) | Step 10 image on disk after the `CSI_EXPLAIN` / D-164 / D-165 edits: `step10_recognise_count_vitals.bin` dated 2026-09-25 20:13, **979,520 B = 0xef240**, newer than `main.c` (20:11) and `Kconfig.projbuild` (20:10) | this build was **never logged**. The 0xea3d0 figure elsewhere in the docs is an earlier full rebuild (2026-09-25 15:30); a 2026-09-25 19:00 build with `CSI_LLTF_ONLY` = y (0xea3e0, 0 `warning:`) came between them. Both logs are in a previous session's scratch folder, outside the repo. Warning count of the 0xef240 build not recorded; the 2026-09-28 forced recompile below gives the same size with 0 warnings | `ls -l` of the files, 2026-09-28 (before that day's rebuilds) |
| 2026-09-28 | Step 10 build before flashing, with `main/*.c` touched so all 8 of Step 10's own objects recompiled | 0 `warning:`, 0 `error:`, 0xef240 (979,520 B), 53 % free. This is the image flashed first on 2026-09-28 and soaked (§5.1 rows 12:01–13:52 and 13:51–14:07); 0xef360 replaced it at 19:00 (§5.1 row 19:00) | `D:\ESP32C5_CSI_logs\2026-09-28_step10_soak\build10_0928b.log` |
| 2026-09-28 | Step 10 build after D-167 (a warning at shape lock when the stored baseline's channel differs, plus a note on the `status` line) | first build 0xef3f0; after the 2026-09-28 verification the `status` note lost its `'recal'` hint during a CSI outage (`cmd_recal` refuses then), and the final build is `main.c` recompiled, 0 `warning:`, 0 `error:`, **0xef410**, 53 % free. **Not flashed** | `build10_0928c.log`, `build10_0928d.log`, same folder |
| 2026-09-28 | `run_tests.ps1`, all 8 suites, after D-167 | 11,453 checks, 0 failures: dsp 5,980, ncc 749 (731 in the 2026-09-25 row), vitals 2,386, gait 108, count 592, gate 97, pvitals 1,304, e2e 237. `ALL HOST TESTS PASSED`, exit code 0; the same again on the final 0xef410 source | `host_tests_0928.log`, `host_tests_0928b.log`, same folder |
| 2026-09-28 | `tools/kconfig_matrix.py`, after the D-167 Step 10 build | negative control `rc=1 errors=3 -> fails as expected`; `combos: 192, with warnings/errors: 0`; exit code 0. The script now also varies `CSI_EXPLAIN` (its docstring), hence 192 rather than the 96 of 2026-09-25. The same again on the final 0xef410 source | `kmatrix_0928.log`, `kmatrix_0928b.log`, same folder |
| 2026-09-28 | Step 08 and root builds after D-167 | Step 08: 0 `warning:`, `Project build complete`, 0xdb340, 57 % free (the log's closing `BUILD FAILED` is the G2 redirection artefact). Root (`idf.py build`): 0 `warning:`, 0xdb350, 57 % free. The `sdkconfig` of both has `CSI_LLTF_ONLY` off, the HT/HE-only capture that got 0 CSI/s from the laptop hotspot in Step 03 (§5.1 row 0c, D-159), so as built they would get no CSI from it; neither has run | `build08_0928.log`, `buildroot_0928.log`, same folder |
| 2026-09-28 | Step 10 build after D-168 (automatic recalibration when the stored baseline comes from another channel, at lock and after lock; the D-167 warning and its `status` note removed) | `main.c` recompiled, 0 `warning:`, 0 `error:`, **0xef330**, 53 % free. **Not flashed** | `build10_0928e.log`, same folder |
| 2026-09-28 | `run_tests.ps1` and `tools/kconfig_matrix.py` after D-168 | 11,453 checks, 0 failures, `ALL HOST TESTS PASSED`; negative control fails as expected, `combos: 192, with warnings/errors: 0` | `host_tests_0928c.log`, `kmatrix_0928c.log`, same folder |
| 2026-09-28 | Step 08 build after D-168, with `CSI_LLTF_ONLY` = y set in its `sdkconfig` that day (the file is git-ignored, so this row is the record of the change) | 0 `warning:`, `Project build complete`, 0xdb340, 57 % free; `build/config/sdkconfig.h` has `#define CONFIG_CSI_LLTF_ONLY 1`. The first Step 08 image that could take CSI from this hotspot; its 12-bit L-LTF decode has not run on the board | `build08_0928b.log`, same folder |
| 2026-09-28 | root build after D-168 (its own `sdkconfig`, `CSI_LLTF_ONLY` off, left alone) | 0 `warning:`, 0xdb340, 57 % free | `buildroot_0928b.log`, same folder |
| 2026-09-28 | `tools/serial_capture.py` reopen loop (G19) | `py_compile` OK. With a fake pyserial module: port lost at 0.00 s, two refused reopens 0.3 s apart, open again at 0.60 s, lines resumed, the `--send` went out, and DTR/RTS were False before every `open()` (the fake asserted it). A port that never appears (COM99, 2 s): exits 0. **Not yet run on the C5** at that point. Its first C5 runs, 2026-09-28 (§5.1 rows 18:57 and 19:00:33–19:01:33), lost no port, so the reopen loop is still not exercised on hardware | not kept (session console, 2026-09-28) |
| 2026-09-28 | Step 10, Step 08 and root builds after the D-168 refinements (DECISIONS D-168): Step 08's `PH_SAVE` stamps the channel of the packet it saves on (step08 main.c:672); Step 10's `PH_SAVE` saves nothing on a packet that adopted a new channel (step10 main.c:3722); channel 0 on either side counts as not known at `PH_LOCK` (step08 main.c:442-443, step10 main.c:3476-3477); with `CSI_FORCE_RECALIBRATE`, `PH_LOCK` logs `recalibrating (CSI_FORCE_RECALIBRATE)` instead of the shape-mismatch message (step08 main.c:465-467, step10 main.c:3504-3506) | Step 10: 0 `warning:`, 0 `error:`, **0xef360**, 53 % free; rebuilt after a comment fix (above the `channel_follow` call): the same 0xef360, 0 `warning:`; and again after a comment-only fix to `channel_follow`'s header: 0xef360, 0 `warning:`, 0 `error:`. Step 08 (`CSI_LLTF_ONLY` = y): 0 `warning:`, 0xdb340, 57 % free. Root (`idf.py build`, its own `sdkconfig`, `CSI_LLTF_ONLY` off): 0 `warning:`, 0xdb350, 57 % free. **None flashed** at the time (the board then had 0xef240). Step 10 0xef360 from `build10_0928h.log` was flashed at 19:00 the same day (§5.1 row 2026-09-28 (19:00)) | `build10_0928f.log`, `build10_0928g.log`, `build10_0928h.log`, `build08_0928c.log`, `buildroot_0928c.log` in `D:\ESP32C5_CSI_logs\2026-09-28_step10_soak\` (outside the repo) |
| 2026-09-28 | `run_tests.ps1` and `tools/kconfig_matrix.py` after the D-168 refinements | 11,453 checks, 0 failures: dsp 5,980, ncc 749, vitals 2,386, gait 108, count 592, gate 97, pvitals 1,304, e2e 237; `ALL HOST TESTS PASSED`. No host test exercises the refinements: `main.c` is not host-compiled, and `test_e2e.c` models neither baseline reuse nor `channel_follow` nor `PH_SAVE`. Matrix: negative control `rc=1 errors=3 -> fails as expected`; `combos: 192, with warnings/errors: 0`; the same again after the comment fix | `host_tests_0928d.log`, `kmatrix_0928d.log`, `kmatrix_0928e.log`, same folder |
| 2026-09-28 | `tools/session_runner.py` (new, DECISIONS D-169; §2.7), **offline only** | `py_compile` with `-W error`: OK. `--dry-run`: phase1 20 segments, 2040.00 s; phase2b 4 segments, 1820.00 s. `--simulate` of `soak10_0928.log` at `--scale 0.02`, phase2b and phase1: both end `# END <t> completed`; 0 board lines failing `dashboard.py`'s `TS_RE`, 0 `#` lines failing `MARK_RE`, 0 segment end / next start time mismatches, 0 timestamps going backwards. A review found 10 defects and a skeptic confirmed 9; all 9 were fixed and re-tested (among them: `serial_capture.py`'s `# port lost` notes not replayed by `--simulate`; a missing `--out` folder crashing after the port was opened; a failed start `status` never retried). The 10th (no write timeout) could not be reproduced without a port and was applied anyway: `write_timeout` 2 s, and the start `status` tried at most 3 times. With a fake pyserial module: one write timeout gave `# PORT 0.00 lost Write timeout ...`, a reopen, and `# SENT 0.30 status`; a send that kept failing (one `Write timeout`, then two failures from an earlier, incomplete fake that lacked `flush()`) gave `'status' (start) failed 3 times; not retried`. DTR/RTS were never touched after `open()`. The `final_sim` replays predate the write timeout and the last edit (17:59); `--simulate` re-run on the final file, phase2b and phase1: `# END 39.50 completed`, `# END 43.84 completed`, with the same 0 / 0 / 0 checks (`final_check_*.log`). **Not run on the C5** | `final_sim_p2b.log`, `final_sim_p1.log`, `final_check_p2b.log`, `final_check_p1.log`, `write_timeout_fake\run2.log`, `write_timeout_fake\run.log` in `D:\ESP32C5_CSI_logs\2026-09-28_session_tools\` (outside the repo) |
| 2026-09-28 | `tools/session_score.py` (new, D-169; §2.7), **offline only** | `py_compile` with `-W error`: OK. A review found 8 defects (among them: a missed `PH_SETTLE` live-rate warning; the designed presence hold counted as false alarms after an activity; a boot after a port loss not counted as a restart; drops hidden behind `not applicable`); all 8 were reproduced and fixed. The two `--simulate` logs above were scored, but the replayed soak had people moving at unrecorded times, so those verdicts mean nothing. **The 2026-09-28 soak** (§5.1 row 13:51–14:07), whole log: 1,714 JSON rows parsed, 1 unparsable; 1,669 `detect` rows; `dropped` max 0, never increased; `proc_ms_max` median 2.30, 95th percentile 11.00, max 20.80 ms; `rate_hz` 50.2; 5 `status` replies, heap first 94,248, last 94,132, minimum 94,132 B; 0 restarts (2,237 uptime readings). Heap mark NOT met (94,248 → 94,132 B; the first value predates the `recal` calibration, 40.27–84.92 s of the log); dropped mark not applicable (15.0 min < 30 min). With `--after-calibration` (after the `calib-B` row at 84.92 s): 1,589 `detect` rows, `present` 544 (34.2 %; 34.6 % with the 10 s grace left out), 30 onsets, `proc_ms_max` max 18.70 ms, 4 `status` replies all 94,132 B, heap mark met within that window. These agree with `soak_summary.txt` (544 of 1,587 = 34.3 %: its 86 s cutoff drops the first two post-calibration rows, both 0) and with §5.1. **The 2026-09-25 boot log** (`docs/hw_logs/2026-09-25_step10_boot_hotspot_ch1.log`): 3 boots, each within about 1 s after a port loss, and a 4th loss followed only by a reopen, as §5.1 records. None of this measures the accuracy of anything | `score_final_sim_p2b.txt`, `score_final_sim_p1.txt`, `score_soak_whole.txt`, `score_soak_after.txt`, `score_hwlog_0925_step10_boot.txt`, same folder as the row above |
| 2026-09-28 | `tools/serial_capture.py` write timeout (G20) | `write_timeout` 2 s, so a stuck `--send` write raises and is handled as a port loss, then the command is resent. With the same fake pyserial module: `# 4.02 port lost: Write timeout`, `# 4.02 port open: COM99`, the command resent; DTR/RTS never touched after `open()`. **Not run on the C5** at that point. On the C5 on 2026-09-28 (§5.1 row 18:57) the one `--send` write raised no error (no `port lost` line), so the timeout is still not exercised on hardware | `write_timeout_fake\cap.log`, same folder |
| 2026-10-12 | **Ubuntu 26.04 (same dual-boot machine, repo at `/mnt/vol1/ESP32C5_CSI`), ESP-IDF v6.1** installed with EIM v0.20.0 (`eim install -i v6.1 -t esp32c5`), builds through `tools/linux_idf.sh build` into `~/esp-build/<abs path>` with a per-folder copy of the Windows `sdkconfig` (docs/ide/README.md) | 10 of 11 projects build, 0 `warning:` each: Step 01 0x27880; 02 0xdaef0; 03 0xde050; 04 0xdea50; 05 0xde8e0; 06 0xdfe40; 07 0xdf360; 08 0xe0600 (`CSI_LLTF_ONLY` = y); 10 **0xf4460** (52 % free, `CSI_LLTF_ONLY` = y; ELF SHA-256 begins `6fed13e2d60a89ab`); root 0xe0600. Sizes differ from the Windows v6.0.2 images (Step 10 0xef360) because the IDF differs. **Step 09 fails**: ninja `.../espressif__esp_csi_gain_ctrl/6.1/esp32c5/libesp_csi_gain_ctrl.a ... missing` (the component ships prebuilt libs per IDF version and has no 6.1 folder; corrected 2026-10-16: this row first said "4.4 and 5.0 folders only", but the pinned copy has 4.4, 5.0, 5.1, 5.2, 5.3, 5.4, 5.5 and 6.0, row 2026-10-16 below); that build also rewrote `step09_espressif_sensing/dependencies.lock` (`idf` 6.0.2 -> 6.1.0), restored by hand the same day, and `linux_idf.sh` now refuses Step 09 on a non-6.0 IDF. **No v6.1 image has been flashed or run; no host tests or Kconfig matrix were run on Linux** | `~/ESP32C5_CSI_logs/2026-10-12_linux_v6.1/` (Ubuntu home, outside the repo) |
| 2026-10-12 | clangd on Linux (`esp-clangd` 21.1.3 from EIM) `--check=main/main.c` in Step 10 with the Linux compile database | without `--resource-dir`: `'stddef.h' file not found` and `va_start` errors (esp-clangd ships no clang resource headers); with `--resource-dir=<esp-clang>/lib/clang/21`: no diagnostics (2 `SwapBinaryOperands` tweak self-test failures only). An explicit `--compile-commands-dir` overrides a `.clangd` `CompilationDatabase` (clangd 23.1.0 `--check`, two-file test) | this file, G21 |
| 2026-10-16 | Ubuntu, ESP-IDF v6.1: `tools/linux_idf.sh build` of the same 10 projects, sources unchanged since 2026-10-12 (no file in the tree newer than 2026-10-12) | rc 0 and 0 `warning:` lines each; every `binary size` equals the 2026-10-12 row: Step 01 0x27880; 02 0xdaef0; 03 0xde050; 04 0xdea50; 05 0xde8e0; 06 0xdfe40; 07 0xdf360; 08 0xe0600; 10 0xf4460; root 0xe0600. Step 09 not attempted (refused on v6.1, D-170). **No board connected** (`doctor`: `port: none`; no `/dev/ttyACM*`), so nothing was flashed | `~/ESP32C5_CSI_logs/2026-10-16_linux_checks/` (Ubuntu home, outside the repo), `build_<step>.log` |
| 2026-10-16 | **Host tests on Linux, first run** (gcc 15.2.0, Ubuntu 15.2.0-16ubuntu1) with the new `step10_recognise_count_vitals/test/run_tests.sh` (§2.1; DECISIONS D-171): `gcc -std=c11 -O2 -Wall -Wextra -Werror`, binaries under `${TMPDIR:-/tmp}/esp32c5_host_tests` | First run: 7 suites pass; **`test_pvitals` did not compile** under `-Werror`: `test_pvitals.c:112` `missing initializer for field 'br_eps'` (`-Wmissing-field-initializers`, on `{ { 0 } }`) and `test_util.h:66` `'rng_gauss' defined but not used` (`-Wunused-function`); MSVC `/W4 /WX` accepts both. Built without `-Werror` it passed, 1,304 checks. Both fixed in test code only (`static const pv_rec_t z;`, `static inline` on `rng_gauss`); no firmware module changed. Re-run with `-Werror`: **8 suites, 11,453 checks, 0 failures**, per suite equal to the 2026-09-28 Windows row (dsp 5,980; ncc 749; vitals 2,386; gait 108; count 592; gate 97; pvitals 1,304; e2e 237); e2e seed 5000 prints the same path-frozen figures (20.6 % correct, 79.4 % unknown; A 1/0/8, B 0/3/6). An independent re-run reproduced the first run. The two test-code edits have **not** been re-run under MSVC | `~/ESP32C5_CSI_logs/2026-10-16_linux_checks/` (Ubuntu home, outside the repo), `host_tests_gcc.log` (first run), `host_tests_gcc_after_fix.log` |
| 2026-10-16 | **Kconfig matrix on Linux, first run**: `tools/kconfig_matrix.py --build-dir ~/esp-build/mnt/vol1/ESP32C5_CSI/step10_recognise_count_vitals` (new option, §2.2) against the v6.1 Step 10 build (riscv32-esp-elf-gcc esp-15.2.0_20251204); all 9 switched symbols defined in its `sdkconfig.h` (`CSI_LLTF_ONLY` = 1, not varied) | `combos: 192, with warnings/errors: 0`; `negative control (CONFIG_CSI_PING_INTERVAL_MS removed): rc=1 errors=3 -> fails as expected`; exit 0. An independent re-run into another `--out` gave the same log; the build dir's newest mtime was unchanged. Without `--build-dir` on Linux the script picks the Windows `build/` and fails every combination (rc 127, `.exe` compiler): a failure, but its message does not point to `--build-dir` | `~/ESP32C5_CSI_logs/2026-10-16_linux_checks/` (Ubuntu home, outside the repo), `kconfig_matrix.log` |
| 2026-10-16 | **Step 10 build with the activity state and presence episodes** (DECISIONS D-172): new `main/activity.c` / `activity.h`; `act_push` in `main.c` after count and recognition and before the vitals crediting, mirrored in `test_e2e.c` `gl_packet()`; Kconfig `CSI_ACTIVITY_ENABLE` (default y, depends on `CSI_VITALS_ENABLE \|\| CSI_ID_ENABLE \|\| CSI_COUNT_ENABLE`) and `CSI_ACTIVITY_RESTLESS_X100` (250); JSON `activity`, `activity_s`, `episode_s` appended after `vitals_of`; console `stats`. Ubuntu, ESP-IDF v6.1, `tools/linux_idf.sh build`, its `sdkconfig` with `CSI_LLTF_ONLY` = y. The sources after the review fixes (row below), re-run by the main session at 18:43 | `Project build complete`, 0 `warning:` lines: `step10_recognise_count_vitals.bin binary size 0xf56d0 bytes ... 0x10a930 bytes (52%) free`. ELF SHA-256 `5b043b58226872eaa6eb6d3e7fe12922f1adc1dbec61e52b00c05955960601a3` (linked at 18:30; the 18:43 build linked nothing new, and `sha256sum` of the ELF gives the same hash). Linker map: `.bss.s_act` 0xd8 = 216 B. Paper bounds in `main.c` comments, **not measured**: the JSON line at most 862 B + NUL of its 1,024-B buffer (was 793, DECISIONS D-128), the EXPLAIN `vitals` line 272 B. Step 08 and the root rebuilt in the same run: 0xe0600 each, 0 `warning:` lines, equal to the 2026-10-12 sizes; Step 08's `main.c` was not touched (mtime 2026-09-28). **Not flashed** (no board connected); **no Windows v6.0.2 build** of these sources | `~/ESP32C5_CSI_logs/2026-10-16_activity/final/` (Ubuntu home, outside the repo): `build_step10.log`, `build_step08_final_human_detector.log`, `build_root.log`; hash in `after_review/sha256.txt` |
| 2026-10-16 | Host tests on Linux after the activity state: `run_tests.sh` (gcc 15.2.0 `-Werror`) with the new `test_activity` suite and `activity.c` linked into `e2e`; main-session re-run of the final sources | **9 suites, 11,515 checks, 0 failures**: dsp 5,980; ncc 749; vitals 2,386; gait 108; count 592; gate 97; pvitals 1,304; **activity 57**; **e2e 242** (237 before; scenario `e4` added). `e4` at seed 5000, agreement of the activity state with the segment's truth, % of packets (reported, **not gated**): walks in 90.1 %, sits breathing 90.0 % (`still`), fidgets 1 s in 4 **35.4 %** (`restless`; 62.1 % read `still`), walks 100.0 %, left 71.9 % (`empty`). The `e2e` seed-5000 recognition figures are unchanged. `run_tests.ps1` lists `activity` but was **not run** (no Windows session); the gcc run is the only evidence for the new suite | `~/ESP32C5_CSI_logs/2026-10-16_activity/final/host_tests.log` (outside the repo) |
| 2026-10-16 | Kconfig matrix on Linux after the activity state: `tools/kconfig_matrix.py --build-dir ~/esp-build/.../step10_recognise_count_vitals`, new `CSI_ACTIVITY_ENABLE` dimension (§2.2) | `combos: 368, with warnings/errors: 0`; `negative control (CONFIG_CSI_PING_INTERVAL_MS removed): rc=1 errors=3 -> fails as expected`; 0 `warning:` lines in the log | `~/ESP32C5_CSI_logs/2026-10-16_activity/final/kconfig_matrix.log` (outside the repo) |
| 2026-10-16 | Review of the activity state before the final re-run: a review from four angles (lenses) of `activity.c`, its `main.c` wiring and its tests, with small harnesses (`reviewer_harnesses.txt`); then a mutation check of `activity.c` | **4 defects found and fixed**: (1) `empty` stayed reported while someone was PRESENT; (2) every walk ended through `restless`; (3) no test of a breathing-hold second bridging a WAIT; (4) threshold and window mutants survived the tests. After the fixes: 38 mutants (and a passing unmutated baseline), **34 caught, 4 survive** (`gap_ge`, `emptysec_count`, `hf_gt0`, `wait_held_notimeout`; §3.3 item 8). A 9-room `e2e` sweep (seeds 5000-5008) was run at 18:15, **before** these fixes: rooms 5004 and 5007 still fail e1 (`228 checks, 1 failures`), as §3.3 item 2 records; its activity figures predate the fixes and are not quoted | `~/ESP32C5_CSI_logs/2026-10-16_activity/after_review/` (`mutations.txt`, `mutations_list.txt`, `reviewer_harnesses.txt`) and `e2e_seeds_5000-5008.txt` (outside the repo) |
| 2026-10-16 | Python tools for the new keys: `presence_monitor.py` (`CSV_FIELDS` + `activity`, `activity_s`, `episode_s`; `STEP10_KEYS` + `activity`; a `*** <time>  activity: <state>` event line), `dashboard.html` (`CSV_FIELDS`, an *activity* tile), `session_score.py` version 3 (activity match per segment and a confusion table, §2.7). `session_runner.py` unchanged | `py_compile -W error` passes for `presence_monitor`, `session_score`, `session_runner`, `dashboard`, `csi_monitor`. A fake-port run of `presence_monitor.py`: CSV header ends `vitals_of, activity, activity_s, episode_s`, event lines `activity: moving (episode 12 s)` and `activity: still (episode 40 s)`. Regression: the 10 logs scored on 2026-09-28 (sample session, four `final_*` runs, the soak, `listen_1352`, `boot_after_flash`, the two `docs/hw_logs` Step 10 logs), each whole and with `--after-calibration` (20 runs): text differs only in the 2 tool-version header lines, JSON equal except `tool` / `written`, no `activity` key in any (no such log carries the key). A synthetic phase1 log (`make_synthetic.py`) with known activity: `check_synthetic.py` 25 `ok`, 0 failures; its score reads 80.2 % of 636 rows matching, verdict `no mark`. `dashboard.html` was **not** put through the §2.6 replay checks. Nothing ran against the board | `~/ESP32C5_CSI_logs/2026-10-16_activity/py/` (`py_compile.txt`, `fake_presence_monitor.txt`, `regression_diff.txt`, `synthetic/check_syn_p1.txt`, `tools_diff.patch`) and `after_review/py/` (outside the repo) |
| 2026-10-16 | Research, no build: ESP-IDF v6.0.2 (7101770dc) against v6.1 (fff9895c) for CSI; the Step 09 managed components; ESPectre's current report (DECISIONS D-173) | **No CSI change** in v6.1: `wifi_csi_acquire_config_t` (v6.1 `esp_wifi_he_types.h:44-65`), `wifi_csi_info_t` and `rx_ctrl` identical; the CSI calls unchanged; no new CSI Kconfig option; the C5's `soc_caps.h` Wi-Fi capabilities unchanged; the same CSI strings in `libpp.a` (the closed libraries were compared **by strings only**, so an internal fix cannot be ruled out). v6.1's Wi-Fi changes are NAN security, random STA MAC, OWE softAP, PASN, DPP and an FTM `ppm` field. `idf.py flash` on v6.1 writes only changed regions (v6.1 `docs/en/migration-guides/release-6.x/6.1/tools.rst`; G22). `esp_csi_gain_ctrl` 0.1.5 and `esp_radar_motion_dec` 0.1.1 (the latest) ship libraries in folders 4.4, 5.0-5.5 and 6.0, **no 6.1** (`find` in `step09_espressif_sensing/managed_components/`), which corrects the 2026-10-12 row's "4.4 and 5.0 folders only". `esp_wifi_sensing` 0.1.1~3 (2026-09-07) is newer than the pinned 0.1.1~2 (`dependencies.lock:111`) but adds no feature. ESPectre's performance report (last update 2026-10-07, `HT20/HT-LTF` view, 20 real paired datasets): C5 F1 98.9 % Lightweight, 99.5 % High-Accuracy, from host replay; COMPARISON.md cited 97.2 % / 99.9 % and is corrected | `~/ESP32C5_CSI_logs/2026-10-16_research/` (`plan.md`, `gh/espectre_perf.md`, `*_strings.txt`, `inc.diff`); v6.1 source at `~/.espressif/v6.1/esp-idf`; ESPectre `docs/performance/README.md` (COMPARISON §6) |
| 2026-10-30 | Research sweep, no build: four parallel passes (papers and datasets 2025-01 to 2026-10; open-source code and datasets; closed products, standards and patents; blogs, news, maker write-ups and videos), following the 2026-10-16 survey (DECISIONS D-173, D-174) | The 2026-10-16 verdict stands: no source "shows one single-antenna node measuring heart rate, counting people or recognising activity classes with a real-world accuracy" (REPORT.md §1). Acted on: (1) the 9-node study COMPARISON §7 cited as "near chance" (arXiv 2602.10823) is superseded by its authors' *More Links, Same Airtime* (data: Zenodo 10.5281/zenodo.23574119, CC BY 4.0), and ESPectre 3.0.0 is GPL-3.0 or commercial, with a holdout its report calls "a regression set, not a sealed test" (COMPARISON §7 corrected the same day); (2) AGC distorts CSI multiplicatively, so additive-noise synthetic rooms are a weakness (arXiv 2607.01882, 2605.26836) → `test_e2e` (f), row below; (3) heart-rate failure modes reported for other CSI code (a lock on the 3rd breathing harmonic from an assumed packet rate; "68 bpm from pure noise") → `test_vitals` (9)-(12b), row below; (4) the recordings above are replayable → §5.3. Not acted on: avoid C5 GPIO16-22 (flash/PSRAM bus) in any sensor wiring (REPORT §3 item 4); ask for the UW Tacoma ESP32-C5 dataset (IEEE DataPort, login; item 5); licensing (item 6). The replay of item 5 used its own scorer, not `session_score.py`. Limits: every figure is the authors' or vendors' own; most papers were read from the abstract and no PDF in full; videos were not watched; Reddit was blocked and many IEEE Xplore and patent pages failed; nothing was checked by a second reader (REPORT.md §4) | `~/ESP32C5_CSI_logs/2026-10-30_research/REPORT.md` and `papers/`, `opensource/`, `closed/`, `blogs/` (each with `final_report.md`), outside the repo |
| 2026-10-30 | `test_vitals.c` sections (9)-(12b), heart-rate failure modes (REPORT §3 item 2): noise with no chest, harmonic-rich breathing with no heart, breathing plus heart-band noise, irregular 40-100 Hz packet rates, a weak or absent heart with link silences. Ubuntu, `run_tests.sh` (gcc 15.2.0 `-Werror`) | `vitals` 2,386 → **2,472** checks (+86), 0 failures, **7 KNOWN LIMIT lines** (first form, superseded by the 2026-11-01 row): empty room with a shared 0.1-2 Hz fluctuation of 0.02 → heart rate 49.8-89.3 bpm in 16 of 100 analyses and breathing in 34 of 100 (at 0.005, breathing in 8 of 100); breathing plus shared heart-band noise 0.02 → heart rate 68.9-109.0 bpm in 44 of 100; link silences → 6 and 12 wrong heart rates (heart ×0.50, ×0.25) and 23 phantoms (49.2-56.2 bpm) in 3,997 analyses with no heart. Seven single-rule mutants of `vitals.c` each fail (4-57 failures). Scratch fix F2 (interpolate across empty bins in `vitals_push`): the then-suite 2,472 / 0 and the three silence lines gone (`F2_scratch_12b.txt`); **not applied** (D-174). Full suite 11,641 checks with the e2e row below | `~/ESP32C5_CSI_logs/2026-10-30_step1/hr/` (outside the repo): `run_only_vitals.log`, `run_full_suite.log`, `mutations.txt`, `mutations_detail.txt`, `F2_scratch.diff`, `F2_scratch_12b.txt`, `fix_candidates.txt`, `test_vitals.diff` |
| 2026-10-30 | `test_e2e.c` scenario (f) (REPORT §3 item 3): an **assumed** multiplicative AGC receiver in front of the int8 rounding (common gain steps, drift and offset; fixed and gain-state ripple; Rapp compression), an unperturbed twin of every packet, and a shadow of Step 08's static (D) branch that feeds nothing (§3.2). Ubuntu, gcc `-Werror` | `e2e` 242 → **282** checks, 0 failures, **5 KNOWN LIMIT lines** (f-int8, f-comp, f-state, f-cal, f-mirror; §3.3 item 11). The (a)-(e) output is unchanged, 126 identical lines (`regression_a-e_diff.txt`). Seeds 5000-5008: 282 / 0 in seven rooms; 5004 and 5007 `268 checks, 1 failures`, only e1. Every magnitude is an assumption. Superseded in detail by the 2026-11-01 row | `~/ESP32C5_CSI_logs/2026-10-30_step1/agc/` (outside the repo): `run_tests_full.log`, `regression_a-e_diff.txt`, `seeds_5000-5008_summary.txt`, `seeds_5000-5008_aggregate.txt`; its `test_e2e.c.patch` and `run_tests_full_final.log` are superseded by `after_review/` |
| 2026-11-01 | **Offline replay of real, labelled recordings: the project's first comparison against real data** (not the C5, not this firmware's image). A host port of Step 08's state machine with the D branch (`replay_main.c`, outside the repo, from step08 `main.c` SHA-256 `3601db0c…`, unchanged), linked with the repo's `csi_dsp.c`, `motion_gate.c` and `activity.c` (SHA-256 unchanged), on six scripted trials of *More Links, Same Airtime* (§5.3); re-run the same day after a review found the port normalised by the median of 40 tones instead of all 52 stored entries | Self-test 16 / 16. Headline and caveats in §5.3: on the median link PRESENT covers 1.00 of seated and walking time in every mode; 0.03-0.10 of the first empty phase and 0.08-0.16 of the second; 20-50 onsets per hour in the empty phases; 52 / 37 / 56 of 336 runs per mode stuck PRESENT through the second empty phase, all held by the static flag. **Not a C5 accuracy figure**; no §4 mark is met or missed by it | §5.3 |
| 2026-11-01 | Review of the 2026-10-30 test work (18 defects: 16 fixed, 1 fixed in part, 1 rejected with a reason; the reviewing session's own count, no review record was saved), then the final host-test run (Ubuntu, `run_tests.sh`, gcc 15.2.0 `-Werror`). Only `test_vitals.c` and `test_e2e.c` changed in the repo; nothing under `main/` (the three replayed modules have the same SHA-256 before and after) | **9 suites, 11,673 checks, 0 failures** (11,641 before the review): dsp 5,980; ncc 749; vitals **2,499**; gait 108; count 592; gate 97; pvitals 1,304; activity 57; e2e **287**. **15 KNOWN LIMIT lines**: vitals 10 (§3.3 item 10), e2e 5 (item 11). Main changes: rows that passed or failed by seed luck now run 50-100 seeds with run-count bounds; shape 3 runs 20 seeds per rate with a 5 % rate bound and also checks breathing; new shape 4 (harmonics 6-8 only); the wrong-value ranges print correctly (49.8-52.5 and 49.8-60.9 bpm); f-shadow (3 checks) and f-rng (2 checks); an f1 / f2 lookup miss is now a failure; the f-cal verdict is computed per model. Mutants: §3.3 item 8. Seed sweep 5000-5008: 287 / 0 in seven rooms, 5004 and 5007 `273 checks, 1 failures`, only e1 (§2.1). Re-run by the documentation session the same day: the same suite output (`~/ESP32C5_CSI_logs/2026-11-01_docs/host_tests_rerun.log`). MSVC `run_tests.ps1` **not run**; no firmware build (no `main/` change); board not connected | `~/ESP32C5_CSI_logs/2026-10-30_step1/after_review/` (outside the repo): `run_tests_full.log`, `run_tests_before.log`, `run_only_vitals.log`, `seeds/`, `seeds_summary.txt`, `*.patch`, `vitals_output_diff.txt`, `e2e_output_diff.txt`, `harness/`; the re-run in `~/ESP32C5_CSI_logs/2026-11-01_docs/host_tests_rerun.log` |
| 2026-11-03 | **F2 applied (DECISIONS D-175, superseding D-85), first pass.** `vitals_push`'s gap loop (step10 `main/vitals.c`) pushes the closed bin's mean and then, for each empty bin, a straight line toward the packet that ends the gap; the reset test is unchanged and runs first. `vitals.h` PIPELINE comment updated. `test_vitals.c`: new (6b) `test_gap_fill` (exact ring values over a 0.65 s gap, and an adjacent bin); the three (12b) silence rows changed from KNOWN LIMIT lines to `CHECK(bad == 0)`. Ubuntu, `run_tests.sh` (gcc `-Werror`). Board not used | `vitals` 2,499 → 2,508 checks, 0 failures; KNOWN LIMIT lines 10 → 7 (the three silence lines gone). Before F2 those rows printed 6 / 12 / 23 wrong heart values (×0.50 / ×0.25 / no heart; 49.8-52.5, 49.8-60.9, 49.2-56.2 bpm); with F2, 0 at 8 seed bases (1700, 2700 … 8700). The repeat loop restored as a mutant: 4 FAILs ((6b) and the three silence rows). `test_e2e` output byte-identical to the 2026-11-01 run (`~/ESP32C5_CSI_logs/2026-11-01_docs/host_tests_rerun.log`); e2e seeds 5000-5008 with F2: 5004 and 5007 fail only e1 (273 checks), the others 287 / 0. The review row below replaced the (12b) `CHECK` by a rate bound and strengthened (6b) | `~/ESP32C5_CSI_logs/2026-11-03_impl/` (outside the repo): `host/run_tests_full.log`, `sweep12b/`, `mutation/f2_revert.diff`, `mutation/f2_reverted_test_vitals.log`, `seeds/` |
| 2026-11-03 | **Receiver gain logging (DECISIONS D-176), first pass, with the Step 10 build and the Kconfig matrix.** New step10 `main/csi_gain.c` / `csi_gain.h` (no ESP-IDF headers): `csi_gain_read` takes byte 23 (uint8 agc) and byte 22 (int8 fft) of `rx_ctrl` and refuses a header of 23 bytes or fewer; `csi_gain_track_t` summarises the packets per JSON line. `test/test_gain.c`, a 10th suite `gain` in both runners; `csi_gain.c` in `main/CMakeLists.txt`. Kconfig `CSI_GAIN_LOG` (menu "CSI capture", default y); JSON `agc`, `fft`, `gain_chg` after `episode_s`; line bound 862 → 900 + NUL, header example 426 → 457 characters (the printed line, `JSON ` prefix included). Ubuntu, ESP-IDF v6.1, `tools/linux_idf.sh build`; host tests gcc `-Werror` | Host tests: 10 suites, **11,704 checks**, 0 failures (dsp 5,980; ncc 749; vitals 2,508; gait 108; count 592; gate 97; pvitals 1,304; activity 57; gain 22; e2e 287), 12 KNOWN LIMIT lines (vitals 7, e2e 5). Step 10: **0xf59b0**, 0 `warning:` lines, 52 % free (0xf56d0 on 2026-10-16). Matrix: `combos: 552, with warnings/errors: 0`; `negative control (CONFIG_CSI_PING_INTERVAL_MS removed): rc=1 errors=3 -> fails as expected`. Byte evidence: ESP-IDF v6.1 and v6.0.2 `esp_wifi_he_types.h`, the `SOC_WIFI_MAC_VERSION_NUM == 3` layout (the C5, esp32c5 `soc_caps.h:630`): `noise_floor` byte 20, bytes 21-25 reserved, `channel` byte 28, `second` byte 29, size 64; a compiled check stores `channel` at offset 28, size 64; `esp_csi_gain_ctrl` v0.1.5's 6.0 libraries for esp32c5, esp32c6 and esp32c61 disassemble `esp_csi_gain_ctrl_get_rx_gain` as `lbu a5,23(a0)` → agc, `lb a5,22(a0)` → fft. **Not flashed; never seen on silicon.** Step 08 and the root not rebuilt (sources unchanged). MSVC `run_tests.ps1` not run. A board was present on `/dev/ttyACM0` (USB 303a:1001, serial `38:44:BE:BF:9D:9C`, from pyserial `list_ports`, port not opened; the session's own report, no listing saved): neither recorded board (COM13 `30:ed:a0:e4:50:b8`; the first board `d0:cf:13:e0:0b:f8`, DECISIONS D-152); not touched, the user asked which board it is | `~/ESP32C5_CSI_logs/2026-11-03_impl/`: `host/run_tests_full.log`, `build/build10.log`, `matrix/kmatrix.log`, `gain_bytes/`, `sha256_main.txt` |
| 2026-11-03 | Python tools for the gain keys (D-176), first pass: `presence_monitor.py` (`CSV_FIELDS` + `agc`, `fft`, `gain_chg`; docstring), `dashboard.html` (`CSV_FIELDS`), `session_score.py` version 4 (a GAIN block and JSON `gain`; logging only, no mark), `tools/kconfig_matrix.py` (the `CSI_GAIN_LOG` dimension, only where JSON is on) | `session_score.py` 4 on a synthetic labelled log with gain keys: walk 100 rows, gain changed in 66 (66 packets), agc 44-48; empty 182 rows, changed in 19 (38 packets), agc 40-46. On `docs/hw_logs/*.log` and `tools/sample_session.log`: no GAIN block, rc 0. The review row below made it version 5 | `~/ESP32C5_CSI_logs/2026-11-03_impl/score/` |
| 2026-11-03 | **Review of D-175 / D-176 and the final run** (`REVIEW_RECORD.md`): 12 defects (2 medium, 10 low), each re-checked against the source first; **all 12 fixed, none rejected**. (1, medium) the (12b) silence rows' `CHECK(bad == 0)` depended on the seed base (base 1770: 1 wrong value, 52.4 bpm, no heart): now a rate bound of 2 per 1,000 analyses, plus a KNOWN LIMIT line for any wrong value under it; the comments call D-175 a reduction. (2) (6b) used one value for every subcarrier: now one per subcarrier, each channel checked against its own subcarrier's line. (3) the first detect line's `gain_chg` covered the whole calibration: new `gain_detect_start()` at both `PH_DETECT` sites and a `gain before detection` log line. (4) Kconfig help and prompt ("on each detect JSON line"). (5) `vitals.h` gap wording. (6) `json_gain` placement. (7, medium) `session_score.py` skipped the gain pool for unlabelled logs; (8) `rows_with_key` counted only numeric rows; (9) Infinity crashed it, NaN / negative values broke the sums; (10) its docstring → version 5. (11) `presence_monitor.py` docstring. (12) `run_tests.sh` header. Ubuntu, gcc `-Werror`, ESP-IDF v6.1. Board not touched | **10 suites, 11,705 checks, 0 failures**: dsp 5,980; ncc 749; vitals **2,509** (+1, the (6b) `chan[c] != c` precondition); gait 108; count 592; gate 97; pvitals 1,304; activity 57; gain 22; e2e 287. **12 KNOWN LIMIT lines** (vitals 7, e2e 5), none from (12b) at the shipped base; the `test_e2e` section byte-identical to the first pass and to 2026-11-01. (12b) sweep over 52 seed bases (shipped seed counts): D-175 1 wrong in 332,374 silence analyses, the repeat loop 889; 0 for both in the no-silence rows. Mutation: 9 fill mutants, all FAIL at `test_vitals.c:702` ((6b)). Hand mirror of the new gain order (`main.c` is not host-compiled): the first detect line reports `gain_chg` 0 over 25 packets (3 over 3,225 before). `session_score.py` 5: 11 test logs rc 0 (the Infinity log was rc 1); the seven `docs/hw_logs` logs and `sample_session.log` give version 4's output except the version line. Step 10 **0xf5a90**, 0 `warning:` lines, 52 % free (+0xe0); **not flashed**. Matrix 552, 0 with warnings or errors, negative control fails as expected (the 184 `gain0` and 184 `json0` combinations compile the gain-off stub). MSVC not run; no Windows build; Step 08 / root not rebuilt; no git operation | `~/ESP32C5_CSI_logs/2026-11-03_impl/after_review/`: `REVIEW_RECORD.md`, `run_tests_full.log`, `run_tests_vs_before.diff`, `sweep12b/`, `mutation/`, `calgain/`, `score/`, `build/build_step10.log`, `build/sha256_build.txt`, `matrix/kmatrix.log`, `sha256_after.txt`, `patches/` |

### 5.3 Offline replays of recorded CSI (not hardware, not the C5)

A replay runs a host port of the detector on someone else's recordings. It
shows how the arithmetic behaves on real indoor channels with a known
occupancy. It does **not** measure this firmware on the C5: the chip,
sounding, tone set and packet timing all differ (caveats below), so it
meets or misses no §4 mark and is not an accuracy figure. Never quote it as
"Step 08 detects X %".

**2026-11-01: Step 08 (and Step 10's gate and activity state) on *More
Links, Same Airtime*.**

- **Data:** Khamaisi, Azizi, Bastos Rodrigues (2026), *More Links, Same
  Airtime: WiFi CSI Recordings from an Office, a Residence and a
  Laboratory*, v1.0, Zenodo, <https://doi.org/10.5281/zenodo.23574119>,
  CC BY 4.0 (the record's LICENSE: "Copyright (c) 2026 Karim Khamaisi and
  Bruno Rodrigues, Sensing Group, University of St. Gallen."). The paper,
  by K. Khamaisi and B. Rodrigues, is a MobiSys 2027 submission, not a
  published paper (its DATASHEET); companion code
  github.com/sensing-group/more-links-same-airtime (MIT, Zenodo
  10.5281/zenodo.23574129), read, not run. Only `trials.tar` was used,
  changed by `convert.py` (re-timed within each datagram, exact duplicates
  dropped, phase-labelled, written as CSV).
- **What ran:** six scripted trials (T0003, T0004 residence; T0008-T0011
  laboratory), each empty 300 s, seated 180 s, walking 120 s, empty 120 s,
  one adult; 56 directed links each; three packet modes per link (`raw`
  every frame, ~20.7/s; `first` frame and `mean` amplitude per trigger
  burst, ~3.6/s) = **1,008 link runs**. Each run is an independent Step 08
  detector with the default Kconfig thresholds (motion 5.0 / release 2.5 sd,
  static 5.0 sd for 4 s, hold 5 s, freeze cap 600 s, no-CSI 5 s, select
  0.20), calibrated on the first 35 s with no leave-room countdown and no
  PH_LOCK; plus an 8 sd run (motion and static). Self-test 16 / 16.
- **Review re-run (same day):** the first run gave the port only the 40
  data tones; the firmware takes all 52 stored entries, and the 12 zero
  entries take part in the per-packet median (step08 `main.c:495-510`). Fixed
  and re-run; the first run's outputs are in `after_review/replay_before/`
  and are superseded. Effect: raw onsets/h 59 → 50, `mean` 15 → 20, stuck
  runs 48 / 37 / 64 → 52 / 37 / 56; seated, walking and AUC d unchanged.

Fraction of time PRESENT, median [IQR] over 336 link runs per mode
(`out/replay_results.md`):

| Thresholds | Mode | empty #1 | seated | walking | empty #2 | onsets/h in empty | AUC `m_sigma` seated / walking | AUC `d_sigma` seated / walking |
|---|---|---|---|---|---|---|---|---|
| 5 sd (default) | raw | 0.10 [0.03-0.24] | 1.00 [0.91-1.00] | 1.00 [1.00-1.00] | 0.16 [0.05-0.43] | 50 [20-99] | 0.60 / 0.96 | 1.00 / 1.00 |
| 5 sd (default) | first | 0.03 [0.00-0.11] | 1.00 [0.79-1.00] | 1.00 [1.00-1.00] | 0.08 [0.01-0.26] | 20 [0-40] | 0.64 / 1.00 | 1.00 / 1.00 |
| 5 sd (default) | mean | 0.03 [0.00-0.13] | 1.00 [1.00-1.00] | 1.00 [1.00-1.00] | 0.09 [0.03-0.51] | 20 [0-30] | 0.67 / 1.00 | 1.00 / 1.00 |
| 8 sd | raw | 0.02 [0.00-0.10] | 1.00 [0.55-1.00] | 1.00 [0.95-1.00] | 0.05 [0.00-0.16] | 20 [7-49] | same | same |
| 8 sd | first | 0.00 [0.00-0.05] | 1.00 [0.42-1.00] | 1.00 [1.00-1.00] | 0.05 [0.00-0.15] | 10 [0-20] | same | same |
| 8 sd | mean | 0.00 [0.00-0.03] | 1.00 [0.93-1.00] | 1.00 [1.00-1.00] | 0.05 [0.01-0.20] | 0 [0-10] | same | same |

Scoring windows have ±5 s guards; detection starts a median 36.1 s into a
trial. What it says about Step 08's logic is §3.3 item 12: the thresholds
sit inside the real empty-room tail, the seated person is carried by the D
branch (the motion branch is a walking detector here), and 11-17 % of runs
stay PRESENT after the person leaves until the 600 s freeze cap, which no
trial reaches. The motion flag was already on at walking start in 132 /
257 / 297 runs; otherwise it fired a median 2.0 / 4.2 / 3.8 s after walking
began.

**Caveats** (`out/replay_results.md`, "Caveats"):

- ESP32-S3 nodes sending to each other (dedicated in-room senders,
  2.4 GHz channel 11), not a C5 hearing a hotspot's ping replies.
- int8 L-LTF captured with `channel_filter_en = true`, amplitudes about
  13 LSB; the C5 path is 12-bit L-LTF and its CSI config has no
  channel-filter field.
- 52 stored entries, 40 data tones, against the C5's 26 L-LTF tones; which
  26 tones the C5 delivers is not documented in the repo, so there is no
  tone-level mapping.
- Trigger bursts at 1.8-3.8 per second per link, not an even 50/s, so every
  packet-count constant changes meaning; residence links have many > 1 s
  gaps.
- One adult, one phase order, six trials, two rooms; the second empty phase
  (120 s) is the only drift control; no two people, pets, fans, long
  sessions or this project's room.
- Calibration without LOCK or the leave-room countdown; no stored baseline;
  Step 10's breathing hold, vitals, recognition and counting not modelled;
  no cross-check against `test_e2e.c`.
- Each link scored alone; links of one trial are not independent.

**Raw logs and tools** (outside the repo, Ubuntu home):
`~/ESP32C5_CSI_logs/2026-10-30_step1/replay_tools/` (`README.md` with the
reproduce recipe and the attribution, `convert.py`, `replay_main.c`,
`score.py`, `extra_stats.py`), `out/replay_results.md`, `out/replay/` and
`out/replay_thr800/` (traces, `scores.json`, `tables.md`,
`build_and_selftest.txt`), `after_review/extra_stats_new.txt`,
`after_review/profile_290_305.py` and `adhoc_new.txt`, the first run in
`after_review/replay_before/`; the dataset in `dataset/` (`trials.tar`,
LICENSE, DATASHEET.md, SHA256SUMS).

---

## 6. Windows and hardware gotchas

These are the ones found so far. Add to the list as more turn up.

| # | Symptom | Cause | What to do | Source |
|---|---|---|---|---|
| **G1** | After a script toggled RTS/DTR, the C5 went silent until the **RST button** was pressed; esptool then reported "Write timeout" (DECISIONS D-153) | Pulsing RTS/DTR from pyserial on Windows left the C5's USB-Serial-JTAG latched in reset | Reset only with **esptool or idf.py** (or the RST button): flashing ends in `--after hard-reset`, or run an esptool command such as `esptool --port COM13 chip_id`. `tools/serial_capture.py` keeps `--reset` disabled and sets DTR and RTS False before opening the port. `presence_monitor.py` and `csi_monitor.py` do the same since 2026-09-25 (§2.5), though they are untested on the C5: if the board goes silent after starting one, press RST | found 2026-09-25; tools/serial_capture.py docstring |
| **G2** | `build.ps1` reports `BUILD FAILED` and exit code 1, although the build succeeded | Under Windows PowerShell 5.1, redirecting with `*> file` or `2>&1` turns ESP-IDF's Kconfig `NOTE:` lines on stderr into error records. `$?` goes false, and `build.ps1` checks `if (-not $?)` after `idf.py build` (build.ps1:246-247) | Run it without redirection, or judge the log: look for `Project build complete` and a `binary size` line, and count `warning:` / `error:` lines | found 2026-09-25 |
| **G3** | A loop calling `.\build.ps1` twice fails on the second call | `build.ps1` does `Set-Location` into the step folder (build.ps1:228) and does not change back | Call it by full path (`& D:\ESP32C5_CSI\build.ps1 …`), or `Set-Location D:\ESP32C5_CSI` before each call | found 2026-09-25 |
| G4 | `export.sh` fails with "MSys/Mingw is not supported" | ESP-IDF refuses Git Bash / MSYS on Windows | PowerShell with `export.ps1` | README *Setting up your shell* |
| G5 | `set-target esp32c5` fails, and the build silently becomes classic-ESP32 | ESP-IDF v6.0.1 lacks the RISC-V toolchain on this machine | Stay on v6.0.2 at `C:\esp\v6.0.2\esp-idf` | README *Everything is on ESP-IDF v6.0.2* |
| G6 | menuconfig shows classic-ESP32 options (Steps 01–09) | menuconfig ran before any `set-target` | Run `.\build.ps1 <step>` once first | README *Step 02*; README *Step 10 — recognise, count, vitals (experimental)* |
| G7 | "port is busy" | Only one program can own a COM port | Close `idf.py monitor`, the Python tools and any serial terminal | README *If flashing fails* |
| G8 | "The semaphore timeout period has expired" when flashing | The chosen port is a Bluetooth port (COM3/COM4 here) | `esptool --port COMx chip_id` first | README *If flashing fails* |
| G9 | Board lights up, but no COM port appears | Charge-only USB cable | Use a data cable, or try the board's other USB socket | README *If flashing fails* |
| G10 | `ninja: error: remove(...): The process cannot access the file…` | A file lock from antivirus or two builds at once | Build again | README *What I verified, and what I could not* |
| G11 | Typing in `idf.py monitor` seems to do nothing (Step 10 console) | Typed characters are not echoed | Type blind and press Enter, or use `presence_monitor.py --send` | README *Console commands* |
| G12 | Build dies with `-lkernel32`-style errors | `IDF_PATH` unset or wrong, so CMake picks the host compiler | Fix the environment (`export.ps1`), not the toolchain | each project's `CMakeLists.txt` check |
| G13 | Step 10 warns `live CSI rate … differs from the calibrated …` | The ping interval changed since calibration, or the AP slowed down | `recal`. Re-enrol and retrain if `CSI_PING_INTERVAL_MS` changed | README *Before recognising anyone* |
| G14 | `build.ps1 <step> -Flash` never returns in a script or AI tool call | `-Flash` runs `idf.py flash monitor` (build.ps1:251), and monitor is interactive | Flash with `idf.py -p COMn flash` from the step folder, then capture with `serial_capture.py` (§2.4) | DECISIONS D-156 (1) |
| G15 | A PowerShell script variable behaves oddly | The variable was named `$args`, which PowerShell reserves | Use another name | DECISIONS D-156 (2) |
| G16 | `idf.py` or `esptool` not found in an automated call | Shell state does not persist between calls | `& C:\esp\v6.0.2\esp-idf\export.ps1` in every call that needs them | DECISIONS D-156 (4) |
| G17 | The COM port enumerates but will not open ("device attached … not functioning", error 31) | Windows-side USB state | Unplug and replug, or press BOOT+RESET, before blaming the toolchain | DECISIONS D-157 (seen 2026-09-22) |
| G18 | After COM13 disappeared and came back, Step 10 accepted input but printed nothing and did not answer `status` | **Not established.** As read from the ESP-IDF v6.0.2 source, a console write waits at most about 50 ms and then drops output while nobody reads, so a write that blocks for ever is not what the source shows. A reset, or a host program holding the stale handle of the old port, is not ruled out | Close the program and reopen the port (`dashboard.py` reopens it by itself). If the board stays silent, press RST and look for the reset-reason line at boot (D-165). Log the run in §5.1 | seen 2026-09-25; DECISIONS D-164. 2026-09-28: the D-164 image kept detecting while no host read the console and said so (`output resumed ...`, §5.1). The 2026-09-25 silence itself is still unexplained |
| G19 | `serial_capture.py` could not record across a port loss: if COM13 disappeared, the capture ended with a pyserial exception and missed the boot that followed | Until 2026-09-28 it opened the port once and had no handler around `s.read()` (`tools/serial_capture.py`, the read loop). Found by reading the code on 2026-09-28; not yet observed on the C5. On 2026-09-25 COM13 disappeared before each unexplained boot (§5.1), but not every kind of restart is shown to re-enumerate USB: in ESP-IDF v6.0.2 on the C5, the software restart and the panic restart reset the CPU and a fixed list of peripherals that does not include USB-Serial-JTAG (`components/esp_system/port/soc/esp32c5/system_internal.c`: `esp_system_reset_modules_on_exit`, `esp_restart_noos_inner`; `port/panic_handler.c`: `panic_restart`). On the C5 on 2026-09-28, in the 19:00:33–19:01:33 capture (at about 19:01:06), one `esp_restart()` (`rst:0xc (SW_CPU)`, `last reset: SW (3)`) left COM13 open, and `serial_capture.py` recorded the whole reboot with no port loss (§5.1; n = 1; a panic restart has not been observed) | **Fixed 2026-09-28 in `serial_capture.py`**: it now waits for the port, writes `# <s> port lost: ...` and `# <s> port open: COMn`, and keeps recording (its docstring, PORT LOSS; tested with a fake port only, §5.2). `dashboard.py --log FILE` also reopens (D-163), and `session_runner.py` has the same reopen loop (D-169; §2.7). The 2026-09-28 soak used `soak.py`, kept with its raw logs in `D:\ESP32C5_CSI_logs\2026-09-28_step10_soak\` and not in the repo | found 2026-09-28 by reading `tools/serial_capture.py`; a software restart with the port held open seen 2026-09-28 (§5.1 row 19:00:33–19:01:33) |
| G20 | `Write timeout` at `Connecting...` when flashing Step 10 about a minute after an `esptool chip_id` hard reset; afterwards the board printed nothing in a 10 s listen, and one further `chip_id` also got `Write timeout` | Not established. esptool drove DTR/RTS both times (`chip_id` ended with `Hard resetting via RTS pin`; the flash began with `--before default-reset`); no other program did (`serial_capture.py` holds both low). Whether esptool's reset latched the chip (D-153) is not known. n = 1 (§5.1). On 2026-09-28 at 19:00, `idf.py -p COM13 flash` with no `esptool chip_id` before it (the port identified with pyserial `list_ports`, §2.4) connected, wrote and verified with no `Write timeout`, and after its `--after=hard-reset` the app ran without RST (inferred from its uptime; the 13:19 flash, entered with BOOT+RST, needed RST; §5.1). n = 1: whether skipping `chip_id` is why is not established, and the cause of the earlier timeout is still not established | DECISIONS D-158: hold BOOT, tap RST, release BOOT, flash with `--before no-reset`, then **press RST**. After `--after hard-reset` the board printed nothing until RST was pressed on 2026-09-28 (probably the app never started; not observed directly), and `--after watchdog-reset` did not start it on 2026-09-25. Since 2026-09-28 `serial_capture.py` and `session_runner.py` open the port with a 2 s write timeout, so a stuck write raises and is handled as a port loss instead of hanging (tested with a fake pyserial only, §5.2) | seen 2026-09-28; not seen at the 19:00 flash that day (§5.1) |
| G21 | On Ubuntu (dual boot, same NTFS checkout) the ESP-IDF extension finds no ESP-IDF, or a Linux build breaks the Windows one | Folder `.vscode/settings.json` overrides user settings for Resource-scoped keys (`idf.currentSetup`, `idf.customExtraVars`, `idf.port`, `idf.buildPath`), and until 2026-10-12 those files held `C:\` paths; a shared `build/` holds a Windows `CMakeCache.txt`; a v6.1 build rewrites `sdkconfig` and `dependencies.lock` in the shared tree | Machine paths live only in each OS's user settings (docs/ide/). On Linux use `tools/linux_idf.sh` (build dir and `sdkconfig` under `~/esp-build`); do not build Step 09 on v6.1 | seen 2026-10-12 (§5.2); DECISIONS D-170 |
| G22 | On Ubuntu (ESP-IDF v6.1), a flash after `erase-flash` or onto a new board writes less than expected | **Not seen; read in the v6.1 docs.** Since v6.1, `idf.py flash` with `*_flashed.bin` files from an earlier flash in the build directory "only writes the flash regions that changed", then verifies and does a full flash if verification fails; Espressif recommends `idf.py flash -a` for "brand-new, empty, or erased chips" (v6.1 `docs/en/migration-guides/release-6.x/6.1/tools.rst`, read 2026-10-16). That v6.0.2 on Windows always writes every region is an inference from this being listed as a v6.1 change, not checked in the v6.0.2 source | After an erase, on a new board, or whenever in doubt: `tools/linux_idf.sh idf <step> -p /dev/ttyACM<n> flash -a`. `tools/linux_idf.sh flash` runs plain `idf.py flash` | read 2026-10-16 (§5.2); DECISIONS D-170, D-173. Nothing has been flashed from Linux yet |
