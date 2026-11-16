# References for the ESP32-C5 CSI Human Detector

**Reference sweep: 2026-09-07.** Everything below was opened and read on that
date. Nothing is quoted from memory — if a version number, byte count or rate
appears here, it appeared verbatim on the page cited next to it.

**Addendum sweep: 2026-09-24** — §3.4 (identity, counting, vital signs: the
literature Step 10 is held to) and the RF-coexistence entry in §4.1, same
rule. Where only an abstract could be read, the entry says so; where a figure
circulating about a paper could not be found in the paper, the entry says that
too, rather than repeating it.

Every entry carries a tag, because on this chip the difference matters more
than the topic does:

| Tag | Means |
|---|---|
| 🟢 **C5-ready** | Declares ESP32-C5 support against the MAC-v3 CSI API. Usable as-is. |
| 📐 **C5 measurements** | Reports *numbers measured on real C5/C6 silicon*. Highest value here, because nothing in this repo has run on hardware yet. |
| 🟡 **Algorithm only** | Good ideas, classic-ESP32 or non-ESP API. Will **not** compile on C5 — port the maths, not the code. |
| ⚪ **Background** | Standards, datasets, platform context. |

> The single biggest change since this repo was written: **Espressif now ships
> its own C5-capable Wi-Fi sensing stack** (`esp_wifi_sensing` + `esp-radar`),
> and it implements almost exactly this project's architecture — router ping,
> per-link baseline, jitter metric, hysteresis, on-site training. See §2.

---

## 1. Numbers measured on real ESP32-C5 silicon 📐

Read this section before flashing Step 04. It tells you what the geometry probe
*should* print, so a wrong answer is recognisable as wrong.

### 1.1 The C5-specific sub-carrier table now exists in ESP-IDF docs 🟢📐

<https://docs.espressif.com/projects/esp-idf/en/latest/esp32c5/api-guides/wifi-driver/wifi-vendor-features.html>
→ *Wi-Fi Channel State Information*

The README says the ESP-IDF CSI guide is written for classic ESP32. That is now
half-right: **the per-target `esp32c5` page carries its own sub-carrier table**,
with total byte counts spanning **106–490 bytes** — but its *columns* are still
labelled with the classic vocabulary (*signal mode*, *STBC*, *secondary
channel*) even though C5 code reads `rx_ctrl.cur_bb_format`. So the table is
usable and the field names around it are not.

> ⚠️ **Open the page; do not trust a reproduction of the big table.** Its rows
> came through this sweep with the columns visibly scrambled (a "non HT" row
> carrying an HE-LTF index range, for example). The *byte totals* it lists are
> internally consistent and are reproduced below; the row labels are not.

Its PHY sub-carrier table, which did come through cleanly:

| PHY standard | Sub-carrier range | Invalid sub-carrier | Sub-carrier (total/data) |
|---|---|---|---|
| 802.11a/g | -26 to +26 | 0 | 53 total, 52 usable |
| 802.11n, 20 MHz | -28 to +28 | 0 | 57 total, 56 usable |
| 802.11n, 40 MHz | -58 to +58 | -1, 0, 1 | 117 total, 114 usable |
| 802.11ac, 20 MHz | -28 to +28 | 0 | 57 total, 56 usable |
| 802.11ax, 20 MHz (SU) | -122 to +122 | -1, 0, 1 | 245 total, 242 usable |

Since CSI is one signed byte each for I and Q, **`csi_data_len` = 2 × sub-carrier
count** — and a two-stream (STBC) frame carries two LTF blocks, so it doubles
again. Every byte total printed in the ESP-IDF table falls out of that:

| What the router sent | LTF blocks × sub-carriers | Expected `csi_data_len` |
|---|---|---|
| Legacy L-LTF, 20 MHz | 1 × 53 | **106** |
| 802.11n HT-LTF, HT20 | 1 × 57 | **114** |
| HT20, two streams (STBC) | 2 × 57 | **228** |
| 802.11n HT-LTF, HT40 | 1 × 117 | **234** |
| HT40, two streams (STBC) | 2 × 117 | **468** |
| 802.11ax HE-LTF, 20 MHz SU | 1 × 245 | **490** |

**All six are legitimate.** In particular, Step 03/04 set
`acquire_csi_he_stbc_mode = 2` and `acquire_csi_su = acquire_csi_mu = 1`, so
STBC and HE frames *will* be acquired — **228 and 468 are expected outputs, not
symptoms of a broken board.** A histogram spread across several of these means
the router is mixing PHY formats packet to packet, which is exactly the case
`CSI_LLTF_ONLY` exists for. A value outside the set is the one that should
worry you.

The same page confirms the call order this repo already uses
(`CONFIG_ESP_WIFI_CSI_ENABLED` → `esp_wifi_set_csi_rx_cb()` →
`esp_wifi_set_csi_config()` → `esp_wifi_set_csi()`) and states the constraint
that shapes the whole design: *"The CSI receiving callback function runs from
Wi-Fi task. So, do not do lengthy operations in the callback function."*

### 1.2 esp-csi-rs — independently measured C5 rates and lengths 📐

<https://github.com/csi-rs/esp-csi-rs> · docs <https://csi-rs.github.io/esp-csi-rs/> · Apache-2.0

A Rust `no-std` CSI driver, early development, but the only third-party source
found that publishes **measured C5 numbers**:

* Sub-carriers: *"typically ~56 for HT20 HT-LTF or ~53 for legacy 20 MHz L-LTF"*;
  HT40 *"roughly twice … typically ~117–128 (`csi_data_len / 2`)"* — independently
  corroborates §1.1.
* Rate, raw injection on C5/C6: *"roughly 90 CSI reports per second at a 10 ms
  period, measured at a paired collector."*
* Rate, station mode: *"sustains ~290 reports/s as a station."*
* Raw-TX emitter *"verified on the ESP32-C5 and ESP32-C6"* (does not work on S3).
* Dual-band gotcha worth knowing: *"On the dual-band C5 the band follows the
  primary channel number automatically (`>= 36` selects 5 GHz)."*

**Why this matters here:** ~90–290 CSI/s brackets what Step 05's rate counter
should show. If Step 03 reports single-digit packets per second, the ping
interval is the problem, not the CSI config.

