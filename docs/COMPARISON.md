# How this project compares with existing work

Swept 2026-09-25 from open-source projects, papers, products and standards.
Every accuracy figure was re-checked against its source by a second reader.
**Updated 2026-10-16:** ESPectre's figures and star count (§1, §3, §5), the
activity state (§2D), and a survey of current videos and projects (§7). The
2026-10-16 additions were read once, not re-checked by a second reader.
Figures marked "(unverified)" could not be reached; figures that turned out to
be wrong or misattributed are listed at the very end, not used.

**Read this first.** This project has run on one ESP32-C5 for bring-up
only (Steps 01, 03, 04 and 10 on a laptop hotspot; TESTING.md §5.1), and no
run recorded who was in the room. Every accuracy cell
for it says **"Not measured yet"**, and nothing below claims an accuracy
advantage. Where it is different is scope and architecture. See
[`REFERENCES.md`](REFERENCES.md) for the annotated reference list this builds on.

## 1. Does something like this already exist? (overview)

**Terms used below:**
- **Hardware:** TX/RX = transmitter/receiver. "ESP32" means the classic ESP32 unless a variant is named. pkt/s = CSI packets per second.
- **Metrics:**
  - MAE = mean absolute error. Wi-CaL's "MAE" is actually a *median*.
  - LoA = Bland-Altman 95 % limits of agreement.
  - FAR/FRR = false accept / false reject rate. EER = equal error rate.
  - FPIR = a stranger named as someone. FNIR = an enrolled person missed.
  - LOSO = leave-one-session-out cross-validation.
  - bpm = beats (or breaths) per minute.
- **Where a figure comes from:**
  - PR = peer-reviewed. PP = preprint.
  - RC = the project's own README or benchmark. VC = vendor claim.
  - (abs) = only the abstract was read. (full) = the full text was read.

| System | Presence | Who | Count | Breathing | HR | Runs on | ESP32-C5 | Published accuracy |
|---|---|---|---|---|---|---|---|---|
| **This project** | moving yes, still weak; plus a coarse activity state (empty / still / restless / moving, §2D) | ≤4 people, 1 walker | 1 vs 2+ | 1 still person | experimental | chip (1 C5 + router) | Step 10 has run on one C5 (bring-up, no accuracy); Step 08 never run | **Not measured yet** |
| ESPectre | motion only | no ("does not identify people") | no | no | no | chip (1 board + router) | yes | C5 F1 98.9 % (Lightweight) / 99.5 % (High-Accuracy) (RC: host replay of 20 paired HT20/HT-LTF recordings, report of 2026-10-07; it was 97.2 % / 99.9 % on 2026-09-25) |
| Espressif `esp_wifi_sensing` / esp-radar | presence | no | no | no | no | chip; core algorithm is a prebuilt binary | yes | none published |
| PeterkoCZ91 esphome-wifi-csi | presence | no | no | rate (DFT) | no | chip; its ML model runs on a host | 2.4 GHz only | F1 0.833 occupied vs empty, 3 RX nodes + host (RC) |
| RuView | claims | no | 0/1 | claims | claims | host models (claims on-device S3) | **not supported** | count 62.3 % (1 session); vitals unmeasured by its own ADR-293 |
| Commercial Wi-Fi (Xfinity, Plume, Origin, nami) | motion | no | no | no | no | ISP gateway/mesh, cloud or router | n/a | none published |
| mmWave radar (HLK-LD2410C/LD2450, Aqara FP2, Seeed MR60BHA2) | yes, including still people | no | tracks ≤3–5 | MR60BHA2, FP2 | MR60BHA2, FP2 | on module | n/a | mostly none; FP2 falls ">98 %" (VC) |
| Research papers (Gaiba, Wi-CaL, Pulse-Fi, …) | – | yes | yes | yes | yes | PC or laptop | none on C5 | see tables 2A–2C |

The tool-only projects (ESP32-CSI-Tool, Wi-ESP, esp-csi-rs, Nexmon, PicoScenes, Intel 5300 and Atheros CSI tools) only capture raw CSI and publish no sensing accuracy. esp-csi-rs is the only one of them that supports the C5.

---

## 2A. Recognise who

