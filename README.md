# ESP32-C5 Wi-Fi CSI Human Detector — build it step by step

A complete, open-source human presence/motion detector built on **Wi-Fi Channel
State Information (CSI)** from a single **ESP32-C5**. No camera, no PIR, no
extra sensor — just the radio that is already in the chip.

Ten small projects. Each one builds, flashes, and does one new thing. By
Step 08 you have a finished detector that calibrates itself and reports
`HUMAN DETECTED` over serial, JSON, and an optional LED. Step 09 benchmarks
it against Espressif's own sensing stack. Step 10 adds **experimental**
person recognition, 1-vs-2+ counting, and breathing / heart rate on top. Read
the table below before you believe any of those.

### Documentation map (start here, humans and AI assistants)

| Read | For |
|---|---|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | **How it works**: repo map, tasks, the per-packet pipeline, NVS, Kconfig, and how to change X safely |
| [docs/DECISIONS.md](docs/DECISIONS.md) | **Why**: each design choice, what was rejected, its evidence and date (D-numbers). Appendix A lists docs known to lag the code |
| [docs/TESTING.md](docs/TESTING.md) | **How to test**: exact build/test/flash commands, what each test level proves, the dashboard's replay and smoke test (§2.6), the hardware test plan, the dated results log (§5, the record of what is verified) and Windows gotchas (§6) |
| [`tools/dashboard.py`](tools/dashboard.py) → [*See how it decides*](#see-how-it-decides-the-live-dashboard-and-the-why-lines) | **Watch Step 10 decide**: a live page in your browser that shows how it recognises, counts and measures, step by step, from the board's `EXPLAIN` lines (`python tools\dashboard.py --port auto --open`) |
| [docs/REFERENCES.md](docs/REFERENCES.md) · [docs/COMPARISON.md](docs/COMPARISON.md) | literature and published ceilings · comparison with prior art |
| [docs/MARKET_COMPARISON.md](docs/MARKET_COMPARISON.md) | **Which sensor for which job**: Wi-Fi CSI vs radar, thermal, ToF, cameras, bed sensors and wearables for presence, counting and heart rate; by application, with prices (USD/INR), installation, diagrams and animations (research of 2026-11-16; full report in [docs/research/](docs/research/2026-11-16_full_report.md)) |

Nothing is validated on hardware until TESTING §5 says so. AI-assistant
files (`CLAUDE.md`, `.claude/`) are git-ignored, so everything a newcomer
needs is in these tracked files.

---

## Read this before you start — what this actually does

**Yes, this is possible on the ESP32-C5, and this repo implements it (not yet
measured on hardware).** But be clear
about what "recognise the human" means with one antenna:

| | |
|---|---|
| ✅ **Detects a human who moves** | walking, gesturing, shifting weight, standing up, reaching. This is what single-antenna CSI is known to deliver in published work ([REFERENCES](docs/REFERENCES.md)). This code has not yet been measured on a board ([TESTING §0](docs/TESTING.md#0-where-things-stand-2026-11-03)). |
| ⚠️ **Motionless person: unreliable** | there is a secondary "static presence" branch that helps, but do not build a safety system on it. |
| 🧪 **Which human: EXPERIMENTAL (Step 10)** | Step 10 learns up to four people from how they *walk* (you enrol each one, walking alone by several different routes) and names whoever walks past, one at a time. The best published ESP32 result is **95 % for 2 people, 74 % for 3**, in a controlled room ([§3.4](docs/REFERENCES.md)). That is the ceiling, not what this code has shown. It cannot recognise someone sitting still (their vitals carry the name they were recognised by on the way in, see below), or a stranger (only `"unknown"`). With two or more people moving it answers `"multiple"`, but only after you have trained the counter. Before that, two people walking together get named as one. Recognising anyone, anywhere, several at once is not achievable on this hardware; anyone claiming it is overselling. **Unvalidated on hardware.** |
| 🧪 **How many: EXPERIMENTAL (Step 10)** | One link can at best tell **one moving person from two or more**, after you train it in your room. Until then it reports 1 whenever anyone is present. Real counting (0–5 people, 89.8 % within ±1) took **four crossing links** in the literature. It says nothing about people sitting still: two people who sit down without having been seen moving together are invisible to it. **Unvalidated on hardware.** |
| 🧪 **Breathing / heart rate: EXPERIMENTAL (Step 10)** | Breathing rate for one person sitting or lying still is plausible. Confident breathing can also keep a still person PRESENT, provided they were detected before they went still. The rates are tagged (`vitals_of`) with the last person recognised walking in **alone** this visit. That is a carried-over claim, not a measurement of whose chest it is. When the room is known to hold two or more people (`count` 2, or two different names this visit) the rates are withheld and `vitals` reads `"multiple"`, because one link cannot separate two people's breathing. Knowing that needs a trained counter or two recognised names. Heart rate is reported as `hr_bpm_exp` and **means nothing until you check it** against a chest strap or pulse oximeter with `tools/vitals_validate.py`. In the synthetic tests it only came through when breathing was strong. Not a medical device. **Unvalidated on hardware.** |
| ⚠️ **Not human-specific** | a large dog, a ceiling fan, or curtains in a draught will also trigger it. It senses changes in radio propagation, not people. |
| ⚠️ **Needs recalibration** | after you move the board, move furniture, or change the Wi-Fi channel. |

### Verification status — please read

* **Compile-verified, Steps 01–09.** On 2026-09-07, Steps 01–09 *and* the
  root project were clean-rebuilt (`build/` and `sdkconfig` deleted first)
  with `idf.py set-target esp32c5 build` on **ESP-IDF v6.0.2**. Every one
  produced a binary with no errors. On 2026-09-22, review fixes changed the
  `main.c` of Steps 02–09. Since then Step 03 was rebuilt (2026-09-23 and
  2026-09-25), Step 04 on 2026-09-25 (0xd97a0), and Step 08 (0xdb340, 57 % of
  its 2 MB partition free) and the root project (0xdb350) on 2026-09-28 (see
  below). Steps 02, 05–07 and 09 have **not** been rebuilt since those edits,
  so build them before you rely on them.
* **Compile-verified, Step 10** (2026-09-24). Clean build (`build/` and
  `sdkconfig` deleted, `set-target esp32c5`, `build`): **0 warnings,
  0 errors**. After a later round of review fixes the same day, an
  incremental rebuild was again **0 warnings, 0 errors**: the image is
  0xe7fc0 = 950,208 bytes, leaving **55 %** of its 2 MB app partition free,
  and static allocations use 190,219 of 320,928 bytes of HP SRAM (59 %).
  After those fixes it also built clean from scratch with all four Step 10
  switches off (0xdc180) and with only recognition off (0xe6400), and
  `main.c` compiled without a warning in 26 switch combinations.
* **Step 10 + per-person vitals** (`vitals_of`, the `vit_log` log,
  `"multiple"`), 2026-09-25. Incremental build with `main.c` and
  `person_vitals.c` forced to recompile: **0 warnings, 0 errors**. The image
  is 0xea3d0 = 959,440 bytes, with **54 %** of the 2 MB app partition free;
  a full rebuild at 15:30 (1,089 steps) gave the same image (docs/TESTING.md
  §5.2). Static HP SRAM is 190,539 of 320,928 bytes (59 %), and the new state
  `s_pv` is 304 bytes. `main.c` compiled with **0 warnings in 96 switch
  combinations**: ID × COUNT × VITALS (with or without the breathing hold,
  or off) × JSON × bar line × console. For this check each combination got
  a patched `sdkconfig.h` and was compiled with the firmware's own compiler
  command.
* **Step 10 host tests** (`step10_recognise_count_vitals\test\run_tests.ps1`,
  MSVC `/std:c11 /W4 /WX`), 2026-09-25. All 8 suites pass: `dsp` 5,980
  checks, `ncc` 731, `vitals` 2,386, `gait` 108, `count` 592, `gate` 97,
  `pvitals` 1,304, `e2e` 237, with 0 failures. The tests use **synthetic
  signals** with known answers: simulated breathing chests with a sine
  heartbeat, simulated walkers, and one mover against two. They prove the
  arithmetic. They do **not** show that it recognises, counts or measures a
  real person. Recognition, counting and heart rate are experimental.
  One known weakness showed up only when the tests were re-run with 9
  different random seeds for the synthetic rooms. The end-to-end "recognised
  walker sits down" scenario (e1) passes in 7 of the 9 rooms at 50 Hz. In
  the other 2, the motion gate calls only 26–31 % of the walker's packets
  MOVING, so too few gait windows complete and the walker is never
  recognised. At 100 Hz it is also 7 of 9, but different rooms fail, for
  other reasons: a false trigger in the empty room, and two seated chests
  that kept Step 08 on the whole time. So a higher packet rate is not a
  proven fix.
* **Hardware (2026-09-25 to 2026-09-28): bring-up, one 15-minute Step 10
  soak and one flash of the D-168 image, no accuracy.** The record is
  [docs/TESTING.md §5.1](docs/TESTING.md#51-hardware); this is a summary. On
  an ESP32-C5 (COM13) with a Windows laptop hotspot: Step 01 boots. Step 04
  captured CSI: 106-byte L-LTF frames at about 50/s, **12-bit packed** (26
  subcarriers, not 53). The hotspot sends only legacy frames, so Steps 03
  and 05–07 get none from it. Step 10 booted several times on 2026-09-25
  and **lost the USB port four times in about 30 s, for a reason nobody
  recorded**. Three of the losses were followed by a fresh boot
  (one after 33 s with no output, one during the Wi-Fi join, one in the
  calibration countdown); the fourth, also in the countdown, ends the
  capture. On 2026-09-28 the image built just before D-167 (0xef240, with
  D-164 and D-165, built with `CSI_LLTF_ONLY` = y, not the default) ran for
  15 minutes at 50 Hz **without a restart**. It
  calibrated, dropped 0 packets and kept about 94 KB of heap free. Its
  slowest packet since boot took 52 ms, set before the capture began, while
  no host read the console; in the capture's detect rows it was at most
  20.8 ms (TESTING §5.1). At 19:00 on 2026-09-28 Step 10 0xef360 (D-168 and
  its four refinements, TESTING §5.2) was flashed, and it boots: the boot
  after its own restart (the hotspot was off, so the Wi-Fi join failed and
  the firmware restarted itself, D-165) printed `last reset: SW (3)` and
  `baseline found: 106 bytes CSI, 26/26 subcarriers, ch 6, 50.2 Hz`. That
  boot is identified by its ELF hash; its `Compile time` line is stale. With
  the hotspot off it never joined Wi-Fi, so **its channel check has not run
  on the board yet** (see [docs/TESTING.md §5.1](docs/TESTING.md#51-hardware)).
  **Step 08 has not run on the board. No accuracy of any kind has been
  measured**, and no run so far recorded who was in the room, so the
  presence and breathing values the soak printed mean nothing yet.
* **Builds and host checks on 2026-09-28**, after Steps 08 and 10 were made
  to recalibrate when the stored baseline comes from another Wi-Fi channel
  (D-168) and after its four refinements the same day: Step 10 0xef360,
  Step 08 0xdb340 (built with `CSI_LLTF_ONLY` = y for the laptop hotspot),
  root 0xdb350, **0 warnings** each. Step 10's host tests: 8 suites, 11,453
  checks, 0 failures. `main.c` is not host-compiled and `test_e2e.c` does
  not model the channel check, so no host test exercises it. `main.c`
  compiled with 0 warnings in **192** switch combinations (the 96 above,
  with `CSI_EXPLAIN` on and off). Step 10 0xef360 was flashed on 2026-09-28
  at 19:00 (Hardware above); Step 08 0xdb340 and root 0xdb350 have not been
  flashed. The numbers are in
  [docs/TESTING.md §5.2](docs/TESTING.md#52-software-checks).
* **Scripted hardware sessions: tools only, no results.**
  `tools/session_runner.py` plays a TESTING §4 protocol (Phase 1 presence or
  Phase 2 step 2b) with timed cues while it logs the board, and
  `tools/session_score.py` scores such a log against the §4 marks (DECISIONS
  D-169). Both were tested offline only (dry runs, a replayed 2026-09-28
  soak log, a fake serial port) and have never run against the board. The
  checks are in [docs/TESTING.md §5.2](docs/TESTING.md#52-software-checks).
  **Nothing about accuracy is measured yet.**
* Treat the numeric defaults (thresholds, timings) as **starting points to
  tune**, not validated values. Step 04 exists precisely so you measure your
  own numbers instead of trusting anyone's constants.
* Steps 01–09 carry an `ESP_IDF_VERSION_MAJOR` shim so they also build on
  **v5.5.1**, but v6.0.2 is what the project is pinned to and verified
  against. Step 10 was built and tested on v6.0.2 (Windows), built on v6.1
  (Ubuntu) since 2026-10-12 and host-tested there since 2026-10-16 (next
  bullet); only v6.0.2 images have
  run on the board. See *Everything is on ESP-IDF v6.0.2* below for why.
* **Ubuntu side (ESP-IDF v6.1, DECISIONS D-170, D-171).** 10 projects build
  with 0 warnings (Step 09 cannot build on v6.1); the Step 10 host tests
  pass under gcc with warnings as errors (the same 11,453 checks) and the
  192-combination switch matrix is clean, 2026-10-16, before the activity
  state the same day (after it: 9 suites / 11,515 checks and 368
  combinations, next bullet)
  ([docs/TESTING.md §5.2](docs/TESTING.md#52-software-checks)). **No v6.1
  image has run on the board.** ESP-IDF v6.1 changes nothing in the CSI
  interface this project uses (DECISIONS D-173). On v6.1, use
  `idf.py flash -a` after an `erase-flash` (TESTING G22).
* **Step 10 activity state and presence episodes (experimental, built
  2026-10-16, never run on the board).** Step 10 now also reports a coarse
  `activity` (`empty` / `still` / `restless` / `moving`) and presence-episode
  seconds (*What each feature does*, below; DECISIONS D-172). Built on
  Ubuntu with ESP-IDF v6.1 only: 0xf56d0, **0 warnings**, not flashed, and no
  Windows v6.0.2 build yet. Step 10's host tests under gcc: **9 suites,
  11,515 checks, 0 failures** (the new `activity` suite 57, `e2e` 242);
  the MSVC runner lists the new suite but has not been run. `main.c`
  compiles with 0 warnings in **368** switch combinations (552 since
  2026-11-03, below). On synthetic
  data the state agrees with the scripted truth on about 90 % of packets
  while walking in or sitting still, and on only 35 % while fidgeting.
  Those are synthetic numbers; on a real person nothing is measured. Step 08
  and the root project are unchanged (0xe0600 each on v6.1). The numbers are
  in [docs/TESTING.md §5.2](docs/TESTING.md#52-software-checks).
* **Heart-rate and AGC failure modes in the host tests, and a first offline
  replay of real recordings (2026-10-30 to 2026-11-01; no firmware change,
  nothing run on the board).** After a research sweep (DECISIONS D-174), the
  vitals tests also feed `vitals.c` noise with no chest, harmonic-rich
  breathing with no heartbeat, heart-band noise, irregular packet rates and
  short link silences, and the end-to-end test gained an **assumed** model
  of the receiver's automatic gain control. Step 10's host tests under gcc:
  **9 suites, 11,673 checks, 0 failures**. They also print **15 KNOWN LIMIT
  lines**: things the tests show going wrong today, printed with their
  numbers instead of failing. Among them: a fluctuation shared by every
  subcarrier can give a heart rate (and breathing) in a synthetic empty
  room; 0.3–0.9 s gaps in the packet stream can give a ~50 bpm heart rate
  where there is no heartbeat; and a gain change through receiver
  compression can fire the test's shadow copy of the static branch in a
  synthetic empty room (that shadow feeds nothing, so what a device would
  then do is not measured). The proposed fixes were not applied then; two
  of them are since 2026-11-03 (next bullet), AGC handling is not.
  Separately, a host port of Step 08 (kept outside the repo) was run on six
  public, labelled recordings from another research group (ESP32-S3 boards
  sending to each other, one adult, two rooms). On the median link it
  reported PRESENT for all of the seated and walking time, and for 3–10 %
  and 8–16 % of the two empty phases; the seated person was caught by the
  static (D) branch, not the motion branch, and 11–17 % of link runs stayed
  PRESENT after the person left. **This is not a C5 accuracy figure**: the
  chip, radio setup, subcarriers and packet timing all differ. The MSVC test
  runner was not run. The numbers are in
  [docs/TESTING.md §5.2](docs/TESTING.md#52-software-checks) and §5.3.
* **Two of those fixes, built 2026-11-03 (synthetic tests and a build only;
  nothing run on the board).** (1) When the packet stream has a short gap
  (under about 1 s), the vitals code now fills the empty 100 ms slots with a
  straight line toward the packet that ends the gap, instead of repeating
  the last value (DECISIONS D-175). In the synthetic tests that makes the
  ~50 bpm heart rate after link silences **much rarer, not impossible**: 1
  wrong value in 332,374 analyses with silences over 52 random-seed bases,
  against 889 with the old rule. (2) Step 10 now also logs the receiver's
  automatic-gain setting for the packets behind each detect JSON line (new
  keys `agc`, `fft`, `gain_chg`; DECISIONS D-176). It is **logging only**:
  nothing the detector decides reads it. The two header bytes it reads are
  not a documented ESP-IDF field (Espressif's own gain library reads them),
  and they have never been checked on a board, so check the first capture
  before trusting them (docs/TESTING.md §4). Step 10's host tests under
  gcc: **10 suites, 11,705 checks, 0 failures**, and **12 KNOWN LIMIT
  lines** (the three about silences are gone). `main.c` compiles with 0
  warnings in **552** switch combinations. Step 10 builds on ESP-IDF v6.1
  as 0xf5a90 with **0 warnings**, **not flashed**: the board still runs
  0xef360 from 2026-09-28, which has neither change. Step 08 and the root
  project did not change. The MSVC test runner was not run. The numbers
  are in [docs/TESTING.md §5.2](docs/TESTING.md#52-software-checks).

---

## Why most ESP32 CSI tutorials will not compile on the C5

This matters, because you will find a lot of them and they will waste your day.

The ESP32-C5 is a **Wi-Fi 6 (802.11ax), MAC version 3** part. Its CSI API is
genuinely different from the classic ESP32/S3/C3:

| Classic ESP32 tutorials use | On ESP32-C5 |
|---|---|
| `wifi_csi_config_t` with `.lltf_en`, `.htltf_en`, `.stbc_htltf2_en`, `.ltf_merge_en`, `.channel_filter_en`, `.manu_scale`, `.shift` | **None of these fields exist.** `wifi_csi_config_t` is an alias for `wifi_csi_acquire_config_t`, a bitfield struct with `.acquire_csi_legacy`, `.acquire_csi_ht20`, `.acquire_csi_su`, `.val_scale_cfg`, … |
| `rx_ctrl.sig_mode`, `.mcs`, `.cwb`, `.stbc`, `.ant`, `.secondary_channel` | **None of these exist.** `rx_ctrl` is `esp_wifi_rxctrl_t`; the PHY type is `rx_ctrl.cur_bb_format`. |
| "CSI is always 128 bytes / 64 subcarriers" | **Not true here.** Length depends on the PHY the router used. Step 04 measures it. |

These facts were read directly out of the ESP-IDF sources (v5.5.1; the
same structs are present in v6.0.2):
`components/esp_wifi/include/local/esp_wifi_types_native.h` and
`components/esp_wifi/include/esp_wifi_he_types.h`, gated on
`CONFIG_SOC_WIFI_MAC_VERSION_NUM == 3`.

CSI support itself is confirmed: `SOC_WIFI_CSI_SUPPORT 1` in
`components/soc/esp32c5/include/soc/soc_caps.h`.

---

## What you need

**Hardware**
* 1 × ESP32-C5 board (ESP32-C5-DevKitC-1 or similar)
* 1 × USB-C cable (a **data** cable — charge-only cables are a classic time sink)
* A **2.4 GHz** Wi-Fi network you know the password for
  *(the C5 is dual band, but this project locks it to 2.4 GHz on purpose — see below)*

**Software** — already installed on this machine:
* ESP-IDF **v6.0.2** at `C:\esp\v6.0.2\esp-idf` — the same install the
  Antigravity ESP-IDF extension selects, so the IDE and the command line agree
* Python 3.13

For the PC-side plotting tools:
```powershell
pip install pyserial matplotlib numpy
```

---

## The 30-second version

```powershell
cd D:\ESP32C5_CSI

# 1. tell it your WiFi
.\build.ps1 step08_final_human_detector
cd step08_final_human_detector
idf.py menuconfig      # -> ESP32-C5 CSI Human Detector -> WiFi -> SSID + password
cd ..

# 2. find your board, then flash it
.\build.ps1 step08_final_human_detector -Flash -Port COM12

# 3. leave the room for ~45 s while it calibrates itself, then walk back in
```

Prefer buttons to commands? Open the folder in Antigravity and press
**Ctrl+Shift+B** — see *Working in Antigravity* below.
If a task misbehaves, run the **ESP-IDF: Doctor** task first.

That works. But **do the steps in order the first time** — when something
misbehaves you will want to know which stage broke, and Steps 03/04 are the
ones that tell you.

---

## Working in Antigravity (no PowerShell needed)

Antigravity is installed on this machine and already has the **Espressif
ESP-IDF extension v2.1.0**. This project ships a `.vscode/` folder so you can
build and flash from the IDE with a couple of clicks.

### Which folder to open

Any of these work:

| Open this | What the extension builds |
|---|---|
| `D:\ESP32C5_CSI` (the plain repo folder) | the **finished detector** — the root is itself a project |
| `D:\ESP32C5_CSI\ESP32C5_CSI.code-workspace` | every step (01–10) as a folder; pick one and build it |
| `D:\ESP32C5_CSI\step08_final_human_detector` | just that step |

Every one of them is a valid ESP-IDF project directory, so the extension's own
Build / Flash buttons have something real to work on. That was not true before:
the repo root had no `CMakeLists.txt`, which is what produced

```
CMakeLists.txt not found in project directory D:\ESP32C5_CSI
Failed to set target esp32c5: non zero exit code 2
```

The root `CMakeLists.txt` pulls in `step08_final_human_detector/main`
directly — there is no second copy of the source, so the root build and Step 08
can never drift apart.

> The extension takes the **first** workspace folder as *the* project
> (`this.workspace = workspaceFolders[0].uri` in its own code). The
> `.code-workspace` therefore lists only the step folders, never the
> repo root, so folder[0] is always a buildable project.

**And the Tasks work from anywhere.** `Ctrl+Shift+B` takes the step from a
dropdown and calls `build.ps1` directly, so it does not care which folder is
open.

### Just do this

1. Open `D:\ESP32C5_CSI\ESP32C5_CSI.code-workspace` in Antigravity (see above).
2. Press **Ctrl+Shift+B**, or **Ctrl+Shift+P → "Tasks: Run Task"**.
3. Pick a task, pick the step from the dropdown, and if it asks, type your
   COM port.

| Task | What it does |
|---|---|
| **ESP-IDF: Doctor** | **Run this first if anything fails.** Checks toolchain, RISC-V compiler, connected boards, built steps |
| **ESP-IDF: Build** | Configure for `esp32c5` + build (this is Ctrl+Shift+B). No board needed |
| **ESP-IDF: Build + Flash + Monitor** | The everyday one |
| **ESP-IDF: Monitor only** | Reopen serial without reflashing |
| **ESP-IDF: menuconfig** | GUI config — **this is where you set WiFi SSID/password**. No board needed |
| **ESP-IDF: Clean rebuild** | After editing `sdkconfig.defaults` |
| **ESP-IDF: Erase flash** | Full wipe — deletes the saved calibration |
| **Tools: CSI waterfall plot** | Live plot for Step 05 |
| **Tools: Presence monitor + plot** | Live tuning plot for Step 08 / Step 10 |
| **Tools: Install Python plotting deps** | One-time `pip install` |

**Leave the port prompt on `auto`** — the board is detected for you. Only type
a COM number if you have several boards plugged in at once.

### If flashing fails

Run **ESP-IDF: Doctor** first. The overwhelmingly common cause is that the
board is not actually presenting a serial port.

The tasks now check the port *before* they start building, so a missing board
gives you this immediately rather than five minutes of compiling followed by a
wall of CMake errors:

```
==============================================================
 NO ESP32-C5 FOUND
==============================================================
 Serial ports currently on this machine:
   COM4  Standard Serial over Bluetooth link   <- Bluetooth, NOT your board
   COM3  Standard Serial over Bluetooth link   <- Bluetooth, NOT your board
```

Checklist, in the order worth trying:

1. **Is the board plugged in?** A new COM port should appear within a second
   or two of connecting it. On this machine, COM3 and COM4 are Bluetooth and
   are always present — they are never your board.
2. **Is it a data USB cable?** Charge-only cables light up the board but
   create no COM port at all. This is the single most common cause.
3. **Try the board's other USB socket.** Most C5 devkits have two: one wired
   to the built-in USB-Serial-JTAG peripheral, one to a UART bridge chip.
4. **Driver missing?** If Device Manager shows an unknown device, install the
   CP210x (Silicon Labs) or CH34x (WCH) driver.
5. **Force download mode:** hold **BOOT**, tap **RESET**, release **BOOT**,
   then flash again.

Two other failure modes worth knowing:

* *"the port is busy"* — something else already owns it. Close the serial
  monitor, the Python plotting tools, and Arduino IDE's Serial Monitor. Only
  one program can hold a COM port at a time.
* *Flashing to a Bluetooth port* fails with *"The semaphore timeout period has
  expired"* rather than a clean error. If you see that, you picked the wrong
  port.

You can always build without a board — `ESP-IDF: Build` and `menuconfig`
need no hardware at all.

### Everything is on ESP-IDF v6.0.2, deliberately

> **2026-10-12:** this is true on Windows. When the same checkout is opened on Ubuntu (dual boot), Ubuntu uses ESP-IDF v6.1 with separate build folders and `sdkconfig` files. See `docs/ide/README.md` and DECISIONS D-170.

The IDE and the command line now use **the same ESP-IDF install**:

```
C:\esp\v6.0.2\esp-idf
```

That is the install the Antigravity extension already selects on its own,
through the EIM registry at `C:\Espressif\tools\eim_idf.json`. Rather than
fight it, the project moved to match it.

| | Uses |
|---|---|
| Antigravity ESP-IDF extension | `C:\esp\v6.0.2\esp-idf`, tools `C:\Espressif\tools`, python `...\tools\python\v6.0.2\venv` |
| `build.ps1` and all Tasks | the same `C:\esp\v6.0.2\esp-idf` via its `export.ps1` |
| `.vscode/settings.json` (root + Steps 01–08 and 10) | mirrors the extension's own values exactly |

**Why this was necessary.** The extension does not take its ESP-IDF from
workspace settings — it takes it from EIM. Its log proves it:

```
ESP_IDF_VERSION=6.0.2
PATH=...C:\Espressif\tools\python\v6.0.2\venv\Scripts
SYSTEM_PATH=...;C:\esp\v6.0.2\esp-idf;C:\Espressif\tools
```

Earlier the project was pinned to v5.5.1 while the extension ran v6.0.2. Same
source tree, two different SDK versions — different `sdkconfig` formats and a
renamed Wi-Fi enum. That is what produced the endless
`Python interpreter ... is not from installed venv` warning. It is gone now
because both sides genuinely agree.

**The one v6.0 API break**, handled by a version shim so the code still builds
on v5.5.x too:

```c
#include "esp_idf_version.h"
#if ESP_IDF_VERSION_MAJOR >= 6
#define CSI_WIFI_BW20 WIFI_BW20      /* v6.0 renamed it */
#else
#define CSI_WIFI_BW20 WIFI_BW_HT20   /* v5.x name */
#endif
```

> **Do not switch to v6.0.1.** It is EIM-registered but its `riscv32-esp-elf`
> toolchain is not installed, so `set-target esp32c5` fails and the build
> silently falls back to the **xtensa ESP32** target — where the classic CSI
> struct exists and this code correctly refuses to compile. Verified directly.

> The shared registry `C:\Espressif\tools\eim_idf.json` and your global
> Antigravity settings are **not modified** by this project — they serve your
> other ESP-IDF projects.

### Build the finished detector straight from the repo root

`D:\ESP32C5_CSI` is itself a valid ESP-IDF project. Its `CMakeLists.txt` pulls
in `step08_final_human_detector/main` directly — there is no second copy of the
source, so the two cannot drift apart.

That means opening the plain folder `D:\ESP32C5_CSI` and pressing the
extension's Build button works, instead of failing with
`CMakeLists.txt not found in project directory D:\ESP32C5_CSI`. It produces
the same firmware as Step 08.

The step folders remain independent projects — use those to walk through
the tutorial. Root build output lands in `D:\ESP32C5_CSI\build`, separate from
each step's own `build/`, so nothing collides.

### If the extension still misbehaves

Use the Tasks (Ctrl+Shift+B). They call v6.0.2's `export.ps1` directly and do
not depend on the extension's configuration at all. That is why they exist.

### IntelliSense

This IDE has **clangd**, not Microsoft cpptools, so each step carries a
`.clangd` pointing at its own `build/compile_commands.json`. **Build a step
once** and code navigation works — including jumping straight into the
C5-specific CSI structs.

### What I verified, and what I could not

* **Verified:** Steps 01–09 and the root project clean-rebuild on ESP-IDF
  v6.0.2 (as of 2026-09-07; *Verification status* at the top says what has
  been rebuilt since); the exact command line each Task runs does build;
  every `.vscode` JSON file parses; every ESP-IDF path in every
  `settings.json` exists on disk; `compile_commands.json` exists for
  Steps 01–10, so clangd has a database.
* **Not verified:** I cannot launch Antigravity, so the extension's own
  status-bar buttons and the Task dropdowns are untested in the running IDE.
  On a C5, Steps 01, 03, 04 and 10 have run (2026-09-25 to 2026-09-28,
  [docs/TESTING.md §5.1](docs/TESTING.md#51-hardware)); Step 08 has not.

> If a build fails with `ninja: error: remove(...): The process cannot access
> the file because it is being used by another process`, that is a file lock
> (antivirus, or two builds at once) — not a code error. Just build again.

---

## Setting up your shell

Every ESP-IDF command needs the environment activated. Two options:

**Option A — use the helper (recommended)**

`build.ps1` in this folder activates ESP-IDF for you:

```powershell
.\build.ps1 step01_hello                        # configure + build
.\build.ps1 step01_hello -Flash -Port COM12     # build + flash + monitor
.\build.ps1 step03_csi_hello -Clean             # wipe build dir first
```

**Option B — activate manually**

```powershell
C:\esp\v6.0.2\esp-idf\export.ps1
cd D:\ESP32C5_CSI\step01_hello
idf.py set-target esp32c5
idf.py build
idf.py -p COM12 flash monitor
```

> **Use PowerShell, not Git Bash.** ESP-IDF on Windows refuses to run under
> MSys/MinGW — `export.sh` will fail with
> *"MSys/Mingw is not supported"*. This is expected; use `export.ps1`.

### Find your board's COM port

```powershell
# list candidate ports
Get-PnpDevice -Class Ports | Where-Object Status -eq OK | Select-Object FriendlyName

# then CONFIRM it is really your C5 before flashing
C:\esp\v6.0.2\esp-idf\export.ps1
esptool --port COM12 chip_id
```

`esptool chip_id` should report **ESP32-C5**. If it reports a different chip
or times out, you have the wrong port. Do not skip this check — flashing the
wrong port is how you overwrite a different project.

*(Unplug everything else USB-serial first if you have several boards attached —
this machine already has a lot of CP210x and CH340 entries registered.)*

Press **Ctrl+]** to quit `idf.py monitor`.

---

# The steps

## Step 01 — Does anything work at all?

**Folder:** `step01_hello`  **Prove:** toolchain + cable + board.

```powershell
.\build.ps1 step01_hello -Flash -Port COM12
```

**Expected output**

```
I (312) step01: ESP32-C5 CSI project - Step 01 (sanity)
I (312) step01: IDF version   : v6.0.2
I (312) step01: CPU cores     : 1
I (312) step01: Flash size    : 4 MB
I (312) step01: STA MAC       : xx:xx:xx:xx:xx:xx
I (1312) step01: heartbeat 0  (free heap 340000)
```

**If nothing prints:**
* Wrong COM port → run `esptool chip_id`.
* Charge-only USB cable → swap it.
* Some C5 boards route the console over a UART bridge instead of the built-in
  USB-Serial-JTAG. Open `sdkconfig.defaults`, comment out the two
  `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG` lines, then
  `.\build.ps1 step01_hello -Clean -Flash -Port COM12`.
* Board not in download mode → hold **BOOT**, tap **RESET**, release **BOOT**,
  then flash.

---

## Step 02 — Join Wi-Fi and lock the radio

**Folder:** `step02_wifi_sta`  **Prove:** association + a locked radio.

```powershell
.\build.ps1 step02_wifi_sta      # first: sets the target to esp32c5
cd step02_wifi_sta
idf.py menuconfig
#   -> ESP32-C5 CSI Project Configuration
#      -> WiFi SSID       = your 2.4 GHz network
#      -> WiFi Password   = your password
cd ..
.\build.ps1 step02_wifi_sta -Flash -Port COM12
```

**Expected output**

```
I (2100) step02: band mode 2.4GHz-only : ESP_OK
I (2100) step02: bandwidth HT20        : ESP_OK
I (2100) step02: power save OFF        : ESP_OK
I (4300) step02: got IP     : 192.168.1.42
I (4300) step02: gateway    : 192.168.1.1
I (4310) step02: channel       : 6 (secondary 0)
I (4310) step02: RSSI          : -43 dBm
I (6310) step02: link alive: ch=6  rssi=-43 dBm
```

### Why the radio is locked — the single most important design decision

The number of CSI subcarriers depends on the channel bandwidth and PHY format.
The C5 is a dual-band Wi-Fi 6 chip, so left alone it will happily roam to
5 GHz or negotiate HE40/HE80 — and the CSI array length changes underneath
you. Any variance-based detector then produces garbage.

So Step 02 (and everything after it) does three things right after
`esp_wifi_start()`:

```c
esp_wifi_set_band_mode(WIFI_BAND_MODE_2G_ONLY);   // no 5 GHz roaming
esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20); // 20 MHz only
esp_wifi_set_ps(WIFI_PS_NONE);                     // radio never sleeps
```

> These **must** be called after `esp_wifi_start()` — before it they return
> `ESP_ERR_WIFI_NOT_STARTED`.

Power save off matters too: a sleeping radio misses frames and its RX
timestamps drift.

**Troubleshooting:** if it prints `You are on a 5 GHz channel!`, force your
router to advertise a 2.4 GHz SSID, or give the 2.4 GHz band its own name.

---

## Step 03 — First CSI capture

**Folder:** `step03_csi_hello`  **Prove:** CSI callbacks fire, at a usable rate.

```powershell
.\build.ps1 step03_csi_hello -Flash -Port COM12
```
*(Set your SSID/password again — each step has its own `sdkconfig`.)*

**Expected output**

```
I (4500) step03: CSI enabled
I (4500) step03: pinging 192.168.1.1 every 20 ms to generate CSI traffic
I (5500) step03: CSI packets:     47 total |  47.0 /s | last len 128 B | rssi -43 dBm | ch 6
I (6500) step03: CSI packets:     98 total |  51.0 /s | last len 128 B | rssi -43 dBm | ch 6
```

### The two things that trip everyone up

**1. `CONFIG_ESP_WIFI_CSI_ENABLED` defaults to `n` in ESP-IDF.**
Your code compiles fine and then `esp_wifi_set_csi()` fails at *runtime*. It is
already set in this project's `sdkconfig.defaults`:

```
CONFIG_ESP_WIFI_CSI_ENABLED=y
```

**2. CSI only exists for packets the chip RECEIVES.**
An idle station receives only beacons — about 10/s, far too slow to see a
person move. So the firmware pings the gateway continuously (default every
20 ms → ~50 Hz), and `dump_ack_en = 1` gives extra CSI from ACK frames.

> **Note on the ping timeout:** `ESP_PING_DEFAULT_CONFIG()` sets
> `timeout_ms = 1000`. The ping loop blocks for the whole timeout when a reply
> is lost, so one dropped packet would cost a full second of CSI. Every project
> here overrides it to `2 × interval`. If you copy this code elsewhere, keep
> that override.

**What to check:** the `/s` figure should be close to
`1000 / CSI_PING_INTERVAL_MS` — about **50/s** at the default. If it collapses
into single digits, the link is marginal: move closer to the router.

**If the counter stays at 0 — stop here, nothing later can work:**

| Symptom | Cause |
|---|---|
| `esp_wifi_set_csi` returns `ESP_ERR_NOT_SUPPORTED` | `CONFIG_ESP_WIFI_CSI_ENABLED` is not `y`. Run `.\build.ps1 step03_csi_hello -Clean`. |
| Rate is ~10/s, not ~50/s | Ping is failing. Some routers rate-limit ICMP to themselves — try pinging another device on the LAN instead. |
| Counter stuck at 0 but Wi-Fi is connected | Power save re-enabled, or you are not actually associated. |

---

## Step 04 — Measure your CSI geometry (do not skip this)

**Folder:** `step04_csi_geometry`  **Prove:** what your CSI actually looks like.

```powershell
.\build.ps1 step04_csi_geometry -Flash -Port COM12
```

Leave the room empty. Let it run ~30 s.

**Expected output**

```
I (6000) step04: --------- first CSI packet, decoded ---------
I (6000) step04:   info->len                    = 128 bytes
I (6000) step04:   rx_channel_estimate_len      = 64
I (6000) step04:   cur_bb_format                = 1 (11g/11a)
I (6000) step04:   first_word_invalid           = 0
I (6000) step04:   raw bytes [0..7]             = 12 -34 15 -31 18 -28 20 -25
I (6000) step04:   layout: pairs of (imag, real), int8 each

I (11000) step04: ===== CSI GEOMETRY HISTOGRAM (512 packets, 0 dropped) =====
I (11000) step04:   bytes  subcarriers  fwi  PHY format      count   share  rssi range
I (11000) step04:    128            64    0  11g/11a           489   95.5%  -45..-41 dBm
I (11000) step04:    256           128    0  HT (11n)           23    4.5%  -44..-42 dBm
W (11000) step04: >>> DOMINANT SHAPE: 128 bytes, 64 subcarriers, 11g/11a
W (11000) step04: >>> Write these two numbers down. Step 05 uses them.
```

*(Those numbers are an illustration of the format. **Yours will differ** — that
is the entire point of this step.)*

### 👉 The decision this step gives you

Look at the **share** of the dominant shape:

* **One shape ≥ ~90%** → leave `CSI_LLTF_ONLY` **OFF**. The auto-lock in
  later steps picks that shape and you get the richest CSI available.
* **Fragmented across 3+ shapes** → turn `CSI_LLTF_ONLY` **ON** in
  `menuconfig` for Steps 05–08. Only the legacy long training field is then
  captured — it is present in *every* 802.11 preamble regardless of PHY, so
  every packet yields exactly the same subcarrier count. You lose some
  resolution, you gain a rock-steady stream. For a presence detector that is
  a good trade.

### Reading the raw layout

CSI is stored as pairs of `int8`: **imaginary first, then real**, one pair per
subcarrier. So:

```
amplitude[k] = sqrt( buf[2k]² + buf[2k+1]² )
subcarriers  = (len - (first_word_invalid ? 4 : 0)) / 2
```

If `first_word_invalid` is set, the **first four bytes are garbage** (a
documented hardware limitation) and must be skipped.

---

## Step 05 — Amplitude, and killing the AGC

**Folder:** `step05_amplitude`  **Prove:** a clean, comparable signal.

```powershell
.\build.ps1 step05_amplitude     # first: sets the target to esp32c5
cd step05_amplitude
idf.py menuconfig      # SSID/password, and CSI_LLTF_ONLY if Step 04 said so
cd ..
.\build.ps1 step05_amplitude -Flash -Port COM12
```

**Expected output**

```
W (5000) step05: CSI mode: all LTFs, auto-locking to the dominant shape
I (6000) step05: learning shape... 150/200 packets, 2 distinct lengths
W (7000) step05:  CSI SHAPE LOCKED
W (7000) step05:    length      : 128 bytes
W (7000) step05:    subcarriers : 64
W (7000) step05:    dominance   : 95.5% of 200 packets
I (8000) step05: acc 48 rej 2 drop 0 | nsub 64 | median amp avg 31.4 (min 29.1 max 34.0, spread 1.17x)
CSI,52,8021,-43,64,3140,98,101,104,99,...
```

### The three problems this step solves

**1. The shape keeps changing.** Watch the first 200 packets, lock onto the
most common length, discard the rest. Averaging across shapes is meaningless.

**2. The AGC rescales everything.** The receiver's automatic gain control
changes gain packet to packet, multiplying every subcarrier by a
random-looking constant. To a variance detector that looks exactly like "the
whole channel changed". The fix:

> **Divide each packet by its own MEDIAN amplitude.**
> Motion changes the *shape* of the amplitude curve across subcarriers
> (frequency-selective fading). AGC changes only its overall *scale*. Dividing
> by the median removes the scale and keeps the shape.
>
> Median, not mean — null and guard subcarriers sit near zero and drag a mean
> around as their count shifts with PHY format.

**3. The first four bytes can be garbage.** Skipped when `first_word_invalid`.

### The diagnostic to watch

The once-per-second line prints the **normalisation divisor itself**:

```
median amp avg 31.4 (min 29.1 max 34.0, spread 1.17x)
```

On a static empty scene that should be roughly steady (spread near 1×). A
spread of 3× or more means the hardware is auto-scaling hard. That is not
fatal — the normalisation absorbs it — but it tells you the link is weak, and
detection later will be noisier. Move closer to the router.

### See it with your own eyes

```powershell
# in a SECOND terminal - close idf.py monitor first, only one process can own the port
python tools\csi_monitor.py --port COM12
```

Two panels: the current amplitude across subcarriers, and a waterfall of the
last few seconds. **Wave your hand in front of the board.** You should see
vertical streaks tear through the waterfall.

> If the waterfall is a flat, featureless block even when you wave your arm at
> the antenna, stop and fix that now. No amount of threshold tuning downstream
> will rescue a signal that is not there.

---

## Step 06 — Calibrate against an empty room

**Folder:** `step06_baseline`  **Prove:** the board learns your room.

```powershell
.\build.ps1 step06_baseline -Flash -Port COM12
```

> ### ⚠️ Start it, then LEAVE THE ROOM and close the door.
> Stay out for the whole ~35 seconds. A baseline taken while you were sitting
> there is the single most common reason these systems "don't work".

**Expected output**

```
W (7000) step06: *** LEAVE THE ROOM NOW. Calibrating for ~35 seconds. ***
I (12000) step06: packet rate 49.8 Hz -> alpha_fast 0.0396, alpha_slow 0.000335
I (12000) step06: >>> phase A: measuring per-subcarrier noise
I (27000) step06: >>> selected 52 of 64 subcarriers (mean amp >= 0.20)
I (27000) step06: >>> phase B: measuring the empty-room noise floor of M and D
W (42000) step06:   CALIBRATION COMPLETE  (ESP_OK)
W (42000) step06:   subcarriers       : 64  (using 52)
W (42000) step06:   motion  M (empty) : mean 0.0412  std 0.0071
W (42000) step06:   deviate D (empty) : mean 0.0128  std 0.0034
W (42000) step06:   -> Step 07 will threshold at about
W (42000) step06:        M > 0.0767   (mean + 5 std)
W (42000) step06: Saved to NVS. Steps 07 and 08 will load it on boot.
W (42000) step06: NOW WALK BACK IN and watch M jump.
```

Then it stays live with a bar graph. **Walk back in.** `M` should jump hard —
tens of standard deviations. That is your proof the pipeline works, before any
decision logic exists.

### The two metrics (Steps 07 and 08 use exactly these)

For every packet we build the normalised amplitude vector `na[k]`, then keep
two exponential moving averages of it:

| | time constant | meaning |
|---|---|---|
| `fast[k]` | ~0.5 s | what the channel looks like *now* |
| `slow[k]` | ~60 s | what the room *normally* looks like |

```
MOTION      M = mean over selected k of  |na[k] - fast[k]| / sigma[k]
DEVIATION   D = mean over selected k of  |fast[k] - slow[k]| / sigma[k]
```

* **M** is high when the channel is *changing* — someone walking, an arm
  moving. **This is the primary detector**; it is what CSI is genuinely good at.
* **D** is high when the channel has *settled somewhere new* — someone came in
  and is now standing still. Slower and less trustworthy.
* Dividing by `sigma[k]` — that subcarrier's own empty-room noise — stops a few
  naturally noisy subcarriers from dominating the average.

### Why `D` compares against a moving average, not a frozen snapshot

This is a deliberate design choice worth understanding. A **frozen** empty-room
baseline drifts out of date: temperature changes, the router retunes, a door is
left open, furniture moves. Within hours you would be collecting false
positives all day.

Comparing `fast` against a **slow 60-second EMA** absorbs all of that
automatically. A person entering shows up as a short-term deviation from the
slow reference; hour-scale drift simply moves the reference with it.

The stored NVS baseline is still essential — but for the two things it is
genuinely good at: **which subcarriers to use**, and **what the noise floor
is** so thresholds can be set from measurement instead of a magic number.

### What the two calibration phases measure

* **Phase A** (15 s) — how noisy is each subcarrier on its own? → `sigma[k]`,
  and which subcarriers are worth using at all. Null/guard subcarriers sit near
  zero and carry no information, so they are dropped.
* **Phase B** (15 s) — how much do `M` and `D` wobble with nobody there? →
  `m_mean`/`m_std`, `d_mean`/`d_std`, which *become* the thresholds. **The
  system tunes itself to your room.**

**Troubleshooting**

| Symptom | Fix |
|---|---|
| `only N usable subcarriers - that is too few` | Lower `CSI_SELECT_MIN_AMP`, or move closer to the router. |
| `m_std` is ~0 | Calibration saw too few packets. Check the rate in Step 03 first. |
| M barely moves when you walk in | The room was **not** empty during calibration → inflated `m_std` flattens everything. Recalibrate properly. |

---

## Step 07 — Presence detection

**Folder:** `step07_presence`  **Prove:** an actual yes/no answer.

Requires Step 06 to have run. It loads the baseline from NVS and **refuses to
guess** if it is missing.

```powershell
.\build.ps1 step07_presence -Flash -Port COM12
```

> **Do not `erase-flash` between Steps 06 and 07** — that wipes the baseline
> out of NVS. Plain `flash` preserves it.

**Expected output**

```
I (400) step07: baseline loaded:
I (400) step07:   csi_len 128, nsub 64, using 52 subcarriers, ch 6, 49.8 Hz
I (400) step07:   empty-room M 0.0412 +/- 0.0071 | D 0.0128 +/- 0.0034
I (4600) step07: thresholds: motion 5.00 sd (release 2.50), static 5.00 sd for 4000 ms
empty    |##......................................| motion    1.2 sd  dev    0.3 sd  level   4%  rssi -43
W (9200) step07: >>> HUMAN DETECTED  (motion 18.4 sd, deviation 2.1 sd)
PRESENT  |########################################| motion   18.4 sd  dev    2.1 sd  level 368%  rssi -44
W (18000) step07: >>> room clear  (motion 1.1 sd, deviation 0.8 sd)
```

### The decision logic

```
m_sigma = (M - m_mean) / m_std      how many standard deviations above
d_sigma = (D - d_mean) / d_std      the empty room we currently are
```

**MOTION branch — primary, with hysteresis**
Trigger when `m_sigma ≥ 5.00`; release only when it falls below `2.50`. Two
different thresholds stop the output chattering on and off around one value.

**STATIC branch — secondary, deliberately hard to trigger**
`d_sigma` must stay above `5.00` **continuously for 4 seconds** before it may
assert presence. `D` also rises for slow environmental drift, so a momentary
spike must not be enough. The branches are **not** simply OR'd on equal terms —
motion is primary and static must earn it.

**HOLD**
Once `PRESENT`, stay `PRESENT` for 5 s after the last trigger, so someone who
pauses between steps does not blink the output.

### Tuning

All in `menuconfig` → *Detection tuning*:

| Symptom | Change |
|---|---|
| False alarms in an empty room | Raise `CSI_MOTION_SIGMA_X100` (500 → 800) |
| Misses someone walking | Lower it (500 → 300) |
| Output flickers on/off | Lower `CSI_MOTION_RELEASE_X100`, raise `CSI_PRESENCE_HOLD_MS` |
| Slow to clear after they leave | Lower `CSI_PRESENCE_HOLD_MS` |
| Static presence never triggers | Lower `CSI_STATIC_SIGMA_X100` / `CSI_STATIC_SUSTAIN_MS` — but expect false positives |

Values are in **hundredths**: `500` = 5.00 standard deviations.

> ### ⚠️ One invariant you must not break
> `METRIC_SMOOTH` (0.15) is defined identically in Steps 06, 07 and 08. The
> metrics `M` and `D` are smoothed by it **before** calibration accumulates
> their statistics, and again before the detector compares them.
>
> If you change it in one file and not the others, calibration will describe a
> differently-smoothed signal than the one being thresholded. Smoothing reduces
> variance, so a detector thresholding a smoothed value against an *unsmoothed*
> `m_std` reads systematically **low** — it under-triggers, and it looks exactly
> like "the thing doesn't work". Change it everywhere or nowhere, and
> recalibrate afterwards.

---

## Step 08 — The finished detector ⭐

**Folder:** `step08_final_human_detector`

Everything above in one firmware, and it **calibrates itself**.

```powershell
.\build.ps1 step08_final_human_detector   # first: sets the target to esp32c5
cd step08_final_human_detector
idf.py menuconfig      # ESP32-C5 CSI Human Detector -> WiFi -> SSID + password
cd ..
.\build.ps1 step08_final_human_detector -Flash -Port COM12
```

**On a brand-new board there is nothing else to do.** No baseline exists, so it
runs calibration by itself: a 10-second countdown to leave the room, ~35 s of
measurement, then it starts detecting.

**Expected output**

```
W (300) csi-human: no baseline (ESP_ERR_NVS_NOT_FOUND) - will calibrate automatically
W (7000) csi-human: CSI shape locked: 128 bytes, 64 subcarriers, fwi=0, ch=6 (95.5%)
W (7000) csi-human: no stored baseline - calibrating
I (8000) csi-human: [leave-room] 9 s left, 48 packets
JSON {"t_ms":8021,"state":"leave-room","present":0,"remaining_s":9}
...
W (52000) csi-human:  CALIBRATED (ESP_OK)
W (52000) csi-human:  DETECTING NOW - walk in and watch it fire.
JSON {"t_ms":53000,"state":"detect","present":0,"motion_sd":0.90,"dev_sd":0.20,"level":18,"rssi":-43,"rate_hz":49.8,"nsub":52}
W (61000) csi-human: >>> HUMAN DETECTED  (motion 21.3 sd, deviation 1.8 sd)
```

On every later boot it reloads the baseline and goes straight to detecting.

### Features

* **Auto-calibration** whenever no valid baseline is stored
* **Baseline validation** — at shape lock, a few seconds after boot, it
  compares the stored baseline's CSI shape (length, subcarrier count,
  first-word flag) with the live one. If they differ (for example
  `CSI_LLTF_ONLY` toggled, or a router whose frames have a different shape),
  it recalibrates instead of producing nonsense. The same happens when the
  stored baseline comes from a **different Wi-Fi channel** (`stored baseline
  was calibrated on channel X, now channel Y - recalibrating`), because a
  baseline describes one channel. Step 10 also recalibrates when the router
  changes channel while it runs; Step 08 only checks at boot
  (docs/DECISIONS.md D-168). Either way, **leave the room** when it counts
  down
* **JSON output** every 500 ms for a host script / MQTT / Home Assistant
* **Human-readable bar graph** alongside it
* **Optional LED** — `CSI_LED_GPIO` (default `-1`, disabled)
* **Optional recalibrate button** — `CSI_BUTTON_GPIO`, hold 2 s

> **Why the LED and button default to disabled:** devkit pin assignments differ
> between boards, and I will not hand you a pin number I could not verify.
> Check your board's schematic, then set them. Note that boards whose only LED
> is an addressable RGB (WS2812) type will **not** work with the simple on/off
> drive — that needs the `led_strip` component.

### Recalibrating

Three ways, in order of convenience:

1. **Button** — if you set `CSI_BUTTON_GPIO`, hold it 2 s.
2. **menuconfig** (board-independent, always works) — set
   `CSI_FORCE_RECALIBRATE = y`, flash, let it calibrate, then set it back to
   `n` and flash again so it does not recalibrate on every reboot.
3. **Erase NVS** — `idf.py -p COM12 erase-flash` then flash. Nuclear option.

### Host-side monitoring

```powershell
# live status in the terminal
python tools\presence_monitor.py --port COM12

# live plot of the motion score against the threshold - best for tuning
python tools\presence_monitor.py --port COM12 --plot

# log everything to CSV for later analysis
python tools\presence_monitor.py --port COM12 --csv session.csv
```

The JSON schema:

```json
{"t_ms":123456,"state":"detect","present":1,"motion_sd":7.4,
 "dev_sd":1.2,"level":148,"rssi":-46,"rate_hz":49.6,"nsub":52}
```

Step 10 appends its own keys after these (see *Step 10 → JSON*). The same
tool reads both, and the CSV gains a `host_time` wall-clock column.

---

## Getting good results — physical placement

The detector only sees changes in the radio path **between the board and your
router**. That geometry is most of your accuracy:

* **Put the board and the router on opposite sides of the area you care
  about.** A person walking between them cuts the path — the strongest signal
  you can get.
* Board and router in the same corner → a person barely perturbs anything.
* **1–6 m apart** works well. Very close together is insensitive; very far
  apart gives a weak, noisy link.
* Keep the board **still**. Any board movement is indistinguishable from room
  movement. Tape it down.
* Avoid placing it right next to large metal objects (fridge, filing cabinet).
* **Recalibrate after moving anything** — the board, the router, the furniture.

## Tuning workflow

1. `python tools\presence_monitor.py --port COM12 --plot`
2. Leave the room. The motion trace should sit near 0 sd with occasional 1–2 sd
   spikes. **If it regularly crosses the 5 sd line while empty, your threshold
   is too low** — raise `CSI_MOTION_SIGMA_X100`.
3. Walk in. It should jump well above the line — typically 10–50 sd.
4. If walking barely moves it: recalibrate (was the room actually empty?), or
   improve the geometry above, or lower the threshold.
5. Check the gap between the empty-room noise and the walking-in peak. That gap
   is your real margin. Put the threshold in the middle of it.

For a timed, logged version of this test (empty room, walking passes, sitting
still), see the scripted sessions in docs/TESTING.md §2.7.

---

## Troubleshooting index

| Problem | Step | Fix |
|---|---|---|
| `MSys/Mingw is not supported` | any | Use PowerShell, not Git Bash. |
| Nothing on serial | 01 | Wrong port, charge-only cable, or console routed to UART — see Step 01. |
| Cannot connect to Wi-Fi | 02 | Must be **2.4 GHz**. Check SSID/password in `menuconfig`. |
| `You are on a 5 GHz channel!` | 02 | Give the 2.4 GHz band its own SSID on the router. |
| CSI count stays 0 | 03 | `CONFIG_ESP_WIFI_CSI_ENABLED` not set → `-Clean` rebuild. |
| CSI rate ~10/s not ~50/s | 03 | Ping failing/rate-limited — try a different ping target. |
| Histogram fragments across shapes | 04 | Turn on `CSI_LLTF_ONLY` for Steps 05–08. |
| Median amplitude spread > 3× | 05 | Weak link — move closer to the router. |
| Waterfall is flat when you wave | 05 | Signal problem — do not proceed until fixed. |
| `only N usable subcarriers` | 06 | Lower `CSI_SELECT_MIN_AMP`. |
| `No usable calibration in NVS` | 07 | Run Step 06 first; do not `erase-flash` in between. |
| False alarms | 07/08 | Raise `CSI_MOTION_SIGMA_X100`. |
| Misses people | 07/08 | Lower it; recalibrate; improve geometry. |
| Compile error on `lltf_en` etc. | any | You pasted classic-ESP32 code. See the C5 API table at the top. |
| `enroll` / `count-train` refused | 10 | Sessions need calibration to have finished (JSON `"state":"detect"`), and only one runs at a time. `stop` the other one first. |
| Enrolment progress stalls | 10 | A window counts only if ≥ 80 % of its packets were moving, by Step 08's flag *and* the motion gate. Keep walking, and watch `hf` while you walk: if it stays under the gate threshold, lower `CSI_VITALS_MOTION_RATIO_X100`. Check that `rate_hz` is ~100 (ping 10 ms). |
| `person` is always `"unknown"` | 10 | Enrolled along one route only (re-enrol by several different routes), or the board, router or furniture moved since enrolment, or the ping rate changed. Re-enrol. |
| Two people walking together get one person's name | 10 | The counter is untrained, so `count` is always 1 and `"multiple"` never fires. Train it (`count-train 1`, then `count-train 2`). |
| `vitals` stays `moving` / `settling` while you sit still | 10 | Your breathing or fidgeting reaches the motion gate's threshold: compare `hf` with it and raise `CSI_VITALS_MOTION_RATIO_X100`. |
| `hf` is `null` | 10 | The gate has no noise floor yet. It learns one from seconds in which the room is reported empty, or failing that, after ~3 s, from the quietest recent second. `status` says which. |
| `dropped` climbs, or `proc_ms_max` is large | 10 | The pipeline falls behind the CSI stream. Go back to a 20 ms ping, or switch a Step 10 feature off in menuconfig. |
| `hr_bpm_exp` is always `null` | 10 | Expected much of the time. It needs stillness, confident breathing *and* a clear peak. |
| `hr_bpm_exp` gives a number | 10 | Validate it with `tools\vitals_validate.py` before believing it. |

---

## How the code is organised

Every step is a **complete, standalone ESP-IDF project**. The Wi-Fi bring-up is
deliberately duplicated across them rather than factored into a shared file —
that keeps the copy-one-folder-and-flash property that makes a stepwise
tutorial work. In each `main.c` the new logic is at the top and the copied
Wi-Fi/ping/CSI boilerplate below it. Steps 05–07 mark it with a
`---- unchanged from Step 0N ----` banner, Steps 03, 04 and 09 with a "same
as" or "identical to" section header, and Steps 02, 08 and 10 not at all.
The copies differ in formatting and comments, and Step 02 also logs the
radio-lock return codes, so find every copy by function name before fixing
one ([docs/ARCHITECTURE.md §7.6](docs/ARCHITECTURE.md#76-change-boilerplate-shared-by-several-steps)).

```
D:\ESP32C5_CSI\
├── README.md                        this file
├── build.ps1                        one-command build/flash helper
├── step01_hello\                    board sanity check
├── step02_wifi_sta\                 WiFi + radio lock
├── step03_csi_hello\                first CSI capture
├── step04_csi_geometry\             measure YOUR CSI shape
├── step05_amplitude\                amplitude + AGC normalisation + CSV
├── step06_baseline\                 empty-room calibration -> NVS
├── step07_presence\                 detection logic
├── step08_final_human_detector\  ⭐  the finished system
├── step09_espressif_sensing\        benchmark: Espressif's own stack
├── step10_recognise_count_vitals\ 🧪 Step 08 + recognise / count / vitals (experimental)
│   ├── main\
│   │   ├── main.c                   Step 08's pipeline + console + the three features
│   │   ├── csi_dsp.c/.h             FFT, Hann, detrend, median, 8x8 eigen, sub-carrier groups
│   │   ├── csi_ncc.c/.h             nearest-centroid classifier with "unknown" (NVS blob)
│   │   ├── motion_gate.c/.h         "is anybody moving?" above ~1.6 Hz, for vitals (JSON hf)
│   │   ├── vitals.c/.h              breathing + experimental heart rate
│   │   ├── gait_id.c/.h             walking-signature features for recognition
│   │   ├── count_est.c/.h           one-vs-several-movers features
│   │   ├── person_vitals.c/.h       whose vitals (JSON vitals_of) + per-person vitals log
│   │   ├── activity.c/.h            coarse activity state + presence episodes (experimental)
│   │   └── csi_gain.c/.h            receiver gain per packet, logging only (JSON agc, fft, gain_chg)
│   └── test\                        host unit tests (MSVC or gcc), synthetic signals only
├── docs\
│   ├── ARCHITECTURE.md              how the code runs, and how to change it safely
│   ├── DECISIONS.md                 why each choice was made (D-numbers), with evidence and dates
│   ├── TESTING.md                   commands, test levels, hardware test plan, dated results log
│   ├── REFERENCES.md                annotated reference list
│   ├── COMPARISON.md                comparison with prior art and the standards each output must meet
│   └── hw_logs\                     short raw-log excerpts linked from TESTING §5.1 (full logs stay outside the repo)
└── tools\
    ├── csi_monitor.py               live waterfall plot (Step 05)
    ├── presence_monitor.py          JSON reader / plotter / logger / command sender (Steps 08, 10)
    ├── vitals_validate.py           Step 10 heart / breathing rate vs a reference device
    ├── serial_capture.py            timed, timestamped serial log of any step, without idf.py monitor
    ├── session_runner.py            plays a TESTING §4 session with cues, logging the board
    ├── session_score.py             scores such a log against the TESTING §4 marks
    ├── dashboard.py                 live dashboard server: serial port or --replay of a log
    ├── dashboard.html               the dashboard page (self-contained, offline)
    ├── sample_session.log           demo session for dashboard.py --replay (marked synthetic, not a measurement)
    └── kconfig_matrix.py            compiles Step 10's main.c in 552 Kconfig switch combinations
```

The nine Step 10 modules include no ESP-IDF headers at all. That is what
lets `test\run_tests.ps1` compile them on the PC and check them against
signals with known answers.

Shared calibration record: `main/csi_baseline.h` (Steps 06–08 and 10) — a ~3.5 KB struct
holding `sigma[k]`, the selected-subcarrier mask, and the empty-room metric
statistics, stored as one NVS blob.

### Step 09 — the benchmark (optional, but do it before tuning)

`step09_espressif_sensing/` contains **none of this project's detection code**.
It runs Espressif's own `esp_wifi_sensing` FSM — the same router-ping, baseline,
hysteresis architecture, written by the people who wrote the radio — so that
Step 08's numbers have something honest to be compared against. It is the only
external component this repo pulls in:

```yaml
# step09_espressif_sensing/main/idf_component.yml
dependencies:
  espressif/esp_wifi_sensing: "^0.1.1"     # -> pulls esp-radar 0.3.4
```

Flash Step 08 and Step 09 alternately in the same room and compare:
Step 08's motion score against `jitter_value`, its presence branch against
`wander_value`, and its single threshold against Step 09's *two*
(`enter_level` / `exit_level`).

> ⚠️ **Check they are reading the same frames first.** esp-radar picks its own
> CSI acquire configuration, which need not match the one in Step 03/04
> (`acquire_csi_legacy` / `_ht20` / `_su` / `_mu`, `he_stbc_mode = 2`). If one
> side is scoring HE frames and the other L-LTF, the two numbers are not
> comparable and the benchmark is silently meaningless. Run Step 04's histogram
> and confirm both see the same `csi_data_len` before you trust any comparison.

> **It is a separate project on purpose.** `esp_wifi_set_csi_rx_cb()` has one
> callback slot, last writer wins, and `esp_wifi_sensing` registers its own.
> Linking it into Step 08 would silently disable whichever registered first —
> a failure that looks like "the detector stopped working" and has no visible
> cause.
>
> Its `CMakeLists.txt` also carries a two-line `-fno-strict-aliasing` fix:
> **esp-radar 0.3.4 does not compile at -O2.** See
> [`docs/REFERENCES.md`](docs/REFERENCES.md) §2.1 for why, and why the warning
> must be fixed rather than silenced.

### Build settings — this repo builds at -O2

Every project here — the ten steps and the root — sets
`CONFIG_COMPILER_OPTIMIZATION_PERF` (-O2),
`CONFIG_FREERTOS_HZ=1000` and `CONFIG_ESPTOOLPY_FLASHMODE_QIO` in their
`sdkconfig.defaults`, with the reasoning written inline in those files.
Assertions are deliberately left **on** until something has actually run on
hardware. Full rationale, including two settings that were tried and reverted,
is in [`docs/REFERENCES.md`](docs/REFERENCES.md) §7.

---

## Step 10 — recognise, count, vitals (experimental) 🧪

**Folder:** `step10_recognise_count_vitals`  **Prove:** nothing yet. This is
the experimental layer, and proving it is your job (see *Validating* below).

Step 10 is Step 08 with three extra analyses running on the same CSI stream,
plus (since 2026-10-16) a coarse activity state built from their decisions.
Everything Step 08 does, Step 10 still does the same way: the same
calibration, the same presence decision, and the same JSON fields first. If
Step 08 has already calibrated this board, Step 10 loads that baseline from
the same NVS record. If not, it calibrates itself exactly as Step 08 does.
The only way Step 10 changes Step 08's presence answer is the breathing hold
(below).

> ⚠️ **Where this stands.** Step 10 compiles without a single warning, and its
> maths passes unit tests on the PC against **synthetic** signals. None of it
> has seen a real person in a real room. The accuracy figures quoted below are
> the best *published* ESP32 results, obtained under easier conditions than
> one C5 pinging a router: controlled rooms and, where the papers say so,
> dedicated transmitters, several links or trained models. They are
> **ceilings, not predictions** ([`docs/REFERENCES.md`](docs/REFERENCES.md) §3.4).

```powershell
.\build.ps1 step10_recognise_count_vitals   # first: sets the target to esp32c5
cd step10_recognise_count_vitals
idf.py menuconfig      # WiFi SSID + password, then the "Recognise / count / vitals" menu
cd ..
.\build.ps1 step10_recognise_count_vitals -Flash -Port COM12
```

(Step 10's `sdkconfig.defaults` also names the target, so running
`menuconfig` first no longer configures it for the classic ESP32. Steps
01–09 do not, so for them the order above matters.)

### What each feature does — and what it cannot do

| | Does | Cannot |
|---|---|---|
| **Recognise** (`gait_id.c` + `csi_ncc.c`) | Learns up to **4 people** from how their walking disturbs the channel: how fast, at what step cadence, and which sub-carriers. Names whoever walks past **alone**, once 3 of the last 7 gait windows agree. | Recognise a still person. Tell anyone apart while 2+ people move. It answers `"multiple"` then, but **only with a trained counter**. Untrained, two people walking together get classified as one person's gait. Know a stranger: the best it can say is `"unknown"`. Survive the board being moved: re-enrol. Published ceiling: 95 % for 2 people, 74 % for 3. |
| **Count** (`count_est.c`) | `0` when the room is empty, `1` whenever someone is present. After on-site training, **1 vs 2+ moving people**, judged by how many independent directions the channel is being disturbed in (the eigenvalue spread of the 8 × 8 sub-carrier-group covariance). | Count past "two or more". Count people sitting still. Work in a room or placement it was not trained in. Real 0–5 counting took **four crossing links** (Wi-CaL). |
| **Breathing** (`vitals.c`) | Breaths per minute for one person **sitting or lying still**, after ~20 s of stillness (stillness as the *motion gate* judges it, below). Confident breathing can keep a still person PRESENT (below). The rate is attributed (`vitals_of`, `person_vitals.c`) to the last person recognised walking in **alone** this visit, and kept in a small per-person log (*Whose vitals*, below). With `count` 2 or two names this visit it is withheld: `vitals` reads `"multiple"`. | Anything while someone moves. Separate two still people, whose breathing mixes. Know that two people are there unless it saw them: the 2+ case needs a **trained counter** (or two recognised names), and two people who sit down without having moved together are invisible to the counter, so they get one mixed rate. Know whose chest it is: the name is carried over from the walk in, not measured. Guarantee a reading: a quiet sleeper at the wrong angle may show nothing. |
| **Activity** (`activity.c`, keys `activity`, `activity_s`, `episode_s`; experimental, 2026-10-16) | A coarse state of the room, from decisions Step 10 already makes (presence after the breathing hold, and the motion gate's `hf`): `empty` (nobody PRESENT for 5 s), `moving` (the gate saw motion in 2 of the last 5 s), `restless` (seconds with fidget-level `hf` in the last 10 s plus moving seconds in the last 5 s total at least 2), `still` (PRESENT, and none of that for 10 s). A change must hold 2 s. Also counts **presence episodes** (seconds present, and how many of them still / restless / moving), where a lapse the breathing hold undoes does not start a new episode; console `stats` shows them. | Tell sitting from standing or lying: nothing in one link's 26 amplitude tones supports posture. Stay `still` for a motionless person once PRESENT lapses: a lasting `still` is in practice the breathing hold keeping PRESENT, so with `CSI_VITALS_HOLDS_PRESENCE` off it rarely appears. Tell a fan, a pet or a curtain from a person (Step 08's limits). Its thresholds come from synthetic data, and a seated person breathing hard can read `restless`. Remember episodes across a reboot: RAM only. No accuracy is measured. |
| **Heart rate** (`vitals.c`, key **`hr_bpm_exp`**) | Reports a number only when the heart band shows a clear peak, only while breathing is also confident, and only while nobody moves. Attributed, withheld and logged exactly like breathing. | Be trusted before you validate it. A heartbeat moves the chest 10–50× less than breathing does, in a band where breathing harmonics also land. In the synthetic test it came through only when breathing was strong (*Heart rate: what the synthetic test shows*, below). Its per-person mean is just as unvalidated. **Not a medical device.** |

### Stillness: the vitals motion gate

Vitals must only analyse a room in which nobody moves, and **Step 08's motion
flag cannot make that call.** Its fast EMA (0.5 s) puts a corner at about
0.32 Hz, which is inside the 0.1–0.5 Hz breathing band, so a breathing chest
lifts `M`. In the synthetic end-to-end test, a seated person breathing at
2 noise units (enough to carry a heartbeat 1/10 that size) held Step 08's
flag on for 98.6–100 % of the time. Vitals gated on that flag never ran a
single analysis.

So Step 10 has its own gate, `motion_gate.c`. It high-passes the 8
sub-carrier-group signals twice, which puts its corner at ~1.6 Hz. Walking,
gestures and fidgeting are above that and breathing is not. It then divides
the remaining energy by the level it measured while Step 08 said the room was
empty. That ratio is the JSON value **`hf`**:

| `hf` | Means |
|---|---|
| about 1 | nobody moving. Breathing barely moves it |
| ≥ `CSI_VITALS_MOTION_RATIO_X100` / 100 (default **4.0**) for 0.3 s | MOVING. Below it for 1 s: still again |
| `null` | no noise floor yet (see below) |

**Gait windows and count blocks use the gate too.** A packet counts as
*moving* for recognition and counting only if Step 08's flag **and** the gate
both say so. Without that veto, a person breathing hard in a chair filled
gait windows and count blocks as if they were walking. **A still, breathing
person no longer produces gait windows.** While the gate has no floor it
reports MOVING, so recognition and counting then fall back to the flag alone
and vitals waits.

**Where the floor comes from.** Normally from the empty room: seconds in
which Step 08 reported empty on every packet, which is what you get after a
calibration. If the board boots from a stored baseline with someone already
in the room, the gate uses the quietest 1-second interval of the last
2 minutes instead, and `hf` stays `null` for the first ~3 s. Known weakness:
if someone moves from the very first packet, that motion *becomes* the floor
and the gate calls it still after ~4 s (host test `gate`, case (e)). One
quiet or empty second repairs it. `status` prints where the current floor
came from.

**Tuning `CSI_VITALS_MOTION_RATIO_X100`** (menuconfig → *Recognise / count /
vitals*). The setting exists only with vitals enabled. With vitals off,
recognition and counting use the gate at a fixed 4.0. Watch `hf` in
`presence_monitor.py`'s status line or the JSON:

1. **Empty room:** `hf` should sit near 1.
2. **Sit still and breathe normally:** it should stay well below the
   threshold. If it reaches it, `vitals` keeps showing `moving` / `settling`
   and never gets to `ok`: raise the threshold.
3. **Walk around:** it should sit far above the threshold. If slow walking or
   fidgeting stays below it, vitals analyse motion and report nonsense, and
   enrolment stalls: lower the threshold.

Set it comfortably between the two. For scale, here is what the synthetic
tests saw, in units of the group-signal noise. Nothing here was measured on
hardware:

| Synthetic case | `hf` |
|---|---|
| empty room, 3 × 120 s (`e2e` (d)) | mean 1.00, max 1.59 |
| seated person, breathing 15/min at 0.2–4 noise units, floor from the empty room (`e2e` (c)) | max 1.42–2.33 |
| the same, gate never saw the room empty | max 2.69 |
| breathing 30/min at 8× the noise (`gate` (b)) | max 3.73 |
| walking at 1× / 2× / 5× the noise (`gate` (c)) | not detected / mean 4.8 / mean ~25 |
| the synthetic walkers of `e2e` (a) | mean 6.2, below 4.0 on 30 % of packets |

The default has a cost. On those walkers the gate called only 80.8 % of
walking packets MOVING, and the device got 222 gait windows where the flag
alone gave 287 (of 369). A threshold of 3.0 gives 250 on the same data and
2.5 gives 263, at the price of less margin over hard breathing (3.73 above).
How strong *real* walking is in `hf` has never been measured.

### Breathing can hold presence, carefully

With `CSI_VITALS_HOLDS_PRESENCE` on (the default), confident breathing keeps
a motionless person PRESENT, or brings them back if Step 08's 5 s hold
lapsed:

```
W (…) csi-human: >>> still person, breathing 14.8/min - holding PRESENT
```

This only applies to a still spell, as the gate sees it, that **began while
PRESENT**. A periodic signal in a room that was already empty when it went
quiet can never *start* presence this way. Two limits, both unmeasured:

* **Walking out.** When someone leaves, the gate goes still about 1 s after
  the walk ends, while Step 08's 5 s hold still says PRESENT. That empty-room
  spell therefore counts as "began present". From then on, only vitals' own
  false-positive rate keeps a fan or a curtain moving at 6–30 cycles/min from
  holding PRESENT in the empty room, until the next motion, a CSI gap or a
  recalibration. In the synthetic tests, noise passed for breathing in 0 of
  400 analyses (`vitals`) and 0 of 147 (`e2e`, empty room). A real fan has
  never been tried.
* **Walking out gently.** Someone who leaves too gently for the gate never
  ends the still spell, and their breathing is still in the 30 s analysis
  window. PRESENT can then be held for part of a window after they have gone.

Turn the option off for Step 08's behaviour: vitals are still reported, and
presence is left alone.

**What survives a lapse that the hold undoes.** Breathing needs ~20 s of
still data, so for someone who sits down, Step 08's 5 s hold usually runs out
first. Presence then ends, and the visit's gait votes and count are cleared.
When breathing brings PRESENT back, `count` and `count_src` return to what
they were before the lapse, provided the gate stayed still throughout it and
it lasted no more than 2 × `CSI_VITALS_WINDOW_S` (60 s by default). `person`
does not return: a seated person casts no gait votes, so it stays `null`. The
name is carried separately, as `vitals_of` (next section). With the option
off, a lapse ends the visit.

Only breathing ends a lapse this way. If Step 08 reports somebody again
first (its own motion or static trigger), the lapse goes on until the
breathing hold takes over, and any movement the gate sees before then ends
the visit. That is because a walk-in looks the same at first: Step 08 fires
first, and the gate only later. In the synthetic tests the gate reported
MOVING 0.4–3.5 s after Step 08, and in one 6 s walk not at all. Without
this rule, a newcomer who sat down within 60 s of someone else leaving
would take over that person's name and count.

### Whose vitals: `vitals_of` and the per-person vitals log

Recognition needs a person **walking**, and vitals need a person **sitting
still**. The two never happen at the same moment, so Step 10 cannot measure
whose breathing it is hearing. What it does instead is remember who was
recognised on the way in and carry that name onto the vitals, for as long as
nothing has contradicted it. `vitals_of` in the JSON is that claim: *the last
person recognised walking in alone during this visit, who has not been
contradicted since*. It is not a measurement of whose chest this is. It needs
both recognition and vitals switched on (`person_vitals.c`), and is `null`
otherwise.

**The rules.**

* A visit starts with nobody named. Each decisive recognition answer while
  `count` ≤ 1 updates it: a name backed by 3 of the last 7 windows, or
  `"unknown"` once 3 windows have voted.
* A name → `vitals_of` is that name, if it is the first name of this visit
  (or the same one again).
* `"unknown"` → `vitals_of` is `null`. The rates are still reported, but
  credited to nobody. The first name is remembered, so raj → unknown → raj
  gets raj back.
* A **second, different name** in the same visit (raj → sam, or
  raj → unknown → sam), or `count` reaching 2 → **multiple**: `vitals` reads
  `"multiple"`, and `br_bpm`, `br_conf`, `hr_bpm_exp`, `hr_conf` and
  `vitals_of` are all `null`. One link cannot separate two people's breathing,
  and one number would describe neither of them.
* **"multiple" is sticky** until the room has been empty. `count` going back
  from 2 to 1 does not undo it, because nobody can tell who stayed. (The
  stickiness lives in the recognition latch. In a build with recognition off,
  `"multiple"` follows `count` alone, which also stays at 2 while everyone
  sits, because a block with too little movement does not vote.) A running
  `count-train 2` session also shows `"multiple"`: you have said two or more
  people are moving.
* Fidgeting while PRESENT keeps the name. The exception is when Step 08
  alone has ended a lapse and breathing has not yet taken over: then any
  movement the gate sees ends the visit (below), because the gate cannot
  tell a fidget from a walk-in. For a "multiple" visit that means two
  seated people who fidget in that window get one mixed rate, with no name
  on it.
* A presence lapse that the breathing hold undoes does not end the visit.
  During the lapse the JSON says `present` 0, `vitals` `"no-person"`,
  `vitals_of` `null`. After the restore, `vitals_of` is back:

| Moment | `present` | `person` | `count` / `count_src` | `vitals` | `vitals_of` |
|---|---|---|---|---|---|
| raj walks in alone and is recognised | 1 | `"raj"` | 1 | `"moving"` | `"raj"` |
| raj sits; Step 08's 5 s hold runs out before breathing is confident | 0 | `null` | 0 / `"presence"` | `"no-person"` | `null` |
| (sometimes) Step 08 re-detects raj before breathing is confident | 1 | `null` | 1 / `"presence"` | `"settling"` or `"no-signal"` | `null` (the lapse is not over yet) |
| confident breathing restores, or takes over holding, PRESENT | 1 | `null` | as before the lapse | `"ok"` | `"raj"` |

**What ends a visit** (nobody named, first name forgotten):

* the motion gate seeing movement while the room is reported empty, or
  after Step 08 has reported somebody again but before breathing has taken
  over (see *What survives a lapse*, above), so whoever walks in next starts
  a new visit;
* the room reported empty, or back only on Step 08's word, for longer than
  2 × `CSI_VITALS_WINDOW_S` (60 s by default) from the start of the lapse;
* presence ending, when `CSI_VITALS_HOLDS_PRESENCE` is off;
* a CSI outage (`no_csi`), `recal` or the button, a new baseline, a reboot,
  the start of an `enroll` or `count-train` session (so `vitals_of` is `null`
  during sessions), and `forget` of the person it names.

Changes are logged:

```
W (…) csi-human: >>> vitals of: raj
W (…) csi-human: >>> vitals of: nobody (unknown walker)
W (…) csi-human: >>> vitals of: nobody (2 names this visit)
```

The human-readable line adds `of=raj` whenever `vitals_of` differs from
`who`, which it does after a restore (`who=-`). `status` shows the name on its
vitals line (`(of raj)`, or `(of -)`), and while vitals are `"multiple"` adds
`vitals withheld: 2+ people this visit - one rate would mix them`.

**What it cannot catch.** None of these has been measured.

* **Someone who sits down before being recognised.** Recognition needs at
  least 3 qualifying gait windows: 512 packets, so ~5 s of walking at 100 Hz
  or ~10 s at 50 Hz, and usually more (*How long it takes*, below). If sam
  walks in while raj sits and sits down before that, the counter saw one
  mover (sam) and recognition gave no new answer. sam's breathing then mixes
  into a reading that is reported, and logged, as raj's. This cannot be
  detected. If sam is not enrolled and does get `"unknown"`, the mixed rate
  is reported under no name.
* **An untrained or switched-off counter.** `count` then never reaches 2, and
  `"multiple"` can only come from two different names. Two people who walk in
  together are classified as one gait, possibly under one name, and then get
  one mixed rate under it. Two people nobody recognised get one rate. If more
  than one person uses the room, train the counter.
* **Walking out, then a fan.** This is the *Walking out* limit above, now
  with a name on it. If a fan or a curtain passes for breathing and brings
  PRESENT back within 60 s of the lapse, its "breathing" is reported, and
  logged, as the person who left, for as long as the hold lasts. The 60 s
  only bounds how late a restore can still claim that name.
* **Walking in, unseen by the gate.** Someone who walks in within 60 s of
  raj leaving, and moves too little above ~1.6 Hz for the gate ever to call
  it MOVING, looks exactly like raj still sitting there. Their breathing is
  then reported, and logged, as raj's. A walk-in the gate does see ends
  raj's visit however late it sees it (the synthetic tests, `e2e` e3).
  In one synthetic room a 6 s walk-in was never seen.
* **A wrong recognition.** The name is whatever recognition said, and
  recognition is unvalidated. So is the attribution.

**The log.** Each enrolled slot has one small record: breathing and heart
rate means, the number of episodes behind them, and the last episode's
values. A completed vitals analysis (about every 2 s, never per packet or per
JSON line) credits the named person when all of these hold: PRESENT, `count`
≤ 1, `vitals_of` names them, and `vitals` is `"ok"` with a breathing rate.
The heart rate is added only when it is valid too.

Consecutive analyses share most of their data (a 30 s window every 2 s, and
the heart rate is already the median of the last 5), so 15 of them are not 15
measurements. The samples of one still spell are therefore averaged into one
*episode* value. An episode ends when the gate says moving, when the vitals
buffer starts over, or when the name changes or the visit ends. It is kept
only if it has at least 3 samples (breathing and heart rate counted
separately), and is then folded into a running mean. After 32 episodes the
mean becomes an exponentially weighted one, so it follows slow drift. The
heart-rate fields keep the `_exp` suffix: a mean of an unvalidated number is
still unvalidated. `people` prints each record:

```
CMD people: 2 of 4 slots ready (a slot needs 10 gait windows, enrolment collects 30)
    slot 0: raj, 30 windows, ready
        vitals: 3 episodes, breathing 14.8/min (mean 15.2, 41 samples), heart ~71/min (mean ~73, 2 episodes) EXPERIMENTAL, last 412 s ago
    slot 1: sam, 30 windows, ready
        vitals: none logged
    slot 2: empty
    slot 3: empty
    enrolled on <N>-byte CSI, channel <C>
    vitals now: raj
```

The first rate is the last episode's, and `samples` counts the analyses
behind the breathing episodes. `enrolled on` (printed once an enrolment has
completed) is the CSI length and channel the templates were made on. The last
line is `vitals now: <name>`,
`nobody named`, or `multiple (2+ this visit)`. There is no clock on the
board, so "last … ago" is uptime, kept in RAM only. After a reboot it reads
`not seen since boot`, although the means are kept.

**Where it is stored.** One ~170-byte blob, NVS key **`vit_log`**, next to
the baseline and the models. Apart from `forget`, `vitals-reset` and
`enroll` (below), it is written only when it has changed, and at most once every
`CSI_VITALS_LOG_SAVE_S` (300 s by default, counted from boot). A failed write
waits the same interval before it retries. So a power cut loses up to that
many seconds of log. At the default, the worst case is 86400 / 300 = 288
small writes a day, which works out to decades of wear on the 24 KB NVS
partition (an estimate, not a measurement). A save is logged only when it
fails or takes longer than 50 ms. At boot the log is checked as a whole
(size, version, names, every number in range). If anything fails, it starts
empty rather than partly trusted. A record whose name does not match the
template in the same slot is dropped, and the drop is logged. So a slot that
was re-enrolled, or a recognition model that failed to load, never hands one
person's history to another. A slot re-enrolled under the **same** name
passes that check, which is why `enroll` clears the slot's log in flash as
soon as it starts, before the new template can be saved.

**Deleting it.** `forget <slot>` / `forget all` delete the log along with the
template(s). `vitals-reset <slot>` / `vitals-reset all` delete only the log
and keep the templates. `enroll` into a slot deletes that slot's log when
the session starts, and the old template is replaced when it completes. All
three write the log to NVS at once. If that write fails, `enroll` adds an
indented `vitals log NVS <error>` line to its answer.

> ⚠️ **"forget" is not a secure wipe.** This is health-related data under a
> person's name, and NVS is **not encrypted** in this project. Deleting or
> rewriting an NVS entry only marks the old one as erased. Its bytes, and the
> older copies from earlier saves, stay in flash until NVS reuses that page,
> and anyone with the board and `esptool` can read the flash. The gait
> templates are in the same position. If that matters to you, turn on
> ESP-IDF's NVS encryption. It needs flash encryption or an HMAC key in
> eFuse, both of which burn eFuses for good, and neither is set up or tested
> here. `idf.py erase-flash` does remove everything, calibration included.

### Before recognising anyone: raise the packet rate

The gait features need **≥ 100 packets/s**, and the default 20 ms ping gives
~50. Set `CSI_PING_INTERVAL_MS` to **10**, but only after Step 03 has shown
heap headroom and that your router keeps up (its rate line should read
~100/s). **Then recalibrate** (`recal`). The packet rate is measured during
calibration and stored with the baseline, and every gait frequency is computed
from it. A boot whose live rate is more than 20 % off the stored one warns:

```
W (…) csi-human: live CSI rate 98.1/s differs from the calibrated 49.8/s - recalibrate ('recal'), and re-enrol if CSI_PING_INTERVAL_MS changed
```

**Templates belong to the rate they were enrolled at.** Change the ping
interval and you must re-enrol everyone and retrain the counter. Breathing is
resampled to 10 Hz by timestamp, so it works at either rate. (The host tests
ran recognition at 100 Hz and counting and vitals at 50 Hz.)

### Console commands

Type them into `idf.py monitor` and press Enter. **Typed characters are not
echoed**, so you are typing blind. Or send them with
`presence_monitor.py --send` (below). Each command is answered by a line
starting `CMD `, and `status`, `people` and `help` add indented lines under
it. Enrolment and count training need the detector running (JSON
`"state":"detect"`), and only one session runs at a time.

| Command | Does |
|---|---|
| `help` (or `?`) | list the commands |
| `status` | one block: phase, PRESENT / empty (or *NO USABLE CSI … presence unknown* during an outage, as the JSON's `no_csi`), the live packet rate over the last second next to the calibrated one (*nominal* before any calibration), channel, free heap; count, person, vitals and whose they are (`(of raj)`, plus a `vitals withheld` line while they are `"multiple"`); the motion gate (`hf`, where its floor came from); the session; the slowest packet and dropped packets since boot |
| `enroll <slot 0-3> <name>` | start enrolling a person into a slot (`enrol` works too). Name: 1–15 characters of `A-Z a-z 0-9 _ -`. `unknown`, `multiple` and `null` are reserved, and a name already in another slot is refused. **The slot's old template is deleted as the session starts**, and with it that slot's vitals log. |
| `people` | list the slots: name, windows collected, `ready` / `collecting` / `incomplete`, and under each one its vitals log. The last line says whose vitals are being reported right now (*Whose vitals*, above) |
| `forget <slot>` / `forget all` | delete template(s) **and that person's vitals log**, both saved to NVS at once. Refused while an enrolment runs |
| `vitals-reset <slot 0-3>` / `vitals-reset all` | delete a person's vitals log (or everyone's), saved to NVS at once. The templates are kept. Needs recognition and vitals both switched on |
| `count-train 1` | collect "one person moving" blocks |
| `count-train 2` | collect "two or more people moving" blocks |
| `count-reset` | delete the count model. Refused while count training runs |
| `stop` | cancel the running enrolment or training, **discarding** what that session collected. That clears the class it was filling, and NVS is updated to match |
| `recal` | recalibrate. Same as holding the button 2 s, and only while detecting. Refused (and the button ignored) during a CSI outage (`no_csi`): wait for the link to return, or reboot if the CSI shape changed. Cancels any session and keeps the models |
| `stats` | the activity state and how long it has held, this presence episode's seconds (still / restless / moving / other), the last episode, and how many episodes ended since boot. Needs `CSI_ACTIVITY_ENABLE`. Experimental: nothing here is validated |
| `explain` | print the `config` explain line again: the live thresholds and model state that the dashboard's "How it decides" texts use (*See how it decides*, below). Needs `CSI_EXPLAIN` |

```powershell
# send commands, then keep monitoring (and logging) on the same port
python tools\presence_monitor.py --port COM12 --send "people" --send "status"
python tools\presence_monitor.py --port COM12 --send "enroll 0 raj" --csv enrol_raj.csv
python tools\presence_monitor.py --port COM12 --send "vitals-reset all"
```

### See how it decides: the live dashboard and the why lines

Step 10 can show its working, not just its answers. With `CSI_EXPLAIN` on
(the default) it prints two extra kinds of line:

* A **`why:` line** in plain English for every decision, for you to read in
  `idf.py monitor`. Each one gives the numbers, the thresholds they were
  compared with (the live ones) and the result. These examples show the
  shape of the lines; the values are made up:
  ```
  I (…) csi-human: why: gait window 12 - 84% moving; nearest raj 1.21, next asha 2.02 (reject > 2.50, margin needs 1.15, got 1.67) -> vote raj; votes raj 4 of 7 -> person raj
  I (…) csi-human: why: gait window 13 - only 40% of packets moving (needs 80%) -> no vote
  I (…) csi-human: why: count block 5 - movement in 62% of packets; one-mover 1.80, two+ 0.90 -> vote 2+; votes 3 of 5 -> count 2+
  I (…) csi-human: why: count block 6 - no count model yet ('count-train 1' then 'count-train 2') -> count follows presence (1)
  I (…) csi-human: why: PRESENT - motion 7.4 sd >= threshold 5.0
  I (…) csi-human: why: vitals - breathing 14.8/min (conf 6.2 >= 4.0), heart ~71/min EXPERIMENTAL (conf 5.4 >= 5.0), of raj
  ```
* An **`EXPLAIN {json}` line** with the same numbers, for programs. The
  live dashboard is built on these. Their format is
  [ARCHITECTURE §3.7](docs/ARCHITECTURE.md#37-explain-output-step-10-csi_explain).

There is one `why:` line per event: at 50 packets/s, one per gait window
about every 2.6 s, and one per count block about every 5.1 s.

**Start the dashboard.** Close `idf.py monitor` first, because only one
program can own the port. The dashboard's console panel replaces typing
into the monitor.

```powershell
& C:\esp\v6.0.2\esp-idf\export.ps1       # its Python already has pyserial (otherwise: pip install pyserial)
python tools\dashboard.py --port auto --open                                   # or --port COM13
python tools\dashboard.py --port COM13 --log D:\ESP32C5_CSI_logs\office.log --open   # also keep a raw log
python tools\dashboard.py --replay tools\sample_session.log --open             # no board: play back a session
```

It serves the page at <http://127.0.0.1:8765/>, which only this PC can
open. (`--host 0.0.0.0` would let a phone on your network open it, and send
console commands too, so only do that on a network you trust.) It never
pulses DTR/RTS, which on Windows can latch the C5 in reset
([DECISIONS D-153](docs/DECISIONS.md#d-153-never-pulse-rtsdtr-from-pyserial-on-the-c5s-usb-serial-jtag-under-windows)),
and it reopens the port by itself after the board resets. `--log` writes
the same format that `--replay` reads, so any session can be played back
later. Replay needs no board and no pyserial, and the console buttons do
nothing in it. If `tools\sample_session.log` starts with `# SYNTHETIC`, its
numbers are made up: it shows how the page works, not what your room does.

**What you are looking at.** Each panel has a short "How it decides" text
that uses the thresholds your board is actually running with.

| Panel | Shows |
|---|---|
| Pipeline | CSI in → calibrated baseline → motion score → PRESENT, then three branches: gait windows → votes → **who**; count blocks → votes → **how many**; motion gate still → vitals → **breathing / heart**. Each stage lights up when it last fired, with its current value |
| Link | packets/s, RSSI, dropped packets, the CSI shape (length, subcarriers, channel, L-LTF or not) and the slowest packet. A banner while there is no CSI or while it calibrates (leave-room countdown, warm-up, phase A / B) |
| CSI waterfall | the amplitude of each subcarrier in use, scrolling in time (`CSI_EXPLAIN_AMP_HZ` rows per second). Someone walking tears streaks through it |
| Presence | the motion score over the last 60 s against the motion and release lines (5.00 and 2.50 sd by default), the static score against its line, PRESENT shaded, and why presence last changed |
| Recognise | for each enrolled slot, how far the latest gait window was from it, against the reject line; the nearest and the runner-up; the vote ring; who it has decided on. With nobody enrolled, it says how to enrol, with a prefilled `enroll 0 <name>` button |
| Count | distance to "one mover" and to "two or more", how much of the block was moving, the vote ring and the estimate. Untrained, it explains `count-train 1` and `count-train 2`, with buttons |
| Vitals | breathing and the **experimental** heart rate, each with its confidence against the threshold; how long it has been still against the 20 s needed; whose vitals; why it says `"multiple"` |
| Console | buttons for `status`, `people`, `help`, `explain`, `recal`, `stop`, `count-train 1`, `count-train 2`, `count-reset`, `enroll`, `forget`, `vitals-reset`, a box for any command, and the `CMD` replies |
| Event log | the `why:` lines, presence changes and recognitions, with times. "Record" saves the session |

**How it recognises, in the order the panel shows it.** A gait window is
256 packets, and a new one starts every 128 (2.6 s at 50 packets/s):

1. **Did enough of it move?** At least 80 % of its packets must be
   *moving*: Step 08's motion flag **and** the motion gate. If not, no vote
   (`why: … only 40% of packets moving`). No vote either while `count` is
   2+, while a session runs, or when nobody is enrolled.
2. **How far is it from each person?** The distance to every enrolled slot
   that holds ≥ 10 windows, in units of the window-to-window spread seen
   while enrolling, so about 1 is typical for the right person.
3. **Vote.** The nearest person gets the vote only if the distance is no
   more than the reject line (2.5) and the runner-up is at least 1.15×
   farther. Otherwise the window votes `"unknown"`: too far from everyone,
   or too close to call.
4. **Decide.** Of the last 7 votes, a name needs ≥ 3, strictly more than
   `"unknown"`, and no tie. That is `person`.

Details: *How it answers*, below.

**How it counts.** A count block is 256 packets, and blocks do not overlap
(5.1 s at 50 packets/s):

1. **Was anyone moving?** The block votes only if ≥ 30 % of its packets
   were moving.
2. **In how many directions is the channel disturbed?** The block's
   features describe how the disturbance spreads over the 8 subcarrier
   groups (the eigenvalue spread of their covariance). The idea: one walker
   disturbs them mostly along one main direction, two walkers along more
   independent ones.
3. **Vote.** Whichever the block is nearer to, the "one mover" or the
   "two or more" examples you trained with `count-train`.
4. **Decide.** `count` is the majority of the last 5 votes, and 2 needs a
   strict majority.

Untrained, there is nothing to compare with, so `count` is simply 1
whenever anyone is present. It counts **movers**: people sitting still are
invisible to it. Details: *Training the counter*, below.

**Your room today: three people, a laptop hotspot, nothing trained.** What
to expect:

* **Calibration** needs an empty room. If the other two can step out for
  45 s, do that and send `recal`. If they cannot, use the workaround in
  the box below, and read what it costs.
* **Presence:** someone walking should trigger it. After a still-room
  calibration, people sitting still are not detected.
* **Recognise** says nobody is enrolled. Gait windows still appear
  (`why` `"no-model"`), so you can watch how much of each window counts as
  moving while someone walks.
* **Count** says there is no count model yet, so `count` follows presence:
  1 while PRESENT, even with three of you in the room, and 0 when not
  (after a still-room calibration, that includes all three sitting still).
* **Vitals:** with three people breathing and an untrained counter, any
  breathing rate is a **mix of three chests**. `vitals` can only say
  `"multiple"` once the counter is trained or two names were recognised.
  Do not read it as anyone's rate.

To see **counting** work, train it: `count-train 1` while one person walks
around and the other two sit still, hands off the keyboard (about 2 minutes
at 50 packets/s; whether seated people disturb the training has not been
measured), then
`count-train 2` while two walk at the same time (about 2 minutes). Then
watch the Count panel with one walker, then two. With two sitting still and
one walking, the right answer is 1.

To see **recognition** work, first settle the packet rate, because
templates belong to the rate they were enrolled at. Recognition wants
≥ 100 packets/s (*Before recognising anyone: raise the packet rate*); the
laptop hotspot has only been measured at about 50/s
([TESTING §5.1](docs/TESTING.md#51-hardware)). At 50/s you can still watch
windows and votes, but fewer windows will count. Train the counter before
enrolling. Then enrol each person walking **alone**: the procedure says
everyone else leaves (*Enrolling a person*). If they cannot, they sit still.
Whether seated people spoil a template has not been measured.

> ⚠️ **Workaround, not the procedure: calibrating a room you cannot empty.**
> The calibration needs an **empty** room. When that is impossible, everyone
> stays in place, **seated and still** (no typing, no gestures, no phone in
> hand), from `recal` until the `CALIBRATED` banner, and ignores the "leave
> the room" prompts. That is 45 s at the defaults: 10 s countdown, 5 s
> warm-up, 15 s phase A, 15 s phase B. What it breaks: the measured noise
> and both thresholds then include your breathing and small movements, so
> the detector is less sensitive. The static-presence reference contains
> you, so **the seated people are not detected while they sit still**, and
> the room reads empty until somebody moves. The motion gate's "nobody
> moving" level is learnt from the same seated room. What still works:
> **movement** is detected, against a higher threshold. How much
> sensitivity is lost has not been measured. The baseline stays in NVS, and
> Step 08 would load it too, so send `recal` in an empty room as soon as you
> can, and label anything measured on it "still-room baseline"
> ([DECISIONS D-166](docs/DECISIONS.md#d-166-still-room-calibration-is-a-workaround-for-a-room-that-cannot-be-emptied-not-the-procedure),
> [TESTING §2.6](docs/TESTING.md#26-the-live-dashboard-replay-live-use-smoke-test)).

**What the dashboard does not tell you.** It shows how the firmware reaches
its answer, not that the answer is right. Recognition, counting and heart
rate are **experimental and unvalidated**
([TESTING §5](docs/TESTING.md#5-results-log)), and the page carries a
permanent note saying so. It has not yet been run against the board
(TESTING §2.6).

**Older firmware.** Step 08, or Step 10 built without `CSI_EXPLAIN`, prints
no explain lines. The page then shows what the `JSON` line carries and
says `explain lines not received - flash Step 10 with CSI_EXPLAIN`.
`presence_monitor.py` ignores explain lines, so it and its CSV are
unchanged.

### Enrolling a person

1. The board must be **where it will live**, calibrated and detecting, at the
   ping rate you will use from now on (10 ms, above).
2. **Everyone else leaves.** The template learns whatever moves, and a second
   person walking through it poisons the template. Nothing guards against
   that: the counter is not consulted during a session.
3. Send `enroll 0 raj`. The board answers:
   ```
   CMD enroll 0 raj: started - raj walks around the room ALONE, at a normal pace, by several different routes, until 'enrol raj: done' (30 windows)
   ```
4. **Walk alone, at your normal pace, by several different routes** through
   the area between the board and the router: across it, along it,
   diagonally, near the board and far from it. Do not stop. The routes matter
   most. `person_dist` is measured in units of the spread seen *during
   enrolment*, so one fixed route gives a template so narrow that your own
   later walks fall outside it. In the synthetic end-to-end test, enrolling
   along one route left 79 % of the same people's later windows, and 14 of
   their 18 walks, as `"unknown"`. With a varying path it was 2.4 % of windows.
5. Progress is logged for each window that counts, and the JSON `session`
   field follows it (`"enroll:raj 12/30"`):
   ```
   I (…) csi-human: enrol raj: 12/30 windows
   ```
   At the target it saves to NVS and stops by itself:
   ```
   W (…) csi-human: >>> enrol raj: done - slot 0, 30 windows, saved: ESP_OK
   ```
6. Repeat for each person, one slot each. A slot takes part in recognition
   once it holds ≥ `CSI_ID_MIN_WINDOWS` (10) windows.
7. **Re-enrol after moving the board, the router or furniture, or after
   changing channel or ping rate.** A template describes how a person
   disturbs *this* link. The model stores the CSI shape and channel of its
   **most recent** enrolment (one record for the whole model, not one per
   slot), and logs a warning when detection starts, or when the Wi-Fi
   channel changes while running, if they no longer match. Enrolling one
   slot on a different channel from the others also warns, once: the model
   then mixes two channels and the next boot cannot tell, so re-enrol
   everyone.

**How long it takes.** A gait window is 256 packets, and a new one starts
every 128 packets (2.56 s at 50 Hz, 1.28 s at 100 Hz). A window counts only
if ≥ 80 % of its packets were *moving*, meaning Step 08's flag **and** the
motion gate. Pauses don't count, and neither does walking too gentle to reach
the gate's threshold. If every window counted, 30 would take 79 s at 50 Hz or
40 s at 100 Hz. They don't all count: on the device path of the synthetic
end-to-end test (100 Hz), only 222 of 369 walking windows (60 %) qualified.
At that rate 30 windows take about 50 attempts. **Budget about 2 min 10 s of
continuous walking at 50 Hz, or about 65 s at 100 Hz**, and more if you
pause. The pass rate for real walking has never been measured. If progress
stalls, watch `hf` while you walk (*Stillness* above).

### How it answers

While someone is present, `count` is ≤ 1 and no session is running, each
gait window is classified against every slot holding ≥ 10 windows. A window
votes for the nearest template only if two tests pass: its distance is no
more than `CSI_ID_REJECT_X100` / 100 (2.5), and the runner-up is at least
`CSI_ID_MARGIN_X100` / 100 (1.15×) farther away. Otherwise it votes
`"unknown"`. A distance is the RMS over 18 gait features, in units of the
enrolled templates' pooled window-to-window spread. About 1 is typical for an
enrolled person (1.04 and 1.39 in the two synthetic tests).

The last 7 votes of the current visit decide `person`. A name needs ≥ 3 votes,
strictly more than `"unknown"` has, and no tie with another name. Otherwise
the answer is `"unknown"` — but only once at least 3 windows have voted.
Before that, and with no name yet, `person` is `null`, so a visit does not
open with a meaningless `"unknown"`. The votes are cleared when presence ends,
and when the counter switches to two or more: the counter lags by about three
blocks, so those votes may be two movers read as one. That includes a lapse
the breathing hold later undoes, and a seated person casts no new votes, so
`person` stays `null` after such a restore. The name the vitals belong to is
kept separately, as `vitals_of` (*Whose vitals*, above). Changes are logged:

```
W (…) csi-human: >>> recognised: raj (dist 0.84, 5/7 windows)
W (…) csi-human: >>> recognised: unknown - no enrolled person has a clear majority (4/7 windows matched nobody)
W (…) csi-human: >>> recognised: multiple people moving - not classifying
```

In the synthetic end-to-end test (two enrolled walkers at 100 Hz, path
varying, 3 seeds), 97.6 % of their held-out windows were right, 2.4 % were
`"unknown"`, and none got the other name. A never-enrolled third walker came
out `"unknown"` in 63 of 63 windows. Two caveats. Those windows were gated by
the test's ground truth ("walking"), not by the device's flag-and-gate path,
which the test only counts (222 of 369). And the walkers are separable by
construction. So this shows the arithmetic works. It is not a recognition
rate, and device-path accuracy has not been measured even synthetically.

> ⚠️ **The `"multiple"` guard needs a trained counter.** Recognition only runs
> while `count` ≤ 1. Until the counter is trained, though, `count` is `1`
> whenever *anyone* is present. Two people walking together then get
> classified as if they were one, and the answer is meaningless. The same goes
> for vitals: without a trained counter, `"multiple"` can only come from two
> different names, so two people get one mixed rate. If more than one person
> uses the room, train the counter (next section) before trusting `person`,
> `vitals_of` or a rate.

### Training the counter

1. Send `count-train 1` with **exactly one** person walking around, and keep
   going until it finishes: 20 blocks (`CSI_COUNT_TRAIN_WINDOWS`). A block is
   256 packets, the blocks do not overlap, and a block counts only if ≥ 30 %
   of its packets were moving (flag and gate). That is at least 102 s of
   walking at 50 Hz, or 51 s at 100 Hz. On the synthetic device path, 210 of
   229 blocks (92 %) qualified, so budget about 2 minutes or about 1 minute:
   ```
   I (…) csi-human: count-train:1: 7/20 blocks
   I (…) csi-human: count-train:1: block skipped - moving only 12% of it (needs 30%)
   W (…) csi-human: >>> count-train:1 done (20 blocks), saved: ESP_OK - now 'count-train 2' with two or more people moving
   ```
2. Then send `count-train 2` with **two or more** people walking around at
   the same time.
3. Until both classes hold 20 blocks, `count` is `1` whenever anyone is
   present (`count_src:"presence"`). Once both are trained, every block with
   ≥ 30 % movement is classified, and `count` is the majority of the last 5
   (`count_src:"trained"`). A `2` needs a strict majority, and a tie stays
   `1`. Changes are logged as
   `>>> count: two or more moving (3 of the last 5 blocks say two or more)`.
   A block in which nobody moved enough does not vote, and the last answer
   stands. **Each new visit starts again at `1` (`"presence"`)** until its
   own first block has been classified, because the votes are cleared when
   presence ends. A lapse that the breathing hold undoes is not a new visit:
   the last estimate and its `count_src` come back with PRESENT (*Breathing
   can hold presence*, above).

`count-reset` starts over. Retrain after moving anything, the same as
enrolment. In the synthetic end-to-end test at 50 Hz, trained and classified
on the device path's blocks exactly as the firmware keeps them (moving = flag
**and** gate, duty ≥ 30 %), 60 of 60 held-out blocks were classified right,
and all 12 five-block majorities. The duty the model learnt averaged 0.76 for
one mover and 0.95 for two. That run's two walkers also disturb nearly
orthogonal directions by construction, which is the easy case. `test_count`'s
own synthetic set scores 18 of 20. Neither is a room result.

### Breathing and heart rate, in six lines

1. While the motion gate says still, 12 sub-carriers' amplitudes are averaged
   into 100 ms bins **by timestamp**, not packet count, because ping replies
   jitter. A bin no packet landed in is filled by a straight line from the
   last bin toward the packet that ends the gap (since 2026-11-03,
   DECISIONS D-175; it used to repeat the last value, which could fake a
   ~50 bpm heartbeat).
2. After `CSI_VITALS_MIN_S` (20 s) of stillness, every 2 s, each channel's
   last ≤ 30 s is detrended, Hann-windowed and FFT'd. One channel per packet
   keeps the soft-float cost bounded.
3. The 12 spectra are normalised to unit power in each band and summed with
   **equal weight**. It is a power sum, so channels in opposite phase cannot
   cancel. Weighting each channel by how clean its own peak was let pure
   noise pass for a breath in ~9 % of analyses (`vitals.h`), so the weighting
   was dropped.
4. **Breathing** is the strongest peak in 6–30 breaths/min, reported if it
   stands ≥ 4× above the band's median and is not on the band edge. So only
   about **7.6–28.7 breaths/min** can actually be reported: a rate outside
   that, or slow drift the detrend missed, puts the peak on an edge bin.
5. **Heart** is the strongest peak in 48–120 beats/min *after cutting out
   breathing harmonics 2–8*. It is reported only if it is ≥ 5× the median,
   breathing is confident, and it is neither on a band edge nor the shoulder
   of a notched harmonic. What you see is the median of the last 5.
6. The gate going MOVING, a packet gap over 1 s, a CSI outage or a
   recalibration throws the buffer away. Presence ending does not. A still
   person whose PRESENT lapsed must keep accumulating the ~20 s that can bring
   it back.

### Heart rate: what the synthetic test shows

`test_e2e` (c) walks a synthetic person in, sits them still for 60 s at
50 Hz, and runs the device path: `main.c`'s front end with the motion gate.
The synthetic chest breathes 15/min, with 2nd and 3rd harmonics, and has a
66 bpm **sine** heartbeat on **white** noise. Amplitudes are in units of the
per-packet noise:

| Breathing | Heart | Heart ÷ breathing | Analyses | Breathing reported / right | Heart reported / right (±3 bpm) |
|---|---|---|---|---|---|
| 0.2 | 0.02 | 1/10 | 20 | 19 / 19 | 0 / 0 |
| 0.4 | 0.04 | 1/10 | 23 | 23 / 23 | 0 / 0 |
| 0.6 | 0.06 | 1/10 | 20 | 20 / 20 | 0 / 0 |
| 1.0 | 0.10 | 1/10 | 20 | 20 / 20 | 0 / 0 |
| 2.0 | 0.20 | 1/10 | 21 | 21 / 21 | 21 / 21 |
| 4.0 | 0.40 | 1/10 | 23 | 23 / 23 | 23 / 23 |
| 4.0 | 0.20 | 1/20 | 20 | 20 / 20 | 5 / 5, not valid at the end |
| 0.6 | 0.12 | 1/5 | 20 | 20 / 20 | 0 / 0 |
| 0.6 | 0.30 | 1/2 | 20 | 20 / 20 | 20 / 20 |
| 0.6 | 0.20 | 1/3 (deliberately generous) | 23 | 23 / 23 | 23 / 23 |

How to read it. A real heartbeat moves the chest 10–50× less than breathing,
so real ratios are 1/10 to 1/50. At 1/10 the heart rate came through only when
breathing was at least 2 noise units. At 1/20 it was reported in a quarter of
the analyses, and 1/50 was never tried. Nobody knows whether a real chest
reaches 2 noise units on your link. Real heartbeats are not sine waves either,
and real noise is not white. What the tests do show: breathing was right in
every row, and breathing with no heartbeat at all never produced a heart rate
(0 of 300 analyses, `test_vitals` (4)). The same rows run through Step 08's
flag instead of the gate never analysed anything at 2.0 and 4.0, because the
flag was on for the whole sit (*Stillness* above).

**Where it does produce a heart rate from nothing (synthetic, 2026-10-30).**
`test_vitals` (9)–(12b) look for false heart rates on purpose. With no
heartbeat, `vitals.c` still reported one when a fluctuation was shared by
every subcarrier (49.8–89.3 bpm in 16 of 100 analyses of a synthetic empty
room; with synthetic breathing plus shared heart-band noise, in 44 of 100), and
when the packet stream had 0.3–0.9 s silences (49.2–56.2 bpm in 23 of 3,997
analyses). The tests printed these as KNOWN LIMIT lines on 2026-11-01; the
shared-fluctuation lines still print. Since 2026-11-03 a
fix for the silences is applied (DECISIONS D-175: the gap is filled by a
straight line instead of a repeated value). It makes that phantom much
rarer, not impossible: 1 wrong value in 332,374 analyses with silences over
52 seed bases, 889 before (docs/TESTING.md §3.3 item 10). The shared
fluctuation is not fixed.

### Validating heart rate — do this before you believe it

**Why.** The best verified ESP32 heart-rate result (Pulse-Fi, 0.46 bpm)
needed a dedicated transmitter, seated subjects, a *trained* neural network
and a pulse-oximeter reference, and its evaluation is optimistic
([`docs/REFERENCES.md`](docs/REFERENCES.md) §3.4). RuView's heartbeat claim
is unsubstantiated for the same reason (same file, §5). A spectral peak from
router pings has none of that behind it, and the synthetic table above shows
how much signal it needs. Worse, noise can also produce a tidy number between
48 and 120. That is why the key is called `hr_bpm_exp`.

**How.**

1. Get a reference that logs to CSV with wall-clock time. A **chest strap**
   (ECG-based) is best; a logging **finger pulse oximeter** is fine.
2. Record both at once:
   ```powershell
   python tools\presence_monitor.py --port COM12 --csv session.csv
   ```
   Every row gets a `host_time` column from this PC's clock. If the
   reference logs on a phone, sync both clocks first.
3. Sit still, alone, for 5+ minutes after the first estimate. Then do it
   again straight after climbing stairs, so the reference actually
   **changes**.
4. Compare:
   ```powershell
   python tools\vitals_validate.py --csi session.csv --ref strap.csv --kind hr
   ```
   It pairs each row with the nearest reference sample (±2 s) and prints
   coverage, MAE, RMSE, bias, % within ±5 bpm and correlation. It also prints
   what an oracle that always answers the reference's median would score.
   Resting heart rate barely moves, so "90 % within 5 bpm" can come from a
   number that ignores you entirely. The script's TRACKING / ACCURACY
   verdicts are there to catch that.

Breathing works the same way: `--kind br --tolerance 2`, against a
respiration belt, or breathe to a metronome and log its rate as the
reference. `--person raj` keeps only the rows whose `vitals_of` is `raj`
(a CSV recorded before `vitals_of` existed is refused: record it again).
Recognition has no script yet. Log with `--csv`, write down who
walked past and when, and count the hits yourself. Include someone who was
**not** enrolled: the right answer for them is `"unknown"`.

### JSON

Step 08's fields come first and are unchanged, so Step 08 tools still work.
The Step 10 keys follow, always in this order and only on `"state":"detect"`
lines. The calibration and `no_csi` lines are exactly Step 08's. New keys are
only ever appended after `dropped`, so no key ever moves; that is why
`vitals_of`, and after it the activity keys and the gain keys, come after the diagnostics. This is the example `main.c`
carries. It fills every key at once for illustration: during a real
enrolment `person` and `vitals_of` would be `null`.

```json
{"t_ms":123456,"state":"detect","present":1,"motion_sd":7.4,"dev_sd":1.2,
 "level":148,"rssi":-46,"rate_hz":49.6,"nsub":52,
 "count":1,"count_src":"presence",
 "person":"raj","person_dist":0.84,"person_votes":"5/7",
 "vitals":"ok","br_bpm":14.8,"br_conf":6.2,"hr_bpm_exp":71.3,"hr_conf":5.4,
 "session":"enroll:raj 12/30",
 "hf":1.07,"proc_ms_max":8.4,"dropped":0,"vitals_of":"raj",
 "activity":"still","activity_s":42,"episode_s":318,
 "agc":36,"fft":-2,"gain_chg":0}
```

| Key | Values |
|---|---|
| `count` | always a number: `0`, `1`, `2` (2 means "two or more") |
| `count_src` | always present: `"presence"` (the `1` only means someone is present) or `"trained"` (a block of *this* visit was classified by the trained model) |
| `person` | `null` (recognition off, nobody present, no slot with ≥ 10 windows, or fewer than 3 windows classified this visit and no name yet — including just after the counter went back from 2 to 1, and after the breathing hold restored a lapsed presence, when `vitals_of` keeps the name), `"unknown"`, `"multiple"` (count 2, which needs a trained counter), or an enrolled name |
| `person_dist` | mean distance of the votes behind `person`, 2 decimals, each vote capped at 999. `null` when there are none, and for `"multiple"` |
| `person_votes` | `"k/n"`: k of the last n (≤ 7) windows back `person`. For `"unknown"`, k windows matched nobody. `null` whenever `person` is `null`, and for `"multiple"` |
| `vitals` | `"off"` (vitals disabled), `"no-person"` (not PRESENT: nothing is reported, although the analysis keeps running), `"multiple"` (PRESENT, but two or more people this visit: `count` 2, two different names, or a `count-train 2` session. `br_bpm`, `br_conf`, `hr_bpm_exp`, `hr_conf` and `vitals_of` are all `null`), `"moving"`, `"settling"`, `"no-signal"` (analysed, no confident breath), `"ok"`. The first that applies wins, in that order |
| `br_bpm` | breaths/min with confident breathing, else `null` |
| `br_conf` | breathing peak / band median whenever an analysis has run (`"ok"` or `"no-signal"`), so it **also appears beside a `null` `br_bpm`**. Below the threshold (4.0 by default) the peak was too weak. At or above it, the peak sat on a band edge — a rate outside about 7.6–28.7/min, or slow drift — which is never reported |
| `hr_bpm_exp` | **experimental** beats/min, only when the heart rate is valid (which requires valid breathing), else `null` |
| `hr_conf` | heart peak / band median, **only together with `hr_bpm_exp`**, else `null`. The firmware withholds some strong-looking peaks (band edges, shoulders of notched harmonics), and a `hr_conf` of 30 beside a `null` rate would read as a heartbeat that went unreported |
| `session` | `null`, `"enroll:<name> k/N"`, `"count-train:1 k/N"`, `"count-train:2 k/N"` |
| `hf` | the motion gate's ratio, 2 decimals: ~1 = nobody moving, ≥ 4.0 (default) = moving. Reported whether or not anyone is PRESENT. `null` until the gate knows its noise floor, and always `null` in a build with vitals, recognition and counting all switched off |
| `proc_ms_max` | diagnostic, always a number: the slowest per-packet processing time since the previous JSON line, in ms |
| `dropped` | diagnostic, always a number: CSI packets lost to a full queue since boot (the pipeline fell behind for longer than the queue covers) |
| `vitals_of` | always present: the enrolled name the vitals are attributed to, else `null`. A name only while PRESENT, `count` ≤ 1 and the last person recognised walking in alone this visit has not been contradicted since (*Whose vitals*, above). It can appear while `vitals` is still `"moving"` or `"settling"`: it says whose the next rates will be. `null` for `"unknown"`, `"multiple"`, nobody recognised yet, during sessions, after a lapse that only Step 08 has ended (until breathing takes over holding PRESENT), and always in a build without both recognition and vitals. A carried-over claim, not a measurement |
| `activity` | **experimental** (2026-10-16): `"empty"`, `"still"`, `"restless"`, `"moving"` (*What each feature does*, above), or `null`: not enough seconds yet, someone PRESENT before the motion gate knows its noise floor, right after a CSI gap, and always without `CSI_ACTIVITY_ENABLE`. No posture |
| `activity_s` | whole seconds in the current `activity`; `null` when `activity` is `null` |
| `episode_s` | whole seconds of the current presence episode; a lapse the breathing hold undoes does not end it (the value stays put during the lapse); `null` with no episode, and always without `CSI_ACTIVITY_ENABLE` |
| `agc` | **diagnostic** (2026-11-03, DECISIONS D-176): the receiver's AGC gain of the last packet since the previous detect line, read from a byte of the packet header that ESP-IDF does not document (Espressif's own gain library reads it). Logging only: nothing the detector decides uses it. Never yet checked on a board: on the first capture it should change with distance and signal strength (`rssi`), and stay nearly constant on a still link; if it never changes, or equals the channel, ignore it. `null` with `CSI_GAIN_LOG` off or no packet since the previous line |
| `fft` | **diagnostic**: the receiver's FFT gain of that same packet (signed), same source and caveats as `agc` |
| `gain_chg` | **diagnostic**: how many packets since the previous detect line had a different `agc` or `fft` from the packet before them (stops at 65535). The first detect line after a calibration counts from the switch to detection; the calibration's own packets are summarised once in a `gain before detection: ...` log line. `null` like `agc` |

`presence_monitor.py` logs all of them, adds `host_time`, and shows
`n=1 who=raj br=15 hr~72 hf=1.07` in its status line. The `~` flags the
experimental value, the same as the firmware's own human-readable line. When
`vitals_of` is set and differs from `person` (after a restore, `who=-`) it
adds `of=raj`, and its event log notes changes of `vitals_of` and when the
vitals switch to `"multiple"`. Since 2026-10-16 it also logs `activity`,
`activity_s` and `episode_s`, and prints `*** <time>  activity: <state>`
when the state changes. Since 2026-11-03 it also writes `agc`, `fft` and
`gain_chg` to the CSV (not shown on screen).

### Kconfig — `menuconfig` → *Recognise / count / vitals*

| Symbol | Default | Meaning |
|---|---|---|
| `CSI_VITALS_ENABLE` | y | breathing + experimental heart rate |
| `CSI_VITALS_WINDOW_S` | 30 | analysis window, s (16–51) |
| `CSI_VITALS_MIN_S` | 20 | still data needed before the first estimate, s |
| `CSI_VITALS_BR_CONF_X100` | 400 | breathing peak / median (×100) needed to report |
| `CSI_VITALS_HR_CONF_X100` | 500 | heart peak / median (×100) needed to report |
| `CSI_VITALS_HOLDS_PRESENCE` | y | confident breathing keeps a still person PRESENT (never starts it). Needs vitals |
| `CSI_VITALS_MOTION_RATIO_X100` | 400 | motion gate: `hf` at or above this (×100) counts as moving (150–5000). Needs vitals; otherwise the gate uses 4.0 |
| `CSI_ID_ENABLE` | y | gait-based recognition of enrolled people |
| `CSI_ID_ENROLL_WINDOWS` | 30 | gait windows collected per enrolment |
| `CSI_ID_MIN_WINDOWS` | 10 | windows a slot needs before it is used |
| `CSI_ID_REJECT_X100` | 250 | distance (×100) above which a window is "unknown" |
| `CSI_ID_MARGIN_X100` | 115 | runner-up / best distance (×100) required |
| `CSI_VITALS_LOG_SAVE_S` | 300 | at least this many seconds between flash writes of the per-person vitals log (60–86400). Up to this much log is lost on a power cut. Has no effect unless recognition and vitals are both on |
| `CSI_COUNT_ENABLE` | y | 1 vs 2+ occupancy estimate |
| `CSI_COUNT_TRAIN_WINDOWS` | 20 | blocks collected per count-training class |
| `CSI_CONSOLE_ENABLE` | y | the serial command console |
| `CSI_EXPLAIN` | y | `EXPLAIN` lines for the dashboard and a plain-English `why:` log line per decision (*See how it decides*) |
| `CSI_EXPLAIN_AMP_HZ` | 5 | amplitude lines per second for the dashboard's CSI waterfall (0–20; 0 = off) |
| `CSI_ACTIVITY_ENABLE` | y | experimental activity state and presence episodes (`activity`, `activity_s`, `episode_s`, console `stats`). Needs vitals, recognition or counting (it reads the motion gate) |
| `CSI_ACTIVITY_RESTLESS_X100` | 250 | a second whose mean `hf` is at least this (×100) but below the motion gate's threshold counts as fidgeting (100–5000). Synthetic starting point: a seated, breathing chest reached `hf` 2.33 in a host test |

Plus `CSI_PING_INTERVAL_MS` in *WiFi* (see the packet-rate note above), and
`CSI_GAIN_LOG` (y) in *CSI capture*: log the receiver gain on each detect
JSON line (`agc`, `fft`, `gain_chg`, the JSON table above; logging only,
2026-11-03). The
models and the vitals log live in NVS next to the baseline (`id_model`,
`cnt_model`, `vit_log`). A plain `flash` keeps them, and `erase-flash`
deletes them along with the calibration.

**Cost.** The C5 has **no FPU**, so every float operation here is emulated
in software. The modules are written around that: float only, no per-sample
`sinf`/`expf`, and FFT work spread across packets rather than done all at
once. Their state is static: vitals ~30 KB, gait ~11 KB, count < 1 KB, and the
gate 624 bytes (30,984, 11,536 and 624 bytes in the linker map). The
activity state (2026-10-16) is 216 bytes (`.bss.s_act` in the v6.1 linker
map) and adds a few integer adds per packet and one short loop per second
(its header's estimate). The
per-person vitals state adds roughly 0.3 KB, a few integer compares per
packet and ~10 float operations per completed analysis (its header's
estimate; not yet read from a linker map). A vitals-log save is an NVS write
of ~170 bytes inside the pipeline task, at most once every
`CSI_VITALS_LOG_SAVE_S` or on `forget` / `vitals-reset`. The periodic save
runs outside the per-packet timing, so it does not show in `proc_ms_max`: a
save slower than 50 ms is logged instead, and one that outlasts the queue
shows up in `dropped`. The CSI queue
grew from Step 08's 10 slots to 16 (+4.7 KB of heap), which absorbs 320 ms of
stall at 50 Hz and 160 ms at 100 Hz. That works because the ping task runs
**above** the pipeline (priority 5 against 4; Step 08 uses 2). `esp_ping` keeps
one echo in flight, so a ping task below the pipeline would simply stop
sending during a stall: the stream would pause instead of queueing, and
`dropped` could never count an overload. The CPU
figures in the headers are **estimates, not measurements**: ~6–10 ms per
vitals FFT (12 FFTs every 2 s, one per packet), and ~12–32 ms per gait window
(one every 128 packets; a host count of operations times an assumed 50–100
cycles each). Counting keeps running sums per packet and runs two power
iterations every 256 packets. The gate adds ~40 operations per packet. The worst packet, one that gets a gait
window, a count block and a vitals FFT together, has never been timed on the
chip.

**The firmware now measures that for you. Read it on the first flash:**
`proc_ms_max` and `dropped` in the JSON, and the `pipeline:` line of
`status`, which reports the slowest packet since boot. If packets are
dropped, a warning follows within a minute:

```
W (…) csi-human: 12 CSI packets dropped in the last minute (12 since boot) - the pipeline fell behind its 16-packet queue; slowest packet 41.3 ms
```

`status` also prints the live packet rate next to the calibrated one. While
detecting, the firmware compares the two every 10 s and warns once when they
differ by more than 20 % (and again when the rate is back), because gait and
count frequencies assume the calibrated rate:

```
W (…) csi-human: live CSI rate 71.4/s is more than 20% off the calibrated 99.1/s (0 dropped since boot) - gait and count assume the calibrated rate: check the router, or 'recal' if the ping interval changed
```

With `dropped` at 0 that points at the router answering pings slower; with
`dropped` climbing, at the pipeline. If `dropped` keeps climbing, go back to
a 20 ms ping or switch a Step 10 feature off. Free heap is printed at
start-up (`free heap after start-up:`) and by `status`. It has never been
measured either.

### Host tests

```powershell
powershell -ExecutionPolicy Bypass -File step10_recognise_count_vitals\test\run_tests.ps1
powershell -ExecutionPolicy Bypass -File step10_recognise_count_vitals\test\run_tests.ps1 -Only vitals
```

These need Visual Studio 2022 with the C++ workload. They compile the nine
modules with MSVC `/std:c11 /W4 /WX` (a warning is a failure) and run ten
suites against synthetic inputs with known answers: `dsp`, `ncc`, `vitals`,
`gait`, `count`, `gate`, `pvitals` (the `vitals_of` latch, the log's episode
and mean rules, and rejection of a damaged `vit_log` blob), `activity` (the
activity state's rules on scripted decisions; added 2026-10-16), `gain`
(which header bytes `csi_gain.c` reads and how it counts gain changes;
added 2026-11-03), and `e2e`, which drives all of them the way `main.c`
does (it has no gain step: nothing reads the gain). On Ubuntu,
`bash step10_recognise_count_vitals/test/run_tests.sh` runs the same suites
with gcc `-Werror` (DECISIONS D-171). As of 2026-11-03 the ten-suite set
has run under gcc only; the MSVC runner has not been re-run since the
`activity` suite was added (2026-10-16), nor since `gain` (2026-11-03).
The current results are in *Verification status* at the top. A pass means the
arithmetic is right. It does not mean any of it works on a person.

---

## Where to go next

* **Two boards** — a second ESP32 as a dedicated CSI sender (ESP-NOW at a fixed
  rate) removes the dependency on router behaviour entirely and gives a much
  cleaner signal. The cleanest upgrade available.
  *(Instrument the received CSI rate from day one — see
  [`docs/REFERENCES.md`](docs/REFERENCES.md) §1.5.)*
* **Validate Step 10 on a real person.** This is the most valuable next hour.
  Check heart rate against a chest strap with `tools\vitals_validate.py`, and
  recognition with two enrolled people plus a written log of who walked past.
  Breathing, the old entry on this list, is now implemented in Step 10. It is
  just as unvalidated.
* **Multi-link counting with the second board.** The second C5 (on COM13 on
  this machine) can be a fixed transmitter, which gives a second link and a
  steady 100 Hz for the gait features. Treat two links as a stepping stone:
  the counting result worth having (0–5 people, 89.8 % within ±1) used
  **four** crossing links
  ([`docs/REFERENCES.md`](docs/REFERENCES.md) §3.4).
* **Identity from the phone or watch someone carries (BLE).** This is the
  practical route to *who*, and it works while people sit still, which gait
  recognition never will. The C5 has Bluetooth LE, but it **time-shares the
  one RF path with Wi-Fi** (`docs/REFERENCES.md` §4.1). Every millisecond spent scanning is a
  millisecond of CSI not received, so expect a lower CSI rate and measure it
  before relying on both.
* **Human vs pet / robot vacuum.** A large deployment cut non-human false
  alarms from 63.1 % to 8.4 % with gait and speed features
  (`docs/REFERENCES.md` §3.4). Step 10's
  gait features are a starting point; it does not do this today.
* **Direction of movement** — correlate which subcarriers change first.
* **Web dashboard** — add `esp_http_server` and serve the JSON over Wi-Fi.
* **MQTT / Home Assistant** — publish the JSON to a broker.

## References

**→ [`docs/REFERENCES.md`](docs/REFERENCES.md) is the full annotated list** —
papers, Espressif components, third-party implementations, datasets and video,
each tagged *C5-ready* / *C5-measured* / *algorithm-only* / *background*, plus a
ranked list of what to change in this repo first. Last swept **2026-09-07**;
the identity / counting / vital-signs literature behind Step 10 (§3.4) was
added **2026-09-24**.

**→ [`docs/COMPARISON.md`](docs/COMPARISON.md)** (2026-09-25) compares this
project with existing open-source projects, papers, commercial products and
the relevant accuracy standards, task by task. Every figure in it was
re-checked against its source.

The essentials, inline:

* **ESP-IDF Wi-Fi CSI guide — read the `esp32c5` page, not the generic one:**
  [`.../esp32c5/api-guides/wifi-driver/wifi-vendor-features.html`](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c5/api-guides/wifi-driver/wifi-vendor-features.html)
  → *Wi-Fi Channel State Information*. The per-target page **does** carry a
  C5 sub-carrier/byte-count table (106–490 bytes); its column *labels* still use
  the classic vocabulary (`sig_mode`, `cwb`, `stbc`) that does not exist on C5.
  `docs/REFERENCES.md` §1.1 turns it into the six `csi_data_len` values Step 04
  should actually print.
* C5 CSI structs — `components/esp_wifi/include/esp_wifi_he_types.h`
  (`CONFIG_SOC_WIFI_MAC_VERSION_NUM == 3` branch)
* `components/esp_wifi/include/local/esp_wifi_types_native.h`
* `components/soc/esp32c5/include/soc/soc_caps.h` — `SOC_WIFI_CSI_SUPPORT`
* Espressif's [esp-csi](https://github.com/espressif/esp-csi) — **this now
  targets the C5 directly**, not just older chips. Its README, the
  `get-started/csi_recv_router` example (router-ping, the design this repo
  mirrors) and the newer `esp-radar/wifi_sensing_demo` all list ESP32-C5. The
  one example that still fails to build on C5 is `esp-radar/console_test` —
  [esp-csi #241](https://github.com/espressif/esp-csi/issues/241), open, and its
  error list is field-for-field the incompatibility table above.
* Espressif's own C5-capable sensing components —
  [`esp_wifi_sensing`](https://components.espressif.com/components/espressif/esp_wifi_sensing)
  (router ping, dynamic baseline, hysteresis, on-site training) on top of
  [`esp-radar`](https://components.espressif.com/components/espressif/esp-radar).
  Both list `esp32c5` and require ESP-IDF ≥ 5.4. The nearest thing to a
  reference implementation of this project — see `docs/REFERENCES.md` §2.

---

*Built and compile-verified against ESP-IDF v6.0.2, target `esp32c5`.
Not yet tested on hardware — see the verification note at the top.*
