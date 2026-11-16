# Architecture

How this repository is laid out, how the finished detector (Step 08) and the
experimental layer (Step 10) work at run time, where state lives, and how to
change things without breaking them. Written for an engineer or an AI opening
the repo cold.

Written 2026-09-25. Step 08 line references, and Step 10 references marked
"on 2026-09-28", are to the 2026-09-28 working tree (references marked "on
2026-11-03" are to that day's tree) after the D-168
refinements (DECISIONS D-168); other Step 10 references (`main.c:2715`)
are to the 2026-09-25 tree and many are stale
(DECISIONS A-30); `test_e2e.c` references marked "on 2026-11-01" are to
that day's test tree; function names are the stable anchor, so search
for the name if a line number has drifted. README is cited by its section
heading, not by line number, because README lines move often (the same rule
as DECISIONS.md). Where this document and the code disagree, the code wins.
Please fix this file when you find a difference.

Related documents:

| Document | What it is for |
|---|---|
| [`../README.md`](../README.md) | The step-by-step tutorial: what each step proves, how to build, flash, tune and read the output. |
| [`DECISIONS.md`](DECISIONS.md) | Why each design choice was made, what was rejected, and its evidence and status. |
| [`TESTING.md`](TESTING.md) | How the code is verified (builds, switch matrix, host tests), what that proves and what it does not. |
| [`REFERENCES.md`](REFERENCES.md) | Annotated literature and prior art, including the published accuracy ceilings. |
| [`COMPARISON.md`](COMPARISON.md) | How this project compares with existing work. |

---

## 1. What this is, why, and where it stands

A human presence and motion detector that uses Wi-Fi Channel State
Information (CSI) from one ESP32-C5 and nothing else. The board joins a
2.4 GHz router, pings the gateway continuously so it keeps receiving frames,
and watches how the per-subcarrier amplitude of those frames changes as
people move through the radio path. It is taught as ten standalone ESP-IDF
projects, each adding one thing (README § "How the code is organised"), so
that when something misbehaves you can tell which stage broke (README § "The
30-second version"). Step 08 is the finished detector: it calibrates itself
against an empty room, stores the calibration in NVS and reports
PRESENT/EMPTY over serial as JSON. Step 10 is Step 08's pipeline plus three
**experimental** analyses: gait-based recognition of up to 4 enrolled people,
one-versus-two-or-more counting of moving people, and breathing rate with an
experimental heart rate for a person sitting still. Since 2026-10-16 it also
derives an experimental coarse activity state (empty / still / restless /
moving) and presence-episode counters from decisions it already makes (§6,
DECISIONS D-172). Its only change to Step
08's presence answer is the breathing hold (`CSI_VITALS_HOLDS_PRESENCE`,
default on), which can extend or restore PRESENT but never start it (§3.5
step 9, DECISIONS D-90). Step 10 also differs in plumbing: it refuses a
recalibration during a CSI outage, and uses a 16-deep CSI queue and ping
priority 5 (§3.1). After lock it can also recalibrate by itself when
`channel_follow` adopts a new Wi-Fi channel (§3.3, DECISIONS D-168). Step
08 has no `channel_follow`: it checks the channel only at shape lock, so it
acts on a change after lock only at the next boot.

**Status on 2026-11-01** (the record is [TESTING.md §5](TESTING.md#5-results-log); this is a summary)

* **Builds with a dated record:** Steps 01, 03 and 04 (2026-09-25); Step 08
  (0xdb340, with `CSI_LLTF_ONLY` = y), the root project (0xdb350) and Step
  10 (0xef360), all 2026-09-28 with 0 warnings, after D-168 (automatic
  recalibration on a channel change) and its four refinements of the same
  day (§3.3; TESTING §5.2). **Step 08 and the root project are not
  flashed.** The board holds Step 10's 0xef360, flashed on 2026-09-28 at
  19:00 (TESTING §5.1). **Steps 02, 05, 06, 07 and 09
  were last built on 2026-09-07**, and their `main.c` changed on 2026-09-22,
  so their current sources have no recorded build. Rebuild each
  (`& D:\ESP32C5_CSI\build.ps1 <step>`) and add a TESTING §5.2 row before
  calling them built.
* Step 10: `main.c` compiled warning-free in 192 Kconfig switch combinations;
  the 8 host-test suites pass (11,453 checks, synthetic signals only), both
  on 2026-09-28 on Windows (TESTING §5.2).
* **Ubuntu, ESP-IDF v6.1** (D-170, D-171): 10 projects build with 0 warnings
  (2026-10-12; sizes unchanged in the 2026-10-16 rebuild before the
  activity state, after which Step 10 is 0xf56d0, next bullet); host tests
  under gcc `-Werror` give the same 11,453 checks, and the 192-combination
  matrix is clean (2026-10-16, before the activity state; after it 11,515
  checks and 368 combinations, next bullet; TESTING §5.2). No v6.1 image has been flashed.
* **Activity state and presence episodes** (D-172, 2026-10-16, Ubuntu
  only): Step 10 builds as 0xf56d0 with 0 warnings (Step 08 and root
  unchanged at 0xe0600); 9 host suites, 11,515 checks, pass under gcc (11,673
  since 2026-11-01 and 11,705 in 10 suites since 2026-11-03, next bullets); the
  matrix was then 368 combinations, 0 warnings (552 since 2026-11-03). **Never flashed**, no Windows
  v6.0.2 build, and `run_tests.ps1` (MSVC) not re-run (TESTING §5.2).
  v6.1 brings no CSI change (D-173).
* **Heart-rate and AGC failure modes in the host tests, and a first offline
  replay of real recordings** (D-174, 2026-10-30/11-01, Ubuntu, test code
  and offline tools only; nothing under `main/` changed). `test_vitals`
  gained heart-rate failure-mode cases and `test_e2e` an assumed AGC receiver
  model with a shadow of Step 08's static branch; 9 suites, **11,673
  checks**, 0 failures under gcc, plus 15 printed KNOWN LIMIT lines
  (shortcomings measured, not fixed; TESTING §3.3 items 10-11). A port of
  Step 08's state machine, outside the repo, was replayed on another
  group's ESP32-S3 recordings with a logged occupancy: the seated person was
  carried by the static (D) branch, the 5 sd thresholds sit inside the real
  empty-room tail, and 11-17 % of runs stayed PRESENT after the person left
  (TESTING §5.3, §3.3 item 12). **That is not a C5 accuracy figure.** The
  fixes it suggests (F2 gap interpolation, per-packet gain logging, AGC
  handling) were proposals then; the first two are implemented since
  2026-11-03 (next bullet), AGC handling is not. MSVC not run; board
  not connected (TESTING §5.2).
* **F2 and receiver gain logging** (D-175, D-176, 2026-11-03, Ubuntu, host
  and build only). Vitals now fill the empty 100 ms bins of a gap shorter
  than about 1 s by a straight line toward the packet that ends it,
  instead of repeating the last value (§6 `vitals`). In the synthetic
  tests that makes the ~50 bpm phantom heart rate after link silences much
  rarer (1 in 332,374 silence analyses over 52 seed bases, against 889
  with the old rule); it does not remove it. Step 10 also reads the
  receiver's AGC and FFT gain from two undocumented `rx_ctrl` bytes and
  logs them on the detect JSON line (`agc`, `fft`, `gain_chg`; §3.5, §6
  `csi_gain`). Nothing the detector decides reads them, and the bytes
  have never been seen on silicon. After that day's review: 10 suites,
  **11,705 checks**, 0 failures, 12 KNOWN LIMIT lines (gcc); 552 Kconfig
  combinations clean; Step 10 0xf5a90, 0 warnings, **not flashed**; Step
  08 and the root unchanged and not rebuilt; MSVC not run (TESTING §5.2).
* `tools/session_runner.py` and `tools/session_score.py` (2026-09-28,
  DECISIONS D-169) play and score the TESTING §4 Phase 1 (1a-1c) and
  Phase 2 step 2b sessions. They were tested offline only (dry runs,
  recorded and replayed logs, a fake pyserial module; TESTING §5.2) and
  have **never run on the C5**.
* **Hardware** (TESTING §5.1), on one ESP32-C5 at COM13 with a Windows
  laptop hotspot: Step 01 boots (2026-09-25). Step 04 captured 106-byte
  L-LTF CSI at about 50/s and confirmed the **12-bit packing** (26
  subcarriers; DECISIONS D-160). The hotspot sends only legacy frames, so
  Steps 03 and 05–07 get no CSI from it (D-159). Step 10 booted several times
  on 2026-09-25 and restarted three times in about 15 s of one capture
  (after 33 s with no output, during the Wi-Fi join, and in the leave-room
  countdown); a fourth port loss, also in the countdown, ends the capture.
  Cause not recorded. On 2026-09-28 the image with D-164 and D-165 (built with
  `CSI_LLTF_ONLY` = y, not the default) ran **15 minutes at 50 Hz with no
  restart**: it calibrated, dropped 0 packets
  and had 94,132 bytes of heap free. Its slowest packet since boot, 52.2 ms,
  was set before the capture while no host read the console (at most
  20.8 ms in the capture's detect rows; TESTING §5.1). At 19:00 the same
  day 0xef360 was flashed, and it booted with the hotspot off. In the 60 s
  capture it did not join Wi-Fi, so its channel check (D-168) has not run
  on the board and no CSI arrived. Its `esp_restart()` after the failed
  join (§3.2 step 7), one software restart, kept COM13 open: the capture
  recorded the whole reboot with no port loss (TESTING §5.1, G19).
  **Step 08 has never run on the board.**
* **No accuracy of any kind has been measured.** No run so far recorded who
  was in the room. Presence, recognition, counting, breathing, heart rate
  and the activity state are all unverified on hardware; the 2026-11-01
  replay ran a port of Step 08 on other hardware's recordings (above). Recognition, counting, heart rate and the activity state (D-172) are
  experimental and must not be described as validated. Published figures in
  REFERENCES.md §3.4 are ceilings, not predictions.
* The record of what has been verified is [TESTING.md §5](TESTING.md#5-results-log).
  This section, TESTING §0 and README *Verification status* summarise it;
  when a new result lands, update all three and point them at the §5 row
  (TESTING §5 says how).

---

## 2. Repository map

### 2.1 The ten steps, as a progression

Every step folder is a complete ESP-IDF project with its own `CMakeLists.txt`,
`sdkconfig.defaults`, `main/Kconfig.projbuild` and `build/`. The Wi-Fi, ping
and CSI boilerplate is copied into each one on purpose rather than shared
(README § "How the code is organised"). A fix to that boilerplate therefore
has to be applied to every copy (see §7.6).

| Step | Folder | What it adds |
|---|---|---|
| 01 | `step01_hello` | Chip info and a 1 s heartbeat with free heap. No Wi-Fi. Proves toolchain, cable and board together. |
| 02 | `step02_wifi_sta` | Joins 2.4 GHz Wi-Fi as a station and locks the radio (2.4 GHz only, HT20, power save off) right after `esp_wifi_start()`. Reconnect policy. |
| 03 | `step03_csi_hello` | Enables CSI (`CONFIG_ESP_WIFI_CSI_ENABLED`, acquire config), pings the gateway, and prints CSI packets/s next to free heap. Nothing is interpreted yet. |
| 04 | `step04_csi_geometry` | Diagnostic only: captures legacy (11g L-LTF), HT20, HE SU/MU and ACK CSI (HT40, VHT, DCM and beamformed stay off, step04 `main.c:211-226`) and prints a per-shape histogram plus a byte-packing check, so you measure your own CSI shape. Its capture config must not be copied into a detector. |
| 05 | `step05_amplitude` | Shape auto-lock, amplitude, per-packet median (AGC) normalisation, the `CSI_LLTF_ONLY` 12-bit decode, and a decimated CSV stream for `tools/csi_monitor.py`. |
| 06 | `step06_baseline` | Empty-room calibration (warm-up, phase A per-subcarrier sigma and selection, phase B metric statistics). Writes the NVS `baseline`. |
| 07 | `step07_presence` | Loads the baseline (refuses to run without it) and makes the PRESENT decision: motion hysteresis, sustained static branch, hold time, slow-reference freeze, no-CSI watchdog. |
| 08 | `step08_final_human_detector` | Steps 02-07 in one firmware: self-calibrates when no valid baseline exists, JSON and bar-line output, optional LED and recalibrate button, reconnects forever. **The finished detector.** |
| 09 | `step09_espressif_sensing` | A benchmark, not part of the pipeline: Espressif's `esp_wifi_sensing` running on the same board and router. Separate project because `esp_wifi_set_csi_rx_cb()` has one callback slot. The repo's only external dependency (pinned by `dependencies.lock`). |
| 10 | `step10_recognise_count_vitals` | Step 08 plus: motion gate, vitals, gait recognition, 1-vs-2+ counting, whose-vitals latch with a per-person log, an activity state with presence episodes (2026-10-16), and a serial console. **Experimental.** Has its own `test/` directory with host unit tests. |

Steps 06, 07, 08 and 10 each carry an identical copy of `main/csi_baseline.h`
(checked with `diff` on 2026-09-25). Step 10's copy is used unchanged, so
Step 10 can load a baseline that Step 08 saved, and the reverse.

### 2.2 Everything else

| Path | What it is |
|---|---|
| `CMakeLists.txt` (root) | Makes the repo root an ESP-IDF project that builds **Step 08's sources in place** via `EXTRA_COMPONENT_DIRS` (CMakeLists.txt:19-20). There is no copy of the code. The build goes to the root `build/`. The comment at line 10 says "eight step folders"; there are ten. |
| `sdkconfig.defaults` (root and every step); `partitions.csv` (root and Steps 03-10) | Tracked build profile, and the partition table nvs 0x6000, phy_init 0x1000, factory 2M. Steps 01 and 02 have no `partitions.csv` and use ESP-IDF's default table (DECISIONS D-19). `sdkconfig` is git-ignored because it holds the Wi-Fi SSID and password (.gitignore:9-15). |
| `build.ps1` | One-command build/flash helper for Windows PowerShell: activates ESP-IDF v6.0.2's `export.ps1`, validates the serial port **before** building, runs `set-target esp32c5` only if no `sdkconfig` exists, builds, and flashes. |
| `.vscode/tasks.json`, `ESP32C5_CSI.code-workspace` | IDE tasks that call `build.ps1`, or `export.ps1` / `idf.py` directly, or the Python tools: Doctor, Build, Build + Flash + Monitor, Monitor only, menuconfig (calls `export.ps1` and `idf.py` itself, tasks.json:127-140), Clean rebuild, Erase flash, CSI waterfall plot, Presence monitor + plot, Install Python plotting deps. The workspace lists only step folders, so its first folder is always a buildable project. |
| `tools/presence_monitor.py` | Reads `JSON ` lines from Steps 08 and 10: text or live-plot view, CSV logging (`--csv`), sending console commands (`--send`). Since 2026-10-16 it logs `activity`, `activity_s`, `episode_s` and prints a `*** <time>  activity: <state>` line on a change; since 2026-11-03 also `agc`, `fft`, `gain_chg` (CSV only, not shown; D-176). |
| `tools/csi_monitor.py` | Live waterfall of Step 05's `CSI,...` CSV stream. |
| `tools/vitals_validate.py` | Scores Step 10's breathing and heart rate from a `presence_monitor.py` CSV against a reference device, including an always-median oracle so a flat number cannot look accurate. |
| `tools/serial_capture.py` | Records any step's serial output for N seconds with relative timestamps, without `idf.py monitor`. `--send` posts Step 10 console commands at 4 s, 6 s, 8 s and so on. `--reset` is disabled, and DTR/RTS are set False before the port opens (TESTING G1). A 2 s write timeout (since 2026-09-28) makes a stuck `--send` raise; that is handled as a port loss and the command is sent again (TESTING G20). |
| `tools/session_runner.py` | Plays a TESTING §4 protocol (`--protocol phase1`, steps 1a-1c, or `phase2b`) while logging the board in `serial_capture.py`'s `%7.2f <line>` format plus `#` marker lines (segment start and end, cues, port events), and cues the operator (printed, beeped, optionally spoken, with a 10 s countdown before each change of activity). `--note` (placement, who is in the room, AP and channel) is required. DTR/RTS are set False before the port opens and never touched; it reopens the port after a loss; a 2 s write timeout turns a stuck write into a port loss. It sends only `status`, and only to a board identified as Step 10 or, with `--console`, one not yet identified; never to a board identified as Step 08 or Step 09. `--dry-run` prints the schedule and opens nothing; `--simulate FILE` plays a recorded log as the port. Tested offline only, never on the C5 (DECISIONS D-169). |
| `tools/session_score.py` | Scores such a log, or a `serial_capture.py`, `dashboard.py --log` or 2026-09-28 soak log, against the TESTING §4 marks: per segment the present fraction and onsets from the `detect` JSON rows, plus dropped packets, `proc_ms_max`, heap, live rate and restarts. Every mark is printed with its origin and a verdict (met / NOT met / no mark / not applicable). A log without segment markers is one unlabelled segment with no presence verdict. The schedule is the ground truth only as far as the operator followed it. Since version 3 (2026-10-16) it also compares Step 10's `activity` with each segment's cue and prints a confusion table, with no mark (TESTING §2.7, D-172). Since version 4 (2026-11-03; version 5 after that day's review) it also prints a GAIN block for the D-176 keys, with no mark (TESTING §2.7). Standard library (DECISIONS D-169). |
| `tools/kconfig_matrix.py` | Compiles Step 10's `main.c` in 552 Kconfig switch combinations (368 before `CSI_GAIN_LOG`, 2026-11-03; 192 before `CSI_ACTIVITY_ENABLE`, 2026-10-16) with the firmware's own gcc command, plus a negative control (TESTING §2.2, DECISIONS D-145). Needs a prior Step 10 build with every switch on; `--build-dir` for the Ubuntu build (D-171). |
| `tools/dashboard.py` | Live dashboard server (DECISIONS D-163). Owns the COM port (DTR/RTS low before opening, never touched again; reopens the port after a chip reset) or replays a recorded log (`--replay`, `--speed N`, `--max-gap`, `--loop`; commands refused), and serves `http://127.0.0.1:8765/` (`--host`, `--http-port`): `GET /` the page, `GET /events` every serial line as Server-Sent Events (a late page first gets the latest `config`/`cal` lines and a backlog), `POST /cmd` one console command (JSON body, 1-63 printable ASCII characters, same-origin only). `--log FILE` appends every line as `%7.2f <line>`, which `--replay` reads back. Standard library; pyserial for a real port. The HTTP interface is in its docstring. |
| `tools/dashboard.html` | The dashboard page: one self-contained file, no external scripts, works offline. Draws the pipeline strip, link, CSI waterfall, presence, recognition, count, vitals, activity (a tile, 2026-10-16), console and event-log panels from the `EXPLAIN` lines (§3.7), with each panel's "How it decides" text built from the live `config` thresholds. Falls back to the `JSON` line when no explain lines arrive. Its `CSV_FIELDS` (the recorded CSV's columns) include `agc`, `fft`, `gain_chg` since 2026-11-03; no panel shows them. |
| `tools/sample_session.log` | A recorded or clearly marked synthetic (`# SYNTHETIC` on line 1) session in `serial_capture.py`'s `%7.2f <line>` format, for `dashboard.py --replay` demos and the smoke test (TESTING §2.6). Lines starting `#` are ignored. |
| `docs/` | This file, DECISIONS.md, TESTING.md, REFERENCES.md, COMPARISON.md, and `hw_logs/` (raw logs or excerpts linked from TESTING §5.1; full logs stay outside the repo). |
| `step10_recognise_count_vitals/test/` | `run_tests.ps1` (MSVC `/std:c11 /W4 /WX`; binaries in `test/bin/`, git-ignored, .gitignore:41) and `run_tests.sh` (gcc `-Werror`, binaries outside the tree, D-171), plus 10 host test programs (`test_dsp.c`, `test_ncc.c`, `test_vitals.c`, `test_gait.c`, `test_count.c`, `test_gate.c`, `test_pvitals.c`, `test_activity.c` since 2026-10-16, `test_gain.c` since 2026-11-03, `test_e2e.c`) and `test_util.h`. Since 2026-10-30 `test_vitals.c` (9)-(12b) and `test_e2e.c` scenario (f) (an assumed AGC receiver and a shadow static branch) print **KNOWN LIMIT** lines for shortcomings they measure instead of failing (TESTING §3.1). 11,673 checks on 2026-11-01, 11,705 on 2026-11-03 (gcc). |

`CLAUDE.md` and other AI-assistant files are git-ignored (.gitignore:61-62), so
a fresh clone never sees them. Anything a new reader needs must be in a
tracked file such as this one.

**Git state on 2026-09-25** (`git status`). Nothing since `8c60baf` is
committed. **Untracked** (`??`): `docs/ARCHITECTURE.md`, `docs/DECISIONS.md`,
`docs/TESTING.md`, `docs/COMPARISON.md`, `tools/kconfig_matrix.py`,
`tools/serial_capture.py`, `step09_espressif_sensing/.vscode/`,
`step10_recognise_count_vitals/main/person_vitals.c`, `main/person_vitals.h`
and `test/test_pvitals.c`. Add all of them in the same commit as the rest:
`main/CMakeLists.txt` and `test/run_tests.ps1` reference the three Step 10
files, so without them a clean checkout of Step 10 will not build or test;
TESTING §2.2 and §2.5 depend on the two tools; and the four docs are where
the project knowledge lives (DECISIONS D-149). **The index is older than the
working tree**: 17 staged files (`AM` / `MM`, for example Step 10's
`main/main.c`, `Kconfig.projbuild`, `test/test_e2e.c`, `README.md`,
`tools/presence_monitor.py`, `tools/vitals_validate.py`) have further
unstaged edits, and `tools/csi_monitor.py` has unstaged edits only
(`git diff --stat`: 18 files, 2,370 insertions). Committing only what is
staged would commit stale versions, so re-stage everything before
committing (DECISIONS D-150). The explain work of the same day adds three
more untracked files, `tools/dashboard.py`, `tools/dashboard.html` and
`tools/sample_session.log`, which README and TESTING §2.6 depend on: add
them in the same commit. The session tools of 2026-09-28 add two more
untracked files (`git status` on 2026-09-28), `tools/session_runner.py` and
`tools/session_score.py`, which TESTING §2.7 and §4 depend on: add them in
the same commit too. So is `docs/hw_logs/` (seven raw logs or excerpts on
2026-09-28, four of them written that day), which TESTING §5.1 links and
DECISIONS D-159, D-160, D-165 and D-169 cite: add the folder in the same
commit. On 2026-10-16 `git status` also lists as untracked Step 10's
`main/activity.c`, `main/activity.h`, `test/test_activity.c` and
`test/run_tests.sh`, plus `tools/linux_idf.sh` and `docs/ide/`.
`main/CMakeLists.txt` and both test runners reference the Step 10 files,
so a commit without them would not build or test: add them together.
The snapshot of 2026-11-03 also lists Step 10's `main/csi_gain.c`,
`main/csi_gain.h` and `test/test_gain.c` as untracked; `main/CMakeLists.txt`
and both runners reference them, so the same applies.

### 2.3 Build and flash mechanics worth knowing

* Build from **PowerShell** through `build.ps1` or `export.ps1`. ESP-IDF on
  Windows refuses Git Bash / MSys. Bash is fine for editing and grepping.
* `build.ps1` does `Set-Location $proj` (build.ps1:228), so it leaves you in
  the step folder. A script that calls `.\build.ps1` twice must use its full
  path the second time (observed 2026-09-25).
* On Windows PowerShell 5.1, running `build.ps1` with its output redirected
  (`*> file`, `2>&1`) returns exit code 1 even when the build succeeds,
  because ESP-IDF's Kconfig `NOTE:` lines go to stderr and trip the
  `if (-not $?)` check after `idf.py build` (build.ps1:247). Judge a redirected run
  by `Project build complete` / `binary size` in the log (observed
  2026-09-25).
* Reset the C5 only through esptool or `idf.py`. Pulsing RTS/DTR from a
  pyserial script on Windows left the C5's USB-Serial-JTAG latched in reset,
  and the physical RST button was needed to recover (observed 2026-09-25).
  When reading the port from Python, set DTR and RTS False before opening
  it and never pulse them afterwards. `tools/serial_capture.py`,
  `tools/presence_monitor.py` and `tools/csi_monitor.py` all do this (the
  two monitors since 2026-09-25). If the board goes silent anyway, press RST
  (TESTING G1).
* To flash without the interactive monitor (scripts, AI agents), use
  `idf.py -p COMn flash` from the step folder, then read the port with
  `serial_capture.py`. `build.ps1 -Flash` runs `idf.py flash monitor`, which
  never returns in an automated call (TESTING §2.4, DECISIONS D-156).
  On 2026-09-28 at 19:00 a plain `idf.py -p COM13 flash`, with no
  `esptool chip_id` before it, wrote and verified all three images with no
  `Write timeout` (n = 1; whether skipping `chip_id` is why is not
  established; TESTING §5.1, G20). The port was confirmed from pyserial's
  `list_ports`, which does not open it (COM13's serial number is the
  board's BASE MAC). The app then started without an RST press, inferred
  from its uptime.
* A plain `flash` keeps NVS (baseline, models, vitals log). `erase-flash`
  wipes all of it (§4).

---

## 3. Runtime architecture (Step 08 and Step 10)

Step 10's `main.c` is Step 08's `main.c` with additions marked `Step 10:` in
the comments. Everything described for Step 08 below is also true of Step 10
unless a Step 10 difference is named.

### 3.1 Tasks, priorities and queues

| Task / context | Created by | Priority | Stack | Does |
|---|---|---|---|---|
| Wi-Fi driver task (ESP-IDF) | ESP-IDF | ESP-IDF's (not set here) | - | Calls `csi_rx_callback` for every CSI-carrying frame (step08 `main.c:130`, step10 `main.c:478`). |
| `esp_ping` session task | `ping_start` from the GOT_IP event | **2** in Step 08 (`main.c:926`), **5** in Step 10 (`main.c:3292`) | ESP-IDF default | Sends an ICMP echo to the gateway every `CSI_PING_INTERVAL_MS`. The replies are the frames that carry CSI. |
| `csi_pipeline` | `app_main` | 4 | 12,288 B (step08 `main.c:1085`, step10 `main.c:3561`) | Everything else: shape lock, calibration, detection, Step 10 analytics, output, and (Step 10) executing console commands. |
| `csi_console` (Step 10 only) | `app_main` | 1 | 4,096 B (`main.c:3568`) | Reads lines from USB-Serial-JTAG, parses them, and posts `cmd_t` to `s_cmd_q`. Mutates no shared state. |
| Default event loop | `wifi_init_sta` | ESP-IDF's | - | `event_handler`: connect, retry or back off, record the gateway, start/resume/stop the ping. |
| `esp_timer` one-shot | `wifi_init_sta` | - | - | `reconnect_cb` calls `esp_wifi_connect()` after a backoff delay. |

| Queue | Depth | Element | Producer -> consumer |
|---|---|---|---|
| `s_raw_q` | 10 in Step 08 (`main.c:86`), 16 in Step 10 (`main.c:282`) | `csi_raw_t`: len, fwi, rssi, channel, 768-byte buffer; Step 10 adds `t_us`, the arrival time (`main.c:289-300`) | `csi_rx_callback` (non-blocking send; a full queue increments `s_dropped`) -> `pipeline_task` (100 ms receive timeout) |
| `s_cmd_q` (Step 10) | 4 (`CON_QUEUE_LEN`) | `cmd_t` (verb, arg, name) | `console_handle_line` -> `console_drain` in `pipeline_task` |

Why the Step 10 numbers differ from Step 08: Step 10 can stack a gait
window, a count block and a vitals FFT on one packet, and that combination has
never been timed. 16 slots ride out 320 ms of stall at 50 Hz. That only works
because the ping task outranks the pipeline: `esp_ping` keeps one echo in
flight, so a ping task below the pipeline would stop sending during a stall,
and `dropped` could never count an overload (`main.c:275-282`, `3283-3292`).

**Single-owner rule (Step 10).** All Step 10 state is static and is read and
written only by `pipeline_task`. The console task only parses and posts. There
is no lock anywhere in `main.c` (`main.c:328-334`). The one value that
crosses tasks is `s_dropped`: a `volatile`, naturally aligned `uint32_t`
written by the CSI callback and read once per use (`main.c:3071-3077`).

### 3.2 Boot sequence (`app_main`)

1. `nvs_flash_init()`. On `ESP_ERR_NVS_NO_FREE_PAGES` or
   `ESP_ERR_NVS_NEW_VERSION_FOUND` it **erases the whole NVS partition**
   (baseline included) and initialises again.
2. Step 10: `console_install()`, first, before anything else prints
   (`main.c:3517`, `2265-2290`). It installs the USB-Serial-JTAG driver with a
   2 KB TX ring and moves `printf` onto it. What a console write does when
   no host is reading, and how Step 10 keeps the pipeline from waiting on
   it, is DECISIONS D-164. Step 10's boot banner also prints the reset
   reason, `esp_reset_reason()` as a name and a number (D-165).
3. `baseline_load(&s_base)`.
4. Step 10: `analytics_init()` (`main.c:3405-3504`). It initialises the gate,
   vitals and gait; loads `id_model`; then loads `vit_log` and adopts only the
   records whose name matches the current template in that slot; then loads
   `cnt_model`.
5. `led_init()`, `button_init()`, `xQueueCreate(s_raw_q)`.
6. `wifi_init_sta()`: STA mode, `esp_wifi_start()`, then the radio lock
   (`esp_wifi_set_band_mode(WIFI_BAND_MODE_2G_ONLY)`,
   `esp_wifi_set_bandwidth(..., CSI_WIFI_BW20)`, `esp_wifi_set_ps(WIFI_PS_NONE)`).
7. Wait for `WIFI_CONNECTED_BIT` or `WIFI_FAIL_BIT`. On FAIL (first join
   exhausted `CSI_WIFI_MAX_RETRY`): log, wait 5 s, `esp_restart()`.
8. `csi_start()`: acquire config, `esp_wifi_set_csi_rx_cb`, `esp_wifi_set_csi(true)`.
   The ping was already started by the GOT_IP handler.
9. Create `csi_pipeline`. Step 10 then creates `csi_console` (priority 1) and
   prints free heap.

### 3.3 The pipeline's phase machine

`pipeline_task` runs one loop per received packet, or per 100 ms timeout. It
carries a `phase_t` (step08 `main.c:336-346`, step10 `main.c:941-951` on 2026-09-28):

```
boot
  |
  v
PH_LOCK  (observe CSI_LOCK_PACKETS packets, lock the most common length)
  |
  +-- stored baseline matches the locked shape and channel -> PH_SETTLE (CSI_CAL_WARMUP_S)
  |                                                                |
  +-- no baseline / mismatch / CSI_FORCE_RECALIBRATE               |
        |                                                          |
        v                                                          |
      PH_LEAVE (10 s) -> PH_WARMUP (5 s, measures rate_hz)         |
        -> PH_A (15 s, mu[k], sigma[k]) -> PH_SELECT (mask)        |
        -> PH_B (15 s, empty-room M/D stats) -> PH_SAVE (to NVS)   |
                                                  |                |
                                                  v                v
                                                PH_DETECT <--------+
                                                  |
  button held 2 s, or 'recal' [S10]; only in      |
  PH_DETECT, and [S10] not during no_csi  --------+--> PH_LEAVE (EMAs re-seeded)

  [S10] Wi-Fi channel changed after lock (channel_follow): from any
  calibration phase, or from SETTLE/DETECT unless the stored channel
  is 0 or is the new one; not during no_csi  -------> PH_LEAVE (same path)
```

The durations are the Kconfig defaults (`CSI_CAL_LEAVE_ROOM_S`,
`CSI_CAL_WARMUP_S`, `CSI_CAL_PHASE_A_S`, `CSI_CAL_PHASE_B_S`). "Matches" means
`csi_len`, `nsub` and `fwi` are equal, `n_selected >= 8`, `m_std > 0`, and
the baseline's channel and the live one at shape lock are equal, or either
one is 0 ("not known") (step08 `main.c:436-444`, step10 `main.c:3470-3478`
on 2026-09-28). The channel joined the match on 2026-09-28 (DECISIONS D-168,
which superseded the warning of D-167 the same day): that day the hotspot
came back on channel 6, and the image built before both used the channel-1
baseline without a word (TESTING §5.1). Step 10 also recalibrates after
lock. When `channel_follow` adopts a new channel it returns true, and its
caller (step10 `main.c:3541-3546` on 2026-09-28) sets `chan_recal` if a
calibration is in progress, or if the stored channel is nonzero and differs
from the new one. The next iteration takes the button / `recal` path in any
phase past LOCK, and while `no_csi` it waits (§3.5 step 5). A PH_SAVE on the
adoption packet saves nothing (`3722` on 2026-09-28). Step 08 has no
`channel_follow`, so there a change after lock is not acted on until the
next boot. Its PH_SAVE stamps the channel of the packet it saves on, or the
channel at lock if that packet's is 0 (step08 `main.c:672`), so the next
boot compares against the channel the calibration ran on. None of this has
run on a board yet.

### 3.4 Per-packet data flow (diagram)

`[S10]` marks Step 10 additions. Everything else is Step 08 and is identical
in Step 10.

```
 Wi-Fi driver task                              esp_ping task (S08 prio 2, S10 prio 5)
 -----------------                              ICMP echo to the gateway every
 frame with CSI received  <---- router reply <-- CSI_PING_INTERVAL_MS (default 20 ms)
        |
        v
 csi_rx_callback()          copy len, fwi, rssi, channel, buf  [S10 + t_us = arrival time]
        |                   [S10, CSI_GAIN_LOG] + agc, fft = rx_ctrl bytes 23 / 22 (csi_gain_read)
        |  xQueueSend(s_raw_q, &r, 0)    queue full -> s_dropped++   (never blocks)
        v
 s_raw_q  (S08: 10 x csi_raw_t, S10: 16)
        |  xQueueReceive(..., 100 ms)    timeout -> skip straight to "periodic"
        v
 pipeline_task ("csi_pipeline", prio 4) --------------------------------------------------
 |  [S10] console_drain() -> cmd_execute()          commands posted by csi_console (prio 1)
 |  recal? (button 2 s / [S10] 'recal', in PH_DETECT; [S10] chan_recal, any phase
 |          past LOCK; [S10] not during no_csi) -> [S10] analytics_on_recal() -> PH_LEAVE
 |  [S10, CSI_GAIN_LOG] csi_gain_push(agc, fft): every dequeued packet, every phase; feeds nothing
 |  PH_LOCK: shape_observe() x CSI_LOCK_PACKETS -> shape_lock()
 |           -> PH_SETTLE [S10 analytics_configure()] or PH_LEAVE
 |  r.len != locked length -> wrong_shape++, drop
 |  [S10] channel_follow(r.channel) -> true on adoption -> chan_recal if calibrating, or if
 |        the stored channel is nonzero and not the new one (D-168); acted on at the top
 |        of the next iteration
 |  csi_amp() for k < nsub -> median_of() (floored at 1.0) -> s_na[k] = amp[k] / median
 |  s_fast[k] EMA (tau 0.5 s); s_slow[k] EMA (tau 60 s, frozen while PRESENT, max 600 s)
 |  calibration phases (PH_LEAVE .. PH_SAVE [S10 analytics_configure(); PH_SAVE
 |     saves nothing while chan_recal is set]; [S10] on entering PH_DETECT:
 |     gain_detect_start() logs "gain before detection" and restarts the count) -- or --
 |  PH_DETECT:
 |    M = mean_k |na - fast| / sigma_k    D = mean_k |fast - slow| / sigma_k   (selected k)
 |    smooth both (METRIC_SMOOTH 0.15) -> M_s, D_s
 |    m_sigma = (M_s - m_mean) / m_std   d_sigma = (D_s - d_mean) / d_std
 |       (baseline phase-B statistics; 0 if the std is 0)  -> empty-room std devs
 |    motion_flag: on at 5.00, off below 2.50 | static: d_sigma >= 5.00 for 4 s
 |    new_present = trigger OR (present AND < 5 s since last trigger)
 |    [S10] -------------------------------------------------------------------------
 |    |  dsp_group_dev() -> grp[8] -> mgate_push(t_us, grp, room_empty = !new_present) -> still
 |    |  vitals_push(t_us, s_na, still) -> vit_done
 |    |  breathing hold: still && episode began PRESENT && br_valid -> new_present = true
 |    |  pv_packet(t_us, still); drop the carried count on movement / after 60 s
 |    ---------------------------------------------------------------------------------
 |    presence transition: LED, log line
 |       [S10] start -> s_carry_wait = true | end -> pv_presence_end(), analytics_reset(false)
 |    [S10] -------------------------------------------------------------------------
 |    |  carry hand-back when breath_hold (pv_presence_start, count restored)
 |    |  if present: count_step(grp, motion_flag && !still)
 |    |              id_step(grp, motion_flag && !still, current_count())
 |    |  act_push(t_us, present, still, hf, breath_hold)  (CSI_ACTIVITY_ENABLE, 2026-10-16)
 |    |  if vit_done: pv_analysis()
 |    ---------------------------------------------------------------------------------
 |    every CSI_JSON_PERIOD_MS: "JSON {...}"  ([S10] report_build() -> json_step10())
 |       [S10] json_step10() ends with json_gain(): agc, fft, gain_chg (D-176)
 |    every 250 ms (CSI_HUMAN_READABLE): bar line  ([S10] + n= who= of= br= hr~)
 |    [S10, CSI_EXPLAIN] "EXPLAIN {...}" + a "why:" log line when a gait window, count
 |       block, vitals analysis or presence change completes; "amp" at most
 |       CSI_EXPLAIN_AMP_HZ per second (§3.7)
 |  periodic:
 |    [S10] this packet's processing time -> proc_ms_max; once a minute: dropped check
 |    no-CSI watchdog (PH_DETECT): CSI_NO_CSI_TIMEOUT_MS without a usable packet
 |       -> present = false, LED off  [S10 analytics_reset(true)]
 |    [S10] vit_log save if due (pv_save_due)
 |    no_csi JSON line; once a second: calibration progress / [S10] live-rate check
 ------------------------------------------------------------------------------------------
        |
        v
 USB-Serial-JTAG console -> idf.py monitor / tools/presence_monitor.py
```

### 3.5 Walk-through, one packet in Step 10's PH_DETECT

Line numbers are step10 `main.c`. For Step 08, drop every "Step 10" item; the
remaining Step 08 lines are named in brackets.

1. **Capture** - `csi_rx_callback` (`478-493`; Step 08 `130-144`). It rejects
   a null buffer, zero length or anything over `CSI_MAX_CSI_BYTES` (768),
   copies the frame into a `csi_raw_t` on its stack, and calls `xQueueSend`
   with timeout 0. It does no maths, no printing and no blocking. The Wi-Fi
   task must never wait on the pipeline. Step 10 with `CSI_HAVE_GAIN` also
   copies two `rx_ctrl` bytes, the AGC and FFT gain, with `csi_gain_read`
   (`695-697` on 2026-11-03; D-176).
2. **Commands (Step 10)** - at the top of every loop iteration `pipeline_task`
   builds a `pipe_view_t` and calls `console_drain` (`2367-2377`). This runs
   `cmd_execute` (`2056-2087`) for every queued command, so commands run in
   this task, between packets.
3. **Recalibration** - `!no_csi && (chan_now || (phase == PH_DETECT &&
   (recal_req || button_held_2s())))`, where `chan_now` is `chan_recal &&
   !no_csi` and step 5 sets `chan_recal` (`3418-3443` on 2026-09-28; Step
   08 `399-415`, `phase == PH_DETECT && button_held_2s()`, which has no
   `no_csi` test, no command and no `chan_recal`). It clears the decision
   state and calls `analytics_on_recal` (cancels any session, resets
   analytics, clears the gate's noise floor). The EMAs re-seed from the
   next packet, and the phase goes to PH_LEAVE.
4. **Dequeue and shape** - `xQueueReceive` with a 100 ms timeout (`2407`).
   `t_work0` starts the per-packet timer (`2414`). Step 10 with
   `CSI_HAVE_GAIN` then pushes the packet's gain into `s_gain`
   (`3694-3698` on 2026-11-03), for every dequeued packet in every phase;
   it feeds nothing but the JSON line. In PH_LOCK, `shape_observe`
   / `shape_lock` pick the most common `len` over `CSI_LOCK_PACKETS` packets.
   A dominant share under 90% logs an error recommending `CSI_LLTF_ONLY`. If
   the stored baseline matches, `analytics_configure(rate_hz)` runs (`2448`).
   After the lock, any packet whose `len` differs is counted as `wrong_shape`
   and dropped (`2465-2468`).
5. **Channel (Step 10)** - `channel_follow(r.channel)` (`1306` on
   2026-09-28) adopts a new Wi-Fi channel after 100 consecutive accepted
   packets on it: it stores it in `s_locked_ch`, which `status`, PH_SAVE
   and the model stamps then use. It warns, re-runs `model_shape_check`
   (a model trained on another channel describes a different radio
   channel) and returns true. Its caller (`3541-3546` on 2026-09-28) then
   sets `chan_recal` if a calibration is in progress, or if the stored
   channel is nonzero and differs from the new one. The next iteration
   recalibrates on the button / `recal` path in any phase past LOCK, and
   waits while `no_csi` (`3418-3443` on 2026-09-28). A PH_SAVE that reaches
   the same packet saves nothing (`3718-3722` on 2026-09-28; DECISIONS
   D-168).
6. **Amplitude and AGC** (`2482-2538`; Step 08 `495-551`) - `csi_amp` gives
   `sqrt(im^2 + re^2)` per subcarrier: int8 pairs by default, or the 12-bit
   L-LTF words when `CSI_LLTF_ONLY` is set. `median_of` finds the median,
   floored at 1.0, and `s_na[k] = amp / median` removes the AGC's per-packet
   scale. The fast EMA always updates. The slow EMA is frozen while PRESENT in
   PH_DETECT, for at most `CSI_SLOW_FREEZE_MAX_S` (default 600 s).
7. **Metrics and Step 08's decision** (`2715-2755`; Step 08 `716-767`) - M and
   D are averaged over the selected subcarriers, each term divided by that
   subcarrier's empty-room sigma. Both are smoothed with `METRIC_SMOOTH`
   (identical in calibration phase B). They are expressed as `m_sigma` and
   `d_sigma` in empty-room standard deviations. Then: motion hysteresis
   (`THR_MOTION` / `THR_RELEASE`), the static branch sustained for
   `CSI_STATIC_SUSTAIN_MS`, and the `CSI_PRESENCE_HOLD_MS` hold. The result
   is `new_present`. The formulas: `m_sigma = (M_s - m_mean) / m_std` and
   `d_sigma = (D_s - d_mean) / d_std`, each 0 when its std is 0 (step08
   `main.c:733-734`, step10 `main.c:3806-3807` on 2026-09-28, DECISIONS D-66). Two
   different sigmas are involved: `sigma_k` (phase A, per subcarrier) weights
   the subcarriers inside M and D, while `m_mean`/`m_std` and
   `d_mean`/`d_std` (phase B, per metric) turn the smoothed metric into
   standard deviations. Then `trigger = motion_flag || static_flag`
   (step10 `2750`) and `new_present = trigger || (present && now -
   last_trigger < CSI_PRESENCE_HOLD_MS)` (`2753`).
8. **Group signals and motion gate (Step 10)** (`2780-2784`) -
   `dsp_group_dev` turns the selected subcarriers into 8 group signals `grp[]`
   (deviation from the fast EMA in noise units). `mgate_push(&s_gate, r.t_us,
   grp, !new_present)` returns `still`. `room_empty` is Step 08's decision
   **before** the breathing hold, because the hold is derived from the gate's
   own output and feeding it back would close a loop (`2768-2779`).
9. **Vitals and the breathing hold (Step 10)** (`2786-2832`) - on the first
   still packet after a moving one, `s_still_began_present` records whether
   this still episode began PRESENT. `vitals_push(&s_vit, r.t_us, s_na, still)`
   returns `vit_done` when an analysis completed on this packet. With
   `CSI_VITALS_HOLDS_PRESENCE`, a still packet in an episode that began PRESENT
   with a valid breathing rate refreshes `last_trigger` and forces
   `new_present = true`. This is Step 10's only influence on Step 08's
   presence answer.
10. **Lapse bookkeeping (Step 10)** (`2834-2857`) - `pv_packet(&s_pv, r.t_us,
    still)` ends a pending vitals episode on movement, and releases the latch
    if it moved or was absent longer than `CSI_CARRY_US` during a lapse. The
    carried count is dropped under the same rules. This runs **before** the
    transition, so movement on the packet that ends a lapse still counts as
    movement while absent.
11. **Presence transition** (`2859-2907`; Step 08 `758-767`) - updates the LED
    and logs. Step 10, on a start: sets `s_carry_wait` (or calls
    `pv_presence_start` when nothing can be carried). On an end: calls
    `pv_presence_end`, reads the count to carry, calls
    `analytics_reset(false)` (clears the gait and count accumulators and both
    vote rings, but not vitals and not the gate), then stores the carry.
12. **Carry hand-back (Step 10)** (`2909-2951`) - while `s_carry_wait` is set,
    the first packet on which the breathing hold is asserting PRESENT hands
    back the latch (`pv_presence_start`) and the carried count. A gate-MOVING
    packet first ends the wait with both already released. Why: on a walk-in
    Step 08 fires 0.4-3.5 s before the gate does (test_e2e), so ending the
    lapse at Step 08's own start gave a newcomer the name and count of the
    person who had left. While `s_carry_wait` is set, `who_present(present)`
    (`465-472`) returns false. `report_build` then gets `vitals_of` = null
    from `pv_vitals_of` (`1502`), and `pv_analysis` credits nothing
    (`2985`), even though `present` may already be 1 and rates may print
    (DECISIONS D-110 (1)). The wait ends on breathing, not on recognition,
    because a seated person casts no votes (D-107). A valid breathing hold
    shows that the still spell which began PRESENT before the lapse is
    unbroken (D-108). With `CSI_VITALS_HOLDS_PRESENCE` off, `CSI_CARRY_WAIT`
    is 0 (`CSI_CNT_CARRY` needs the hold too, §5.5), presence end releases
    the latch, and
    `vitals_of` stays null until the person is recognised walking again.
13. **Count, then recognition (Step 10)** (`4199-4221` on 2026-10-16) - only while PRESENT.
    `moving = motion_flag && !still`, so a breathing person who trips Step 08's
    flag cannot fill gait windows or count blocks. `count_step` runs first, so
    that `id_step` knows whether this might be two people and stays silent at
    count >= 2.
13b. **Activity state and presence episodes (Step 10, since 2026-10-16)**
    (`4223-4232` on 2026-10-16; DECISIONS D-172) - `act_push(&s_act, r.t_us,
    present, still, mgate_ratio(&s_gate), breath_hold)` with this packet's
    **final** presence (after the breathing hold and the transition),
    because a lasting "still" is PRESENT held by the hold (activity.h).
    `breath_hold` tells the episode counter that a lapse was undone, not
    ended. It runs before the vitals crediting, whose EXPLAIN `vitals` event
    reports the state. Compiled only with `CSI_HAVE_ACTIVITY` (§5.5).
14. **Credit the vitals log (Step 10)** (`4234-4244` on 2026-10-16) - `pv_analysis` runs once
    per completed analysis (about every 2 s), after this packet's transition,
    count and recognition, so present, count and latch are all this packet's.
15. **Output** (`2994-3052`; Step 08 `773-797`) - the JSON line: Step 08's keys
    in Step 08's order, then `json_step10(report_build(...))`, assembled in a
    1024-byte buffer and printed with one `printf`. The bar line prints every
    250 ms. JSON `present` is `present` after this packet's transition
    (step08 `758-767`, step10 `2859-2860`). A `detect` line is printed only
    from a packet processed in PH_DETECT, once `CSI_JSON_PERIOD_MS` has
    passed since the last one (step08 `773-781`). With no packets there is
    no `detect` line at all: the periodic section prints the `no_csi` line
    (`present` 0) after `CSI_NO_CSI_TIMEOUT_MS` (step 16). Step 10's
    `json_step10` ends with `json_gain` (`1993`, called at `2044`, on
    2026-11-03): `agc`, `fft`, `gain_chg` after `episode_s`, taken from
    `s_gain`, or three `null`s (D-176).
16. **Periodic, after every iteration** (`3060-3216`; Step 08 `805-860`) -
    Step 10: record this packet's processing time (`proc_ms_max`, and the
    since-boot maximum for `status`), and once a minute warn if `s_dropped`
    grew. No-CSI watchdog: after `CSI_NO_CSI_TIMEOUT_MS` in PH_DETECT without a
    usable packet it sets `present = false`, turns the LED off, and in Step 10
    calls `analytics_reset(true)`, which also resets vitals, the gate's filters
    and the latch (the gate's floor is kept). Step 10: save `vit_log` if
    `pv_save_due`, outside the per-packet timing. The `no_csi` JSON line. Once
    a second: calibration progress, and in Step 10 a live-rate check against
    the calibrated rate every 10 s (warns once at more than 20% off).

**The per-packet order in steps 8-14 (13b included) is a contract.** `test/test_e2e.c`
re-implements it by hand in `gl_packet()` (test_e2e.c:1729-1796; `act_push`
at `:1794`, both on 2026-11-01; `:1426` and `:1491` on 2026-10-16). Reordering
these calls in `main.c` does not break the host tests; it makes them test
the old behaviour. Change the mirror in the same commit. The gain push
(D-176) is not part of the contract: it feeds no decision, so `gl_packet()`
has no gain step.

`test/test_pvitals.c` section (k) has a second, smaller mirror,
`drv_packet()` (test_pvitals.c:1287-1345), and it **predates D-108**: it has
no `s_carry_wait` and no `who_present`, calls `pv_presence_start` and hands
back the carried count at every presence start (the replaced D-109 rule),
and calls `pv_packet` before the vitals step rather than after the breathing
hold. Its "Step 08 re-triggers while the gate stays still: kept" check
(test_pvitals.c:490-495) asserts the pre-D-108 hand-back. Treat (k) as a
unit-level test of `person_vitals.c`, not as a replay of `main.c`'s current
order (TESTING §3.3 item 3).

### 3.6 What is reset when

`analytics_reset` in step10 `main.c:935-989` is the reference; its comment
states "the rules the JSON relies on".

| Event | Cleared | Kept |
|---|---|---|
| Presence ends (`analytics_reset(false)`) | gait window, count block, both vote rings, `s_carry_wait` | vitals buffer (a still person whose PRESENT lapsed must keep accumulating), motion gate, latch (`pv_presence_end` decides), carried count (the caller re-takes it), the activity state and the presence episode (an episode spans a lapse the breathing hold undoes; `act_push` itself ends it otherwise, activity.h) |
| No-CSI watchdog, recalibration, new baseline (`analytics_reset(true)`) | all of the above, plus vitals, the gate's filters and decision, the latch (`pv_release`), any carried count, and the activity state and episode (`act_reset`, which ends an open episode; `main.c:1157-1162` on 2026-10-16) | the gate's noise floor (except after a recalibration or new baseline, where `analytics_on_recal` / `analytics_configure` clear it, because `grp` is then in the new sigma's units), models, vitals log |
| Session start (`cmd_enroll`, `cmd_count_train`) | both vote rings and the count estimate; `enroll` also resets the gait window, clears that slot's vitals-log record and saves the log at once; `count-train` resets the count block; the latch is released | models other than the class being filled |
| Switch to `PH_DETECT` (boot or `recal`; `gain_detect_start`, `main.c:506-515` on 2026-11-03, D-176) | the gain period's packet and change counts and agc lo/hi, after the calibration's summary is logged once as `gain before detection: ...` | the last agc / fft, so a change across the switch counts on the first detect line. No other event resets `s_gain`: a detect line after a CSI outage reports `null` because no packet arrived |
| `stop`, or a recalibration during a session (`session_cancel`, `main.c:1048-1075`) | the class being filled (then that model is saved). Cancelling an enrolment also clears the ID vote ring and that slot's vitals-log record (`pv_slot_gone`). Cancelling a count session also clears the count votes, estimate and carry (`cnt_votes_clear`) and the gait window (`gait_reset`) | the other classes, vitals, the gate |

### 3.7 Explain output (Step 10, `CSI_EXPLAIN`)

With `CSI_EXPLAIN` on (the default), Step 10 prints, besides the `JSON` line,
one `EXPLAIN {json}` line per decision event, plus a plain-English `why:`
log line for a person reading `idf.py monitor` (DECISIONS D-161, D-162).
`tools/dashboard.py` and `dashboard.html` are built on these lines (D-163).
**Source of this section:** the design specification of 2026-09-25 and the
`CSI_EXPLAIN` / `CSI_EXPLAIN_AMP_HZ` help in `main/Kconfig.projbuild`. The
firmware was written in parallel with it; `main.c`'s header comment lists
every key (per that help). Where the code and this table disagree, the
code wins (header of this file); fix the table.

**Line rules.**

* Prefix `EXPLAIN ` (with the space), exactly like `JSON `, then one JSON
  object whose `ev` key names the event. One `printf` per line, so a
  console reply cannot land inside it (DECISIONS D-128).
* Floats have at most 3 decimals. Unknown and non-finite values print as
  `null`. The only strings are literals and names that passed `name_valid`,
  which is why no escaping is needed (§7.1 item 4).
* **Cost.** Nothing new runs per packet except the rate-limited `amp` line.
  Every other line is printed only when its event completes. The worst-case
  line length is worked out in a comment beside the explain code in
  `main.c`, and must stay well under the console TX ring (`console_install`;
  D-133 records 2 KB, the `CSI_EXPLAIN` Kconfig help says 4 KB). The same
  help puts the extra output at about 3–4 KB/s, and says an `amp` line is
  about 150–550 bytes, formatted with integer arithmetic (no soft-float
  `printf`), and **skipped** while the TX buffer is full, i.e. while no
  host reads (D-164).
* Existing tools are unaffected: `presence_monitor.py` ignores lines that
  do not start with `JSON `, `CMD ` or four spaces (D-136), and
  `serial_capture.py` records every line.

| `ev` | Printed when | Keys |
|---|---|---|
| `config` | detection starts (after the shape lock and a calibration or a stored baseline), after `recal`, and on the console command `explain` | `t_ms`, `rate_hz`, `csi_len`, `nsub`, `channel`, `lltf` (bool), `thr_motion`, `thr_release`, `thr_static`, `static_sustain_ms`, `presence_hold_ms`, `gate_ratio`; `id`: {`reject`, `margin`, `votes`, `min_votes`, `win`, `hop`, `min_moving`, `slots`: [{`name` or `null`, `n`, `ready`}]}; `count`: {`votes`, `win`, `min_duty`, `ready`, `n1`, `n2`}; `vitals`: {`window_s`, `min_s`, `br_conf_min`, `hr_conf_min`} |
| `cal` | a calibration completes | `m_mean`, `m_std`, `d_mean`, `d_std`, `nsub_selected`, `csi_len`, `channel` |
| `amp` | at most `CSI_EXPLAIN_AMP_HZ` times a second (default 5; 0 = never) | `t_ms`; `a`: the normalised amplitude (the same `na` the metrics use) of each **selected** subcarrier, 2 decimals, at most 64 values (with more selected, every 2nd, 3rd, … one, per the Kconfig help); `m` (motion_sd), `d` (dev_sd), `hf` (gate ratio), `still` (bool), `present` (0/1) |
| `gait` | every completed gait window (a 256-packet window every 128 packets), whether or not it votes | `win` (counter), `moving_frac`, `used` (bool); `why` when not used: `"few-moving"` (below the 80 % `GAIT_MIN_MOVING` bar), `"multiple"` (count ≥ 2), `"no-model"`, `"session"`, `"enrol"`; `dist`: one distance per slot 0–3, `null` for a slot that is not ready; `nearest`, `second_dist`, `best` (slot, or -1 = unknown); `reason`: `"ok"`, `"too-far"` (distance above `reject`) or `"ambiguous"` (runner-up inside the `margin`); `vote`: a name, `"unknown"` or `null`; `ring`: the vote ring oldest to newest as a string, a slot digit per vote and `?` for unknown (e.g. `"00?0"`); `tally`: {`label`: a name, `"unknown"` or `null`, `k`, `n`} |
| `count` | every completed count block (256 packets, not overlapping) | `blk`, `duty`, `used` (bool) and `why` (`"low-duty"` below 0.3, `"no-model"`, `"session"`); `feat`: the count feature vector (at most 8 values); `dist`: [distance to one mover, to two or more], `null` until the model is ready; `vote` (1, 2 or `null`); `ring` (e.g. `"1121"`); `est` (0, 1 or 2) |
| `vitals` | every completed vitals analysis (`vitals_push` returned true; about every 2 s while the gate says still) | `state`, `br`, `br_conf`, `hr_exp`, `hr_conf`, `still_s`, `vitals_of` (a name or `null`), `multiple` (bool); since 2026-10-16 also `activity`, `activity_s`, `episode_s`, as on the JSON line (`main.c` header) |
| `presence` | every change of PRESENT | `present`, `reason`: `"motion"`, `"static"`, `"breathing-hold"`, `"hold-expired"`, `"no-csi"` or `"recal"`; `m`, `d` |

How the recognition keys map onto §6's rules: a window votes only if it is
`used`; the vote is the nearest ready slot when `reason` is `"ok"`, and
`"unknown"` otherwise; `tally` is `id_tally`'s answer over the ring (a name
needs at least 3 votes, strictly more than `"unknown"`, and no tie). For
counting: a block votes only when `used`; `est` is the 5-block majority,
where 2 needs a strict majority. The heart-rate key is `hr_exp`, not `hr`,
for the reason §8 gives for `hr_bpm_exp`.

**Changing it.** Treat it like the JSON line (§7.1): add keys, never
rename, move or retype one, because `dashboard.html` parses them by name
and recorded logs (`tools/sample_session.log`, `--log` files) must still
replay. In the same change: update this table, the parser in
`dashboard.html`, and, if the event is new, the sample log; then run the
replay smoke test (TESTING §2.6). A new event must be printed on an event,
never per packet. A new `why` or `reason` value must also be added to the
dashboard's explanations.

---

## 4. Persistent state (NVS)

Everything lives in the default `nvs` partition (24 KB, `partitions.csv`),
namespace `"csi"` (`CSI_NVS_NAMESPACE`, csi_baseline.h:19). The Wi-Fi driver
also needs NVS (step02 `main.c:168`). Each record is one blob, written byte
for byte from a C struct, so **the struct layout is the storage format**.

| Key | Struct | Magic / version | Written by | Read by |
|---|---|---|---|---|
| `baseline` | `csi_baseline_t` (csi_baseline.h:22-47), ~3.5 KB | `0x43354253` "C5BS", version 2 | the save phase of a calibration in Steps 06 (`CAL_SAVE`), 08 and 10 (`PH_SAVE`), via `baseline_save` | Steps 07, 08, 10 (`baseline_load`) |
| `id_model` | `ncc_model_t` (csi_ncc.h), `nfeat` = 18 | `0x3143434E` "NCC1", `NCC_VERSION` 1 | Step 10: enrolment completes, `forget`, a cancelled enrolment | Step 10 `analytics_init` (`model_load`) |
| `cnt_model` | `ncc_model_t`, `nfeat` = 6 | "NCC1", version 1 | Step 10: count-training completes, `count-reset`, a cancelled training | Step 10 `analytics_init` |
| `vit_log` | `pv_log_t` (person_vitals.h:90-109), exactly 168 bytes | `0x314C5650` "PVL1", version 1 | Step 10: periodic save, `enroll` start, `forget`, `vitals-reset` | Step 10 `analytics_init` (`vit_log_load`, then `pv_adopt_log`) |

**`csi_baseline_t`** holds the CSI shape it is valid for (`csi_len`, `nsub`,
`fwi`, `channel`), `n_selected`, the measured `rate_hz`, the per-subcarrier
`mu[384]`, `sigma[384]` and `selected[384]`, and the empty-room
`m_mean/m_std/d_mean/d_std` from which every threshold is derived.
`rate_hz` also drives the EMA coefficients and, in Step 10, every gait and
gate frequency (`analytics_configure`).

**`ncc_model_t`** holds a header (`magic`, `version`, `nfeat`, and `csi_len`
and `channel` stamped when a session completes) and 4 classes, each with
`name[16]`, `n`, and Welford `mean[18]` and `m2[18]`. The stamp is stored
**once per model, not per class**. `model_mix_check` warns when a new class
is added on a different shape or channel than the others, because the new
stamp will hide that. A per-class record would need `NCC_VERSION` 2
(`main.c:1005-1010`).

**`pv_log_t`** holds 4 records of 40 bytes (`name[16]`, `br_eps`, `hr_eps`,
`samples`, `br_last`, `br_mean`, `hr_last_exp`, `hr_mean_exp`) behind an
8-byte header. Two `_Static_assert`s pin the layout (person_vitals.c:44-46).
Several comments say "~170 bytes"; the asserts make it exactly 168.

### 4.1 Load policy: all or nothing

| Loader | Rejects when | On rejection |
|---|---|---|
| `baseline_load` (step10 `main.c:551-565`) | size, magic or version differ | no baseline, so the firmware calibrates. The PH_LOCK shape check (§3.3) is a second gate. |
| `model_load` (`618-635`) | size differs, `ncc_valid` fails (magic, version, `nfeat`, unterminated name, non-finite or negative statistics), or `model_names_ok` fails (name outside `[A-Za-z0-9_-]`, or data without a name) | an **empty** model, never a partly trusted one |
| `vit_log_load` (`655-671`) | `pv_log_valid` fails: size, magic, version, `nslots`, name alphabet, counts under an empty name, `hr_eps > br_eps`, any float non-finite or outside [0, 200] | an **empty** log. The NVS key is not erased; the next save overwrites it. |
| `pv_adopt_log` (person_vitals.c:285) | a record's name differs from the name now in that slot of `id_model` (re-enrolled, forgotten, or the model failed to load) | that record is cleared and `dirty` is set |

The writer side keeps its own output valid: `person_vitals.c` bounds values,
clamps the running mean and saturates counters, so a log it wrote always
passes `pv_log_valid` (person_vitals.c:31-34). `ncc_add` commits nothing
unless every input and every updated statistic is finite (csi_ncc.c:113-156).

### 4.2 Write policy

| Record | When it is written |
|---|---|
| `baseline` | Once, at PH_SAVE of each calibration. |
| `id_model` / `cnt_model` | When a session completes, on `forget` / `count-reset`, and on `session_cancel`. The cancel save matters because the class was already wiped in RAM when the session started; without the save, a reboot would bring it back. |
| `vit_log` | Only when `dirty`, and at most once per `CSI_VITALS_LOG_SAVE_S` (default 300 s) counted from the last **attempt** (`pv_save_due` / `pv_saved`). Also immediately on `forget`, `vitals-reset` and `enroll` start. The periodic save runs after the per-packet timing, so it does not show in `proc_ms_max`; one slower than 50 ms is logged. Up to 300 s of log is lost on a power cut. The wear estimate is in the `CSI_VITALS_LOG_SAVE_S` Kconfig help, labelled as an estimate. |

### 4.3 How the stored state is lost

* `idf.py erase-flash` (or the "Erase flash" task): everything.
* `nvs_flash_init()` returning `NO_FREE_PAGES` / `NEW_VERSION_FOUND` at boot:
  `app_main` erases the whole partition (every step does this).
* `CSI_FORCE_RECALIBRATE=y`: the stored baseline is ignored at every boot
  until the option is set back to n. Models and log are kept.
* A mismatched baseline (shape changed, `CSI_LLTF_ONLY` toggled, or stored
  on another Wi-Fi channel, D-168; in Step 10 also a channel change after
  lock): the firmware recalibrates and overwrites it.
* `forget` and `vitals-reset` delete records, but NVS is **not encrypted**,
  and an erase only marks entries as erased. This is a delete, not a secure
  wipe (step10 `main.c:1938-1942`; README § "Whose vitals: `vitals_of` and
  the per-person vitals log").

---

## 5. Configuration

### 5.1 Where configuration comes from

* `main/Kconfig.projbuild` of each step defines the project's options
  (`menuconfig` -> "ESP32-C5 CSI Human Detector"). Thresholds are **integers
  in hundredths** (`*_X100`: 500 = 5.00 standard deviations), converted in
  code (`THR_MOTION` and similar, step10 `main.c:284-286`).
* `sdkconfig.defaults` (tracked) seeds a fresh `sdkconfig` (ignored). Each
  step has its own `sdkconfig`, so the SSID and password must be set per
  step. Step 10's defaults also set `CONFIG_IDF_TARGET="esp32c5"`, so running
  `menuconfig` before the first build cannot configure it for the classic
  ESP32. Steps 01-09 lack this line, so run `build.ps1` once before
  `menuconfig` for them.
* Build-wide choices in every `sdkconfig.defaults`: -O2 for the app, -Os for
  the bootloader, 1000 Hz tick, QIO 80 MHz flash, assertions on, and the
  USB-Serial-JTAG console. Steps 03-10 and the root also set CSI on, Wi-Fi RX
  buffers 16/32, the custom partition table and a 921600-baud console line;
  Steps 01 and 02 do not (DECISIONS D-17 to D-20). These choices are
  explained inline in those files and in REFERENCES.md §7. Their reasons are
  recorded in DECISIONS.md.

### 5.2 Step 08 options (also present in Step 10)

| Menu | Symbols |
|---|---|
| WiFi | `CSI_WIFI_SSID`, `CSI_WIFI_PASSWORD`, `CSI_WIFI_MAX_RETRY` (10), `CSI_PING_INTERVAL_MS` (20, range 5-1000) |
| CSI capture | `CSI_LLTF_ONLY` (n), `CSI_LOCK_PACKETS` (200) |
| Calibration | `CSI_FORCE_RECALIBRATE` (n), `CSI_CAL_WARMUP_S` (5), `CSI_CAL_PHASE_A_S` (15), `CSI_CAL_PHASE_B_S` (15), `CSI_CAL_LEAVE_ROOM_S` (10), `CSI_SELECT_MIN_AMP` (20) |
| Detection tuning | `CSI_MOTION_SIGMA_X100` (500), `CSI_MOTION_RELEASE_X100` (250), `CSI_STATIC_SIGMA_X100` (500), `CSI_STATIC_SUSTAIN_MS` (4000), `CSI_PRESENCE_HOLD_MS` (5000), `CSI_SLOW_FREEZE_MAX_S` (600), `CSI_NO_CSI_TIMEOUT_MS` (5000) |
| Outputs | `CSI_LED_GPIO` (-1), `CSI_LED_ACTIVE_LOW` (n, depends on `CSI_LED_GPIO >= 0`), `CSI_BUTTON_GPIO` (-1), `CSI_JSON_PERIOD_MS` (500, 0 disables), `CSI_HUMAN_READABLE` (y) |

The defaults are starting points to tune, not validated values
(README § "Verification status — please read").

Step 10's "CSI capture" menu has one option that Step 08's lacks:
`CSI_GAIN_LOG` (y; log the receiver gain on each detect JSON line, logging
only; step10 `Kconfig.projbuild:57-79`, DECISIONS D-176).

### 5.3 Step 10 options (menu "Recognise / count / vitals")

| Symbol | Default | Depends on | Notes |
|---|---|---|---|
| `CSI_VITALS_ENABLE` | y | - | feature switch |
| `CSI_VITALS_WINDOW_S` | 30 (16-51) | - | also sets `CSI_CARRY_US` |
| `CSI_VITALS_MIN_S` | 20 (10-51) | - | |
| `CSI_VITALS_BR_CONF_X100` | 400 | - | also gates the heart rate and the breathing hold |
| `CSI_VITALS_HR_CONF_X100` | 500 | - | |
| `CSI_VITALS_HOLDS_PRESENCE` | y | `CSI_VITALS_ENABLE` | bool |
| `CSI_VITALS_MOTION_RATIO_X100` | 400 (150-5000) | `CSI_VITALS_ENABLE` | the gate threshold; 4.0 is hard-coded when vitals is off |
| `CSI_ID_ENABLE` | y | - | feature switch |
| `CSI_ID_ENROLL_WINDOWS` | 30 | - | |
| `CSI_ID_MIN_WINDOWS` | 10 | - | clamped to `CSI_ID_ENROLL_WINDOWS` in code |
| `CSI_ID_REJECT_X100` | 250 | - | |
| `CSI_ID_MARGIN_X100` | 115 | - | |
| `CSI_VITALS_LOG_SAVE_S` | 300 (60-86400) | - | used only when ID and vitals are both on |
| `CSI_COUNT_ENABLE` | y | - | feature switch |
| `CSI_COUNT_TRAIN_WINDOWS` | 20 | - | |
| `CSI_CONSOLE_ENABLE` | y | - | also needs the USB-Serial-JTAG console (below) |
| `CSI_EXPLAIN` | y | - | `EXPLAIN` lines and `why:` log lines (§3.7); the console command `explain` reprints the `config` event |
| `CSI_EXPLAIN_AMP_HZ` | 5 (0-20) | - | rate of the `amp` line; 0 = off |
| `CSI_ACTIVITY_ENABLE` | y | `CSI_VITALS_ENABLE \|\| CSI_ID_ENABLE \|\| CSI_COUNT_ENABLE` | bool, 2026-10-16: the activity state and presence episodes (D-172); its `depends on` is the Kconfig form of `CSI_HAVE_GATE` |
| `CSI_ACTIVITY_RESTLESS_X100` | 250 (100-5000) | - | a second whose mean `hf` is at or above this (and below the gate threshold) is fidgeting; synthetic starting point |

### 5.4 The "no `depends on` for ints" convention

Step 10's int settings deliberately have no `depends on`. They stay defined
when their feature is switched off, "so the firmware compiles the same way
whichever switches are set" (step10 `Kconfig.projbuild:203-211`).
ESP-IDF's generated `sdkconfig.h` omits a bool that is `n` (for example,
`CONFIG_CSI_LLTF_ONLY` does not appear in Step 10's
`build/config/sdkconfig.h`, which is why the code tests bools with `#if`, not
`#ifdef`). It likewise omits any symbol whose `depends on` is unmet. An int
with a `depends on` would therefore need its own `#if` at every use. Under
this rule it does not: `CSI_CARRY_US`, for example, uses
`CONFIG_CSI_VITALS_WINDOW_S` unconditionally (`main.c:221`).

The exceptions are deliberate:

* `CSI_VITALS_MOTION_RATIO_X100` depends on `CSI_VITALS_ENABLE`. `main.c`
  reads it only under `#if CONFIG_CSI_VITALS_ENABLE` and otherwise uses 4.0
  (`CSI_GATE_RATIO`, `main.c:232-240`).
* `CSI_VITALS_HOLDS_PRESENCE` (a bool) depends on `CSI_VITALS_ENABLE`. Where
  its value is used as data, `main.c` guards it with `#if defined(...) && ...`
  (`main.c:3454`).
* Step 08's `CSI_LED_ACTIVE_LOW` depends on `CSI_LED_GPIO >= 0` and is read
  only inside `#if CONFIG_CSI_LED_GPIO >= 0`.

### 5.5 Derived macros (step10 `main.c:239-344` on 2026-09-28; grep `#define CSI_HAVE_`)

Code and state for a disabled feature are **compiled out**, never left behind
as unused statics. That is how all 552 switch combinations (368 before
2026-11-03, 192 before 2026-10-16) compile with 0 warnings. Note that an unused static would **not** fail the build: the IDF
compiles with `-Werror` but exempts unused-function, unused-variable,
unused-but-set-variable, deprecated-declarations and `-Wextra` warnings
(`-Wno-error=...`, §7). The 0-warnings rule is enforced by counting
`warning:` lines, not by a failing build.

| Macro | Definition | Controls |
|---|---|---|
| `CSI_HAVE_MODELS` | `CSI_ID_ENABLE \|\| CSI_COUNT_ENABLE` | the NCC models, `s_sess`, sessions, `model_load/save` |
| `CSI_HAVE_WHO` | `CSI_ID_ENABLE && CSI_VITALS_ENABLE` | the whose-vitals latch `s_pv`, `vit_log`, `vitals_of` (otherwise always `null`) |
| `CSI_CNT_CARRY` | `COUNT && VITALS && VITALS_HOLDS_PRESENCE` | carrying `s_cnt_est` across a presence lapse |
| `CSI_CARRY_WAIT` | `(CSI_HAVE_WHO && HOLDS_PRESENCE) \|\| CSI_CNT_CARRY` | `s_carry_wait`: hand-back only when the breathing hold asserts PRESENT |
| `CSI_CARRY_US` | `2 * CONFIG_CSI_VITALS_WINDOW_S` s (60 s by default) | the longest absence the latch and the count are carried across |
| `CSI_HAVE_GATE` | `CSI_VITALS_ENABLE \|\| CSI_HAVE_MODELS` | the group map, `s_inv_sigma`, the motion gate `s_gate`, JSON `hf` |
| `CSI_GATE_RATIO` | `CONFIG_CSI_VITALS_MOTION_RATIO_X100 / 100` with vitals on, else `4.0f` | the gate threshold |
| `CSI_HAVE_ACTIVITY` | `CONFIG_CSI_ACTIVITY_ENABLE && CSI_HAVE_GATE` (`main.c:324-331` on 2026-10-16) | `s_act`, `act_push`/`act_reset`/`act_init`, the `activity` / `activity_s` / `episode_s` values (otherwise `null`), console `stats` content, the `status` and `explain` activity lines |
| `CSI_HAVE_GAIN` | `CONFIG_CSI_GAIN_LOG && CONFIG_CSI_JSON_PERIOD_MS > 0` (`main.c:423-428` on 2026-11-03) | `s_gain`, the `csi_raw_t` fields `gain_ok` / `agc` / `fft`, `csi_gain_read` in the callback, `csi_gain_push`, `gain_detect_start` (an empty stub otherwise); without it `json_gain` writes three `null`s |
| `CSI_HAVE_CONSOLE` | `CSI_CONSOLE_ENABLE && CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG && CONFIG_VFS_SUPPORT_IO` | the console task, `s_cmd_q`, driver install. The driver's VFS glue exists only with that console channel, so otherwise the link would fail. |
| `CSI_HAVE_REPORT` | `CSI_JSON_PERIOD_MS > 0 \|\| CSI_HUMAN_READABLE \|\| CSI_HAVE_CONSOLE \|\| (CSI_EXPLAIN && CSI_VITALS_ENABLE)` | `report_t` / `report_build`. **Can be 0**, so latch and log logic must never live only in report code. |
| `CSI_VIT_DONE` | `CSI_VITALS_ENABLE && (CSI_HAVE_WHO \|\| CSI_EXPLAIN)` | `vitals_push()`'s "an analysis completed on this packet" flag, used by the whose-vitals log and the EXPLAIN `vitals` event |
| `ID_MIN_WINDOWS` | `min(CSI_ID_MIN_WINDOWS, CSI_ID_ENROLL_WINDOWS)` (`main.c:498-502` on 2026-09-28) | a slot is usable at this many windows. Without the clamp a fresh enrolment could never be used. |

---

## 6. Step 10 module interfaces

Nine modules sit beside `main.c` (`activity.c` since 2026-10-16, `csi_gain.c` since 2026-11-03). None includes an ESP-IDF header, so the same
files compile on the C5 and on the PC for the host tests. All are always
compiled; a feature switched off simply never calls them, and `--gc-sections`
drops the code (main/CMakeLists.txt:1-4). The C5 has **no FPU** (built
`-march=rv32imac -mabi=ilp32`), so every float operation is a libgcc routine.
Hence: float only, no per-sample `sinf`/`expf`, and FFT-sized work spread so
that no packet pays for more than one FFT (csi_dsp.h:8-16). **Every CPU figure
below is a host operation count or an estimate. None has been timed on the
C5.** The JSON `proc_ms_max` and `dropped` fields exist to measure it.

**`csi_dsp.c/.h` - shared DSP toolbox.** Radix-2 complex FFT (`dsp_fft`, up
to 1024 points), `dsp_detrend`, `dsp_hann`, `dsp_parabolic_offset`,
`dsp_median`. It also defines the subcarrier grouping used everywhere:
`dsp_group_map` splits the selected subcarriers into `CSI_NGROUPS` = 8
contiguous groups once per baseline, and `dsp_group_dev` produces per packet
`grp[g]`, the mean of `(na - fast) * inv_sigma` over the group, i.e. the
deviation from the 0.5 s EMA in empty-room noise units (high-passed at about
0.3 Hz). `dsp_top_eig` is a power iteration for the top eigenvalue and vector
of an 8x8 PSD matrix: about 0.8k operations when it exits in 3-4 steps and at
most about 5.3k for 30 steps, against about 20k for `dsp_sym_eig` (cyclic
Jacobi). `dsp_sym_eig` is not called by the firmware; it is kept as the exact
reference that `test_dsp.c` and `test_count.c` check `dsp_top_eig` against
(csi_dsp.h:74-85, 153-158).

**`csi_ncc.c/.h` - nearest-centroid classifier with rejection.** Used for
both recognition (18 gait features, 4 slots) and counting (6 features,
classes 0 = one mover, 1 = two or more). Inputs: a feature vector. State: the
`ncc_model_t` blob (§4). `ncc_add` does a Welford update and rejects
non-finite input. `ncc_classify(m, x, min_n, reject_rms, margin)` returns
`best` (or -1 for unknown/ambiguous), `nearest`, `dist` and `second`, where
distance is the RMS over features in units of the **pooled** within-class
standard deviation (floored at 1e-4). Recognition passes reject 2.5 and
margin 1.15 from Kconfig. Counting passes reject 1e9 and margin 1, i.e.
always the nearest class (`count_step`, `main.c:1245-1249`). NaN is made to
**reject**, never accept: comparisons are written so that NaN fails them, and
a non-finite distance becomes the finite `NCC_FAR`. Runs once per completed
gait window or count block, never per packet. For the explain lines (§3.7),
`ncc_distances(m, x, min_n, out)` returns the distance to **every** usable
class with the same code and the same pooled std, so `out[nearest]` equals
`dist` and the smallest other equals `second`; an unusable class gets
`NCC_NO_DIST` (-1.0f, never NaN) (csi_ncc.h, `ncc_distances`, added
2026-09-25). Test: `test_ncc.c`.

**`motion_gate.c/.h` - "is anybody moving?" above about 1.6 Hz.** Input per
packet: `t_us`, `grp[8]`, and `room_empty` (Step 08's `!new_present` before
the breathing hold). Output: `still`, and `mgate_ratio()`, the JSON `hf`
(motion energy over its empty-room level, about 1 when nobody moves; -1
means unknown and prints as `null`). Two cascaded first-order high-passes (tau
0.10 s) remove breathing, which Step 08's 0.5 s EMA lets through. The noise
floor comes from seconds in which every packet was room-empty, with a
fallback to the quietest of the last 120 one-second means. MOVING needs the
ratio at or above the threshold for 0.3 s; STILL needs it below for 1 s. An
unknown floor reads MOVING. Known weakness: motion present from the moment
the gate starts can become the floor (motion_gate.h:39-47). Cost about 40
operations per packet; 624 bytes static (linker map, quoted in README
§ "Kconfig — `menuconfig` → *Recognise / count / vitals*", paragraph
**Cost.**).
Test: `test_gate.c`.

**`vitals.c/.h` - breathing and experimental heart rate.** Input per packet:
`t_us`, all normalised amplitudes `s_na`, and the gate's `still`. Twelve
subcarriers spread across the selected set (`vitals_configure`) are binned
by **arrival time** into 100 ms bins (a 10 Hz series, 512-sample ring = 51.2
s). Any movement, or a packet gap of 1.0-1.1 s or more (depending on where
in its bin the last packet fell; vitals.c:375-383 on 2026-11-03, D-175),
drops the buffer; since 2026-11-03 the empty bins of a shorter gap are filled by a straight line from the last
bin's mean toward the value of the packet that ends the gap (D-175,
superseding D-85's "repeat the last value"). Every 2 s, once
`min_s` of still data exists, one analysis transforms **one channel per
packet** (a 512-point FFT) and finishes 12 packets later: detrend, Hann, FFT,
per-channel normalisation to unit band power, equal-weight power sum.
Breathing is the 0.10-0.50 Hz peak, reported at a peak/median of at least 4.0
and not on a band edge. Heart rate is searched in 0.80-2.00 Hz with the 2x-8x
breathing harmonics notched out, only while breathing is confident; it is
reported at 5.0 as the median of the last 5 estimates in this still episode,
under the JSON key `hr_bpm_exp`. Output: `vitals_out_t` (state, `br_*`,
`hr_*`, `still_s`, `updates`), and `vitals_push()` returns true when an
analysis completed. About 6-10 ms per FFT (estimate); 30,984 bytes static
(vitals.h:44-46). Tests: `test_vitals.c`, `test_e2e.c`. Since 2026-10-30
`test_vitals.c` (9)-(12b) also feed it noise, harmonic-rich breathing,
heart-band noise, irregular packet rates and link silences; it reports a
heart rate from a fluctuation shared by all subcarriers, printed as KNOWN
LIMIT lines, and until 2026-11-03 also across 0.3-0.9 s silences (TESTING
§3.3 item 10; DECISIONS D-85, D-86, D-174). Since D-175 that phantom is
much rarer (1 wrong heart value in 332,374 silence analyses over 52 seed
bases), no silence row prints a KNOWN LIMIT line at the shipped seed base,
the silence rows check a rate (at most 2 wrong heart values per 1,000
analyses) and (6b) checks the filled values exactly.

**`gait_id.c/.h` - walking-signature features for recognition.** Input per
packet: `grp[8]` and `moving` (`motion_flag && !still`). A window is 256
packets, one every 128. It yields 18 features only if at least 80% of its
packets were moving: 6 band-power fractions, the spectral centroid, the
cadence and its autocorrelation value, the log variance of the first
principal component (found with `dsp_top_eig`), and the 8 per-group variance
shares. Frequencies come from the **measured** packet rate
(`gait_set_rate`), so enrolment and recognition must use the same ping rate.
Classification happens in `main.c:id_step` through `csi_ncc`, followed by a
7-window vote ring (`id_tally`: a name needs at least 3 votes, strictly more
than "unknown", and no tie). About 58k operations per window at 100 Hz and
73k at 50 Hz by host count, roughly 12-32 ms by estimate; 11,536 bytes
static (gait_id.h:61-67; README § "Kconfig — `menuconfig` → *Recognise /
count / vitals*", paragraph **Cost.**). Test: `test_gait.c`.

**`count_est.c/.h` - one mover versus two or more.** Input per packet:
`grp[8]` and `moving`. It accumulates running sums over non-overlapping
256-packet blocks, so there is no sample ring (under 1 KB). Per block it
yields 6 features: participation ratio (exact), lambda2/lambda1 and
lambda1/trace (two power iterations with deflation and a 2x2 Rayleigh-Ritz
step), log trace, duty (fraction of moving packets) and burstiness.
`main.c:count_step` trains with `count-train 1/2`. Once both classes hold
`CSI_COUNT_TRAIN_WINDOWS` blocks it classifies blocks with duty of at least
0.3 and takes a 5-block majority, where "2" needs a strict majority. Untrained,
`count` is only presence. Per packet 8 adds and 36 multiply-adds; per block
about 6-12k operations by host count (Kconfig help `CSI_COUNT_ENABLE`).
Test: `test_count.c`.

**`person_vitals.c/.h` - whose vitals, and the per-person log.** Input: the
event calls in §3.5 (`pv_packet`, `pv_presence_end`/`start`,
`pv_on_label`, `pv_on_count`, `pv_analysis`, `pv_release`, `pv_slot_gone`,
`pv_log_clear`), with `main.c`'s `who_present(present)` as the `present`
argument of `pv_vitals_of` and `pv_analysis`, so nothing is named or
credited while a lapse's carry waits (§3.5 step 12). Output:
`pv_vitals_of()` (the JSON `vitals_of`),
`pv_multiple()` (forces vitals to "multiple"), and the `pv_log_t` image that
`main.c` saves. The latch goes NONE -> UNKNOWN / PERSON(s) -> MULTIPLE. The
first name of a visit wins, a second name or `count >= 2` gives MULTIPLE, and
MULTIPLE is sticky until release. The log is credited once per completed
analysis, only when present, count <= 1, the latch is PERSON and breathing is
OK. The samples of one still episode are averaged into one value, and
episodes with fewer than 3 samples are discarded. The running mean becomes an
EWMA after 32 episodes. `vitals_of` is a **carried-over claim** (the last
person recognised walking in alone this visit), not a measurement of whose
chest it is (person_vitals.h:6-19). Integer compares per packet and about 10
float operations per analysis; `pv_t` is 304 bytes (person_vitals.h:59-60).
Test: `test_pvitals.c`.

**`activity.c/.h` - activity state and presence episodes (2026-10-16,
DECISIONS D-172).** Reads no CSI: once per detect-phase packet,
`act_push(a, t_us, present, still, hf, held)` takes main.c's post-hold
presence, the gate's decision and ratio, and the breathing-hold flag. It
bins them into 1-second records by timestamp (so 50 and 100 packets/s give
the same states) and keeps the last 10 in a ring. Each second it derives
`empty` (5 s with no PRESENT packet), `moving` (≥ 2 of the last 5 s ACTIVE:
≥ 1/4 of packets gate-MOVING or mean `hf` ≥ the gate threshold),
`restless` (≥ 2 of the last 10 s with mean `hf` in [restless_lo, threshold),
or ACTIVE within the last 5), `still` (10 known quiet seconds), or no state
while PRESENT before the gate's floor is known; a change must hold 2 s, and
a gap over 1 s clears the ring and the state. Presence episodes count whole
seconds per label; a lapse the breathing hold undoes within `carry_us`
(`CSI_CARRY_US`, or 0 without the hold) does not split one. Accessors:
`act_state`, `act_state_s`, `act_state_name` (NULL = JSON null),
`act_episode_s`; `act_reset` ends an episode. It cannot tell sitting from
standing or lying; its thresholds are synthetic (D-83); "still" lasts only
while PRESENT does. A few integer adds and one float add per packet, one
division and a 10-byte loop per second (activity.h); `act_t` is 216 bytes
(`.bss.s_act` in the 2026-10-16 linker map). Tests: `test_activity.c` and
`test_e2e.c` (e4).

**`csi_gain.c/.h` - receiver gain per packet, for logging only (2026-11-03,
DECISIONS D-176).** Reads no amplitude: `csi_gain_read(rx_ctrl, len, &agc,
&fft)` takes byte 23 (uint8, AGC gain) and byte 22 (int8, FFT gain) of a
raw `rx_ctrl` header and returns false for a header of 23 bytes or fewer.
These are reserved bits in ESP-IDF v6.0.2 and v6.1; the positions come
from Espressif's `esp_csi_gain_ctrl` binary, not a documented field, and
have never been seen on silicon (csi_gain.h:12-25). `csi_gain_track_t`
summarises the packets since the last `csi_gain_take`: the last values,
agc lo/hi, `changes` (packets whose (agc, fft) differed from the packet
before, counted across takes, saturating at 65,535) and `packets`;
`csi_gain_take` keeps the last values, so a change across a boundary
still counts. A few compares per packet; nothing the detector decides
reads it. Test: `test_gain.c` (22 checks).

**`main.c` - glue.** The Step 08 pipeline, plus: `analytics_init` (boot),
`analytics_configure` (after a baseline is accepted or saved),
`analytics_reset` and `analytics_on_recal`, `channel_follow`, `count_step`,
`id_step` and `id_tally`, `report_build`, the JSON writer (`json_begin`,
`json_str`, `json_num`, `json_step10`, `json_print`), the console
(`console_install`, `console_task`, `console_handle_line`, `console_drain`,
`cmd_execute` and the `cmd_*` handlers), and the NVS helpers (`model_load`,
`model_save`, `vit_log_load`, `vit_log_save`). **`main.c` is not compiled on
the host.** `test_e2e.c` copies its constants and its per-packet decisions by
hand (test_e2e.c:20-21; constants `:138-168` and `gl_packet` `:1729-1796` on
2026-11-01), so a change to either has to be copied there too.

---

## 7. How to change X safely

Before any change to Step 10: build it
(`& D:\ESP32C5_CSI\build.ps1 step10_recognise_count_vitals`) and run the host
tests (`powershell -ExecutionPolicy Bypass -File
D:\ESP32C5_CSI\step10_recognise_count_vitals\test\run_tests.ps1`). After it,
do both again. Both must be warning-free. The host tests enforce that
themselves (`/W4 /WX`). The firmware does not fully: the IDF builds with
`-Werror` but exempts unused-function, unused-variable,
unused-but-set-variable, deprecated-declarations and `-Wextra` warnings
(`-Wno-error=...` in `build/compile_commands.json`). Those still print as
warnings, so the 0-warnings rule is checked by counting `warning:` lines
(TESTING §2.3) and by `tools/kconfig_matrix.py`, not by a failing build.
`-Wswitch` (§7.2) is still an error. The per-change checklist, including
when the 552-combination switch check (368 before 2026-11-03) is required, is
[TESTING §2.0](TESTING.md#20-before-you-call-a-change-safe).

### 7.1 Add a JSON key (Step 10)

0. **A Step 08 key** goes after `nsub` in Step 08's fixed `printf`
   (step08 `main.c:776-780`); rebuild the root project too, which builds
   Step 08's sources. Step 10 prints Step 08's keys first, so if Step 10 is
   to carry it as well, add it in Step 10 **after the last key** (`gain_chg`, step 2), not beside
   its Step 08 siblings (DECISIONS D-128).
1. Add the field to `report_t` (`main.c:1390-1405`) and fill it in
   `report_build` (`1407-1510`). That keeps the JSON, the bar line and
   `status` consistent.
2. Emit it in `json_step10` (`1943` on 2026-10-16; the other line numbers in this list are older, DECISIONS A-30) **after the last key**, currently
   `gain_chg` (since 2026-11-03: `vitals_of`, then `activity`,
   `activity_s`, `episode_s`, then `agc`, `fft`, `gain_chg`; `json_step10`
   at `2021` and its `json_gain()` call at `2044` on 2026-11-03). **Never insert a key between existing ones and never reorder
   them** (`1588-1598`). `presence_monitor.py` parses by key name
   (`json.loads`, presence_monitor.py:179), but third-party hosts may read by
   position, and CSV column order comes from `CSV_FIELDS`, so old CSV headers
   must still line up.
3. Emit it **in every build**, as `null` when its feature is compiled out, so
   the key set never depends on Kconfig.
4. Use `json_num` for floats (NaN and infinity become `null`) and `json_str`
   for strings. `json_str` does **not escape**, so pass only literals or
   names that passed `name_valid`.
5. Re-check the worst-case line length against the 1024-byte buffer. The
   bound is worked out in the comment above the JSON buffer in `main.c`
   (862 + NUL on 2026-10-16, after the activity keys; 900 + NUL since
   2026-11-03, after the gain keys). An overflowing
   line is not printed. A key that also goes into an EXPLAIN event needs
   that event's bound redone too (`EX_LINE_BOUND` comment; `vitals` 272).
6. Add the key to `CSV_FIELDS` in `tools/presence_monitor.py:106-119`
   (`:124-139` on 2026-11-03), appended at the end, and to `CSV_FIELDS` in
   `tools/dashboard.html` (`:951-954` on 2026-11-03).
   `csv.DictWriter(..., extrasaction="ignore")`
   **silently drops** any key missing from that list (presence_monitor.py:449-450,
   `:485-486` on 2026-11-03).
   If the key should show in the text view, the Step 10 summary is
   `step10_summary()` (presence_monitor.py:203) and the event log (lines
   printed with `***`) is around presence_monitor.py:250-270. Show the key in
   the docstring's example line (presence_monitor.py:16).
   `tools/vitals_validate.py` reads the CSV by column name
   (`csv.DictReader`, vitals_validate.py:164, :277), so an appended column
   does not affect it.
7. Document it in the `main.c` header comment (`89-130`) and in the README JSON
   table (README "Step 10 -> JSON").
8. If it depends on per-packet decisions that `test_e2e.c` mirrors, extend the
   mirror.
9. Step 10 keys appear only on `state:"detect"` lines. Calibration and
   `no_csi` lines are exactly Step 08's (D-128), so the new CSV column is
   empty on those rows.

### 7.2 Add a console command (Step 10)

1. Add a verb to `cmd_verb_t` (`main.c:1647-1657`).
2. Parse it in `console_handle_line` (`2144-2222`), which runs in the
   **console task**. Parse and validate only; touch no shared state. Reply to
   errors there. Never echo unvalidated typed text.
3. Add a `case` to `cmd_execute` (`2056-2087`) calling a new `cmd_<verb>`
   handler, which runs in the **pipeline task** and may change state. The
   switch has **no `default:`** on purpose: `-Wswitch` then fails the build if
   a verb is not handled.
4. Guard the handler with the same `#if` as the feature it touches, and print
   a "disabled" reply in the `#else` branch (see `cmd_count_train`).
5. Start every reply line with `CMD `, and indent continuation lines by at
   least 4 spaces. `presence_monitor.py` forwards only those lines
   (presence_monitor.py:172-175) and drops anything else.
6. If it starts a session, use `session_can_start` (PH_DETECT only, one
   session at a time), and decide what it does to the vote rings, the count
   estimate and the latch. `cmd_enroll` and `cmd_count_train` are the models
   to copy.
7. If it deletes a person's data, save immediately (as `forget` does), and
   use `pv_slot_gone` / `pv_log_clear`, which **discard** a pending episode.
   Committing it would resurrect the deleted data.
8. Update the command lists together: `console_help` (`2091-2106`), the
   `main.c` header (`75-87`), the `CSI_CONSOLE_ENABLE` Kconfig help
   (`Kconfig.projbuild:497-501`) and the README console table. If the
   dashboard should offer it as a button, add it to the console panel of
   `tools/dashboard.html` too.

### 7.3 Add a Kconfig switch

1. Put it in the step's `main/Kconfig.projbuild`. An int setting gets **no
   `depends on`** (§5.4). If you must break that rule, read it only under the
   feature's `#if`, or with `#if defined(CONFIG_X) && CONFIG_X`.
2. Thresholds are integers in hundredths (`_X100`), with a `range` and help
   text that says what raising and lowering it does, and whether the value
   was measured or is a starting point.
3. For a new **feature bool** (Step 10): derive any combination macros next to
   the others (`main.c:239-344` on 2026-09-28), and compile its state and code out entirely
   when it is off. An unused static is a warning. It does **not** fail the
   build (unused-function and unused-variable are exempt from `-Werror`,
   §7), so only a count of `warning:` lines and `tools/kconfig_matrix.py`
   catch it. Every new bool doubles the switch matrix that TESTING.md's
   combination check has to cover, and a new switch must be added to
   `kconfig_matrix.py` by hand. Prefer reusing an existing switch. A bool
   with a `depends on` must be generated only where Kconfig lets it exist:
   `CSI_ACTIVITY_ENABLE` (2026-10-16) is the worked example, 192 → 368
   combinations, not 384 (the script's docstring, THE MATRIX).
   `CSI_GAIN_LOG` (2026-11-03) is the second: it matters only where a JSON
   line exists, so the script varies it only with JSON on, 368 → 552, not
   736.
4. Note that `sdkconfig` is per step and git-ignored. A new option reaches
   existing builds only through `menuconfig` or a fresh `sdkconfig`
   (`-Clean`). Put non-default choices that every user needs in
   `sdkconfig.defaults`.
5. Add the option to the README Kconfig table for that step.

### 7.4 Add a Step 10 analytics module and its host test

1. Create `main/<name>.c/.h` with **no ESP-IDF includes**. Use float only,
   never double. Avoid per-sample libm calls. Keep per-packet work at
   O(nsub) or less, and spread FFT-sized work so that no packet pays for more
   than one. State goes in a struct that `main.c` declares **static**; the
   pipeline stack is 12 KB.
2. Write the header like the existing ones: what it can and cannot do, the
   algorithm, the cost (labelled as a host count or an estimate), the memory,
   and the test that covers it.
3. Follow the finiteness idiom (`(v == v) && fabsf(v) <= FLT_MAX`, or
   comparisons that NaN fails), not `isfinite()`, which promotes to double.
   Reject non-finite input rather than letting it into persistent state.
4. Add the `.c` to `SRCS` in `main/CMakeLists.txt:10-11`.
5. Hook it into `main.c`: `analytics_init` (boot), `analytics_configure`
   (anything that depends on the baseline or the measured rate),
   `analytics_reset` (decide what presence-end and hard resets clear), and the
   per-packet order in `PH_DETECT`. Update the order comment and the mirrors
   in `test_e2e.c` / `test_pvitals.c` (§3.5).
6. Add `test/test_<name>.c` using `test_util.h`, and register it in the
   `$tests` table in `test/run_tests.ps1` and in `test/run_tests.sh`
   (`names` and `mods`) with the module sources it links; if `test_e2e.c`
   calls the module, add the `.c` to its `e2e` entry in both.
   Under MSVC `/WX`: build NaN and infinity from bit patterns (`0x7FC00000`,
   `0x7F800000`, as test_ncc.c:26-27 does), use `f` suffixes on float
   literals, and avoid unused parameters and signed/unsigned comparisons.
7. Test against **synthetic signals with known answers**, and say in the test
   header that a pass does not show it works on a real person.

### 7.5 Change a blob format (NVS)

1. Any change to `csi_baseline_t`, `ncc_model_t` or `pv_log_t` changes the
   stored format, because the struct is written byte for byte.
2. Bump the version constant (`CSI_BASELINE_VERSION`, `NCC_VERSION`,
   `PV_LOG_VERSION`). Every loader rejects a version it does not know, and the
   firmware then starts empty: a missing baseline recalibrates, while models
   and the log must be re-enrolled, retrained or re-accumulated. Say so in
   the release notes, the README and DECISIONS.md.
3. Keep or add a `_Static_assert` on the size, as `person_vitals.c:44-46`
   does, so padding cannot change the format silently.
4. Extend the validator (`ncc_valid` / `model_names_ok`, `pv_log_valid`, or
   the size, magic and version test in `baseline_load`). Make the writer
   produce only records the validator accepts.
5. `csi_baseline.h` exists in **four identical copies** (Steps 06, 07, 08,
   10). Change all four in the same commit, or Steps 07/08 and Step 10 will
   reject each other's baseline.
6. Add a host test that a damaged or old-format blob is rejected, like
   `test_pvitals.c` does for `vit_log`.

### 7.6 Change boilerplate shared by several steps

`wifi_init_sta` and `event_handler` (bring-up and reconnect policy) are copied
into Steps 02-10. `ping_start` and `csi_start` are copied into Steps 03-08 and
10. Step 09 uses `esp_wifi_sensing`'s own router ping, and Step 04's
`csi_start` captures legacy and ACK CSI on purpose. A fix to one copy is a
fix to all of them. Find every copy by function name (`grep` for
`wifi_init_sta`, `event_handler`, `ping_start`, `csi_start` across
`step*/main/main.c`), not by banner: only Steps 05-07 carry an
"unchanged from Step 0N" banner, Steps 03, 04 and 09 have a "same as" /
"identical to" section header, and Steps 02, 08 and 10 have no marker. The
copies are also not byte-identical (formatting, comments, and Step 02 logs
the radio-lock return codes, DECISIONS D-34), so compare function bodies
before assuming a copy is identical. Apply the fix everywhere it applies,
and rebuild every step you touched, calling `build.ps1` by its full path
(TESTING G3).

`METRIC_SMOOTH` must stay identical across Steps 06, 07, 08 and 10, and in
the calibration and detect paths of each, or the stored thresholds stop
matching what is thresholded (step10 `main.c:270-274`). Change it everywhere
or nowhere, then recalibrate.

### 7.7 Change Step 08's detection pipeline

The detector (shape lock, AGC normalisation, EMAs, M/D, `METRIC_SMOOTH`,
thresholds, static branch, hold, slow-EMA freeze, no-CSI watchdog) exists in
three places:

* `step08_final_human_detector/main/main.c`, which the root project also
  builds;
* `step10_recognise_count_vitals/main/main.c`, the same code plus `Step 10:`
  additions (§3, DECISIONS D-76);
* the front-end copy in `step10_recognise_count_vitals/test/test_e2e.c`
  (DECISIONS D-142), which since 2026-10-30 also holds a shadow of the
  static branch (`sh_static()`, `sh_freeze()`, scenario (f)) that feeds
  nothing.

A fourth copy of Step 08's state machine, `replay_main.c` of the
2026-11-01 offline replay, lives outside the repo
(`~/ESP32C5_CSI_logs/2026-10-30_step1/replay_tools/`, ported from step08
`main.c` SHA-256 `3601db0c…`; TESTING §5.3). Nothing keeps it in step:
re-port it, or say which version it ported, before quoting a new replay.

Make the change in Step 08 and Step 10 together. If they must differ, record
why in DECISIONS.md. Mirror it in `test_e2e.c` in the same change, or the
tests go on checking the old behaviour. Steps 05-07 show earlier forms of
these stages; decide whether the tutorial should show the change too. Then
build Step 08, the root and Step 10, and run the host tests
([TESTING §2.0](TESTING.md#20-before-you-call-a-change-safe)). A change to
anything calibration measures (`METRIC_SMOOTH`, sigma, subcarrier selection)
makes stored baselines mean something different: recalibrate (§7.6).

---

## 8. Traps for a new reader

These are verified against the code on 2026-09-25 (the channel bullet on
2026-09-28).

* JSON `nsub` is **`n_selected`**, the subcarriers in use, not the total
  (step08 `main.c:780`, step10 `main.c:3012`).
* Since 2026-09-28 the baseline reuse check compares the Wi-Fi channel
  (DECISIONS D-168): a stored baseline from another channel is recalibrated
  at shape lock, and a channel of 0 on either side counts as not known
  (step08 `main.c:442-444`, step10 `main.c:3476-3478` on 2026-09-28).
  Step 08 checks only at shape lock; Step 10 also recalibrates when
  `channel_follow` adopts a new channel during a calibration, or in
  SETTLE/DETECT unless the stored channel is 0 or is the new one (step10
  `main.c:3541-3546` on 2026-09-28; §3.3).
* `csi_rx_callback` does no amplitude maths. The `-O2` comment in the
  `sdkconfig.defaults` files (for example step03_csi_hello/sdkconfig.defaults:39)
  and REFERENCES.md:768 say the amplitude loop runs inside the Wi-Fi task
  callback. In the code it runs in the pipeline task.
* Step 08's motion flag cannot tell a breathing person from a moving one.
  That is why Step 10 has its own gate (motion_gate.h:5-17). Do not gate
  anything new on `motion_flag` alone.
* `vitals_of` and the per-person log attach a name to a rate by **carrying it
  over** from the walk in. Nothing measures whose chest it is.
* "Unknown" and "multiple" are JSON sentinel values of `person`, which is why
  `enroll` refuses them as names (along with "null").
* The heart rate is `hr_bpm_exp` everywhere, including the per-person log
  (`hr_mean_exp`), because it is unvalidated. Keep the suffix.
* Several comments and docs contain stale statements (for example "this
  board has never been flashed" in `sdkconfig.defaults`). They are listed in
  DECISIONS.md.