| System | Hardware | Runs | Trained? | Accuracy (basis) | Cost / availability | Key limitation |
|---|---|---|---|---|---|---|
| **This project (Step 10)** | 1 ESP32-C5 + home router | on chip | on-site enrolment (30 gait windows per person), nearest centroid | **Not measured yet** | 1 C5 board (price not sourced); code unpublished, no licence | ≤4 enrolled, 1 walker, walking only; "multiple" needs the trained counter; re-enrol if the board moves |
| Gaiba 2024 (CCNC) | ESP32, link count unknown | not stated (probably a host) | implied | 95 % for 2 people, 74 % for 3, controlled room (PR, abs) | paywalled; open-access copy needs a manual download | walking or standing, number of links and classifier all unknown |
| Custance 2026 (T-BIOM) | ESP32 per the authors' later paper; links unknown | host | yes: CNN + LSTM + attention | 97.9 % on 25 people (their own set); 96.1 / 96.66 % on two public 10-person sets (PR, abs). Open-set AUC 96.2 %, EER 10.8 % (unverified: secondary source) | paywalled | model far too heavy for the FPU-less C5; train/test split unknown |
| Custance 2026 (arXiv 2601.02177) | "ESP32", 5 GHz, 3 antennas (underspecified; does not match a classic ESP32) | host | yes: SVM after signal separation | 39–56 % (best, NMF: 46.7 % lab / 60.0 % classroom) with 1, 2 or 5 people walking at once (PP, full) | free | identifying several walkers at once fails |
| Avola 2025 (DMS 2025) | 2 ESP32s, dedicated TX 3 m away, 100 Hz, laptops | host | yes: transformer | 99.82 %, 6 people **standing still** (PR; arXiv v1 read in full) | free on arXiv | test data from the same session (sequential 70/10/20 split); not gait |
| WifiU 2016 (UbiComp) | router (2 antennas) to Intel 5300 laptop (3 antennas), 2,500 pkt/s | host, offline | yes: SVM | top-1 79.28 % (50 people), 92.31 % (10-person sets); strangers: FAR 8.05 %, FRR 9.54 %, EER 8.6 % (PR, full) | Intel 5300 discontinued | 25× the ≥100 pkt/s Step 10 needs for gait; a change of clothes pushes FRR to ≥25 % |
| WiWho 2016 (IPSN) | commodity Wi-Fi (NIC not verified) | host | yes: decision tree | 92 % falling to 80 % for 2 to 6 people, 20 volunteers (PR, abs) | paywalled | walked about 1 m from the TX-RX line (per WifiU) |
| All products and open-source projects surveyed | – | – | – | none identifies people. Comcast: "not identify specific people"; ADT deployment: "does not currently identify individual users" | – | no shipping equivalent |

## 2B. Count people