Tracks `esp-hal` 1.1 / `esp-radio` 0.18 / `esp-rtos` 0.3, Rust edition 2024.

### 1.3 The C5 5 GHz static-CSI bug is now closed 🟢📐

**esp-idf #18493 (IDFGH-17558)** — <https://github.com/espressif/esp-idf/issues/18493>
**Status: closed, "Resolution: Done".** *(Previously tracked here as an open hazard.)*

Reported against **v5.5.4** on an ESP32-C5-WROOM-1U rev v1.0: on **5 GHz ch 36**
the `buf` IQ data was identical across frames while metadata updated normally —
*12,000 frames yielded 2 unique IQ patterns*. 2.4 GHz was unaffected. Setting
`acquire_csi_force_lltf = 1` (the C61 remedy) **had no effect on C5**.

**How to apply:** closed internally, but no public fix note or "fixed in
vX.Y" statement is visible on the issue. This project locks to 2.4 GHz anyway.
Before ever moving to 5 GHz, re-run Step 04 and check that IQ actually varies —
do not assume the closure covers your IDF build.

### 1.4 The `acquire_csi_force_lltf` escape hatch, explained 📐

**esp-idf #18118** — <https://github.com/espressif/esp-idf/issues/18118> — closed, "Resolution: Done".

CSI broken for 802.11g packets on **ESP32-C61**: the callback delivered
nonsensical, unchanging data. Setting `acquire_csi_force_lltf = true` fixes it,
at the cost of demodulating L-LTF for packets that would normally carry HT-LTF.
Reporter traced it to a specific `esp-phy-lib` commit.

**How to apply:** this is exactly what `CONFIG_CSI_LLTF_ONLY` does in
`step05_amplitude/main/main.c:320`. Independent confirmation that forcing L-LTF
is the right first move when CSI looks frozen or nonsensical — try it before
suspecting your own code.

### 1.5 One ESP-NOW implementation measured 8 pkt/s — measure yours 📐

**PeterkoCZ91/esphome-wifi-csi** (§2.7) reports pairwise packet rates of
**~8 pkt/s over ESP-NOW on an ESP32-C6** versus **206.5 pkt/s over UDP** from
the same rig.

**Read this as a claim about that implementation, not about ESP-NOW.**
Espressif's own `csi_send`/`csi_recv` pair is built *around* ESP-NOW with a
pinned channel and a pinned PHY rate (`esp_now_set_peer_rate_config()`), and
esp-csi-rs measured ~90 CSI/s from raw injection on C5/C6 (§1.2) — so a
correctly configured ESP-NOW sender is clearly capable of far more than 8 Hz.
The likely difference is send pacing and per-packet ack behaviour in that
particular fork.

**How to apply:** the README's *"Where to go next"* is right that a second
ESP32 is the cleanest upgrade — a fixed, known transmitter beats router
behaviour. Just **instrument the received CSI rate on day one** rather than
assuming it, and start from Espressif's `csi_send` configuration rather than a
hand-rolled ESP-NOW loop. A clean signal at 8 Hz is worse for motion detection
than a noisier one at 200 Hz.

---

## 2. Espressif's own C5-capable sensing stack 🟢

This did not exist in a usable form when the repo was written. It is now the
most useful thing on this list, for two reasons: it is a **benchmark** to
measure Step 08 against, and its **tuning knobs are a shortlist of the
parameters that turned out to matter** in a shipped implementation.

### 2.1 `espressif/esp_wifi_sensing` — v0.1.1~2 🟢

<https://components.espressif.com/components/espressif/esp_wifi_sensing>

* Requires **ESP-IDF >= 5.4**; depends on **`esp-radar >= 0.3.3`**.
* Targets: **ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C5, ESP32-C6, ESP32-C61.**
* Described as *"a Wi-Fi sensing FSM component built on CSI data"* — motion
  detection with *"dynamic baseline with built-in smoothing, adaptive noise
  estimation, hysteresis"*, presence via waveform analysis, on-site calibration
  training, ACTIVE/INACTIVE event callbacks.
* **Link keep-alive:** *"keeps the CSI sampling path active via an internal
  ping, suitable for router-based scenarios"* — i.e. Espressif independently
  arrived at this repo's Step 02 ping design.

API surface (useful as a checklist of what a complete implementation needs):

```
esp_wifi_sensing_fsm_create / _delete
esp_wifi_sensing_fsm_add_channel / _remove_channel      # one FSM, many peer BSSIDs
esp_wifi_sensing_fsm_update_data / _get_state / _get_channel_diag
esp_wifi_sensing_fsm_set_channel_config / _get_channel_config
esp_wifi_sensing_fsm_register_event_cb / _unregister_event_cb
esp_wifi_sensing_fsm_train_start / _train_stop / _train_remove
esp_wifi_sensing_fsm_ping_router_start / _stop
esp_wifi_sensing_fsm_set_ping_frequency_hz / _get_ping_frequency_hz
esp_wifi_sensing_fsm_set_amplitude_log_enabled
```

Tuning knobs, with their documented meanings — **compare these against this
repo's `CSI_MOTION_SIGMA_X100` / `CSI_SELECT_MIN_AMP`:**

| Field | Documented as |
|---|---|
| `sensitivity` | motion detection sensitivity, recommended range (0, 1] |
| `presence_sensitivity` | presence detection sensitivity in [0, 1] |
| `active_jitter_min` | minimum raw jitter required to enter or stay in ACTIVE |
| `active_filter_ms` | minimum hold time after entering ACTIVE |
| `max_channel_num` | 16 (default) |
| `raw_buf_size` | 20 (default) |
| `polling_interval` | 20 ms (default) |
| `ping_frequency_hz` | router-ping rate (tunable) |

Note `active_filter_ms` — a minimum *hold* time after entering ACTIVE. That is
the anti-flicker mechanism Step 07/08 needs and is the cheapest single
improvement to copy.

**✅ Verified on v6.0.2, 2026-09-07 — see `step09_espressif_sensing/`.** This was
an open question in the first draft of this document; it has been answered by
building it. The component manager resolves and the firmware links against the
ESP-IDF **v6.0.2** this repo is pinned to. Resolved versions:

| Component | Version pulled |
|---|---|
| `espressif/esp_wifi_sensing` | **0.1.1~2** |
| `espressif/esp-radar` | **0.3.4** |
| `espressif/esp_csi_gain_ctrl` | 0.1.5 |
| `espressif/esp_radar_motion_dec` | 0.1.1 |
| `espressif/cmake_utilities` | 0.5.3 |

⚠️ **One real defect found: `esp-radar` 0.3.4 does not compile at `-O2`.**
Fifteen instances of:

```
esp_radar.c:416: error: dereferencing type-punned pointer will break
                 strict-aliasing rules [-Werror=strict-aliasing]
```

GCC only runs strict-aliasing analysis at `-O2` and above, so this never fires
in Espressif's own default `-Og` builds — but this repo now builds at `-O2`
(§7). The code genuinely type-puns: `esp_radar.c` reads the integer field
`rx_ctrl->he_siga1` through `(esp_wifi_htsig_t *)`, `(esp_wifi_vht_siga1_t *)`
and friends, **to reconstruct the classic `mcs` / `cwb` / `stbc` values that do
not exist as struct fields on a MAC-version-3 part.** It is this project's
README incompatibility, seen from Espressif's side of the fence.

The fix is two lines in `step09_espressif_sensing/CMakeLists.txt`, scoped to
that one component:

```cmake
idf_component_get_property(esp_radar_lib espressif__esp-radar COMPONENT_LIB)
target_compile_options(${esp_radar_lib} PRIVATE -fno-strict-aliasing)
```

`-fno-strict-aliasing`, not `-Wno-error=strict-aliasing`: at `-O2` the optimiser
is otherwise entitled to assume those accesses cannot alias and may reorder or
discard them, so silencing the diagnostic alone would leave a genuine
miscompilation risk.

### 2.2 `espressif/esp-radar` — v0.3.4 🟢

<https://components.espressif.com/components/espressif/esp-radar>

Requires **ESP-IDF ">= 5.4"**. Targets, verbatim:
`esp32, esp32s2, esp32s3, esp32c3, esp32c5, esp32c6, esp32c61`.
Produces **`waveform_wander`** and **`waveform_jitter`** — two metrics where
this repo has one. Worth reading as a hint that slow drift and fast jitter want
separating.

### 2.3 `esp-csi` → `examples/esp-radar/wifi_sensing_demo` 🟢

<https://github.com/espressif/esp-csi>

Single board + router — the same topology as this project. Monitors *"the
connected AP plus two fixed peer MAC addresses"*. Built on `esp_wifi_sensing`,
with on-site training, LED feedback, and a **browser-based Web Serial monitor**
for live diagnostics (a good model for replacing `tools/presence_monitor.py`).

Its live diagnostic line names the internal state directly:
`jitter_value, smooth_scaled, enter_level_scaled, exit_level_scaled, state, init_stage`
— note **separate enter and exit levels**, i.e. explicit hysteresis. Console
command `HMSCMD RESET_BASELINE` recalibrates.

### 2.4 `esp-csi` → `examples/get-started/csi_recv_router` 🟢

Targets *"ESP32/ESP32-S2/ESP32-C3/ESP32-S3/ESP32-C5/ESP32-C6/ESP32-C61 SoC"*.
Works by triggering *"the router to send packets through the Ping command to
obtain the CSI data between the device and the router"* — the design this
repo's Steps 02–03 mirror. The `csi_send`/`csi_recv` pair in the same folder is
the router-free two-board variant.

### 2.5 The C5 incompatibility, confirmed on Espressif's own tracker ⚠️

