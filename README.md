# ESP32-C5 Wi-Fi CSI Human Detector — build it step by step

A complete, open-source human presence/motion detector built on **Wi-Fi Channel
State Information (CSI)** from a single **ESP32-C5**. No camera, no PIR, no
extra sensor — just the radio that is already in the chip.

Eight small projects. Each one builds, flashes, and does one new thing. By
Step 08 you have a finished detector that calibrates itself and reports
`HUMAN DETECTED` over serial, JSON, and an optional LED.

---

## Read this before you start — what this actually does

**Yes, this is possible on the ESP32-C5, and this repo does it.** But be clear
about what "recognise the human" means with one antenna:

| | |
|---|---|
| ✅ **Detects a human who moves** | walking, gesturing, shifting weight, standing up, reaching. This is what single-antenna CSI genuinely delivers, and it works well. |
| ⚠️ **Motionless person: unreliable** | there is a secondary "static presence" branch that helps, but do not build a safety system on it. |
| ❌ **Cannot identify *which* human** | identity recognition is not achievable from single-antenna CSI amplitude. Anyone claiming otherwise on this hardware is overselling it. |
| ⚠️ **Not human-specific** | a large dog, a ceiling fan, or curtains in a draught will also trigger it. It senses changes in radio propagation, not people. |
| ⚠️ **Needs recalibration** | after you move the board, move furniture, or change the Wi-Fi channel. |

### Verification status — please read

* **Compile-verified.** All eight steps *and* the root project were
  clean-rebuilt (`build/` and `sdkconfig` deleted first) with
  `idf.py set-target esp32c5 build` on **ESP-IDF v6.0.2**, and every one
  produced a binary with no errors. The final firmware is 0xe0e40 bytes,
  56% of its partition free.
* **NOT tested on hardware.** No ESP32-C5 has been connected at any point.
  Runtime behaviour, the real CSI packet rate, the actual subcarrier counts,
  and detection accuracy are all unverified on silicon.
* Treat the numeric defaults (thresholds, timings) as **starting points to
  tune**, not validated values. Step 04 exists precisely so you measure your
  own numbers instead of trusting anyone's constants.
* The code also builds on **v5.5.1** thanks to the `ESP_IDF_VERSION_MAJOR`
  shim, but v6.0.2 is what the project is pinned to and verified against —
  see *Everything is on ESP-IDF v6.0.2* below for why.

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
| `D:\ESP32C5_CSI\ESP32C5_CSI.code-workspace` | all 8 steps as folders; pick one and build it |
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
> `.code-workspace` therefore lists only the eight step folders, never the
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
| **Tools: Presence monitor + plot** | Live tuning plot for Step 08 |
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
| `.vscode/settings.json` (root + all 8 steps) | mirrors the extension's own values exactly |

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

The eight step folders remain independent projects — use those to walk through
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

* **Verified:** all 8 steps and the root project clean-rebuild on ESP-IDF
  v6.0.2; the exact command line each Task runs does build; every
  `.vscode` JSON file parses; every ESP-IDF path in every `settings.json`
  exists on disk; `compile_commands.json` exists for all eight steps so
  clangd has a database.
* **Not verified:** I cannot launch Antigravity, so the extension's own
  status-bar buttons and the Task dropdowns are untested in the running IDE.
  And no ESP32-C5 has ever been connected, so nothing here has run on
  silicon.

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
* **Baseline validation** — if the CSI shape no longer matches what was
  calibrated (channel changed, router changed, `CSI_LLTF_ONLY` toggled), it
  detects that and recalibrates instead of producing nonsense
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

---

## How the code is organised

Every step is a **complete, standalone ESP-IDF project**. The Wi-Fi bring-up is
deliberately duplicated across them rather than factored into a shared file —
that keeps the copy-one-folder-and-flash property that makes a stepwise
tutorial work. In each `main.c` the new logic is at the top; the boilerplate
sits below a `/* ---- unchanged from Step 0N ---- */` banner.

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
├── docs\
│   └── REFERENCES.md                annotated reference list
└── tools\
    ├── csi_monitor.py               live waterfall plot (Step 05)
    └── presence_monitor.py          JSON reader / plotter / logger (Step 08)
```

Shared calibration record: `main/csi_baseline.h` (Steps 06–08) — a ~2 KB struct
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

All nine projects set `CONFIG_COMPILER_OPTIMIZATION_PERF` (-O2),
`CONFIG_FREERTOS_HZ=1000` and `CONFIG_ESPTOOLPY_FLASHMODE_QIO` in their
`sdkconfig.defaults`, with the reasoning written inline in those files.
Assertions are deliberately left **on** until something has actually run on
hardware. Full rationale, including two settings that were tried and reverted,
is in [`docs/REFERENCES.md`](docs/REFERENCES.md) §7.

## Where to go next

* **Two boards** — a second ESP32 as a dedicated CSI sender (ESP-NOW at a fixed
  rate) removes the dependency on router behaviour entirely and gives a much
  cleaner signal. The cleanest upgrade available.
  *(Instrument the received CSI rate from day one — see
  [`docs/REFERENCES.md`](docs/REFERENCES.md) §1.5.)*
* **Breathing detection** — band-pass the per-subcarrier amplitude around
  0.15–0.5 Hz. Works in ideal conditions; unreliable in general.
* **Direction of movement** — correlate which subcarriers change first.
* **Web dashboard** — add `esp_http_server` and serve the JSON over Wi-Fi.
* **MQTT / Home Assistant** — publish the JSON to a broker.

## References

**→ [`docs/REFERENCES.md`](docs/REFERENCES.md) is the full annotated list** —
papers, Espressif components, third-party implementations, datasets and video,
each tagged *C5-ready* / *C5-measured* / *algorithm-only* / *background*, plus a
ranked list of what to change in this repo first. Last swept **2026-09-07**.

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