| System | Hardware | Runs | Trained? | Accuracy (basis) | Cost / availability | Key limitation |
|---|---|---|---|---|---|---|
| **This project (Step 10)** | 1 C5 + router | on chip | threshold trained on site (eigenvalue spread) | **Not measured yet** | as above | only 0 / 1 / 2+ **moving** people; still people are invisible; untrained, it reports 1 whenever anyone is present |
| Wi-CaL 2022 (IEEE Access) | 4 ESP32 TX/RX pairs, 100 pkt/s | host | yes: LightGBM, per room | median error 0.35 (0–5 people) and 0.41 (0–10); 89.8 % / 81.8 % within ±1 person; **one link alone: 0.55–0.82** (0–10 people); LOSO (PR, full) | open-access paper | needs 4 links and a host |
| Wi-Monitor 2022 (GIoTS) | 1 ESP32 pair (per the paper's keywords) | host | yes: DNN | 84.6 % exact count for 0–3 people (70.3 % with the other filter); 76.5 % for 0/5/10/15/20 people (PR, abs) | paywalled | dedicated pair, not a router; setup not read |
| RuView count_v1 | ESP32-S3/C6 + Pi, PC or Seed hub | host | yes | 62.3 % overall, 34.3 % on the "1 person" class, MAE 0.377; one 30-min session, labels 0/1 only, random split (RC) | MIT licence; C5 not supported | tested only on 0 vs 1 people, one session |
| TI IWR6843 (TIDEP-01000) | 60 GHz radar, 3 TX / 4 RX | on chip | tracker, not ML | frames with zero count errors: 1 person 79.4–99.8 %, 3: 40.2 %, 4: 33.4 %, 5: 29.1 / 79.7 %, 6: 41.4 %; position error 0.18–0.35 m (VC, checked against video) | evaluation board in stock; price not listed | people who sit still drop out |
| HLK-LD2450 | 24 GHz radar, 1 TX / 2 RX + a host MCU | on module | – | none published; tracks ≤3 moving targets to 6 m (VC spec) | US$4.46 | 3 targets at most; moving people only |
| Aqara FP2 | 60 GHz radar unit | on device | – | none published; "up to 5, in theory", best with ≤3 (VC) | US$82.99 | people-counting on entry was a 2023 roadmap item |
| Custance WiFlow (arXiv 2601.02203) | 2 ESP32s (1 link), 100 Hz | host | yes: PyTorch | door crossings, not people in the room: MAE 0.44 with 10-shot training (PP; sweep only, not re-checked) | free | counts entries and exits only |

ESPectre, `esp_wifi_sensing`, the Peterko fork and all the commercial Wi-Fi products do not count people at all.

## 2C. Heart rate / breathing

| System | Hardware | Runs | Trained? | Accuracy (basis) | Cost / availability | Key limitation |
|---|---|---|---|---|---|---|
| **This project (Step 10)** | 1 C5 + router, about 50 pkt/s by default | on chip | no: FFT peak over 12 sub-carriers, 10 Hz time bins | **Not measured yet** | as above | one still person; `hr_bpm_exp` is experimental; tagged with the recognised name (`vitals_of`), rates withheld as `"multiple"` for 2+ people |
| Pulse-Fi 2025 (DCOSS-IoT + arXiv 2510.24744) | 2 ESP32s, 1 antenna each, dedicated TX at 80 Hz | host | yes: LSTM | HR MAE 0.46 bpm (30 s windows) / 0.50 (5 s); 10-fold CV 0.892. Breathing MAE 0.09/min. 7 seated people, pulse oximeter as reference (PR + PP, full) | papers | shuffled, overlapping windows make the results optimistic |
| COVID-Beat 2022 (JCDE) | home router to 1 ESP32, PC over USB | host | no: filters + spectral peak | breathing "accuracy" 91.81–99.6 %, formula not given; 5 people sitting or lying (PR) | open access | breathing only; metric not defined |
| DeMan 2015 (JSAC) | router pings to Intel 5300 desktop, 50 Hz | host | one-time learned threshold | breathing error 0.86 bpm (line of sight) / 0.97 (no line of sight), 35 × 1 min; still person detected: TP 94.64 %, TN 94.49 % (PR, full) | Intel 5300 discontinued | 3-antenna receiver |
| PhaseBeat 2017 / 2020 | Intel 5300 (3 RX) + laptop sending 400 pkt/s | host | no | median error: breathing 0.25 bpm, HR 1.19 bpm. HR rises to **2.91 at 20 Hz sampling** and **4.82 with an omni TX antenna** (PR, full) | discontinued hardware | needs 2 RX antennas (phase difference) |
| TensorBeat 2017 | Intel 5300 (3 RX), 20 Hz | host | no | breathing for 1–5 people: 1 person max error <0.9 bpm; 5 people 82.4 % success (every error <2 bpm) (PR, full) | discontinued hardware | separating people needs antenna pairs |
| RuView | ESP32-S3/C6 + host | claims on-device (S3) | mixed | its own ADR-293: "no way to produce a MEASURED vitals number" (RC) | MIT; C5 not supported | unmeasured |
| Seeed MR60BHA2 | 60 GHz radar (2 TX / 2 RX) | on module | not stated | datasheet "90 %" typical for both, metric undefined (VC); one user vs Polar H10: correlation about 0.33 (forum post, conditions not stated) | US$26.90 | 1 person at rest, 0.4–1.5 m, sleep use only |
| Withings Sleep Rx | under-mattress air bladder + microphone | device + app | not stated | HR within ±5 bpm of ECG (FDA 510(k) K231667); sleep apnea (AHI ≥15): sensitivity 88.0 %, specificity 88.6 %, n = 118 vs a sleep lab (PR) | FDA-cleared; price unverified (sources conflict) | bed only, one sleeper |

Two more systems, not in the table:
- **Google Nest Hub (Soli radar)** ships a respiration rate with no published accuracy. Its heart-rate figure, MAE 1.69 bpm (62 users, ECG reference), is research on the same radar, not a feature it ships.
- **No accuracy published:** the Peterko fork (on-device breathing DFT) and nickbild/csi_hr (2 ESP32s + PC LSTM for heart rate).

## 2D. Activity state (added 2026-10-16)

| System | Hardware | Runs | Trained? | Accuracy (basis) | Key limitation |
|---|---|---|---|---|---|
| **This project (Step 10, `activity`)** | 1 C5 + router | on chip | no: per-second motion-gate duty and `hf` over 5-10 s windows, held 2 s; thresholds set on synthetic data (DECISIONS D-172) | **Not measured yet.** Synthetic only: in `test_e2e` (seed 5000) it agrees with the scripted truth on 90 % of packets while walking in or sitting and breathing, and on 35 % while fidgeting (TESTING §5.2, 2026-10-16) | four coarse states, no posture; `still` lasts only while PRESENT does (normally the breathing hold); a fan or pet moves it like a person |
| Armenta-Garcia et al. 2025 (Sensors 25(19):6220) | 2 classic ESP32s at 50 pkt/s, dedicated TX | int8 TFLM model on an ESP32-S3 (which has an FPU) | yes | 92.43 % on 5 activities; 232 ms per inference with one subcarrier, about 10.9 s with all (PR; read for the 2026-10-16 survey) | whether train and test share subjects is not stated |
| STAR (arXiv 2510.26148) | ESP32-S3 captures only | Rockchip RV1126 NPU | yes | not quoted here (PP) | needs an NPU |

ESPectre (motion only), `esp_wifi_sensing` (motion and presence) and the
commercial Wi-Fi products report no activity classes. No Espressif CSI
package does activity, counting, breathing or heart rate; `espressif/esp_health`
0.1.0 does heart rate and SpO2 from a PPG optical sensor, not from Wi-Fi
(2026-10-16 survey, §7).

---

## 3. Standards: the bar each output would have to meet

| Output | Bar (source, status) | Does any Wi-Fi system meet it? | This project |
|---|---|---|---|
| Presence | IEEE 802.11bf-2025 sets **no** accuracy bar; it is a protocol standard, free through IEEE GET. The pre-standard TGbf target sheet (11-20/1712r2, 2020) asks for miss ≤0.001 and false alarm ≤0.0001 per second | None reports in those units. ESPectre's C5 FP rate, 1.1 % (Lightweight) / 0.1 % (High-Accuracy) in its 2026-10-07 report (2.9 % / 0.1 % on 2026-09-25), is per window, from its own host replay | Not measured |
| Presence test method | IEC 63180 (PIR) and NEMA WD 7 (PIR, ultrasonic, microwave): walk-test grid, major vs minor motion, no pass/fail line | n/a: a method to borrow | Not tested |
| Heart rate | ±10 % or ±5 bpm, whichever is larger (ANSI/AAMI EC13:2002). EC13 covers ECG devices only and has been superseded, but papers borrow it. Consumer bar: MAPE ≤10 % (ANSI/CTA-2065, 2018 edition, measured during activity) | The reported errors are inside it: Pulse-Fi 0.46–0.89 bpm; PhaseBeat median 1.19 (but max 10.02). Both needed a dedicated TX or a 3-antenna card, and splits favourable to themselves. None reports the EC13 pass rate. None is certified | Not measured; script has no MAPE or EC13 rate |
| Breathing | No ISO/IEC bar found. Clinical convention: LoA within ±3 breaths/min (Toften 2024) | Reported errors are inside ±3 (PhaseBeat, TensorBeat, DeMan, Pulse-Fi), but none reports LoA. A radar study that does: LoA −1.1 to +1.2 (Toften) | Not measured; script has no LoA |
| Identity | ISO/IEC 19795-1:2021 sets no threshold. An open-set system should report FPIR and FNIR | Closest: WifiU FAR 8.05 %, FRR 9.54 % (Intel 5300). The ESP32 papers report closed-set accuracy only. The TGbf identity use case was 60 GHz only | Not measured; no scoring script |
| Counting | No bar found. TGbf names the "number of persons" measure but gives no number | – | Not measured |
| US regulation | FDA General Wellness guidance (final, 2026-01-06): a displayed value that "mimic[s] those used clinically" must be validated, and the product must not claim clinical accuracy | – | README says "Not a medical device"; `hr_bpm_exp` must stay labelled experimental until validated |
| Reference device for validation | Polar H10 chest strap: 99.6 % beat-interval signal quality vs 94.6 % for a Holter, both scored against raw ECG. Apple Watch HR LoA −7.19 to +6.64 bpm is too loose for a reference. FDA's 2013 pulse-oximeter guidance sets no pulse-rate limit | – | Use a chest strap |

There is no Wi-Fi Alliance sensing certification: wi-fi.org says only that members are "working on" it. The Wireless Broadband Alliance's "Wi-Fi Sensing Test Methodology" (2024 edition, free after registration) is worth downloading before the first hardware test.

---

## 4. Where this project is genuinely stronger

This section is about scope and architecture. None of it is about accuracy.

- **The only system surveyed that attempts identity, 1-vs-2+ counting, breathing and heart rate together on one board** (and, since 2026-10-16, a coarse activity state, §2D).
  - ESPectre is motion only; its README says it "does not identify people, count them".
  - `esp_wifi_sensing` does presence only.
  - The Peterko fork does presence and breathing, and its published F1 needs 3 nodes plus a host.
  - RuView's models run on a host, and it does not support the C5.
  - The commercial Wi-Fi products do motion only.
  - The radar modules do not identify anyone.
- **The least hardware: one C5 plus the router you already have.**
  - Pulse-Fi and Avola needed a dedicated ESP32 transmitter.
  - Wi-CaL used 4 links.
  - WifiU, PhaseBeat, TensorBeat and DeMan used a 3-antenna Intel 5300 and a PC.
  - RuView recommends 2+ nodes and a host.
- **Everything runs on the chip: no PC, cloud or subscription.**
  - Every ESP32 paper found ran its analysis on a PC, laptop or Pi. Pulse-Fi only *estimated* 0.15 s per inference, on an ESP32 that has an FPU.
  - Xfinity keeps motion history in Comcast's cloud for 7 days, where it can be disclosed under subpoena, and its Shield Select tier costs $15 a month.
  - The build fits the FPU-less C5 (Step 10 images 0xef360 on v6.0.2 and 0xf56d0 on v6.1). Heap and per-packet time were measured once: a 15-minute run of one image (0xef240) at 50 Hz on 2026-09-28 left 94,132 B of heap free, with the slowest packet at most 20.8 ms in its detect rows (52.2 ms since boot) (TESTING §5.1). No other image has been measured.
  - ESPectre and `esp_wifi_sensing` also run fully on the chip, so this is no advantage over them.
- **All algorithms are in source, with nothing trained elsewhere.** By contrast, Espressif's core is a prebuilt binary: `esp_radar_motion_dec`, and the `esp_wifi_sensing` 0.1.1~2 that Step 09 pins. ESPectre's neural-network weights are trained offline on its author's recordings. This only counts once the code is published with a licence.
- **It says "I don't know" instead of guessing.**
  - It answers `"unknown"` for strangers, which is the open-set framing ISO/IEC 19795-1 expects.
  - It answers `"multiple"`, and withholds vitals, when 2+ people are present. The literature shows why: multi-walker identity reaches only 39–56 % (Custance), and multi-person breathing needed antenna pairs (TensorBeat, PhaseBeat).
  - Gaiba and Avola tested closed-set only, with no strangers.
- **Vitals labelled with a person's name: nothing surveyed does this**, because nothing surveyed identifies people at all. Caveats: it is built and host-tested (2026-09-25) but never observed on hardware (Step 10 has run on the C5, but no vitals were attributed in any recorded run), and the name is carried over from the person's walk in. It is not measured from their chest.
- **Correct 12-bit L-LTF decoding on the C5, but only in an optional mode** (`CSI_LLTF_ONLY`, off by default).
  - In that mode it decodes the C5's 12-bit words as an Espressif collaborator described them (esp-idf #18118). RuView's C5 branch reads the same bytes as int8, which gives garbage (RuView commit dd02efe, checked 2026-09-22).
  - There is no edge over esp-radar, which decodes the same way, or ESPectre, which switches the C5 to 8-bit mode instead. Both routes are valid.
  - The imag/real word order is unverified. It affects only the sign of the phase, not amplitude.

## 5. Where others are stronger

- **Measured results exist for them.** ESPectre publishes C5 figures: F1 98.9 % (Lightweight) and 99.5 % (High-Accuracy, a neural network) in its performance report of 2026-10-07 (97.2 % / 99.9 % on 2026-09-25, then from its replay of one bedroom). The report replays 20 paired 2.4 GHz HT20/HT-LTF recordings on a host ("Timings and CPU estimates therefore describe the report host, not an ESP device"), so none of it carries over to this project's 50 Hz L-LTF on a laptop hotspot. On measured C5 presence, ESPectre is ahead today.
- **Still people.**
  - Radar sees them directly: the HLK-LD2410C (US$2.46) detects moving and still people to 5 m, and the Aqara FP2 senses breathing and heartbeat.
  - DeMan exceeded 95 % TP/TN for still people at 50 Hz, Step 10's default rate, but with a 3-antenna receiver.
  - Step 08 calls its own still-person detection unreliable.
- **Counting.**
  - Wi-CaL: median error 0.35 with LOSO testing. Even one Wi-CaL link estimates 0–10 people (median error 0.55–0.82).
  - TI radar tracks positions to 0.18–0.35 m.
  - The LD2450 tracks 3 people for US$4.46.
  - Step 10 separates only 1 from 2+.
- **Heart rate.**
  - Step 10 computes HR from **10 Hz binned amplitude on one omni antenna** (`VIT_FS_HZ 10`). That is PhaseBeat's worst case: its median HR error went from 1.19 to 2.91 bpm at 20 Hz, and to 4.82 bpm with an omni antenna.
  - Pulse-Fi used a dedicated 80 Hz transmitter and a trained LSTM.
  - Withings is FDA-cleared at ±5 bpm.
- **How many people it can recognise.** Custance reached 97.9 % over 25 people and WifiU measured stranger rejection over 50. Step 10 has 4 slots.
- **Telling people from pets and fans.**
  - ADT's router deployment cut non-human false alarms from 63.1 % to 8.4 % with gait and speed features.
  - Origin and Xfinity claim pet and fan filtering, with no figures.
  - Step 10 does none of this.
- **Maturity.**
  - ESPectre has about 9,700 GitHub stars (9,708 on 2026-10-30; 9,623 on 2026-10-16; 9,395 on 2026-09-25), regular releases and ESPHome, MQTT, Home Assistant and Matter integrations.
  - Since 3.0.0 (2026-10-07) ESPectre is dual-licensed: "GPLv3 or commercial. The ESPHome frontend stays GPLv3-only" (its 3.0.0 release notes). This project has no licence, so ESPectre code can be read but not copied unless the project adopts GPLv3 or buys the commercial licence.
  - Its performance report says of its holdout: "The holdout was inspected during development, so it is a regression set, not a sealed test" (read 2026-10-30). Its C5 F1 figures (§1) are therefore development figures as well as host replays.
  - Espressif maintains its own stack.
  - This project has Step 10 uncommitted, no remote, no licence and no MQTT or Home Assistant support.
- **Stored data.** The per-person health log sits in **unencrypted NVS**, and `forget` is not a secure wipe (the README says so itself).

---

## 6. Sources

**This project**
- README / REFERENCES: `D:\ESP32C5_CSI\README.md`, `D:\ESP32C5_CSI\docs\REFERENCES.md`
- Step 10 code: `D:\ESP32C5_CSI\step10_recognise_count_vitals\main\` (`person_vitals.h`, `main.c:408-411`, `vitals.h`)
- C5 12-bit L-LTF format (esp-idf #18118) → https://github.com/espressif/esp-idf/issues/18118

**Open-source projects**
- ESPectre performance → https://raw.githubusercontent.com/francescopace/espectre/main/docs/performance/README.md
- ESPectre dataset / README / CSI notes → https://raw.githubusercontent.com/francescopace/espectre/main/data/dataset_info.json · https://github.com/francescopace/espectre · https://raw.githubusercontent.com/francescopace/espectre/main/docs/CSI.md
- Espressif esp-csi / esp_wifi_sensing / esp_radar_motion_dec → https://github.com/espressif/esp-csi · https://components.espressif.com/components/espressif/esp_wifi_sensing · https://components.espressif.com/api/components/espressif/esp_radar_motion_dec
- PeterkoCZ91 fork → https://raw.githubusercontent.com/PeterkoCZ91/esphome-wifi-csi/master/README.md
- RuView README / count benchmark / ADR-293 / issue #1660 → https://github.com/ruvnet/RuView · https://raw.githubusercontent.com/ruvnet/RuView/main/docs/benchmarks/person-count-cog.md · https://raw.githubusercontent.com/ruvnet/RuView/main/docs/adr/ADR-293-vitals-ground-truth-rig.md · https://github.com/ruvnet/RuView/issues/1660
- esp-csi-rs C5 rate → https://raw.githubusercontent.com/csi-rs/esp-csi-rs/main/docs/emitter-support.md
- nickbild/csi_hr → https://github.com/nickbild/csi_hr

**Papers**
- Gaiba 2024 → https://doi.org/10.1109/CCNC51664.2024.10454655
- Custance T-BIOM 2026 → https://pure.hud.ac.uk/en/publications/transformer-network-based-gait-identification-using-wifi/
- Custance multi-person → https://arxiv.org/abs/2601.02177 · WiFlow → https://arxiv.org/abs/2601.02203
- Avola 2025 → https://arxiv.org/abs/2507.12854 (DMS 2025, DOI 10.18293/dms2025-004)
- WifiU → https://cs.nju.edu.cn/ww/papers/ubicomp16_gait.pdf
- WiWho → https://api.openalex.org/works/doi:10.1109/IPSN.2016.7460727
- Wi-CaL → https://catalog.lib.kyushu-u.ac.jp/opac_download_md/7161171/7161171.pdf
- Wi-Monitor → https://link.springer.com/chapter/10.1007/978-3-031-20936-9_11
- Pulse-Fi → https://inrg.engineering.ucsc.edu/files/2025/05/Pulse_Fi_short_paper_iotdcoss.pdf · https://arxiv.org/abs/2510.24744
- COVID-Beat → https://academic.oup.com/jcde/article/9/3/992/6590610
- PhaseBeat → https://www.eng.auburn.edu/~szm0001/papers/PhaseBeat_ACMHealth20.pdf
- TensorBeat → https://arxiv.org/abs/1702.02046
- DeMan → https://tns.thss.tsinghua.edu.cn/~cswu/papers/JSAC15_Deman_paper.pdf
- ADT / Zhu deployment → https://arxiv.org/abs/2506.04322
- Toften 2024 (breathing ±3 convention) → https://pmc.ncbi.nlm.nih.gov/articles/PMC11631942/

**Products and reference devices**
- Xfinity WiFi Motion → https://www.xfinity.com/support/articles/wifi-motion-faqs · https://www.xfinity.com/hub/smart-home/wifi-motion · https://finance.yahoo.com/technology/ai/articles/comcast-launches-xfinity-shield-redefining-133000869.html
- Origin AI → https://www.originwirelessai.com/wifi-sensing/
- Aqara FP2 → https://us.aqara.com/products/presence-sensor-fp2 · https://phuongviethcm.com/wp-content/uploads/2025/10/20250925-Specification-Aqara-Cb-FP2.pdf
- HLK-LD2410C / LD2450 → https://www.hlktech.net/index.php?id=1095 · https://www.hlktech.net/index.php?id=1157 · https://www.tinytronics.nl/product_files/006000_HLK-LD2450-Instruction-Manual.pdf
- Seeed MR60BHA2 → https://files.seeedstudio.com/wiki/mmwave-for-xiao/mr60/datasheet/MR60BHA2_Breathing_and_Heartbeat_Module.pdf · https://forum.seeedstudio.com/t/benchmark-results-for-mr60bha2-with-esp32c6-vs-polar-h10-heartbeat-accuracy-concerns/292559
- TI IWR6843 (TIDUE71D) → https://www.ti.com/lit/ug/tidue71d/tidue71d.pdf
- Nest Hub Soli HR research → https://pmc.ncbi.nlm.nih.gov/articles/PMC10590449/
- Withings Sleep Rx → https://www.accessdata.fda.gov/cdrh_docs/pdf23/K231667.pdf · https://pubmed.ncbi.nlm.nih.gov/33590821/
- Polar H10 → https://link.springer.com/article/10.1007/s00421-019-04142-5
- Apple Watch meta-analysis → https://www.nature.com/articles/s41746-025-02238-1

**Standards and regulation**
- IEEE 802.11bf-2025 → https://standards.ieee.org/ieee/802.11bf/11574/
- TGbf KPI sheet → https://mentor.ieee.org/802.11/dcn/20/11-20-1712-02-00bf-wifi-sensing-use-cases.xlsx
- EC13 bar (quoted) → https://www.frontiersin.org/journals/physiology/articles/10.3389/fphys.2025.1638809/full
- CTA-2065-A → https://shop.cta.tech/products/physical-activity-monitoring-for-heart-rate-ansi-cta-2065-a
- FDA pulse oximeter guidance 2013 → https://www.fda.gov/media/72470/download · ISO 80601-2-61:2026 → https://www.iso.org/standard/84595.html
- ISO/IEC 19795-1:2021 → https://cdn.standards.iteh.ai/samples/73515/544e07f312664b1da1a84368dca02899/ISO-IEC-19795-1-2021.pdf
- IEC 63180 → https://webstore.iec.ch/en/publication/61559 · NEMA WD 7 → https://www.cooperlighting.com/global/controls-wiring/wd7-occupancy-sensor-testing
- FDA General Wellness 2026 → https://www.fda.gov/media/90652/download
- WBA test methodology → https://wballiance.com/wi-fi-sensing-test-methodology-and-performance-metrics/ · Wi-Fi Alliance → https://www.wi-fi.org/current-work-areas

**Figures left out on purpose**
- Gaiba "88.9–94.5 %": it belongs to FreeSense (arXiv 1608.03430).
- MR60BHA2 "85 %": not on any page that was actually opened.
- LD2450 "0.15 m / 2–20°": from an Amazon reseller listing, and it conflicts with Hi-Link's manual.
- RuView "100 % presence": retracted by RuView.
- Pulse-Fi "118 participants": that is the other (non-ESP32) dataset.
- "~290/s on C5": that figure was measured on an S3.
- ISO 80601-2-55 "±1 breath/min": a device spec in K192488, not the standard.

---

## 7. Survey of 2026-10-16: current videos and projects

A research pass on 2026-10-16 (DECISIONS D-173). **The videos were not
watched**: every video claim below comes from its title, description, date
and view count. Repository claims come from their READMEs. Nothing here
was re-checked by a second reader.

**What the evidence supports, in one line:** single-node motion on the chip
is real; still presence on a single node is unshown; on-chip breathing has
no published accuracy; heart rate, counting and activity classes have so far
needed several devices or a dedicated sender, a host or NPU, and labelled
training.

- **Motion on one node, on the chip** (the same class as Step 08):
  ESPectre (GPL-3.0) is motion-only by its own README, which says it "does
  not identify people, count them, prove that a room is empty"; its own
  roadmap lists stationary presence and breathing as "Research-gated". Its
  C5 figures are host replays (§5). The video tutorials found (Github
  Awesome, Blueprint Bytes, Adafruit's Arduino port, hometechfun, Alex
  WysiWyg) build motion detectors with ESPectre. DrWalls is motion-only.
  Espressif's `esp_wifi_sensing` does motion and presence with on-site
  training; Espressif's 2025 video claims motion, environmental sensing and
  positioning, with no published accuracy. Tommy (closed source, no
  accuracy published) needs at least 2 devices per zone.
- **Breathing:** the only single-node, on-chip version found is the
  PeterkoCZ91 ESPectre fork, whose bandpass coefficients "assume ~100
  pkt/s"; no accuracy published.
- **Heart rate:** nickbild/csi_hr uses 2 ESP32s, a dedicated sender, an
  LSTM on a PC and a MAX30102 reference; Pulse-Fi's 0.15 s per inference on
  an ESP32 is arithmetic from operation counts, not a measurement. No
  source shows heart rate from one single-antenna node, so `hr_bpm_exp`
  stays experimental.
- **Counting:** RuView reports 62.3 % on 0 vs 1 person (§2B). A 9-node
  ESP32-C3 home study scored AUC 0.489 for a full 72-link mesh and 0.541 for
  one well-placed link, both near chance (arXiv 2602.10823). **Superseded
  (checked 2026-10-30):** the same group's repository now accompanies *More
  Links, Same Airtime* (Khamaisi, Azizi, Rodrigues, U. St. Gallen), which
  "revisits that question at equal airtime and with sound evaluation, and
  finds the opposite", and says why the preprint's apartment recording is not
  used (github.com/sensing-group/more-links-same-airtime README; data
  Zenodo 10.5281/zenodo.23574119, CC-BY-4.0). Both concern presence on a
  multi-link mesh, not counting on one node. No single-node
  ESP32 counter with published accuracy was found.
- **Activity:** §2D. Everything found needs labelled training data, and
  none runs on a C5.
- **Identity and pose:** WhoFi uses routers with 3 receive antennas, not an
  ESP32. RuView's own README puts its live single-ESP32 pose model at
  PCK@20 = 3.0 % and says single-antenna CSI "does not carry" that
  information. Jeija's ESPARGOS uses a phase-synchronised 4 × 2 antenna
  array, which one C5 cannot do.
- **Red flags:** videos that repeat RuView's claims of seeing through walls
  or measuring vital signs from drones (Billy Coder, Repo_AI_Review, ATL
  PPJSV, from their titles and descriptions). Two other creators' video
  descriptions call RuView a project that did not work or was fake (not
  seen on camera).

**A different modality: the radar build the user pointed to.** "Build an
ESP32 Human Tracker with Radar, Thermal Sensing & Vital Signs", How To
Electronics, published 2026-10-08, about 101,500 views
(<https://www.youtube.com/watch?v=OezHKAKaDfQ>; article
<https://how2electronics.com/diy-human-tracking-vital-signs-monitor-esp32-c1001-amg8833/>).
Read from its title, description and the article on 2026-10-16; the claims
are the maker's. **It is not Wi-Fi CSI.**

| | |
|---|---|
| Hardware | classic ESP32 + DFRobot C1001 (SEN0623) 60 GHz mmWave radar over UART 115200 8N1 (5 V supply, 3.3 V logic) + AMG8833 8 × 8 thermal sensor (I2C 0x69, about 60°, about 10 fps, used for the tracker only) + OLED |
| Who does the sensing | the radar module: presence, distance and position, heart rate, breathing and sleep stage ("The module performs the radar processing internally"; "The ESP32 displays the values supplied by the module") |
| Output | UDP 10 times a second to a Python standard-library web dashboard; position about every 2 s |
| Limits stated | chest window about 0.4-1.5 m; module firmware G60SM1SYv010109; not for diagnosis; **no accuracy published** |
| Openness | the radar's serial protocol is not publicly documented (DFRobot forum); DFRobot publishes an Arduino library, <https://github.com/DFRobot/DFRobot_HumanDetection> |

What it means here: nothing to port, because the module does the work this
project does in software. It is a candidate **reference sensor** for the
TESTING.md §4 sessions (an independent occupancy record, a breathing
reference), not ground truth: its own accuracy is unpublished. An option,
not a plan (TESTING.md §4 Phase 1 notes; DECISIONS D-173). It belongs with
the other radar rows of §1 (still people seen directly, vital signs on the
module), not with the Wi-Fi rows.

**Sources added 2026-10-16:** ESPectre performance report (§6, "ESPectre
performance"; last update 2026-10-07) and roadmap
<https://github.com/francescopace/espectre> (`docs/ROADMAP.md`);
Armenta-Garcia et al., Sensors 2025;25(19):6220; STAR
<https://arxiv.org/abs/2510.26148>; 9-node ESP32-C3 study
<https://arxiv.org/abs/2602.10823>; the How To Electronics video and article
above.

**Checked 2026-10-30** (research sweep, `~/ESP32C5_CSI_logs/2026-10-30_research/REPORT.md`
on the Ubuntu side, outside the repo; DECISIONS D-174): the 9-node study above is
superseded by its authors; ESPectre 3.0.0 is dual-licensed and its holdout is
"a regression set, not a sealed test". The sweep found no single-node source
with a real-world accuracy for heart rate, counting or activity classes, so
the one-line verdict above stands.