[**esp-csi #241**](https://github.com/espressif/esp-csi/issues/241) — **open**,
filed 9 Nov 2025 against IDF v5.3.1 — reports `examples/esp-radar/console_test`
failing to compile on ESP32-C5 with missing `shift, channel_filter_en, lltf_en,
htltf_en, stbc_htltf2_en, ltf_merge_en, manu_scale, sig_mode, mcs, cwb,
smoothing, not_sounding, aggregation, stbc, fec_coding, sgi, ampdu_cnt,
secondary_channel, ant`.

That is field-for-field the incompatibility table in this repo's README.
**Espressif's own tracker confirms the premise of this project.** Practical
consequence: prefer `wifi_sensing_demo` (the new `esp_wifi_sensing` stack) over
`console_test` (the old direct-esp-radar example) on C5.

### 2.6 ESPectre — the most complete third-party C5 implementation 🟢

<https://github.com/francescopace/espectre> · <https://espectre.dev/> · GPLv3 (+ commercial)

Targets, verbatim: *"ESP32-C6, ESP32-C5, ESP32-C3, ESP32-S3, ESP32-S2, and
classic ESP32."* Requires a 2.4 GHz 802.11n network. Frontends: **ESPHome,
Native, Matter occupancy sensor, MQTT with Home Assistant discovery, Direct
HTTP API**, plus a MicroPython "Micro-ESPectre".

`docs/ALGORITHMS.md` — its lightweight detector, worth contrasting with Step 07:

* Score = weighted logistic fusion, not a bare threshold:
  `logit = b + w_ac * z(turb_autocorr) + w_iqr * z(turb_iqr_over_mean_aggr)`,
  through a sigmoid. Features are the **lag-1 autocorrelation** of a
  gain-invariant turbulence stream and its **robust relative IQR**.
* **Fixed 12 sub-carriers: `[4, 8, 13, 18, 23, 28, 36, 41, 46, 51, 56, 60]`**,
  with the rationale spelled out: *"motion perturbation stays coherent over
  about 10 subcarriers while quiet noise is nearly independent per tone, so
  spreading the selected tones across the band provides independent
  observations."* → **This is a direct critique of any selection rule that can
  pick adjacent sub-carriers.** Check what Step 06's mask actually selects.
* Detector window **1000 ms** (100 packets at a 100 pps target); minimum valid
  occupancy **70%**; first **64** ready evaluations count as startup evidence.
* **Hampel filter on by default: window 7, threshold 5.0 MAD.** Optional
  first-order Butterworth IIR low-pass, off by default.

`docs/TUNING.md` — defaults, and the false-positive playbook this repo lacks:

| Parameter | Default |
|---|---|
| `detection algorithm` | `lightweight` (vs `high_accuracy` pre-trained model) |
| `evaluation_interval_ms` | 250 |
| `motion_on_hits` / `motion_off_hits` | 4 / 3 |
| `hampel_enabled` / `_window` / `_threshold` | true / 7 / 5.0 |
| `lowpass_enabled` / `lowpass_cutoff` | false / 11.0 |
| `csi_target_pps` | 100 |
| `segmentation_window_size_ms` | 1000 |

Its ordered procedure for fans / curtains / pets: verify occupancy and
placement → raise threshold → raise `motion_on_hits` to reject brief bursts →
enable/tune the low-pass → recalibrate in a quiet room. Placement advice:
*"roughly 3–8 m from the access point"*, out of metal enclosures, behind as few
heavy obstacles as practical.

The MicroPython "Micro-ESPectre" reportedly documents a different pair — **MVS**
(Moving Variance Segmentation) and **NBVI** (Normalized Band Variance Index,
selecting 12 non-consecutive sub-carriers from a baseline of 10 × window_size,
default 1000 packets), claiming >90% recall and <15% false-positive rate.
*Per a search summary only — the file itself returned 404 on this sweep and was
not opened. Treat both the algorithm description and the figures as unverified.*

### 2.7 PeterkoCZ91/esphome-wifi-csi — an ESPectre fork with C5 hardware 🟢📐

<https://github.com/PeterkoCZ91/esphome-wifi-csi> · GPLv3

Lists **FireBeetle ESP32-C5 (2.4/5 GHz, simultaneous TX+RX capable)** among its
modules — one of very few projects naming a specific C5 board it runs on. Two
CSI sources: **AP-link** (CSI from the router's own periodic **beacon frames**,
no ping needed) and **pairwise** (a dedicated TX node broadcasting probes at
200 pkt/s, which bounds detection to the direct path).

Publishes: movement score, **breathing rate** (DFT over a 0.08–0.6 Hz bandpass),
breathing score, phase turbulence (SD of inter-sub-carrier phase differences),
presence (hysteresis), motion, packet rate, RSSI, plus DSER (dynamic-to-static
energy ratio) and PLCR (path-length change rate proxy). Calibration is
*"idle-gated baseline"* + Hampel.

Measured: ESP-NOW **~8 pkt/s** (C6) vs UDP **206.5 pkt/s** (see §1.5);
3-RX-node fusion over 45 features → **F1 = 0.833** (walk/sit vs empty);
~100–150 mA at 3.3 V; 6–12 h from a 2000 mAh LiPo. Requires
`framework: type: esp-idf`; Arduino is not supported.

**The beacon-frame idea is the cheapest thing to steal here** — it removes the
ping traffic entirely, at the cost of the beacon interval (typically 100 ms)
capping your rate.

---

## 3. Algorithms worth porting 🟡

The maths transfers; none of this code compiles on C5.

### 3.1 Embedded-specific, read these first

* **Hernandez & Bulut, "WiFi Sensing on the Edge: Signal Processing Techniques
  and Challenges for Real-World Systems"**, *IEEE Communications Surveys &
  Tutorials* **25(1):46–76 (2023)**, DOI `10.1109/COMST.2022.3209144`.
  Free PDF: <https://ebulutvcu.github.io/COMST22_WiFi_Sensing_Survey.pdf>
  Surveys which sensing techniques actually survive the move to a constrained
  edge device, and builds an ESP32-based system. The closest thing to a
  textbook for this project.

* **Armenta-Garcia, Gonzalez-Navarro, Caro-Gutierrez & Garcia-Reyes, "Tools and
  Methods for Achieving Wi-Fi Sensing in Embedded Devices"**, *Sensors*
  **25(19):6220**, 8 Oct 2025. <https://pmc.ncbi.nlm.nih.gov/articles/PMC12526573/>

  Its throughput table is the important part — **the serial link, not the radio,
  is usually the bottleneck**:

  | Configuration | Theoretical | Empirical |
  |---|---|---|
  | ASCII @ 230,400 baud | ~45 pps | ~30 pps (saturated) |
  | Binary @ 230,400 baud | ~225 pps | ~85 pps |
  | Internal CSI estimation, no I/O | 200 pps max | 163–182 pps measured |

  Also: 64 sub-carriers → 52 (drop null + pilot) → 47 empirically; and a
  **single highest-variance sub-carrier reached 92.43% accuracy at 232 ms
  inference latency**, versus ~10.9 s using all 52. Strong support for this
  repo's sub-carrier-mask approach. Compares the classic extraction tools
  (Intel 5300 / Atheros / Nexmon / ESP32 CSI Tool); does **not** cover C5/C6.

### 3.2 The two classic single-link papers this project is really an implementation of

* **PADS** — Qian, Wu, Yang, Liu, He & Xing, *"PADS: Passive detection of moving
  targets with dynamic speed using PHY layer information"*
  (<https://ieeexplore.ieee.org/abstract/document/7097784/>); extended as
  *"Enabling Contactless Detection of Moving Humans with Dynamic Speeds Using
  CSI"*, ACM TECS 2018 —
  PDF <https://tns.thss.tsinghua.edu.cn/~qiankun/files/TECS18_PADS_paper.pdf>.
  Uses the **maximum eigenvalue of the CSI correlation matrix** as a unified
  feature over amplitude and phase, plus outlier filtering on amplitude.

  **Caveat, same as FarSense below:** PADS as published exploits *"space
  diversity across multiantennas in MIMO systems"* and computes phase
  *differences between antenna pairs* — neither is available on a one-antenna
  C5 DevKit. What ports is the **feature**, not the setup: build the correlation
  matrix across **sub-carriers over a time window** instead of across antennas,
  and take its largest eigenvalue as the motion score. That adaptation is this
  document's suggestion, not the paper's — but the resulting score is still
  cheap enough for an MCU and captures cross-sub-carrier structure that
  per-sub-carrier variance throws away. **The most promising algorithm upgrade
  on this list, and the one that needs validating first.**

* **DeMan** — Wu, Yang, Zhou, Liu, Liu & Cao, *"Non-Invasive Detection of Moving
  and Stationary Human With WiFi"*, *IEEE JSAC* **33(11):2329–2342 (2015)** —
  PDF <https://tns.thss.tsinghua.edu.cn/~cswu/papers/JSAC15_Deman_paper.pdf>.
  The canonical treatment of the **motionless-person** problem this repo flags
  as unreliable: it treats **breathing as the intrinsic indicator of stationary
  presence**, fitting a sinusoidal model to minute chest motion and harnessing
  **frequency diversity across OFDM sub-carriers** rather than trusting one.
  Read before touching the static-presence branch.

### 3.3 Also useful

* Palipana et al., *"Channel State Information Based Human Presence Detection…"* —
  <https://ambientintelligence.aalto.fi/sameera/pdfs/p177-palipana.pdf>
* **FarSense**, *"Pushing the Range Limit of WiFi-based Respiration Sensing with
  CSI Ratio of Two Antennas"* — <https://arxiv.org/pdf/1907.03994>.
  The **CSI-ratio** trick cancels amplitude noise and phase offset by dividing
  two antennas' CSI. **Needs two RX antennas — the C5 DevKit has one**, so it is
  not directly applicable; included because it explains *why* single-antenna
  respiration sensing is as hard as this repo says it is.
* *"WiFi CSI-Based Long-Range Through-Wall Human Activity Recognition with the
  ESP32"* — <https://link.springer.com/chapter/10.1007/978-3-031-44137-0_4>

### 3.4 Recognise, count, vital signs — the ceilings Step 10 is held to

*Swept 2026-09-24.* Step 10 adds person recognition, 1-vs-2+ counting,
breathing and an experimental heart rate. These are the best **verified**
results found for each, on ESP32 wherever one exists. Every one was obtained under easier conditions
than one C5 pinging a router: controlled rooms and, where the paper was read
and says so, dedicated transmitters, several links or trained models. (For
Gaiba only the abstract was read, so beyond "a controlled environment" its
setup - including how many links - is unverified.) **Treat them as ceilings, not as what Step 10
achieves.** None of them is a measurement of this repo, and nothing in this
repo has run on hardware.

| Task | Best verified result | Under what conditions | What Step 10 does instead |
|---|---|---|---|
| Who is it | **95 %** of 2 people, **74 %** of 3 (Gaiba 2024) | controlled environment; abstract only | nearest-centroid on 18 gait features, ≤ 4 enrolled, one walker at a time |
| Who, with several people | **39–56 %** (Custance 2026) | 1–10 people, six separation methods | refuses: `person:"multiple"` whenever count ≥ 2 — which needs the trained counter |
| How many | median error **0.35**, **89.8 %** within ±1, 0–5 people (Wi-CaL 2022) | **four** crossing ESP32 links at 100 Hz, trained per room, people walking | one link, 1 vs 2+ movers, trained on site |
| Heart rate | MAE **0.46 bpm** (Pulse-Fi 2025) | dedicated ESP32 transmitter, 80 Hz, 7 seated people, trained LSTM, optimistic split | FFT peak picking, no training, router pings — `hr_bpm_exp`, unvalidated |
| Human vs pet/robot | non-human false alarms **63.1 % → 8.4 %** (Zhu 2025) | **not ESP32-specific**: commercial routers and IoT devices, gait/speed features + SVM | not implemented |

* **Gaiba, Bedogni, Gori, Melis & Prandini, "Wi-Fi Sensing for Human
  Identification Through ESP32 Devices: An Experimental Study"**, *IEEE CCNC
  2024*, pp. 206–209, DOI `10.1109/CCNC51664.2024.10454655`. Record with
  abstract: <https://unimore.unifind.cineca.it/resource/item/441836> 🟡

  The abstract reports *"an accuracy of 95% in distinguishing two users while
  74% in"* distinguishing three, from a data collection *"in a controlled
  environment"*. **Abstract only:** the full text is behind IEEE Xplore and was
  not read, so whether the subjects walked or stood, how many ESP32s were
  used, and which classifier ran are all unverified here. A search-engine
  summary attributes *"88.9% to 94.5% … from 6 to 2"* users to this paper; that
  figure appears on no page read in this sweep and is not repeated.

  **How to apply:** this is the ceiling for Step 10's recognition — two or
  three people, one at a time, one room. Note how fast it falls: 95 % → 74 %
  from two people to three. That is why Step 10 caps enrolment at four slots,
  answers `"unknown"` readily, and needs several agreeing windows before it
  names anyone.

* **Custance, Khan & Parkinson, "Why Commodity WiFi Sensors Fail at
  Multi-Person Gait Identification: A Systematic Analysis Using ESP32"**,
  arXiv [2601.02177](https://arxiv.org/abs/2601.02177) (Jan 2026, revised
  Jun 2026). 🟡 *Abstract only.* Six signal-separation methods, 1–10 people:
  *"performance remains moderate (39%-56% accuracy)"*, the best (NMF) at 56 %,
  with 97–99 % feature-space overlap between people. The authors put the limit
  on *"sensing quality and spatial diversity"* rather than the algorithm.
  **How to apply:** the reason Step 10 never tries to recognise anyone while
  its counter says two or more people are moving. That guard only works once
  the counter has been trained on site: untrained, `count` is 1 whenever
  anyone is present, so two people walking together are classified as one.

* **Choi, Fujimoto, Matsui, Misaki & Yasumoto, "Wi-CaL: WiFi Sensing and
  Machine Learning Based Device-Free Crowd Counting and Localization"**,
  *IEEE Access* **10:24395–24410 (2022)**, DOI `10.1109/ACCESS.2022.3155812`,
  open access (CC BY 4.0):
  <https://catalog.lib.kyushu-u.ac.jp/opac_download_md/7161171/7161171.pdf> 🟡
  * **Four** ESP32 transmitter/receiver pairs, i.e. four links crossing the area
    *"vertically, horizontally, and diagonally"*, each at 100 packets/s.
  * 5.5 × 5.5 m meeting room, 0–5 people walking freely: **0.35** error and
    *"89.8% of counting predictions occurred within-1-person error"*.
    11 × 5.5 m seminar room, 0–10 people: 0.41 and 81.8 %.
  * ⚠️ The paper's "MAE" is the **median** absolute error —
    *"MAE is the median value of the absolute crowd counting errors"*, chosen
    because of outliers. Do not compare it with a mean absolute error.
  * Leave-one-session-out cross-validation, LightGBM on per-link features. With
    fewer links *"the system performance drops when we include only a single
    link data"* (§V-C6); the per-link figures were not transcribed here.

  **How to apply:** real counting is a multi-link problem. One link can at
  best separate one mover from several, which is all Step 10 attempts. The
  second C5 is the first step toward this topology.

* **Kocheta, Bhatia & Obraczka, "Pulse-Fi: A Low-Cost System for Accurate
  Heart Rate Monitoring Using Wi-Fi Channel State Information"**,
  *DCOSS-IoT 2025*, DOI `10.1109/DCOSS-IoT65416.2025.00037`. UC Santa Cruz.
  Author PDF, read in full:
  <https://inrg.engineering.ucsc.edu/files/2025/05/Pulse_Fi_short_paper_iotdcoss.pdf>
  · press release: <https://news.ucsc.edu/2025/09/pulse-fi-wifi-heart-rate/> 🟡
  * ESP32 dataset: **two** ESP32s (a dedicated transmitter and a receiver),
    80 Hz, 20 MHz / 64 sub-carriers, **seven participants seated** in a chair
    between them at 1, 2 and 3 m for 5 minutes each, with a finger **pulse
    oximeter** as ground truth.
  * Method: band-pass 0.8–2.17 Hz, then a **trained LSTM** — not a
    spectral-peak picker.
  * Result: **MAE 0.46 bpm** with 30 s windows (0.43 / 0.48 / 0.49 at
    1 / 2 / 3 m).
  * ⚠️ **Read the evaluation before quoting that number.** Windows overlap by
    all but one packet (*"the first window is packets 1–100, the next is
    2–101"*) and are split 64/16/20 into train/validation/test *"ensuring
    generalization by shuffling"*. Test windows therefore have near-identical
    neighbours in the training set. That is not a held-out-person test, and
    0.46 bpm should be read as optimistic.
  * ⚠️ **The press coverage mixes the two datasets.** *"118 participants"* and
    *"sitting, standing, lying down, or walking"* describe the second dataset
    (EHealth: a Raspberry Pi 4B with Nexmon at 5 GHz / 80 MHz, a Galaxy Watch
    as reference), not the ESP32 one. The lab page's *"0.08 BPM"* appears
    nowhere in the paper. The nearest figure, 0.07 bpm, is a *prior* work's
    best single position on EHealth.

  **How to apply:** this is the most credible ESP32 heart-rate result found.
  It still needed a dedicated transmitter, a seated subject, a trained model
  and a reference device. Step 10 has no training and runs off a router's ping
  replies. That is why its output is called `hr_bpm_exp` and why the README
  tells you to check it with `tools/vitals_validate.py` before believing it.
  It is the same standard §5 applies to RuView.

* **Zhu, Hu, Wu, Wang, Wang & Liu, "Scaling WiFi Sensing for Ubiquitous Home
  Monitoring: Lessons from Real-World Deployment on Millions of Devices"**,
  arXiv [2506.04322](https://arxiv.org/abs/2506.04322) (v1 Jun 2025, v2 Aug
  2026). Authors at ADT Research (Rockville, MD) and the University of Hong
  Kong. ⚪
  * 280 edge devices in 15 homes over two years, > 4 M motion samples. CSI at
    100 Hz from commercial routers and IoT devices (Qualcomm, Broadcom,
    MediaTek and Espressif ESP8266 among the chipsets). **Per-device antenna
    counts are not stated** in the parts read, so do not assume this is a
    single-antenna result.
  * Pets, robot vacuums and oscillating appliances *"generated false positives
    in 63.1% of detection events"*. An SVM on *"gait existence, stride length,
    cycle time, average speed, speed variance, and speed percentiles"* plus six
    statistical features cut that to **8.4 %**.
  * On identity, verbatim: *"our system does not currently identify individual
    users; when multiple people are near the same device, overlapping motion
    signals limit differentiation, leaving the single-link multi-user sensing
    problem an open challenge."*

  **How to apply:** two things. Gait/speed features are the route to the
  README's "not human-specific" row, which Step 10 does **not** attempt. And a
  team shipping on routers at scale calls single-link multi-user sensing
  unsolved. Step 10 documents the same limit rather than claiming past it.

---

## 4. Platform, standards, datasets ⚪

### 4.1 Espressif platform

* **ESP-Techpedia, "Introduction to ESP-CSI Solution"** —
  <https://docs.espressif.com/projects/esp-techpedia/en/latest/esp-friends/solution-introduction/esp-csi/esp-csi-solution.html>
  Espressif's own CSI-quality ranking of its parts, verbatim:
  **`ESP32-C5 ＞ ESP32-C6 ＞ ESP32-C3 ≈ ESP32-S3 ＞ ESP32`** — the C5 is the best
  CSI receiver Espressif makes, so this project is on the right chip. Also
  describes their lab rig: directional antennas + antenna switch, a **clock
  buffer shared between the TX and RX chips to eliminate frequency offset** (the
  reason their phase-based work is possible and single-board phase work is not),
  and copper sheets to shape the sensing range.
* **ESP-IDF "RF Coexistence" guide, `esp32c5` page** —
  <https://docs.espressif.com/projects/esp-idf/en/latest/esp32c5/api-guides/coexist.html>
  *(added 2026-09-24)*. *"Each chip has only one RF path, shared by these two
  or three modules … time-division multiplexing is used"* between the chip's
  radios, Wi-Fi and Bluetooth LE among them. Wi-Fi STA connected + BLE scan is
  listed as a supported combination. **Relevant to Step 10's next steps:** BLE
  (a phone or watch as the identity token) is the practical route to *who*, but
  every millisecond the radio spends on BLE is one it is not receiving the CSI
  this project runs on. Measure the CSI rate with BLE on before relying on
  both.
* **ESP-IDF Workshop: ESP32-C5** —
  <https://developer.espressif.com/workshops/esp-idf-with-esp32-c5/>
  Dual-band Wi-Fi 6, partition tables, OTA, low-power/ULP. Not CSI, but the
  best C5-specific ESP-IDF material.

### 4.2 Standard

* **IEEE Std 802.11bf-2025 (WLAN Sensing) — published 26 September 2025.**
  Task-group page: <https://www.ieee802.org/11/Reports/tgbf_update.htm>
  Overview paper (NIST): <https://ieeexplore.ieee.org/document/10467185/> ·
  free PDF <https://tsapps.nist.gov/publication/get_pdf.cfm?pub_id=935175>
  Sensing is now a ratified part of 802.11 rather than a CSI hack. The C5 is an
  802.11ax part, so this is the direction the ecosystem moves in — nothing to
  implement today, but it defines the vocabulary.

### 4.3 Datasets (for offline algorithm work, before you have your own captures)

* **Zenodo 10925351** — <https://zenodo.org/records/10925351> — synchronised CSI
  and 3D trajectory data over three days, in both static and dynamic
  environments.
* **IEEE DataPort, "Channel State Information Dataset for Multi-Human Activity
  Recognition in Indoor Environments"** —
  <https://ieee-dataport.org/documents/channel-state-information-dataset-multi-human-activity-recognition-indoor-environments>
  ESP32-NodeMCU + ESP32-CSI-Tool, 80+ participants, six activities, single- /
  two- / three-user scenarios.
* **"A high-dimensional, multi-transceiver channel state information dataset…"** —
  <https://www.sciencedirect.com/science/article/pii/S2352340924006401>
  4 × ESP32-S3-DevKitC-1 as two transceiver pairs, ~300–450 packets per sample,
  ~1200 samples per activity, 166 sub-carriers.

### 4.4 Older capture toolkits 🟡

Both are **classic-ESP32 only** — listed because most papers and datasets above
were captured with them, so their CSV layouts are worth recognising.

* **ESP32 CSI Toolkit** (Hernandez) — <https://stevenmhernandez.github.io/ESP32-CSI-Tool/>
  Active-AP / active-STA / passive modes, serial + microSD capture. Tested on
  TTGO T8, NodeMCU ESP32, TTGO TS V1.2. No C5/C6.
* **Wi-ESP** — <https://wrlab.github.io/Wi-ESP/> — WROOM-32 / WROVER, amplitude
  and phase, L-LTF and HT-LTF. Paper: *"Wi-ESP—A tool for CSI-based Device-Free
  Wi-Fi Sensing (DFWS)"*, DOI `10.1093/jcde/qwaa048`. No C5/C6.

---

## 5. Community projects, demos and video

Useful for expectation-setting and integration ideas. Claims here are
self-reported; none are independently validated.

* **Home Assistant — "Human Presence using WiFi Sensing CSI on ESP32"** —
  <https://community.home-assistant.io/t/human-presence-using-wifi-sensing-csi-on-esp32/791452>
  Worth reading for the failure modes: the original `mzakharo/esp32_csi_human_presence`
  repo (posted 6 Nov 2024) was **removed**, breaking everyone's ESPHome builds
  by June 2025; ESPectre was suggested as the replacement on 28 Dec 2025. Users
  ask the same two questions this project answers honestly — *does it see you
  when you are idle*, and *how do I stop rooms overlapping*.
* **ESPectre on Home Assistant** —
  <https://community.home-assistant.io/t/espectre-wi-fi-motion-detection-for-home-assistant/961251>
* **WaveSight** — <https://github.com/ErfanDL/WaveSight> — ESP-IDF v5.4.4,
  jitter metric off the router link, ~60 s auto-calibration, live web dashboard
  with RSSI/jitter charts. **ESP32 / S2 / S3 / C3 only — no C5.** A good model
  for the README's "web dashboard" next step; no license stated.
* **RuView** — <https://ruview.pro/> · <https://github.com/ruvnet/RuView> · MIT.
  Lists ESP32-C5 and claims presence, motion, **breathing and heart rate through
  walls** from a single node plus a router. **No published validation, accuracy
  figures or peer review are presented on the site.** The heartbeat claim in
  particular is far beyond what any of the peer-reviewed work in §3 demonstrates
  on single-antenna commodity hardware — treat it as unsubstantiated.
  *(2026-09-24)* This repo holds its own Step 10 to the same standard. Its heart
  rate is named `hr_bpm_exp` in the JSON and stays **experimental and
  unvalidated** until someone checks it against a chest strap or pulse oximeter
  with `tools/vitals_validate.py`. §3.4 shows what the best verified ESP32
  heart-rate result took: a dedicated transmitter, a seated subject, a trained
  model and a reference device.
* **Espressif video: "ESP-CSI: Transforming Wi-Fi into a Sensing Platform"** —
  <https://www.youtube.com/watch?v=JjdpzM6zVJ8>
* **Espressif video: "ESP-WIFI-CSI: Espressif's Human-Detection Solution"** —
  <https://www.youtube.com/watch?v=tFxKUzEDSdw> — an ESP lamp + router demo.
  *(Video metadata could not be fetched; dates unverified.)*
* **Hackster: "ESP-CSI: DIY WiFi Human Presence Detection"** —
  <https://www.hackster.io/limengdu0117/esp-csi-diy-wifi-human-presence-detection-f80508>
  *(Returned HTTP 403 to automated fetch — listed unread, contents unverified.)*

---

## 6. What to actually do with this

Ordered by value per hour, given that **no step has run on silicon yet**:

1. **Flash Step 04 and check `csi_data_len` against §1.1.** Expect a value from
   **{106, 114, 228, 234, 468, 490}** — 106 / 114 / 490 are the single-stream
   20 MHz cases, 228 / 468 the two-stream STBC ones, 234 the HT40 one. A value
   outside that set is the thing worth investigating. Then check the packet
   rate against §1.2's 90–290 CSI/s.
2. **Add a hold timer.** `active_filter_ms` (§2.1) and `motion_on_hits` /
   `motion_off_hits` = 4 / 3 (§2.6) are two independent implementations of the
   same anti-flicker idea. This repo has none.
3. **Audit Step 06's sub-carrier mask for adjacency.** ESPectre's reasoning in
   §2.6 — coherent over ~10 sub-carriers, so spread them — says a
   highest-variance rule that picks neighbours is buying almost no new
   information.
4. **Add a Hampel filter** (window 7, 5.0 MAD, §2.6) ahead of the motion score.
   Cheap, and it is on by default in a shipped product.
5. **Split the metric in two** — `waveform_wander` vs `waveform_jitter` (§2.2):
   slow drift and fast jitter want different thresholds.
6. **Before building the ESP-NOW second board, measure its rate** (§1.5).
7. **Then, if you want a real accuracy jump:** the PADS correlation-matrix
   eigenvalue (§3.2) as the motion score, and DeMan's breathing model (§3.2)
   for the static-presence branch.
   🧪 *Partly implemented, experimental and unvalidated — Step 10
   (2026-09-24):*
   * **DeMan-style breathing** is in `step10_recognise_count_vitals/main/vitals.c`.
     It takes DeMan's frequency diversity: the spectra of 12 sub-carriers,
     each normalised to unit power in the band, are summed with equal weight
     (a power sum, so channels in opposite phase cannot cancel). It uses an
     FFT peak rather than DeMan's sinusoidal fit. Confident breathing can
     hold a still person PRESENT (`CSI_VITALS_HOLDS_PRESENCE`), but only in a
     still spell that began while PRESENT.
   * **Stillness for it comes from a separate motion gate**
     (`motion_gate.c`), not from Step 08's `M`. Step 08's 0.5 s fast EMA has
     its corner at ~0.32 Hz, inside the breathing band, so breathing itself
     held Step 08's motion flag on in Step 10's synthetic end-to-end test and
     starved the breathing analysis. The gate high-passes the sub-carrier-group
     signals at ~1.6 Hz and compares their energy with the empty-room level
     (JSON `hf`). This is this repo's design, not a published method.
   * **The PADS eigenvalue idea** is used for 1-vs-2+ counting in
     `count_est.c`, on the 8 × 8 sub-carrier-group covariance, without a full
     eigen-decomposition. The participation ratio (Σλ)² / Σλ² is computed
     exactly as trace(C)² / ‖C‖²_F. λ₂/λ₁ and λ₁/Σλ come from power iteration
     (`dsp_top_eig`), with deflation and a 2 × 2 Rayleigh–Ritz step for λ₂.
     The top eigenvector, also found by power iteration, drives the gait
     features in `gait_id.c`. The Jacobi solver `dsp_sym_eig` remains for the
     host tests only: the firmware's linker map lists it among the discarded
     sections. None of this is the motion score: Step 10's presence still
     triggers on Step 08's `M`. The "motion score" half of this item is still
     open.

   All of it is host-tested on synthetic signals only. Nothing has been
   checked in a real room.
8. **Benchmark against Espressif's stack.** ✅ *Done — `step09_espressif_sensing/`
   builds and runs `esp_wifi_sensing` against the same board, router and room.
   Flash it and Step 08 alternately and compare the numbers directly.*
9. **Before believing Step 10's heart rate, validate it.** Record
   `presence_monitor.py --csv` next to a chest strap or pulse oximeter and run
   `tools/vitals_validate.py` (§3.4 explains why this is not optional). Do the
   same for recognition: enrol two people, then log who walked past and when,
   and count the hits yourself.

---

## 7. Build configuration (changed 2026-09-07)

The repo previously built at **`-Og`** — `CONFIG_COMPILER_OPTIMIZATION_DEBUG`,
the ESP-IDF default. The CSI amplitude loop runs inside the Wi-Fi task callback,
once per packet, at up to ~290 packets/s (§1.2): it is the hot path of the whole
project and it was not being optimised. All nine projects now set:

| Setting | Was | Now | Why |
|---|---|---|---|
| `COMPILER_OPTIMIZATION_PERF` | `_DEBUG` (-Og) | **-O2** | 960 of 1111 translation units, measured in `compile_commands.json` |
| `FREERTOS_HZ` | 100 | **1000** | a 10 ms tick is the same size as the 3.4–10 ms inter-packet interval it has to measure |
| ↳ *and it unblocks the ping rate* | | | at 100 Hz the ping simply **could not** be scheduled faster than every 10 ms, capping CSI near 100/s. `CSI_PING_INTERVAL_MS` can now go below 20 — the lever for reaching §1.2's 90–290/s |
| `ESPTOOLPY_FLASHMODE_QIO` | DIO | **QIO** | 2× flash read bandwidth for XIP cache misses |
| `COMPILER_OPTIMIZATION_ASSERTIONS` | on | **on (kept)** | nothing has run on silicon yet; a named assert beats a silent hang |

Two things were tried and **reverted**, both worth knowing:

* **`BOOTLOADER_COMPILER_OPTIMIZATION_PERF` breaks the build.** -O2 grows the
  bootloader to 0x6280 bytes against a 0x6000 ceiling set by the partition-table
  offset at 0x8000. It also buys nothing — the bootloader executes once, before
  the app, and is not on the CSI path. Left at `-Os`, which leaves 0x540 (5%)
  free.
* **`ESPTOOLPY_FLASHFREQ_120M` is a dead end on C5.** The option exists, but
  `components/esptool_py/Kconfig.projbuild:97` resolves it to `'80m'` on this
  target. 80 MHz is already the ceiling.

And one to **not** enable, if you go looking for more:

* **`COMPILER_ENABLE_RISCV_ZCMP`** (`esp-idf/Kconfig:374`) is a code-size
  extension, not a speed one — and the ESP-IDF **v6.1 release notes list it
  under Known Issues: *"ESP32-C5 is not compatibility with
  CONFIG_COMPILER_ENABLE_RISCV_ZCMP=y"***. It is correctly off by default here;
  leave it off.

Note that `CONFIG_ESPTOOLPY_FLASHMODE` still reads `"dio"` in the generated
sdkconfig even with QIO selected — that is by design (same Kconfig, line 81).
The image is *written* in DIO and the second-stage bootloader upgrades the mode
at runtime, which is why enabling QIO carries no flashing risk.

**Wi-Fi RX buffers were deliberately left alone** at 16 static / 32 dynamic. The
obvious move is to raise them, but 16 static buffers is already ~26 KB of DRAM
on a single-core part with no PSRAM, and there is no free-heap measurement to
spend against yet. Step 03 now prints free heap next to the CSI rate every
second — flash it, read the number under load, and raise them only if the
headroom is real *and* the rate is short of the 90–290/s bracket in §1.2.

---

*Compiled 2026-09-07; §3.4 and the §4.1 coexistence entry added 2026-09-24;
§6 item 7 brought in line with the final Step 10 code the same day.
Versions, issue states and figures were current on those dates; re-check §2
component versions before depending on them.*
