# Decision log

Why the code is the way it is. Each entry records one non-obvious choice:
what was decided, why, what was rejected, the evidence and its date, where
it lives, and whether it still holds. The README is the tutorial;
[REFERENCES](REFERENCES.md) holds the literature and the build-setting tables;
[COMPARISON](COMPARISON.md) compares with prior art. This file is the "why".

**State of the project on 2026-11-03** (this log was started on 2026-09-25; nothing below changed on the board since 2026-09-28. Since then: the Ubuntu side, D-170 and D-171: 10 projects build on v6.1, host tests and the Kconfig matrix pass there, no v6.1 image has run; on 2026-10-16 Step 10's experimental activity state and presence episodes, D-172, built on Ubuntu only (0xf56d0, 0 warnings, never flashed; 368 Kconfig combinations pass on gcc; the MSVC runner was not re-run), plus the research of D-173: v6.1 changes nothing for CSI, and a mmWave radar module is noted as a possible reference sensor, not chosen; and on 2026-10-30/11-01, D-174: a second research sweep, heart-rate failure-mode tests and an AGC scenario in the host tests (test code only: 9 suites, **11,673 checks**, 0 failures, 15 printed KNOWN LIMIT lines, gcc), and the first offline replay of real labelled recordings through a port of Step 08 (another group's ESP32-S3 data, **not** a C5 accuracy figure). Its fixes (F2 gap interpolation, per-packet gain logging, AGC handling) were proposals; nothing under `main/` changed then. On 2026-11-03, on the user's go-ahead, two of them became code: **D-175** (F2: the empty 100 ms bins of a short vitals gap are filled by a straight line toward the packet that ends it, superseding D-85's repeat rule; in the synthetic tests it makes the ~50 bpm phantom heart rate after link silences much rarer, it does not remove it) and **D-176** (the receiver's AGC and FFT gain, read from two undocumented `rx_ctrl` bytes, logged as `agc`, `fft`, `gain_chg` on the detect JSON line; logging only, never seen on silicon). After their review: 10 suites, **11,705 checks**, 0 failures, 12 KNOWN LIMIT lines (gcc), 552 Kconfig combinations clean, Step 10 0xf5a90 with 0 warnings on v6.1, **not flashed**; MSVC not run, Step 08 and the root not rebuilt (sources unchanged). AGC handling and threshold or freeze-cap changes stay open (D-174)).
Builds with a dated record on ESP-IDF v6.0.2: Steps 01, 03 and 04
(2026-09-25), and Step 08, the root project and Step 10 (2026-09-28, after
D-168 and its refinements, 0 warnings each: Step 10 0xef360, Step 08 0xdb340
with `CSI_LLTF_ONLY` = y, root 0xdb350; TESTING §5.2). The board now holds
Step 10's 0xef360 (D-168 and its refinements), flashed on 2026-09-28 at
19:00. The hotspot was off, so it has not joined, and its D-168 channel
check has not run yet (TESTING §5.1, 2026-09-28 19:00). Step 08 and the
root are not flashed. Steps 02, 05, 06, 07 and 09 were last built on
2026-09-07, before their `main.c` changed on 2026-09-22, so their current
sources have no recorded build (D-146; checked from file dates). Step 10's
`main.c` compiles warning-free in 552 Kconfig switch combinations
(2026-11-03; 368 on 2026-10-16), and its 10 host test suites (11,705 checks) pass under gcc on
**synthetic** signals (2026-11-03, Ubuntu, D-175, D-176; 11,673 in 9 suites on 2026-11-01, D-174); MSVC was last run at 8
suites / 11,453 checks and 192 combinations (2026-09-28, Windows). On the
ESP32-C5: Step 01, Steps 03 and 04 on a laptop hotspot (12-bit L-LTF
confirmed, D-160), and Step 10: several boots on 2026-09-25 with restarts
nobody explained, then a 15-minute run at 50 Hz on 2026-09-28 with no
restart (TESTING §5.1). **Step 08 has never run on the board, and no
accuracy of any kind has been measured.** Recognition, counting and heart
rate are experimental and unvalidated, and so is the activity state (D-172). The gain keys (D-176) are a diagnostic whose byte positions come from a vendor library, not from silicon. `tools/session_runner.py` and
`tools/session_score.py` exist to run and score the TESTING §4 sessions
(Phase 1, Phase 2 2b); they were tested offline only (D-169; TESTING §5.2).

---

## How to use this log

**Read before you change something.** Find the area, read the entry's *Why*
and *Rejected* lines, then decide. Several "obvious improvements" were tried
or considered already.

| If you are about to change… | Read |
|---|---|
| `sdkconfig.defaults`, a `CMakeLists.txt`, `build.ps1`, `.vscode/`, the ESP-IDF version | [§1 Build and toolchain](#1-build-and-toolchain) |
| the CSI acquire config, the ping, Wi-Fi reconnect, the CSI callback or queue | [§2 Radio and CSI capture](#2-radio-and-csi-capture) |
| thresholds, EMAs, smoothing, calibration (Steps 05–08) | [§3 Detection DSP](#3-detection-dsp-steps-0508) |
| Step 10's per-packet order, the motion gate, vitals, recognition, counting, the whose-vitals latch, the activity state | [§4 Step 10 analytics](#4-step-10-analytics) |
| anything that reads or writes NVS (baseline, `id_model`, `cnt_model`, `vit_log`) | [§5 Persistence](#5-persistence-nvs) |
| JSON keys, console verbs, `tools/*.py`, LED or button | [§6 Interfaces](#6-interfaces) |
| tests, docs, git | [§7 Testing and process](#7-testing-and-process) |
| flashing, serial ports, the first runs on a board | [§8 Hardware bring-up gotchas](#8-hardware-bring-up-gotchas) |

**Add an entry** when you make a choice a newcomer would not guess, or reject
an alternative someone is likely to propose again. Use the next free number
(numbers are global; put the entry at the end of its area even if the number
is out of order). Keep the six fields. Date the evidence. If nobody recorded
why a value was chosen, write "reason not recorded". Do not guess one.

**Never delete or renumber.** When a decision is replaced, set its status to
`superseded`, name the entry that replaced it, and add the new entry. Other
documents and commit messages may cite D-numbers.

**Status values.**
* `active`: in force, and verified as far as it can be without running on a board.
* `unverified on hardware`: in force, but it depends on runtime behaviour
  that nobody has observed on an ESP32-C5 yet. As of 2026-09-28 the C5 has
  associated, captured L-LTF CSI and run Step 10 (TESTING §5.1); most
  detection, calibration-quality and Step 10 analytics behaviour is still
  unobserved.
* `superseded`: replaced. The entry says by what.

`unverified on hardware` becomes `active` only when a
[TESTING §5.1](TESTING.md#51-hardware) row records the behaviour observed on
the C5; cite that row's date in the entry's Evidence. TESTING §5 is the
record of what has been verified, and says what else to update with it.

**How to read "Where".** Code is cited as `path:line` (repo-relative); the
symbol in brackets is what to grep for when lines move. README sections are
cited by their heading, because README line numbers change often.
`docs/REFERENCES.md` and `docs/COMPARISON.md` are cited by § number.
Exception: §4 cites Step 10 files as `main/…` and `test/…`, relative to
`step10_recognise_count_vitals/`. Line numbers were checked against the
working tree on 2026-09-25; Step 08 citations were re-mapped to the
2026-09-28 working tree after the D-168 refinements, and Step 10 `main.c`
citations can be stale (A-30). Citations marked "on 2026-11-03" (D-175, D-176 and the dated notes of that day) are to that day's working tree.

**Sources outside the repo.** `CLAUDE.md` and `.claude/` are git-ignored on
purpose (D-149), and some facts were first written down only in the
maintainer's local assistant notes or in session scratch notes that are not
kept. When such a note is the only source, the entry says "first tracked
record: this log" and gives the note's date. This file is now their home.

**Known contradictions** between the docs and the code are listed in
[Appendix A](#appendix-a-documentation-known-to-lag-the-code-2026-09-25),
not fixed here. Where they disagree, trust the code and this log.

---

## 1. Build and toolchain

### D-01 Pin every project to ESP-IDF v6.0.2, the install the IDE extension uses
- **Decision:** `build.ps1`, `.vscode/tasks.json` and the Antigravity ESP-IDF extension all use `C:\esp\v6.0.2\esp-idf`, activated through its `export.ps1`.
- **Why:** the extension does not take its ESP-IDF from workspace settings. It takes it from the EIM registry `C:\Espressif\tools\eim_idf.json`, which selects v6.0.2. While the project was pinned to v5.5.1 and the extension ran v6.0.2, one source tree saw two SDKs (different sdkconfig formats, a renamed Wi-Fi enum) and an endless "Python interpreter ... is not from installed venv" warning. Moving the project to match the extension made both agree.
- **Rejected:** keeping the v5.5.1 pin (`C:\Users\rajme\esp\v5.5.1` is on disk but not EIM-registered); v6.0.1 (D-02).
- **Evidence:** extension log quoted in the README section below. Steps 01–09 and the root were clean-rebuilt on v6.0.2 on 2026-09-07.
- **Where:** `build.ps1:24-28` (`$IDF_EXPORT`); `.vscode/tasks.json:16-20`; README § "Everything is on ESP-IDF v6.0.2, deliberately".
- **Status:** active

### D-02 Never select ESP-IDF v6.0.1
- **Decision:** do not switch to v6.0.1, even though EIM registers it.
- **Why:** its `riscv32-esp-elf` toolchain is not installed. `idf.py set-target esp32c5` then fails and the build silently falls back to the xtensa ESP32 target, where the classic CSI struct exists and this code correctly refuses to compile.
- **Rejected:** none recorded.
- **Evidence:** the README says "verified directly" (date not recorded). The dead `C:\esp\v6.0.1\esp-idf` entry was still in the EIM registry on 2026-09-07 (session note).
- **Where:** README § "Everything is on ESP-IDF v6.0.2, deliberately"; `.vscode/tasks.json:22-24`.
- **Status:** active

### D-03 Upgrade ESP-IDF only through the EIM GUI, then retarget everything
- **Decision:** to leave v6.0.2, the user installs v6.1 (or v6.0.3) with the EIM GUI. Then `build.ps1`, `.vscode/tasks.json` and every `.vscode/settings.json` (`idf.currentSetup`, and the paths in `idf.customExtraVars`, D-29) are retargeted, and every project is clean-rebuilt.
- **Why:** the only installer on this machine is a GUI (`C:\Users\rajme\.espressif\eim_gui\eim-gui-windows-x64.exe`; no CLI exists). An IDF that is not in the registry builds from PowerShell but is invisible to the extension (D-28), which then shows a bogus "C compiler is not able to compile a simple test program". The v6.0→v6.1 migration guide lists no Wi-Fi, CSI, esp_wifi or FreeRTOS breaking changes. One C5 known issue applies (D-13).
- **Rejected:** `git clone` + `install.ps1`; downloading a toolchain into the shared `C:\Espressif` tree without asking.
- **Evidence:** release notes and migration guide checked 2026-09-07 (session note).
- **Where:** first tracked record: this log.
- **Status:** active

### D-04 Version shim `CSI_WIFI_BW20` for the v6.0 enum rename
- **Decision:** every Wi-Fi step (02–10) defines `CSI_WIFI_BW20` as `WIFI_BW20` when `ESP_IDF_VERSION_MAJOR >= 6`, else as `WIFI_BW_HT20`.
- **Why:** v6.0 renamed the 20 MHz bandwidth enum. The README calls it "the one v6.0 API break". The shim keeps the steps building on v5.5.x as well.
- **Rejected:** none recorded.
- **Evidence:** compile-verified on v6.0.2 only (2026-09-07; Step 10 2026-09-24/25). The v5.5.x build is claimed through the shim but not re-verified.
- **Where:** `step02_wifi_sta/main/main.c:28-34`; `step03_csi_hello/main/main.c:43-49`; `step10_recognise_count_vitals/main/main.c:149-153`; README § "Everything is on ESP-IDF v6.0.2, deliberately".
- **Status:** active

### D-05 Build from PowerShell through `build.ps1`, never from Git Bash
- **Decision:** `.\build.ps1 <step> [-Flash] [-Port COMn] [-Clean] [-Doctor]` activates v6.0.2's `export.ps1`, sets the target, builds, and optionally flashes and monitors. Git Bash is fine for edits and greps, not for `idf.py`.
- **Why:** ESP-IDF on Windows refuses MSys/MinGW. Sourcing `export.sh` from Git Bash fails with "MSys/Mingw is not supported". The helper also means nobody has to remember the `export.ps1` path.
- **Rejected:** Git Bash + `export.sh`.
- **Evidence:** error text from a session note (date not recorded).
- **Where:** `build.ps1:1-28`; README § "Setting up your shell".
- **Status:** active

### D-06 `build.ps1` finds and checks the serial port before it builds
- **Decision:** `Get-BoardPorts` picks USB serial ports by vendor ID (303A Espressif USB-Serial-JTAG, 10C4 CP210x, 1A86 CH34x, 0403 FTDI) and skips Bluetooth ports. `Resolve-Port` refuses an ambiguous multi-board case and prints a checklist when no board is found. All of this runs before the build. The IDE tasks default the port prompt to `auto`.
- **Why:** a missing board should fail at once, not after minutes of compiling. COM3/COM4 on this machine are "Standard Serial over Bluetooth link" and are never the board; flashing one fails with "The semaphore timeout period has expired". Many CP210x/CH340 entries are registered here, and flashing the wrong port overwrites another project, so confirm with `esptool --port COMn chip_id` (it must report ESP32-C5).
- **Rejected:** none recorded.
- **Evidence:** date not recorded.
- **Where:** `build.ps1:31-143` (`Get-BoardPorts`, `Show-NoBoardHelp`, `Resolve-Port`), `build.ps1:214-223`; `.vscode/tasks.json:56-61`; README § "Find your board's COM port", § "If flashing fails".
- **Status:** active

### D-07 Two Windows PowerShell 5.1 fixes inside `build.ps1`
- **Decision:** `Get-BoardPorts` ends with `return ,$found`. The Doctor's built-steps list filters on `^step\d\d_`.
- **Why:** without the leading comma PowerShell unrolls a one-element array, so with exactly one board (the normal case) `.Count` is `$null` in PS 5.1 and the script reported "no board found". The earlier `step0*` filter silently hid `step10` onwards.
- **Rejected:** plain `return $found`; the `step0*` filter.
- **Evidence:** date not recorded.
- **Where:** `build.ps1:61-66`, `build.ps1:176-180`.
- **Status:** active

### D-08 Run `set-target` only when there is no `sdkconfig`; Step 10 names its target in `sdkconfig.defaults`
- **Decision:** `build.ps1` and the menuconfig task run `idf.py set-target esp32c5` only if `sdkconfig` is absent. Step 10's `sdkconfig.defaults` also sets `CONFIG_IDF_TARGET="esp32c5"`.
- **Why:** with no `sdkconfig` (a fresh clone; it is git-ignored, D-21) ESP-IDF guesses the target from `sdkconfig.defaults` and otherwise falls back to esp32. A menuconfig run before `set-target` writes an esp32 `sdkconfig`, `build.ps1` then skips `set-target`, and the C5-only CSI config does not compile. Steps 01–09 lack the line, so run `build.ps1` once before menuconfig there.
- **Rejected:** none recorded.
- **Evidence:** date not recorded.
- **Where:** `build.ps1:236-240`; `.vscode/tasks.json:127-140` (menuconfig task); `step10_recognise_count_vitals/sdkconfig.defaults:5-11`.
- **Status:** active

### D-09 Every `CMakeLists.txt` stops with a clear message when `IDF_PATH` is wrong
- **Decision:** before `include(project.cmake)`, check that `$ENV{IDF_PATH}/tools/cmake/project.cmake` exists; if not, stop with `message(FATAL_ERROR ...)`. The root's message names both installed ESP-IDF paths; each step's says to run `export.ps1` or use `build.ps1`.
- **Why:** without the guard, a bad `IDF_PATH` makes `include()` fail, CMake carries on, `project()` picks the **host** compiler, and the visible error is a wall of `ld.lld: unable to find library -lkernel32` and "The C compiler ... is not able to compile a simple test program". That looks like a broken toolchain and is not. After such a failure delete `build/`: the poisoned `CMakeCache.txt` has no `CMAKE_TOOLCHAIN_FILE`. If the `-lkernel32` wall ever returns, the guard did not run, so it is a different problem. A lone backslash in a CMake quoted string is an invalid escape (`.\build.ps1` breaks the parse).
- **Rejected:** none recorded.
- **Evidence:** seen 2026-09-07 when the IDE resolved no ESP-IDF setup and fell back to `%USERPROFILE%\esp\esp-idf`; the guard was tested against that bogus path and a good one (session note, 2026-09-07).
- **Where:** `CMakeLists.txt:22-49`; `step01_hello/CMakeLists.txt:2-8` (same block in every step); `step10_recognise_count_vitals/CMakeLists.txt:2-8`.
- **Status:** active

### D-10 The repo root is itself a project that builds Step 08's sources in place
- **Decision:** the root `CMakeLists.txt` sets `EXTRA_COMPONENT_DIRS` to `step08_final_human_detector/main` and builds into `D:\ESP32C5_CSI\build`, separate from each step's `build/`.
- **Why:** opening the plain repo folder and pressing the extension's Build button used to fail with "CMakeLists.txt not found in project directory D:/ESP32C5_CSI / Failed to set target esp32c5". Building Step 08's own files means the root and Step 08 can never drift apart.
- **Rejected:** a second copy of Step 08's code at the root.
- **Evidence:** root rebuilt 2026-09-24: 0 warnings, image 0xdb290 (session note).
- **Where:** `CMakeLists.txt:1-20`; README § "Build the finished detector straight from the repo root".
- **Status:** active

### D-11 Build the app at -O2 (`CONFIG_COMPILER_OPTIMIZATION_PERF`)
- **Decision:** every `sdkconfig.defaults` (all steps and the root) sets `CONFIG_COMPILER_OPTIMIZATION_PERF=y` instead of the ESP-IDF default -Og.
- **Why:** per-packet CSI processing is the hot path and was not optimised at all under -Og. The change reached 960 of 1,111 translation units when counted in `compile_commands.json` on 2026-09-07 (docs/REFERENCES.md §7); that count is not reproducible from today's builds. On 2026-09-25 the `compile_commands.json` files hold 955 entries, 954 of them at -O2 (Steps 01–08 and the root), 958 / 957 (Step 09) and 962 / 961 (Step 10). Two parts of the recorded rationale are disputed: the "~290 packets/s on this chip" figure (A-15) and the claim that the amplitude loop runs inside the Wi-Fi callback (A-16; the callbacks only copy and queue, D-39). The decision stands on the hot-path argument either way.
- **Rejected:** `CONFIG_COMPILER_OPTIMIZATION_DEBUG` (-Og), the previous setting.
- **Evidence:** docs/REFERENCES.md §7 table, 2026-09-07.
- **Where:** `step03_csi_hello/sdkconfig.defaults:29-43` (same block in every project); docs/REFERENCES.md §7; README § "Build settings — this repo builds at -O2".
- **Status:** active

### D-12 Keep the bootloader at -Os
- **Decision:** `CONFIG_BOOTLOADER_COMPILER_OPTIMIZATION_SIZE=y` in every `sdkconfig.defaults`.
- **Why:** at -O2 the bootloader grows to 0x6280 bytes against a 0x6000 ceiling (set by the partition-table offset 0x8000) and the build fails. It would buy nothing anyway: the bootloader runs once, before the app, off the CSI path. At -Os 0x540 bytes (5 %) stay free.
- **Rejected:** `BOOTLOADER_COMPILER_OPTIMIZATION_PERF` ("Bootloader binary size 0x6280 bytes is too large for partition table offset 0x8000").
- **Evidence:** that build failure, 2026-09-07, quoted in the file.
- **Where:** `step03_csi_hello/sdkconfig.defaults:44-50` (same block everywhere); docs/REFERENCES.md §7.
- **Status:** active

### D-13 Do not enable `CONFIG_COMPILER_ENABLE_RISCV_ZCMP`
- **Decision:** leave it off (its default).
- **Why:** it is a code-size extension, not a speed one, and the ESP-IDF v6.1 release notes list the ESP32-C5 as incompatible with `CONFIG_COMPILER_ENABLE_RISCV_ZCMP=y` under Known Issues.
- **Rejected:** enabling it as a "performance" option.
- **Evidence:** v6.1 release notes, read 2026-09-07.
- **Where:** docs/REFERENCES.md §7.
- **Status:** active

### D-14 Flash in QIO mode at 80 MHz
- **Decision:** `CONFIG_ESPTOOLPY_FLASHMODE_QIO=y`, `CONFIG_ESPTOOLPY_FLASHFREQ_80M=y`.
- **Why:** QIO doubles flash read bandwidth for XIP cache misses on the CSI path, and is no flashing risk: the image is still written in DIO (the generated `sdkconfig` keeps `ESPTOOLPY_FLASHMODE="dio"`, esptool_py Kconfig line 81) and the second-stage bootloader switches to QIO at runtime. 80 MHz is the ceiling: the 120M option resolves to `80m` on this target (same Kconfig, line 97).
- **Rejected:** `ESPTOOLPY_FLASHFREQ_120M` (a dead end on the C5); DIO (the previous setting).
- **Evidence:** checked against the Kconfig sources in `C:\esp\v6.0.2\esp-idf`, 2026-09-07.
- **Where:** `step03_csi_hello/sdkconfig.defaults:68-79`; docs/REFERENCES.md §7.
- **Status:** active

### D-15 FreeRTOS tick at 1000 Hz
- **Decision:** `CONFIG_FREERTOS_HZ=1000` everywhere.
- **Why:** at 100–290 CSI packets/s the gap between packets is 3.4–10 ms, and a 10 ms tick rounds every delay and timeout to the size of what is being measured. At 100 Hz the ping also could not be scheduled faster than every 10 ms, which caps CSI near 100/s. Cost: slightly more interrupt overhead.
- **Rejected:** 100 Hz (the previous default).
- **Evidence:** reasoning only, 2026-09-07. Nothing measured.
- **Where:** `step03_csi_hello/sdkconfig.defaults:61-66`; docs/REFERENCES.md §7.
- **Status:** active

### D-16 Assertions stay on until Step 04 runs clean on hardware
- **Decision:** `CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_ENABLE=y`, assertion level 2.
- **Why:** during first bring-up a named assert with file and line beats a silent hang. Switch to `ASSERTIONS_DISABLE` / level 0 only once Step 04 runs clean.
- **Rejected:** disabling assertions now.
- **Evidence:** as of 2026-09-25 only Step 01 has run on a board, so the condition is not met. The comment "This board has never been flashed" in these blocks is now stale (A-5).
- **Where:** `step03_csi_hello/sdkconfig.defaults:52-59`; `step10_recognise_count_vitals/sdkconfig.defaults:63-70`; docs/REFERENCES.md §7.
- **Status:** active

### D-17 Wi-Fi RX buffers 16 static / 32 dynamic, deliberately not raised further
- **Decision:** Steps 03+ set `CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=16` and `CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=32`. Steps 01–02 (no CSI) keep the defaults.
- **Why:** "WiFi + CSI needs more RX buffers than the default." They were not raised further because 16 static buffers already cost about 26 KB of DRAM on a part with no PSRAM, and there was no free-heap measurement to spend against. Raise them only if Step 03's free-heap print shows headroom **and** the CSI rate is short of the 90–290/s bracket. Note (2026-09-25): at the default 20 ms ping the expected rate is about 50/s (D-35), so "short of the 90–290/s bracket" is always true there; read the condition as "short of `1000 / CSI_PING_INTERVAL_MS`". The heap reading this waits for is TESTING §4 Phase 0 check 0c.
- **Rejected:** raising the counts "for performance" without a measurement.
- **Evidence:** the 2026-09-25 reading of 338,196 B free heap came from Step 01, which starts no Wi-Fi, so it is **not** the number this decision waits for. Heap under Wi-Fi + CSI load has never been measured.
- **Where:** `step03_csi_hello/sdkconfig.defaults:16-18`, `:81-91`; `step03_csi_hello/main/main.c:320-327` (free-heap print); docs/REFERENCES.md §7.
- **Status:** unverified on hardware

### D-18 `CONFIG_ESP_WIFI_CSI_ENABLED=y` in every CSI project's defaults
- **Decision:** set it in the tracked `sdkconfig.defaults` of Steps 03–10 and the root.
- **Why:** it defaults to `n`. The code still compiles, but `esp_wifi_set_csi()` then fails at runtime with `ESP_ERR_NOT_SUPPORTED`. If a built project lacks it, rebuild with `build.ps1 <step> -Clean`.
- **Rejected:** none recorded.
- **Evidence:** compile-verified 2026-09-07; never exercised at runtime.
- **Where:** `step03_csi_hello/sdkconfig.defaults:6-9`; `step03_csi_hello/main/main.c:11-13`; README § "The two things that trip everyone up".
- **Status:** active

### D-19 Custom partition table: 2 MB factory app, 24 KB NVS, 4 MB flash
- **Decision:** `partitions.csv` = nvs 0x6000, phy_init 0x1000, factory 2M, with `CONFIG_PARTITION_TABLE_CUSTOM=y` and `CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y` (Steps 03–10 and the root).
- **Why:** the recorded reason is "the default 1 MB gets tight once CSI + the detector + the web UI are all in the image". No web UI exists (it is only a "Where to go next" idea), so that part is aspirational (A-19). The 24 KB NVS size feeds the `vit_log` wear estimate (D-123).
- **Rejected:** the default 1 MB app partition.
- **Evidence:** Step 08 image 0xdb270, 57 % of 2 MB free (build log 2026-09-23); Step 10 image 0xea3d0 = 959,440 B, 54 % free (2026-09-25).
- **Where:** `step03_csi_hello/partitions.csv:1-4` (identical in Steps 03–10 and the root); `step03_csi_hello/sdkconfig.defaults:20-24`.
- **Status:** active

### D-20 Console on the built-in USB-Serial-JTAG, 921600 baud
- **Decision:** `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` with `CONFIG_ESP_CONSOLE_SECONDARY_NONE=y`; Steps 03+ also set `CONFIG_ESP_CONSOLE_UART_BAUDRATE=921600`. The main-task stack is 4096 (Steps 01–02) or 6144 (Steps 03+); reason for those sizes not recorded.
- **Why:** USB-Serial-JTAG works on most C5 devkits. A board that only exposes a UART bridge (CP210x/CH340) must comment out the two USJ lines and clean-rebuild. 921600: "Faster console so high-rate CSI logging does not stall the app."
- **Rejected:** none recorded.
- **Evidence:** Step 01 printed over this console on the C5 at COM13, 2026-09-25 (D-151).
- **Where:** `step01_hello/sdkconfig.defaults:2-7`; `step03_csi_hello/sdkconfig.defaults:2-4`, `:26-27`; README § "Step 01 — Does anything work at all?".
- **Status:** active

### D-21 `sdkconfig` is git-ignored because it holds Wi-Fi credentials
- **Decision:** ignore `sdkconfig` and `sdkconfig.old`; track `sdkconfig.defaults`, which holds no credentials. Every step has its own `sdkconfig`, so the SSID and password are set per step.
- **Why:** once set, `sdkconfig` holds `CONFIG_CSI_WIFI_SSID` and `CONFIG_CSI_WIFI_PASSWORD`. Side effect: `build.ps1 -Clean` deletes `sdkconfig` too, so the credentials must be entered again after it.
- **Rejected:** none recorded.
- **Evidence:** on 2026-09-25 the `sdkconfig` files of Steps 03 and 04 hold the user's laptop-hotspot credentials. On 2026-09-24 the root `sdkconfig` still had placeholder credentials (session note).
- **Where:** `.gitignore:7-15`; `build.ps1:230-233` (`-Clean`).
- **Status:** active

### D-22 Track `dependencies.lock`; do not track `managed_components/`
- **Decision:** `!dependencies.lock` in `.gitignore`, so `step09_espressif_sensing/dependencies.lock` is committed; `managed_components/` stays ignored.
- **Why:** the lock pins `esp_wifi_sensing` 0.1.1~2 and `esp-radar` 0.3.4 exactly. Without it a later 0.4.x resolves silently "and the benchmark stops being a benchmark". It was ignored until 2026-09-07, which was harmless while there were no external components. `managed_components/` is downloaded output.
- **Rejected:** ignoring `dependencies.lock` (the state before 2026-09-07).
- **Evidence:** resolved versions recorded 2026-09-07: also `esp_csi_gain_ctrl` 0.1.5, `esp_radar_motion_dec` 0.1.1, `cmake_utilities` 0.5.3, idf 6.0.2, target esp32c5.
- **Where:** `.gitignore:17-28`; `step09_espressif_sensing/main/idf_component.yml:1-5`; `step09_espressif_sensing/dependencies.lock`; docs/REFERENCES.md §2.1.
- **Status:** active

### D-23 Step 09: fix esp-radar with `-fno-strict-aliasing` on that one component
- **Decision:** `step09_espressif_sensing/CMakeLists.txt` looks up the esp-radar library target and adds `-fno-strict-aliasing` to it as a PRIVATE compile option.
- **Why:** esp-radar 0.3.4 does not compile at -O2: 15 `-Werror=strict-aliasing` errors, reported at line 416 of the downloaded `esp_radar.c`. It really does type-pun: it reads `rx_ctrl->he_siga1` through `htsig`/`vht_siga1` pointer casts to rebuild the classic mcs/cwb/stbc fields that MAC-v3 parts lack. Only silencing the warning would leave the optimiser free to reorder or drop those accesses, which is a real miscompilation risk. `-fno-strict-aliasing` removes the assumption itself. Why it is scoped to the esp-radar target instead of set repo-wide: reason not recorded (`step09_espressif_sensing/CMakeLists.txt:36` says only "Scoped to the one component").
- **Rejected:** `-Wno-error=strict-aliasing`; building the repo at -Og again (Espressif's default, where the error never fires).
- **Evidence:** Step 09 build on v6.0.2 after the move to -O2, 2026-09-07. Verified against esp-radar 0.3.4 only: if `dependencies.lock` is moved to a newer esp-radar (D-22), rebuild Step 09 at -O2 with and without this option before keeping it.
- **Where:** `step09_espressif_sensing/CMakeLists.txt:13-38`; docs/REFERENCES.md §2.1; README § "Step 09 — the benchmark".
- **Status:** active

### D-24 Step 10 feature switches compile out completely
- **Decision:** `main.c` derives guard macros from the Kconfig switches: `CSI_HAVE_MODELS` (ID or COUNT), `CSI_HAVE_WHO` (ID and VITALS), `CSI_CNT_CARRY` (COUNT, VITALS and the breathing hold), `CSI_CARRY_WAIT`, `CSI_HAVE_GATE` (VITALS or MODELS), `CSI_HAVE_CONSOLE`, `CSI_HAVE_REPORT` (JSON, bar line or console; can be 0). A disabled feature's code and state are compiled out, never left as unused statics. Latch and log logic must not live only inside report code, because `CSI_HAVE_REPORT` can be 0. Since the `CSI_EXPLAIN` work (2026-09-25, D-161) there is also `CSI_VIT_DONE` (VITALS and (WHO or EXPLAIN)), `CSI_HAVE_REPORT` also covers EXPLAIN with vitals, and the `fmt_num` guard includes `|| CSI_EXPLAIN` (main.c, 2026-09-28).
- **Why:** every switch must build with 0 warnings both ways. The firmware compiles with `-Wall -Wextra -Werror`, but the IDF exempts unused-function, unused-variable, unused-but-set-variable, deprecated-declarations and `-Wextra` warnings from `-Werror` (`-Wno-error=...`, Step 10 `build/compile_commands.json`, checked 2026-09-25). So an unused static prints a warning without failing the build, and the rule is enforced by counting `warning:` lines (TESTING §2.3) and by `tools/kconfig_matrix.py` (D-145), not by the compiler. Widening the `fmt_num` guard to `CONFIG_CSI_HUMAN_READABLE || (CSI_HAVE_CONSOLE && CSI_HAVE_GATE)` fixed a pre-existing "defined but not used" warning (all three features off, console on, bar line off).
- **Rejected:** none recorded.
- **Evidence:** 96 switch combinations, 0 warnings, 2026-09-25 (D-145). 192 combinations (with `CSI_EXPLAIN`), 0 warnings, 2026-09-28 (D-145, TESTING §5.2). 2026-11-03: `CSI_HAVE_GAIN` (`CONFIG_CSI_GAIN_LOG && CONFIG_CSI_JSON_PERIOD_MS > 0`, `main/main.c:423-428` on 2026-11-03; D-176) compiles the gain tracker, the three `csi_raw_t` gain fields and `gain_detect_start` out (an empty inline stub remains), and `json_gain` then writes three `null`s, so the key set does not depend on the switch; 552 combinations, 0 warnings (D-145, TESTING §5.2).
- **Where:** `step10_recognise_count_vitals/main/main.c:239-344` (guards; grep `#define CSI_HAVE_`, `#define CSI_VIT_DONE`), `fmt_num` (guard at `:1761`, definition at `:1763` on 2026-09-28).
- **Status:** active

### D-25 Step 10 Kconfig: int settings have no `depends on`; per-person vitals got no switch of its own
- **Decision:** Step 10's int settings stay defined whatever the switches are, so `CONFIG_` symbols exist in every combination. The exceptions: `CSI_VITALS_MOTION_RATIO_X100` (int) and `CSI_VITALS_HOLDS_PRESENCE` (bool) depend on VITALS. The ratio is read only under `#if CONFIG_CSI_VITALS_ENABLE` (`main.c:302-306` on 2026-09-28). The hold is tested with plain `#if`, where an undefined macro counts as 0, plus `#if defined(...) && ...` at the one place its value is used as data (`main.c:4560` on 2026-09-28); ARCHITECTURE §5.4. Per-person vitals is simply ID and VITALS together; it added one int, `CSI_VITALS_LOG_SAVE_S` (default 300, range 60–86400, no `depends on`).
- **Why:** the firmware then compiles the same way whichever switches are set. A new bool would double the warning matrix (D-145).
- **Rejected:** a separate enable switch for per-person vitals (2026-09-25 design notes, not kept).
- **Evidence:** Kconfig parsed with ESP-IDF's `esp_kconfiglib` without warnings, 2026-09-25.
- **Where:** `step10_recognise_count_vitals/main/Kconfig.projbuild:203-211`, `:290-293`, `:311-315`, `:428-455`; `step10_recognise_count_vitals/main/main.c:298-306` on 2026-09-28.
- **Status:** active

### D-26 IDE tasks call `build.ps1` / `export.ps1` directly, as `"type": "process"`
- **Decision:** `.vscode/tasks.json` offers Doctor, Build (Ctrl+Shift+B), Build + Flash + Monitor, Monitor, menuconfig, Clean rebuild, Erase flash and the Python tool tasks. Each runs `powershell -NoProfile -ExecutionPolicy Bypass` itself.
- **Why:** the tasks work whatever state the extension is in, and without touching the global EIM registry that the user's other ESP-IDF projects depend on (D-31). `"type": "process"` hands the argument list straight to the executable, avoiding a second round of shell quoting. The user prefers IDE buttons to typed commands.
- **Rejected:** relying on the extension's own Build and Flash buttons.
- **Evidence:** the README says the exact command line each task runs does build. The task dropdowns were not tested inside the running IDE (date not recorded).
- **Where:** `.vscode/tasks.json:1-216`; README § "Just do this".
- **Status:** active

### D-27 A multi-root `.code-workspace` never lists the repo root first
- **Decision:** `ESP32C5_CSI.code-workspace` lists the step folders only.
- **Why:** the extension takes the first workspace folder as *the* project (`this.workspace = workspaceFolders[0].uri` in its code), so folder 0 must always be a buildable project.
- **Rejected:** none recorded.
- **Evidence:** read from the extension's `dist/extension.js` (session note, date not recorded).
- **Where:** `ESP32C5_CSI.code-workspace:2-43`; README § "Which folder to open".
- **Status:** active

### D-28 `idf.currentSetup` is the only setting that selects the extension's ESP-IDF
- **Decision:** keep `idf.currentSetup` exactly equal to a `path` entry of the EIM registry (`C:/esp/v6.0.2/esp-idf`), and let the extension's setup picker write it. `idf.espIdfPath`, `idf.toolsPath`, `idf.pythonBinPath` and `idf.customExtraPaths` (and their `...Win` variants) are kept only as a record and may be deleted.
- **Why:** the extension does `s.find(e => e.idfPath === t)` against the registry, whose Windows default location is hard-coded as `C:\Espressif\tools\eim_idf.json`. The other keys are absent from its schema in 2.1.0 and 2.2.0 and make the project *look* configured when it is not. When no setup resolves, `IDF_PATH` falls back to `%USERPROFILE%\esp\esp-idf` (which does not exist) and the build becomes a host build (D-09). `idf.port = "detect"` is the extension's own sentinel, not a bug. "[Serial port] The "path" argument must be of type string" is a symptom of an unresolved setup. The configure wizard rewrites `settings.json` and strips its comments.
- **Rejected:** hand-editing `idf.currentSetup`; trusting the `~/.espressif/tools/eim_idf.json` copy (the non-Windows branch, a red herring here).
- **Evidence:** verified against the extension's schema and `dist/extension.js`, 2026-08-24 (session note).
- **Where:** `.vscode/settings.json:25-40`, `:73-81`.
- **Status:** active

### D-29 Every project folder sets `IDF_PATH`, `IDF_TOOLS_PATH`, `IDF_PYTHON_ENV_PATH` in `idf.customExtraVars`
- **Decision:** the root and every step's `.vscode/settings.json` carry those three keys (Step 09's settings file was created for this).
- **Why:** the extension auto-updated to v2.3.0 on 2026-09-23. v2.3.0 builds its environment by running the EIM activation script with `-e`, which prints only PATH, ESP_IDF_VERSION, SYSTEM_PATH and four tool variables, never these three. It then overlays `customExtraVars` and finally defaults `IDF_PATH` to `%USERPROFILE%\esp\esp-idf`. The root folder lacked the keys, so Flash failed with "[Flash] Make sure you have the esptool.py installed and set in $PATH". Python is resolved as `IDF_PYTHON_ENV_PATH\Scripts\python.exe`, so that key is needed too. The tell: an environment dump in the ESP-IDF output channel without `IDF_PATH`.
- **Rejected:** relying on `idf.currentSetup` alone (not enough since v2.3.0).
- **Evidence:** extension log `ENOENT ... C:\Users\rajme\esp\esp-idf\...\esptool.py`; fix verified offline (venv python + esptool shim → esptool v5.3.1), 2026-09-24. Needs a window reload.
- **Where:** `.vscode/settings.json:41-67`; `step10_recognise_count_vitals/.vscode/settings.json:7-14`; `step09_espressif_sensing/.vscode/settings.json` (untracked on 2026-09-25).
- **Status:** active

### D-30 When EIM rewrites its registry while the IDE is open, reload the window before changing anything
- **Decision:** first response to a misbehaving extension: reload the window; then compare `idf.currentSetup` with `C:\Espressif\tools\eim_idf.json`; then delete `build/`.
- **Why:** on 2026-09-07 EIM rewrote the registry (added a `D:\esp\v6.0.2` entry and moved `idfSelectedId`). The extension's stale in-memory copy resolved nothing and the IDE build produced the `-lkernel32` wall. Reloading fixed it with `settings.json` unchanged. The extension uses `idf.currentSetup`, not `idfSelectedId`. EIM leaves `eim_idf.json.bak-<date>` beside the file, which is how to spot a rewrite.
- **Rejected:** following EIM's new `idfSelectedId`; blaming the dead v6.0.1 entry (both ruled out).
- **Evidence:** incident at 12:15 on 2026-09-07 (session note).
- **Where:** first tracked record: this log.
- **Status:** active

### D-31 Never modify the machine-wide ESP-IDF or IDE configuration
- **Decision:** do not edit `C:\Espressif\tools\eim_idf.json`, `C:\Espressif\esp_idf.json` or the global `AppData\Roaming\Antigravity\User\settings.json`. The project configures itself per folder only.
- **Why:** those files serve the user's other ESP-IDF projects (one of them has `...Win` keys pointing at ESP-IDF v5.4.4). `idf.eimIdfJsonPath` is application-scope, so changing it hits every project.
- **Rejected:** none recorded.
- **Evidence:** session note (date not recorded).
- **Where:** `.vscode/tasks.json:26-29`; README § "Everything is on ESP-IDF v6.0.2, deliberately".
- **Status:** active

### D-32 IntelliSense through clangd with a `.clangd` per step
- **Decision:** Microsoft C/C++ IntelliSense is disabled. Each step's `.clangd` points at its `build/compile_commands.json` and removes GCC/RISC-V flags that clang rejects (`-fno-tree-switch-conversion`, `-fstrict-volatile-bitfields`, `-fno-shrink-wrap`, `-mno-*`, `-march=*`, `-mabi=*`).
- **Why:** the IDE ships clangd, not cpptools. `compile_commands.json` carries the exact esp32c5 defines that decide which CSI struct you see. Build a step once and code navigation works.
- **Rejected:** Microsoft cpptools.
- **Evidence:** `compile_commands.json` exists for Steps 01–10 (README verification list; date not recorded).
- **Where:** `step10_recognise_count_vitals/.clangd:1-16`; `.vscode/settings.json:18-24`; README § "IntelliSense".
- **Status:** active

---

## 2. Radio and CSI capture

### D-33 Use the C5's own CSI API; do not port classic ESP32 tutorials
- **Decision:** configure CSI through `wifi_csi_config_t`, which on the C5 is an alias of the bitfield struct `wifi_csi_acquire_config_t` (`.acquire_csi_legacy`, `_ht20`, `_su`, `_mu`, `.val_scale_cfg`, `.dump_ack_en`, …). Read the PHY type from `rx_ctrl.cur_bb_format`.
- **Why:** the C5 is a Wi-Fi 6, MAC version 3 part. The classic fields (`lltf_en`, `htltf_en`, `stbc_htltf2_en`, `ltf_merge_en`, `channel_filter_en`, `manu_scale`, `shift`; `rx_ctrl.sig_mode`, `mcs`, `cwb`, `stbc`, `ant`, `secondary_channel`) do not exist there and will not compile.
- **Rejected:** classic ESP32 CSI tutorials and their structs.
- **Evidence:** read from ESP-IDF's `esp_wifi_types_native.h` and `esp_wifi_he_types.h` (gated on `CONFIG_SOC_WIFI_MAC_VERSION_NUM == 3`); esp-csi issue #241 lists the same missing fields on the C5 (docs/REFERENCES.md §2.5). Compile-verified. Where Step 04's `cur_bb_format` name table comes from is not recorded.
- **Where:** `step03_csi_hello/main/main.c:27-32`; `step04_csi_geometry/main/main.c:121-135` (format names); README § "Why most ESP32 CSI tutorials will not compile on the C5".
- **Status:** active

### D-34 Lock the radio: 2.4 GHz only, HT20, power save off — after `esp_wifi_start()`
- **Decision:** Step 02 and every later Wi-Fi step call `esp_wifi_set_band_mode(WIFI_BAND_MODE_2G_ONLY)`, `esp_wifi_set_bandwidth(WIFI_IF_STA, CSI_WIFI_BW20)` and `esp_wifi_set_ps(WIFI_PS_NONE)` right after `esp_wifi_start()`. Step 02 also logs an error if the AP's primary channel is above 14 (5 GHz).
- **Why:** the CSI subcarrier count depends on band, bandwidth and PHY. Left alone, the dual-band C5 can roam to 5 GHz or negotiate HE40/HE80, and the CSI array length changes underneath a variance detector; the README calls this "the single most important design decision". Power save is off because a sleeping radio misses frames and its RX timestamps drift; there is no Kconfig symbol for it, so it can only be set at runtime. Before `esp_wifi_start()` these calls return `ESP_ERR_WIFI_NOT_STARTED`. `esp_wifi_set_band_mode()` restarts the radio when the mode changes, so calling it after connect cannot leave the station on 5 GHz.
- **Rejected:** 5 GHz (docs/REFERENCES.md §1.3: re-run Step 04 and check that IQ varies before ever moving there); HT40 (`acquire_csi_ht40 = 0` for the same reason).
- **Evidence:** the band-mode restart was verified by disassembly (session note, 2026-09-22). Only Step 02 logs the return codes of the three calls; Steps 03–10 discard them (reason not recorded). No CSI captured yet.
- **Where:** `step02_wifi_sta/main/main.c:145-160` (lock), `:200-203` (channel check); `step03_csi_hello/main/main.c:272-275`; `step08_final_human_detector/main/main.c:1030-1033`; `step10_recognise_count_vitals/main/main.c:3396-3399`; README § "Why the radio is locked — the single most important design decision".
- **Status:** unverified on hardware

### D-35 CSI traffic comes from pinging the gateway forever, every 20 ms by default
- **Decision:** from the `GOT_IP` handler, `esp_ping` the gateway with `ESP_PING_COUNT_INFINITE` every `CONFIG_CSI_PING_INTERVAL_MS` (default 20 ms ≈ 50 packets/s, range 5–1000).
- **Why:** CSI exists only for frames the chip receives. An idle station receives only beacons (about 10/s), far too slow to see a person move. Every ping reply is a reception. Espressif independently uses the same design (`esp_wifi_sensing`'s internal router ping; esp-csi `csi_recv_router`).
- **Rejected:** beacon-only CSI (the PeterkoCZ91 fork removes the ping traffic, but the ~100 ms beacon interval caps the rate; docs/REFERENCES.md §2.7, noted, not adopted).
- **Evidence:** the rate has never been measured on this board. Expected at the default 20 ms ping: about 50/s, one CSI frame per ping reply (README § "Step 03 — First CSI capture", "What to check": "close to `1000 / CSI_PING_INTERVAL_MS`"). The 90–290 CSI/s bracket is a third-party figure for other traffic patterns (esp-csi-rs, raw injection at 10 ms, station mode; docs/REFERENCES.md §1.2; see A-15 on the 290), not a target for this firmware at its defaults. The README advises pinging another LAN device if the router rate-limits ICMP, but the code always pings the gateway and has no option for another target.
- **Where:** `step03_csi_hello/main/main.c:122-179` (`ping_start`); `step03_csi_hello/main/Kconfig.projbuild:23-31`; README § "Step 03 — First CSI capture"; docs/REFERENCES.md §2.1, §2.4.
- **Status:** unverified on hardware

### D-36 Ping timeout = 2 × the ping interval, never the 1000 ms default
- **Decision:** every project that pings sets `cfg.timeout_ms = CONFIG_CSI_PING_INTERVAL_MS * 2`.
- **Why:** `ESP_PING_DEFAULT_CONFIG()` sets `timeout_ms = 1000`, and the ping loop blocks for the whole timeout when a reply is lost, so one dropped packet would cost a full second with no CSI. Keep this override if you copy the code elsewhere.
- **Rejected:** the 1000 ms default.
- **Evidence:** reasoning; nothing measured.
- **Where:** `step03_csi_hello/main/main.c:152-155`; `step05_amplitude/main/main.c:443`; `step08_final_human_detector/main/main.c:924`; `step10_recognise_count_vitals/main/main.c:3281`.
- **Status:** unverified on hardware

### D-37 Ping session lifecycle: resume or re-aim on `GOT_IP`, pause on disconnect, no `ESP_ERROR_CHECK`
- **Decision:** `ping_start()` runs on every `GOT_IP`. If a session exists for the same gateway it resumes it; if the gateway changed it stops and deletes the old session and creates a new one. `STA_DISCONNECTED` calls `esp_ping_stop()`. Session-creation errors are logged, not `ESP_ERROR_CHECK`ed.
- **Why:** the function must cope with a session that already exists. Pausing while the link is down avoids flooding the console with send errors. `ESP_ERROR_CHECK` inside the event handler would abort and reboot the board on a transient failure. Why `data_size` is 32 and the ping task priority is 2 in Steps 03–08 is not recorded (Step 10: D-47).
- **Rejected:** `ESP_ERROR_CHECK` on `esp_ping_new_session` (an earlier version, per the code comment).
- **Evidence:** date not recorded.
- **Where:** `step03_csi_hello/main/main.c:129-179`, `:190-192`, `:212-214`; `step08_final_human_detector/main/main.c:900-941`, `:951-953`.
- **Status:** unverified on hardware

### D-38 Wi-Fi reconnect: a bounded first join, then retry forever with timer backoff
- **Decision:** the first join makes up to `CONFIG_CSI_WIFI_MAX_RETRY` (default 10) attempts 1 s apart, then sets `WIFI_FAIL_BIT`; `app_main` logs, waits 3 s (5 s in Steps 08 and 10) and calls `esp_restart()`. (Step 07's 5 s delay at `step07_presence/main/main.c:611` is its missing-baseline loop, not the Wi-Fi-fail path, which waits 3 s at `:644`.) After the first successful join the board never gives up: retries back off 1, 2, 4, 8, 16, 30, 30… s through a one-shot `esp_timer` that calls `esp_wifi_connect()`. The ping stops while the link is down and resumes (or re-aims) on `GOT_IP`, where the retry count resets.
- **Why:** a wrong password is reported instead of retried forever. A router reboot or firmware update is off the air for 1–3 minutes (no source cited for that figure), and a board that stops retrying stays offline until someone power-cycles it. A `vTaskDelay` inside the event handler would stall every other event on the default event loop. Why `MAX_RETRY` is 10: not recorded.
- **Rejected:** giving up after `MAX_RETRY` (Steps 02–09 before 2026-09-23); `vTaskDelay` in the handler.
- **Evidence:** found by the 2026-09-22 code review; fixed 2026-09-23 (uncommitted). Not exercised on a board.
- **Where:** `step02_wifi_sta/main/main.c:51-111`; `step03_csi_hello/main/main.c:181-240`; `step08_final_human_detector/main/main.c:943-1000`; `step09_espressif_sensing/main/main.c:64-116`.
- **Status:** unverified on hardware

### D-39 The CSI callback only copies and queues; a worker task does the maths
- **Decision:** from Step 05 on, `csi_rx_callback` checks the packet, copies length, first-word flag, RSSI, channel and buffer (plus an arrival time: `t_ms` in Step 05, `t_us` in Step 10; Steps 06–08 copy none), and calls `xQueueSend(q, &r, 0)`. Step 03's callback only counts packets and records the last length and RSSI; it has no queue. If the queue is full it increments `s_dropped`. No printf, no floating point, no blocking. Amplitude, median, EMAs and metrics run in a worker task (priority 4). The queue holds 10 raw packets in Steps 05–08 (64 metadata records in Step 04); Step 10 uses 16 (D-47).
- **Why:** the callback runs in the Wi-Fi driver task, and Espressif's C5 docs say "do not do lengthy operations in the callback function". Never block the Wi-Fi task: if the consumer is behind, drop. Step 05's larger worker stack is explained ("the worker does floating point and snprintf"); the other queue depths, the worker priority and the stack sizes are not.
- **Rejected:** processing inside the callback.
- **Evidence:** ESP-IDF docs quoted in docs/REFERENCES.md §1.1. The `sdkconfig.defaults` -O2 comment contradicts this (A-16).
- **Where:** `step03_csi_hello/main/main.c:77-90`; `step05_amplitude/main/main.c:103-124`; `step08_final_human_detector/main/main.c:127-144`; docs/REFERENCES.md §1.1.
- **Status:** active

### D-40 Default detector capture: HT-LTF and HE SU/MU only; legacy (11g) and ACK CSI off
- **Decision:** in their default mode Steps 03, 05–08 and 10 set `acquire_csi_ht20 = 1`, `su = 1`, `mu = 1`, `legacy = 0`, `force_lltf = 0`, `dump_ack_en = 0`, plus `ht40 = 0` ("we lock to HT20"), `vht = 0`, `dcm = 0`, `beamformed = 0`, `he_stbc_mode = 2`, `val_scale_cfg = 0`.
- **Why:** on the C5, L-LTF CSI (legacy frames and ACKs) arrives as 12-bit samples in 16-bit words, while HT/HE-LTF CSI is plain int8: two incompatible layouts in one stream (D-41). The code comment says mixing them "gave a ~50/50 two-shape stream in which the lock often picked the 106-byte ACK shape and then decoded it wrongly". Trade-off: a router that answers pings with 11g frames now yields **no CSI at all**. Step 03's rate shows that at once, so flash Step 03 before Step 08; the fallback is `CSI_LLTF_ONLY` (D-42). Why `vht`, `dcm`, `beamformed` = 0, `he_stbc_mode = 2` and `val_scale_cfg = 0`: not recorded (the `val_scale_cfg` comment points to Step 04, which does not explain it).
- **Rejected:** `acquire_csi_legacy = 1` + `dump_ack_en = 1` ("extra CSI from ACK frames"), the configuration before 2026-09-23.
- **Evidence:** the "~50/50" figure has no source, date or capture, and no CSI had been captured on this hardware, so treat it as a 2026-09-22 review finding, not a measurement. Implemented 2026-09-23 (uncommitted). The README still describes the old configuration (A-7).
- **Where:** `step03_csi_hello/main/main.c:19-25`, `:96-113`; `step05_amplitude/main/main.c:382-407`; `step08_final_human_detector/main/main.c:878-891`; `step10_recognise_count_vitals/main/main.c:3242-3246`.
- **Status:** unverified on hardware

### D-41 On the C5, L-LTF CSI is 12-bit samples in 16-bit words: decode it that way
- **Decision:** wherever L-LTF is captured, decode 4 bytes per subcarrier: imaginary = the low 12 bits of the little-endian word `u[0] | u[1] << 8`, sign-extended (`(int16_t)(w << 4) >> 4`); real = the same from `u[2..3]`. Valid bytes = `len & ~3`, so a 106-byte frame gives 104 bytes = 26 subcarriers. `lltf_bit_mode` is left at its 12-bit default.
- **Why:** read as int8 pairs, these bytes give numbers of plausible size that mean nothing. esp-radar decodes C5 L-LTF the same way. `lltf_bit_mode` exists only in newer IDF headers (v5.5.4+ / v6.0.x), and the code comment says "the one public report of setting it saw no change"; that report is not cited anywhere in the repo.
- **Rejected:** int8-pair decoding of C5 L-LTF (what RuView's C5 branch does, D-148); setting `lltf_bit_mode = 1`. ESPectre's route (switching the C5 to 8-bit mode) is described as equally valid but was not used.
- **Evidence:** esp-idf #18118 (Espressif, 2026-01-20: "LLTF uses 12-bit full-precision mode and HT/HE-LTF use 8-bit"), read 2026-09-22. The imag/real word order is unverified; it affects only the sign of the phase, not amplitude (docs/COMPARISON.md §4). First-flash test: in 106-byte frames the odd-index bytes should all have high nibble 0x0 or 0xF (D-43).
- **Where:** `step05_amplitude/main/main.c:125-168`; `step06_baseline/main/main.c:113-144`; `step07_presence/main/main.c:118-135`; `step08_final_human_detector/main/main.c:146-183`; docs/COMPARISON.md §4.
- **Status:** unverified on hardware

### D-42 `CSI_LLTF_ONLY`: an optional one-shape mode, chosen from Step 04's histogram
- **Decision:** a Kconfig bool, default `n`. When on: `legacy = 1`, `force_lltf = 1`, all HT/HE acquisition off, `he_stbc_mode = 0`, `dump_ack_en = 1`, 12-bit decode (D-41). The rule: if one shape holds about 90 % or more in Step 04, leave it off (the auto-lock picks that shape); if traffic fragments across 3 or more shapes, turn it on for Steps 05–08. Steps 05, 06 and 08 log an error recommending it when the locked shape has under 90 % share. It must match between Steps 06 and 07, and toggling it changes the shape, which invalidates a stored baseline (Step 08 then recalibrates).
- **Why:** the L-LTF is in every 802.11 preamble regardless of PHY, so every packet yields the same subcarrier count; ACKs are L-LTF too, so they add frames without adding a second shape. Less resolution, but "a rock-steady stream".
- **Rejected:** none recorded.
- **Evidence:** esp-idf #18118 (on the C61, `force_lltf` fixes nonsensical 11g CSI) and #18493 (`force_lltf` had no effect on the C5 5 GHz frozen-IQ report), docs/REFERENCES.md §1.3–§1.4. Those sections' C5 advice predates D-41 and is stale (A-13).
- **Where:** `step05_amplitude/main/main.c:361-382`; `step05_amplitude/main/Kconfig.projbuild:26-41`; `step08_final_human_detector/main/Kconfig.projbuild:28-34`; README § "👉 The decision this step gives you".
- **Status:** unverified on hardware

### D-43 Step 04 captures every LTF type and measures the shape: never copy its config into a detector
- **Decision:** Step 04 turns on legacy, HT20, SU, MU and ACK CSI. It bins packets by (length, `cur_bb_format`, `first_word_invalid`), up to 16 bins, and every 5 s prints a histogram with share, RSSI range and a packing check. The packing check samples up to 64 odd-index bytes per packet and reports how many have high nibble 0x0, 0xF or anything else; "other" near 0 % means 12-bit words. It prints the first packet decoded both ways and names the dominant shape.
- **Why:** on the C5 the subcarrier count differs for 11g, HT20 and HE20, and the buffer holds the enabled LTFs concatenated. Hard-coding a blog's "64 subcarriers, 128 bytes" gives a detector that runs and detects nothing. The packing check "reports, it does not decide".
- **Rejected:** trusting published constants.
- **Evidence:** not yet run on a board. Known pitfalls: a very weak int8 signal (all |values| < 16) can also read near 0 % "other", so check the RSSI; the histogram's "subcarriers" column always divides by 2, so it over-reports 12-bit shapes (106 B prints 53; the true count is 26), which the footer text warns about.
- **Where:** `step04_csi_geometry/main/main.c:86-100` (packing check), `:105-164` (histogram), `:207-231` (capture config), `:343-347`, `:360-363`; README § "Step 04 — Measure your CSI geometry (do not skip this)".
- **Status:** unverified on hardware

### D-44 What Step 04's output is checked against: six legitimate lengths and a 90–290/s rate
- **Decision:** the legitimate C5 `csi_data_len` values are {106, 114, 228, 234, 468, 490}. A length outside that set is the one to investigate. The rate is checked against 90–290 CSI/s. Correction (2026-09-25): at the default 20 ms ping the expected rate is about 50/s (D-35; README § "Step 03 — First CSI capture"), so this firmware is not expected to reach 90/s at its defaults. The 90–290/s bracket is esp-csi-rs's figure for other traffic patterns (raw injection at 10 ms; a station); TESTING §4 check 0c uses the ping-derived rate instead.
- **Why:** these are the totals in ESP-IDF's C5 subcarrier table: 106, 114 and 490 are the single-stream 20 MHz cases, 228 and 468 the two-stream STBC ones, 234 the HT40 one (HT40 acquisition is off here, D-34). STBC and HE frames are acquired (`he_stbc_mode = 2`, `su`/`mu` = 1), so 228, 468 and 490 are expected outputs, not faults.
- **Rejected:** none recorded.
- **Evidence:** ESP-IDF's esp32c5 CSI docs page (read 2026-09-07); esp-csi-rs measurements on C5/C6 (docs/REFERENCES.md §1.2). docs/REFERENCES.md §1.1 derives 106 as "1 × 53" int8 pairs, which predates D-41 (A-12); the set of lengths stands.
- **Where:** docs/REFERENCES.md §1.1, §1.2, §6 item 1; README § "👉 The decision this step gives you".
- **Status:** unverified on hardware

### D-45 Skip the first 4 bytes when `first_word_invalid` is set
- **Decision:** when `info->first_word_invalid` is set, start decoding 4 bytes into the buffer and leave those bytes out of every statistic.
- **Why:** "bytes 0..3 are invalid due to a hardware limitation" (the README calls it a documented hardware limitation). That is one 12-bit subcarrier or two int8 ones. Averaging them in corrupts the result.
- **Rejected:** none recorded.
- **Evidence:** the limitation's source document is not cited in the repo.
- **Where:** `step05_amplitude/main/main.c:25-27`, `:146-154`; README § "Reading the raw layout".
- **Status:** unverified on hardware

### D-46 Cap CSI buffers at 768 bytes (384 subcarriers); drop longer packets
- **Decision:** `MAX_CSI_BYTES` / `CSI_MAX_CSI_BYTES` = 768. The callback drops anything longer.
- **Why:** "HE20 with several LTFs concatenated is the worst case; anything longer is dropped rather than truncated." The same bound sizes the baseline arrays.
- **Rejected:** truncating oversize buffers.
- **Evidence:** the largest expected length is 490 B (docs/REFERENCES.md §1.1).
- **Where:** `step05_amplitude/main/main.c:74-77`, `:108`; `step06_baseline/main/csi_baseline.h:13-14`.
- **Status:** active

### D-47 Step 10: ping task priority 5, above the pipeline (4); CSI queue 16 deep
- **Decision:** Step 10 sets the esp_ping `task_prio = 5`, the pipeline task 4, the console task 1, and `CSI_QUEUE_LEN = 16` (Steps 03–08: ping priority 2; raw queue 10 in Steps 05–08; Step 03 has no queue and Step 04 a 64-deep metadata queue, D-39).
- **Why:** esp_ping keeps one echo in flight (send, block for the reply, `vTaskDelayUntil`). A ping task below the pipeline stops sending while a gait window or count block runs: the stream pauses instead of queueing, evenly spaced samples arrive as a gap and a burst, and `dropped` can never count an overload. Above it, pings keep their cadence and the queue absorbs the stall: 16 slots cover 320 ms at 50 Hz (160 ms at 100 Hz) for about 4.7 KB more heap. The console runs at 1 so typing can never cost a CSI packet.
- **Rejected:** a ping task below the pipeline (Step 08's 2).
- **Evidence:** not measured; the JSON fields `proc_ms_max` and `dropped` exist to measure it (D-132). The combined cost of a gait window, a count block and a vitals FFT on one packet has never been measured on the chip.
- **Where:** `step10_recognise_count_vitals/main/main.c:275-282` (`CSI_QUEUE_LEN`), `:3283-3292` (ping priority), `:3561-3566` (task priorities).
- **Status:** unverified on hardware

### D-48 Step 10: default ping stays 20 ms; raise it to 10 ms before recognising anyone; the measured rate drives everything
- **Decision:** the default `CSI_PING_INTERVAL_MS` stays 20. The help text recommends 10 ms for recognition, but only after Step 03 shows enough free heap and a router that answers that fast, followed by `recal`. Gait, EMAs and the gate use the packet rate **measured** at calibration and stored in the baseline. The settle phase, and a check every 10 s while detecting, warn once when the live rate is more than 20 % off. Changing the ping interval means `recal`, re-enrolling everyone and redoing `count-train`.
- **Why:** gait features look at motion up to about 16 Hz and beyond, which needs at least 100 packets/s (at 50 Hz everything above 25 Hz is lost). A stored baseline does not notice that the ping interval changed, and templates enrolled at one rate do not match windows measured at another.
- **Rejected:** none recorded.
- **Evidence:** host tests ran recognition at 100 Hz and counting and vitals at 50 Hz. In the 2026-09-25 seed sweep, scenario e1 passed 7 of 9 rooms at both 50 and 100 Hz, failing in different rooms, so a higher rate is not a proven fix (D-144). The 100 Hz half is a session measurement: scenario (e) is hard-coded to 50 Hz (`fe_calibrate(50.0f)`, `step10_recognise_count_vitals/test/test_e2e.c:2453` on 2026-11-01; `:1941` when written), so it cannot be re-run from the repo as it is (TESTING §3.3 item 2).
- **Where:** `step10_recognise_count_vitals/main/Kconfig.projbuild:17-38`; `step10_recognise_count_vitals/main/main.c:3144-3185` (live-rate check); `step10_recognise_count_vitals/main/gait_id.h:21-23`; README § "Before recognising anyone: raise the packet rate".
- **Status:** unverified on hardware

### D-49 Step 09 is a separate benchmark project running Espressif's `esp_wifi_sensing`
- **Decision:** `step09_espressif_sensing` contains none of this project's detection code. It runs Espressif's `esp_wifi_sensing` FSM (on top of esp-radar) on the same board, router and room, and is flashed alternately with Step 08. It is the repo's only external dependency.
- **Why:** `esp_wifi_set_csi_rx_cb()` has one callback slot and the last writer wins; `esp_wifi_sensing` registers its own. Linking both into one firmware would silently disable whichever registered first, a bug that "looks like the detector stopped working and has no visible cause".
- **Rejected:** linking `esp_wifi_sensing` into Step 08.
- **Evidence:** built on v6.0.2, 2026-09-07. Never run on a board. The `esp_wifi_sensing` core ships as a prebuilt `.a` per IDF version and target (docs/COMPARISON.md §4).
- **Where:** `step09_espressif_sensing/main/main.c:1-24`; `step09_espressif_sensing/main/idf_component.yml:1-5`; README § "Step 09 — the benchmark (optional, but do it before tuning)"; docs/REFERENCES.md §6 item 8.
- **Status:** unverified on hardware

### D-50 Step 09 wiring: sensing peer = the AP's BSSID; `CTRL_START` before the router ping
- **Decision:** after association: create the FSM with the default config, add a channel for `ap.bssid`, register the ACTIVE/INACTIVE callbacks, call `esp_wifi_sensing_fsm_control(CTRL_START)`, then start the component's own router ping. The radio lock is the same as in the other steps (D-34).
- **Why:** the sensing channel is identified by the peer's MAC, which in the router-ping topology is the AP itself. Without `CTRL_START` the FSM task only sleeps: `create()` leaves it stopped and it never processes a sample. This is workflow step 4 in `esp_wifi_sensing.h`, and the component's own test app calls it at this point, before the router ping.
- **Rejected:** none recorded.
- **Evidence:** the missing `CTRL_START` was found by the 2026-09-22 review and fixed (uncommitted). Header and test-app lines checked 2026-09-25 in `managed_components/` (downloaded, untracked). The fix has not been compiled: Step 09's last build is 2026-09-07 (D-146).
- **Where:** `step09_espressif_sensing/main/main.c:191-226`; `step09_espressif_sensing/managed_components/espressif__esp_wifi_sensing/include/esp_wifi_sensing.h` (untracked).
- **Status:** unverified on hardware

### D-51 Step 09 trains on site at every boot; a failed `train_stop` is not fatal
- **Decision:** wait `TRAIN_LEAVE_S` = 10 s, `train_start`, wait `TRAIN_S` = 30 s, `train_stop`. A failure is logged and the benchmark keeps measuring motion. Then one diagnostic line per second: state, jitter, wander, smooth, enter/exit levels, init stage, someone, trained, heap.
- **Why:** presence (`wander_value`, `presence_someone`) stays 0 until training, while motion (jitter, ACTIVE/INACTIVE) runs without it. The library does not persist training, so it repeats every boot and you must leave the room each time. `train_stop` returns `ESP_ERR_INVALID_STATE` when too few samples arrived, and "a benchmark that boot-loops on a quiet link is worse than one that reports untrained". 30 s at the default ping rate is "a few hundred samples"; the component README asks for a static environment.
- **Rejected:** `ESP_ERROR_CHECK` on `train_stop`.
- **Evidence:** the missing training was found by the 2026-09-22 review and fixed (uncommitted). The fix has not been compiled: Step 09's last build is 2026-09-07 (D-146).
- **Where:** `step09_espressif_sensing/main/main.c:53-57`, `:228-287`.
- **Status:** unverified on hardware

### D-52 Wi-Fi Aware / NAN is rejected for the sensing core; ESP-NOW is the router-free path
- **Decision:** do not use Espressif's `wifi_aware` / NAN. A router-free CSI source should be a second, stationary ESP32 sending ESP-NOW, as Espressif's esp-csi `csi_send` / `csi_recv` do (pinned channel, `esp_now_set_peer_rate_config()`).
- **Why:** `esp_wifi_nan_sync_start()` unconditionally calls `esp_wifi_set_mode(WIFI_MODE_NAN)` (ESP-IDF v6.0.2, `components/esp_wifi/wifi_apps/nan_app/src/nan_app.c` line 828, re-checked 2026-09-25), and there is no STA+NAN mode in v6.0.2, v6.1 or master. Entering NAN drops the AP association and kills the gateway-ping CSI stream. The NAN peer would be a phone, an uncontrolled moving transmitter that destroys the fixed-link baseline. Whether `csi_rx_cb` fires in NAN mode is undocumented.
- **Rejected:** NAN-Sync; NAN-USD (discovery only, behind `IDF_EXPERIMENTAL_FEATURES`, compiled out of `wifi_aware`).
- **Evidence:** ESP-IDF source reading, 2026-09-04 (session note).
- **Where:** first tracked record: this log.
- **Status:** active

### D-53 A second board as a fixed ESP-NOW transmitter: planned, and its rate must be measured on day one
- **Decision:** not implemented. The plan is to use the second C5 as a dedicated sender for a steadier link (about 100 Hz) and as a step towards multi-link counting, starting from Espressif's `csi_send` configuration and measuring the received rate first.
- **Why:** a fixed, known transmitter removes the dependence on the router's behaviour. One ESP-NOW implementation (PeterkoCZ91, on a C6) measured about 8 packets/s against 206.5 over UDP. That describes that implementation, but a clean 8 Hz signal is worse than a noisy 200 Hz one.
- **Rejected:** a hand-rolled ESP-NOW loop.
- **Evidence:** docs/REFERENCES.md §1.5; esp-csi-rs measured about 90 CSI/s from raw injection on C5/C6 (§1.2).
- **Where:** README § "Where to go next"; docs/REFERENCES.md §1.5.
- **Status:** unverified on hardware

### D-54 BLE identity would time-share the one RF path
- **Decision:** not implemented. If a phone/watch BLE identity is added, measure the CSI rate with BLE scanning on before relying on both.
- **Why:** a BLE identity works while people sit still, which gait recognition never will. But the C5 has one RF path shared by Wi-Fi and BLE through time division, so every millisecond spent scanning is CSI not received.
- **Rejected:** none recorded.
- **Evidence:** ESP-IDF esp32c5 RF coexistence guide, read 2026-09-24 (docs/REFERENCES.md §4.1).
- **Where:** README § "Where to go next"; docs/REFERENCES.md §4.1.
- **Status:** active

### D-176 Log the receiver gain per packet (`rx_ctrl` bytes 23 and 22) on the detect JSON line; logging only
- **Decision:** (2026-11-03, D-174 proposal (2), on the user's go-ahead) Step 10's CSI callback reads byte 23 (uint8, the AGC gain) and byte 22 (int8, the FFT gain) of each packet's `rx_ctrl` header with `csi_gain_read(&info->rx_ctrl, sizeof(info->rx_ctrl), &agc, &fft)`, which refuses a NULL or a header of 23 bytes or fewer (`main/csi_gain.c`, no ESP-IDF headers, host-tested). The pipeline pushes every dequeued packet's values, in every phase, into one `csi_gain_track_t` right after `t_work0`. Each **detect** JSON line takes the summary and appends `"agc"` and `"fft"` (the last packet's values since the previous detect line) and `"gain_chg"` (packets whose (agc, fft) differed from the packet before, counted across line boundaries, saturating at 65,535) after `episode_s` (D-128); all three are `null` without the switch or with no packet since the previous line. Calibration and `no_csi` lines stay exactly Step 08's. At each switch to detection (boot or `recal`; both `phase = PH_DETECT` sites) `gain_detect_start()` logs the calibration's packets once as `gain before detection: N packets, agc lo-hi, C[+] changes (agc A fft F last)` (`+` when saturated) and starts a new count; the last values are kept, so a change across the switch still counts on the first detect line. Kconfig `CSI_GAIN_LOG` (bool, default y, menu "CSI capture", prompt "Log the receiver gain on each detect JSON line (agc, fft, gain_chg)"); `CSI_HAVE_GAIN` = `CONFIG_CSI_GAIN_LOG && CONFIG_CSI_JSON_PERIOD_MS > 0`. **Nothing the detector decides reads it**: it is no input to the front end, presence, the gate, vitals, recognition, counting or the activity state, so `test_e2e`'s `gl_packet` mirror has no gain step. Host side: the three keys in `CSV_FIELDS` of `presence_monitor.py` and `dashboard.html`; `session_score.py` (version 5) prints a GAIN block with no mark (D-169). Step 08 is unchanged.
- **Why:** `test_e2e` (f) showed, with **assumed** magnitudes, that gain steps through compression or a gain-state-dependent response give false static PRESENT and empty-room breathing (TESTING §3.3 item 11), and every AGC fix D-174 lists needs the real step sizes first. ESP-IDF documents no gain field: in v6.0.2 and v6.1 `esp_wifi_he_types.h`, the layout for `SOC_WIFI_MAC_VERSION_NUM == 3` (the C5; esp32c5 `soc_caps.h:630`) has `noise_floor` at byte 20, bytes 21-25 reserved, `channel` at byte 28, `second` at byte 29 and size 64; a compiled check (`off.c` / `off.s`) stores `channel` at struct offset 28 with size 64. Espressif's `esp_csi_gain_ctrl` v0.1.5 (Apache-2.0, in Step 09's managed components) disassembles, in its 6.0 libraries for esp32c5, esp32c6 and esp32c61, `esp_csi_gain_ctrl_get_rx_gain` as `lbu a5,23(a0)` → agc and `lb a5,22(a0)` → fft. So the byte positions follow a vendor binary, not a documented field. The board logged channel 1 (2026-09-25) and 6 (2026-09-28) from `rx_ctrl.channel` (TESTING §5.1), consistent with `channel` at byte 28.
- **Rejected:** (a) **Linking `esp_csi_gain_ctrl`** and calling its `esp_csi_gain_ctrl_get_rx_gain`: it ships libraries for IDF 4.4, 5.0-5.5 and 6.0, none for 6.1 (D-173; `step09_espressif_sensing/managed_components/espressif__esp_csi_gain_ctrl/`, version folders `4.4/`, `5.0/`-`5.5/` and `6.0/` at the component root; the C5 library is `6.0/esp32c5/`), and the Ubuntu side builds on v6.1 (D-170); the module needs only the two byte positions. (b) **Gain compensation or any other AGC handling now** (D-174 (3)): there is no real gain data yet; this entry exists to collect it. (c) **Forcing the gain** (`esp_csi_gain_ctrl_set_rx_force_gain`, "may cause packet loss"): the same reason, and it would change what the detector sees. (d) **Counting the first detect line from the last JSON line** (the first pass): the calibration's packets were pushed too and no calibration line takes them, so the first detect line reported the whole calibration (3 changes over 3,225 packets in a hand mirror of `main.c`'s order); the count now restarts at detection (review defect 3).
- **Evidence:** host and build only, 2026-11-03 (TESTING §5.2 rows of that date; Ubuntu, ESP-IDF v6.1). `test_gain` 22 checks (bytes 22 and 23 read, byte 28 not; a short header refused; the change count, lo/hi and saturation; 2,000 random headers and 200 random gain walks). In a hand mirror of the new order (`main.c` is not host-compiled), the first detect line reports `gain_chg` 0 over 25 packets where the first pass reported 3 over 3,225 (`~/ESP32C5_CSI_logs/2026-11-03_impl/after_review/calgain/`). Kconfig matrix 552 combinations, 0 warnings (D-145). Step 10 0xf59b0 on the first pass, **0xf5a90** after the review, 0 `warning:` lines, 52 % free, the `gain before detection` string in the `.bin`; **not flashed**. `session_score.py` on synthetic logs and on the recorded logs (D-169). Byte evidence: `~/ESP32C5_CSI_logs/2026-11-03_impl/gain_bytes/` (both `esp_wifi_he_types.h` versions, the three disassemblies, `off.c` / `off.s`). Review: `~/ESP32C5_CSI_logs/2026-11-03_impl/after_review/REVIEW_RECORD.md`, defects 3, 4, 6-12, all fixed. **Never seen on silicon.**
- **Where:** (this entry is in §2, so: `main/` and `test/` are relative to `step10_recognise_count_vitals/`) `main/csi_gain.h` (where the bytes come from; first-capture check `:22-25`), `main/csi_gain.c`; `main/main.c` (on 2026-11-03): JSON header `:113`, `:158-171`, `CSI_HAVE_GAIN` `:423-428`, `csi_raw_t` gain fields `:441-445`, `s_gain` and `gain_detect_start` `:496-518`, `csi_gain_read` in `csi_rx_callback` `:695-697`, JSON bound comment `:1890-1898`, `json_gain` `:1986-2006`, its call in `json_step10` `:2044`, `csi_gain_push` `:3694-3698`, `gain_detect_start()` at `:4010` and `:4032`; `main/Kconfig.projbuild:57-79`; `main/CMakeLists.txt`; `test/test_gain.c`; `test/run_tests.sh:62`, `:74-75`; `test/run_tests.ps1:49-50`; `tools/kconfig_matrix.py:47-69`; `tools/presence_monitor.py:44-51`, `:138`; `tools/dashboard.html:954`; `tools/session_score.py` (GAIN; `gain_count` `:828`, `gain_figures` `:835`, `gain_pool` `:854`); TESTING §3.2, §3.3 item 11, §4 (*Keep for every session*).
- **Status:** built, **unverified on hardware**. First-capture sanity check (`csi_gain.h:22-25`, TESTING §4): `agc` should move with distance or RSSI and stay nearly constant on a still link; a value that never changes, or one that equals the channel, means the bytes are not the gain, and the keys must then be ignored.

---

## 3. Detection DSP (Steps 05–08)

Step 10 runs this pipeline unchanged (D-76), so everything here applies to it too.

### D-55 Auto-lock to the dominant CSI shape
- **Decision:** observe the first `CONFIG_CSI_LOCK_PACKETS` packets (default 200, range 20–2000), tally them by length only (up to 12 shapes), lock onto the most common length and reject every other length from then on. Log an error if the dominant share is below 90 %. `first_word_invalid` and channel are taken from the packet that completes the lock.
- **Why:** consecutive packets can carry different subcarrier counts. "Averaging across shapes is meaningless"; "mixing lengths in one statistic is the classic way to get a detector that never triggers". The 90 % figure mirrors Step 04's rule (D-42). Why 200 packets, and why the flags come from the last packet: not recorded.
- **Rejected:** none recorded.
- **Evidence:** not run on a board.
- **Where:** `step05_amplitude/main/main.c:193-253`, `:278-291`; `step05_amplitude/main/Kconfig.projbuild:52-60`; `step08_final_human_detector/main/main.c:294-331`, `:421-477`.
- **Status:** unverified on hardware

### D-56 Divide each packet by its own median amplitude (AGC normalisation)
- **Decision:** compute each subcarrier's amplitude, take the median over all subcarriers of the locked shape (clamped to at least 1.0 against an all-zero packet), and set `na[k] = amp[k] / median`. The median uses an insertion sort into a static scratch array. Step 05 logs the divisor once per second (average, min, max, max/min spread) and warns above a 3× spread.
- **Why:** automatic gain control multiplies every subcarrier by a varying constant, which looks like "the whole channel changed". Motion changes the *shape* across subcarriers; AGC changes only the *scale*, so dividing by the median removes the scale and keeps the shape. A median, not a mean, because null and guard subcarriers sit near zero and drag a mean around as their count shifts with PHY. Insertion sort: "n <= 384 … plenty fast and has no allocation". A spread above 3× means a weak link (move closer to the router), not a fatal error. Why the 3× threshold, and why the median covers all subcarriers rather than the selected ones: not recorded.
- **Rejected:** mean normalisation.
- **Evidence:** not run on a board. 2026-10-30/11-01 (synthetic, D-174; TESTING §3.3 item 11): in `test_e2e` (f) the median division removes a common gain exactly (f0: worst relative change 1.18e-07 over ±1-6 dB), but not (a) a gain drop in int8 CSI, where the fixed rounding noise grows relative to the signal (f-int8: −4.5 and −6 dB held kept an empty room PRESENT 99.6 %), nor (b) shape changes from compression or a gain-state-dependent response (f-comp, f-state). All receiver magnitudes there are assumptions. The 2026-11-01 replay of real ESP32-S3 recordings uses the median of all 52 stored entries, nulls included, as here (TESTING §5.3).
- **Where:** `step05_amplitude/main/main.c:170-191`, `:308-312`, `:332-353`; `step08_final_human_detector/main/main.c:494-505`; README § "The three problems this step solves", § "The diagnostic to watch".
- **Status:** unverified on hardware

### D-57 Two EMAs per subcarrier: fast 0.5 s and slow 60 s, with alpha from the measured packet rate
- **Decision:** `fast[k]` (time constant 0.5 s) is "what the channel looks like now"; `slow[k]` (60 s) is "what the room normally looks like". `alpha = 1 - exp(-1 / (rate_hz * tau))`, where `rate_hz` is measured during calibration warm-up and stored in the baseline. Steps 07/08 recompute alpha from the stored rate. In Step 08, until the rate is loaded (a stored baseline accepted in PH_LOCK) or measured (PH_WARMUP), `alpha_fast` = 0.1 and `alpha_slow` = 0.001, hard-coded (`step08_final_human_detector/main/main.c:371`); `rate_hz` holds `1000 / CSI_PING_INTERVAL_MS` only as a placeholder (`:372`), and alpha is recomputed at `:454` and `:573`.
- **Why:** time constants mean seconds only if alpha follows the real packet rate, which differs by router. Why 0.5 s and 60 s specifically: not recorded.
- **Rejected:** none recorded.
- **Evidence:** not run on a board.
- **Where:** `step06_baseline/main/main.c:83-84`, `:336-362`; `step08_final_human_detector/main/main.c:77-78`, `:569-575`.
- **Status:** unverified on hardware

### D-58 Two metrics: M (motion, primary) and D (deviation, secondary)
- **Decision:** `M` = mean over selected k of `|na[k] - fast[k]| / sigma[k]`; `D` = mean over selected k of `|fast[k] - slow[k]| / sigma[k]`. M is the primary detector. D may assert presence only after it has been sustained (D-68).
- **Why:** M is high while the channel is changing (walking, an arm moving), "what CSI is genuinely good at". D is high when the channel has settled somewhere new (someone came in and stands still); it is slower and less reliable. Dividing by `sigma[k]` stops a few naturally noisy subcarriers dominating the average. Espressif's esp-radar likewise separates `waveform_jitter` from `waveform_wander` (docs/REFERENCES.md §2.2).
- **Rejected:** none recorded.
- **Evidence:** no accuracy measured (2026-09-25).
- **Where:** `step06_baseline/main/main.c:12-33`; `step07_presence/main/main.c:345-358`; README § "The two metrics (Steps 07 and 08 use exactly these)".
- **Status:** unverified on hardware

### D-59 D's reference is the 60 s slow EMA, not a frozen empty-room snapshot
- **Decision:** the reference for D is the slow EMA. The stored NVS baseline supplies only the subcarrier mask, per-subcarrier sigma and the metric noise floor.
- **Why:** a frozen baseline goes out of date as temperature changes, the router retunes, a door stays open or furniture moves, and "you would collect false alarms for the rest of the day". The slow EMA absorbs hour-scale drift while a person entering still shows as a short-term deviation.
- **Rejected:** a frozen empty-room snapshot.
- **Evidence:** reasoning; not run on a board.
- **Where:** `step06_baseline/main/main.c:35-39`; README § "Why `D` compares against a moving average, not a frozen snapshot".
- **Status:** unverified on hardware

### D-60 Per-subcarrier sigma from the empty room, floored at 0.005
- **Decision:** `sigma[k]` is the empty-room sample standard deviation of `na[k]` from calibration phase A (Welford accumulators in double), clamped to at least `SIGMA_FLOOR` = 0.005.
- **Why:** "never divide by ~0". Why 0.005, and why Welford in double: not recorded.
- **Rejected:** none recorded.
- **Evidence:** not run on a board.
- **Where:** `step06_baseline/main/main.c:85`, `:374-400`; `step08_final_human_detector/main/main.c:79`, `:587-607`.
- **Status:** unverified on hardware

### D-61 Subcarrier selection: mean normalised amplitude ≥ 0.20, and at least 8 of them
- **Decision:** phase A selects subcarrier k when its mean normalised amplitude is at least `CONFIG_CSI_SELECT_MIN_AMP / 100` (default 20, i.e. 0.20 of the packet median). Fewer than 8 selected is logged as an error; Steps 07/08 treat it as a degenerate baseline.
- **Why:** null and guard subcarriers sit near zero and carry only noise. Why 0.20 and why 8: not recorded.
- **Rejected:** none recorded.
- **Evidence:** support for masking: a single highest-variance subcarrier reached 92.43 % in Armenta-Garcia 2025 (docs/REFERENCES.md §3.1). Open critique: ESPectre says motion is coherent over about 10 neighbouring subcarriers, so a rule that keeps adjacent subcarriers buys little; auditing Step 06's mask for adjacency has not been done (docs/REFERENCES.md §2.6, §6 item 3).
- **Where:** `step06_baseline/main/main.c:389-424`; `step06_baseline/main/Kconfig.projbuild:58-66`; `step08_final_human_detector/main/main.c:440` (`s_base.n_selected >= 8`).
- **Status:** unverified on hardware

### D-62 Thresholds come from a two-phase empty-room calibration, not magic numbers
- **Decision:** Step 06 runs WAIT_LOCK → WARMUP (5 s) → PHASE_A (15 s: per-subcarrier sigma and selection) → SELECT → PHASE_B (15 s: empty-room mean and std of M and D) → SAVE → LIVE, about 35 s with the room empty. The phase-B statistics become the thresholds. Step 08 adds a 10 s leave-the-room countdown and calibrates by itself when no valid baseline exists (D-73).
- **Why:** warm-up lets the averages settle. Phase A measures the noise; phase B measures how much M and D wobble in an empty room, "so the system tunes itself to YOUR room". A baseline taken with someone in the room is "the single most common reason these systems don't work": it inflates `m_std` and flattens every later reading. Why these durations: not recorded.
- **Rejected:** fixed hand-set thresholds.
- **Evidence:** not run on a board. 2026-11-01, offline replay of real recordings (another group's ESP32-S3 node-to-node CSI, not the C5; TESTING §5.3, §3.3 item 12; D-174): with phase B's 15 s statistics the per-link 99th percentile of `m_sigma` over the next ~4 minutes of empty room reached 5 sd on 93 / 138 / 110 of 336 runs (raw / first / mean), and of `d_sigma` on 132 / 148 / 135; median `m_sigma` p99 3.66 sd. So in that data phase B understates the empty-room spread (an autocorrelated metric, a median 54 packets in the burst modes, a room that is not stationary). Synthetic, 2026-10-30 (`test_e2e` f-cal): gain steps during calibration made the detector deaf or falsely PRESENT, depending on model and room (assumed magnitudes).
- **Where:** `step06_baseline/main/main.c:1-46`, `:287-573`; `step06_baseline/main/Kconfig.projbuild:32-56`; README § "Step 06 — Calibrate against an empty room", § "What the two calibration phases measure".
- **Status:** unverified on hardware

### D-63 Re-seed the slow EMA at the empty-room mean before phase B
- **Decision:** in SELECT, set `slow[k] = mu[k]` (the phase-A empty-room mean) before phase B starts.
- **Why:** until then the slow EMA has been dragging along whatever it saw first, often the user walking out. On a 60 s time constant that leftover would inflate D through phase B and bake a too-high `d_mean`/`d_std` into the baseline. Steps 07/08 seed from the same `mu`, so D means the same thing in calibration and detection.
- **Rejected:** seeding the slow EMA from the first packet.
- **Evidence:** reasoning; not run on a board.
- **Where:** `step06_baseline/main/main.c:408-417`; `step08_final_human_detector/main/main.c:613-620`.
- **Status:** unverified on hardware

### D-64 At detection start: fast EMA from the first packet, slow EMA from the stored mean
- **Decision:** `fast[k]` = the first packet's `na[k]`; `slow[k]` = the baseline's `mu[k]` (Step 07, and Step 08's settle phase). Step 08 then warms the EMAs for `CSI_CAL_WARMUP_S` before detecting.
- **Why:** seeding `slow` from the first packet meant a boot with someone in the room took that person as "empty", and even an empty boot left D inflated for minutes while a one-packet seed decayed on the 60 s constant.
- **Rejected:** a first-packet seed for the slow EMA (the earlier behaviour, per the code comment).
- **Evidence:** reasoning; not run on a board.
- **Where:** `step07_presence/main/main.c:304-315`; `step08_final_human_detector/main/main.c:507-521`, `:705-713`.
- **Status:** unverified on hardware

### D-65 `METRIC_SMOOTH` = 0.15, identical in calibration and detection
- **Decision:** M and D are smoothed with `METRIC_SMOOTH` before phase B accumulates their statistics and again before the detector thresholds them. The constant is defined identically in Steps 06, 07, 08 and 10.
- **Why:** smoothing reduces variance. Thresholding a smoothed value against an unsmoothed `m_std` reads systematically low and under-triggers, which "looks exactly like the thing doesn't work". Change it in every step or none, then recalibrate. Why 0.15: not recorded.
- **Rejected:** calibrating on the raw metric.
- **Evidence:** reasoning; not run on a board.
- **Where:** `step06_baseline/main/main.c:87-92`; `step07_presence/main/main.c:82-84`; `step08_final_human_detector/main/main.c:81-85`; `step10_recognise_count_vitals/main/main.c:274`; README § "Tuning".
- **Status:** active

### D-66 Thresholds are in empty-room standard deviations, set in hundredths
- **Decision:** `m_sigma = (M_smoothed - m_mean) / m_std` and `d_sigma` likewise (0 when the std is 0). Kconfig thresholds are integers in hundredths (500 = 5.00 sd), converted by `THR_*` macros.
- **Why:** thresholds follow the measured empty-room distribution, "so the detector auto-tunes to the noise floor of YOUR room". Why hundredths: not recorded.
- **Rejected:** none recorded.
- **Evidence:** the defaults are "starting points to tune, not validated values" (README verification status). 2026-11-01 replay (not C5 data): D-62's evidence; raising motion and static to 8 sd cut empty-room PRESENT to a median 0-5 % and onsets to 0-20/h, with walking ~100 % and the seated median 1.00 but its lower quartile 0.55 / 0.42 / 0.93 (TESTING §5.3). Not a basis for changing the defaults (D-174).
- **Where:** `step07_presence/main/main.c:87-90`, `:364-365`; `step06_baseline/main/csi_baseline.h:42-46`.
- **Status:** unverified on hardware

### D-67 Motion branch with hysteresis: trigger at 5.00 sd, release below 2.50 sd
- **Decision:** the motion flag sets when `m_sigma >= CSI_MOTION_SIGMA_X100 / 100` (default 500) and clears only when `m_sigma < CSI_MOTION_RELEASE_X100 / 100` (default 250).
- **Why:** two thresholds stop the output chattering around a single value. Raise the trigger for false alarms; lower it if walking is missed.
- **Rejected:** a single threshold.
- **Evidence:** never tuned on hardware. 2026-11-01 replay of real ESP32-S3 recordings (not the C5; TESTING §5.3): the motion branch separates walking from empty (AUC of `m_sigma` 0.96 / 1.00 / 1.00) but barely separates seated from empty (0.60 / 0.64 / 0.67); at 5.00 sd it gives 20-50 onsets per hour in the empty phases, mostly short crossings that the 5 s hold stretches (D-69).
- **Where:** `step07_presence/main/main.c:367-372`; `step07_presence/main/Kconfig.projbuild:35-52`; `step08_final_human_detector/main/Kconfig.projbuild:86-99`; README § "The decision logic".
- **Status:** unverified on hardware

### D-68 Static branch: `d_sigma` ≥ 5.00 sustained for 4 s
- **Decision:** the static flag is true only after `d_sigma` has stayed at or above `CSI_STATIC_SIGMA_X100 / 100` (default 500) continuously for `CSI_STATIC_SUSTAIN_MS` (default 4000). Any dip resets the timer.
- **Why:** D also rises for slow environmental drift, so a momentary spike must not be enough. The branches are not OR'd on equal terms: motion is primary, and static "must earn it".
- **Rejected:** OR-ing static and motion on equal terms.
- **Evidence:** Step 08's own header calls still-person detection unreliable. Not measured on the C5. 2026-11-01 replay of real ESP32-S3 recordings (not the C5; TESTING §5.3, §3.3 item 12): the static flag was on a median 100 % of the seated time and the motion flag 3-11 %; AUC of `d_sigma` seated vs empty 1.00. In that data the D branch, not the motion branch, carries a seated person, and the 4 s sustain is what keeps D's empty-room tail (p99 ≥ 5 sd on 132-148 of 336 runs) from triggering. The same branch holds the stuck runs of D-70.
- **Where:** `step07_presence/main/main.c:374-384`; `step07_presence/main/Kconfig.projbuild:54-69`; README § "The decision logic".
- **Status:** unverified on hardware

### D-69 Presence hold: 5 s after the last trigger
- **Decision:** PRESENT = motion OR sustained static, held for `CSI_PRESENCE_HOLD_MS` (default 5000) after the last trigger.
- **Why:** someone who pauses between steps would otherwise blink the output.
- **Rejected:** none recorded.
- **Evidence:** not run on a board. docs/REFERENCES.md §6 item 2 says "this repo has none"; that is stale (A-14). 2026-11-01 replay (not C5 data): in the first empty phase (raw mode) the motion flag was on a median 1 % of the time and the hold alone 8 %, so most false PRESENT time there is the hold after a brief crossing (TESTING §5.3).
- **Where:** `step07_presence/main/main.c:386-405`; `step07_presence/main/Kconfig.projbuild:71-77`; `step08_final_human_detector/main/Kconfig.projbuild:111-114`.
- **Status:** unverified on hardware

### D-70 Freeze the slow EMA while PRESENT, for at most 600 s
- **Decision:** while PRESENT (in Step 08 only during detection), `slow[k]` stops adapting, for at most `CSI_SLOW_FREEZE_MAX_S` (default 600, range 0–7200; 0 = no cap, not recommended). When the cap expires a log line says the reference is adapting again.
- **Why:** if it keeps adapting it absorbs a person within about a minute (a still person then reads clear) and reports their departure as a change as large as their arrival, giving PRESENT for 1–2 minutes in an empty room. The cap stops a genuine change made while someone was present (a moved chair, an open door) from holding PRESENT forever. Residual: a still person who never crosses a threshold is still absorbed on the 60 s constant. Why 600 s: not recorded.
- **Rejected:** an always-adapting reference (Steps 07/08 before 2026-09-23); an uncapped freeze.
- **Evidence:** found by the 2026-09-22 review; fixed 2026-09-23 (uncommitted). 2026-11-01 replay of real ESP32-S3 recordings (not the C5; TESTING §5.3, §3.3 item 12): 52 / 37 / 56 of 336 link runs (raw / first / mean) stayed PRESENT on > 90 % of the 120 s empty phase after the person left, every one held by the static flag (median `d_sigma` 12.4 sd, raw), mostly in three laboratory trials. The freeze holds the changed reference and the 600 s cap outlasts every trial, so the data show only the start of such a hold; 8 sd thresholds leave 37 / 33 / 42. Which cap is better cannot be decided from these trials (D-174). Synthetic, 2026-10-30 (`test_e2e` f-comp, f-state, shadow D, assumed magnitudes): a gain-induced shape change, once it holds PRESENT, freezes the slow EMA, so D does not decay until the cap.
- **Where:** `step07_presence/main/main.c:322-343`; `step07_presence/main/Kconfig.projbuild:79-92`; `step08_final_human_detector/main/main.c:526-549`; `step08_final_human_detector/main/Kconfig.projbuild:116-129`.
- **Status:** unverified on hardware

### D-71 No-CSI watchdog: 5 s without a usable packet clears PRESENT
- **Decision:** if no packet of the calibrated shape has been accepted for `CSI_NO_CSI_TIMEOUT_MS` (default 5000, range 1000–60000), clear motion, static and hold state, set PRESENT false, turn the LED off and log "Presence UNKNOWN, reporting clear". Step 08 also emits JSON `state` `"no_csi"` with `present` 0 until packets flow, and refuses `recal` meanwhile (D-134). When packets resume, the fast EMA is re-seeded and metric smoothing restarts. In Step 08 the watchdog runs only while detecting.
- **Why:** without it a dropped link or a changed CSI shape freezes the last decision, possibly PRESENT with the LED on, until packets return (Step 07) or forever (Step 08). Why Step 08 limits it to the detect phase: not recorded.
- **Rejected:** none recorded.
- **Evidence:** added 2026-09-23 (uncommitted); not run on a board.
- **Where:** `step07_presence/main/main.c:237-266`, `:285-290`, `:316-320`; `step08_final_human_detector/main/main.c:384-387`, `:805-826`; `step08_final_human_detector/main/Kconfig.projbuild:131-140`.
- **Status:** unverified on hardware

### D-72 Reject wrong-shape packets during detection, and say so
- **Decision:** Step 07 rejects every packet whose length differs from the baseline's `csi_len`; after more than 500 rejects with none accepted it logs "The channel or PHY changed. Rerun Step 06". Step 08 warns when wrong-shape packets outnumber accepted ones.
- **Why:** a baseline is meaningful only for the shape it was calibrated on. Why 500: not recorded.
- **Rejected:** none recorded.
- **Evidence:** not run on a board.
- **Where:** `step07_presence/main/main.c:271-283`; `step08_final_human_detector/main/main.c:479-483`, `:855-858`.
- **Status:** unverified on hardware

### D-73 Step 08: one pipeline task with a leave-the-room countdown and a settle phase
- **Decision:** one `pipeline_task` runs LOCK, then either SETTLE (valid baseline) or LEAVE (10 s) → WARMUP → A → SELECT → B → SAVE, then DETECT: about 45 s on a first boot (10 + 5 + 15 + 15). It logs progress each second plus a JSON `remaining_s` line.
- **Why:** LEAVE is a "countdown after boot so you have time to walk out of the room"; SETTLE warms the EMAs before detecting; a brand-new board "just works". Why 10 s: not recorded.
- **Rejected:** none recorded.
- **Evidence:** not run on a board.
- **Where:** `step08_final_human_detector/main/main.c:333-362`, `:364-861`; `step08_final_human_detector/main/Kconfig.projbuild:71-77`; README § "Step 08 — The finished detector ⭐".
- **Status:** unverified on hardware

### D-74 Placement: board and router on opposite sides of the area, 1–6 m apart, board fixed
- **Decision:** put the board and the router on opposite sides of the monitored area, 1–6 m apart; tape the board down; avoid large metal; recalibrate after moving anything.
- **Why:** the detector sees only changes in the radio path between board and router. A person crossing that path gives the strongest signal, and moving the board is indistinguishable from movement in the room.
- **Rejected:** none recorded.
- **Evidence:** ESPectre advises roughly 3–8 m from the AP (docs/REFERENCES.md §2.6). Not measured here.
- **Where:** README § "Getting good results — physical placement".
- **Status:** unverified on hardware

### D-75 Not human-specific; filtering out pets and fans is not attempted
- **Decision:** the detector senses propagation changes, so dogs, fans and curtains in a draught also trigger it. Neither Step 08 nor Step 10 filters non-humans.
- **Why:** a documented limit. The literature route (gait/speed features plus a classifier) is left as a next step.
- **Rejected:** none recorded.
- **Evidence:** Zhu 2025 (ADT): non-human false alarms 63.1 % → 8.4 % with gait/speed features and an SVM, on commercial routers and IoT devices, not ESP32 (docs/REFERENCES.md §3.4).
- **Where:** `step08_final_human_detector/main/main.c:23-33`; README § "Read this before you start"; docs/REFERENCES.md §3.4; docs/COMPARISON.md §5.
- **Status:** active

### D-166 Still-room calibration is a workaround for a room that cannot be emptied, not the procedure
- **Decision:** the calibration procedure stays an **empty** room (D-62, D-73). Where the room cannot be emptied (the office of 2026-09-25, three people and no empty room available), a still-room calibration is allowed as a **workaround** for bring-up and for watching the dashboard (D-163): everyone stays in place, seated and still (no typing, no gesturing, no phone in hand), from `recal` until the `CALIBRATED` banner, and ignores the "leave the room" prompts. At the defaults that is 45 s (`CSI_CAL_LEAVE_ROOM_S` 10 + `CSI_CAL_WARMUP_S` 5 + `CSI_CAL_PHASE_A_S` 15 + `CSI_CAL_PHASE_B_S` 15). A boot with no stored baseline first locks the CSI shape (`CSI_LOCK_PACKETS` 200, about 4 s at 50 packets/s). Every result obtained on such a baseline is labelled "still-room baseline", and `recal` is run in an empty room as soon as one is available.
- **Why:** it is the only way to calibrate at all in that room. What it breaks, read from the calibration code (Step 10 `main.c`, `pipeline_task`, the `PH_A`, `PH_SELECT` and `PH_B` cases): (1) phase A's per-subcarrier `mu[k]` and `sigma[k]` include the seated people's breathing and small movements, so every M and D term is divided by a larger sigma; (2) phase B's `m_mean`, `m_std`, `d_mean` and `d_std` include them too, so both thresholds sit higher. D-62 already names this as "the single most common reason these systems don't work": it inflates `m_std` and flattens every later reading; (3) `PH_SELECT` re-seeds the slow EMA at that `mu[k]` (D-63), so D's reference **contains** the seated people. While they sit still, D sees nothing, and the static branch (D-68) cannot report them: the room reads EMPTY with three people in it until somebody moves. By the same reasoning (not measured), one of them getting up and leaving moves the channel away from that reference, which the static branch may report as PRESENT, and PRESENT then freezes the slow EMA for up to `CSI_SLOW_FREEZE_MAX_S` (D-70); (4) the motion gate learns its noise floor from seconds that Step 08 calls empty (D-82), and those seconds now contain three seated people, so `hf` near 1 means "like the seated room", not "nobody moving". What still works: detection of **movement** (walking, standing up, reaching), against the raised threshold. How much sensitivity is lost has not been measured. Not caused by the calibration, but true in any room of three: until the counter is trained, `count` is 1 whenever anyone is present, so `vitals` never says `"multiple"` (D-130) and any `br_bpm` is a mix of several chests; and recognition is only defined for one mover at a time (D-97).
- **Rejected:** describing it as an alternative calibration procedure (the spec of 2026-09-25 asks for "a workaround, not the procedure").
- **Evidence:** reasoning from the calibration code and D-59, D-62, D-63, D-68, D-70, D-82, 2026-09-25. On 2026-09-28 a Step 10 `recal` ran with neither the board's placement nor who was in the room recorded (TESTING §5.1, row 2026-09-28 13:51–14:07), so that baseline is treated as a still-room or occupied one. Not measured: no still-room baseline has been compared with an empty-room one on this board. The baseline is saved to NVS and reused at every boot, by Step 08 as well (D-114), until the next calibration. First tracked record: this log.
- **Where:** README § "See how it decides: the live dashboard and the why lines" (the workaround box); docs/TESTING.md §2.6; Step 10 `main.c` `pipeline_task` (`case PH_A`, `case PH_SELECT`, `case PH_B`). Line numbers are not given because `main.c` was being edited in parallel on 2026-09-25.
- **Status:** unverified on hardware

---

## 4. Step 10 analytics

All paths in this section are under `step10_recognise_count_vitals/`. Every
number below comes from **synthetic** host tests unless it says otherwise;
none of it has run on a board.

### D-76 Step 10 is a new project copied from Step 08; Step 08 and the root build are untouched
- **Decision:** Step 10 runs Step 08's pipeline unchanged (same calibration, presence decision, NVS baseline, and JSON keys first) and adds recognition, counting and vitals on top. Its only influence on Step 08's presence answer is the breathing hold (D-90).
- **Why:** keep the finished detector and its tools stable while the experimental layer is developed. The user asked on 2026-09-24 to "recognise the person, count the person, also measure the HB".
- **Rejected:** modifying Step 08 in place.
- **Evidence:** Step 08 and root images unchanged by Step 10's work (root rebuilt 2026-09-24, 0xdb290).
- **Where:** README § "Step 10 — recognise, count, vitals (experimental) 🧪"; `step10_recognise_count_vitals/main/main.c:1-130` (header).
- **Status:** active

### D-77 Design every Step 10 computation for a chip with no FPU
- **Decision:** `float` only (no `double`), no per-sample `sinf`/`expf`, precomputed tables, FFT work spread across packets (D-87), counting through running sums per packet plus a few power iterations per 256-packet block (D-99). Large state is static (D-79).
- **Why:** the C5 is built with `-march=rv32imac -mabi=ilp32`, so every float operation is software-emulated. The CPU costs in the module headers (about 6–10 ms per vitals FFT, 12–32 ms per gait window) are estimates, not measurements, and the worst packet (gait window + count block + FFT together) has never been timed.
- **Rejected:** none recorded.
- **Evidence:** static sizes from the linker map: vitals 30,984 B, gait 11,536 B, gate 624 B, count under 1 KB; static HP SRAM 190,539 of 320,928 B (59 %), 2026-09-25.
- **Where:** README § "Kconfig — `menuconfig` → *Recognise / count / vitals*" (cost notes); `main/vitals.h:44-46`; `main/csi_dsp.h:8-16`.
- **Status:** unverified on hardware

### D-78 One task owns all Step 10 state; no locks anywhere
- **Decision:** models, sessions, votes, vitals and the whose-vitals latch are statics touched only by `pipeline_task`. `console_task` only parses lines and posts a `cmd_t` to `s_cmd_q` (4 deep; it answers "CMD busy" when full). The pipeline drains that queue after every packet and on every 100 ms receive timeout, and executes the commands itself. The only cross-task datum is `s_dropped`, one volatile, naturally aligned 32-bit read.
- **Why:** with every mutation in one task, no lock or critical section is needed; on this 32-bit core a 32-bit load cannot see half an update.
- **Rejected:** locks or critical sections.
- **Evidence:** code structure; nothing to measure.
- **Where:** `main/main.c:328-334`, `:1634-1642`, `:2292-2307`, `:2367-2377`.
- **Status:** active

### D-79 All large Step 10 state is static; the pipeline stack is 12 KB
- **Decision:** `vitals_t` (about 30 KB), `gait_t` (about 11 KB) and `pv_t` are file-scope statics. The pipeline task stack is 12,288 bytes.
- **Why:** the work arrays cannot live on a 12 KB stack. `report_t` still sits on the stack in a few places, which is fine at its size.
- **Rejected:** none recorded.
- **Evidence:** linker map: `vitals_t` 30,984 B; `.bss.s_pv` 304 B (2026-09-25).
- **Where:** `main/main.c:328-334`, `:3561`; `main/vitals.h:44-46`; `main/person_vitals.h:58-60`.
- **Status:** active

### D-80 Gait and count work on 8 contiguous subcarrier groups
- **Decision:** the selected subcarriers are split into `CSI_NGROUPS` = 8 contiguous, nearly equal runs. Per packet, `grp[g]` = the mean over group g of `(na - fast) * inv_sigma`: the deviation from the 0.5 s EMA in units of empty-room noise, i.e. high-passed at about 0.3 Hz. The motion gate, `gait_id` and `count_est` all consume these 8 signals.
- **Why:** motion perturbs roughly ten neighbouring subcarriers together, so eight groups cover a 52-subcarrier HT20 frame with little lost, and the covariance becomes a cheap 8×8. Because the group signals are high-passed, their mean is small next to their spread, so a one-pass covariance is numerically safe.
- **Rejected:** none recorded.
- **Evidence:** `test_count` checks the one-pass float covariance against a two-pass double reference, with and without an offset.
- **Where:** `main/csi_dsp.h:26-31`, `:60-72`; `main/csi_dsp.c:123-159`; `main/count_est.c:71-85`; `main/main.c:2780-2784`.
- **Status:** unverified on hardware

### D-81 Vitals use their own ~1.6 Hz motion gate, not Step 08's motion flag
- **Decision:** `motion_gate.c` runs two cascaded first-order high-passes (`MGATE_TAU_S` 0.10 s, corner about 1.6 Hz) on the 8 group signals, smooths their energy over 0.5 s and divides it by an empty-room floor (D-82). JSON `hf` is that ratio. At or above the threshold for 0.3 s the gate says MOVING; below it for 1 s, still. Vitals analyse only while it says still. Step 08's detection is left exactly as it was.
- **Why:** Step 08's fast EMA (0.5 s) has its corner near 0.32 Hz, inside the 0.1–0.5 Hz breathing band, so about 60 % of a 15/min breath passes into M. A breathing chest therefore holds Step 08's flag on, and vitals gated on that flag would never run: the measurement would be switched off by the signal it measures. A second-order high-pass at 1.6 Hz keeps about 2.5 % of a 0.25 Hz breath while walking (2–20 Hz) passes almost entirely. This is this repo's own design, not a published method.
- **Rejected:** gating vitals on Step 08's motion flag (the previous device path).
- **Evidence:** in the synthetic e2e test, a seated person breathing at 2 noise units held Step 08's flag on 98.6–100 % of the time and vitals never ran; `test_gate`: `hf` stays at or below about 1.6 for a 15/min breath 8× the group noise.
- **Where:** `main/motion_gate.h:5-27`, `:49-54`; `main/main.c:2757-2784`; README § "Stillness: the vitals motion gate"; docs/REFERENCES.md §6 item 7.
- **Status:** unverified on hardware

### D-82 Motion-gate noise floor: empty-room seconds first, the quietest second as a fallback (known weakness)
- **Decision:** the floor is a 20 s EMA of one-second energy means, taken only from seconds in which every packet had `room_empty`. The fallback is the minimum of the last 120 one-second means, used once 3 exist. Until a floor exists the gate says MOVING (JSON `hf` is null for about 3 s). `room_empty` is Step 08's presence for that packet *before* the breathing hold is applied. The floor survives a no-CSI reset and is cleared on a new baseline or recalibration.
- **Why:** no absolute threshold is needed, and an empty-room second cannot contain anybody's motion. The fallback covers a boot that reloads a baseline with somebody already in the room. `room_empty` is taken before the hold because the hold is derived from the gate (still → vitals → breathing → hold); feeding it back would close a loop. A new baseline changes the units of `grp`, so an old floor would be wrong.
- **Rejected:** feeding the hold-adjusted presence in as `room_empty` (a feedback loop).
- **Evidence:** known weakness: if every second in the ring contains motion (someone moving from the first packet), that motion becomes the floor and the gate calls it still after about 4 s, so vitals run on motion and gait/count get no moving packets. One quiet or empty second repairs it. `test_gate` case (e) and `test_e2e` print this case on purpose.
- **Where:** `main/motion_gate.h:29-47`; `main/motion_gate.c:40-88`; `main/main.c:2768-2779`.
- **Status:** unverified on hardware

### D-83 Gate threshold defaults to 4.0; it is a vitals setting, fixed at 4.0 when vitals is off
- **Decision:** the gate ratio is `CONFIG_CSI_VITALS_MOTION_RATIO_X100 / 100` (default 400, range 150–5000) with vitals on, otherwise a hard-coded 4.0. Tune it between the `hf` of an empty room or a seated person and that of walking.
- **Why:** vitals is what the gate was built for and where a wrong threshold does the damage. 4.0 keeps margin over hard breathing (synthetic maximum 3.73 at 30/min, 8× noise). The cost: on the synthetic walkers the gate called only 80.8 % of walking packets MOVING, giving 222 gait windows against 287 with Step 08's flag alone (of 369). 3.0 gives 250 and 2.5 gives 263, with less margin over breathing.
- **Rejected:** 3.0 and 2.5 (more gait windows, less margin).
- **Evidence:** synthetic `hf` table: empty room mean 1.00 / max 1.59; seated breathing max 1.42–2.33 (2.69 without the empty-room floor); walking at 2× noise mean 4.8 (2026-09-24). Real walking has never been measured in `hf`.
- **Where:** `main/main.c:232-240`; `main/Kconfig.projbuild:311-346`; README § "Stillness: the vitals motion gate".
- **Status:** unverified on hardware

### D-84 "Moving" for recognition and counting = Step 08's flag AND the gate not still
- **Decision:** gait windows and count blocks see `moving = motion_flag && !still`. Counting and recognition run only while PRESENT, counting first so recognition knows this packet's count. While the gate has no floor it reports MOVING, so they fall back to the flag alone.
- **Why:** a still person breathing hard trips Step 08's flag. Without the gate's veto their breathing would fill gait windows and count blocks as if they were walking, enrolling a template of somebody sitting or voting a count. Running only while PRESENT means an empty room costs nothing.
- **Rejected:** Step 08's flag alone.
- **Evidence:** synthetic tests, 2026-09-24 (the window counts in D-83).
- **Where:** `main/main.c:2953-2975`; README § "Stillness: the vitals motion gate".
- **Status:** unverified on hardware

### D-85 Vitals bin by packet arrival time into 100 ms bins; repeat a value, never interpolate
- **Decision:** the packet timestamp is taken in the CSI callback (`csi_raw_t.t_us`), not at dequeue. The amplitudes of 12 subcarriers spread across the selected set go into uniform 10 Hz bins. An empty bin repeats the previous value. A gap over 1 s (`VIT_MAX_GAP_US`), a clock running backwards, or any moving packet throws the buffer away.
- **Why:** ping replies jitter and get lost, so packet counts are not time; a gait window or an FFT can also delay dequeueing and bunch packets together. Repeating a value cannot create a periodicity that was not there; more than a second of held values would be invented data. The 12 channels are spread out because which subcarriers see the chest depends on multipath, and neighbours mostly repeat each other. Binning by time also lets breathing work at either ping rate.
- **Rejected:** binning by packet count or dequeue time; interpolation.
- **Evidence:** `test_vitals` uses a synthetic link with ±3 ms jitter and 10 % of replies lost. 2026-10-30, reviewed 2026-11-01 (synthetic; TESTING §3.3 item 10; D-174): with 0.3-0.9 s link silences, repeating the last bin freezes a breathing swing into a step whose spectrum leaks into the low heart band: 23 of 3,997 analyses with no heart reported 49.2-56.2 bpm, and a weak heart (×0.50, ×0.25) gave 6 and 12 values more than 3 bpm off. So repeating a value can create a false heart-band peak; breathing stayed within tolerance in these runs (13 and 15/min). A scratch copy that interpolates linearly across the empty bins (F2) removed all of them and kept the then-suite green; **not applied**, pending the user's decision, which would supersede this entry (D-174). The same tests confirm that binning by arrival time keeps 72 bpm right at irregular 40-100 Hz rates, while a 40 Hz stream stamped as 50 Hz reads 90 bpm. 2026-11-03 (synthetic, D-175): F2 applied. With the repeat loop restored as a mutant, (6b) and the three (12b) silence rows fail at the shipped seed base (6 / 12 / 23 wrong heart values, 49.8-52.5 / 49.8-60.9 / 49.2-56.2 bpm; `~/ESP32C5_CSI_logs/2026-11-03_impl/after_review/mutation/tv_revert.log`); over 52 seed bases the repeat loop gives 889 wrong heart values in 332,374 silence analyses, D-175 gives 1 (`~/ESP32C5_CSI_logs/2026-11-03_impl/after_review/sweep12b/SUMMARY.txt`). So D-85's *Why* ("repeating a value cannot create a periodicity that was not there") does not hold for a breathing swing cut by a silence.
- **Where:** `main/main.c:289-300`; `main/vitals.h:16-22`; `main/vitals.c:77-82`, `:375-402`.
- **Status:** superseded by D-175 (2026-11-03) for what fills an empty bin; the text above is kept as the record. Binning by arrival time, the 12 spread channels and the reset on a long gap, a backward clock or a moving packet are unchanged and carried into D-175 (unverified on hardware).

### D-86 Vitals combine the 12 channels as equal-weight power spectra
- **Decision:** each channel's band spectrum is normalised to unit power in the band and added with equal weight. Powers are added, never complex values. Breathing is the peak of the combined 0.10–0.50 Hz spectrum.
- **Why:** power sums let channels in opposite phase reinforce instead of cancel (DeMan's frequency diversity, docs/REFERENCES.md §3.2). The first design weighted each channel by its own (peak/median)², which on noise selects the luckiest channel. A false breathing detection is not harmless: with the breathing hold (D-90) it keeps an emptied room PRESENT.
- **Rejected:** (peak/median)² weighting; DeMan's sinusoidal fit (an FFT peak is used instead). The price of equal weights: a chest seen by only 2–3 of the 12 channels is usually missed.
- **Evidence:** with (peak/median)² weights, 8.5 % of 20,000 simulated 30 s noise windows passed the 4.0 threshold (41 of 400 analyses in `test_vitals` case 3). With equal weights none did (p99.9 2.4, max 3.0; 3.2 in the heart band). Synthetic, 2026-09-24. 2026-10-30/11-01 (synthetic, D-174): equal weights do not help against a fluctuation **shared** by all channels, because the combined spectrum then has the statistics of one noise spectrum (test_vitals.c:822-826 on 2026-11-01; 884-888 on 2026-11-03): an empty room with a shared 0.1-2 Hz fluctuation of 0.02 gave breathing in 34 and a heart rate in 16 of 100 analyses; shared heart-band noise under synthetic 15/min breathing gave a heart rate in 44 of 100 (0.02) and 51 of 1,000 (0.005). The same noise drawn per channel gave none (TESTING §3.3 item 10).
- **Where:** `main/vitals.c:155-187`; `main/vitals.h:28-35`.
- **Status:** unverified on hardware

### D-87 At most one FFT per packet: an analysis is spread over 12 packets
- **Decision:** every 2 s (20 bins), once enough still data exists, one analysis transforms **one** channel per `vitals_push()` call (a 512-point FFT) and finishes after 12 packets. Each channel takes the newest samples at the time of its own call.
- **Why:** with no FPU one soft-float FFT is estimated at 6–10 ms, so no packet may pay for more than one, and the pipeline must never fall a whole analysis behind. Windows a bin or two apart do not matter because only power spectra are combined, and taking the newest samples leaves the ring free to wrap mid-analysis.
- **Rejected:** transforming all 12 channels on one packet.
- **Evidence:** the cost is an estimate; never measured on the C5.
- **Where:** `main/vitals.h:23-27`; `main/vitals.c:189-194`, `:404-439`; `main/Kconfig.projbuild:230-233`.
- **Status:** unverified on hardware

### D-88 Breathing and heart reporting criteria, and what resets the buffer
- **Decision:** breathing needs `CSI_VITALS_MIN_S` (20 s) of stillness and uses at most `CSI_VITALS_WINDOW_S` (30 s). It is reported if the strongest 6–30/min peak is at least `CSI_VITALS_BR_CONF_X100 / 100` (4.0) × the band median and not on a band edge, which makes 7.6–28.7/min reportable. Heart rate is searched only when breathing is confident: bins within 0.05 Hz of 2–8× the breathing frequency are removed, a peak on a band edge or lower than an excluded neighbour (a notch shoulder) is not reported, the threshold is 5.0 × median (`CSI_VITALS_HR_CONF_X100`), and the output is the median of the last 5 confident estimates within one still spell. The buffer is thrown away on gate MOVING, a gap over 1 s, a CSI outage or recalibration, but **not** when presence ends.
- **Why:** an edge peak means an out-of-band rate or undetrended drift. Breathing harmonics land in the 48–120 bpm heart band (15/min has harmonics at 60, 75, 90, 105/min), and without a breathing frequency they cannot be removed. Presence ending must not reset the buffer, because a still person whose PRESENT lapsed must keep accumulating the 20 s that can bring it back (D-90). Why these threshold values: not recorded beyond the noise figures below.
- **Rejected:** none recorded.
- **Evidence:** synthetic breathing with harmonics and no heartbeat, 100 seeds: without the shoulder test 10–21 % of analyses reported a heart rate, with it none did. Cost: a real 72 bpm under 15/min breathing is withheld in about 8 % of analyses (`test_vitals`: 159 of 2,000 withheld, every reported value within 0.52 bpm). 2026-10-30, reviewed 2026-11-01 (synthetic, D-174; TESTING §3.3 item 10): the criteria hold against white noise, per-channel heart-band noise, two non-sinusoidal breathing shapes (asymmetric, kinked exhale) and a harmonics-6-to-8-only shape (no phantom heart rate); the phase-modulated shape 3 is held only to a ≤ 5 % rate bound (phantom heart rates in 3 of 200 runs at 12/min, 59.9-60.0 bpm, and 2 at 22/min, 49.3 bpm; test_vitals.c:913-918 on 2026-11-01), and irregular 40-100 Hz rates (72 bpm or withheld). They do not hold against a shared fluctuation (D-86), link silences (D-85) or phase-modulated breathing (up to 2.01 × the true breathing rate at 8-12/min in 19 of 60 runs; breathing withheld as an edge peak in 480 of 800 analyses at 14.65-15/min). Mutants removing the breathing gate, the notch or the shoulder test, lowering either threshold, or narrowing `VIT_NOTCH_KMAX` to 5 or 7 all fail the tests. The F2 proposal (D-85) would change the evidence these criteria rest on, not the criteria themselves. 2026-11-03 (synthetic, D-175; TESTING §3.3 item 10): F2 is applied; these criteria are unchanged. The (12b) silence rows now `CHECK` a rate (at most 2 wrong heart values per 1,000 analyses, `test/test_vitals.c:1305` on 2026-11-03) and print a KNOWN LIMIT line only when a wrong value appears: none at the shipped seed base, 1 in 332,374 silence analyses over 52 seed bases (52.4 bpm with no heart, base 1770), against 889 with the repeat loop. Link silences therefore remain a failure mode of these criteria, a much rarer one in these synthetic runs.
- **Where:** `main/vitals.c:223-313`, `:353-358`; `main/vitals.h:5-14`, `:36-42`; `main/Kconfig.projbuild:257-288`; README § "Breathing and heart rate, in six lines".
- **Status:** unverified on hardware

### D-89 Heart rate is published as `hr_bpm_exp` and means nothing until checked against a reference
- **Decision:** the JSON key is `hr_bpm_exp`; heart rate is marked EXPERIMENTAL everywhere (per-person means carry an `_exp` suffix too). Before believing it, compare it with a chest strap or pulse oximeter using `tools/vitals_validate.py` (D-137). Not a medical device.
- **Why:** a heartbeat moves the chest 10–50× less than breathing, in a band where breathing harmonics land, and noise can produce a tidy number between 48 and 120. Ten-hertz binned amplitude on one omni antenna is the hardest case in the literature (PhaseBeat: 2.91 bpm error at 20 Hz, 4.82 with an omni transmitter). The best verified ESP32 result (Pulse-Fi, MAE 0.46 bpm) needed a dedicated transmitter, seated subjects and a trained LSTM.
- **Rejected:** calling it validated; simultaneous heart rate for several people (no 1×1-link result exists).
- **Evidence:** synthetic e2e: at a 1/10 heart:breathing ratio, heart rate came through only when breathing was at least 2 noise units; at 1/20, in about a quarter of analyses; 1/50 not tried (2026-09-24). 2026-10-30/11-01: `test_vitals` (9)-(12b) also produce heart rates where there is no heart (shared fluctuations, link silences; D-85, D-86, D-88), and the 2026-10-30 research sweep found no single-node source with a real-world heart-rate accuracy (D-173, D-174). More reasons to keep the `_exp` suffix.
- **Where:** README § "Heart rate: what the synthetic test shows", § "Validating heart rate — do this before you believe it"; docs/REFERENCES.md §3.4; docs/COMPARISON.md §2C (PhaseBeat), §3, §5.
- **Status:** unverified on hardware

### D-90 Confident breathing may hold or restore PRESENT, but never start it
- **Decision:** with `CSI_VITALS_HOLDS_PRESENCE` (default y), the first gate-still packet after a moving one latches `s_still_began_present`. If the gate is still, the spell began PRESENT, and breathing is valid, the hold refreshes the last trigger and forces PRESENT. This is Step 10's only influence on Step 08's presence decision. Off = Step 08's behaviour.
- **Why:** Step 08 loses a person who sits perfectly still: its hold is 5 s, while breathing needs about 20 s of data, so a seated person nearly always lapses first. Breathing as the indicator of a stationary person follows DeMan (docs/REFERENCES.md §3.2). Requiring the spell to have begun PRESENT means breathing can never start presence in a room that was empty when it went quiet. Someone who walks in too gently for the gate starts no new spell and cannot be held, which is the safe direction.
- **Rejected:** none recorded.
- **Evidence:** noise passed for breathing in 0/400 analyses (`test_vitals`) and 0/147 (e2e empty room). 2026-10-30/11-01 (synthetic, D-174): a fluctuation shared by all channels did pass (148 of 2,000 analyses at 0.005, 34 of 100 at 0.02; D-86), and in `test_e2e` f-state an assumed gain-state response gave 15 empty-room breathing analyses, after which the breathing hold held PRESENT on 1,451 packets. Both are routes by which this rule would keep an empty room PRESENT. Known limits, unmeasured: right after someone walks out, a fan or curtain at 6–30 cycles/min can hold PRESENT until the next motion, CSI gap or recalibration; a gentle exit can hold PRESENT for part of a window.
- **Where:** `main/main.c:363-372`, `:2786-2832`; `main/Kconfig.projbuild:290-309`; README § "Breathing can hold presence, carefully".
- **Status:** unverified on hardware

### D-91 What presence ending clears, and what a hard reset clears
- **Decision:** `analytics_reset(false)` at presence end clears the gait and count accumulators and both vote rings, but not vitals, not the motion gate, not the whose-vitals latch (`pv_presence_end` decides) and not the carried count. `analytics_reset(true)`, used on no-CSI, recalibration and a new baseline, also clears vitals, the gate's filters and decision (the floor is kept), the latch (`pv_release`) and any carry. Models and the vitals log are never touched there.
- **Why:** a still person whose PRESENT lapsed must keep accumulating the breathing that can bring it back, and the gate describes the room whoever Step 08 thinks is in it. After a CSI gap nothing buffered can be trusted: a breathing buffer would bridge a hole and a gait window would splice two moments.
- **Rejected:** none recorded.
- **Evidence:** the code comment calls these "the rules the JSON relies on".
- **Where:** `main/main.c:935-989`.
- **Status:** unverified on hardware

### D-92 Recognition: 18 gait features per walking window, one person at a time
- **Decision:** a window is 256 packets, one every 128, produced only if at least 80 % of its packets were moving (D-84). Features: 6 band-power fractions, spectral centroid, cadence and its autocorrelation, and log variance, all of the first principal component x(t) (features [0..9], found with `dsp_top_eig`, D-99); then the 8 per-group variance shares, the diagonal of the 8×8 group covariance divided by its trace (features [10..17], `main/gait_id.h:58-59`). Frequencies use the **measured** packet rate, so enrolment and recognition must run at the same ping rate (D-48). No recognition while the count is 2+ (D-97), of a still person, or of a stranger (only `"unknown"`).
- **Why:** how a body disturbs the channel while walking (speed spread, cadence, which subcarriers it moves) differs between people. A mostly still window describes breathing and noise, not a walk.
- **Rejected:** none recorded.
- **Evidence:** ceiling from the literature: Gaiba 2024, ESP32, controlled room, 95 % for 2 people and 74 % for 3 (abstract only, docs/REFERENCES.md §3.4). Synthetic e2e (2 walkers, 100 Hz, 3 seeds): 97.6 % right, 2.4 % unknown, 0 swapped; a never-enrolled walker was "unknown" in 63/63 windows. Those walkers are separable by construction and gated by ground truth; accuracy through the device path was not measured even synthetically.
- **Where:** `main/gait_id.h:7-67`; `main/gait_id.c:373-389`; README § "How it answers".
- **Status:** unverified on hardware

### D-93 A nearest-centroid classifier with rejection and a runner-up margin
- **Decision:** one small classifier, `csi_ncc`, serves recognition and counting. Each class keeps a Welford mean and m2 per feature. Distance = RMS over the features in units of the **pooled** within-class std (floored at 1e-4). Recognition rejects to "unknown" above `CSI_ID_REJECT_X100 / 100` = 2.5 and requires runner-up/nearest ≥ `CSI_ID_MARGIN_X100 / 100` = 1.15 when 2+ classes are usable; a class is usable after `CSI_ID_MIN_WINDOWS` (10).
- **Why:** it trains on the device from a few dozen windows, needs no matrix inverse, fits in a few hundred bytes of NVS, and fails visibly ("unknown") rather than confidently. With 2–3 enrolled people that is about as much model as one antenna's data supports. The std is pooled because per-class variances from a few dozen windows are noisy, and a class that happened to enrol wide would attract every stranger. Why 2.5, 1.15 and 10: not recorded beyond this.
- **Rejected:** "something cleverer" (no specific alternative recorded).
- **Evidence:** `test_ncc` uses separable synthetic Gaussian clusters only; it says nothing about real people.
- **Where:** `main/csi_ncc.h:6-16`; `main/csi_ncc.c:202-226`; `main/Kconfig.projbuild:402-426`.
- **Status:** unverified on hardware

### D-94 A NaN must reject, never accept
- **Decision:** in the analytics modules, comparisons are written so that NaN fails them (e.g. `!(dist <= reject)`). `ncc_add` computes the whole update in temporaries and commits nothing unless every result is finite. A non-finite distance maps to `NCC_FAR` = 1e30 (finite). Finiteness is tested as `(v == v) && fabsf(v) <= FLT_MAX`, not with `isfinite()`.
- **Why:** every comparison with NaN is false, so a NaN distance is never "> reject" and would be accepted. One bad feature vector must not poison a template that lives in NVS for months. `NCC_FAR` is finite because distances are printed with `%.2f` and `inf` is not valid JSON. `isfinite()` promotes to double, slow soft-float on the C5. The idiom relies on IEEE comparisons, so never build with `-ffast-math`. (`main.c`'s JSON printer does call `isfinite()`, once per printed number, which is fine because ESP-IDF does not build with `-ffast-math`.)
- **Rejected:** `isfinite()`.
- **Evidence:** `test_ncc` NaN/inf checks.
- **Where:** `main/csi_ncc.c:7-19`, `:33-45`, `:113-156`; `main/motion_gate.c:27-33`; `main/person_vitals.c:28-30`.
- **Status:** active

### D-95 Vote ring: 7 windows; a name needs at least 3 votes, more than "unknown", and no tie
- **Decision:** `id_tally` over the last 7 window votes returns a slot only if it has the most votes, no tie, at least 3 votes, and strictly more than "unknown". "unknown" is reported only once 3 windows have voted; until then `person` is null. `ID_MIN_WINDOWS` is clamped to `CSI_ID_ENROLL_WINDOWS`. A name already in another slot is refused (case-insensitive).
- **Why:** several agreeing windows are needed before naming anyone (Gaiba's drop from 95 % to 74 % with a third person). Before 3 votes nobody could have been named, so "unknown" would open every episode meaninglessly. Without the clamp a fresh enrolment could never be used. Two templates for one person would split the votes and fail the margin test against each other.
- **Rejected:** none recorded.
- **Evidence:** synthetic e2e (D-92).
- **Where:** `main/main.c:375-397`, `:858-907`, `:1346-1361`; `main/Kconfig.projbuild:391-400`.
- **Status:** unverified on hardware

### D-96 Enrol alone, at a normal pace, by several different routes
- **Decision:** `enroll <slot> <name>` collects `CSI_ID_ENROLL_WINDOWS` (30) qualifying windows; a window counts only if at least 80 % of its packets were moving. The person must walk alone, at a normal pace, by several routes (across, along, diagonally, near and far).
- **Why:** distance is measured in units of the spread seen during enrolment, so one fixed route gives a template so narrow (the group-share features barely vary) that later walks of the same person land 2–9 units away, which is "unknown" at 2.5. Nothing guards against a second person during enrolment; the counter is not consulted.
- **Rejected:** single-route enrolment.
- **Evidence:** synthetic e2e: with one route, most of the same people's later walks were "unknown": 79.4 % of windows, 14 of 18 walks (default `test_e2e` run on 2026-09-25, TESTING §5.2: "enrolled A/B: 20.6% correct, 79.4% unknown", walks A 1/0/8, B 0/3/6), against 2.4 % with a varying path. README says the same; the `main.c` comment was corrected from 78 % to 79 % on 2026-09-25 (A-24). Only about 60 % of windows qualified on the synthetic device path, so 30 take about 2 min 10 s at 50 Hz or 65 s at 100 Hz. Real walking never measured.
- **Where:** `main/main.c:1898-1905`; `main/Kconfig.projbuild:384-389`; README § "Enrolling a person".
- **Status:** unverified on hardware

### D-97 No classification while two or more people move
- **Decision:** `id_step` casts no vote while the count is 2 or more and logs "multiple" once; `person` reports `"multiple"`. When the counter's estimate changes to 2 the vote ring is cleared (but not the "logged" flag). Votes are also cleared when presence ends.
- **Why:** superimposed gaits would make the classifier confidently name somebody. Multi-walker identification on ESP32 reaches only 39–56 % (Custance 2026), and single-link multi-user sensing is called an open challenge (docs/REFERENCES.md §3.4). The count lags by about 3 blocks, so earlier votes may be two movers read as one; kept, they would name somebody again the moment the count returns to 1.
- **Rejected:** identifying several simultaneous walkers.
- **Evidence:** caveat: this needs a **trained** counter. Untrained, the count is 1 whenever anyone is present, so two people walking together are classified as one gait. (2026-09-25 review finding 3 added this caveat to the `main.c` header.)
- **Where:** `main/main.c:1265-1276`, `:1323-1331`, `:1428-1434`; README § "How it answers".
- **Status:** unverified on hardware

### D-98 Counting means one versus two-or-more **movers**, trained in the room
- **Decision:** `count_est` produces 6 features per non-overlapping 256-packet block: participation ratio, λ2/λ1, λ1/trace, log trace, duty and burstiness. `count-train 1` / `count-train 2` each collect `CSI_COUNT_TRAIN_WINDOWS` (20) blocks with at least 30 % moving. Classification always takes the nearest class (reject 1e9, margin 1), and the count is the majority of the last 5 classified blocks; a 2 needs a strict majority, a tie stays 1. Blocks under 30 % moving neither vote nor change the estimate. Untrained, the count is just presence (`count_src` `"presence"`). Each visit restarts at 1.
- **Why:** one link cannot count people. Independent movers disturb the subcarrier groups independently, so the group covariance has more than one significant eigenvalue (PADS's max-eigenvalue idea moved from antennas to subcarriers × time, as docs/REFERENCES.md suggested). With exactly two trained classes "neither" is not useful. A block where people barely moved says nothing about how many there are. Consequence: seated people are invisible to it, and while everyone sits the last estimate stays frozen.
- **Rejected:** counting beyond "2+" on one link; rejection or margin on the count classifier.
- **Evidence:** real counting (Wi-CaL: median error 0.35, 89.8 % within ±1 for 0–5 people) needed four crossing links (docs/REFERENCES.md §3.4). Synthetic e2e at 50 Hz: 60/60 held-out blocks and 12/12 five-block majorities (walkers near-orthogonal by construction); `test_count`'s own set 18/20.
- **Where:** `main/count_est.h:7-16`; `main/main.c:1196-1288`; `main/Kconfig.projbuild:457-491`; README § "Training the counter".
- **Status:** unverified on hardware

### D-99 Top eigenvalues by power iteration plus a 2×2 Rayleigh-Ritz step, not a full Jacobi
- **Decision:** `gait_id` and `count_est` use `dsp_top_eig` (at most 30 iterations, early exit at 1e-6). `count_est` deflates for λ2 and refines both with a Rayleigh-Ritz step on span{v1, v2}. The start vector is A·s, with s_k the sign of (column k)·(column kmax). `dsp_sym_eig` (cyclic Jacobi) is kept only as the exact reference the tests check against; the linker discards it from the firmware.
- **Why:** an 8×8 Jacobi is about 20k soft-float operations, and only the top one or two eigenvalues are needed. Rayleigh-Ritz fixes the pull-together of λ1 and λ2 when two movers are similarly strong. The obvious fixed starts (all-ones, the diagonal) can be exactly orthogonal to the top eigenvector when a mover raises some groups and lowers others.
- **Rejected:** a full Jacobi in firmware; all-ones or diagonal start vectors.
- **Evidence:** instrumented host operation counts, not timed on the C5: count block about 19–24k operations with Jacobi → about 6–7k; gait window about 102k → 58k at 100 Hz. Without Rayleigh-Ritz, 0.873 read as 0.885 on a synthetic two-mover block. Over 20,000 random Wishart matrices the chosen start was near-orthogonal (|cos| < 0.01) in 0.085 % of cases, all-ones in 2.2 %.
- **Where:** `main/csi_dsp.h:74-158`; `main/count_est.c:24-63`, `:157-205`; `main/gait_id.c:57-66`, `:107-139`.
- **Status:** unverified on hardware

### D-100 Cadence from a time-domain autocorrelation; pair-average above 60 Hz
- **Decision:** cadence = 1/lag of the highest local maximum of the biased linear autocorrelation of the detrended, un-windowed principal-component signal, over lags 0.4–1.4 s, computed directly, with parabolic refinement. Above 60 Hz it runs on pair averages `y(k) = (x(2k) + x(2k+1)) / 2`.
- **Why:** at 100 Hz a 1.4 s lag is 140 samples, more than the 128 a circular 256-point FFT autocorrelation can represent. The biased form tapers later peaks, so the first period beats its multiples. Pair-averaging cuts the cost about 4× (about 32k → 9k operations at 100 Hz) with gain above 0.99 on the 0.7–2.5 Hz rhythm. 50 Hz stays at full rate, because halving it would give a 40 ms lag grid and a 12.5 Hz band edge.
- **Rejected:** FFT-based (circular) autocorrelation; decimating at 50 Hz.
- **Evidence:** host operation counts; not timed on the C5.
- **Where:** `main/gait_id.h:39-56`; `main/gait_id.c:260-346`.
- **Status:** unverified on hardware

### D-101 `vitals_of` is a carried-over claim, not a measurement
- **Decision:** `vitals_of` = the last person recognised walking in **alone** during this visit and not contradicted since. It is reported only beside rates that are reported at all: PRESENT, count ≤ 1, latch PERSON, and not while a lapse's carry is waiting for the breathing hold (D-108). It needs recognition and vitals both enabled.
- **Why:** recognition needs a person walking and vitals need a person sitting still, so the two never happen at the same moment and whose chest it is can never be measured. One 1×1 link cannot separate two seated people's breathing, and the counter only sees movers. The user asked on 2026-09-25 for the "heart beat of the individual person".
- **Rejected:** simultaneous heart rate for several people (no 1×1-link result exists).
- **Evidence:** documented residuals that cannot be detected: someone who walks in and sits before being recognised mixes into the latched person's reading; with an untrained counter, "multiple" comes only from two names, so two unrecognised seated people get one rate; a fan passing for breathing within 60 s of someone leaving keeps their name. Synthetic e2e: until the gate notices a walk, analyses are credited to the seated person (1 extra sample in one room, 7 in one 100 Hz room during a 29 s walk; the logged rate stayed within ±1).
- **Where:** `main/person_vitals.h:6-19`; `main/main.c:1497-1503`; README § "Whose vitals: `vitals_of` and the per-person vitals log".
- **Status:** unverified on hardware

### D-102 Latch: the first name wins; a second different name means MULTIPLE
- **Decision:** the latch states are NONE / UNKNOWN / PERSON(s) / MULTIPLE, and the visit's first slot is remembered. A decisive tally for slot s latches PERSON(s) if s is the first name (or equals it); any **other** slot gives MULTIPLE. "unknown" moves PERSON to UNKNOWN but keeps the first slot, so A → unknown → A recovers while A → unknown → B is MULTIPLE. The latch is fed on every decisive vote, not only when the printed label changes. A slot with an unusable name latches UNKNOWN but keeps its slot.
- **Why:** the log persists per person. With replacement, "A seated, B walks in and sits" would write the mixed signal into B's history. Blocking costs only a false null (for example A left quietly and B arrived within one presence), which is the safe direction.
- **Rejected:** "the latest recognised name wins" and "unknown never clears" (an alternative in the 2026-09-25 design notes, not kept); letting the latch follow the printed label (mixed breathing logged under B).
- **Evidence:** `test_pvitals` sections (a)–(l).
- **Where:** `main/person_vitals.c:342-376`; `main/person_vitals.h:21-40`; `main/main.c:1363-1383`.
- **Status:** unverified on hardware

### D-103 MULTIPLE is sticky until the room has been empty; a count going from 2 to 1 changes nothing
- **Decision:** `pv_on_count(≥ 2)` sets MULTIPLE (committing any pending episode first). A count below 2 changes nothing. Only a release clears it: the room empty for longer than the carry limit, movement while absent, no-CSI, recalibration, a new baseline or a session start.
- **Why:** nobody can tell who stayed. Seated people are never counted, so "one mover now" cannot mean the second person left.
- **Rejected:** returning rates after 2 → 1 with `vitals_of` null (2026-09-25 design notes, not kept).
- **Evidence:** consequence: `vitals_of` can stay null and rates stay withheld for hours while two people share a room.
- **Where:** `main/person_vitals.c:378-385`; `main/person_vitals.h:14-16`, `:34`; `main/main.c:1277-1285`.
- **Status:** unverified on hardware

### D-104 Credit the vitals log once per completed analysis, under strict conditions
- **Decision:** `pv_analysis` runs only when `vitals_push()` returns true (about every 2 s), never per packet or per JSON line. It credits only if present, count ≤ 1, latch PERSON, vitals state ok with valid breathing, and breathing in (0, 200]. Heart rate is added only if valid and in range.
- **Why:** a completed analysis is the natural sample. The return value of `vitals_push` used to be thrown away. Range tests written as `v > 0 && v <= 200` reject NaN and both infinities without `isfinite()` (D-94).
- **Rejected:** crediting per packet or per JSON line.
- **Evidence:** `test_pvitals`.
- **Where:** `main/person_vitals.c:439-483`; `main/main.c:2977-2988`.
- **Status:** unverified on hardware

### D-105 Average per still episode: at least 3 samples to commit; a running mean that becomes an EWMA after 32 episodes
- **Decision:** the samples of one still spell are averaged into one value per rate. An episode with fewer than 3 breathing samples (`PV_EP_MIN`) is discarded. The episode value is folded into the record with k = min(episodes + 1, 32): an exact mean up to 32 episodes, then an EWMA with alpha 1/32. A heart episode commits only inside a committed breathing episode.
- **Why:** consecutive analyses share about 28 of their 30 s of data, and heart rate is already a median of 5, so they are not independent. Folding each one in would weight a long sit by its length and claim a precision nobody measured. A 2-sample episode would count as much as an hour, and the first analyses of a spell are the least settled. The EWMA follows slow drift instead of freezing. "Last … ago" is uptime only; the board has no clock.
- **Rejected:** a per-analysis running mean capped at 1,800 samples; a per-sample mean with a saturating count (both 2026-09-25 design alternatives, not kept).
- **Evidence:** mutation check, 2026-09-25: 15 single-rule mutations (lower `PV_EP_MIN`, no EWMA cap, …), 14 caught by `test_pvitals`; the missed one changes nothing observable.
- **Where:** `main/person_vitals.c:12-20`, `:111-131`, `:147-190`; `main/person_vitals.h:42-56`; README § "Whose vitals: `vitals_of` and the per-person vitals log".
- **Status:** unverified on hardware

### D-106 Episode boundaries; deleting a slot discards its pending episode
- **Decision:** a pending episode ends when the gate says moving, when the vitals buffer restarts (`still_s` falls, strict `<`), on any latch change (committed first) and on a release. `pv_slot_gone` and `pv_log_clear` **discard** a pending episode for that slot instead of committing it. A pending episode belongs to (slot, name); at commit, a record under a different name is re-initialised, never merged. A counter about to overflow (about 36 h of one still spell) commits early.
- **Why:** samples belong to the latch they were credited under, and committing before a delete would resurrect the deleted person's data. The `<` is strict because `still_s` plateaus at 51.2 s in a long spell; a restart landing on exactly the old value is missed, but the gate restarts MOVING on any gap over 1 s, before vitals' own gap limit, so the episode has already ended.
- **Rejected:** none recorded.
- **Evidence:** `test_pvitals`.
- **Where:** `main/person_vitals.c:21-27`, `:192-216`, `:414-437`, `:485-512`.
- **Status:** unverified on hardware

### D-107 Carry the name and the count across a presence lapse, for at most 60 s
- **Decision:** when presence ends and the breathing hold is compiled in, the latch and the pending episode are kept (absent = true). While absent, the gate saying MOVING releases them, and so does an absence longer than `CSI_CARRY_US` = 2 × `CONFIG_CSI_VITALS_WINDOW_S` seconds (60 s). Nothing is credited while absent. With `CSI_CNT_CARRY` (count, vitals and the hold all on), the count is read *before* `analytics_reset(false)` and stored as `s_cnt_carry` *after* it, dropped on MOVING or after the same limit, and cleared by `cnt_votes_clear()`. Without the hold, presence end releases.
- **Why:** a seated person nearly always lapses first (5 s hold against about 20 s to a breathing estimate), and without the carry their name was lost and never came back, because a seated person casts no votes. Presence end also wipes the count and seated people never vote a count again, so two people who sat down would return as count 1 while vitals still said "multiple". Clearing the carry in `cnt_votes_clear()` stops a stale count coming back after `count-reset`, a session start, no-CSI or recalibration. After 60 s the room is taken to have been empty.
- **Rejected:** accepting count 1 beside vitals "multiple" (2026-09-25 design alternative, not kept).
- **Evidence:** synthetic e2e e1 (room 5000): presence lapsed once for 16 s, breathing restored it, and every present packet named A. e2: after a lapse the count came back as 2; from then on 5,776 present packets were all "multiple", with no name and 0 credits.
- **Where:** `main/main.c:197-221`, `:423-434`, `:911-923`, `:2852-2857`; `main/person_vitals.c:387-437`; `main/person_vitals.h:35-40`, `:111-116`.
- **Status:** unverified on hardware

### D-108 Hand the carried name and count back only when the breathing hold asserts PRESENT
- **Decision:** at every presence start `s_carry_wait` is set and the lapse continues for what it carries. On the first packet where the breathing hold is true, the wait ends: `pv_presence_start` runs and the count is restored. If the gate calls MOVING first, both are let go. While waiting, `vitals_of` is null and nothing is credited. Replaces D-109.
- **Why:** Step 08's flag has no hold, while the gate smooths for 0.5 s and then holds 0.3 s, so on a walk-in Step 08 fires **first**. Handing the carry back at Step 08's start gave a newcomer the name and count of whoever had just left. A valid hold means the still spell that began PRESENT is unbroken: any MOVING packet restarts vitals, and a breathing rate then needs about 20 s of still data again.
- **Rejected:** D-109; a fixed probation window after Step 08's start (the reviewer's option): the gate's delay ranged 0.42–3.50 s, so a 3 s window would have failed two of the synthetic rooms.
- **Evidence:** 2026-09-25 review finding 1 (high): in the reviewer's run B's sitting was reported as A on 100 % of B's present packets (4,042/4,042), 35 analyses were credited, and A's log took B's 17.96/min. Step 08 fired 41–740 ms after B started; the gate 0.4–3.5 s after Step 08, or not within a 6 s walk. After the fix, new e2e scenario e3 passes in all 9 rooms; with the old rule restored in a copy it fails 4 checks in each of rooms 5000, 5003 and 5005.
- **Where:** `main/main.c:206-214`, `:436-473`, `:2909-2951`; `main/person_vitals.h:191-205`; `test/test_e2e.c:1774-1787` (the carry wait in `gl_packet`, on 2026-11-01; `:1805-1816` when written, no longer that code).
- **Status:** unverified on hardware

### D-109 End the lapse at any presence start (replaced)
- **Decision:** the original design called `pv_presence_start` at every presence start and relied on "the gate says MOVING while absent" to catch a walk-in.
- **Why:** it assumed a walk-in's gate and Step 08 fire on the same packet. That is false: Step 08 fires 0.4–3.5 s earlier (D-108).
- **Rejected:** n/a.
- **Evidence:** implemented and replaced on 2026-09-25 after review finding 1.
- **Where:** replaced by `main/main.c:2909-2951`.
- **Status:** superseded by D-108

### D-110 Behaviour changes that came with D-108 and were not signed off
- **Decision:** accepted as the safer direction, but recorded as **not signed off**: (1) a seated person whom Step 08 re-detects during a lapse keeps the latch, but `vitals_of` is null and nothing is credited until breathing takes over; before, the name came back at once. (2) A fidget during that waiting window ends the visit, because the gate cannot tell a fidget from a walk-in; for a "multiple" visit, two seated people who fidget then get one mixed rate and no name. (3) If Step 08 alone restored presence and breathing never becomes valid, the name and count are dropped 60 s after the lapse began.
- **Why:** judged safer than naming a newcomer with a departed person's name.
- **Rejected:** none recorded.
- **Evidence:** remaining gap: a walk-in the gate **never** calls MOVING still inherits the name; nothing can tell it apart from a still person (documented in the README).
- **Where:** `main/main.c:450-455`; README § "Whose vitals: `vitals_of` and the per-person vitals log".
- **Status:** unverified on hardware

### D-111 The per-packet call order in the detect phase
- **Decision:** gate → breathing-hold bookkeeping and `vitals_push` → `pv_packet` and the count-carry drop → presence transition (`pv_presence_end` / `s_carry_wait`) → carry hand-back → `count_step`, `id_step` → `pv_analysis`. All latch timing uses `r.t_us` (packet arrival, from `esp_timer`); saves and commands use `esp_timer_get_time()`.
- **Why:** movement on the packet that ends a lapse must still count as movement while absent; the other order keeps a name the latch should release, which `test_pvitals` confirms. `pv_analysis` runs last so present, count and latch are all this packet's.
- **Rejected:** calling `pv_packet` after the presence transition.
- **Evidence:** `test_pvitals` order test; `test_e2e`'s copy of the order matched `main.c` on 2026-09-25.
- **Where:** `main/person_vitals.h:200-211`; `main/main.c:2834-2851`, `:2977-2988`.
- **Status:** unverified on hardware

### D-112 Sessions: one at a time, only while detecting; a cancel discards and saves
- **Decision:** `enroll` and `count-train` start only in the detect phase with no session running. `stop` or a recalibration cancels: the class being filled is cleared and the model saved (ARCHITECTURE §3.6 lists what else a cancel clears). `enroll` start clears the gait window, both vote rings and the slot's vitals-log record, and releases the latch; `count-train` start clears the count block and both vote rings and releases the latch, but does not reset the gait window; finishing `count-train` resets the half-built gait window. Nothing is classified while a session runs.
- **Why:** the class was already wiped in RAM when the session started, so without the save a reboot would resurrect it. No earlier estimate may linger in the outputs. A half-built window holds training data, possibly of two people, that must not be classified.
- **Rejected:** none recorded.
- **Evidence:** not run on a board.
- **Where:** `main/main.c:351-360`, `:1044-1075`, `:1224-1229`, `:1683-1700`.
- **Status:** unverified on hardware

### D-113 What each command does to the latch and the log
- **Decision:** `enroll` start: `pv_slot_gone(slot)` + `pv_release` + an immediate log save (D-124), even when the same person re-enrols. `enroll` cancelled: `pv_slot_gone`. `count-train` start: `pv_release`. `forget`: `pv_slot_gone` + immediate save. `vitals-reset`: `pv_log_clear` (latch and templates kept) + immediate save, allowed during a session. `analytics_reset(true)`: `pv_release`.
- **Why:** the template **is** the identity, so a slot's new owner must not inherit the old owner's history, and nobody is attributed during a session (a `count-train 2` session is two people by definition). `vitals-reset` touches no template, so it is safe mid-session.
- **Rejected:** clearing the slot on `enroll` only if the name changed (2026-09-25 design alternative, not kept).
- **Evidence:** `test_pvitals`.
- **Where:** `main/main.c:1056-1058`, `:1873-1885`, `:1937-1946`, `:1964-1984`, `:2000-2004`; `main/person_vitals.c:485-512`.
- **Status:** unverified on hardware

### D-172 A coarse activity state (empty / still / restless / moving) and presence episodes, from decisions Step 10 already makes
- **Decision:** (2026-10-16) Step 10 derives a room activity state and presence-episode counters from per-packet decisions it already has: `present` after the breathing hold, the motion gate's `still` and `hf` ratio, and whether the hold asserted PRESENT. No new signal, no FFT, no `sinf`/`expf` (`main/activity.h`, ALGORITHM). Packets are summed into 1-second bins by timestamp (not packet count). A second is ACTIVE when at least 1/4 of its packets are gate-MOVING or its mean `hf` is at or above the gate threshold, FIDGET when its mean `hf` is in [`CSI_ACTIVITY_RESTLESS_X100`/100, gate threshold), else QUIET; packets with `hf` < 0 (gate floor unknown) count towards neither. Raw state each second: `empty` after 5 s without a PRESENT packet; `moving` with ≥ 2 ACTIVE seconds in the last 5; `restless` with ≥ 2 seconds in the last 10 that are FIDGET, or ACTIVE within the last 5; `still` when all of the last 10 s are known and none of the above; PRESENT with the gate floor not yet known gives no state (JSON `null`). A new state must hold 2 s (hysteresis); a timestamp gap over 1 s discards the ring and the state. **Episodes:** from the first PRESENT second to the last; a lapse that the breathing hold undoes within `CSI_CARRY_US` (60 s by default; 0 when `CSI_VITALS_HOLDS_PRESENCE` is off) does not split one, while a gate-MOVING packet during the lapse or the wait after it ends it (the same rule as the count carry and the whose-vitals latch, D-108); `act_reset` from `analytics_reset(true)` (no-CSI, recalibration, new baseline) ends it, presence end does not. Output: JSON `"activity"` (`null` / `"empty"` / `"still"` / `"restless"` / `"moving"`), `"activity_s"`, `"episode_s"`, appended after `vitals_of` and `null` when switched off (D-128); the same three in the EXPLAIN `vitals` event; a line in `status` and `explain`; console `stats` (this episode's seconds per label, the last one, the count since boot). Kconfig `CSI_ACTIVITY_ENABLE` (bool, default y, depends on `CSI_VITALS_ENABLE || CSI_ID_ENABLE || CSI_COUNT_ENABLE`, the Kconfig form of `CSI_HAVE_GATE`) and `CSI_ACTIVITY_RESTLESS_X100` (int 250, range 100-5000). Episodes live in RAM only. State `act_t` is 216 B static. Step 08 is unchanged.
- **Why:** it gives an activity output without new sensing claims or training data: every input is already computed, so the cost is a few adds per packet and one short loop per second (activity.h header). The research of the same day (D-173) found no single-node evidence for posture or trained activity classes on an FPU-less chip, so only a coarse state from motion energy is supportable. Seconds, not packets: at 50 Hz the gate called only 26-31 % of a synthetic walker's packets MOVING in two of nine `test_e2e` rooms (TESTING §3.3 item 2), so a per-packet label would flicker; a per-second duty does not. "still" is honest only while PRESENT holds, so the state follows the post-hold presence: for a motionless person a lasting `still` is, in practice, PRESENT held by the breathing hold (activity.h, WHAT "still" MEANS IN PRACTICE).
- **Rejected:** (a) **sitting / standing / lying**: nothing in the repo supports posture from 26 amplitude tones of one link; the one on-ESP32 activity result surveyed (Armenta-Garcia et al., Sensors 2025;25(19):6220) used two ESP32s, labelled training data and an ESP32-S3 with an FPU (COMPARISON §2D). (b) **A trained (ML) activity classifier**: no labelled data from any room exists yet; collect TESTING §4 phase1 logs first. (c) **Fall or sudden-transition detection**: no fall or posture source in REFERENCES.md, Step 08 itself calls a motionless person unreliable ("do not depend on it", `step08_final_human_detector/main/main.c:26-27`), and gait's 80 %-moving gate means transitions yield no gait features. (d) **Per-packet labels**: the flicker above. (e) **The research plan's restless rule** (restless when at least 1 of the last 5 seconds has mean `hf` in [lo, threshold)): replaced by 2 seconds in the last 10 (`ACT_FIDGET_MIN_S`, `ACT_QUIET_S`), with ACTIVE seconds counting only inside the 5 s "moving" window. The second part is recorded: activity.h says it is so that "a walk that ends goes to still, not through restless", the review's defect 2 (TESTING §5.2, 2026-10-16). Why 2 of 10 rather than 1 of 5 was not recorded. (f) **Episodes or an activity field in NVS**: the per-person record `pv_rec_t` has no such field, and adding one means a `PV_LOG_VERSION` bump (`main/person_vitals.h:76`); not now. (g) **Adding it to Step 08**: Step 08 stays the unchanged detector the first hardware session benchmarks (as D-167 kept tracking out of it).
- **Evidence:** synthetic and build only, 2026-10-16 (TESTING §5.2 rows of that date): Ubuntu v6.1 build 0xf56d0, 0 warnings; `test_activity` 57 checks and `e2e` 242 (9 suites, 11,515 checks, gcc `-Werror`); 368 Kconfig combinations clean. `e2e` `e4` at seed 5000 reports, not gates, the agreement with each segment's truth: walk-in 90.1 %, seated breathing 90.0 %, fidgeting **35.4 %** (62.1 % read `still`), walking 100.0 %, empty room 71.9 %. A review from four angles found 4 defects, all fixed; of 38 mutants of `activity.c`, 34 were caught and 4 survive (TESTING §3.3 item 8). The thresholds are synthetic: real walking has never been measured in `hf` (D-83), and a seated chest reached `hf` 2.33 in a synthetic test, so `restless` and `still` can overlap. MSVC has not run the new suite. **Never run on the C5.** How a session would score it: TESTING §2.7, §4 Phase 1. 2026-11-01, offline replay (another group's ESP32-S3 recordings, not the C5; `activity.c` compiled unchanged and fed by a port of Step 08's presence with no breathing hold; TESTING §5.3): seated read `still` on 61-68 % of the laboratory's seated time but 19-23 % of the residence's, where there was no state at all (JSON `null`) for 52-57 % of the seated time, because residence links had a median 84 (T0003) and 54 (T0004) gaps > 1 s (`ACT_GAP_US`) per 750 s against 2-3 in the laboratory (recount 2026-11-01, `~/ESP32C5_CSI_logs/2026-11-01_docs/gap_recount.txt`); walking read `moving` on 63 % (raw) to 90 % (mean) in the laboratory. Synthetic, 2026-10-30 (`test_e2e` f-state, assumed magnitudes): a gain-state response left `empty` on 1.7 % of an empty room (reference 100 %). Neither is a measurement on the C5. 2026-11-03 (D-176): `agc`, `fft`, `gain_chg` are now appended after `episode_s`; the activity keys, their order and `activity.c` are unchanged, and the matrix is 552 combinations (D-145).
- **Where:** `main/activity.c`, `main/activity.h`; `main/main.c` (on 2026-10-16): `CSI_HAVE_ACTIVITY` `:324-331`, `s_act` `:461`, `act_reset` in `analytics_reset` `:1157-1162`, `report_build` `:1807-1813`, JSON `:1897-1913`, `status` `:2724-2731`, `explain` rules line `:3074-3084`, `cmd_stats` `:3107`, `act_push` `:4223-4232`, `act_init` `:4696-4713`; `main/Kconfig.projbuild:493-540`; `main/CMakeLists.txt`; `test/test_activity.c`; `test/test_e2e.c` (`act_push` in `gl_packet` `:1794` on 2026-11-01, `:1491` on 2026-10-16; `e4_activity`); `test/run_tests.ps1`, `test/run_tests.sh`; `tools/presence_monitor.py`, `tools/dashboard.html`, `tools/session_score.py` (ACTIVITY); `tools/kconfig_matrix.py` (THE MATRIX).
- **Status:** unverified on hardware

### D-175 Vitals fill the empty 100 ms bins of a short gap by a straight line toward the packet that ends it (F2; supersedes D-85's repeat rule)
- **Decision:** (2026-11-03, D-174 proposal (1), on the user's go-ahead) when a packet lands `nb` ≥ 1 bins after the start of the open bin, `vitals_push` closes that bin and pushes its mean, then pushes, for b = 1 … nb − 1, `last_val[c] + b/nb × (na[chan[c]] − last_val[c])` for each channel c (the value of that channel's own subcarrier in the packet that ends the gap), and opens the bin that contains the packet. The reset test runs first and is unchanged: a packet more than `VIT_MAX_GAP_US` + one bin after the open bin's start (a packet gap of 1.0-1.1 s or more, depending on where in its bin the last packet fell), a clock running backwards, or any moving packet throws the buffer away, so a line spans at most `VIT_MAX_GAP_US / VIT_BIN_US + 1` bins and never a reset. Binning by arrival time, the 12 spread channels, the one-FFT-per-packet rule (D-87) and every reporting criterion (D-88) are unchanged.
- **Why:** D-85's repeat rule froze a breathing swing into a step over a 0.3-0.9 s silence, and the step's spectrum leaked into the low heart band. In `test_vitals` (12b) at the shipped seed base that gave 6 / 12 / 23 wrong heart values with a heart at ×0.50 / ×0.25 / none (49.8-52.5, 49.8-60.9, 49.2-56.2 bpm; D-174, TESTING §3.3 item 10). A straight line between the two measured ends adds no step. The fill **reduces** the ~50 bpm phantom; it does not remove it (vitals.c comment; review defect 1).
- **Rejected:** (a) **Keep repeating** (D-85): the phantoms above; over 52 seed bases the repeat loop gives 889 wrong heart values in 332,374 silence analyses (2.7 per 1,000, up to 14.2 per 1,000 at one base), D-175 gives 1. (b) **Applying F2 on the 2026-10-30 evidence alone** (D-174 Rejected (a)): it was applied only after a run against the reviewed suite and a seed-base sweep (Evidence). (c) **Checking the (12b) silence rows as "no wrong value"** (the first pass): the result depends on the seed base (base 1770 gives one wrong value, 52.4 bpm with no heart), and at 14 of the 52 bases the repeat loop also stays under the bound (`sweep12b/SUMMARY.txt`), so the rows check a rate of at most 2 per 1,000 analyses and (6b) pins the fill itself (review defect 1). (d) **(6b) with one value for every subcarrier** (the first pass): it could not see a line toward the wrong subcarrier's value; each subcarrier now gets its own value (review defect 2).
- **Evidence:** synthetic only, 2026-11-03 (TESTING §5.2 rows of that date; Ubuntu, gcc 15.2.0 `-Werror`). First pass: `test_vitals` 2,499 → 2,508 checks, KNOWN LIMIT lines 10 → 7 (the three silence lines gone); 0 wrong heart values in the silence rows at 8 seed bases (1700, 2700 … 8700; `~/ESP32C5_CSI_logs/2026-11-03_impl/sweep12b/`); the repeat loop restored: 4 FAILs (`~/ESP32C5_CSI_logs/2026-11-03_impl/mutation/`). After the review (`~/ESP32C5_CSI_logs/2026-11-03_impl/after_review/`, `REVIEW_RECORD.md`): (6b) gives subcarrier k the value base × (1 + 0.01k) and checks each channel against the line of its own subcarrier `chan[c]`, after checking that some `chan[c]` ≠ c; all 9 mutants of the fill (`chan0`, `frb1`, `frnb1`, `half`, `mid`, `next`, `nofill_closed_after`, `revert`, `wrongch`) fail at `test/test_vitals.c:702` (`mutation/mutants_summary.txt`); the 52-base sweep gives D-175 1 wrong value in 332,374 silence analyses (base 1770 only) and 0 in every no-silence row (`sweep12b/SUMMARY.txt`); `test_vitals` 2,509 checks, 0 failures, 7 KNOWN LIMIT lines, none from (12b) at the shipped base. `test_e2e` output is byte-identical to the 2026-11-01 run (`~/ESP32C5_CSI_logs/2026-11-01_docs/host_tests_rerun.log`); e2e seeds 5000-5008 with F2: 5004 and 5007 fail only e1 (273 checks), the others 287 / 0 (`~/ESP32C5_CSI_logs/2026-11-03_impl/seeds/`; the review changed only comments in `vitals.c` and the `vitals.h` PIPELINE wording, review defect 5). Says nothing about a real chest or a real link.
- **Where:** `main/vitals.c:375-383` (reset test, unchanged), `:389-413` (gap loop and its comment); `main/vitals.h:16-26` (PIPELINE item 1; `:64` still reads "longer gap -> restart" beside `VIT_MAX_GAP_US`, which stays true); `test/test_vitals.c:653-713` ((6b) `test_gap_fill`), `:1236-1316` ((12b) `test_hr_rate_weak`, rate bound `:1305`, KNOWN LIMIT `:1308-1312`), all on 2026-11-03; TESTING §3.2, §3.3 item 10, §5.2.
- **Status:** unverified on hardware (synthetic evidence only). Known risks: the line runs toward **one packet's** value (`na[chan[c]]`), not a bin mean, so a noisy or outlying packet at the end of a gap tilts every filled bin; and up to about a second of filled values is still made-up data, smoother than the repeat rule's but not measured. The remaining phantom (1 per 332,374 here) shows the fill is not a cure. How real C5 gaps (lengths, how often) compare with the synthetic 0.3-0.9 s silences is unknown.

---

## 5. Persistence (NVS)

### D-114 `csi_baseline_t`: one calibration record, stored as one NVS blob, shared by Steps 06–08 and 10
- **Decision:** the struct holds magic `0x43354253` ("C5BS"), version 2, the shape (`csi_len`, `nsub`, `fwi`, `channel`), `n_selected`, `rate_hz`, per-subcarrier `mu`/`sigma`/`selected[384]`, and `m_mean`/`m_std`/`d_mean`/`d_std` (about 3.5 KB). It lives in NVS namespace `csi`, key `baseline`. A wrong size, magic or version on load returns `ESP_ERR_INVALID_VERSION`. The struct is kept static, not on the stack. `csi_baseline.h` is copied, identical, into Steps 06, 07, 08 and 10; Step 10 loads a baseline Step 08 wrote.
- **Why:** calibrate once in an empty room and reload on every boot. A baseline from a different shape (another router, channel, HT vs HE) is meaningless, so the shape fields let later steps check it and refuse it.
- **Rejected:** none recorded.
- **Evidence:** the four copies were identical by `diff` on 2026-09-25. Two discrepancies: `step06_baseline/main/main.c:246` says "~2 KB" (the struct is about 3.5 KB), and, until D-168 (2026-09-28), Step 08's reuse check compared length, subcarrier count, first-word flag, `n_selected >= 8` and `m_std > 0` but **not** the channel; it now also requires the stored and live channels to match, with 0 on either side counting as not known (`step08_final_human_detector/main/main.c:442-444`). Until 2026-09-28 the README listed "channel changed" as detected (A-22, now closed: since D-168 a channel mismatch at shape lock recalibrates).
- **Where:** `step06_baseline/main/csi_baseline.h:1-47`; `step06_baseline/main/main.c:273-282`; `step07_presence/main/main.c:168-184`; `step08_final_human_detector/main/main.c:200-227`, `:436-444`.
- **Status:** unverified on hardware

### D-115 Step 07 refuses to run without a baseline; Step 08 calibrates itself
- **Decision:** Step 07 loops forever logging "Run Step 06 first" if the baseline will not load, and warns (but runs) on a degenerate one (`m_std <= 0` or fewer than 8 subcarriers). Step 08 runs the full calibration after a leave-the-room countdown when there is no baseline or it does not match; with a valid one it goes straight to LOCK → SETTLE → DETECT.
- **Why:** Step 07 "refuses to guess if it is missing or was made for a different CSI shape". Step 08: "a brand-new board just works".
- **Rejected:** none recorded.
- **Evidence:** not run on a board.
- **Where:** `step07_presence/main/main.c:602-627`; `step08_final_human_detector/main/main.c:432-477`, `:1050-1059`.
- **Status:** unverified on hardware

### D-116 Recalibration: automatic on a mismatch, a 2 s button hold, `CSI_FORCE_RECALIBRATE`, or erasing NVS
- **Decision:** Step 08 recalibrates when the stored shape (length, subcarrier count, first-word flag) no longer matches, for example after a router change or toggling `CSI_LLTF_ONLY`; since 2026-09-28 the channel is part of the match too: a mismatch at shape lock recalibrates (D-168; D-167's warning is superseded; A-22 closed). Manual routes: hold `CSI_BUTTON_GPIO` low for over 2 s (input with internal pull-up, polled only while detecting; it resets the decision state and EMAs, then goes to LEAVE); build with `CONFIG_CSI_FORCE_RECALIBRATE=y` (then set it back to `n` and reflash, or every reboot recalibrates); or erase NVS. Step 10 adds the console `recal` (D-134).
- **Why:** a baseline from another shape gives nonsense thresholds. The button is assumed active-low with a pull-up, "how a BOOT-style button is normally wired". The phase is tested first so the hold timer advances only while detecting; otherwise a hold crossing 2 s during calibration would latch and be swallowed. The EMAs are re-seeded because the old room state would start phase A from a warm but wrong reference. `FORCE_RECALIBRATE` is "the board-independent way".
- **Rejected:** none recorded.
- **Evidence:** not run on a board. See D-114 on the channel check. 2026-09-28 (TESTING §5.1): a channel-1 baseline was used on channel 6 with no warning (that image predates D-167). D-167 added a warning at lock the same day, and D-168 replaced it with a recalibration; neither has run on a board.
- **Where:** `step08_final_human_detector/main/main.c:258-292`, `:394-415`; `step08_final_human_detector/main/Kconfig.projbuild:45-54`, `:161-170`; README § "Recalibrating".
- **Status:** unverified on hardware

### D-117 Never `erase-flash` between Step 06 and Steps 07/08
- **Decision:** use plain `flash`, which keeps NVS. `erase-flash` is the "nuclear" way to force a recalibration. Ask which step is going on the board before flashing, because Steps 06 and 08 write the baseline.
- **Why:** `erase-flash` wipes the baseline (and Step 10's models and vitals log) from NVS.
- **Rejected:** none recorded.
- **Evidence:** reasoning; the IDE task is labelled "Erase flash (wipes the saved calibration)". 2026-09-28 19:00 (TESTING §5.1): after a plain `idf.py -p COM13 flash` of Step 10 0xef360, the boot captured after the firmware's own restart printed `baseline found: 106 bytes CSI, 26/26 subcarriers, ch 6, 50.2 Hz`, the same line the previous image printed at 18:57, so the stored baseline survived that flash on the C5 (n = 1).
- **Where:** `step07_presence/main/main.c:607-608`; `.vscode/tasks.json:157`; README § "Recalibrating".
- **Status:** active

### D-118 NVS init erases and retries on `NO_FREE_PAGES` or `NEW_VERSION_FOUND`
- **Decision:** `nvs_flash_init()`; on `ESP_ERR_NVS_NO_FREE_PAGES` or `ESP_ERR_NVS_NEW_VERSION_FOUND`, `nvs_flash_erase()` and init again.
- **Why:** "NVS is required by the WiFi driver to store calibration data." Pitfall: this path erases the whole default NVS partition, including the CSI baseline, Step 10's models and the vitals log. Why that loss is accepted: not recorded.
- **Rejected:** none recorded.
- **Evidence:** standard ESP-IDF boot sequence.
- **Where:** `step02_wifi_sta/main/main.c:168-174`; `step08_final_human_detector/main/main.c:1039-1044`.
- **Status:** active

### D-119 Step 10's models and vitals log live in NVS beside the baseline; models carry their CSI shape and channel
- **Decision:** NVS keys `id_model` (gait templates), `cnt_model` (count model) and `vit_log` (per-person vitals). When a session completes, the model is stamped with `csi_len` and channel, once per model (a per-class record would need a new `NCC_VERSION`). A mismatch at boot or on a channel change only warns. `channel_follow` adopts a new channel after 100 consecutive accepted packets on it. A recalibration keeps the templates and count model and says so. The calibrated packet rate is stored with the baseline, and a live rate more than 20 % off warns (D-48).
- **Why:** a template describes how a person disturbs **this** link at **this** rate. Moving the board, router or furniture, or changing channel or ping interval, needs re-enrolment and counter retraining; the model is still used (the user may have reasons) but the user is told. The 100-packet rule means a stray frame during a reconnect scan cannot flip the channel.
- **Rejected:** none recorded.
- **Evidence:** not run on a board.
- **Where:** `step10_recognise_count_vitals/main/main.c:991-1023`, `:1122-1180`; `step10_recognise_count_vitals/main/csi_ncc.h:45-46`; README § "Before recognising anyone: raise the packet rate".
- **Status:** unverified on hardware

### D-120 `vit_log` format: "PVL1", version 1, four 40-byte records = 168 bytes, no CRC of its own
- **Decision:** `pv_log_t` = magic `0x314C5650` ("PVL1"), version 1, `nslots` 4, then 4 × `pv_rec_t` (name[16], breathing and heart episode counts, samples, last and mean breathing, last and mean heart). A `_Static_assert` fixes the layout (40-byte record, no padding); name copies are zero-filled. Namespace `csi`.
- **Why:** the struct is written to NVS byte for byte, so its layout **is** the format, and identical logs must be identical blobs. The name is stored per record so a slot's owner can be checked at boot (D-122). No separate CRC because NVS already CRC32-checks every entry and blob chunk, the same trust model as the models and the baseline.
- **Rejected:** 44-byte entries with visit counts and last-seen times, a 184-byte blob (2026-09-25 design alternative, not kept).
- **Evidence:** the size is exactly 8 + 4 × 40 = 168 bytes; comments that say "~170 bytes" are approximate (A-23).
- **Where:** `step10_recognise_count_vitals/main/person_vitals.h:72-109`; `step10_recognise_count_vitals/main/person_vitals.c:43-46`, `:84-93`.
- **Status:** active

### D-121 Load all-or-nothing; the writer guarantees its own log always validates
- **Decision:** `vit_log_load` follows the model loader's pattern. A bad size, magic, version or `nslots`, a name outside the alphabet or unterminated, counts under an empty name, heart episodes above breathing episodes, or any non-finite or out-of-[0, 200] number leaves an **empty** log, never a partly trusted one. The key is not erased; the next save overwrites it. The writer bounds values on the way in, keeps the running mean between the old mean and the new value, and saturates counters.
- **Why:** a single bad number would otherwise make the next boot discard **everyone's** log (one ulp past 200 fails validation). Not erasing leaves the NVS content alone until a real change is saved.
- **Rejected:** partially trusting a damaged blob.
- **Evidence:** `test_pvitals` (l): 240,000 random events, and the log passes `pv_log_valid` after each one (2026-09-25).
- **Where:** `step10_recognise_count_vitals/main/main.c:649-671`; `step10_recognise_count_vitals/main/person_vitals.c:99-131`, `:252-283`.
- **Status:** unverified on hardware

### D-122 At boot, adopt a log record only if its name equals the slot's current template name
- **Decision:** `analytics_init` loads the recognition model first, then `vit_log`, then `pv_adopt_log(names = template names)`. A record under a different name, or under a slot that is now empty, is cleared and marked dirty so the next periodic save persists that. Only records that had a name count as "dropped" in the log message.
- **Why:** a slot re-enrolled or forgotten without the log being saved, or a model that failed to load, must never hand one person's history to somebody else. Counting only named records avoids a false "dropped" message at boot.
- **Rejected:** none recorded.
- **Evidence:** `test_pvitals`.
- **Where:** `step10_recognise_count_vitals/main/main.c:3445-3488`; `step10_recognise_count_vitals/main/person_vitals.c:285-310`.
- **Status:** unverified on hardware

### D-123 Throttle log saves: only when changed, and at least `CSI_VITALS_LOG_SAVE_S` after the last attempt
- **Decision:** after the no-CSI watchdog in the periodic section, save when the log is dirty and at least `CSI_VITALS_LOG_SAVE_S` (default 300, range 60–86400) have passed since the last **attempt**. A failed write waits the same interval. The first save waits a full interval after boot. A log line appears only on an error or a save slower than 50 ms. `forget`, `vitals-reset` and `enroll` save at once (D-113).
- **Why:** flash wear, and a boot loop must not become a flash-write loop; a failing flash backs off instead of retrying on every packet. The save sits after the per-packet timing, so a slow write is not blamed on a packet in `proc_ms_max`, and the 16-slot queue rides it out (D-47). NVS skips writing an identical blob. Cost: up to that many seconds of log lost on a power cut.
- **Rejected:** saving per analysis or per JSON line; also saving at presence end (a 2026-09-25 design alternative; why it was not adopted is not recorded).
- **Evidence:** wear estimates are estimates, not measurements, and two sources differ: the Kconfig help says 288 writes/day, about 3 erases per sector per day, "decades"; the 2026-09-25 design notes said about 4.6 erases/sector/day, about 60 years at 300 s. An NVS erase in the pipeline task may stall (tens to hundreds of ms): watch `proc_ms_max` and `dropped` on the first flash.
- **Where:** `step10_recognise_count_vitals/main/main.c:673-689`, `:3109-3132`; `step10_recognise_count_vitals/main/person_vitals.c:312-322`; `step10_recognise_count_vitals/main/Kconfig.projbuild:428-455`.
- **Status:** unverified on hardware

### D-124 Save the vitals log at once when an enrolment starts
- **Decision:** `cmd_enroll` calls `vit_log_save()` right after `pv_slot_gone()`, as `forget` does. If that save fails, the reply gets an indented `vitals log NVS <err>` line.
- **Why:** at boot a record is kept whenever its name matches its slot's template (D-122). The new template is saved when the enrolment completes (about 40–130 s later), while the cleared log waited up to 300 s. A reset in between would hand the old owner's log to a new person who re-used the name.
- **Rejected:** only softening the README's "never" sentence (the reviewer's alternative).
- **Evidence:** 2026-09-25 review finding 2 (low); fixed the same day.
- **Where:** `step10_recognise_count_vitals/main/main.c:1873-1885`, `:1906-1912`.
- **Status:** unverified on hardware

### D-125 `forget` is a delete, not a secure wipe; "last seen" is uptime in RAM only
- **Decision:** document that deleted gait templates and health-related vitals logs stay readable in flash until NVS reuses the page, and that NVS is not encrypted. `last_seen_us` is RAM-only uptime; `people` prints "not seen since boot" when it is 0, and a credit at uptime 0 is stored as 1 µs.
- **Why:** an NVS erase or overwrite only marks old entries erased; anyone with the board and esptool can read the flash. NVS encryption needs flash encryption or an HMAC key in eFuse, both permanent eFuse burns, neither set up or tested. There is no SNTP or wall clock, so uptime means nothing across reboots. `idf.py erase-flash` removes everything, calibration included.
- **Rejected:** enabling NVS encryption (not done).
- **Evidence:** the README's encryption advice is general ESP-IDF knowledge, not tested here.
- **Where:** `step10_recognise_count_vitals/main/main.c:1937-1946`; `step10_recognise_count_vitals/main/person_vitals.h:139-141`; `step10_recognise_count_vitals/main/Kconfig.projbuild:454-455`; docs/COMPARISON.md §5.
- **Status:** active

### D-167 A stored baseline from another Wi-Fi channel is warned about, not rejected; automatic recalibration left open
- **Decision:** at shape lock, Steps 08 and 10 compare the stored baseline's channel with the live one. On a mismatch they log `stored baseline was calibrated on channel X, now channel Y - presence may be wrong: ...` and **keep using the baseline**. While detecting, Step 10's `status` line adds `(baseline from channel X: 'recal')`, without the `'recal'` hint during a CSI outage, when `recal` is refused. Nothing recalibrates by itself. Whether a channel mismatch should force a recalibration is **open**: the maintainer has not decided.
- **Why:** on 2026-09-28 the laptop hotspot came back on channel 6 instead of 1 (D-159), and Step 10 detected with its channel-1 baseline without a word. The reuse check compares `csi_len`, `nsub` and `fwi` and requires `n_selected >= 8` and `m_std > 0`, but does not look at the channel (D-114, D-116), and `channel_follow` reports only changes that happen after lock. A baseline is a per-subcarrier amplitude picture of one channel, so a baseline from another channel describes a different radio path. Before the `recal`, every detect row said PRESENT (81 in the soak, 30 in the listen before it), with `dev_sd` 4.96–20.64 sd in the soak. After the `recal`, `dev_sd` read -1.04 in the first detect row after calibration (about 45 s after the `recal`), at the same RSSI (-27 dBm in the last row before the `recal` and in that row; RSSI had stayed at -26 to -33 dBm for about 29 s before the `recal`, from uptime 74 s). That is consistent with a stale baseline, but it does not show it: the user had pressed RST about 100 s before the `recal` (uptime 103 s at the `recal`), and neither the board's placement nor who was in the room before, during and after the `recal` was recorded (TESTING §5.1). The warning follows `model_shape_check`, which already treats Step 10's recognition and count models this way.
- **Rejected:** nothing yet; left open. *Automatic recalibration on a mismatch:* it would retire a stale baseline. But a board that restarts while people are in the room would calibrate on an occupied room: routers on automatic channel selection may move after their own restarts, and this office often cannot be emptied (D-166). It would also change the detector that Phase 1 will benchmark (TESTING §4). *Rejecting the baseline* has the same cost, because the shape-mismatch path recalibrates. Neither is chosen.
- **Evidence:** TESTING §5.1 (2026-09-28 soak row; raw: `soak10_0928.log` and `listen_1352.log` in `D:\ESP32C5_CSI_logs\2026-09-28_step10_soak\`, outside the repo) and §5.2 (2026-09-28 builds: Step 10 0xef410, Step 08 0xdb340, root 0xdb350, 0 warnings each; host tests and the 192-combination matrix pass). **The warning has not run on a board**: the soak ran the image built before it.
- **Where:** `step10_recognise_count_vitals/main/main.c` (`PH_LOCK`; grep `stored baseline was calibrated on channel`; `cmd_status`, `ch_note`); `step08_final_human_detector/main/main.c` (same grep). Step 08 has no `channel_follow`, so there a channel change after lock goes unnoticed. The root project builds Step 08's `main.c`.
- **Status:** superseded by D-168 (2026-09-28)

### D-168 A stored baseline from another Wi-Fi channel is recalibrated automatically (supersedes D-167)
- **Decision:** the Wi-Fi channel is part of the baseline match. At shape lock, Steps 08 and 10 treat a stored baseline whose channel differs from the live one as unusable: they log `stored baseline was calibrated on channel X, now channel Y - recalibrating` and run the leave-room countdown and a full calibration. A channel of 0 on either side (the stored one, or the live one at shape lock) means "not known" and is accepted. Step 10 also acts after lock: when `channel_follow` adopts a new channel (100 accepted packets in a row on it), a calibration in progress starts again, and in SETTLE or DETECT the board recalibrates (`Wi-Fi channel changed - recalibrating. LEAVE THE ROOM.`) unless the stored channel is 0 or is the newly adopted channel (possible only when the live channel at shape lock was 0). It takes the `recal` path, and during a CSI outage it waits until CSI flows again. Step 08 acts at boot only, because it has no `channel_follow`. Step 08's `PH_SAVE` stamps the channel of the packet it saves on (the lock channel if that packet's is 0), so a calibration after an in-run channel change is recorded under the channel it ran on, not the boot channel. In Step 10, if a channel is adopted on the `PH_SAVE` packet, nothing is saved and the recalibration starts on the next iteration. With `CSI_FORCE_RECALIBRATE` the log says `recalibrating (CSI_FORCE_RECALIBRATE)`. Recognition and count models from another channel are still only warned about (`model_shape_check`).
- **Why:** the user chose it on 2026-09-28, over D-167's warning, when asked "When the stored calibration is from a different Wi-Fi channel than the live one, what should Steps 08 and 10 do?" (answer: "Auto-recalibrate"). A baseline pictures one channel's subcarriers, and on 2026-09-28 the hotspot's restart moved it from channel 1 to 6 (D-159) while the board detected with the channel-1 baseline (TESTING §5.1). The option the user was shown described the reboot case; **the after-lock half in Step 10 is this log's extension of that choice**, made because the same stale baseline results when the AP changes channel while the board runs. To keep boot-only behaviour, drop the `chan_recal` flag in Step 10's `pipeline_task`.
- **Rejected:** warn only (D-167): it leaves a stale baseline in use until someone types `recal`, and Step 08 has no console. The cost accepted with this choice: a board that restarts while people are in the room, or a Step 10 whose AP changes channel then, calibrates on an occupied room (D-166), and routers on automatic channel selection may move after their own restarts. A `channel_follow` for Step 08 was not added: it would be new tracking in the detector Phase 1 is about to benchmark.
- **Evidence:** builds of 2026-09-28 (TESTING §5.2): Step 10 0xef330, Step 08 0xdb340 (with `CSI_LLTF_ONLY` = y) and the root 0xdb340, 0 warnings each; host tests 11,453 checks and the 192-combination matrix pass. `test_e2e.c` models neither baseline reuse nor `channel_follow`, so no host test exercises it. **Not run on a board.** Refinements, 2026-09-28: a refute-style review of that first implementation found four defects, each confirmed by reading the code: Step 08 saved a recalibration made after an in-run channel change under the boot channel; Step 10 could save old-channel statistics under the new channel when the adoption fell on the `PH_SAVE` packet; a live channel of 0 at shape lock rejected a valid baseline (and Step 08 then stamped the new one 0, which switched the check off); and with `CSI_FORCE_RECALIBRATE` the log also said the stored baseline did not match the CSI shape (a message defect older than D-168). All four were fixed the same day. Builds after the fixes (TESTING §5.2; logs `build10_0928g.log`, `build08_0928c.log`, `buildroot_0928c.log` in `D:\ESP32C5_CSI_logs\2026-09-28_step10_soak\`): Step 10 0xef360, Step 08 0xdb340, root 0xdb350, 0 warnings each; host tests (11,453 checks) and the 192-combination matrix pass. `main.c` is not host-compiled and `test_e2e.c` does not model `PH_SAVE` either, so no host test exercises the fixes. **Still not run on a board** at that point; the flash below ran the image but not the check. 2026-09-28 19:00 (TESTING §5.1): 0xef360 (`build10_0928h.log`, ELF SHA256 `8bcc0f492e10fbc0cd0ed1a5d90a3ff28bd5a5e3734d3008748e32e5f893c8e1`) was flashed to the C5, and `Hash of data verified` was printed for all three images. The boot captured after the firmware's own restart printed `ELF file SHA256: 8bcc0f492...`, which matches that ELF, and `baseline found: 106 bytes CSI, 26/26 subcarriers, ch 6, 50.2 Hz`. The hotspot was off, so there was no Wi-Fi join and no shape lock: **the channel check was not exercised.**
- **Where:** `step10_recognise_count_vitals/main/main.c` (`PH_LOCK`: `shape_ok`, `same_ch`; `channel_follow`, which now returns true when it adopts a channel; `chan_recal` in `pipeline_task` and the recalibration block beside `button_held_2s()`; `PH_SAVE`: `if (chan_recal) break;`); `step08_final_human_detector/main/main.c` (`PH_LOCK`: `shape_ok`, `same_ch`; `PH_SAVE`: `s_base.channel = r.channel ? r.channel : s_locked_ch`). Lines on 2026-09-28: Step 10 `:3470-3478` (reuse check), `:1306` (`channel_follow`), `:3541-3546` (its caller sets `chan_recal`), `:3418-3443` (recalibration block), `:3722` (`PH_SAVE` guard); Step 08 `:436-444` (reuse check), `:465-467` (forced-recalibration message), `:672` (`PH_SAVE` stamp).
- **Status:** unverified on hardware

---

## 6. Interfaces

### D-126 Step 05 prints a decimated CSV stream for plotting
- **Decision:** `CSI,<seq>,<ms>,<rssi>,<nsub>,<median_x100>,<a0>..<aN-1>`, each amplitude normalised × 100 (100 = the packet median), for 1 in `CONFIG_CSI_CSV_DECIMATE` (default 5) accepted packets. `tools/csi_monitor.py` draws it as a waterfall.
- **Why:** at 50 packets/s a full dump can swamp the serial console; 1 in 5 gives about 10 lines/s, which plots well and keeps the console readable. If the waterfall stays flat when you wave, stop: no threshold tuning downstream can rescue it.
- **Rejected:** none recorded.
- **Evidence:** not run on a board.
- **Where:** `step05_amplitude/main/main.c:319-330`; `step05_amplitude/main/Kconfig.projbuild:43-50`; README § "See it with your own eyes".
- **Status:** unverified on hardware

### D-127 Step 08 output: a `JSON `-prefixed line every 500 ms plus an optional 4 Hz bar line
- **Decision:** `JSON {t_ms, state, present, motion_sd, dev_sd, level, rssi, rate_hz, nsub}` every `CSI_JSON_PERIOD_MS` (default 500; 0 disables). During calibration the line carries `state` and `remaining_s`; with no CSI, `state` is `"no_csi"`. `CSI_HUMAN_READABLE` adds a bar line at 4 Hz.
- **Why:** machine-readable output for a host script, Home Assistant or an MQTT bridge; the `JSON ` prefix makes the lines easy to pick out of the log. Pitfalls: the key `nsub` carries the **selected** subcarrier count (`n_selected`), not the total; `level` = 100 × `m_sigma` / motion threshold, clamped to 0–999, so 100 means "at the trigger". Why these names and scales: not recorded.
- **Rejected:** none recorded.
- **Evidence:** not run on a board.
- **Where:** `step08_final_human_detector/main/main.c:769-797`, `:820-854`; `step08_final_human_detector/main/Kconfig.projbuild:172-183`; README § "Host-side monitoring".
- **Status:** unverified on hardware

### D-128 JSON is append-only: Step 08's keys first, new keys only after `dropped`
- **Decision:** Step 08's keys come first and unchanged. Step 10's keys follow in a fixed order, only on `state` `"detect"` lines; calibration and `no_csi` lines are exactly Step 08's. Then the diagnostics `proc_ms_max` and `dropped`, then keys added since (`vitals_of`), then `}`. The key set is the same in every build (`vitals_of` is null when recognition or vitals is off). Each line is built in a 1,024-byte buffer and printed with **one** `printf`; a line that would overflow is not printed.
- **Why:** no key ever moves, so Step 08 tools (`presence_monitor.py`, host scripts, MQTT/Home Assistant consumers) and a CSV with an old header still line up. One `printf` means a console answer cannot land in the middle of a line; a truncated line would be invalid JSON.
- **Rejected:** inserting `vitals_of` beside its relatives (before `hf`), which would have shifted `hf`, `proc_ms_max` and `dropped` (2026-09-25 design alternative, not kept).
- **Evidence:** worst case 793 bytes + NUL (all ten floats at -FLT_MAX, 15-character names); the example line is 375 bytes (measured 2026-09-25). 2026-10-16: with the D-172 keys `activity`, `activity_s`, `episode_s` appended after `vitals_of`, the bound in the `main.c` comment is 862 bytes + NUL and the example line 426 bytes (worked out on paper, not measured; `main.c` comment above the JSON buffer, "no line can exceed 862 + NUL"). 2026-11-03 (D-176): `agc`, `fft`, `gain_chg` appended after `episode_s`, written as `null` without `CSI_GAIN_LOG` or with no packet since the previous line, so the key set still does not depend on the build; only the detect line carries them. The bound in the `main.c` comment is now 900 bytes + NUL (the gain keys at most 38) and the header example 457 characters (the printed line, `JSON ` prefix included; `main/main.c:1890-1898` on 2026-11-03; worked out on paper, not measured).
- **Where:** `step10_recognise_count_vitals/main/main.c:89-130`, `:1529-1541`, `:1588-1631`; README § "JSON".
- **Status:** active

### D-129 One `report_build()` feeds the JSON, the bar line, `status` and `people`
- **Decision:** `report_t` is built by one function and every output reads it; `people`'s "vitals now" line comes from the same report as the JSON. The bar line appends ` of=<name>` only when `vitals_of` differs from `who=`.
- **Why:** one definition means the outputs cannot disagree. `of=` is shown only when it adds information: a seated person has no `person` (nobody can be recognised sitting) yet can have a `vitals_of`.
- **Rejected:** none recorded.
- **Evidence:** tool output checked on sample data, 2026-09-25.
- **Where:** `step10_recognise_count_vitals/main/main.c:1387-1405`, `:1836-1843`, `:3032-3050`.
- **Status:** active

### D-130 Vitals state precedence; `"multiple"` does not depend on recognition
- **Decision:** `vitals` = `"off"` (not configured, or before the first baseline) → `"no-person"` (not present) → `"multiple"` → the vitals module's own state (`moving`, `settling`, `no-signal`, `ok`). `"multiple"` fires when the count is ≥ 2, **or** two names were seen this visit, **or** a `count-train 2` session is running; it nulls the rates, their confidences and `vitals_of`. It lives in the vitals code, not behind the recognition switch.
- **Why:** one breathing rate for two people is a mix, and naming one of them is a guess. If "multiple" depended on recognition, a counting + vitals build without recognition would keep reporting one rate for two counted people. A `count-train 2` session is two or more people by definition.
- **Rejected:** gating "multiple" on recognition and vitals both being on.
- **Evidence:** side effect: before the very first baseline, `status` now says "vitals off" where it used to say "no-person" (2026-09-25).
- **Where:** `step10_recognise_count_vitals/main/main.c:187-190`, `:1452-1480`; README § "JSON".
- **Status:** unverified on hardware

### D-131 Never show a stale or misleading number
- **Decision:** every vitals analysis replaces all four numbers; a rate that is no longer confident is withdrawn, not held. `hr_conf` is published only **with** a heart rate. `br_conf` is shown for `ok` and `no-signal` even beside a null `br_bpm`. NaN or infinity prints as JSON `null`. `person_dist` caps each vote at 999.
- **Why:** `hr_conf` is also filled when a peak is withheld (band edge, notch shoulder) and can read 30+; `"hr_conf":34` beside a null `hr_bpm_exp` would read as a strong heartbeat that somehow went unreported. A `br_conf` above threshold with a null rate signals an edge peak (rate outside 7.6–28.7/min, or drift). `printf` writes `nan`/`inf`, which is not JSON, and one such token makes the host drop the whole line.
- **Rejected:** none recorded.
- **Evidence:** reasoning; not run on a board.
- **Where:** `step10_recognise_count_vitals/main/vitals.c:118-123`, `:321-328`; `step10_recognise_count_vitals/main/main.c:1482-1496`, `:1575-1586`; README § "JSON".
- **Status:** active

### D-132 Instrument the first flash: `proc_ms_max`, `dropped`, a live-rate check, free heap
- **Decision:** the time from dequeue to the end of per-packet work is measured on every packet, and `proc_ms_max` is the maximum since the last JSON line. `dropped` counts CSI packets lost to a full queue since boot, with a warning at most once a minute. The live packet rate is compared with the calibrated one every 10 s and warns once at a 20 % difference. `status` prints the pipeline line and free heap.
- **Why:** per-packet processing time and free heap have never been measured on silicon, and the cost of a gait window, a count block and an FFT landing on one packet is unknown (D-77). `dropped` at 0 with a low rate points at the router; a climbing `dropped` points at the pipeline (go back to a 20 ms ping, or switch a feature off).
- **Rejected:** none recorded.
- **Evidence:** added 2026-09-24; never read on a board.
- **Where:** `step10_recognise_count_vitals/main/main.c:2410-2414`, `:3060-3088`, `:3144-3185`; README § "Kconfig — `menuconfig` → *Recognise / count / vitals*".
- **Status:** unverified on hardware

### D-133 Step 10's serial console
- **Decision:** `CSI_CONSOLE_ENABLE` adds `help`, `status`, `enroll`, `people`, `forget`, `vitals-reset`, `count-train`, `count-reset`, `stop` and `recal`, each answered by a `CMD ` line. One session at a time, only while detecting. Names are 1–15 characters of `[A-Za-z0-9_-]`, checked at `enroll` and at NVS load; `unknown`, `multiple` and `null` are reserved; duplicates are refused. Typed text is never echoed back, only validated names. Lines are at most 63 characters (`CON_LINE_MAX` 64). `cmd_execute` is a switch with no `default`. The USB-Serial-JTAG driver is installed first thing in `app_main`, with a 2 KB TX ring, and `main/CMakeLists.txt` names `esp_driver_usb_serial_jtag` and `vfs`.
- **Why:** the name alphabet makes names safe to print inside JSON and logs without escaping. The reserved names are JSON `person` values with their own meaning. With no `default`, `-Wswitch` under `-Werror` forces every verb to be handled. A full 256-byte TX ring would block `printf` in the pipeline for up to 50 ms, a CSI stall. The driver goes in first because between the two calls `printf` would write the FIFO while the driver's ISR also owns it. `vfs` is named because `driver/usb_serial_jtag_vfs.h` includes `esp_vfs.h` and the driver only exports `vfs` when the console is on USJ. Characters you type are not echoed; `presence_monitor.py --send` is the alternative.
- **Rejected:** a verb called `forget-vitals` (2026-09-25 design alternative; `vitals-reset <slot>|all` was chosen).
- **Evidence:** 96-combination build (D-145). When adding a verb, update together: the `main.c` header, `console_help`, the Kconfig `CSI_CONSOLE_ENABLE` help and the README console table.
- **Where:** `step10_recognise_count_vitals/main/main.c:72-87`, `:578-595`, `:2091-2106`, `:2141-2222`, `:3516-3517`; `step10_recognise_count_vitals/main/CMakeLists.txt:6-14`; `step10_recognise_count_vitals/main/Kconfig.projbuild:493-511`; README § "Console commands".
- **Status:** unverified on hardware

### D-134 `recal` is refused during a CSI outage and outside the detect phase
- **Decision:** the `recal` command and the button act only when the phase is detect and there is no outage; the button's hold timer runs only then.
- **Why:** during an outage the calibration would sit in "leave-room" beside the `no_csi` lines, and after a CSI shape change (which only the boot-time lock re-learns) it could never finish. Restricting it to detection means a half-finished calibration is never interrupted and a hold crossing 2 s mid-calibration is not silently swallowed.
- **Rejected:** none recorded.
- **Evidence:** not run on a board.
- **Where:** `step10_recognise_count_vitals/main/main.c:2067-2082`, `:2379-2405`.
- **Status:** unverified on hardware

### D-135 LED and recalibrate-button GPIOs default to -1 (disabled)
- **Decision:** `CSI_LED_GPIO` and `CSI_BUTTON_GPIO` default to -1; the user sets them from the board's schematic. Optional `CSI_LED_ACTIVE_LOW`. The LED is a plain on/off GPIO.
- **Why:** devkit pin assignments differ, and the author "will not hand you a pin number I could not verify". A board whose only LED is an addressable WS2812 will not work with on/off drive; that needs the `led_strip` component.
- **Rejected:** shipping a default pin number.
- **Evidence:** none needed.
- **Where:** `step07_presence/main/Kconfig.projbuild:106-116`; `step08_final_human_detector/main/Kconfig.projbuild:146-170`; README § "Features".
- **Status:** active

### D-136 `presence_monitor.py`'s contract: forward `CMD ` and indented lines; add every new key to `CSV_FIELDS`
- **Decision:** the monitor parses `JSON ` lines, forwards a serial line as console output only if it starts with `CMD ` or is indented by at least 4 spaces, and ignores everything else (log lines, the bar line). Its CSV uses `csv.DictWriter(fieldnames=CSV_FIELDS, extrasaction='ignore')`. Firmware continuation lines are indented on purpose (for example `people`'s per-slot vitals line at 8 spaces).
- **Why:** a firmware reply line that is not indented is silently not shown, and a JSON key missing from `CSV_FIELDS` is silently dropped from the CSV. `CSV_FIELDS` keeps the firmware's key order, with later keys after `dropped`. When adding a JSON key or a multi-line reply, update `presence_monitor.py` in the same change. Only one program can own a COM port, so close `idf.py monitor` first.
- **Rejected:** none recorded.
- **Evidence:** `py_compile` and sample-data runs, 2026-09-25. 2026-11-03 (D-176): `CSV_FIELDS` gained `agc`, `fft`, `gain_chg` after `episode_s` (`tools/presence_monitor.py:124-139` on 2026-11-03), and so did `tools/dashboard.html`'s (`:954`); the docstring says they are written to the CSV and not shown. Not run against a board.
- **Where:** `tools/presence_monitor.py:103-119`, `:172-175`; `step10_recognise_count_vitals/main/main.c:1827-1830`.
- **Status:** active

### D-137 `vitals_validate.py` scores against an oracle, so a flat number cannot look accurate
- **Decision:** the script pairs firmware rows with a reference device within ±2 s and prints coverage, MAE, RMSE, bias, % within tolerance, correlation, the score of an "always the median" oracle, and TRACKING/ACCURACY verdicts. `--person` filters on `vitals_of` and refuses a CSV recorded before that key existed. Recognition has no scoring script.
- **Why:** resting heart rate barely moves, so "90 % within 5 bpm" can come from a number that ignores the person. Test after exercise so the reference changes.
- **Rejected:** none recorded.
- **Evidence:** docs/COMPARISON.md §3 notes it has no MAPE, pass rate or Bland-Altman limits of agreement. Never run on real data.
- **Where:** `tools/vitals_validate.py`; README § "Validating heart rate — do this before you believe it"; docs/COMPARISON.md §3.
- **Status:** active

### D-161 Step 10 explains its decisions as `EXPLAIN {json}` lines (`CSI_EXPLAIN`)
- **Decision:** a bool `CSI_EXPLAIN` (default y) and an int `CSI_EXPLAIN_AMP_HZ` (default 5, range 0–20, 0 = off) in the Step 10 menu; the int has no `depends on` (D-25). Each explain line is **one** `printf` of `EXPLAIN ` followed by one JSON object whose `ev` key names the event: `config` (detection starts, after `recal`, and on the new console command `explain`), `cal` (a calibration completed), `amp` (normalised amplitudes of the selected subcarriers plus `m`, `d`, `hf`, `still`, `present`, at most `CSI_EXPLAIN_AMP_HZ` lines a second), `gait` (every completed gait window, whether or not it voted), `count` (every completed count block), `vitals` (every completed analysis) and `presence` (every change of PRESENT, with its reason). Floats have at most 3 decimals; unknown and non-finite values print as `null`; the only strings are literals and names that passed `name_valid`. Apart from the rate-limited `amp` line nothing new runs per packet: every other line is printed only when a window, block, analysis or presence change completes. The field list is ARCHITECTURE §3.7.
- **Why:** the JSON line carries outcomes (`person`, `count`, `vitals`) but not the reasons behind them: which slot was nearest and by how much, why a window did not vote, what a count block measured. On 2026-09-25 the user, in an office with two other people and the board, asked for "serial prints or a visual app" to understand how it recognises and counts people. The dashboard (D-163) needs those intermediate values in a form a program can read. With a prefix of its own, the JSON line and the tools built on it do not change (D-128): `presence_monitor.py` ignores any line that does not start with `JSON `, `CMD ` or four spaces (D-136). One `printf` per line for the reason D-128 gives: a console reply cannot land inside it. Printing only on events keeps the per-packet cost where D-77 needs it on a chip with no FPU.
- **Rejected:** none recorded.
- **Evidence:** specified on 2026-09-25 in a session design note (not kept in the repo); first tracked record: this log. The firmware was written in parallel with this entry; not run on a board. The worst-case line length is worked out in a comment beside the explain code in `main.c`, and must stay well under the console TX ring (D-133 records 2 KB; the `CSI_EXPLAIN` Kconfig help, written for this change, says 4 KB, and that serial output roughly doubles to about 3–4 KB/s).
- **Where:** `step10_recognise_count_vitals/main/main.c` (grep `EXPLAIN`); `step10_recognise_count_vitals/main/Kconfig.projbuild` (`CSI_EXPLAIN`, `CSI_EXPLAIN_AMP_HZ`); docs/ARCHITECTURE.md §3.7; README § "See how it decides: the live dashboard and the why lines".
- **Status:** unverified on hardware

### D-162 With `CSI_EXPLAIN`, one plain-English `why:` log line per decision
- **Decision:** beside each gait, count, vitals and presence event, the firmware also logs one short `ESP_LOGI` line starting `why:` that gives the measured values, the thresholds they were compared with (the live ones, not the Kconfig defaults) and the result. For example: `why: gait window 12 - 84% moving; nearest raj 1.21, next asha 2.02 (reject > 2.50, margin needs 1.15, got 1.67) -> vote raj; votes raj 4 of 7 -> person raj`. One line per event: at 50 packets/s that is about one per 2.6 s from gait windows (one every 128 packets) and one per 5.1 s from count blocks (256 packets).
- **Why:** a person reading `idf.py monitor`, without the dashboard, should see why an answer came out as it did. The `EXPLAIN` JSON is written for programs, the `why:` line for people.
- **Rejected:** none recorded.
- **Evidence:** specified on 2026-09-25 (first tracked record: this log). Not run on a board.
- **Where:** `step10_recognise_count_vitals/main/main.c` (grep `why:`); README § "See how it decides: the live dashboard and the why lines".
- **Status:** unverified on hardware

### D-163 Live dashboard: a local web page fed from the serial port (`tools/dashboard.py`, `tools/dashboard.html`)
- **Decision:** `tools/dashboard.py` uses the Python standard library (3.8+) and, for a real port only, pyserial. It sets DTR and RTS low **before** opening the port and never touches them again, and reopens the port by itself when it disappears. It serves the page on `http://127.0.0.1:8765/` by default: `GET /` is the page; `GET /events` is a Server-Sent Events stream of every serial line (JSON, EXPLAIN, `CMD`, log lines), and a page opened late first receives the latest `config` and `cal` explain lines and a backlog of recent lines; `POST /cmd` with `{"cmd":"status"}` writes one console command and a newline. A command must be 1–63 printable ASCII characters (the console's own limit, D-133), the request must be `Content-Type: application/json`, and a browser `Origin` must match the server, so another web site open in the same browser cannot post commands. Options: `--port auto|COMx` (auto = the single USB device with Espressif's vendor ID `0x303A`; with none or several it keeps looking and says why), `--log FILE` (append every line as `%7.2f <line>`, the format `serial_capture.py` writes, so the file can be replayed), `--replay FILE` (play a recorded log instead of a port, at its own pace or `--speed N`; silences longer than `--max-gap`, default 3 s, are shortened; `--loop`; console commands are refused), `--open`, `--http-port`, `--host` and `--echo`. `tools/dashboard.html` is one self-contained file with no external scripts and no build step, so it works offline; it draws with canvas/SVG and has a light and a dark theme. Every panel has a short "How it decides" text built from the live thresholds of the `config` event, and a permanent note says that recognition, counting and heart rate are experimental and unvalidated. Without explain lines (older firmware, Step 08) it falls back to the JSON line and says `explain lines not received - flash Step 10 with CSI_EXPLAIN`. Lines starting `#` are skipped. `tools/sample_session.log` is a replay file for demos and tests: either cut from a real capture, or marked `# SYNTHETIC` on its first line.
- **Why:** the user's request of 2026-09-25 (D-161). A browser page needs no GUI toolkit, and pyserial is already in the ESP-IDF Python environment (otherwise `pip install pyserial`). DTR/RTS: D-153 and D-158. The reconnect is needed because a chip reset re-enumerates the USB-Serial-JTAG port, so the COM port disappears for a moment. Replay shows and tests the page without the board, which another program may own (TESTING G7). Consequences of the design: by default the page, and its console buttons that can `forget` a person or `recal`, are reachable from this PC only; `--host 0.0.0.0` opens both to the network, and the script prints a warning when it does. While the dashboard owns the port, `idf.py monitor` and `presence_monitor.py` cannot run (G7), so the dashboard's console panel takes their place.
- **Rejected:** none recorded.
- **Evidence:** specified on 2026-09-25 (first tracked record: this log). `dashboard.py` read on 2026-09-25 (its docstring and argument parser) to write this entry. Not run against the board.
- **Where:** `tools/dashboard.py`, `tools/dashboard.html`, `tools/sample_session.log`; docs/TESTING.md §2.5, §2.6; README § "See how it decides: the live dashboard and the why lines".
- **Status:** unverified on hardware

### D-169 Scripted hardware sessions: `tools/session_runner.py` plays a §4 protocol, `tools/session_score.py` scores it
- **Decision:** `tools/session_runner.py` plays a built-in TESTING §4 protocol while it logs the board. `--protocol phase1` is 1a–1c: a 20 s lead-in, 120 s empty, 3 tangential and 3 radial 60 s walking passes each followed by a 60 s empty gap, 20 s to enter and 300 s of small movements (1b), 120 s empty, 20 s to enter and 600 s still (1c), 120 s empty at the end (20 segments, 2040 s = 34 min). `--protocol phase2b` is §4 2b: a 20 s lead-in, then 600 s empty, 600 s walking, 600 s seated (4 segments, 1820 s). `--note` (placement, who is in the room, AP and channel) is required; `--out` refuses an existing file and a missing folder. Every board line is written as `%7.2f <line>`, the format of `serial_capture.py` and `dashboard.py --log`, so `dashboard.py --replay` plays the log; everything else is a `#` marker line (`# RUN`, `# PLAN`, `# SEGMENT <t> start|end <idx> <kind> <label>`, `# CUE`, `# SENT`, `# PORT <t> open|lost|reopened|unavailable`, `# BOARD`, `# WARN`, `# END <t> completed|interrupted`), with the kinds setup, calibration, empty, walk, enter, minor and still. Cues are printed, beeped (`--quiet` for none) and optionally spoken (`--speak`), with a 10 s countdown before each change of activity (none between the lead-in, a `--calibrate-first` segment and the first empty segment). It sends only `status` (at the start, every `--status-every` s, default 300, and at the end), and only to a board identified as Step 10 (its banner ` Step 10: `, a `CMD ` reply, or a JSON line with `proc_ms_max`) or with `--console`; never to Step 08 or Step 09; `--no-console` sends nothing. The port: DTR and RTS are set False before `open()` and never touched; after a loss it reopens every 0.3 s and never exits once started; at start-up, a port unavailable for 10 s ends the run before the first cue; `write_timeout` is 2 s, so a stuck write raises and is handled as a port loss; the start `status` is tried at most 3 times in all (`START_TRIES`), the retries after a reopen, then a `# WARN` says the heap check lacks its start value. Offline: `--dry-run` prints the schedule and opens nothing; `--simulate FILE` plays a recorded log as the port at 1/`--scale` pace, recorded port losses included. `tools/session_score.py` scores such a log against the §4 marks. It also reads `serial_capture.py`, `dashboard.py --log` and the 2026-09-25 listener logs (`%7.2f`) and the 2026-09-28 soak format (`%8.2f HH:MM:SS.mmm <line>`), and skips `#` comment lines. A log without `SEGMENT` markers is one `unlabelled` segment with no presence pass/fail. Only `state` `detect` JSON rows are scored, by host time. Per segment: present fraction and onsets; walk (1a): seconds reported empty per walking second, time to the first `present` = 1 (walk-in included); empty (1d): seconds reported PRESENT per empty second, onsets per hour; enter: time to the first `present` = 1; minor (1b): present fraction; still (1c): present fraction, time to the first lapse, and `br_bpm` / `hr_bpm_exp` coverage (never accuracy). `--grace` (default 10 s, scaled by `--scale` only for a `--simulate` log) leaves the start of every segment out; `--exit-s` (default 9 s = `CSI_PRESENCE_HOLD_MS` 5000 + `CSI_STATIC_SUSTAIN_MS` 4000, a stated figure, not measured) also trims an empty segment that follows an activity; every figure is printed with and without what is left out. The row period is the median of 4-row spans / 4. A 0 that cannot resolve a mark is `not applicable (... needs >= p/mark s)`, never `met`. Segments are also pooled per kind. Resources: `dropped`, `proc_ms_max`, `rate_hz` (the calibrated rate), `status` replies (heap first, last and minimum, live rate, slowest packet, uptime) and firmware warnings. Restarts: banners, ESP-ROM lines, uptime going back, and a boot within 30 s after a port loss. Marks, each with its origin and a verdict of met / NOT met / no mark / not applicable: walking seconds reported empty ≤ 0.001/s and empty seconds reported PRESENT ≤ 0.0001/s (TGbf 11-20/1712r2 via COMPARISON §3); `dropped` 0 over 30 min (project mark; an increase is NOT met however short the log); heap at the end not below the start (project mark); live rate ≥ 90/s at a 10 ms ping (project mark); `proc_ms_max` has no mark. Other options: `--after-calibration`, `--from`/`--to` (unlabelled logs only), `--json OUT` (refuses to overwrite). Exit code 0, or 2 on unreadable input or a refused option. It never prints "validated". `tools/serial_capture.py` now also sets a 2 s `write_timeout`: a stuck `--send` raises, is handled as a port loss, and is retried.
- **Why:** the user asked for it. The previous session's report offered "Walking tests: ... I can script that session with timed prompts and a log of who was where", and the user answered "yes please proceede" (2026-09-28). No run so far recorded who was in the room or where the board stood (TESTING §5.1, row 2026-09-28 13:51–14:07: "placement not recorded", "Who was in the room during the `recal` and the rest of the run was not recorded"), hence the mandatory `--note` and the schedule written into the same log as the board's lines. The schedule is the ground truth only as far as the operator followed it: someone who walks in late, or a pet, is not seen by the script (`session_runner.py` NOTES).
- **Rejected:** none recorded.
- **Evidence:** offline checks only, 2026-09-28 (outputs in `D:\ESP32C5_CSI_logs\2026-09-28_session_tools\`, outside the repo). `py_compile -W error` passes for all three scripts. `--dry-run`: phase1 20 segments, 2040.00 s; phase2b 4 segments, 1820.00 s. `--simulate` of phase2b and phase1 at `--scale 0.02` from the 2026-09-28 `soak10_0928.log` (in `D:\ESP32C5_CSI_logs\2026-09-28_step10_soak\`) (`final_sim_p2b.log`, `final_sim_p1.log`): `END completed`, 0 data lines failing `dashboard.py`'s `TS_RE`, 0 `#` lines failing `MARK_RE`, 0 end/start time mismatches, 0 timestamps going backwards. Those replays predate the write timeout and the last runner edit (17:59); re-run on the final file (`final_check_p2b.log`, `final_check_p1.log`): the same checks, `# END 39.50 completed` and `# END 43.84 completed`. Their scores (`score_final_sim_p2b.txt`, `score_final_sim_p1.txt`) mean nothing: the replayed soak had people moving at unrecorded times. A reviewer found 10 defects in `session_runner.py`; a skeptic confirmed 9, and all 9 were fixed and re-tested. The 10th (no write timeout) could not be reproduced without a port and was applied anyway, with the 3-try cap. With a fake pyserial module (`write_timeout_fake\`): one write timeout gave `# PORT 0.00 lost Write timeout ...`, a reopen and the `status` sent (`run2.log`); a send that kept failing (one `Write timeout`, then two failures from an earlier, incomplete fake that lacked `flush()`) gave up the start `status` after 3 tries (`run.log`); `serial_capture.py` logged `port lost: Write timeout`, reopened and resent the command (`cap.log`); neither script touched DTR/RTS after open. A reviewer found 8 defects in `session_score.py`; all 8 were reproduced and fixed. Scoring the real 2026-09-28 soak (`score_soak_whole.txt`, `score_soak_after.txt`): whole log, 1,714 JSON rows parsed and 1 unparsable, 1,669 detect rows; `dropped` max 0, never increased; `proc_ms_max` median 2.30, 95th percentile 11.00, max 20.80 ms; `rate_hz` 50.2; 5 `status` replies, heap first 94,248 B, last and minimum 94,132 B; 0 restarts (2,237 uptime readings). Heap mark NOT met on the whole log, because the start value predates the recalibration (calibration rows at host time 40.27–84.92 s); dropped mark not applicable (15.0 min < 30 min). With `--after-calibration` (after the calib-B row at 84.92 s): 1,589 detect rows, `present` = 1 in 544 (34.2 %; 34.6 % with the 10 s grace), 30 onsets, `proc_ms_max` max 18.70 ms, 4 `status` replies all 94,132 B, so the heap mark is met within that window. These agree with `soak_summary.txt` (1,669 detect rows; `proc_ms_max` median 2.3, max 20.8 ms; 544 of 1,587 = 34.3 % after calibration, whose 86 s cutoff drops the first two post-calibration rows, both 0; 30 onsets; heap 94,248 then 94,132 B) and with the TESTING §5.1 row. The 2026-09-25 boot log (`docs/hw_logs/2026-09-25_step10_boot_hotspot_ch1.log`) scores 3 boots, each within about 1 s after a port loss, and a 4th loss followed only by a reopen, as in §5.1 (`score_hwlog_0925_step10_boot.txt`). None of this measures accuracy. **Not run against the board.** 2026-09-28 (TESTING §5.1): `serial_capture.py`, in its 2026-09-28 version with the reopen loop and the write timeout, ran twice on the C5: 20 s from 18:57 with one `--send status`, and 60 s from 19:00:33. Neither capture lost the port, not even across the firmware's software restart in the second (`rst:0xc (SW_CPU)`; D-165), so the reopen loop and the write timeout are still not exercised on hardware. The one `status` write completed but got no reply: Step 10 creates its console task only after the Wi-Fi join (`step10_recognise_count_vitals/main/main.c:4673-4698` on 2026-09-28, `app_main`), and the hotspot was off. `session_runner.py` and `session_score.py` still have not run against the board. 2026-10-16: `session_score.py` version 3 also scores Step 10's `activity` key against each segment's cue (D-172; TESTING §2.7); on the 10 earlier logs, which carry no such key, its scores were unchanged (TESTING §5.2, 2026-10-16). 2026-11-03 (D-176): `session_score.py` version 4 added a GAIN block (logging only, no mark). After the review it is **version 5**: gain is pooled for every scored kind, `unlabelled` included, with per-segment gain lines in SEGMENTS; the JSON `rows_with_key` is replaced by `rows_numeric`, `rows_null` and `rows_invalid`; a `gain_chg` that is not a non-negative integer (a float, Infinity, NaN, a negative number, a bool) counts as invalid. On the seven `docs/hw_logs/*.log` and `tools/sample_session.log` (no gain keys): rc 0, and the output equals version 4's except the version line; 11 synthetic test logs: rc 0 (`~/ESP32C5_CSI_logs/2026-11-03_impl/after_review/score/`, `rc.txt`, `nogain/summary.txt`). Offline only.
- **Where:** `tools/session_runner.py`, `tools/session_score.py`; docs/TESTING.md §2.7 and §4 (Phase 1; Phase 2, step 2b); `tools/serial_capture.py` (write timeout).
- **Status:** unverified on hardware

---

## 7. Testing and process

### D-138 Every step is a complete standalone project; the duplication is deliberate
- **Decision:** ten independent ESP-IDF projects. Each copies the Wi-Fi, ping and CSI boilerplate; in each `main.c` the new logic sits at the top and the copied boilerplate below it. Steps 05–07 mark it with a `---- unchanged from Step 0N ----` banner, Steps 03, 04 and 09 with a "same as" or "identical to" section header, and Steps 02, 08 and 10 not at all (grep, 2026-09-25). The copies differ in formatting and comments, and Step 02 also logs the radio-lock return codes (D-34), so find copies by function name, not by banner (ARCHITECTURE §7.6). `csi_baseline.h` is copied into Steps 06–08 and 10. Do the steps in order the first time: Steps 03 and 04 tell you which stage broke.
- **Why:** it keeps the copy-one-folder-and-flash property that makes a stepwise tutorial work. Step 09's copy is "identical to Steps 02-08 on purpose". Pitfall: a fix must be applied to every copy (the 2026-09-22 review fixes touched `main.c` in each of Steps 02–09).
- **Rejected:** a shared component or file for the Wi-Fi bring-up.
- **Evidence:** structure of the repo.
- **Where:** `step05_amplitude/main/main.c:416`, `step06_baseline/main/main.c:576`, `step07_presence/main/main.c:437` (banners); `step03_csi_hello/main/main.c:182`, `step04_csi_geometry/main/main.c:387`, `step09_espressif_sensing/main/main.c:65` (section headers); README § "How the code is organised".
- **Status:** active

### D-139 Evidence discipline: nothing is called validated; published figures are ceilings, not predictions
- **Decision:** recognition, counting and heart rate are labelled experimental and unvalidated everywhere. Literature accuracies are quoted only if they appeared verbatim on a page opened on the sweep date; abstract-only reads are flagged; misattributed figures are listed but not used. Every accuracy cell for this project reads "Not measured yet". The Step 07/08 headers state the limits (moving humans only, motionless people unreliable, not human-specific, recalibrate after moving things).
- **Why:** the best published results used easier conditions (controlled rooms, dedicated transmitters, several links, trained models). "Recognising anyone, anywhere, several at once is not achievable on this hardware; anyone claiming it is overselling."
- **Rejected:** repeating circulating figures (e.g. "88.9–94.5 %" attributed to Gaiba belongs to FreeSense; Pulse-Fi's "118 participants" is the non-ESP32 dataset).
- **Evidence:** REFERENCES sweeps 2026-09-07 and 2026-09-24; COMPARISON 2026-09-25, re-checked by a second reader.
- **Where:** README § "Read this before you start — what this actually does"; `step07_presence/main/main.c:30-40`; `step08_final_human_detector/main/main.c:23-33`; docs/REFERENCES.md §3.4; docs/COMPARISON.md §3.
- **Status:** active

### D-140 Host unit tests: ESP-IDF-free modules, MSVC `/std:c11 /W4 /WX`, synthetic signals with known answers
- **Decision:** the seven analytics modules (`csi_dsp`, `csi_ncc`, `vitals`, `gait_id`, `count_est`, `motion_gate`, `person_vitals`) include no ESP-IDF headers and are always compiled; a disabled feature just never calls them and `--gc-sections` drops the code. `test/run_tests.ps1` (needs the Visual Studio 2022 C++ workload) compiles each `test_<name>.c` with the modules it needs, each in its own object directory, and runs 8 suites: dsp, ncc, vitals, gait, count, gate, pvitals, e2e. A warning is a failure. `main.c` is not compiled on the host. Binaries in `test/bin/` are git-ignored.
- **Why:** the same file compiles on the PC and on the C5, and `/W4 /WX` holds the tests to the firmware's warning bar. Separate object directories because `csi_dsp.obj` is built for several tests and two concurrent runs used to clobber a shared one. A pass proves the arithmetic and wiring, **not** that anything works on a real person.
- **Rejected:** none recorded.
- **Evidence:** 2026-09-25: dsp 5,980, ncc 731, vitals 2,386, gait 108, count 592, gate 97, pvitals 1,304, e2e 237 = 11,435 checks, 0 failures. 2026-09-28: the same except ncc 749, = 11,453 checks, 0 failures (TESTING §5.2). Since 2026-10-16 there are eight modules (`activity.c`) and 9 suites (D-172; gcc only since then). 2026-11-01 (gcc, D-174): 9 suites, 11,673 checks, 0 failures (vitals 2,499, e2e 287, activity 57, the rest as 2026-09-28), plus 15 printed KNOWN LIMIT lines, a form these tests gained on 2026-10-30: a measured shortcoming of today's code, printed with its numbers rather than failed (TESTING §3.1). MSVC has not compiled the suites since 2026-09-28. 2026-11-03 (gcc, D-175, D-176): nine modules (`csi_gain.c` added, `main/CMakeLists.txt:1-4`) and 10 suites (`gain` added): 11,705 checks, 0 failures after the review (vitals 2,509, gain 22, the rest as on 2026-11-01), 12 KNOWN LIMIT lines (vitals 7, e2e 5; `~/ESP32C5_CSI_logs/2026-11-03_impl/after_review/run_tests_full.log`). MSVC has still not compiled the suites since 2026-09-28.
- **Where:** `step10_recognise_count_vitals/main/CMakeLists.txt:1-11`; `step10_recognise_count_vitals/test/run_tests.ps1:8-48`; `step10_recognise_count_vitals/test/test_util.h:4-7`; `.gitignore:36-41`; README § "Host tests".
- **Status:** active

### D-141 MSVC `/WX` pitfalls in the tests
- **Decision:** NaN and infinity are built from bit patterns (`0x7FC00000`, `0x7F800000`), never `0.0f/0.0f` or `1e39f`. Float literals carry `f`. Unused parameters and signed/unsigned comparisons are errors.
- **Why:** MSVC folds the literal forms at compile time and `/WX` rejects them (C4723/C4756); C4244, C4100 and C4018/C4389 also become errors under `/WX`.
- **Rejected:** none recorded.
- **Evidence:** the tests compile clean under `/W4 /WX` (2026-09-25).
- **Where:** `step10_recognise_count_vitals/test/test_ncc.c:17-27`; `step10_recognise_count_vitals/test/test_pvitals.c:37-47`.
- **Status:** active

### D-142 `test_e2e.c` hand-copies `main.c`'s decisions; keep the copies in sync
- **Decision:** `test_e2e.c` copies `main.c` / Kconfig constants (`ID_VOTES`, `CNT_VOTES`, `PRESENCE_HOLD_US`, `GATE_RATIO`, `k_vcfg`, `CARRY_US`, …), re-implements the front end, `id_tally`, and `main.c`'s per-packet Step 10 order including `s_carry_wait` and the count carry. `GATE_RATIO` and `E_SEED` can be overridden with `/D`.
- **Why:** `main.c` needs ESP-IDF and cannot be compiled on the host. A change in `main.c` that is not copied makes e2e test stale behaviour while still passing. Not modelled: the static (D) branch, the JSON text, no-CSI/recal, sessions and the console, `forget`/`vitals-reset`, and the NVS save.
- **Rejected:** none recorded.
- **Evidence:** the copied per-packet order matched `main.c` on 2026-09-25. 2026-10-16: `act_push` added to the copy with `main.c`'s (D-172). 2026-10-30: scenario (f) adds a **shadow** of Step 08's static (D) branch with its constants (`TAU_SLOW_S`, `THR_STATIC`, `STATIC_SUSTAIN_US`, `SLOW_FREEZE_MAX_US`); it feeds nothing, so (a)-(e) and the per-packet order are unchanged (126 identical output lines, TESTING §5.2), and "not modelled: the static branch" still holds for presence. A fourth copy of Step 08's state machine, `replay_main.c` of the 2026-11-01 offline replay, lives outside the repo and is not kept in sync (TESTING §5.3).
- **Where:** `step10_recognise_count_vitals/test/test_e2e.c` on 2026-11-01: `:20-21` (the copy's date), `:45-74` ((e)'s glue), `:76-94` ((f)), `:138-168` (constants), `:1596-1599` (`CARRY_US`, `E_SEED`), `gl_packet` `:1729-1796`. The ranges `:44-70`, `:112-136`, `:1302-1349`, `:1444-1469` given here before were of 2026-09-25.
- **Status:** active

### D-143 Documented residuals are asserted as they currently behave
- **Decision:** `test_pvitals` (k) replays every row of the per-person-vitals edge-case table through `drv_packet()`, a small mirror of the per-packet order, and rows marked "documented residual" assert today's behaviour. Section (l) runs random event sequences and checks the latch and log invariants after each one. **Caveat (found 2026-09-25):** `drv_packet()` (`step10_recognise_count_vitals/test/test_pvitals.c:1287-1345`) mirrors the order **before D-108**: no `s_carry_wait` or `who_present`, `pv_presence_start` and the count hand-back at every presence start (D-109's rule), and `pv_packet` before the vitals step. Its check "Step 08 re-triggers while the gate stays still: kept" (`:490-495`) asserts the pre-D-108 hand-back. So (k) is a unit-level test of `person_vitals.c`, not a replay of `main.c`'s current order; only `test_e2e.c` covers that (TESTING §3.3 item 3).
- **Why:** a change in a residual's behaviour is then noticed, and the README updated with it, instead of drifting silently. The residuals: someone who sits before being recognised is credited to the latched person; a fan within 60 s of absence keeps the departed person's name; two unrecognised seated people with an untrained counter get one rate and no name.
- **Rejected:** none recorded.
- **Evidence:** 15 single-rule mutations, 14 caught (2026-09-25).
- **Where:** `step10_recognise_count_vitals/test/test_pvitals.c:12-20`.
- **Status:** active

### D-144 Known weakness: e2e scenario e1 fails in 2 of 9 synthetic rooms
- **Decision:** documented, not fixed. e1 (A walks in alone, is recognised, sits for 90 s) walks 30 s and then up to 120 s until the latch names A; if A is never named, e1 fails once and skips the rest.
- **Why:** a gait window needs 80 % of its packets moving (flag AND gate). Walker A's 1.6 Hz steps sit at the gate's 1.6 Hz corner, so its `hf` hovers near the threshold. It is a window-yield problem, not a latch one, and it means recognition through the device path at the default 50 Hz is fragile for this walker.
- **Rejected:** none recorded.
- **Evidence:** seed sweep over rooms 5000–5008, 2026-09-25: at 50 Hz the gate called 26–92 % of A's walking packets MOVING; in rooms 5004 and 5007 (31 % and 26 %) A was never recognised in 120 s, so 7 of 9 pass. At 100 Hz also 7 of 9, failing in other rooms for other reasons (a false trigger in the empty room; two seated chests keeping Step 08 on). The sweep script was not kept in the repo. Any one 50 Hz room can be re-run with the recipe in TESTING §2.1 (`/DE_SEED=<seed>`); rooms 5004 and 5007 were reproduced that way on 2026-09-25 (223 checks, 1 failure each, at `test_e2e.c:1609`). The 100 Hz figures cannot: scenario (e) is hard-coded to 50 Hz (`fe_calibrate(50.0f)`, `test_e2e.c:2453` on 2026-11-01; `:1941` on 2026-09-25), so they are a session measurement that needs that line edited, or a new `E_RATE` override, to repeat. 2026-11-01, re-run on Ubuntu with gcc on the final sources of D-174 (TESTING §2.1): rooms 5004 and 5007 still fail only e1 (`273 checks, 1 failures`, at `test_e2e.c:1923`, gate MOVING 30.7 % and 26.0 %), the other seven pass with 287 checks; the new scenario (f) passes in all nine.
- **Where:** `step10_recognise_count_vitals/test/test_e2e.c:1889-1902` and the CHECK at `:1923` (on 2026-11-01; `:1572-1585` and `:1609` on 2026-09-25); `fe_calibrate(50.0f)` in `test_whose_vitals` `:2453` (on 2026-11-01); README § "Verification status — please read".
- **Status:** unverified on hardware

### D-145 Compile Step 10's `main.c` in every Kconfig switch combination (96 on 2026-09-25, 192 since `CSI_EXPLAIN`, 368 since `CSI_ACTIVITY_ENABLE`, 552 since `CSI_GAIN_LOG`)
- **Decision:** each combination of ID × COUNT × VITALS (on with the breathing hold / on without / off) × JSON (500 / 0) × bar line × console × `CSI_EXPLAIN` (on / off, since the explain work of 2026-09-25, D-161) gets a patched `sdkconfig.h` and is compiled with `main.c`'s exact command from `compile_commands.json` (gcc 15.2, -O2, the project's warning flags). The LLTF, LED, button, force-recalibrate and `CSI_EXPLAIN_AMP_HZ` switches are not varied (`CSI_EXPLAIN_AMP_HZ` is read only at run time).
- **Why:** catch warnings and errors hidden behind feature switches (D-24).
- **Rejected:** none recorded.
- **Evidence:** 2026-10-16: the script also varies `CSI_ACTIVITY_ENABLE` (D-172) where Kconfig lets it exist, so 368 combinations (23 × 8 × 2), 0 warnings or errors, negative control rc=1 errors=3, on the Ubuntu v6.1 build (TESTING §5.2). 2026-09-28: the script now also varies `CSI_EXPLAIN` (D-161), so 192 combinations, 0 warnings or errors, negative control as below (TESTING §5.2). 2026-09-25: 96 combinations, 0 warnings or errors. Negative control: removing `CONFIG_CSI_PING_INTERVAL_MS` fails (3 errors), proving the patched header is used. Clean full builds on 2026-09-24 with all features off (0xdc180) and recognition off (0xe6400). The check is `tools/kconfig_matrix.py` (added 2026-09-25 from that day's scratch script; untracked on that date, D-150). Run `python tools\kconfig_matrix.py` after building Step 10 with all switches on; command and expected output are in TESTING §2.2. 2026-11-03: the script also varies `CSI_GAIN_LOG` (D-176), but only with `CSI_JSON_PERIOD_MS` 500, because without a JSON line `main.c` compiles the gain code out either way (`CSI_HAVE_GAIN`): 552 combinations (23 × 12 × 2, the script's docstring, THE MATRIX), 0 warnings or errors, negative control rc=1 errors=3, on the Ubuntu v6.1 build, before and after the review (`~/ESP32C5_CSI_logs/2026-11-03_impl/matrix/kmatrix.log`, `~/ESP32C5_CSI_logs/2026-11-03_impl/after_review/matrix/kmatrix.log`; TESTING §5.2). The 184 `gain0` combinations and the 184 `json0` ones compile the `CSI_HAVE_GAIN == 0` stub. Not yet run on Windows.
- **Where:** `tools/kconfig_matrix.py`; docs/TESTING.md §2.2; README § "Verification status — please read".
- **Status:** active

### D-146 Build verification status, per step
- **Decision:** record exactly what was built, when. Steps 01–09 and the root: clean-rebuilt on v6.0.2, 2026-09-07. A session note records every project of the time (Steps 01–09 and the root; Step 10 was created on 2026-09-24) fresh-building from `sdkconfig.defaults` alone with 0 warnings on 2026-09-22, before the review fixes landed. The 2026-09-22 review fixes changed `main.c` in Steps 02–09; Steps 03 and 08 were rebuilt on 2026-09-23 (Step 08 0xdb270, 57 % free), the root on 2026-09-24 (0xdb290). Step 10: 0 warnings, 0xea3d0 = 959,440 B, 54 % free, static HP SRAM 59 % (2026-09-25). Latest builds (2026-09-28): TESTING §5.2 and the Evidence below.
- **Why:** documentation must say exactly what was verified and when.
- **Rejected:** none recorded.
- **Evidence:** README "Verification status", and the file dates of every `build/*.bin` against its `main/main.c`, checked 2026-09-25 (TESTING §5.2). Built after the 2026-09-22 edits: Step 01, Step 03 and Step 04 (2026-09-25; 03 and 04 with the user's hotspot credentials), Step 08 (2026-09-23), the root (2026-09-24), Step 10 (2026-09-25). **Steps 02, 05, 06, 07 and 09: last built 2026-09-07**, before their `main.c` changed (2026-09-22 19:47–19:54), so their current sources have no recorded build; rebuild them before relying on them and log it in TESTING §5.2. Step 10 was fully rebuilt on 2026-09-25 after its last source edits: 0 warnings, 0xea3d0 bytes. README's list was out of date for Step 04 and the root (A-6, fixed). 2026-09-28 (TESTING §5.2): Step 10 0xef240 (the image flashed first that day and soaked), 0xef3f0 and 0xef410 (D-167), 0xef330 (D-168), 0xef360 (the D-168 refinements; the latest, flashed to the C5 on 2026-09-28 at 19:00, TESTING §5.1); Step 08 0xdb340 (with `CSI_LLTF_ONLY` = y from its D-168 build on); root 0xdb350, 0xdb340, then 0xdb350; 0 warnings each. Identify a flashed image by its boot line `ELF file SHA256:`, not by `Compile time:`: the 0xef360 boot of 2026-09-28 printed `ELF file SHA256: 8bcc0f492...` (the ELF of `build10_0928h.log` hashes to `8bcc0f492e10fbc0cd0ed1a5d90a3ff28bd5a5e3734d3008748e32e5f893c8e1`) but `Compile time: Sep 25 2026 20:08:16`, because `esp_app_desc` is not recompiled when only `main.c` changes. Steps 02, 05, 06, 07 and 09 are still last built 2026-09-07.
- **Where:** README § "Verification status — please read".
- **Status:** active

### D-147 Before comparing Step 08 with Step 09, check both score the same frames
- **Decision:** before comparing Step 08's motion with `jitter_value`, its presence branch with `wander_value`, or thresholds with enter/exit levels, confirm with Step 04's histogram that both see the same `csi_data_len`.
- **Why:** esp-radar picks its own CSI acquire configuration, which need not match Steps 03/04. If one side scores HE frames and the other L-LTF, the numbers are not comparable and the benchmark is silently meaningless.
- **Rejected:** none recorded.
- **Evidence:** no comparison has been run (2026-09-25).
- **Where:** `step09_espressif_sensing/main/main.c:16-23`; README § "Step 09 — the benchmark (optional, but do it before tuning)".
- **Status:** unverified on hardware

### D-148 RuView is not adopted as a base
- **Decision:** use RuView at most as a reference for its host protocol.
- **Why:** it compiles for esp32c5 unmodified, but the C5 is not supported (docs/COMPARISON.md §1, §2B); its C5 branch parses the 12-bit L-LTF as int8, which gives garbage (D-41; COMPARISON §4, citing RuView commit dd02efe, checked 2026-09-22); the ESP32 is only a streamer and the models run on a host (COMPARISON §4); its own figures are weak (count 62.3 % on one 0/1 session, COMPARISON §1, §2B). First tracked record: this log (assessment of 2026-09-22, session note, no URL or quote kept): the maintainer's wording "not supported or validated", the branch setting `force_lltf = 1`, and its default pins hitting the C5's flash bus and UART.
- **Rejected:** adopting RuView firmware on the C5.
- **Evidence:** RuView commit dd02efe and issue #1660 (URLs in docs/COMPARISON.md §6), assessed 2026-09-22. The session note also cited issue #622; no tracked file holds a link or quote for it.
- **Where:** docs/COMPARISON.md §1, §2B, §4, §6; docs/REFERENCES.md §5 (RuView entry: site claims only).
- **Status:** active

### D-149 AI-assistant files are git-ignored, so project knowledge must live in tracked docs
- **Decision:** `.gitignore` excludes `CLAUDE.md`, `CLAUDE.local.md`, `.claude/`, `GEMINI.md`, `.gemini/`, `.antigravity/`, `.aider*` and `.mcp.json`.
- **Why:** they hold local agent configuration, memory and session state, all machine-specific, and `.claude/settings.local.json` can hold tokens. Consequence: anything recorded only there is invisible to a fresh clone, which is why this log exists.
- **Rejected:** none recorded.
- **Evidence:** the user's choice in `.gitignore`.
- **Where:** `.gitignore:56-71`.
- **Status:** active

### D-150 Git state on 2026-09-25: leave the index alone; ten paths are untracked and the index is stale
- **Decision:** agents do not run `git add`, `commit`, `stash` or `reset`. Most files were staged mid-session on 2026-09-24 (origin unknown). Untracked on 2026-09-25 (`git status`, `??`): `docs/ARCHITECTURE.md`, `docs/DECISIONS.md`, `docs/TESTING.md`, `docs/COMPARISON.md`, `tools/kconfig_matrix.py`, `tools/serial_capture.py`, `step09_espressif_sensing/.vscode/`, `step10_recognise_count_vitals/main/person_vitals.c`, `main/person_vitals.h` and `test/test_pvitals.c`. **The index is older than the working tree:** 17 staged files (`AM`/`MM`, for example Step 10's `main/main.c`, `main/Kconfig.projbuild`, `test/test_e2e.c`, `README.md`, `docs/REFERENCES.md`, `tools/presence_monitor.py`, `tools/vitals_validate.py`) have further unstaged edits, and `tools/csi_monitor.py` has unstaged edits only (`git diff --stat`: 18 files, 2,370 insertions).
- **Why:** the user's instruction. The owner must add every untracked path in the same commit as the rest: `main/CMakeLists.txt` and `run_tests.ps1` reference the three Step 10 files, so a commit without them would not build or test from a clean checkout; TESTING §2.2 and §2.5 depend on the two tools; and the four docs are where the project knowledge lives (D-149). Files shown `AM` or `MM` must be re-staged (`git add <file>`) before committing, or the commit gets the older staged copy.
- **Rejected:** none recorded.
- **Evidence:** `git status` and `git diff --stat`, 2026-09-25.
- **Where:** `step10_recognise_count_vitals/main/CMakeLists.txt:10-11`; `step10_recognise_count_vitals/test/run_tests.ps1:45-47`; docs/ARCHITECTURE.md §2.2.
- **Status:** active

### D-174 Research sweep and checks of 2026-10-30/11-01: heart-rate and AGC failure modes in the host tests, a first replay of real recordings; fixes proposed, not decided
- **Decision:** (2026-10-30, reviewed 2026-11-01) **Measure and record; change no firmware yet.** What changed: test code only. `test_vitals.c` gained (9)-(12b), the heart-rate failure modes the sweep reported for other CSI code; `test_e2e.c` gained scenario (f), an assumed multiplicative AGC receiver and a shadow of Step 08's static branch; where today's code fails, the tests print **KNOWN LIMIT** lines with the numbers instead of failing (TESTING §3.1, §3.3 items 10-11). COMPARISON §7 was corrected the same day (the 9-node study superseded; ESPectre's licence and holdout). Nothing under `main/` changed. Outside the repo, a host port of Step 08 replayed six public scripted trials (TESTING §5.3). **Proposed, not decided (each waits for the user):** (1) **F2**: in `vitals_push`, fill the empty 100 ms bins of a silence (≤ 1 s) by linear interpolation toward the packet that ends it, instead of repeating the last value. It would supersede D-85 (whose *Rejected* lists interpolation) and change the evidence D-88 rests on; it exists only as `~/ESP32C5_CSI_logs/2026-10-30_step1/hr/F2_scratch.diff`. (2) **Log the receiver's AGC and FFT gain per packet** in the first live session, to measure real gain steps before choosing (3) (TESTING §4). (3) **AGC handling**, the options the (f) lines print: re-anchor D on a gain change (re-seed the slow EMA from the fast one about 1 s later) and skip M for that second; also re-seed the fast EMA and end the vitals still buffer on a gain change; one baseline per gain state; restart calibration on a gain-state change, or compute sigma within constant-gain segments; add the int8 rounding term to each sigma when the live median falls below the calibration median, or recalibrate or warn after a sustained median change of more than 3 dB; or lock the gain (`esp_csi_gain_ctrl_set_rx_force_gain`, "may cause packet loss"). (4) **Thresholds and the freeze cap** (D-62, D-66 to D-70): the replay puts the 5 sd thresholds inside the real empty-room tail and shows runs stuck PRESENT by the static branch under the 600 s cap; 8 sd cut the empty-room false PRESENT but not the stuck runs. No change is proposed until the C5's own TESTING §4 Phase 1 numbers exist. (5) From the sweep, not done: avoid GPIO16-22 on the C5 for any sensor wiring (flash/PSRAM bus); ask for the UW Tacoma ESP32-C5 dataset (IEEE DataPort, login); decide a licence (REPORT §3 items 4-6).
- **Why:** the sweep reported heart rates from noise or a harmonic in other CSI vitals code, and that receiver AGC distorts CSI multiplicatively, which additive-noise synthetic rooms cannot show (arXiv 2607.01882, 2605.26836). Tests that print a measured shortcoming give each decision its numbers before any code changes; changing `vitals.c` or the front end on synthetic evidence alone would trade one synthetic result for another. The replay is the only real-channel evidence the project has, but it is another chip, sounding, tone set and packet timing, so it informs the hardware session; it does not set defaults.
- **Rejected:** (a) **Applying F2 now:** it passed the 2,472-check suite of 2026-10-30, but it reverses D-85, which rejected interpolation, and no F2 run against the reviewed 2,499-check suite is in the logs; the user decides. (b) **Turning the KNOWN LIMIT lines into failing checks:** the suite would be red for behaviour that is documented and not yet fixed, and a red suite hides new failures. (c) **Tuning thresholds or the freeze cap from the replay:** not C5 data (TESTING §5.3 caveats); its trials are too short to show a 600 s cap end. (d) **Reporting the replay as an accuracy:** D-139.
- **Evidence:** TESTING §5.2 rows 2026-10-30 (research sweep; `test_vitals`; `test_e2e` (f)) and 2026-11-01 (replay; review and final run), §5.3. Final host run (Ubuntu, gcc 15.2.0 `-Werror`): 9 suites, 11,673 checks, 0 failures, 15 KNOWN LIMIT lines (vitals 10, e2e 5); seeds 5000-5008 as before (5004, 5007 fail only e1). Replay medians over 336 link runs per mode: PRESENT 1.00 of seated and walking time, 0.03-0.10 and 0.08-0.16 of the two empty phases, 20-50 onsets/h, 52 / 37 / 56 runs stuck through the second empty phase. A review of the work found 18 defects: 16 fixed, 1 in part, 1 rejected with a reason (the reviewing session's own count; no review record was saved; `after_review/` holds the before/after files and patches). Everything is host-side: the board was not connected, MSVC `run_tests.ps1` was not run, no firmware was built (no `main/` change), nothing was committed. Raw logs: `~/ESP32C5_CSI_logs/2026-10-30_research/` and `~/ESP32C5_CSI_logs/2026-10-30_step1/` (`hr/`, `agc/`, `replay_tools/`, `out/`, `after_review/`), outside the repo.
- **Where:** `step10_recognise_count_vitals/test/test_vitals.c:714-1240` ((9)-(12b)); `step10_recognise_count_vitals/test/test_e2e.c:76-94` ((f)), `k_agc_cfg` `:2524`, `test_agc` `:3189` (on 2026-11-01); docs/TESTING.md §3.1, §3.3 items 8 and 10-12, §4 (*Keep for every session*), §5.2, §5.3; docs/COMPARISON.md §7; D-56, D-62, D-66 to D-70, D-85, D-86, D-88 to D-90, D-140, D-142, D-144, D-171 to D-173 carry the evidence lines.
- **Status:** proposals pending user decision. The test changes are in place; F2, gain logging, AGC handling and any threshold or cap change are not decided; nothing of it has run on hardware. 2026-11-03: two proposals were decided and implemented on the user's go-ahead, host and build only: (1) F2 → **D-175** (supersedes D-85), (2) gain logging → **D-176**; neither has run on the board. (3) AGC handling and (4) threshold and freeze-cap changes remain open and wait for real C5 data (TESTING §4); (5), the sweep's other notes (GPIO16-22, the UW Tacoma dataset, a licence), is still not done.

---

## 8. Hardware bring-up gotchas

### D-151 First hardware flash: Step 01 on the C5 at COM13, 2026-09-25
- **Decision:** record the milestone. Step 01 flashed to the board on COM13 (base MAC `30:ed:a0:e4:50:b8`) printed its heartbeat and a free heap of 338,196 B. This is the **only** hardware result so far. No CSI captured; no accuracy of any kind measured. Steps 03/04 are built with the user's laptop-hotspot credentials but not yet run.
- **Why:** nothing had run on silicon before. Step 01 exists because "if this step does not print, nothing later in this project can possibly work": it prints chip info (IDF version, cores, revision, flash size, heap, MAC, the Wi-Fi BGN and BLE flags) and then a heartbeat with free heap every second.
- **Rejected:** none recorded.
- **Evidence:** hardware session 2026-09-25, logged in TESTING §5.1 (README *Verification status* updated the same day, A-1). The README's example "340000" free heap is illustrative.
- **Where:** `step01_hello/main/main.c:1-58`; README § "Step 01 — Does anything work at all?".
- **Status:** active

### D-152 Two boards, and the port settings disagree
- **Decision:** know which board is where before flashing. COM13 (base MAC `30:ed:a0:e4:50:b8`) is the **second** board; it shipped running an unrelated "TPS61165 PWM Control" firmware, and the user said on 2026-09-23 to flash it with no backup. The first board (rev v1.0, base MAC `d0:cf:13:e0:0b:f8`, USB `303A:1001`) was on COM37. On 2026-09-24 the C5's USB-Serial-JTAG enumerated on COM13; COM27 is an unrelated CP210x.
- **Why:** a wrong port flashes the wrong device or fails. The settings do not agree: root `.vscode/settings.json` sets `idf.port` to COM13 but its comment still says COM37 is the C5; `ESP32C5_CSI.code-workspace` sets COM37; Step 10's settings use `detect`; README examples use COM12, which is only an example.
- **Rejected:** none recorded.
- **Evidence:** session notes 2026-09-22 to 2026-09-24; flash of 2026-09-25 on COM13. 2026-11-03 (the session's own report; no port listing saved): a board on `/dev/ttyACM0` on the Ubuntu side, USB 303a:1001, serial `38:44:BE:BF:9D:9C` (pyserial `list_ports`, port not opened), matches neither board above (COM13 `30:ed:a0:e4:50:b8`, the first `d0:cf:13:e0:0b:f8`). It was not touched or flashed; the user was asked which board it is (TESTING §5.2, 2026-11-03).
- **Where:** `.vscode/settings.json:73-87`; `ESP32C5_CSI.code-workspace:54-55`; `step10_recognise_count_vitals/.vscode/settings.json:20-21`.
- **Status:** active

### D-153 Never pulse RTS/DTR from pyserial on the C5's USB-Serial-JTAG under Windows
- **Decision:** reset the board only through esptool or `idf.py`. When reading the port with a pyserial script, leave RTS and DTR alone.
- **Why:** on 2026-09-25, pulsing RTS/DTR from pyserial latched the chip in reset; esptool then got "Write timeout", and the physical RST button was needed to recover.
- **Rejected:** resetting the board by pulsing RTS/DTR from pyserial.
- **Evidence:** first-flash session, 2026-09-25.
- **Where:** first tracked record: this log.
- **Status:** active

### D-154 `build.ps1` with redirected output exits 1 even when the build succeeded
- **Decision:** run `.\build.ps1` without `*> file`, `2>&1` or `*>&1 | Tee-Object`. When a run must be redirected, judge it by `Project build complete` / `binary size` in the log, not by the exit code.
- **Why:** ESP-IDF's Kconfig `NOTE:` lines go to stderr. With stderr redirected, Windows PowerShell 5.1 wraps them as errors and sets `$?` false, so `build.ps1`'s `if (-not $?)` prints "BUILD FAILED" and exits 1 while Ninja finished every step.
- **Rejected:** trusting the exit code of a redirected run.
- **Evidence:** 2026-09-25: a `*>&1 | Tee-Object` run ended in "BUILD FAILED" although all 1,089 build steps ran with 0 warnings, 0 errors and "Project build complete"; re-run without redirection, exit code 0.
- **Where:** `build.ps1:247`.
- **Status:** active

### D-155 `build.ps1` changes directory: call it by its full path in loops
- **Decision:** in a loop or script that runs `build.ps1` more than once, call it as `D:\ESP32C5_CSI\build.ps1`, not `.\build.ps1`.
- **Why:** `build.ps1` does `Set-Location` into the step folder, so a second relative `.\build.ps1` call resolves against the wrong directory.
- **Rejected:** none recorded.
- **Evidence:** observed 2026-09-25.
- **Where:** `build.ps1:228`.
- **Status:** active

### D-156 Other Windows tooling gotchas
- **Decision:** (1) in an automated tool call, flash with `idf.py -p COMn flash` and read the port separately; `idf.py flash monitor` never exits there. (2) Do not name a PowerShell variable `$args`. (3) On `ninja: error: remove(...) ... being used by another process`, just build again. (4) Shell state does not persist between automated calls: activate `export.ps1` in every call that needs `idf.py` or esptool.
- **Why:** (3) is a file lock (antivirus or two builds at once), not a code error.
- **Rejected:** none recorded.
- **Evidence:** session notes, dated up to 2026-09-25.
- **Where:** README § "What I verified, and what I could not" (the ninja lock); otherwise first tracked record: this log.
- **Status:** active

### D-157 A board that Windows refuses to open needs a replug or BOOT+RESET
- **Decision:** if a COM port enumerates but will not open ("device attached ... not functioning", error 31), unplug and replug it or press BOOT+RESET before blaming the toolchain.
- **Why:** on 2026-09-22 the first board enumerated on COM37 but Windows refused to open it this way.
- **Rejected:** none recorded.
- **Evidence:** session note, 2026-09-22.
- **Where:** first tracked record: this log.
- **Status:** active

### D-158 Flash with plain `idf.py -p COMx flash` from a running app; BOOT+RST only when the chip is latched
- **Decision:** flash with `idf.py -p COM13 flash` while the board is running an app. If esptool reports "Write timeout" at "Connecting...", treat the chip as latched (the cause is not established; TESTING G20): hold BOOT, tap RST, release BOOT, then run `esptool --chip esp32c5 -p COM13 --before no-reset --after no-reset write-flash @flash_args` from the step's `build` folder, and press RST to run the new firmware.
- **Why:** on 2026-09-25 the default reset worked twice (Step 01, Step 04) from a running app. It failed only after a pyserial script had pulsed RTS (D-153). `--after watchdog-reset` flashed Step 03 correctly, but the chip then neither ran nor answered esptool until RST was pressed.
- **Rejected:** `--after watchdog-reset` (the chip stayed silent), `--before usb-reset` (Write timeout while latched), and releasing the latch by writing DTR/RTS = (1,1) from pyserial (no effect).
- **Evidence:** first-flash session console, 2026-09-25 (not kept; first tracked record: this log and TESTING §6; §5.1 rows 0a and 0d/0e record only that Steps 01 and 04 then ran). 2026-09-28 (TESTING §5.1, the flashing row; TESTING §6 G20): `idf.py -p COM13 flash` failed with `Write timeout` about a minute after an `esptool chip_id` that had worked and that ended with esptool's own `Hard resetting via RTS pin`; the failing flash itself began with esptool's `--before=default-reset`. The pre-D-165 Step 10 was last seen running before the `chip_id`; whether it was running at the moment of the flash was not checked. No pyserial *script* pulsed RTS/DTR (`serial_capture.py` holds both low). Whether esptool's reset latched the chip (D-153) is not established; n = 1. So "plain flash from a running app" held for Steps 01 and 04 on 2026-09-25 and was not shown for Step 10 until the 19:00 flash below. After BOOT+RST, `--before no-reset --after hard-reset` wrote and verified the images, but the board printed nothing until RST was pressed (probably the app had not started): `--after hard-reset` (2026-09-28) left the chip idle until RST, as `--after watchdog-reset` did on 2026-09-25 (how that flash was entered was not recorded). `--after no-reset` itself has not been run; it is prescribed because both of those still needed RST. 2026-09-28 19:00 (TESTING §5.1): the port was confirmed with pyserial's `list_ports`, which does not open it (COM13, VID 0x303a, PID 0x1001, serial number `30:ED:A0:E4:50:B8` = the BASE MAC), and **no `esptool chip_id` was run before the flash**. With the 0xef240 Step 10 last seen at 18:57 in its reason-201 and self-restart cycle (hotspot off; its state at 19:00:15 was not observed), a plain `idf.py -p COM13 flash` (`--before=default-reset --after=hard-reset`) connected, wrote and verified all three images (`Hash of data verified` ×3) and ended with `Hard resetting via RTS pin...` and `Done`: **no `Write timeout`**. The app then ran without an RST press: a capture opened about 9 s after the flash ended found it at uptime about 10 s (`W (10061) ... retry 3/10`). Its banner came before the capture opened, so this is inferred from the uptime. So "plain flash from a running app" held once for Step 10, assuming the old image was running at 19:00:15 (likely, since its cycle spends only a moment in each reboot, but not observed). The same `--after hard-reset` was followed by silence until RST after the BOOT+RST entry at 13:19, and by a running app here after esptool's own default reset; n = 1 each, and why is not established. Whether leaving out `chip_id` is why this flash had no `Write timeout` is not established either.
- **Where:** first tracked record: this log; TESTING §6 (G1, G20).
- **Status:** active

### D-159 A Windows Mobile Hotspot works as the AP, but it switches itself off and sends only legacy 11g frames
- **Decision:** a laptop hotspot is acceptable for bring-up. Turn off its "power saving" option (Settings → Network → Mobile hotspot), and use the L-LTF path (`CSI_LLTF_ONLY=y` in Steps 08/10) with it. For the full HT/HE path (more subcarriers, int8), use a real 802.11n/ax router.
- **Why:** on 2026-09-25 the hotspot ("Testing", 2.4 GHz, channel 1) had switched itself off, so the board's first ten joins failed with reason 201 (AP not found). Once it was on, it answered the board's pings at about 50/s, but 100 % of those replies were 106-byte 11g/11a L-LTF frames. Steps 03 and 05–07 capture HT/HE only, so they received 0 CSI.
- **Rejected:** none; this records the measured behaviour of this AP.
- **Evidence:** TESTING §5.1 rows 0b–0e; logs in `docs/hw_logs/2026-09-25_*`. 2026-09-28: the hotspot was Off again (WinRT `TetheringOperationalState`), the board looped on reason 201, and after `StartTetheringAsync` (about 12:01) the hotspot was next observed at about 13:51 on **channel 6**, not 1: the board logged `wifi:new:<6,0>, old:<1,0>`, and Step 10's `status` said `channel 6` (TESTING §5.1). Its channel in between was not observed. So this hotspot does not keep its channel across restarts (seen once), which is what exposed D-167. 2026-09-28 19:00 (TESTING §5.1): the hotspot was off again and was not turned on in that session (18:57–19:05); both Step 10 images, 0xef240 before the flash and 0xef360 after it, looped on reason 201.
- **Where:** Step 03 `main.c` acquire config (legacy off); Step 04 histogram; `CSI_LLTF_ONLY` in the Step 08/10 Kconfig.
- **Status:** active

### D-160 The C5's L-LTF CSI is 12-bit packed: confirmed on silicon
- **Decision:** keep decoding 106-byte L-LTF frames as 12-bit samples in little-endian 16-bit words, 26 subcarriers per frame, with sign extension done in software (the decode behind `CSI_LLTF_ONLY`). Do not treat them as 53 int8 pairs.
- **Why:** the hypothesis came from esp-idf #18118 and was unverified until 2026-09-25. Step 04 on real silicon then found the odd-byte high nibble at 0 in 100.0 % of 2,163 frames. Plain int8 pairs would give a spread of high nibbles, so the frames are 12-bit words with no sign extension by the hardware.
- **Rejected:** decoding as int8 (the RuView C5 path does this and gets garbage; COMPARISON §4).
- **Evidence:** `docs/hw_logs/2026-09-25_step04_hotspot_ch1.log`; TESTING §5.1 row 0d/0e.
- **Where:** Step 04 histogram ("odd hi-nibble" column); the L-LTF decode in the Step 08/10 `main.c`.
- **Status:** active (confirmed on hardware)

### D-164 Console output when the USB host goes away: what ESP-IDF v6.0.2 does, and what Step 10 adds
- **Decision:** the detector must keep running when nobody reads the console. The finding, from the ESP-IDF v6.0.2 sources (paths under `C:\esp\v6.0.2\esp-idf\components\`):
  1. `printf` and `ESP_LOG` write to picolibc's line-buffered `stdout` (`__BLBF`; `esp_libc/src/picolibc/picolibc_init.c:145`), which goes to the USB-Serial-JTAG VFS. Step 10 switches that VFS onto the driver (`usb_serial_jtag_vfs_use_driver()` in `console_install`; TX ring 2 KB in D-133, 4 KB according to the `CSI_EXPLAIN` Kconfig help written for this change).
  2. While `usb_serial_jtag_is_connected()` is false, the VFS write returns -1 at once (`esp_driver_usb_serial_jtag/src/usb_serial_jtag_vfs.c:182-187`, marked `TODO: IDF-14303`).
  3. "Connected" means the host's USB start-of-frame packets arrive. A tick hook marks the port disconnected after about 3 ms without one and connected again at the next one (`esp_driver_usb_serial_jtag/src/usb_serial_jtag_connection_monitor.c:23-27`, `:38-72`). It is a bus-level signal: it does not say whether any program has the COM port open.
  4. When connected but nobody reads, each character is first offered with a zero timeout. If the ring is full, one retry waits up to 50 ms (`TX_FLUSH_TIMEOUT_US`); once that retry has failed, later characters are dropped without waiting until one is accepted again (`usb_serial_jtag_vfs.c:71-80`, `:709-725`). Both the TX mutex and the ring send in `usb_serial_jtag_write_bytes` are bounded by the same timeout (`esp_driver_usb_serial_jtag/src/usb_serial_jtag.c:265-288`).

  So, as read, a console write from the pipeline can stall it for about 50 ms each time the ring fills (D-133's figure), and not indefinitely. **The silence seen on 2026-09-25 is not explained by the write path alone:** after COM13 disappeared and came back, the board accepted input but printed nothing and did not answer `status`. Candidates not yet ruled out: the chip had reset (D-165 now prints why); a host program still held the stale handle of the old port (`dashboard.py` reopens the port by itself, D-163); or picolibc's handling of the -1 that the VFS returns while disconnected (the line carries `TODO: IDF-14303`; picolibc's source is not in the ESP-IDF tree and was not read). The change Step 10 makes is specified as: console output bounded or non-blocking (for example a `usb_serial_jtag_is_connected()` check, a finite TX timeout, drop when full), so that the detector keeps running with no host. The exact change, and the source lines it relies on, are in the comment at `console_install` in `main.c`.
- **Why:** a pipeline that waits on the console stops taking CSI packets. `dropped` would then count the loss (D-132), but presence, recognition and counting would all stall without a host attached, and a detector must not depend on someone watching it.
- **Rejected:** none recorded.
- **Evidence:** the ESP-IDF v6.0.2 source lines above, read on 2026-09-25. The 2026-09-25 runs are now TESTING §5.1 rows (logged 2026-09-28). **Observed on the C5 on 2026-09-28** (TESTING §5.1): with nobody reading the port for about 14 s and later about 29 s, the D-164 image printed `console: output resumed - for ~14 s no host read it (port closed, or USB re-enumerated) and what was printed then was dropped; the detector kept running (reset reason: POWERON)`, its uptime ran on, `status` reported 0 packets dropped, and `transmit buffer found full at 43 of the 1-s checks (longest run 29 s)`. This was observed only with the USB host connected and the port closed (`status`: `USB host connected`). The disconnected path (point 2) and a re-enumeration like 2026-09-25's have not been exercised. The 2026-09-25 silence itself is still unexplained (G18).
- **Where:** `step10_recognise_count_vitals/main/main.c` (`console_install`, and the output path it changes); docs/TESTING.md §6 G18.
- **Status:** active (for a USB host that is connected with the port closed, TESTING §5.1 2026-09-28; a disconnected host and a USB re-enumeration are still unverified on hardware)

### D-165 Print the reset reason at boot
- **Decision:** Step 10's boot banner prints `esp_reset_reason()` as a name and a number.
- **Why:** on 2026-09-25 the board restarted without explanation, and nothing on the console said why. The values, from `esp_system/include/esp_system.h:25-40` in ESP-IDF v6.0.2: 0 `UNKNOWN`, 1 `POWERON`, 2 `EXT`, 3 `SW` (`esp_restart`), 4 `PANIC`, 5 `INT_WDT`, 6 `TASK_WDT`, 7 `WDT` (other watchdogs), 8 `DEEPSLEEP`, 9 `BROWNOUT`, 10 `SDIO`, 11 `USB` ("reset by USB peripheral"), 12 `JTAG`, 13 `EFUSE`, 14 `PWR_GLITCH`, 15 `CPU_LOCKUP`. One cause is already in the firmware: when the first Wi-Fi join fails, Step 10 waits 5 s and calls `esp_restart()` (`app_main`, after `WIFI_FAIL_BIT`), and a Windows hotspot that had switched itself off caused exactly such failed joins on 2026-09-25 (D-159). That restart reads `SW`.
- **Rejected:** none recorded.
- **Evidence:** specified on 2026-09-25 (first tracked record: this log). **Observed on the C5 on 2026-09-28** (TESTING §5.1): after the RST button the banner printed `last reset: POWERON (1)`, and `status` repeated it. So on this board **the RST button read POWERON** (once), the same as a power cut or a supply loss deep or fast enough to cause a power-on reset. A dip caught by the brownout detector reads BROWNOUT (9), which the banner reports as a power problem. A future restart that reads POWERON therefore cannot be told apart from a button press or a power loss by this line alone. The 2026-09-25 restarts stay unexplained for a different reason: that firmware printed no reset reason, and nothing recorded what was done at the desk (TESTING §5.1). 2026-09-28 19:00 (TESTING §5.1; `docs/hw_logs/2026-09-28_step10_d168_flash_boot_hotspot_off.log`): with the hotspot off, the 0xef360 image logged `retry 10/10` at uptime 33,953 ms and `WiFi failed. Check SSID/password (must be 2.4 GHz).` at 37,367 ms, and 5 s later (`wifi:flush txq` at 42,367 ms) the ROM printed `rst:0xc (SW_CPU)`. The new banner read `last reset: SW (3)`. So the `esp_restart()` after `WIFI_FAIL_BIT` was observed on the C5, and it reads `SW`, as specified above. COM13 stayed open across that restart: the capture has no `port lost` line and recorded the whole reboot (TESTING G19). The pre-D-167 image (0xef240) also read `last reset: SW (3)` in the 18:57 pre-flight listen, while it looped on reason 201.
- **Where:** `step10_recognise_count_vitals/main/main.c` (`app_main`; grep `esp_reset_reason`).
- **Status:** active

### D-170 Dual boot: machine paths only in per-OS user settings; Ubuntu on ESP-IDF v6.1 with its own build dir and sdkconfig
- **Decision:** (user's choice, 2026-10-12) The 11 tracked `.vscode/settings.json` files and `ESP32C5_CSI.code-workspace` carry no machine paths or ports. `idf.currentSetup`, `idf.customExtraVars`, `idf.customExtraPaths`, `idf.port`, `clangd.path` and `clangd.arguments` live in each OS's Antigravity user settings (templates and reasons: `docs/ide/`). Ubuntu uses ESP-IDF **v6.1** from EIM at `~/.espressif`; Windows stays on v6.0.2. On Ubuntu every project builds into `~/esp-build/<abs path>` with its own `sdkconfig` there, copied once from the Windows one (`tools/linux_idf.sh`). Step 09 is refused on any IDF other than 6.0.x.
- **Why:** the repo is one NTFS checkout opened from both OSes. Folder settings override user settings for the extension's Resource-scoped keys (its `package.json`), so the `C:\` paths broke the extension on Linux. Windows `build/` folders hold a Windows `CMakeCache.txt`. A v6.1 build would rewrite `sdkconfig` (and Step 09's `dependencies.lock`, seen 2026-10-12) under the verified v6.0.2 Windows builds. The NTFS partition was 96 % full.
- **Rejected:** (a) Install ESP-IDF on Linux and leave the tracked files alone: the extension stays broken on Linux. (b) v6.0.2 or v6.0.3 on Linux, matching Windows: offered as the recommendation, but the user chose v6.1. (c) Build dirs inside the tree (`build-linux/`): NTFS is nearly full, and clangd's `.clangd` `CompilationDatabase: build` would still need overriding.
- **Evidence:** TESTING §5.2, the two 2026-10-12 rows (10 of 11 projects build with 0 warnings on v6.1; Step 09 fails; clangd check) and the three 2026-10-16 rows (same sizes rebuilt; host tests and Kconfig matrix on Linux); TESTING §6 G21. No v6.1 image has run on the C5.
- **Where:** `docs/ide/README.md`, `docs/ide/windows-user-settings.jsonc`, `tools/linux_idf.sh`, `.vscode/settings.json`.
- **Status:** active (build only); Windows needs `docs/ide/windows-user-settings.jsonc` pasted once into its user settings.

### D-171 Host tests and the Kconfig matrix also run on Ubuntu (gcc -Werror; `--build-dir`)
- **Decision:** (2026-10-16) `step10_recognise_count_vitals/test/run_tests.sh` mirrors `run_tests.ps1` (same suites, modules and order) with `gcc -std=c11 -O2 -Wall -Wextra -Werror`, binaries outside the tree. `tools/kconfig_matrix.py` gains `--build-dir` (default `<project>/build`, so Windows is unchanged) for the `~/esp-build` builds of D-170. The two places gcc flagged in test code were fixed in the test code (`test_pvitals.c` `rec_zero`: an uninitialised `static const`, zero-filled; `test_util.h` `rng_gauss`: `static inline`), keeping `-Werror`.
- **Why:** after D-170 the Ubuntu side could build but not run the L1/L2/L3 checks: `run_tests.ps1` needs MSVC, and the matrix looked only in the in-tree `build/`. A second compiler also checks the tests' portability.
- **Rejected:** (a) `-Wno-missing-field-initializers -Wno-unused-function`: a lower bar than MSVC `/W4 /WX` and the firmware's own warning count. (b) Writing binaries into `test/bin`: the tree is shared NTFS and nearly full (D-170). (c) A symlink `build -> ~/esp-build/...`: it would shadow the Windows `build/`.
- **Evidence:** TESTING §5.2, rows 2026-10-16: 8 suites / 11,453 checks equal to Windows; 192 combinations, 0 warnings, negative control fails. 2026-10-30 and 2026-11-01 (TESTING §5.2): the runner carried the new `test_vitals` and `test_e2e` code through gcc `-Werror` (11,641 checks, then 11,673 after the review, 0 failures), and the same gcc flags drive the e2e seed sweep with `-DE_SEED` (`~/ESP32C5_CSI_logs/2026-10-30_step1/agc/seeds.sh`). A re-run on 2026-11-01 reproduced the suite output (`~/ESP32C5_CSI_logs/2026-11-01_docs/host_tests_rerun.log`).
- **Where:** `step10_recognise_count_vitals/test/run_tests.sh`, `test_pvitals.c:112`, `test_util.h:66`, `tools/kconfig_matrix.py` (`--build-dir`; exits with that hint if it finds a Windows `.exe` compiler on Linux); TESTING §2.1, §2.2.
- **Status:** active on Ubuntu; the two test edits not yet re-run under MSVC. 2026-10-16: `run_tests.sh` and `run_tests.ps1` both list the new `activity` suite (D-172); only the gcc runner has run it (TESTING §5.2). 2026-11-01: the 2026-10-30 test code has also run under gcc only; two `CHECK`s in `test_e2e.c` (`f_find`, `f_row`) use a variable condition so that MSVC `/W4` would not raise C4127, which is untested. 2026-11-03: both runners list a tenth suite, `gain` (D-176; `run_tests.sh:62`, `:74-75`; `run_tests.ps1:49-50`); only the gcc runner has run it (11,705 checks after the review, TESTING §5.2).

### D-173 Research of 2026-10-16: v6.1 changes nothing for CSI; what current videos and projects show; a mmWave radar as a possible reference sensor (not decided)
- **Decision:** (1) **No CSI reason to change ESP-IDF version.** Windows stays on v6.0.2 and Ubuntu on v6.1 (D-170); v6.1 brings no CSI change. Flash from v6.1 with `idf.py flash -a` after an `erase-flash` or onto a new chip (TESTING G22). Step 09 still builds only on v6.0.x. The `esp_wifi_sensing` 0.1.1~3 bump is optional and not made. (2) **What the survey supports** (how this project reads the prior art; COMPARISON §7): single-node motion on the chip is real; still presence on a single node is unshown; on-chip breathing has no published accuracy; heart rate, counting and activity classes have so far needed several devices or a dedicated sender, a host or NPU, and labelled training. So `hr_bpm_exp` stays experimental, the counter stays 1 vs 2+, and only the untrained coarse activity state of D-172 was built. (3) **A reference sensor is an option, not a decision.** The video the user pointed to (How To Electronics, "Build an ESP32 Human Tracker with Radar, Thermal Sensing & Vital Signs", 2026-10-08) uses a DFRobot C1001 (SEN0623) 60 GHz mmWave module that computes presence, distance, breathing, heart rate and sleep stage itself. It is a different modality, not Wi-Fi CSI, and nothing to copy into the firmware; logged beside the C5 it could serve as a second occupancy and breathing record for TESTING §4 (Phase 1, Phase 3). The user has been asked; nothing is bought or built.
- **Why:** before building on v6.1 or on any video's claim, check them. The CSI comparison: `wifi_csi_acquire_config_t` (v6.1 `esp_wifi_he_types.h:44-65`), `wifi_csi_info_t` and `rx_ctrl` identical, the CSI calls unchanged (v6.1 `esp_wifi.h:1285`, `:1300`, `:1313`, `:1339`), no new CSI Kconfig option, the C5's `soc_caps.h` Wi-Fi capabilities unchanged, the same CSI strings in `libpp.a`. v6.1's Wi-Fi changes are NAN security, random STA MAC, OWE softAP, PASN, DPP and an FTM `ppm` field. The 8-bit L-LTF mode (53 tones) is not new: it is in v6.0.2 (`esp_wifi_he_types.h:63`), and D-41 chose 12-bit. A reference sensor matters because no run so far recorded who was in the room, and the session tools take the cue schedule as the truth (TESTING §2.7).
- **Rejected:** (a) Changing IDF version for CSI: nothing to gain. (b) Posture, an ML classifier, fall detection, a better counter, heart-rate changes: no single-node evidence on an FPU-less chip (D-172 Rejected; COMPARISON §7). (c) Treating the radar module's numbers as ground truth: its maker publishes no accuracy, its chest window is about 0.4-1.5 m, its serial protocol is not publicly documented, and it is "not for diagnosis" (the article). (d) The 8-bit L-LTF switch and ESPectre's experimental `wifi_raw` acknowledgement source: left as hardware A/B experiments for later; `wifi_raw` also conflicts with D-40 (`dump_ack_en = 0`).
- **Evidence:** 2026-10-16, reading only; nothing built or run for it. First tracked record: this log and TESTING §5.2 (row "Research, no build"); the session's research notes and sources are copied to `~/ESP32C5_CSI_logs/2026-10-16_research/` (outside the repo: `plan.md`, `gh/espectre_perf.md`, the `libpp`/`libnet80211` strings as `*_strings.txt`, the header diff `inc.diff`, the registry JSON). The closed Wi-Fi libraries were compared **by their strings only**, so an internal change cannot be ruled out. `esp_csi_gain_ctrl` 0.1.5 and `esp_radar_motion_dec` 0.1.1 ship libraries for IDF 4.4, 5.0-5.5 and 6.0, no 6.1 (`step09_espressif_sensing/managed_components/`). ESPectre's performance report (last update 2026-10-07): C5 F1 98.9 % / 99.5 %, host replay of HT20/HT-LTF recordings. The videos in the survey were **not watched**: their claims come from titles, descriptions, dates and view counts. The radar video: title, description and the article <https://how2electronics.com/diy-human-tracking-vital-signs-monitor-esp32-c1001-amg8833/>, read 2026-10-16; its claims are the maker's. 2026-10-30: the second research sweep (D-174) found the verdict of (2) unchanged; it reported C1001 users disagreeing on its vitals (one "+/- 20 beats" and PRESENT 60 s after leaving, another "within a couple of points"), so if adopted, use it as a presence reference and keep a chest strap for heart rate (`~/ESP32C5_CSI_logs/2026-10-30_research/REPORT.md` §2).
- **Where:** docs/COMPARISON.md §1, §2D, §5, §7; docs/TESTING.md §2.4 (Ubuntu note), §4 Phase 1 (notes), §5.2, G22; `tools/linux_idf.sh` (unchanged: `flash` runs plain `idf.py flash`; `idf <step> ... flash -a` passes `-a` through).
- **Status:** active (findings as of 2026-10-16); the reference sensor is an open option.

---

## Appendix A. Documentation known to lag the code (2026-09-25)

These are not decisions. They are places where a tracked document or comment
says something the code or the dated record no longer supports. They are
listed so nobody builds on them; fix them in their own files and strike the
row here when done. Where two sources disagree and nothing settles it, the
row says **unresolved**.

| # | Where | What it says | What is true |
|---|---|---|---|
| A-1 | ~~README § "Verification status" ("NOT tested on hardware. No ESP32-C5 has been flashed"); README § "What I verified, and what I could not" ("no ESP32-C5 has ever been connected"); README § "Read this before you start" ("this repo does it"; moving-person detection "works well")~~ | nothing has run on a board; detection works | **Fixed in README on 2026-09-25**: Step 01 ran on the C5 at COM13 (D-151); no CSI step has run and no accuracy has been measured, so neither "does it" nor "works well" had a source. |
| A-2 | ~~`docs/COMPARISON.md:8`, `:31`~~ | "never flashed" | as A-1. **Fixed 2026-10-16**: COMPARISON's opening and its §1 row now say Steps 01, 03, 04 and 10 have run on one C5 for bring-up (TESTING §5.1), Step 08 never, no accuracy |
| A-3 | README Steps 03–05 example output; README § "Step 06 — Calibrate against an empty room" example ("selected 52 of 64 subcarriers"); README § "Step 08 — The finished detector ⭐" expected output ("CSI shape locked: 128 bytes, 64 subcarriers") | 128-byte / 64-subcarrier / "11g" shapes | 11g is not captured by default (D-40), and 128 is not in the legitimate C5 set (D-44) |
| A-4 | README § "Reading the raw layout" | amplitude = √(buf[2k]² + buf[2k+1]²) for every shape | true for HT/HE-LTF only; C5 L-LTF is 12-bit in 16-bit words (D-41) |
| A-5 | `step03_csi_hello/sdkconfig.defaults:53` (line 53 in Steps 03–09 and the root; line 33 in Step 01, 35 in Step 02); `step10_recognise_count_vitals/sdkconfig.defaults:64` | "This board has never been flashed" | Step 01 was flashed on 2026-09-25; the assertion decision itself still stands (D-16) |
| A-6 | ~~README § "Verification status"~~ | Steps 02, 04–07, 09 and the root not rebuilt since 2026-09-22 | **Fixed (checked 2026-09-28)**: README § "Verification status — please read" now names only Steps 02, 05–07 and 09 as not rebuilt since those edits, and gives the root's 2026-09-28 build. The finding was: out of date only for Step 04 (rebuilt 2026-09-25) and the root (rebuilt 2026-09-24). Steps 02, 05, 06, 07 and 09 really have not been rebuilt: their `build/*.bin` are dated 2026-09-07 and their `main.c` 2026-09-22 (D-146). If they are rebuilt, log it with a date in TESTING §5.2 |
| A-7 | README § "Step 03 — First CSI capture" | "`dump_ack_en = 1` gives extra CSI from ACK frames" | the code sets `dump_ack_en = 0` and `acquire_csi_legacy = 0` by default (D-40) |
| A-8 | README § "Step 05" sample output | "CSI mode: all LTFs" | Step 05 prints "HT/HE-LTF" in the default mode (`step05_amplitude/main/main.c:407`) |
| A-9 | README § "Why the radio is locked" snippet | `WIFI_BW_HT20` | the code uses the `CSI_WIFI_BW20` shim (D-04) |
| A-10 | README § "Working in Antigravity"; `.vscode/settings.json:28` comment | extension v2.1.0; "2.1.0 and 2.2.0" installed | the IDE loaded v2.2.0 (2026-08-24) and auto-updated to v2.3.0 on 2026-09-23, the version that needs D-29 |
| A-11 | `.vscode/settings.json:73` comment; `ESP32C5_CSI.code-workspace:54-55` | COM37 is the C5 | `idf.port` is COM13 in the same file; COM13 is the second board (D-152) |
| A-12 | `docs/REFERENCES.md` §1.1 | one signed byte each for I and Q; 106 = 1 × 53 | C5 L-LTF is 26 subcarriers of 12-bit samples in a 106-byte frame (D-41); the length set is still right |
| A-13 | `docs/REFERENCES.md` §1.3, §1.4 | esp-idf #18493 read as a C5 5 GHz silicon bug; `force_lltf` as "the right first move"; cites `step05_amplitude/main/main.c:320` | the 2026-09-22 reading: the frozen-IQ symptom was the 12-bit L-LTF default plus a promiscuous 20/40 MHz limit (5 GHz still unproven with a home router); on the C5 `force_lltf` makes int8 parsers worse; the CSI config is now at `step05_amplitude/main/main.c:361-407` |
| A-14 | `docs/REFERENCES.md` §6 item 2; §2.1 (`active_filter_ms`) | "Add a hold timer … This repo has none"; the anti-flicker Steps 07/08 need | Steps 07/08 have a 5 s hold and 5.0/2.5 sd hysteresis (D-67, D-69). Whether an entry debounce like ESPectre's `motion_on_hits` is worth adding was not checked. |
| A-15 | every `sdkconfig.defaults` -O2 comment; `docs/REFERENCES.md` §1.2, §7 vs `docs/COMPARISON.md:232` | "~290 packets/s on this chip" (esp-csi-rs, station mode) vs "~290/s on C5: that figure was measured on an S3" | **unresolved**. D-11 and D-15 do not depend on the exact figure. |
| A-16 | every `sdkconfig.defaults` -O2 comment; `docs/REFERENCES.md` §7 | the CSI amplitude loop runs inside the Wi-Fi task callback | the callbacks only copy and queue; the maths runs in a worker task (D-39) |
| A-17 | `.gitignore:3` ("nine of them"); `build.ps1:27` ("All eight steps"); `CMakeLists.txt:10` ("The eight step folders"); `docs/REFERENCES.md` §7 ("All nine projects") | 8 or 9 projects | ten steps plus the root project |
| A-18 | `step09_espressif_sensing/sdkconfig.defaults:1` | header "Step 03: first CSI capture" | copied from Step 03 unchanged; the settings themselves are intended |
| A-19 | `step03_csi_hello/sdkconfig.defaults:20-21` (and every copy) | the 2 MB partition is for "the web UI" | no web UI exists (D-19) |
| A-20 | `.vscode/tasks.json:187` | "Tools: Presence monitor + plot (Step 08)" | the same tool reads Step 10 |
| A-21 | `step09_espressif_sensing/main/main.c:19-20`, `:148-149`; README § "Step 09 — the benchmark" ("its single threshold against Step 09's *two*") | Step 08 has "a single threshold"; "Step 06's 45 s calibration"; 5 GHz a live hazard (citing REFERENCES §1.3) | Step 08 has trigger and release thresholds (D-67); Step 06 takes about 35 s, 45 s is Step 08's first boot with the countdown (D-62, D-73); see A-13 |
| A-22 | ~~README § "Features" / § "Recalibrating" (Step 08 recalibrates when the "channel changed")~~ | a channel change is detected | **Fixed 2026-09-28** (D-168): observed on the C5 in Step 10 on 2026-09-28 (a channel-1 baseline used on channel 6, TESTING §5.1). The channel is now part of the match: Steps 08 and 10 recalibrate on a mismatch at shape lock, and Step 10 also after lock (not yet run on a board). README § "Features" says so. Step 08 still does not notice a channel change after lock |
| A-23 | `step10_recognise_count_vitals/main/person_vitals.h:42`; `step10_recognise_count_vitals/main/main.c:673`, `:3462`; `step10_recognise_count_vitals/main/Kconfig.projbuild:442` | vitals log "~170 bytes" | exactly 168 bytes (D-120) |
| A-24 | `step10_recognise_count_vitals/main/main.c:1898-1901` (comment) | one-route enrolment left 78 % "unknown" | **resolved 2026-09-25**: the default `test_e2e` run prints "79.4% unknown" and walks A 1/0/8, B 0/3/6, i.e. 14 of 18 walks (TESTING §5.2), matching README § "Enrolling a person". The `main.c:1901` comment was corrected to 79 % on 2026-09-25 (D-96) |
| A-25 | `step06_baseline/main/main.c:246` | the baseline is "~2 KB" | about 3.5 KB (D-114) |
| A-26 | `step01_hello/main/main.c:45` | the printed flags show "a dual-band Wi-Fi 6 part" | the code prints only the Wi-Fi BGN (2.4 GHz) and BLE feature flags |
| A-27 | ~~`CLAUDE.md` (git-ignored, local only)~~ | "Nothing has been flashed to hardware yet" | **Fixed 2026-09-25**: CLAUDE.md was rewritten to point at these docs; as A-1 |
| A-28 | `docs/REFERENCES.md` §1.2, "Why this matters" | "~90–290 CSI/s brackets what Step 05's rate counter should show" | at the default 20 ms ping the expected rate is about 50/s (D-35, D-44); 90–290/s is esp-csi-rs's figure for raw injection at 10 ms and for a station, not this firmware's traffic |
| A-29 | ~~this log's opening "State of the project" paragraph; docs/TESTING.md §0 (rows *Hardware*, *CSI on hardware*); docs/ARCHITECTURE.md §1 (*Status*); README § "Verification status — please read"~~ | Step 01 is the only code run on the C5; no CSI captured on this hardware | TESTING §5.1 rows 0b–0e (2026-09-25): Steps 03 and 04 ran on the C5 with a laptop hotspot, and Step 04 captured 2,163 CSI frames (106 B L-LTF, ~50/s, 12-bit packing confirmed, D-159, D-160). The summaries were not updated with those rows (TESTING §5, rule 1). Noted 2026-09-25. **Fixed 2026-09-28**: all four summaries now point at the §5 rows, including the Step 10 runs of 2026-09-25 and 2026-09-28 |
| A-30 | Step 10 `main.c` line citations in docs/ARCHITECTURE.md (§3.2–§3.7, §7), docs/TESTING.md (§4, e.g. Phase 2a's `main.c:3575`) and this log | line numbers from before the 2026-09-25 evening `CSI_EXPLAIN` work | stale by 150 to about 1,100 lines (found by the 2026-09-28 verification; e.g. the per-packet timer `t_work0` cited at `2414` in ARCHITECTURE §3.5, now assigned at `main.c:3452`; `free heap after start-up` cited at `3575` in TESTING §4 Phase 2a, now `main.c:4701`; both on 2026-09-28, after the D-168 refinements). The Step 10 citations marked "on 2026-09-28" in ARCHITECTURE were re-mapped after the D-168 refinements (ARCHITECTURE's opening paragraph), e.g. `channel_follow` `1306`, the PH_LOCK reuse check `3470-3478`, `m_sigma`/`d_sigma` `3806-3807`. Symbols are given next to most citations; grep them. Step 08 citations, in ARCHITECTURE and in this log, were re-mapped to the 2026-09-28 working tree after the D-168 refinements |
| A-31 | `step10_recognise_count_vitals/main/vitals.c:390-393` (comment); D-85 *Why* | repeating a value over an empty bin "cannot create a periodicity that was not there" | **unresolved** (noted 2026-11-01): in `test_vitals` (12b) (breathing at 13 and 15/min, which stayed within tolerance) the held value over a 0.3-0.9 s silence is a step whose spectrum gave phantom heart rates (23 of 3,997 analyses with no heart, 49.2-56.2 bpm) and wrong ones with a weak heart (TESTING §3.3 item 10). It stays until the user decides F2 (D-174); synthetic evidence only |
| A-32 | D-140 *Decision* | "the seven analytics modules"; `run_tests.ps1` "runs 8 suites" | eight modules (`activity.c`) and 9 suites since 2026-10-16 (D-172; TESTING §2.1); nine modules (`csi_gain.c`) and 10 suites since 2026-11-03 (D-176). The entry's Evidence says so; the Decision text was not rewritten |
| A-33 | `step10_recognise_count_vitals/test/test_e2e.c:20-21` (header comment) | the mirrored constants are "copied from main.c / Kconfig defaults as of 2026-09-24" | `ACT_LO` (2026-10-16) and the shadow static-branch constants (2026-10-30) were added later and carry their own dates (test_e2e.c:161-166 on 2026-11-01); the 2026-09-24 date covers only the older ones |
| A-34 | docs/REFERENCES.md §4.3 (*Datasets*) | three datasets, none ESP32-C5, none the one replayed | the 2026-11-01 replay used *More Links, Same Airtime* (Zenodo 10.5281/zenodo.23574119, CC BY 4.0; TESTING §5.3), and the 2026-10-30 sweep lists more, including an ESP32-C5 set (IEEE DataPort 10.21227/m754-py34; `~/ESP32C5_CSI_logs/2026-10-30_research/REPORT.md` §2). Not added to REFERENCES.md yet |
