# Presence, people counting and multi-person heart rate: products, technology, installation and evidence (research update 2026-11-16)

Project: ESP32-C5 Wi-Fi CSI human-presence detector (`/mnt/vol1/ESP32C5_CSI`). This report updates and extends `docs/COMPARISON.md` and `~/ESP32C5_CSI_logs/2026-10-30_research/REPORT.md`. It is a synthesis only. Every fact comes from the verified lane notes in this folder, and each row keeps its source link:

- `wifi_sensing.md`, `mmwave_radar.md`, `counting_sensors.md`, `contact_vitals.md`, `theory_papers.md`, `videos.md`, `install_standards.md`, `market_price.md`.
- Gap passes: `gap_1_alt_tech_nonrf_tags.md`, `gap_2_asia_india_regional.md`, `gap_3_multi_person_hr_adversarial.md`, `gap_4_install_india_reg_conflicts.md`.
- One project finding from the board: `~/ESP32C5_CSI_logs/2026-11-05_hw/`.

**Conventions used in this report**

- **Who makes each claim:**
  - **vendor**: the maker's own page, datasheet or press release.
  - **independent**: a review or test by a third party.
  - **peer-reviewed**: a journal or conference paper.
  - **preprint**: not peer-reviewed.
  - **maker**: a forum, hobby or open-source author.
  - **regulator**: an FDA, FCC or WPC filing.
  - Vendor accuracy is a claim, not a measurement.
- **Missing data:** "not found" means the lane searched and found nothing; it is never a guess. "unconfirmed" means a source was seen once (often a search snippet) but could not be re-opened.
- **Prices** carry their currency, the seller and the date read. INR conversions use **94.40 INR/USD (2026-11-16)** unless marked "@93.93" (the wifi_sensing lane's rate of 2026-11-08). GST status is stated where the seller states it.
- **Capability vocabulary:**
  - "presence": someone is there.
  - "counts (max N)".
  - "positions": coordinates or zones.
  - "BR": breathing rate.
  - "HR-1": heart rate for one person.
  - "HR-multi (N)": heart rate for N people at the same time from one sensor.
- **Search coverage:** the shared web-search budget ran out in every lane. Much of the checking was done by fetching known URLs directly. Section 10 lists what this left unverified.

## Contents

1. Executive summary
2. Ready-product comparison
3. Technology comparison
4. Correct installation process (per technology, plus this project)
5. Theory and research
6. Videos worldwide
7. Market and price
8. Standards, regulation and privacy
9. Recommendations for this project
10. Evidence limits

---

## 1. Executive summary

1. **No product publishes both a person count and an accuracy for heart rate (HR) of 2 or more people at once from one sensor.** Every vitals product whose datasheet, manual or FDA filing was read is one person per sensor, bed or side:
   - Radar modules: Seeed MR60BHA2 ("only supports one person"), Hi-Link LD6002 ("Maximum Number of Tests: 1"), MinewSemi ME72MS02 (max targets 1), Aqara FP2 Sleep mode ("designed for single-person use").
   - FDA-cleared contactless monitors: Xandar Kardian XK300 and Neteera 130H-Plus ("a patient", bed-centred), Sleepiz One+ (one adult per device).
   - Camera rPPG: FaceHeart ("only one face"), Presage ("a single human face"), Oxehealth ("single occupancy rooms").
   - Bed sensors: Emfit ("impossible ... to tell whose heart is beating"), Withings, Dozee.
   - Evidence level: primary documents (vendor and regulator). See §2.2 and §2.3.
2. **The only product-level multi-person HR statements are unquantified claims:**
   - Sakura Tech miRadar8 VSM, a 24 GHz MIMO radar. Its Japanese catalogue says it measures the heartbeat and breathing of several people at once, within 5 m and ±45°. It gives no count, accuracy or price, and a 2026-10-29 site notice calls miRadar8 "開発中" (under development).
   - Harman/CAARESYS (2022): HR/HRV of "each vehicle occupant", with no count or accuracy.
   - TI's 2019 in-car demo: driver and passenger HR together, with no HR accuracy.
   - MotionCatch SE: detects up to 8 people, but gives HR only as a "reference value".
   - Millilab (2023 video): "vital signs of four people", without naming HR.
3. **Research does measure multi-person HR, always by first separating people in space:**
   - Radar:
     - Sumi & Sakamoto: 16 people with two 79 GHz radars, HR MAE 3.69 bpm, 87.5 % of estimates within 10 % (preprint).
     - Tan: 4 people, MAE 2.3 bpm (preprint).
     - Eder: 3 people, 87 % of HR within 2 bpm (preprint).
     - TU Delft MSc: 4 people on 60 GHz, about 10-19 % error.
     - Benny et al. (IEEE Sensors J 2025): separates 5 people, but reports no 5-person HR accuracy.
   - Wi-Fi: SpaceBeat (ACM IMWUT 2024), 3 people at 95.2 % (one minus relative error), but it needs about 18 receive antennas built from Intel 5300 cards.
   - Acoustic: Tran 2024 (Sensors) handles up to 4 people with a median error of 0.66 bpm for two side by side; DF-Sense (IEEE TMC 2025) handles 5-6 people at 1.19 bpm or less.
   - **No multi-person HR result on ESP32-class hardware was found.**
4. **Physics explains why.** At 20 MHz, one Wi-Fi range bin is 7.5 m, larger than the room (ViMo 2020; Lambrecht 2026). One antenna gives no angle, and one mixture is too few for blind-source separation (MultiSense needs 3 antennas). A single-antenna ESP32-C5 therefore sees all chests summed into one signal. Withholding HR when 2 or more people are present (the Step 10 design) is consistent with the whole field.
5. **No shipping Wi-Fi CSI product counts people.** About 15 were checked: Xfinity, Cognitive, Origin/ADT, Plume, nami, WiZ, Hue (which is actually Zigbee RSSI), Huawei, TOMMY, ESPectre and others. All do motion or presence only. Office "Wi-Fi people counting" (VergeSense, Meraki) counts phones and laptops, not bodies.
6. **Wi-Fi counting results need more links or antennas than one ESP32 and a router:**
   - Wi-CaL: 4 ESP32 links give a median error of 0.35-0.41 people; one link alone gives 0.55-0.82.
   - Bus counting: 2 receivers reach 94.86 %, one reaches 77-90 %.
   - Torun/Mostofi: MAE 1.04 for up to 13 seated people with a 3-antenna Intel 5300.
   - Few-shot counting in a new room needs multi-antenna cards (Cha et al.; DASECount).
   - **A second C5 receiver is the cheapest counting lever.**
7. **Shipping counters are radar, ToF, stereo, thermal or AI camera, and every accuracy figure is a vendor claim (95-99.9 %).** No independent test of any named product was found. Consumer ceilings:
   - LD2450: 3 moving targets.
   - Aqara FP2: up to 5, best with 3 or fewer.
   - Aqara FP400: up to 10 (vendor).
   - Density Waffle: reports classes 0/1/2/3+, the same coarse classes as this project's 0/1/2+, for US$279 plus subscription.

   Density's "Time Within Tolerance" score is a ready-made honest metric: exact for 1-3 people, ±1 for 4-7, ±2 for 8+, target at least 90 %.
8. **Single-person contactless HR is weak in cheap modules and good only in cleared devices:**
   - DFRobot C1001 read 8-17 bpm low in DFRobot's own test.
   - MR60BHA2 scored r ≈ 0.33 against a Polar H10 in one user's test.
   - Cleared devices sit at about 3-5 bpm, for one person at rest.
   - The cheap, credible reference is one Polar H10 chest strap per person (₹8,999, Polar India). Apple's 2026 white paper and Eight Sleep both use it.
9. **Project finding, 2026-11-05 (one session, one 11ax AP):** an access point that beamforms to the C5 leaves the shipped CSI configuration (`acquire_csi_beamformed = 0`) with about 0-2 CSI per 10 s.
   - Either disabling the board's beamformee support or setting `acquire_csi_beamformed = 1` restores about 450-480 per 10 s.
   - The `CSI_LLTF_ONLY` configuration got 0 CSI on that AP in both runs.
   - This is not yet recorded in TESTING or DECISIONS (see §4.2).
10. **Placement matters more than the algorithm, per the papers that measured it:**
    - A transmitter 1-1.5 m from a wall added 2.1-11.2 % counting accuracy.
    - Breathing detection was 98.8 % at good Fresnel positions against 61.5 % at bad ones.
    - One well-placed ESP32 link beat a 72-link mesh (preprint).
    - Vendors agree on at least 2 nodes per area, the walking route crossing the link, an empty room during calibration, and no fans, curtains or metal in the beam.
11. **India regulation:**
    - Only 61-61.5 GHz is licence-exempt for 60 GHz devices. The DFRobot C1001 (61-61.5 GHz) fits on paper; the MR60BHA2 (58-62 GHz) does not; the FP2's frequency was not found.
    - TRAI recommended opening 57-66 GHz on 2025-12-10, but no DoT notification was found.
    - Every wireless model needs a WPC ETA (about ₹10,000, secondary source).
    - DPDP Rules: Rule 4 has been in force since about 13/14 Nov 2026, and the main duties apply from about 13/14 May 2027 (read from the gazette text).
12. **Videos:**
    - The first popular ESP32-C5 CSI build appeared on 2026-11-15 (Talking Sasquach). It reports about 49 CSI/s on a busy 5 GHz mesh against about 1/s from 2.4 GHz beacons; HR is "not verified" and there is "no head count".
    - No video found shows HR for 2 or more people against a reference.
    - Viral Wi-Fi HR and through-wall counting claims (RuSense; a "98.5 %" ESP8266 claim) are unreplicated.
13. **What it means for the project:**
    - Claims that are safe after measurement: presence; coarse 0/1/2+ counting scored with a tolerance metric; and possibly single-person breathing.
    - HR stays experimental and single-person, and must never be multi-person on one C5 link.
    - Any multi-person vitals product would need MIMO radar, or one sensor per person or bed, beside the C5.
    - Nothing of this is measured on the C5 yet. Step 08 has never run, and no accuracy or occupancy log exists (CLAUDE.md status).

---

## 2. Ready-product comparison

Columns: presence / counts (max) / positions / BR / HR (single or multi, how many) / range / price USD / price INR / who claims / link. "—" means the product does not offer that function. Split by category so the tables stay readable.

### 2.1 Wi-Fi and RF-mesh sensing products

| Product | Technology | Presence | Counts (max) | Positions | BR | HR | Range | Price USD | Price INR | Claim | Link |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Xfinity WiFi Motion (Xfinity Shield) | Wi-Fi CSI, ISP gateway + up to 5 client devices | motion only; small pets detected at default sensitivity | no | oval areas between gateway and devices | no | no | home; gateway "a couple of feet off the floor" | included with WiFi Shield; Shield Select US$15/mo (xfinity.com hub, 2026-11-16) | not sold in India | vendor | [FAQ](https://www.xfinity.com/support/articles/wifi-motion-faqs), [hub](https://www.xfinity.com/hub/smart-home/wifi-motion) |
| Origin AI TruShield / TruPresence (ADT, US$170 M acquisition 2026-02-24) | Wi-Fi CSI, licensed to ISPs and SoCs | ">99 %" human vs non-human in 20 s | "counting people in hidden spaces", no number | "unlimited number of subjects" claim, no data | "better than one breathe per minute" | HR/HRV engine listed, no numbers; multi-person not claimed | ">10 m" | B2B, not found | not found | vendor | [white paper](https://www.originwirelessai.com/wp-content/uploads/2025/05/Origin-AI_Wifi-Sensing-White-Paper-2025-05-16-PROOF.pdf), [ADT](https://newsroom.adt.com/corporate-news/adt-acquires-origin-ai-to-power-ai-sensing-and-ambient-intelligence-for-the-home) |
| Caregiver by Cognitive (launched 2026-06-16) | Wi-Fi CSI via gateway + 4 CarePod plugs | activity, inactivity, sleep interruptions | no | no | no | no | home | not listed (agency waitlist) | not found | vendor | [page](https://www.cognitivesystems.com/caregiver/) |
| Hex Home | Wi-Fi motion (Command + Sense plugs) | motion | no | no | no | no | 1,000-2,000 sq ft kits | US$180 / US$230 (2021); current availability not found | ≈₹16,907 / ₹21,604 @93.93 (2021 prices) | independent | [Gizmodo](https://gizmodo.com/hex-home-security-is-a-supercharged-motion-detector-tha-1847495877) |
| Linksys Aware | Wi-Fi mesh motion | motion | no | no | no | no | Velop mesh | **discontinued** (end of life 2024-04-01) | — | vendor | [EOL page](https://www.linksys.com/pages/linksys-product-end-of-life) |
| Plume Sense / WorkPass Flow | Wi-Fi motion on SuperPods | motion; "presence detection" for small business | no ("no specific recognition of people") | no | no | no | home / small business | through ISP; quote | not found | mixed | [datasheet](https://cdn.plume.com/a3/d5/5fc58de24a6cb38dc335becbd5a8/datasheet-workpass-en-032526.pdf) |
| nami Agile Security / Aware (Alarm.com) | Wi-Fi motion between plugs + Thread sensors | activity level per area | no | area | no | no | per area | not found | not found | mixed (dealer forum) | [Surety forum](https://support.suretyhome.com/t/what-is-the-difference-between-nami-wi-fi-motion-sensing-and-pir-motion-sensors/50324) |
| WiZ SpaceSense | Wi-Fi motion between WiZ lights | motion | no | no | no | no | one room | not found | Neo downlight ₹1,740 (10 W) / ₹1,990 (15 W) incl. tax, Philips India, 2026-11-16 | vendor | [FAQ](https://faq.wizconnected.com/hc/en/7-wiz-v2/section/144-automation-spacesense), [store](https://in.shop.lighting.philips.com/products/philips-smart-wifi-neo-led-downlight) |
| Philips Hue MotionAware | **Zigbee RSSI** (Ivani sensify), not Wi-Fi | motion | no | no | no | no | 3-4 lights per area | Bridge Pro US$98.99 at launch, later US$139.99 (search summary) | ≈₹13,215 from US$139.99 | vendor | [Hue support](https://www.philips-hue.com/en-ca/support/article/motionawaretm--transform-your-hue-lights-into-motion-sensors/000011) |
| Huawei Wi-Fi 7 AP sensing (hotel/retail) | Wi-Fi 7 CSI on enterprise APs | which room is occupied | not described | room level | "research stage" | no | hallway AP covering rooms | not found | not found | vendor | [Techzine 2026-11-09](https://www.techzine.eu/blogs/infrastructure/145633/how-huawei-is-reinventing-enterprise-networking-with-ai/) |
| Huawei 星光F60 FTTR "Wi-Fi雷达"; China Telecom + Huawei FTTR+X | Wi-Fi CSI in home FTTR gateways (China) | presence, inactivity alerts | not stated | no | FTTR+X lists "呼吸 / 心跳 AI 感知" | listed, no count or accuracy; Huawei's HR claim relies on a paired mmWave radar | home | not found | not found | vendor | [c114](https://m.c114.com.cn/w126-1318853.html), [ithome](https://www.ithome.com/0/903/649.htm), [sspai](https://sspai.com/post/100553) |
| TOMMY Zero / One | ESP32 CSI zones, server-side | motion; still presence from breathing (C5 only, per wifi lane; **C5 support not found** by the install lane) | no | zone | used only for presence | no | ≥2 boards per zone | trial; price not listed | not found | vendor | [llms.txt](https://www.tommysense.com/llms.txt) |
| ESPectre 3.2.0 (2026-11-11) | ESP32 CSI motion (incl. C5), ESPHome/Matter/Arduino | motion | no ("does not ... count them") | no | no | no | one board per room | free (GPLv3) or commercial licence | board only | maker | [README](https://raw.githubusercontent.com/francescopace/espectre/main/README.md) |
| RuView | ESP32-S3/C6 CSI + host models | claimed | measured 62.3 % on 0 vs 1 only; "~3-5 people" is an estimate | pose claims | 6-30/min, "unvalidated" | 40-120 bpm, "unvalidated" | ~5 m | S3 node ~US$9; mesh ~US$54 | ≈₹845 / ₹5,072 @93.93 | maker | [repo](https://github.com/ruvnet/RuView) |
| VergeSense Wi-Fi analytics | **counts Wi-Fi devices** (not CSI) | device-based | "85 %" by building or floor; zones > 5,000 sq ft | zone | no | no | building | not found | not found | vendor | [PDF](https://www.vergesense.com/hubfs/VergeSense%20WiFi%20Analytics%20Overview.pdf) |
| **This project: ESP32-C5 node** | Wi-Fi CSI, single link, 20 MHz L-LTF | not yet measured | experimental 0/1/2+, not measured | no | experimental | experimental `hr_bpm_exp`, one person only, not validated | not established | ≈US$15 (AliExpress, 2025) | DevKitC-1-N8R8 ₹1,426.65 (DigiKey.in, GST not stated, 267 in stock); N8R4 ₹1,290 + 18 % GST ≈ ₹1,522 (CompoIndia, out of stock); XIAO ESP32-C5 ₹695 ex GST (Evelta) — all 2026-11-16 | maker (own) | [DigiKey.in](https://www.digikey.in/en/products/result?keywords=ESP32-C5-DEVKITC-1), [CompoIndia](https://compoindia.com/product/espressif-esp32-c5-devkitc-1-n8r4-development-board/) |

*Also in the lane notes but not tabled here:* Ivani sensify (the engine inside Hue), Cognitive Systems platform, Infineon ACW741x and Qualcomm Dragonwing (chips), Nexmon/PicoScenes/Intel 5300 CSI tools — see `wifi_sensing.md`.

### 2.2 Radar products and modules (24 / 60 / 77-79 / 122 GHz, UWB)

| Product | Technology | Presence | Counts (max) | Positions | BR | HR | Range | Price USD | Price INR | Claim | Link |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Aqara Presence Sensor FP2 | 60 GHz FMCW (exact band not found) | yes, including still people | 5 "in theory", best ≤3 (spec sheet) | 30 zones, 0.5 m cells | used only to detect presence; no rate output in the manual | Sleep mode, single person, **press-reported only** (Dec 2023); no accuracy | 40 m² room; 15 m spec | US$82.99 (us.aqara.com 2026-11-16) | ₹11,777 Amazon.in (price-history.in; 12-month low ₹7,999) | vendor | [spec sheet](https://phuongviethcm.com/wp-content/uploads/2025/10/20250925-Specification-Aqara-Cb-FP2.pdf), [manual](https://www.123led.nl/pdf/LAQ00045.pdf) |
| Aqara Spatial Multi-Sensor FP400 | 60 GHz 4T4R, Zigbee/Thread/Matter | yes, including still | "up to 10 people", 8 zones (vendor); no measured accuracy | 0.5 m grid | no | no | side 10 × 8 m; ceiling 3-4 m | US$79.99 (us.aqara.com 2026-11-16) | not found | vendor | [EU page](https://eu.aqara.com/products/spatial-multi-sensor-fp400) |
| Aqara FP300 | PIR + mmWave, battery, Thread | yes | not found | not found | no | no | not confirmed | US$46.99 Amazon US (matteralpha 2026-11-15) | not found | vendor | [matteralpha](https://www.matteralpha.com/aqara/aqara-presence-multi-sensor-fp300-p2752) |
| Hi-Link HLK-LD2410 / C | 24 GHz 1T1R | moving + still | 1 | distance gates | still presence only, no rate | no | ~5-6 m | US$2.46 (COMPARISON.md, not re-checked); LD2410C "under $10" (XDA) | Robu ₹379 (unconfirmed, 403) | vendor + independent | [manual](https://www.elemon.com.ar/images/industry_news/11-HLK-LD2410-user-manual-V1.02.pdf), [XDA](https://www.xda-developers.com/presence-sensors-cost-80-but-esp32-mmwave-radar-detect-your-breathing/) |
| Hi-Link HLK-LD2450 | 24 GHz 1T2R | moving targets | 3 (integrators and reseller; the vendor page gives no maximum) | X/Y + speed | no | no | 6 m, ±60° | US$4.46 (COMPARISON.md) | ₹999 incl. GST, in stock (Probots 2026-11-16) | vendor + integrator | [manual](https://www.elemon.com.ar/images/productos/hojas-de-datos/radar-modulos/hlk-ld2450.pdf), [Probots](https://probots.co.in/hlk-ld2450-24ghz-mmwave-tracking-radar-sensor.html) |
| Hi-Link HLK-LD2460 | 24 GHz 2T4R | moving + static | ≥3 sitting/standing, up to 5 moving (vendor manual) | X/Y, ≤0.3 m distance error | no | no | moving ≥6 m, static ≥5 m | not found | not found | vendor | [manual](https://revspace.nl/images/9/9d/HLK-LD2460_2T4R_Multi_Target_Trajectory_Tracking_Module_Manual_V1.1.pdf) |
| Hi-Link HLK-LD6001 | 60 GHz | yes | 8 (reseller; "10" unconfirmed) | X/Y | no | no | 8 m | US$51.29-59.20, pre-order, sold out | not found | reseller | [openelab](https://openelab.com/products/hi-link-60g-radar-sensor-module) |
| Hi-Link HLK-LD6002 | 60 GHz, ADT6101P | — | **1** ("Maximum Number of Tests: 1") | distance | 9-48/min, "90 %" (undefined) | **HR-1**, 60-150 bpm, "90 %" (undefined), 0.4-1.5 m | 1.5 m | US$16.95 (electropeak 2026-11-16) | IndiaMART ₹1,988-2,540 ex GST (unconfirmed) | reseller | [electropeak](https://electropeak.com/hi-link-ld6002-60g-heart-rate-and-breathing-monitoring-module) |
| Seeed MR60BHA2 kit (XIAO ESP32-C6), 58-62 GHz | 60 GHz FMCW, ADT6101P | static presence to 6 m | firmware 1.6.4: "up to three individuals within 6 m", "reference only"; one user saw the count return only 0 or 1 | per-target x/y (wiki) | single person, ≤1.5 m, "90 %" (undefined) | **HR-1** ("only supports one person"); "90 %" (undefined); one user found r ≈ 0.33 vs Polar H10 | vitals 1.5 m, presence 6 m | US$24.21 (list 26.90), Seeed 2026-11-16 | ₹3,332.65 Fabtolab (in stock, GST basis not stated); ₹4,309 Olelectronics (out of stock); ₹6,999 Probots (out of stock) | vendor + maker | [datasheet](https://files.seeedstudio.com/wiki/mmwave-for-xiao/mr60/datasheet/MR60BHA2_Breathing_and_Heartbeat_Module.pdf), [forum test](https://forum.seeedstudio.com/t/benchmark-results-for-mr60bha2-with-esp32c6-vs-polar-h10-heartbeat-accuracy-concerns/292559) |
| DFRobot C1001 (SEN0623), 61-61.5 GHz | 60 GHz FMCW | yes, to 11 m | no. The FAQ answer was read as "We don't think this sensor can do anything similar" by one lane and as unanswered by another (**conflict**) | no | 10-25/min at 0.4-1.5 m | HR 60-100 bpm, person count not stated; vendor test read 8-17 bpm low (3 trials); phantom vitals for 60 s after the person leaves (forum) | 11 m presence; fall circle ~4 m at 2.7 m | US$29.00 (DFRobot) | DigiKey.in ₹2,758.19 (GST not stated, 77 in stock); Fabtolab ₹2,848.96; Evelta ₹2,980 ex GST / ₹3,516.40 incl.; techiesms ₹3,500 incl. (out of stock) — 2026-11-16 | vendor + maker | [wiki](https://wiki.dfrobot.com/SKU_SEN0623_C1001_mmWave_Human_Detection_Sensor), [vendor test](https://www.dfrobot.com/blog-13919.html), [DigiKey.in](https://www.digikey.in/en/products/detail/dfrobot/SEN0623/24854509) |
| MinewSemi ME72MS02 | 60 GHz FMCW 2T2R | — | **1** (max targets 1) | — | 92 % of samples within ±3 bpm vs ECG, supine (vendor) | **HR-1**: 90 % of samples within ±5 bpm vs ECG, supine (vendor) | 0.3-3 m | not found | not found | vendor | [product page](https://en.minewsemi.com/millimeter-wave-radar/me72ms02) |
| Acconeer A121 / XM125 (breathing app) | 60 GHz pulsed coherent | yes | not found | distance | single, stationary; 4 test cases within ~1 bpm | not in docs | 0.3-1.5 m | XM125 US$11.57-12.08 (not re-read) | not found | vendor | [docs](https://docs.acconeer.com/en/latest/applications/a121/breathing.html) |
| TI IWR6843 people counting (TIDEP-01000) | 60 GHz 3TX/4RX EVM | yes | "up to 20" (vendor claim); vendor test to 6 people: 33-41 % of frames error-free at 4-6 people | 3D tracks, 0.18-0.35 m error | separate lab | stock vitals demo = one track (E2E snippet only, unconfirmed) | 6-14 m | behind login | not found | vendor | [TIDUE71D](https://www.ti.com/lit/ug/tidue71d/tidue71d.pdf), [lab guide](https://dev.ti.com/tirex/explore/content/mmwave_industrial_toolbox_4_11_0/labs/People_Counting/3D_People_Counting/docs/3d_people_counting_user_guide.html) |
| Everything Presence Lite / Pro / One | LD2450 / SEN0609 + PIR, ESPHome | yes | Lite/Pro up to 3 | X/Y | no | no | 6 m (LD2450) | Lite US$39, One US$64 (Apollo wiki, Oct 2026); Pro £67 | not found. Stock **conflict**: Pro "in stock" (mmwave lane) vs "all sold out" (gap 4), both 2026-11-16 | vendor | [shop](https://shop.everythingsmart.io/products/everything-presence-pro) |
| Apollo MSR-2 / MTR-1 | LD2410B / LD2450 | yes | 1 / 3 | MTR-1 X/Y | no | no | 6 m | US$37.99 / US$39.99 (Oct 2026) | not found | vendor | [Apollo comparison](https://wiki.apolloautomation.com/products/general/comparisons) |
| Density Waffle | radar (band not stated) | yes | **0 / 1 / 2 / 3+ classes**; "99 %" occupied vs available (vendor) | not stated | no | no | 18 ft | US$279 + software (density.io/waffle). **Conflict**: the competitor page puts Waffle in the "<$150" tier | not found | vendor | [Waffle](https://www.density.io/waffle), [accuracy method](https://density.io/resources/the-truth-behind-99-accurate-occupancy-sensors) |
| Framery Room Sensor | PIR + mmWave, 4G | yes | up to 7 (vendor) | no | no | no | 7-10 m | EUR 149 + EUR 5.90/space/month | ≈₹15,515 + ₹614/mo (lane conversion) | vendor | [Framery](https://framery.com/en/smart-office-solutions/framery-room-sensor/) |
| SensMax TAC-B 3D-W | 60 GHz radar, Wi-Fi | yes | counting lines; "~99 % at ~1 person/m², >93 % at ~3 persons/m²" (vendor) | vectors | no | no | 10 m, 100 m² | quote | not found | vendor | [SensMax](https://sensmax.eu/devices/sensmax-tac-b-real-time-people-counting-radar-sensor/) |
| Milesight VS373 | 60 GHz 4D, LoRaWAN | yes, bed presence | not specified | point cloud | "reference breathing rate" | no | ceiling 2.3-3 m | US$330 (sold out, unconfirmed) | not found | vendor | [datasheet](https://resource.milesight.com/milesight/iot/document/vs373-datasheet-en.pdf) |
| Milesight VS370 | 24 GHz radar + PIR, LoRaWAN | "up to 99 %" (no conditions) | no | no | no | no | 7.5 m | not found | not found | vendor | [VS370](https://www.milesight.com/iot/product/lorawan-sensor/vs370) |
| Minew MSR01-A | 60 GHz, BLE | yes | 15 tracked (reseller, unconfirmed) | yes | not found | no | not found | not found | not found | reseller | [BeaconZone](https://www.beaconzone.co.uk/blog/new-human-presence-sensors/) |
| Vayyar Care | 4D imaging radar | yes, falls | not stated | yes | no | no | 13 ft ahead × 6 ft 6 in each side | not listed (older US$244 bundle) | not found | vendor | [docs](https://vayyar.com/care-docs/b2c/) |
| Sakura Tech miRadar8 VSM (Japan) | 24 GHz MIMO-FMCW, 8 virtual channels | yes | "multiple targets"; "10 people" only in a 2019 headline | ±45° | multiple (vendor) | **HR-multi CLAIMED**, count not stated, no accuracy; status "開発中" (2026-10-29) | 5 m | price on request | not found | vendor | [catalogue](https://pr.mono.ipros.com/sakuratech/product/detail/2000530489/), [site](https://www.sakuratech.jp/) |
| MotionCatch SE (Japan) | mmWave + AI | yes, bed exit, falls | "最大同時検知8名" (8 at once) | not stated | reference value | "参考値" (reference value); not per person | 24 m² | quote | not found | vendor | [IPROS](https://pr.mono.ipros.com/intercompo/product/detail/2001540441/) |
| JCF Technology McKare (Korea) | microwave radar | activity, falls | — | — | yes | yes, single implied, no numbers | 5 m | not found | not found | vendor | [press](https://view.asiae.co.kr/en/article/2024103013050090484) |
| Infineon 60 GHz in-cabin (ICMS) | 60 GHz radar | child presence, "99 %" (vendor) | seat occupancy (5 seats) | seat | — | **driver only** | car cabin | not found | not found | vendor | [webinar PDF](https://www.infineon.com/dgdl/Infineon-ICMS_Radar_Webinar_Feb_2025_Handout-Webinar-v01_00-EN.pdf?fileId=8ac78c8c93dda25b01953e3b40bc71cc) |
| Harman / CAARESYS VitalCaare | in-cabin radar | seat occupancy | not stated | localisation | yes | "each vehicle occupant", **no count, no accuracy** | car cabin | not found | not found | vendor | [iConnect007](https://iconnect007.com/article/133267/harman-acquires-incabin-radar-sensing-pioneer-caaresys/133270/aep) |

*Also in the lane notes but not tabled here:* SwitchBot Presence Sensor, HLK-LD6004, Seeed MR60FDA2, MicRadar R60ABD1, TI IWRL6432 labs, Infineon BGT60TR13C, Novelda X4/X7, Amazon Halo Rise (discontinued), Everything Presence One details, Socionext SC1240, Kyocera 60 GHz sensor, Fujitsu mmWave monitor, Qinglei bedside radar, bitsensing sleep care, NXP SR250 and in-cabin UWB radars — see `mmwave_radar.md`, `gap_1_alt_tech_nonrf_tags.md`, `gap_2_asia_india_regional.md`, `gap_3_multi_person_hr_adversarial.md`.

### 2.3 Medical-grade and sleep vitals (contactless, bed and camera)

| Product | Technology | Presence | Counts | Positions | BR | HR | Range | Price USD | Price INR | Claim | Link |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Xandar Kardian XK300 (FDA K202464, 2021-04-26) | IR-UWB radar | "a patient" within 7 m | no | no | 6-55/min | HR-1, 60-120 bpm; the 510(k) shows bench tests only, no clinical accuracy; reseller claims ±1.8 bpm | 7 m presence | not found | not found | regulator + vendor | [510(k)](https://www.accessdata.fda.gov/cdrh_docs/pdf20/K202464.pdf) |
| Neteera 130H / 130H-Plus (K212143, K231733) | 122 GHz micro-radar | bed exit | no | no | 5-40/min | HR-1, 40-160 bpm, per "monitored person"; 46-subject study "met criteria", numbers not published | bed-centred | not found | not found | regulator + vendor | [510(k)](https://www.accessdata.fda.gov/cdrh_docs/pdf23/K231733.pdf) |
| Sleepiz One+ (K223163) | CW Doppler radar, bedside | in bed | one adult per device | no | LoA −1.42/+0.97 vs belt | HR-1: 94 % within ±5 bpm, LoA −2.64/+5.82 bpm vs ECG (199 subjects in all) | bedside | not found | not found | regulator | [510(k)](https://www.accessdata.fda.gov/cdrh_docs/pdf22/K223163.pdf) |
| Google Nest Hub 2 (Soli) | 60 GHz FMCW | sleep/wake | one sleeper (ignores the partner) | no | sensed, no accuracy | not shipped | bedside | not found | not found | vendor | [Google blog](https://www.research.google/blog/contactless-sleep-sensing-in-nest-hub/) |
| Withings Sleep Analyzer / Sleep Rx (K231667) | under-mattress BCG | in bed | 1 per mat | no | BR MAE ≤1.6 cpm (independent) | HR-1: Rx ±5 bpm (vendor/FDA); MAE <2.12 bpm vs PSG ECG (independent, peer-reviewed) | one sleeper | not found | ₹19,200-19,700 (unconfirmed, page 403) | mixed | [K231667](https://www.accessdata.fda.gov/cdrh_docs/pdf23/K231667.pdf), [Ravindran 2024](https://www.medrxiv.org/content/10.1101/2023.10.13.23296936) |
| Emfit QS | under-mattress BCG | in bed | 1 per sensor; two people on one sensor = "impossible ... to tell whose heart is beating" | no | yes | HR-1; data loss in 47 % of participants (Kholghi 2022) | one sleeper | US$338 (unconfirmed) | not found | mixed | [FAQ](https://emfit.com/faq/), [Kholghi](https://pmc.ncbi.nlm.nih.gov/articles/PMC8974370) |
| Dozee VS (Bengaluru; FDA K221555) | under-mattress BCG + cloud | in bed, bed exit | 1 per bed | no | yes | HR-1: MAE <3 bpm, LoA within 8.59 bpm, n = 205 (vendor-authored, JMIR Cardio 2026); 200+ hospitals, 15.5K+ beds (vendor) | one bed | not found | not found (2021 press ₹8,000-12,000, not re-checked) | vendor | [510(k) DB](https://www.510kdatabase.net/k221555/), [site](https://dozeehealth.ai/) |
| Eight Sleep Pod / Sleep Number 360 / Sleeptracker-AI | bed sensing, **one sensor or zone per side** | in bed | 2 sides | no | yes | per side: Eight Sleep MAE <1 bpm (vendor); Sleep Number epoch r = 0.81 (peer-reviewed); no two-sleeper analysis | bed | not found | not found | mixed | [Eight Sleep](https://www.eightsleep.com/blog/hrv-accuracy), [Sleeptracker poster](https://sleeptracker.com/assets/ai-validation/2025-world-sleep-hrv-validation.pdf) |
| FaceHeart FH Vitals (K223622), NuraLogix Anura (K253650), Presage SmartSpectra (K254169), Oxehealth (DEN200019) | camera rPPG | face in view | **one face / single-occupancy room** | — | RR cleared for some (e.g. Presage LoA ±2.2 br/min) | HR-1: FaceHeart 93.8 % within ±3 bpm (n = 527); NuraLogix RMSD 2.5 bpm (n = 57); Presage RMSE 1.73/1.77 bpm; Oxehealth labelled 3 bpm spec | 0.2-1.5 m | Presage free for institutions; others not found | not found | regulator (sponsor data) | [K223622](https://www.accessdata.fda.gov/cdrh_docs/pdf22/K223622.pdf), [K253650](https://www.accessdata.fda.gov/cdrh_docs/pdf25/K253650.pdf), [K254169](https://www.accessdata.fda.gov/cdrh_docs/pdf25/K254169.pdf) |
| Polar H10 (reference strap) | chest-strap ECG, BLE (raw ECG 130 Hz) | — | 1 per wearer | — | not validated | HR-1 reference; RR quality 99.6 % vs Holter (n = 10, peer-reviewed) | worn | US$89.95-104.95 (unconfirmed, 429) | ₹8,999 Polar India (only listed variant out of stock); ₹8,999 WOD Armour, 2026-11-16 | mixed | [SDK](https://github.com/polarofficial/polar-ble-sdk/blob/master/documentation/products/PolarH10.md), [Polar India](https://store-in.polar.com/products/polar-h10-1) |
| Oura Ring 4 / WHOOP 4.0 / Apple Watch S12 | wrist or finger PPG | — | 1 per wearer | — | — | HR-1; nocturnal resting HR MAPE 1.94 % Oura 4, 3.00 % WHOOP 4.0 (peer-reviewed); Apple S12 white paper relative only (vendor) | worn | not found | Oura ₹28,900 + ₹599/mo (Digit 2026-03); WHOOP 4.0 ₹19,999 (Flipkart 2026-11-16) | mixed | [Digit](https://www.digit.in/news/wearable-devices/oura-ring-4-debuts-in-india-with-50-health-metrics-starts-at-rs-28900.html), [the5krunner](https://the5krunner.com/2026/09/10/apple-watch-series-12-heart-rate-white-paper/) |

*Also in the lane notes but not tabled here:* Hillrom/Baxter EarlySense, Murata SCA11H, Philips VitalSigns Camera (RR only), Binah.ai SDK, Somnofy (no HR), Polar Verity Sense, Garmin HRM straps, Movesense MD, Masimo W1, Coospo straps, Fitbit Charge 6, Vernier respiration belt, Tricog and Cardiac Design Labs (contact ECG) — see `contact_vitals.md`, `market_price.md`.

### 2.4 Dedicated people counters (door/line and area)

| Product | Technology | Presence | Counts (max) | Positions | BR / HR | Range / height | Price USD | Price INR | Claim | Link |
|---|---|---|---|---|---|---|---|---|---|---|
| Milesight VS133 / VS135 | ToF 940 nm + AI | yes | line crossing, "up to 99.8 %"; max not stated | in field of view | none | 2.335-3.5 m (VS135 high-mount ≤6.5 m) | US$598-747.50 (MCCI, unconfirmed); EUR 759 ex VAT (Vemco) | ₹56,241 PoE option, sale price (Tyna Tech, GST not stated) | vendor | [VS133 datasheet](https://cdn.shopify.com/s/files/1/0067/0249/7890/files/vs133-datasheet-en.pdf), [VS135 guide](https://resource.milesight.com/milesight/iot/document/vs135-user-guide-en.pdf) |
| Milesight VS121 | AI fisheye camera | yes, seated | region "up to 95 %", line "up to 98 %" | per region | none | 2.5-4 m | not found | not found | vendor | [datasheet](https://cdn.shopify.com/s/files/1/0447/5633/6807/files/vs121-vs121-p-datasheet-en.pdf) |
| Terabee L-XL | IR ToF | yes | door entry/exit, 98 % (defined on entry + exit totals) | no | none | 2.4-4.0 m | EUR 1,016 (unconfirmed) | ₹97,402.15, 8 in stock (DigiKey India; LoRa model has no IN865 band) | vendor | [spec sheet](https://cdn.shopify.com/s/files/1/0447/5633/6807/files/Sensor_-_Terabee-People-Counting-L-XL-Lora_-_Specification-sheet.pdf), [DigiKey.in](https://www.digikey.in/en/products/detail/terabee-sas/TB-PCL-XL-LORA-EU-W/17047393) |
| Xovis PC series | 3D stereo | yes | line/area; accuracy not in datasheet | yes | none | 1.95-20 m by model | not found | ₹1,00,000 excl. taxes (IndiaMART, Bengaluru) | vendor | [PC3-O datasheet](https://api.xovis.com/fileadmin/user_upload/file/ressources/PC3-O_Technical_Datasheet.pdf), [IndiaMART](https://www.indiamart.com/proddetail/people-count-sensor-xovis-23790852273.html) |
| V-Count Nano AI | active stereo + AI | yes | "up to 99 %" line crossing | no | none | 2.2-7 m | US$299-599 + US$9-49/sensor/month | ≈₹38,421-1,12,052 first year (lane conversion) | vendor | [pricing](https://v-count.com/pricing/) |
| Density Open Area / Entry | radar (Open Area); depth DPU, Class 1 laser (Entry) | yes | Open Area headcount by tolerance; Entry in/out | Open Area point cloud | none | Entry 2.3-3.0 m | US$299 + US$0.99/sqft/yr + US$350-450 install (2024 customer quote) | ≈₹28,225 per sensor | vendor | [quote page](https://density.io/pricing/cab2024), [requirements](https://setup.density.io/requirements.html) |
| Butlr Heatic 2 / 2+ | low-resolution thermal | yes | "95 %" (vendor blog) vs 85-92 % (trade article): **conflict**; up to 6 in 5 × 5 ft | ±30 cm (undated page) | none | ~3.1 m | US$280/yr renewal (undated page); otherwise quote | not found (Propstack, Mumbai, is the India partner) | vendor | [Heatic 2](https://www.butlr.com/platform/heatic-2-sensor), [Propstack](https://butlr.com/blog-post-articles/butlr-partnership-propstack) |
| XY Sense Area Pro | on-device vision | yes | "up to 255 people", "~99 %" | x/y | none | 279 m² | not found | not found | vendor | [XY Sense](https://www.xysense.com/area-pro) |
| VergeSense Infinity | on-device AI, battery | yes | "95 %+" enclosed spaces | — | none | full room | not found | not found | vendor | [VergeSense](https://www.vergesense.com/occupancy-intelligence/infinity-area-sensor) |
| FootfallCam 3D Pro2 / 3D ProWave | stereo AI / mmWave radar | yes | "up to 99.9 %" / "up to 99.5 %" | — | none | ProWave 2.5-4 m | quote | ₹45,000 excl. taxes (IndiaMART, model not named) | vendor | [ProWave](https://www.footfallcam.com/Product/FootfallCam-3D-ProWave), [IndiaMART](https://www.indiamart.com/proddetail/footfallcam-people-counter-5050751848.html) |
| Hikvision DS-2CD6825G0/C-IS | dual-lens camera | yes | door counting, no official accuracy | — | none | not confirmed | not found | ₹1,04,175 (Tanotis title only; page 404, unconfirmed) | vendor | [Tanotis (404)](https://www.tanotis.com/products/hikvision-ds-2cd6825g0-c-is-4mp-dual-lens-network-people-counting-camera) |
| Irisys Vector 4D | IR ToF | yes | "99 %" | X/Y | none | 2.0-4.5 m | **discontinued 2023** | ₹1,40,000 excl. taxes (IndiaMART, old stock) | vendor | [EOL notice](https://irisys.net/hubfs/2023%20Update/Docs%20and%20PDFs/Other/Irisys%20Vector%204D%20-%20LTB%20EOL%20Notice%20-%20Final%20-%20All.pdf) |
| Indian marketplace counters (Frogsplash) | camera / 3D stereo | yes | "up to 98 %" / 95-99 % | — | none | ceiling | — | ₹15,000 / ₹40,000 / ₹45,000 per piece (IndiaMART 2026-11-16) | vendor | [IndiaMART](https://m.indiamart.com/frogsplashtechnology/) |
| Wozart Sense Pro (Hyderabad) | thermal + AI | yes, including still | entry/exit, max not stated | — | none | above the door | — | not found | vendor | [homekitnews](https://homekitnews.com/?p=29978) |
| Senseware Attune | ceiling thermal | yes | zone and door counts, no accuracy | zones | none | 2.0-3.5 m | not found | not found | vendor | [install guide](https://help.senseware.co/en/articles/15363120-occupancy-sensor-installation) |
| Haltian Thingsee COUNT | overhead directional counter | — | passage ≤1 m wide | no | none | 230 cm | not found | not found | vendor | [install](https://support.haltian.com/knowledgebase/how-to-install-thingsee-count/) |
| TP-Link VIGI People Counting | head-shoulder detection on IP cameras | yes | "up to 95 %" (lab) | no | none | — | free in VIGI VMS | not found | vendor | [VIGI](https://www.vigi.com/en/technology/vigi-people-counting/) |

*Also in the lane notes but not tabled here:* Milesight VS125, VS126, VS350 and VS330, Axis P8815-2 (discontinued), PointGrab CogniPoint, Steinel HPD2, Disruptive Technologies and Pressac desk sensors, Occuspace, Kepler Vision, KKVS132, 75F, SenseGiz, afreespace — see `counting_sensors.md`, `gap_2_asia_india_regional.md`.

### 2.5 Other technologies (acoustic, floor, thermal arrays, tags)

| Product | Technology | Presence | Counts | Positions | BR / HR | Range | Price USD | Price INR | Claim | Link |
|---|---|---|---|---|---|---|---|---|---|---|
| Amazon Echo Ultrasound Motion Detection | ≥32 kHz Doppler from the smart speaker | motion | no | no | no / no | Range setting, no metres | not found | Echo Dot 5th gen ₹4,999 intro / ₹5,499 (2023, search summary) | vendor | [Amazon Science](https://amazon.science/blog/the-science-behind-ultrasonic-motion-sensing-for-echo) |
| Google Nest Hub ultrasound | sonar | proximity, about 1.2 m | no | no | no / no | ~1.2 m | not found | not found | vendor | [support](https://support.google.com/googlehome/answer/9509981) |
| SensFloor | capacitive floor underlay | yes | "number of people in a room", max and accuracy not found | yes | no / no | room | quote | not found | vendor | [Future-Shape](https://future-shape.com/en/system/) |
| Panasonic Grid-EYE AMG8833 | 8 × 8 thermopile | yes | doorway counting (100 % in a one-month lab test, peer-reviewed) | coarse | no / no | ~7 m | breakout US$44.95 (out of stock) | bare part ₹2,140.61-2,588.89 (DigiKey.in, GST not stated) | mixed | [Adafruit](https://www.adafruit.com/product/3538), [Sensors 2021](https://doi.org/10.3390/s21124062) |
| Melexis MLX90640 | 32 × 24 thermopile | yes | no study found | coarse | no / no | — | US$39.95-74.95 (out of stock) | ≈₹3,771-7,075 (converted) | vendor | [Adafruit](https://www.adafruit.com/product/4407) |
| Kontakt.io Portal Beam | thermal + IR + BLE | yes | "seat-level occupant counting" (vendor), max not found | room | no / no | 200 sq ft at 10 ft | quote | not found | vendor | [Kontakt.io](https://kontakt.io/products/digital/location-occupancy/) |
| Cisco Meraki Location Analytics | Wi-Fi probe RSSI | devices | devices only; randomised MACs removed | needs ≥3 APs | no / no | AP coverage | — | — | vendor | [Meraki docs](https://documentation.meraki.com/MR/Monitoring_and_Reporting/Location_Analytics) |
| ESPARGOS One | phase-synchronised ESP32 antenna array | demos | not evaluated | AoA (MUSIC demos), no accuracy | no / no | not stated | Core Kit EUR 350 net; EU B2B only | not found | vendor | [shop](https://buy.espargos.net/) |

*Also in the lane notes but not tabled here:* SleepScore app, ApneaApp, bed geophones (HeartQuake, BedDot), pressure mats (Scanalytics, ASC, RS PRO), Quuppa, Pozyx, Bluetooth Channel Sounding — see `gap_1_alt_tech_nonrf_tags.md`.

---

## 3. Technology comparison

**Ratings:** "Counting" and "multi-person HR" are the best level evidenced, with its source tier. Installation difficulty is the lanes' assessment from the vendor install guides: Low = plug in and calibrate; Medium = height, angle and zone commissioning; High = ceiling or floor work, cabling, or per-site training.

### 3.1 Capability, privacy, price, installation

| Technology | Counting ability (best evidence) | Multi-person HR ability | Privacy | Typical price (seen) | Install difficulty | Key sources |
|---|---|---|---|---|---|---|
| Wi-Fi CSI, single ESP32 link (this project) | None shipping. Research: one link gives 0.55-0.82 median error for 0-10 people, trained per room (Wi-CaL). This project: 0/1/2+ experimental, unmeasured | **No.** One 20 MHz antenna has no range or angle separation; HR-1 results exist only in research (Pulse-Fi, single user, dedicated transmitter) | High: no images; can sense through walls | ₹695-1,522 per board | Low hardware, but placement-critical | [Wi-CaL thesis](https://naist.repo.nii.ac.jp/record/11764/files/R017974.pdf), [ViMo](https://originwirelessai.com/wp-content/uploads/2024/04/ViMo_fengyu.pdf) |
| Wi-Fi CSI, multi-node mesh | Research: 4 ESP32 links, median error 0.35-0.41 for about 5 or 10 people (Wi-CaL, peer-reviewed); 2 Pi receivers 94.86 % for 0-20 bus passengers (preprint). Caution: a 72-link mesh was beaten by one well-placed link (preprint) | Only with antenna arrays: SpaceBeat, 3 people with about 18 RX antennas (peer-reviewed). Not on ESP32 | High | ₹1,500 × N nodes | Medium: placement of every link | [bus preprint](https://arxiv.org/abs/2410.11400), [SpaceBeat](https://par.nsf.gov/servlets/purl/10554426), [Less is More](https://arxiv.org/abs/2602.10823) |
| ISP or router Wi-Fi motion (Xfinity, Plume, Origin, Huawei FTTR) | No (motion or presence) | No; Huawei pairs Wi-Fi with mmWave radar for HR | Medium: cloud history; Comcast may disclose to third parties | Included in an ISP plan; US$15/mo tier | Low (app opt-in) | [Xfinity FAQ](https://www.xfinity.com/support/articles/wifi-motion-faqs), [9to5mac](https://9to5mac.com/2026/08/18/comcast-just-turned-millions-of-xfinity-routers-into-motion-sensors/) |
| 24 GHz radar (LD2410, LD2450, LD2460) | 1-3 moving targets (LD2450); ≥3 still / 5 moving (LD2460, vendor) | No | High: no images | ₹379-999 per module | Medium: wall 1.5-2.7 m, gate tuning | [LD2450 manual](https://www.elemon.com.ar/images/productos/hojas-de-datos/radar-modulos/hlk-ld2450.pdf), [LD2460 manual](https://revspace.nl/images/9/9d/HLK-LD2460_2T4R_Multi_Target_Trajectory_Tracking_Module_Manual_V1.1.pdf) |
| 60 GHz FMCW radar, consumer and modules | FP2 5 (best ≤3); FP400 10 (vendor); LD6001 8 (reseller); still-people counting preprint-only (mmCounter: F1 87 % in familiar rooms, 60 % in unseen ones) | **Modules and consumer products: single person** (MR60BHA2, LD6002, ME72MS02, FP2). Research: TU Delft 4 people on IWR6843 | High | ₹999-12,000; US$17-83 | Medium (vitals: chest ≤1.5 m) | [mmCounter](https://arxiv.org/abs/2512.10357), [TU Delft](https://repository.tudelft.nl/record/uuid:63813064-dee6-41bc-b4f6-32ea42cb9ff3) |
| 77-79 GHz MIMO FMCW (TI EVM class) | Same MIMO class as TI's 60 GHz IWR6843 counting test (TIDUE71D): 1 person 79-99.8 % zero-error frames, falling to 33-41 % at 4-6 people (vendor) | Research only: 2-16 people (Xu 2; Eder 3; Tan 4; Benny 5 separated; Sumi/Sakamoto 16 with 2 radars, MAE 3.69 bpm). Needs ≥15-45° angular separation | High | EVM prices behind login | High (PC, firmware, geometry) | [Sumi & Sakamoto](https://arxiv.org/abs/2510.10542), [TIDUE71D](https://www.ti.com/lit/ug/tidue71d/tidue71d.pdf) |
| UWB radar (IR-UWB: XK300, Vayyar imaging, NXP SR250, Novelda) | Vayyar in-cabin 8 occupants (presence, no vitals); room counting not found | **No product claims it.** XK300 is per patient, FDA-cleared | High | not found | Medium | [XK300 510(k)](https://www.accessdata.fda.gov/cdrh_docs/pdf20/K202464.pdf), [Vayyar auto](https://vayyar.com/auto/) |
| Thermal array (Grid-EYE, MLX90640, Butlr, Senseware) | Doorway in/out (Grid-EYE 100 % in a lab month); area headcount "95 %" (Butlr, vendor); merges close groups | No | High: heat only | ₹2,141 bare Grid-EYE; US$280/yr Butlr | Low-Medium; empty-room reference at boot (Attune 90 s) | [Sensors 2021](https://doi.org/10.3390/s21124062), [Attune](https://help.senseware.co/en/articles/15363120-occupancy-sensor-installation) |
| ToF / lidar / stereo counter (Milesight, Terabee, Xovis, Density Entry) | Door line 98-99.9 % (vendor); small footprint per unit | No | Medium-High (depth; stereo has image sensors) | ₹45,000-1,00,000+ per unit; EUR 600-1,000+ | High: ceiling, exact height (±5 cm for Xovis), PoE | [Terabee](https://cdn.shopify.com/s/files/1/0447/5633/6807/files/Sensor_-_Terabee-People-Counting-L-XL-Lora_-_Specification-sheet.pdf), [Xovis table](https://intercom.help/vemco/en/articles/15445338-xovis-sensor-coverage-tables-all-models) |
| AI camera (XY Sense, VS121, Steinel HPD2, VIGI) | Area and seated counting; "up to 255" people (XY Sense, vendor); 95 % region (VS121) | No (counting cameras); camera rPPG is separate | Low: images, even if processed on the device; DPIA expected | free (VIGI software) to quote | Medium | [VS121](https://cdn.shopify.com/s/files/1/0447/5633/6807/files/vs121-vs121-p-datasheet-en.pdf) |
| Camera rPPG (FaceHeart, NuraLogix, Presage, Oxehealth, Binah) | n/a | **Cleared devices: one face only.** Research: up to 6 people (Al-Naji 2017, abstract) or 7 (SPIE, 5-8 bpm error, not re-checked) | Low (face video) | SDK licence; Presage free for institutions | Medium: 0.2-1.5 m, >100-300 lux, still subject | [K223622](https://www.accessdata.fda.gov/cdrh_docs/pdf22/K223622.pdf), [Al-Naji](https://doaj.org/article/0d482540e7d74d87831073dac98a87f0) |
| PIR | Cannot count a room; dual PIR counts passage direction (VS350); VergeSense rates PIR 60-70 % | No | High | cheap; IEC 63180 tests range | Low | [VS350](https://resource.milesight.com/milesight/iot/document/vs350-datasheet-en.pdf), [IEC 63180](https://webstore.iec.ch/en/publication/61559) |
| CO2 | 94 % within ±4 occupants (2016 preprint) | No | High | uses existing HVAC sensors | Low | [arXiv 1607.05962](https://arxiv.org/abs/1607.05962) |
| BLE / Wi-Fi probe / UWB tags (Meraki, Kontakt.io, Quuppa, Pozyx, Channel Sounding) | Counts devices, not bodies; MAC randomisation (Android 10+) breaks probe counting | No | Medium (device identity) | quote | Medium (≥3 APs or anchors) | [Meraki](https://documentation.meraki.com/MR/Monitoring_and_Reporting/Location_Analytics), [RateCount](https://arxiv.org/abs/2507.03873) |
| BCG under-mattress (Withings, Emfit, Dozee) | 1 per bed | No: one sensor per sleeper; Emfit says two people on one sensor cannot be separated; VitalMon shared-bed 2017 is title-level only | High | ₹19,200 (Withings, unconfirmed) | Low: under the mattress at chest level, empty bed while calibrating | [Emfit FAQ](https://emfit.com/faq/), [Withings guide](https://media.withings.com/kits/guides/2023/sleep-analyzer/AW_PRODUCT_GUIDE_WSM02_INT_A.pdf) |
| Wearables (Polar H10, rings, watches) | one per wearer | Yes, but one device per person (that is how references work) | Medium (worn, identified) | H10 ₹8,999; Oura ₹28,900 + sub | Low (wet, snug strap) | [Polar H10 manual](https://support.polar.com/e_manuals/h10-heart-rate-sensor/polar-h10-user-manual-english/wear-the-heart-rate-sensor.htm) |
| **Gap: active acoustic sonar** (Echo, Nest; research arrays) | Products: none (motion only). Research: localises up to 4-6 | **Research yes:** Tran 2024, up to 4, 0.66 bpm median for 2 (peer-reviewed); DF-Sense 5-6 at ≤1.19 bpm (peer-reviewed, laptop MATLAB) | Medium (microphones) | Echo Dot ₹4,999-5,499 | Low (product); lab rig (research) | [Tran 2024](https://www.ncbi.nlm.nih.gov/pmc/articles/PMC10819445/), [DF-Sense](https://hal.science/hal-05477388) |
| **Gap: floor vibration / smart floor** (SensFloor, geophones) | Up to 3 concurrent walkers at 90 % detection (Shi 2019); SensFloor headcount claim | No: FloorPulse is single-person, ~10 bpm error at 2 m | High | quote | High (under the floor covering) | [FloorPulse](https://doi.org/10.1017/dce.2026.10074), [Shi 2019](https://doi.org/10.1145/3360773.3360887) |
| **Gap: 60 GHz Wi-Fi 802.11ad CIR** (ViMo) | user detection 97.86 % (not a count accuracy) | Research: 3 people, HR median 1 bpm for two at ≥45° | High | not consumer-accessible | High | [ViMo](https://originwirelessai.com/wp-content/uploads/2024/04/ViMo_fengyu.pdf) |
| **Gap: ESP32 phase-coherent array** (ESPARGOS) | not evaluated | not evaluated | High | Core Kit EUR 350 net (EU B2B) | Medium | [arXiv 2502.09405](https://arxiv.org/abs/2502.09405) |

### 3.2 Pros and cons

| Technology | Pros | Cons | Sources |
|---|---|---|---|
| Wi-Fi CSI, single link | Cheapest; reuses the router; no images; one node per room | No range or angle separation; per-room training or calibration; AGC and beamforming affect capture (§4.2); no measured accuracy on the C5; sees through walls (privacy and false alarms); pets | [Wi-CaL](https://naist.repo.nii.ac.jp/record/11764/files/R017974.pdf), [Lambrecht](https://arxiv.org/abs/2608.20322) |
| Wi-Fi CSI mesh | More links give better counting and localisation (Wi-CaL) | Placement of every link matters; more links can dilute the signal (preprint); still no multi-person HR | [Less is More](https://arxiv.org/abs/2602.10823) |
| ISP/router motion | No extra hardware; mass reach | Motion only; pets trigger it; cloud data disclosure (Comcast); not in India | [Xfinity FAQ](https://www.xfinity.com/support/articles/wifi-motion-faqs) |
| 24 GHz radar | Still presence for ₹379-999; X/Y for 3 people | Sees through thin walls; 1-3 targets; gate tuning took a maker days | [XDA](https://www.xda-developers.com/presence-sensors-cost-80-but-esp32-mmwave-radar-detect-your-breathing/) |
| 60 GHz radar | Still presence; zones; single-person BR/HR modules | Vitals one person only within 1.5 m; weak HR accuracy where tested; India band legality (only 61-61.5 GHz delicensed); fans, curtains, mirrors make ghost targets | [MR60BHA2 datasheet](https://files.seeedstudio.com/wiki/mmwave-for-xiao/mr60/datasheet/MR60BHA2_Breathing_and_Heartbeat_Module.pdf), [TRAI](https://www.trai.gov.in/sites/default/files/2025-12/Recommendation_10122025.pdf) |
| 77-79 GHz MIMO | The only route to multi-person HR (research); 3D tracks | EVM plus PC; accuracy falls with distance (Benny single subject 2.11 bpm at 1 m, 10.39 at 6 m); needs angular spacing | [Benny 2025](https://arxiv.org/html/2511.21314) |
| UWB radar | FDA-cleared single-patient vitals (XK300); through bedding | No multi-person HR claim; no public price | [K202464](https://www.accessdata.fda.gov/cdrh_docs/pdf20/K202464.pdf) |
| Thermal array | Battery, anonymous, works in the dark | Merges close groups; heat sources and sunlight; empty-room boot reference | [Butlr blog](https://www.butlr.com/blog/best-occupancy-sensors-for-workplace), [Attune](https://help.senseware.co/en/articles/15363120-occupancy-sensor-installation) |
| ToF / stereo counter | Best door accuracy claims; proven retail | Door only (not room); ceiling work; 940 nm IR sources, glass and dark carpet break ToF; subscription | [VS135 guide](https://resource.milesight.com/milesight/iot/document/vs135-user-guide-en.pdf) |
| AI camera | Counts seated people; large coverage | Camera privacy; needs light | [Xovis datasheet](https://api.xovis.com/fileadmin/user_upload/file/ressources/PC3-O_Technical_Datasheet.pdf) |
| Camera rPPG | Cleared HR at 1.3-2.5 bpm RMSE | One face; still; lit; camera in the room | [K254169](https://www.accessdata.fda.gov/cdrh_docs/pdf25/K254169.pdf) |
| PIR | Cheapest; standard test method | Misses still people; cannot count | [VergeSense](https://www.vergesense.com/occupancy-intelligence/infinity-area-sensor) |
| CO2 | Already in HVAC | ±4 people; slow | [arXiv 1607.05962](https://arxiv.org/abs/1607.05962) |
| Tags / probes | Identity (staff) | Counts devices only | [Android MAC randomisation](https://source.android.com/docs/core/connect/wifi-mac-randomization-behavior) |
| BCG mats | Good HR when a value is output (MAE <2.2 bpm, independent) | One per sleeper; data loss (Emfit 47 % of participants); bed types excluded | [Ravindran](https://www.medrxiv.org/content/10.1101/2023.10.13.23296936), [Kholghi](https://pmc.ncbi.nlm.nih.gov/articles/PMC8974370) |
| Wearables | Ground truth (H10) | Must be worn; one per person | [Polar SDK](https://github.com/polarofficial/polar-ble-sdk/blob/master/documentation/products/PolarH10.md) |
| Acoustic sonar | Multi-person HR in research with cheap speaker+mic arrays (2-5 kHz bandwidth gives cm-scale range bins) | Lab rigs only; fans and washers raise error; microphones raise privacy concerns | [Tran](https://www.ncbi.nlm.nih.gov/pmc/articles/PMC10819445/), [DF-Sense](https://hal.science/hal-05477388) |
| Floor sensing | No line of sight; counts walkers | Still people invisible; renovation | [SensFloor](https://future-shape.com/en/system/), [FloorPulse](https://doi.org/10.1017/dce.2026.10074) |

---

## 4. Correct installation process

The checklists below come only from the cited guides and papers. Where no source gives a number, it says "not found". Mounting height is the most often missing number for Wi-Fi sensing.

### 4.1 Per-technology checklists

**A. Wi-Fi CSI motion and presence (ESPectre, esp-csi, Xfinity, WiZ, Hue, nami, TOMMY)**
1. Use at least 2 nodes per area (nami, WiZ, TOMMY; Hue needs 3-4). Spacing:
   - nami 10-30 ft.
   - TOMMY at least 1 m, little gain beyond 4 boards.
   - Hue 1-7 m, at varying heights, not in a line, no walls between.
   - WiZ processors 2-6 m from the beacon, with the beacon near the room centre.
   - Sources: [nami forum](https://support.suretyhome.com/t/what-is-the-difference-between-nami-wi-fi-motion-sensing-and-pir-motion-sensors/50324), [WiZ](https://faq.wizconnected.com/hc/en/7-wiz-v2/faq/501-are-there-any-setup-requirements/), [Hue](https://www.philips-hue.com/en-ca/support/article/motionawaretm--transform-your-hue-lights-into-motion-sensors/000011).
2. The monitored area and the walking routes must lie on the link ("ovals must cross the area", Xfinity). The person must block the path: with a high-mounted AP a maker saw only ~40 % contrast, while boards at desk height gave ~6-7× ([winson-DIY](https://www-winson.blogspot.com/2026/11/esp32-s3-wifi-csi-sensing-first-real.html)).
3. Height:
   - Xfinity: gateway "at least a couple of feet off the floor", client devices "at least four feet off the floor" (this reduces pet alerts).
   - Research: tripods at 1 m.
   - No other vendor gives a number.
4. Distance from the AP: about 3-8 m with RSSI of −40 to −70 dBm; below −80 dBm is weak (ESPectre).
   - Wall effect: transmitter 1-1.5 m from a wall (+2.1-11.2 % counting). At 0.1 m, coverage leaks into the next room. Beyond about 2 m the wall effect is negligible ([Wall-Proximity](https://arxiv.org/html/2412.13208v4)).
5. Antenna and enclosure:
   - Prefer an external (IPEX) antenna over the PCB antenna (esp-csi README).
   - Mount the external antenna vertically; rotate the board 90° and repeat the walk test (ESPectre).
   - No metal enclosure; not behind a TV; not right next to the AP.
6. Router:
   - Pin one AP (roaming destabilises the signal) and use a fixed channel (a channel change restarts the history).
   - The router must answer ping or carry traffic; a blocked or rate-limited ping is "the most common" stall (ESPectre).
   - Turn BLE scanning off on the node.
   - On mesh systems, all WiZ devices must sit on the same AP and channel.
   - No measured study of band steering, MU-MIMO or DFS was found.
   - Beamforming: see §4.2.
7. Calibration:
   - Room empty and still: ESPectre about 10-30 s; WiZ uses a 10-minute calibration window; Hue "no people or pets".
   - Re-calibrate or restart after moving a device. A layout-change trigger is not documented by ESPectre.
8. What breaks it:
   - Fans, curtains, plants in airflow, pets and robot vacuums: every vendor names these; Xfinity's default sensitivity catches small pets.
   - Motion outside the link, a sleeping or offline client device, bridge mode, building materials.

**B. 24 GHz radar (LD2410, LD2450, LD2460)**
1. Wall mount at 1.5-2 m (LD2410/LD2450 manuals).
   - LD2460 side mount: 2.2-2.7 m, tilted 25-40° (e.g. 2.6 m / 30°).
   - LD2460 top mount: 2.5-3 m.
   - Enter the height and angle in the PC tool ([LD2460 manual](https://revspace.nl/images/9/9d/HLK-LD2460_2T4R_Multi_Target_Trajectory_Tracking_Module_Manual_V1.1.pdf)).
2. Housing transparent to 24 GHz, no metal. Mount rigidly. Mind the back lobe. Never point several 24 GHz radars at each other ([LD2450 manual](https://www.elemon.com.ar/images/productos/hojas-de-datos/radar-modulos/hlk-ld2450.pdf)).
3. Keep animals, swinging curtains, plants in airflow, large reflectors and AC/fans out of the beam.
4. Tune one distance gate at a time. The default 5 s timeout is too short for seated people (XDA build).
5. Thin walls: the radar sees into the next room.

**C. 60 GHz vitals radar (MR60BHA2, C1001, LD6002, FP2 Sleep, ME72MS02)**
1. Only one person in the detection area, at rest. Chest 0.4-1.5 m from the radar. Two geometries:
   - Side mount at chest height, 1.5 m or less.
   - 1 m above the head of the bed, tilted 45° toward mid-bed ([MR60BHA2 datasheet §6.3](https://files.seeedstudio.com/wiki/mmwave-for-xiao/mr60/datasheet/MR60BHA2_Breathing_and_Heartbeat_Module.pdf)).
   - FP2 Sleep mode: "roughly 1 m" above the bed at 45° (press).
   - C1001 sleep mode: tilted 30-45° down.
2. Allow about 1 minute to lock (MR60BHA2). Large movements stop the measurement.
3. Power:
   - Bare ADT6101P modules: 3.2-3.4 V, ripple ≤50 mV, ≥1 A, DC-DC ≥2 MHz.
   - Kits: clean external 5 V ≥1.5 A. In one maker's build, USB power caused no HR lock, corrupt UART frames and brown-out reboots ([techiesms](https://github.com/techiesms/sleeplamp-mmwave-sleep-tracker)).
4. Avoid in the beam: close-by radars, curtains or plants in wind, running water, metal and mirrors, glass or thin wood, vibration, exhaust fans, metal blinds.
5. Known failure: the C1001 outputs invented HR/BR for 60 s after the person leaves ([forum](https://www.dfrobot.com/forum/topic/335849)). Gate it on presence.

**D. Tracking and counting radar (TI IWR6843, LD6001, FP2 zone mode, FP400, VS373)**
1. Wall-mounted trackers:
   - TI: 1.5-2.5 m with 10-15° down-tilt; configure boundaryBox, staticBoundaryBox and presenceBoundaryBox ([TI lab guide](https://dev.ti.com/tirex/explore/content/mmwave_industrial_toolbox_4_11_0/labs/People_Counting/3D_People_Counting/docs/3d_people_counting_user_guide.html)).
   - LD6001: 2.2 m, 10° down.
2. Ceiling mounts:
   - FP2 fall mode: 2.5-2.8 m, 2 m radius.
   - VS373: 2.3-3 m, parallel to the floor, ≥1 m from walls.
   - MR60FDA2: 2.2-3.0 m.
   - FP400: 3-4 m.
3. FP2 commissioning ([manual](https://www.123led.nl/pdf/LAQ00045.pdf)):
   - A wall mount is required for zones and multi-person tracking.
   - Mark edges (including beyond walls), interference sources (only with people and pets out of the room) and entrances.
   - Hub within 20 m and through at most 2 walls.
   - No official wall height: an Aqara forum post says 1.8-2.2 m, and the 2.5-2.8 m figure is the ceiling height for fall detection.
4. Multi-person geometry from papers:
   - Mount high so people separate in range; 3 non-collinear radars remove blind zones (Chinese journal, breathing only).
   - Benny: 2 m height, tilted to the group centre, ≥15° separation.
   - Sumi/Sakamoto: 2.0 m, 10° down.
5. FP400 zones work only with the Aqara hub; direct Matter gives one whole-area sensor.

**E. Door and line counters (ToF, stereo, thermal)**
1. Ceiling or lintel, looking straight down. The VS133-P allows no tilt; the VS135 allows 15° (10° for high-ceiling units); Xovis allows ±15°/±5°.
2. Height:
   - VS133: tallest person + 0.535 m up to 3.5 m.
   - Terabee: 2.4-4.0 m.
   - Density Entry: 2.3-3.0 m, "does not support installations outside of this range".
   - Haltian COUNT: 230 cm.
   - Xovis: enter the height within ±5 cm, and re-calibrate after a tilt change of 1° or more.
3. Clearances:
   - ≥50 cm from walls (VS133).
   - Door: opposite side from the swing, ≥30-40 cm from the swing path.
   - Density: clear below of hinges and elbows, exit signs, auto-door sensors, cameras, AC and air curtains.
4. Breakers: 940 nm IR sources, glass, mirrors, sunlight; dark carpet breaks auto-height (VS135); lamps affect Haltian.
5. Thermal (Senseware Attune): straight down; keep the scene clear for 90 s after power-up; reboot with the room empty if counts drift; exclusion zones over reflections and hot or cold surfaces.
6. Verify with the device's video mode against a manual count (Milesight, Xovis wizard).
7. Sources: [VS133-P guide](https://www.milesight.com/getting-start/en/vs133-p-quick-start-guide.html), [VS135 guide](https://resource.milesight.com/milesight/iot/document/vs135-user-guide-en.pdf), [Density](https://setup.density.io/requirements.html), [Attune](https://help.senseware.co/en/articles/15363120-occupancy-sensor-installation).

**F. AI camera counters**
- VS121: above 2.5 m for seated people, above 3 m for standing people and lines; draw regions within the recommended table.
- Steinel HPD2: wall or corner at 2.30-6.00 m (optimum 2.8 m), 2-2000 lux.
- Xovis: at least 2 lux.
- Carry out a DPIA (ICO expects one before any employee monitoring).

**G. Camera rPPG (cleared devices)**
- Subject seated and still, 0.2-1.5 m away, more than 100-300 lux.
- Presage requires a static mount installed by a technician who checks light and vibration.
- One face only ([K254169](https://www.accessdata.fda.gov/cdrh_docs/pdf25/K254169.pdf)).

**H. BCG bed sensors**
- Withings:
  - Under the mattress at heart level, with cardboard over the slats.
  - Not on waterbeds, air beds or articulated beds.
  - Portable RF devices ≥30 cm away.
  - After plugging in, wait 10 min before anyone gets in the bed ([guide](https://media.withings.com/kits/guides/2023/sleep-analyzer/AW_PRODUCT_GUIDE_WSM02_INT_A.pdf)).
- Emfit: centre of the sensor under the heart, ~50 cm from the top of the bed; couples place it near the bed edge.
- One sensor per sleeper.

**I. Reference wearable (Polar H10)**
- Moisten the electrodes; fit the strap snugly; rinse after every use.
- Log RR intervals or ECG over BLE, because onboard recording is HR only ([SDK](https://github.com/polarofficial/polar-ble-sdk/blob/master/documentation/products/PolarH10.md)).

**J. Acoustic, floor and tags (gap technologies)**
- Echo ultrasound: enable it in the Alexa app, set sensitivity and range, angle the device away from curtains and fans. Bass-heavy music disturbs it ([Amazon Science](https://amazon.science/blog/the-science-behind-ultrasonic-motion-sensing-for-echo)).
- SensFloor: an underlay beneath the floor covering; no commissioning steps published.
- Meraki: ≥3 APs and a dedicated scanning radio.

### 4.2 Checklist for this ESP32-C5 project

**New hardware finding, 2026-11-05: an 11ax AP beamforming to the board.** One session, one AP, recorded only in `~/ESP32C5_CSI_logs/2026-11-05_hw/`; not yet in TESTING §5 or DECISIONS.

The AP advertises `bfmer(su:1, mu:1)` and HE capability (log line `wifi:(phy)rx_mcs_map ... bfmer(su:1, mu:1)` in `csidiag_150s.log`). A diagnostic build cycled six CSI configs. Its config table is in `csidiag_src/main_v2_bfee_off.c`; config 4 is the Step 08/10 default and config 2 is `CSI_LLTF_ONLY`.

| Run | Board beamformee | Configs with `acquire_csi_beamformed = 0` (cfg 0, 4) | Configs with `= 1` (cfg 1, 3, 5) | cfg 2 (`CSI_LLTF_ONLY`) | Log |
|---|---|---|---|---|---|
| Default | enabled | 0-2 CSI per 10 s; one cfg-4 window had 0 | 453-476 per 10 s, all in HE-SU format | 0-2 per 10 s | `csidiag_150s.log` |
| Beamformee disabled (`he_su_beamformee_disabled = 1`, `vht_su_…`, `vht_mu_…`) | disabled | 453-483 per 10 s | 443-465 per 10 s | **0** per 10 s | `csidiag2_bfee_off_140s.log` |

What this shows, for this AP only:
- The shipped configuration (D-40 sets `beamformed = 0`, `docs/DECISIONS.md:417`) gets almost no CSI when the AP beamforms to the board.
- Either disabling beamformee on the STA or accepting beamformed CSI restores about 44-48 CSI/s.
- The diag's source-MAC counter (`memcmp(info->mac, ap_bssid)`) attributes only about 187-227 CSI per window to the AP and 0 to other senders. Why it does not equal the total is not explained by the log, so do not read the totals as "all from the AP".
- Beamformed CSI is precoded, so accepting it may change what the CSI means. **Disabling beamformee is the cleaner option, but this is an inference.**
- The `CSI_LLTF_ONLY` fallback, which ran the 09-28 soak on the laptop hotspot, gets nothing from this HE-SU AP.

This is a decision for the user (D-40, D-42). Here it is a proposal, not a change.

**Commissioning checklist**, compiled from the sources above and the project's own docs:

1. **Port and board:** confirm the port with `esptool chip_id` or `list_ports` (CLAUDE.md). Avoid GPIO16-22 for any sensor UART. That rule comes from the 10-30 sweep (DECISIONS D-174 item 5) and was not re-verified against an Espressif datasheet in this pass.
2. **AP:**
   - One AP and a fixed channel; no roaming. If a mesh is used, pin the BSSID.
   - The AP must answer ping or carry traffic in HT/HE frames. A router that answers with 11g frames gives no CSI in the default mode (D-40).
   - With an 11ax AP, decide on beamformee (above) before any measurement.
   - First check the rate with Step 03/04: expect about 50 CSI/s (the project's hotspot) or about 44-48/s (the 11-05 AP).
3. **Placement:**
   - The link from the AP to the C5 must cross where people sit or walk.
   - Body height, about 1 m (the papers' tripods; the winson-DIY result).
   - Transmitter or AP 1-1.5 m from walls if possible.
   - AP-to-C5 distance about 3-8 m with RSSI −40 to −70 dBm (ESPectre). The 11-05 AP read −38 dBm, at the edge of that range.
   - No metal enclosure. The external antenna vertical.
   - Keep the chest mid-Fresnel-zone and facing the link for breathing (Gu 2019 summary of Wang 2016 and Zhang 2018).
4. **Interference:** no fans, AC airflow on curtains, pets or robot vacuums in the link during calibration; BLE scanning off.
5. **Calibration:**
   - Empty, still room for the baseline: Step 08's about 35 s (gap 4 notes).
   - Re-calibrate after moving the board, the AP or furniture.
   - Log `agc`, `fft` and `gain_chg` (D-176) to see whether gain steps occur.
6. **References at the same time** (§9): one Polar H10 per seated person; an LD2450 or Grid-EYE for counts; written occupancy notes (TESTING §4). Without these, no accuracy can be claimed.
7. **Scoring:** use Density's tolerance idea: 0/1/2+ classes, exact for 1-3 people, target ≥90 % "within tolerance". Report empty-room false PRESENT separately.
8. **Regulatory before any deployment:** WPC ETA for the product model; DPDP notice and log retention for anything that can identify a person (§8).

---

## 5. Theory and research

### 5.1 Key papers, theses and datasets

"People" is how many were handled at once. Metrics are the authors' own. "Abstract" means only the abstract was read.

**Multi-person vital signs**

| Work (year) | Type | People at once | Metric (authors) | Hardware | Code / data | Link |
|---|---|---|---|---|---|---|
| Sumi & Sakamoto, physiology-informed multivariate VMD (2025, v2 2026-08) | preprint | **16** (2 radars); 6 with 4 radars | HR MAE 3.69 bpm, 87.5 % of estimates within 10 %; BR 100 % | two 79 GHz FMCW radars at 2.0 m height, 10° down | not found | [arXiv 2510.10542](https://arxiv.org/abs/2510.10542) |
| Benny et al., Scalable multisubject VSM + FPGA (IEEE Sensors J 2025) | peer-reviewed | 5 separated; **no 5-person HR MAE** | pooled Random Forest HR MAE 3.64 bpm (held-out split); single subject 2.11 bpm at 1 m, 10.39 at 6 m | TI IWR1642BOOST 77 GHz + DCA1000; ≥15° separation | not found | [arXiv 2511.21314](https://arxiv.org/abs/2511.21314) |
| Benny et al., multi-patient HR/BR (IEEE BioCAS 2023) | peer-reviewed | 3 | HR 93.43 % (1 − relative error); BR 97.94 % | TI IWR1642 | not found | [arXiv 2511.21255](https://arxiv.org/abs/2511.21255) |
| Tan et al., micro-Doppler multi-target VSM (2025) | preprint | 4 within 5 m | HR MAE 2.3 bpm; BR 1.2 | 77 GHz FMCW (IWR2243), public datasets | datasets | [arXiv 2510.19639](https://arxiv.org/abs/2510.19639) |
| Eder et al., phantom-assisted multi-person VSM (2025) | preprint | 3 seated | 87.1 / 94.1 / 95.5 % of HR within 2 / 3 / 4 bpm (PPG reference) | TI IWR1443BOOST 2TX/4RX | not found | [arXiv 2501.06755](https://arxiv.org/abs/2501.06755) |
| Xu et al., phased-MIMO (IEEE JERM 2022) | peer-reviewed | 2 | HR > 82.89 % (abstract version: HR < 3 bpm); ≥30-40° apart at 1.6 m | TI AWR2243 | not found | [PDF](https://web.njit.edu/~cs638/paper/phased_MIMO.pdf) |
| den Hoedt, TU Delft MSc (2022) | thesis | 4 | HR 9.91 % session-averaged, 18.94 % per reading (4-person, one session); pulse-oximeter reference | 60 GHz IWR6843ISK, custom firmware | not found | [repository](https://repository.tudelft.nl/record/uuid:63813064-dee6-41bc-b4f6-32ea42cb9ff3) |
| Mercuri et al., Nature Electronics 2(6) (2019) | peer-reviewed | multiple (title) | not found (paywalled) | radar | not found | [DOI](https://doi.org/10.1038/s41928-019-0258-6) |
| MEDUSA (2023, v6 2026) | preprint | multiple, moving | +20 % over COTS radar | 16 × 16 distributed UWB MIMO | 3.75 TB dataset | [arXiv 2310.05507](https://arxiv.org/abs/2310.05507) |
| **SpaceBeat** (ACM IMWUT 2024) | peer-reviewed | **3 on Wi-Fi** | HR 95.2 % (1 − relative error) with 3 people | Intel 5300, 5.24 GHz 40 MHz, 2 × 9-antenna L-arrays, 1000 pkt/s | not found | [PAR](https://par.nsf.gov/servlets/purl/10554426) |
| ViMo (IEEE IoT-J 2020) | peer-reviewed | 3 (model max 5) | HR median 1 bpm for two at ≥45° | 60 GHz 802.11ad CIR, 32-antenna arrays, 3.52 GHz bandwidth | not found | [PDF](https://originwirelessai.com/wp-content/uploads/2024/04/ViMo_fengyu.pdf) |
| MultiSense (IMWUT 2020) | peer-reviewed | 4 (breathing only) | BR MAE 0.73 bpm | Wi-Fi, 3 antennas each side, ICA | not found | [DOI](https://doi.org/10.1145/3411816) |
| Guan et al., switched antenna array (IEEE JTEHM 2022) | peer-reviewed | 2 (breathing) | 97.04 % within 1.5 m | Intel 5300 + SP4T-switched 4 Yagi antennas | not found | [PDF](https://eprints.gla.ac.uk/284596/2/284596.pdf) |
| MUSE-Fi (MobiCom 2023) | peer-reviewed | multiple (breathing) | not in the abstract | each person < 0.2 m from their own Wi-Fi device | not found | [arXiv 2308.10234](https://arxiv.org/abs/2308.10234) |
| Tran, Ma, Balan (Sensors 2024) | peer-reviewed | up to 4 | median 0.66 bpm (2 side by side); 0.71-1.2 bpm at 0.5-3 m; 10 couples, ECG reference | acoustic FMCW 18-23 kHz, 7-mic array | not found | [PMC](https://www.ncbi.nlm.nih.gov/pmc/articles/PMC10819445/) |
| DF-Sense (IEEE TMC 2025; MobiSys 2023) | peer-reviewed | 5 at 1 m, 6 at 2 m spacing | < 0.92 bpm single to 10 m; ≤ 1.19 bpm with 5 | acoustic OFDM, 16 MEMS mics, laptop | not found | [HAL](https://hal.science/hal-05477388) |
| Al-Naji & Chahl (IEEE JTEHM 2017) | peer-reviewed | up to 6 | r = 0.9994 (abstract) | RGB camera | not found | [DOAJ](https://doaj.org/article/0d482540e7d74d87831073dac98a87f0) |
| VitalMon (SenSys 2017) | peer-reviewed | shared bed; whether both partners are output is **unconfirmed** | 1.90 bpm (unconfirmed) | geophones | not found | [Crossref](https://api.crossref.org/works/10.1145/3131672.3131679) |
| Kyoto Univ. + Panasonic (2017) | press (research) | "multiple individuals ... as little as 7.5 cm" apart | no accuracy | 79 GHz radar | — | [Kyoto](https://www.kyoto-u.ac.jp/index%2Ephp/en/research-news/2017-10-03) |

**Single-person vitals (ESP32 and references)**

| Work (year) | Type | People at once | Metric (authors) | Hardware | Code / data | Link |
|---|---|---|---|---|---|---|
| Pulse-Fi (DCOSS-IoT 2025) | mixed | 1 | ~0.5 bpm after 5 s at ~3 m (press, 118-person Pi set); ESP32 set: 7 people, MAE 0.46-0.50 (shuffled windows) | ESP32 + dedicated TX; Raspberry Pi | per paper | [UCSC](https://news.ucsc.edu/2025/09/pulse-fi-wifi-heart-rate/) |
| SA-WiSense (2025) | preprint | 1 (5 targets in turn) | 91.2 % of breathing within 1 bpm up to 8 m | ESP32, 40 MHz, 218 tones, 120 Hz, single antenna | not found | [arXiv 2507.17623](https://arxiv.org/abs/2507.17623) |
| FarSense (UbiComp 2019) | peer-reviewed | 1 | breathing to 8 m (range up more than 100 %) | 2-antenna CSI ratio | not found | [arXiv 1907.03994](https://arxiv.org/abs/1907.03994) |
| HEAR (2026-10) | preprint | 1 | mmWave HR MAE 17.9 → 1.6 bpm by abstaining on the 50 % least observable windows | 60/120 GHz datasets | project page | [arXiv 2610.03570](https://arxiv.org/abs/2610.03570) |
| Ravindran (JMIR mHealth 2024) | peer-reviewed | 1 | Withings/Emfit HR MAE < 2.12 bpm vs PSG | under-mattress BCG, radar | — | [medRxiv](https://www.medrxiv.org/content/10.1101/2023.10.13.23296936) |
| Ratnam et al., optimal CSI preprocessing (IEEE TWC 2024) | peer-reviewed | 1 | +20 % respiration SNR; gain/phase error model | Wi-Fi testbed | — | [arXiv 2307.12126](https://arxiv.org/abs/2307.12126) |

**People counting and placement**

| Work (year) | Type | People handled | Metric (authors) | Hardware | Code / data | Link |
|---|---|---|---|---|---|---|
| Wi-CaL / Choi NAIST D.Eng (2022) | thesis / peer-reviewed | ~5 and ~10 | median abs. error 0.35 / 0.41 (leave-one-session-out); single link 0.55-0.82 | 4 ESP32 links, 100 pkt/s, trained per room | not found | [thesis](https://naist.repo.nii.ac.jp/record/11764/files/R017974.pdf) |
| Torun, Parsay, Mostofi (2025) | preprint | up to 13 seated | MAE 1.04, NMSE 0.15, 51 s; no RF training | Intel 5300 1 TX / 3 RX, 1 m height | not found | [arXiv 2507.14309](https://arxiv.org/abs/2507.14309) |
| Korany & Mostofi (MobiSys 2021) | peer-reviewed | up to 10 seated | 0 or 1 off in 96.3 % of trials (90 % through walls) — press-release figures | Wi-Fi **received power**, 1 link | — | [DOI](https://doi.org/10.1145/3458864.3468012), [press](https://www.eurekalert.org/news-releases/928280) |
| Guo et al., bus passenger counting (2024) | preprint | 0-20 | 94.86 % (2 receivers); 77.36 / 89.98 % single | 2 Raspberry Pi Nexmon receivers | not found | [arXiv 2410.11400](https://arxiv.org/abs/2410.11400) |
| Wall-Proximity Matters (IEEE IoT-J) | peer-reviewed | 5 stationary | +2.1-11.2 % counting with TX 1-1.5 m from a wall | Nexmon 80 MHz, tripods 1 m | not found | [arXiv 2412.13208](https://arxiv.org/html/2412.13208v4) |
| Bersan et al., beam-steerable gateway (UbiComp Companion '25) | peer-reviewed | up to 8 | per-count only in figures; +10 % from beams | Broadcom Wi-Fi 6E 4 × 4 gateway | not found | [arXiv 2606.23710](https://arxiv.org/abs/2606.23710) |
| Cha, Kim, Choi, meta-learning (IEEE IoT-J 2025) | peer-reviewed | 0-15 | > 95 % with 20 adaptation samples in a new room | Atheros 3 × 3 | not found | [arXiv 2502.03117](https://arxiv.org/abs/2502.03117) |
| DASECount (2022) | preprint | 0-8 | > 92-99 % with 5 samples per class in a new domain | Atheros 2 × 3 | not found | [arXiv 2211.10040](https://arxiv.org/abs/2211.10040) |
| Custance et al., WiFlow adapters (2026-01) | preprint | groups of 2/5/9 | 10-shot MAE 0.44; the zero-shot split is ambiguous | 2 × ESP32-WROOM-32U | no link | [arXiv 2601.02203](https://arxiv.org/abs/2601.02203) |
| mmCounter (2025-12) | preprint | up to 7 still | F1 87 % familiar rooms / 60 % unseen rooms | mmWave radar | not found | [arXiv 2512.10357](https://arxiv.org/abs/2512.10357) |
| Koda & Sakamoto, respiratory-space clustering (IEEE Access 2021) | peer-reviewed | 5-7 static | counting improved 85 % over baseline | array radar | not found | [arXiv 2101.12422](https://arxiv.org/abs/2101.12422) |
| Less is More: dilution effect (2026) | preprint | home activity | one link AUC 0.541 vs full 72-link mesh 0.489 | 9 ESP32-C3 nodes | Zenodo (per 10-30 report) | [arXiv 2602.10823](https://arxiv.org/abs/2602.10823) |
| Lambrecht et al., ceiling FMCW vs IR-UWB vs Wi-Fi (2026-08) | preprint | 1 (bedroom) | coarse sleep states >92 % for all three; activity recognition across subjects Wi-Fi 79 % vs UWB 89 % | IWR6843AOP, DW3000, openwifi SDR 20 MHz 1 × 1 | not found | [arXiv 2608.20322](https://arxiv.org/abs/2608.20322) |
| Jiang et al., CO2 occupancy (2016) | preprint | office | 94 % within ±4 people | CO2 | — | [arXiv 1607.05962](https://arxiv.org/abs/1607.05962) |
| Shi et al., footstep-based multi-walker counting (2019) | peer-reviewed | up to 3 walkers | detection up to 90 %, 0.65 m error | floor geophones | — | [DOI](https://doi.org/10.1145/3360773.3360887) |
| ESP32 Wi-Fi CSI fall PoC (2026-11-11) | preprint | 1 | fall accuracy 93.1 % (small study) | 2 ESP32, 3.5 m TX-RX | — | [arXiv 2611.15532](https://arxiv.org/abs/2611.15532) |
| BeamLens (2026-11-12) | preprint | — | activity 90.16 %; pose MPJPE 8.64 cm | Wi-Fi beamforming feedback (BFI), multi-antenna | — | [arXiv 2611.16185](https://arxiv.org/abs/2611.16185) |

**Datasets**

| Dataset | Content | Licence | Link |
|---|---|---|---|
| WiCount | ESP32-S3, 0-3 people, one meeting room; the card says ESP32 TX but the paper says router TX (**conflict**) | not stated | [HF](https://huggingface.co/datasets/RS2002/WiCount) |
| WiVi32-Count (PRoFENCH) | ESP32 + camera, 0-7 people | repo Apache-2.0; data by request | [GitHub](https://github.com/cvwifi-lab/WiVi32-A_People_Counting_Tool) |
| WiMANS | 0-5 users, 11,286 samples, dual band | none stated | [GitHub](https://github.com/huangshk/wimans) |
| OPERAnet | 6 → 0 walkers counting run (exp028), Wi-Fi + UWB | per figshare | [arXiv 2110.04239](https://arxiv.org/abs/2110.04239) |
| Zenodo 10.5281/zenodo.23574119 | ESP32-S3 mesh, used for the project's 2026-11-01 replay | CC BY 4.0 | [DOI](https://doi.org/10.5281/zenodo.23574119); TESTING §5.3 |

No public ESP32 dataset with references for several people's vital signs was found.

### 5.2 Physics limits: why one single-antenna CSI link cannot separate several heartbeats

1. **Range resolution comes from bandwidth.** It is c / (2B).
   - 20 MHz Wi-Fi gives 7.5 m. ViMo and Lambrecht 2026 both state this; Lambrecht says the bin "exceeds the room dimensions". Everyone in the room therefore falls in the same range bin.
   - 802.11ad at 3.52 GHz gives 4.26 cm (ViMo).
   - TI FMCW with about 3-4 GHz of sweep gives 3.75-5.8 cm (Eder, Benny).
   - Acoustic FMCW at 18-23 kHz has 5 kHz of bandwidth. Because sound travels at only about 343 m/s, c / (2B) ≈ 3.4 cm. This figure is computed here, not quoted by the paper. It is why a cheap speaker and mic array can separate people that 20 MHz Wi-Fi cannot.
2. **Angle needs antennas.** One receive antenna gives no angle of arrival.
   - Multi-person systems separate people by angle: SpaceBeat uses 2 × 9 antennas, ViMo 32-antenna arrays, Benny 2TX/4RX.
   - Each states a minimum angular separation: ViMo ≥45°, Xu ≥30-40° (≈0.5 m at 1.6 m), Benny ≥15°; Guan reports about 15° per person at 1.5 m.
3. **Blind-source separation needs at least as many mixtures as sources.** ICA (MultiSense) uses 3 antennas. One C5 antenna gives one mixture per subcarrier. The 26 L-LTF tones are not independent views of each chest, so they cannot stand in for antennas. *That last point is an inference from the theory cited, not a measured result.*
4. **Fresnel-zone sensitivity is position-dependent.**
   - On one link, breathing can fall into blind spots: 98.8 % accuracy at good positions against 61.5 % at bad ones (Zhang 2018, via Gu 2019).
   - FarSense's fix needs 2 antennas. SA-WiSense's cross-subcarrier ratio is the only single-antenna fix, and it was shown at 40 MHz with 218 tones, not the C5's 26 L-LTF tones at 50 Hz.
5. **Heart signals are weak beside breathing.** HR estimators can lock onto a breathing harmonic or onto noise; the project's own KNOWN LIMIT tests reproduce these failure modes (D-174, D-175). Abstaining is the published remedy: HEAR cut HR MAE from 17.9 to 1.6 bpm at 50 % coverage.
6. **Gain distorts multiplicatively.** AGC steps scale the whole CSI. Ratnam 2024 models the gain and phase error. The project logs `agc`/`gain_chg` (D-176), and how to handle the gain is still open.

**What radar does differently.** An FMCW MIMO radar measures range (GHz bandwidth), angle (virtual array) and phase per range-angle cell. Each cell can hold one chest, so the phase of that cell is that person's displacement. Even so:
- Accuracy degrades with distance and crowding: TI's own test went from 79-99.8 % error-free frames with 1 person to 33-41 % with 4-6 people.
- People must be still and separated.
- Products limit vitals to one person because a stated, validated accuracy is hard to hold once a second person enters (inference from the datasheets' one-person rules).

**802.11bf** (published 2025-09-26) standardises the sounding exchange and CSI reporting only. It "does not define use of the measurements" (IEEE 802.11 chair, 2026) and adds no antennas or bandwidth to a 20 MHz single-antenna device.

---

## 6. Videos worldwide

No video was watched and no transcript was fetched. Each row rests on the description, chapters, linked repository and product pages. View counts and dates were read 2026-11-16. Credibility: **high** = measured against a reference and open; **medium** = maker numbers without a reference; **low** = claims with no data or a sponsored or seeded unit.

| Title | Channel | Date | Language | What it shows | Credibility | Link |
|---|---|---|---|---|---|---|
| Can You Hide From WiFi? Build a Radar to Find Out | Talking Sasquach | 2026-11-15 | English | First popular **ESP32-C5** CSI sniffer: ~49 CSI/s on a 4-AP 5 GHz mesh vs ~1/s on 2.4 GHz beacons; breathing 21 vs 21 hand count (one run); HR "not verified"; "no head count" | medium (honest README) | [video](https://www.youtube.com/watch?v=o4IVyARjKyY), [repo](https://github.com/skizzophrenic/Tab5-Monster-C5-CSI-Human-Sensing) |
| DevCon26: no_std Rust CSI collection stack (esp-csi-rs) | Espressif DevCon26 (by Connected Motion) | 2026-11-15 | English | Rust CSI collector with a C5 target; C5/C6 emitter ~90 CSI/s (maker) | medium | [video](https://www.youtube.com/watch?v=g5CDmULguh8), [docs](https://github.com/csi-rs/esp-csi-rs/blob/HEAD/docs/emitter-support.md) |
| Contactless Heartbeat & Respiration Using WiFi, "64 vs 65 BPM" | srk_nd / pvkkr (KL University, India) | 2026-11-02 | English | ESP8266 → ESP32 HR at 40 cm; "~98.5 %" vs a finger pulse sensor; all 5 README trials read high; the GUI defaults to a simulated demo mode | low | [video](https://www.youtube.com/watch?v=W2XJ5NhWQsE), [repo](https://github.com/pvkkr/wifi-vital-signs-radar) |
| ESP32-S3 WiFi CSI Sensing: Real-Board Test | winson-DIY | 2026-11-04 | Chinese (Taiwan), EN subtitles | Motion index 0.031 still vs 0.196 walking; AP mounted high gave only ~40 % contrast | medium | [video](https://www.youtube.com/watch?v=SC--Egz0Z8g) |
| RuView: WiFi Sensing in Practice | CLI Stack | 2026-10-28 | English | Shows that RuView's "proof" validates only a synthetic fixture | high (independent) | [video](https://www.youtube.com/watch?v=OW1megnp2ZE) |
| Probamos RuView ... ¿o fantasmas? | ProjectOfTheWeek | 2026-09-14 | Spanish | Full 6-node RuView install with calibration | medium | [video](https://www.youtube.com/watch?v=yPDViZ_b6jk) |
| Ragnar RuSense: Camera Free Surveillance | Valleytech | 2026-07-08 | English | Claims people count and HR through walls; no data | low | [video](https://www.youtube.com/watch?v=q2XMGHuvNaE) |
| $50 Human Detecting Radar Feels Illegal | Talking Sasquach | 2026-06-21 | English | ESP32-S3 CSI presence; the description claims HR, but the README does not | low-medium (sponsored) | [video](https://www.youtube.com/watch?v=HqTxv7y6sDM) |
| I Turned My Wi-Fi Into a Surveillance System | Better Stack | 2026-10-11 | English | 3 × ESP32-S3, camera-labelled; 79.5 % over 8 classes in a separate session (1 person, 1 room) | medium | [video](https://www.youtube.com/watch?v=9Tihk9_jUyo) |
| TOMMY Sense TESTED / smarterkram | TechteamGB / smarterkram | 2026-07-06 / 2026-06-05 | English / German | ESP32 Wi-Fi zones; products provided by TOMMY | low (provided units) | [1](https://www.youtube.com/watch?v=CiFnWOYetks), [2](https://www.youtube.com/watch?v=rBtFocrYV4o) |
| ESPectre – Bewegungserkennung nur mit WLAN | pixeledi | 2026-06-19 | German | ESPectre motion; placement chapter | medium | [video](https://www.youtube.com/watch?v=JDopYzPLhmw) |
| MobiSys 2021: Counting a Stationary Crowd | ACM SIGMOBILE (UCSB) | 2021-06-23 | English | Up to 10 seated people from Wi-Fi received power; 96.3 % of trials 0 or 1 off (press figures) | high (peer-reviewed) | [video](https://www.youtube.com/watch?v=A0bQTQJJqeQ) |
| ESP-CSI: Transforming Wi-Fi into a Sensing Platform | Espressif | 2025-08-21 | English | Vendor overview; no accuracy | medium (vendor) | [video](https://www.youtube.com/watch?v=JjdpzM6zVJ8) |
| Synaptics AI-Enhanced WiFi Sensing (Embedded World) | Charbax | 2025-03-20 | English | Still vs active presence; breathing as a pattern; "multiple occupants" with more training (no data) | low (sponsored) | [video](https://www.youtube.com/watch?v=FJ9p7wBFTtQ) |
| Xfinity WiFi Motion coverage | LMGClips / Techlore | 2026-09-02 / 2026-08-19 | English | Motion only; data disclosure to third parties | high (independent, on privacy) | [1](https://www.youtube.com/watch?v=UAMkobKSQtY), [2](https://www.youtube.com/watch?v=QxGYRY7Gc0E) |
| I Built a LAMP That Tracks Your Sleep (C1001) | techiesms (India) | 2026-07-27 | English | Single-person HR/BR at chest 0.5-0.8 m; clean 5 V needed | medium | [video](https://www.youtube.com/watch?v=KlZlvOph3Yc) |
| C1001 60 GHz demos | How To Electronics / Electronic Clinic | 2025-05-31 / 2024-08-08 | English | "accurately measures" HR, no reference | low | [1](https://www.youtube.com/watch?v=AgxiWcCyBF4), [2](https://www.youtube.com/watch?v=TL6rvUwu5pM) |
| Seeed MR60BHA2 reviews; 2022 ECG comparison (that clip is the older **MR60BHA1**) | NTTIPS / Sky From Scratch / Seeed | 2026-02 / 2025-08 / 2022-03 | English | Single-person vitals | low-medium | [1](https://www.youtube.com/watch?v=_bqWsTbPzno), [2022](https://www.youtube.com/watch?v=J1JDgFs_4iw) |
| ME72MS02 single-person respiration & HR | MinewSemi | 2026-09-01 | English | Max targets 1; vendor ±5 bpm in 90 % of samples | medium (vendor) | [video](https://www.youtube.com/watch?v=d2I3_CWcWz8) |
| Non-contact breathing for two subjects | AlephZero | 2026-03-02 | English | 2 sleepers at 1.20 / 1.97 m, 19 / 23 BPM, no reference; separated by range | low | [video](https://www.youtube.com/watch?v=Ipf5VPLMiT0) |
| [mLAB] Vital signs of four people on one radar | Millilab | 2023-01-18 | not stated | "vital signs" of 4 people; **HR not named**; no accuracy | low | [video](https://www.youtube.com/watch?v=0wpfyshEUXo) |
| TI mmWave people counting demos | Texas Instruments | 2018-01-08 / 2021-04-27 | English | Multi-person tracking, entry counting | medium (vendor; test numbers in TIDUE71D) | [1](https://www.youtube.com/watch?v=RT56YzqME6M), [2](https://www.youtube.com/watch?v=Rf_ixb9i1Tc) |
| This $10 Radar Tracks You Through a Wall (LD2450) | That Project | 2026-08-23 | English | 3-target tracking through doors and drywall | medium | [video](https://www.youtube.com/watch?v=hhtvjLvmlTY) |
| Aqara FP400 reviews (5) | Smart Home UK, Byte of Geek, Eric Welander, SmartHome yourself, Spiel und Zeug | 2026-09-17 to 2026-10-20 | English / German | "up to 10 people" (vendor); no measured count; one likely seeded unit | low-medium | [1](https://www.youtube.com/watch?v=hIzOpbdBmag), [2](https://www.youtube.com/watch?v=IEFIF8XfHqo) |
| Aqara FP2 install tips / reviews / fall test | Aqara, Tom's Smart Home, ModernDayReviews, HectorziN | 2025-07 to 2026-10 | English / Spanish | Zones, falls (practical within ~6 ft per one reviewer) | medium | [1](https://www.youtube.com/watch?v=Xa7dpl8DhHM), [fall test](https://www.youtube.com/watch?v=VUXGjJFddJM) |
| Ich habe 9 Präsenzmelder getestet / 5 Sensoren im Härtetest | simon42 / Alex Kly | 2026-02-25 / 2026-01-09 | German | Weeks-long comparison with EUR street prices; FP2 "more prone to ghosting"; distance to the sofa decisive | medium-high (independent, qualitative) | [1](https://www.youtube.com/watch?v=TtcnaOMl8IA), [2](https://www.youtube.com/watch?v=nvSHq_kmrlc) |
| Wi-Fi sensing explainers | W Academy (Hindi), sasugajya, ムセンコネクト (Japanese) | 2026-06 / 2026-04 / 2025-06 | Hindi / Japanese | General explainers; smart-plug demo | low-medium | [Hindi](https://www.youtube.com/watch?v=Ol2o4yFzEcY), [JP](https://www.youtube.com/watch?v=vNMmod1kc_8) |
| DIY mmWave sensor (LD2410/LD2420) | Techposts Hindi | 2024-08-21 | Hindi | ESP32-C3 build | medium | [video](https://www.youtube.com/watch?v=u-Jl2HpJnT4) |
| 802.11bf explainers | Connectivity Illustrated, IEEE CS, Oregon ComSoc | 2026-10 / 2024-08 / 2022-09 | English | Standard theory | high (lectures) | [1](https://www.youtube.com/watch?v=j4HMzgtBtHM) |
| ToF counter installs (Milesight VS133, Terabee, FOORIR FP221) | vendors | not retrieved | English | Install steps (not watched) | medium (vendor) | [VS133](https://www.youtube.com/watch?v=I5pTXR4PgDc), [Terabee](https://www.youtube.com/watch?v=AQEiSqtVnkE) |

**Viral and unverified claims.**
- No video, from any technology, shows HR for 2 or more people against a reference.
- No maker Wi-Fi CSI video counts people with measured accuracy. The only Wi-Fi counting video with data is the MobiSys 2021 talk, which uses received power, not CSI.
- RuSense's through-wall counting and HR, srk_nd's "98.5 %", and the "$50 radar" HR description are unsupported or contradicted by their own repositories.
- The Millilab "four people" clip says "vital signs", not HR.
- Coverage gaps: Bilibili returned captcha pages, so no Chinese platform videos were reviewed, and Korean and Japanese multi-person vitals demos were not searched.

---

## 7. Market and price

### 7.1 Price per capability (India first, 2026-11-16 unless dated)

| Capability | Cheapest credible option | Mid | Enterprise / medical | Notes | Sources |
|---|---|---|---|---|---|
| Presence incl. still people, one room | HLK-LD2450 ₹999 incl. GST (Probots) + host MCU; LD2410 ₹379 (Robu, unconfirmed) | Aqara FP2 ₹11,777 (Amazon.in tracker) | Milesight VS370 (price not found) | The ESP32-C5 node costs ₹695-1,522 but has **no measured presence accuracy** | [Probots](https://probots.co.in/hlk-ld2450-24ghz-mmwave-tracking-radar-sensor.html), [FP2 tracker](https://price-history.in/product/aqara-presence-sensor-fp2-bracket-set-OdsjAqG5) |
| Motion only (Wi-Fi sensing at retail) | WiZ SpaceSense: 2 × Neo downlight ≈ ₹3,480 (2 × ₹1,740, Philips India) | Hue Bridge Pro ≈₹13,215 converted (not seen on Indian retail) | ISP gateways (Xfinity; not in India) | WiZ is the only Wi-Fi sensing product found on Indian retail | [Philips India](https://in.shop.lighting.philips.com/products/philips-smart-wifi-neo-led-downlight) |
| Room count 0-3 / 0-5 | LD2450 ₹999 (3 moving) | FP2 ₹11,777 (5, best ≤3); FP400 US$79.99 ≈ ₹7,551 converted (no Indian price found) | Density Waffle US$279 ≈ ₹26,338 + software; Framery EUR 149 + EUR 5.90/mo | All counting accuracy figures are vendor claims | [Density Waffle](https://www.density.io/waffle), [FP400](https://us.aqara.com/products/spatial-multi-sensor-fp400) |
| Door in/out counting | Grid-EYE ₹2,141 bare + DIY | Marketplace counters ₹15,000-45,000 (IndiaMART) | Milesight VS133 ₹56,241; Terabee ₹97,402; Xovis ₹1,00,000 excl. taxes; V-Count US$299-599 + US$9-49/mo | Plus installation (Density quoted US$350-450 per unit) and subscriptions | [V-Count](https://v-count.com/pricing/), [Density quote](https://density.io/pricing/cab2024) |
| HR, one person, contactless | DFRobot C1001 ₹2,758-3,516 (4 sellers) | MR60BHA2 kit ₹3,333 (Fabtolab) to ₹6,999 (Probots, out of stock) | XK300, Neteera, Sleepiz, Dozee: price not found | Cheap modules have no defined accuracy; the C1001 vendor test read 8-17 bpm low | [DigiKey.in C1001](https://www.digikey.in/en/products/detail/dfrobot/SEN0623/24854509), [Fabtolab MR60BHA2](https://www.fabtolab.com/seeed-studio-xiao-60ghz-mmwave-human-breathing-heartbeat-sensor-mr60bha2) |
| HR reference, one person | Polar H10 ₹8,999 per person | Withings Sleep Analyzer ₹19,200-19,700 (unconfirmed) | — | One device per person | [Polar India](https://store-in.polar.com/products/polar-h10-1) |
| **HR for 2+ people from one sensor** | **no priced product exists** | Sakura Tech miRadar8 VSM: price on request, "under development" | Research: TI EVM + PC (price behind login) | The cost of N people is N devices today | [Sakura Tech](https://pr.mono.ipros.com/sakuratech/product/detail/2000530489/), `gap_3_multi_person_hr_adversarial.md` |

Cost scaling: every vitals product with a stated person count is one per person or bed (Dozee, MR60BHA2, Withings, H10), so cost grows linearly with people ([market_price.md](market_price.md)).

### 7.2 India availability (what can actually be bought here)

| Item | Indian seller (2026-11-16) | Status | Source |
|---|---|---|---|
| ESP32-C5-DevKitC-1-N8R8 | DigiKey.in ₹1,426.65 | 267 in stock; the project's N8R4 is marked obsolete at DigiKey and out of stock at CompoIndia (₹1,290 + GST) | [DigiKey.in](https://www.digikey.in/en/products/result?keywords=ESP32-C5-DEVKITC-1) |
| XIAO ESP32-C5 / FireBeetle 2 ESP32-C5 | Evelta ₹695 / ₹972 ex GST | listed | [Evelta](https://www.evelta.com/search.php?search_query=ESP32-C5) |
| HLK-LD2450 | Probots ₹999 incl. GST | in stock | [Probots](https://probots.co.in/hlk-ld2450-24ghz-mmwave-tracking-radar-sensor.html) |
| DFRobot C1001 | DigiKey.in ₹2,758.19 (77 in stock); Fabtolab ₹2,848.96; Evelta ₹2,980 ex GST; techiesms ₹3,500 (out of stock) | in stock at 2+ sellers | [DigiKey.in](https://www.digikey.in/en/products/detail/dfrobot/SEN0623/24854509), [Evelta](https://evelta.com/c1001-60ghz-mmwave-indoor-fall-detection-sensor-for-arduino-esp32-micro-bit-11-meters/) |
| Seeed MR60BHA2 kit | Fabtolab ₹3,332.65 (in stock); Olelectronics ₹4,309 and Probots ₹6,999 (out of stock) | mixed | [Fabtolab](https://www.fabtolab.com/seeed-studio-xiao-60ghz-mmwave-human-breathing-heartbeat-sensor-mr60bha2), [Probots](https://probots.co.in/60ghz-mmwave-sensor-mr60bha2-breathing-and-heartbeat-detection-xiao-esp32c6.html) |
| Aqara FP2 | Amazon.in ₹11,777 (tracker; 12-month low ₹7,999); not on Flipkart | listed | [price-history.in](https://price-history.in/product/aqara-presence-sensor-fp2-bracket-set-OdsjAqG5) |
| Polar H10 | Polar India ₹8,999 (only listed variant out of stock); WOD Armour ₹8,999 | WOD Armour: price shown, stock not read | [WOD Armour](https://www.wodarmour.in/products/polar-h10-heart-rate-sensor), [Polar India](https://store-in.polar.com/products/polar-h10-1) |
| Grid-EYE AMG8833 | DigiKey.in ₹2,140.61-2,588.89 (bare) | listed | [DigiKey.in](https://www.digikey.in/en/products/result?keywords=AMG8833) |
| WiZ SpaceSense lights | Philips India ₹1,740 / ₹1,990 | listed | [Philips India](https://in.shop.lighting.philips.com/products/philips-smart-wifi-neo-led-downlight) |
| Terabee L-XL | DigiKey.in ₹97,402.15 (no IN865 LoRa band) | 8 in stock | [DigiKey.in](https://www.digikey.in/en/products/detail/terabee-sas/TB-PCL-XL-LORA-EU-W/17047393) |
| Butlr | via Propstack (Mumbai), quote | partner since 2022 | [Propstack](https://butlr.com/blog-post-articles/butlr-partnership-propstack) |
| FootfallCam | 5 Indian resellers, quote; IndiaMART ₹45,000 | listed | [resellers](https://footfallcam.com/Home/WhereToBuy/India) |
| Dozee | Indian maker (Bengaluru), hospital sales | price not found | [dozeehealth.ai](https://dozeehealth.ai/) |
| Oura Ring 4 / WHOOP 4.0 | Oura ₹28,900 + ₹599/mo; WHOOP ₹19,999 (Flipkart) | official in India | [Digit](https://www.digit.in/news/wearable-devices/oura-ring-4-debuts-in-india-with-50-health-metrics-starts-at-rs-28900.html), [Flipkart](https://www.flipkart.com/whoop-4-0-12-month-subscription-wearable-health-fitness-activity-tracker-smartwatch/p/itm85144628074bc) |
| Everything Presence, Milesight, Density, XY Sense, Vayyar | INR not found | — | `market_price.md`, `gap_4_install_india_reg_conflicts.md` |

### 7.3 BOM for a DIY multi-sensor validation rig (India, 2026-11-16)

Purpose: give the C5 a second receiver and run reference sensors beside it, so that presence, 0/1/2+ counting and single-person breathing/HR can finally be measured (TESTING §4). The project already owns one C5 board. GST basis varies by seller and is stated per line.

| Item | Role | Qty | Unit price | Seller, GST basis | Subtotal (₹) | Source |
|---|---|---|---|---|---|---|
| ESP32-C5-DevKitC-1-N8R8 | second CSI receiver (cheapest counting lever, §1.6) | 1 | ₹1,426.65 | DigiKey.in, GST not stated, in stock | 1,426.65 | [DigiKey.in](https://www.digikey.in/en/products/result?keywords=ESP32-C5-DEVKITC-1) |
| Polar H10 | HR reference, **one per seated subject** | 2 | ₹8,999 | WOD Armour (live price; Polar India variant out of stock) | 17,998.00 | [WOD Armour](https://www.wodarmour.in/products/polar-h10-heart-rate-sensor), [Polar India](https://store-in.polar.com/products/polar-h10-1) |
| HLK-LD2450 | moving-person count and X/Y reference (3 max); its 24 GHz sweep was not found, and India's 24.00-24.25 GHz SRD row rests on secondary sources | 1 | ₹999 | Probots, incl. GST, in stock | 999.00 | [Probots](https://probots.co.in/hlk-ld2450-24ghz-mmwave-tracking-radar-sensor.html) |
| DFRobot C1001 | single-person breathing/presence reference; stated band 61-61.5 GHz fits India's delicensed slice on paper | 1 | ₹2,758.19 | DigiKey.in, GST not stated, 77 in stock | 2,758.19 | [DigiKey.in](https://www.digikey.in/en/products/detail/dfrobot/SEN0623/24854509), [Evelta](https://evelta.com/c1001-60ghz-mmwave-indoor-fall-detection-sensor-for-arduino-esp32-micro-bit-11-meters/) |
| Panasonic Grid-EYE AMG8833 (bare) | doorway in/out count reference | 1 | ₹2,140.61 | DigiKey.in, GST not stated; breakout and wiring not priced | 2,140.61 | [DigiKey.in](https://www.digikey.in/en/products/result?keywords=AMG8833) |
| Vernier Go Direct Respiration Belt (GDX-RB) | breathing ground truth (educational, no accuracy statement) | 1 | not found | dealer quote | — | [Vernier](https://www.vernier.com/product/go-direct-respiration-belt/) |
| **Total (listed items)** | | | | mixed GST basis | **25,322.45** | sum of rows above |
| Minimum set (C5 + 1 × H10 + LD2450 + C1001) | | | | | 14,182.84 | sum of rows above |
| Optional: Aqara FP2 | presence and count to 5 (best ≤3), zones | 1 | ₹11,777 | Amazon.in tracker; 60 GHz band not found, so India band legality is unknown | +11,777 | [price-history.in](https://price-history.in/product/aqara-presence-sensor-fp2-bracket-set-OdsjAqG5) |

≈ US$268 for the full listed set at 94.40 INR/USD. The MR60BHA2 is deliberately left out: its stated 58-62 GHz sweep lies outside India's 61-61.5 GHz delicensed slice (§8). Avoid C5 GPIO16-22 for any radar UART (sweep note, unverified; §4.2).

### 7.4 Market size, money and churn (analyst figures disagree 2-5×; treat them as signals of hype, not as measurements)

| Segment | Figure | Source (type) |
|---|---|---|
| Occupancy sensors, global | US$3.85 B (2025) → US$9.03 B (2034); India US$0.18 B by 2026 | [Fortune BI](https://fortunebusinessinsights.com/occupancy-sensor-market-111469) (analyst) |
| People counting, global | US$1.25-2.12 B (2025); Polaris US$1.39 B | [Polaris](https://www.polarismarketresearch.com/press-releases/people-counting-system-market) (analyst) |
| People counting, India | US$31.6 M (2024) → US$60.3 M (2029) | MarketsandMarkets (snippet only, page 403, unconfirmed); [MarketsandMarkets (403)](https://www.marketsandmarkets.com/Market-Reports/geography/people-counting-system-market/india) |
| Non-contact vital-signs detectors | US$327.36 M (2025); India US$8.74 M; radar 35.4 % share | [Straits](https://straitsresearch.com/report/non-contact-vital-signs-detector-market) (analyst) |
| Wi-Fi sensing | US$1.2 B (2025) per Market Intelo, which **conflicts** with Wi-Fi NOW's statement that no vendor has substantial sensing revenue | [Market Intelo](https://marketintelo.com/report/wi-fi-sensing-market) (analyst) |
| Deals | ADT buys Origin AI, US$170 M, >200 patents (2026-02-24); Butlr US$38 M Series B (2024-08); Waving EUR 2.6 M pre-seed for Wi-Fi presence (2026-03); Dozee ₹71.5 cr (2025-03) plus ₹86 cr in convertible debentures (board approval 2025-08) | [ADT](https://news.adt.com/corporate-news/adt-acquires-origin-ai-to-power-ai-sensing-and-ambient-intelligence-for-the-home), [Butlr](https://butlr.com/news/butlr-secures-38m-in-series-b-funding), [Bits&Chips](https://bits-chips.com/article/waving-raises-e2-6m-for-trials-with-wi-fi-detection-of-humans/), [Entrackr](https://entrackr.com/exclusive/exclusive-healthtech-startup-dozee-to-raise-10-mn-in-debt-9656670) |
| Exits and discontinuations | Linksys Aware (EOL 2024); Irisys hardware (EOL 2023, still listed on IndiaMART); Axis P8815-2; Amazon Halo Rise (2023) | vendor notices; [Linksys EOL](https://www.linksys.com/pages/linksys-product-end-of-life), [Irisys EOL](https://irisys.net/hubfs/2023%20Update/Docs%20and%20PDFs/Other/Irisys%20Vector%204D%20-%20LTB%20EOL%20Notice%20-%20Final%20-%20All.pdf) |
| Patents | CSI breathing detection densely patented (~130 US grants since 2025, per the 10-30 report); Origin's portfolio now belongs to ADT | `~/ESP32C5_CSI_logs/2026-10-30_research/REPORT.md` |

**Business model:** enterprise counting sells hardware plus a recurring fee plus installation (Density: US$299 per sensor + US$0.99/sqft/yr + US$350-450 install; V-Count: US$9-49 per sensor per month; Framery: EUR 5.90 per space per month). In India, contactless vitals is a hospital remote-patient-monitoring market (Dozee); Tricog and Cardiac Design Labs sell contact ECG devices.

---

## 8. Standards, regulation and privacy

| Item | What it says | Relevance to the C5 project | Source (type) |
|---|---|---|---|
| **IEEE 802.11bf-2025** | WLAN sensing amendment; approved 2025-05-28, published 2025-09-26; defines sounding and CSI reporting for 1-7.125 GHz and >45 GHz; "does not define use of the measurements"; free through the IEEE GET Program | The C5 has no 11bf. The standard sets no accuracy bar and adds no antennas or bandwidth. Infineon ACW741x is the first IoT chip advertising 11bf CSI (vendor) | [IEEE](https://standards.ieee.org/ieee/802.11bf/11574/), [chair slides](https://www.itu.int/en/ITU-T/Workshops-and-Seminars/2026/0711/Documents/7%20-%20Robert%20Stacey.pdf) |
| **IEC 63180:2020 + AMD1:2025** | Method to measure and declare the detection range of PIR detectors (major/minor motion); CHF 210 | The walk-test grid can be borrowed for Wi-Fi/radar commissioning; it has no pass/fail line | [IEC webstore](https://webstore.iec.ch/en/publication/61559) (standard) |
| **FCC 47 CFR 15.255 as amended by FCC 23-35** | Adopted 2023-05-18, effective 2023-08-23. 60 GHz field-disturbance sensors at +10 dBm EIRP across 57-71 GHz with no duty limit; higher power in 57-64 GHz with off-time limits. Names "chest movement detection to determine breathing patterns" | US legal basis for 60 GHz vitals radar | [DA 23-698](https://docs.fcc.gov/public/attachments/DA-23-698A1.pdf) (regulator) |
| **CEPT ERC Rec 70-03** (June 2019 edition) | 57-64 GHz non-specific SRD 100 mW e.i.r.p.; 61-61.5 GHz 100 mW; 24.0-24.25 GHz 100 mW; radiodetermination 24.05-24.25 GHz | Europe. Later editions and the ETSI 60 GHz radar provisions were **not read** | [CEPT](https://docdb.cept.org/download/3312) (regulator) |
| **India WPC delicensed bands** | Delicensed: 2.4 GHz (GSR 45(E)), 5 GHz (GSR 1048(E)), lower 6 GHz, 24 GHz SRD and **only 61-61.5 GHz** in the 60 GHz range (GSR 1047(E) 2018). The 24 GHz row is 24.00-24.25 GHz after GSR 271(E) 2024 (two secondary sources agree; gazette not read); "24.05-24.5 GHz" is the vehicle-radar table | The C5's 2.4/5 GHz bands are fine. C1001 (61-61.5 GHz) fits on paper; MR60BHA2 (58-62 GHz) does not; FP2/FP400 frequency not found; LD2410/LD2450 actual sweep not found, so their fit to 24.00-24.25 GHz is unknown | [TRAI rec.](https://www.trai.gov.in/sites/default/files/2025-12/Recommendation_10122025.pdf), [legitquest](https://www.legitquest.com/act/use-of-low-power-and-very-low-power-short-range-radio-frequency-devices-exemption-from-licensing-requirement-rules-2018/9F4C), [PCN guide](https://pcnindiaglobal.com/2026/07/06/wpc-de-licensed-frequency-bands-india-complete-guide/) |
| **TRAI V-band recommendation (2025-12-10)** | Recommends licence-exempt 57-66 GHz, including "sensing applications (including radar ...)", with interim limits of 40 dBm EIRP indoor and 25 dBm outdoor | **Not yet a rule**: no DoT notification found | [TRAI](https://www.trai.gov.in/sites/default/files/2025-12/Recommendation_10122025.pdf) (regulator) |
| **WPC ETA-SD** (Saral Sanchar) | Every wireless model in a delicensed band needs Equipment Type Approval by self-declaration with an accredited RF test report; about ₹10,000 per model and 4-6 weeks for a first product | Needed for any C5 product sold in India | [PCN guide](https://pcnindiaglobal.com/2026/07/06/wpc-de-licensed-frequency-bands-india-complete-guide/) (secondary) |
| **BIS CRS / TEC MTCTE** | The 72-category CRS list has no Wi-Fi or IoT sensor category, but covers IT power adaptors and CCTV cameras. MTCTE scope for a sensing device: not found (TEC page TLS error) | The plug adaptor needs BIS; a camera-based counter would too | [BIS list](https://www.crsbis.in/BIS/products-bis.do) (regulator) |
| **India DPDP Act 2023 + DPDP Rules 2025 (G.S.R. 846(E))** | Gazette dated 2025-11-13, e-gazette and PIB 2025-11-14. Rules 1, 2, 17-21 on publication; **Rule 4 (consent managers) one year later, so in force since about 13/14 Nov 2026**; Rules 3, 5-16, 22, 23 after 18 months, **about 13/14 May 2027** (notice, security with one-year log retention, breach report within 72 hours). Act s.7(i) gives an employment ground | Anonymous counts are low risk. Gait ID and per-person HR are personal data (inference). The earlier conflict (AZB vs SCC Online "to be notified") is **resolved by the gazette text** in favour of the one-year rule | [gazette PDF](https://www.meity.gov.in/static/uploads/2025/11/53450e6e5dc0bfa85ebd78686cadad39.pdf), [PIB](https://www.pib.gov.in/PressReleasePage.aspx?PRID=2190014) (regulator) |
| **CDSCO Medical Device Rules 2017 + draft MDSW guidance (2025-10-21)** | Software for "diagnosis, prediction, prevention, monitoring, or treatment" is a device; Class A examples include "basic health monitoring software"; no wellness exemption mentioned; still a draft | A displayed HR marketed for health monitoring could fall in Class A/B in India (inference). Keep `hr_bpm_exp` labelled experimental and non-medical | [India Briefing](https://india-briefing.com/news/cdsco-draft-guidance-medical-software-40691.html) (secondary) |
| **FDA General Wellness policy (revised 2026-01-06)** | Non-invasive wellness products may output physiological values; values that "mimic those used clinically" need validation; clinical-accuracy claims are inappropriate | Same conclusion for the US | [Covington](https://www.cov.com/news-and-insights/insights/2026/01/fda-issues-revised-guidance-on-general-wellness-products), [FDA](https://www.fda.gov/media/90652/download) |
| **FDA 510(k) clearances (contactless HR)** | XK300 K202464, Neteera K212143/K231733, Sleepiz K223163, Dozee K221555, Withings Sleep Rx K231667; camera rPPG product code QME (FaceHeart, NuraLogix, Presage). All single patient. The camera bar is RMSE ≤3 bpm | These show the bar a medical HR claim must meet: about 3-5 bpm, one person at rest | [K202464](https://www.accessdata.fda.gov/cdrh_docs/pdf20/K202464.pdf), [K231733](https://www.accessdata.fda.gov/cdrh_docs/pdf23/K231733.pdf), [K223163](https://www.accessdata.fda.gov/cdrh_docs/pdf22/K223163.pdf), [K231667](https://www.accessdata.fda.gov/cdrh_docs/pdf23/K231667.pdf) (regulator) |
| **GDPR / UK ICO** | ICO expects a DPIA before any employee monitoring and that employees be consulted (Baker McKenzie summary); vendors claim "GDPR compliant by design" | A DPIA-like assessment is good practice under DPDP too (inference) | [Baker McKenzie](https://connectontech.bakermckenzie.com/ico-guidance-on-monitoring-of-workers-key-takeaways-for-employers/) (secondary) |
| **Privacy of Wi-Fi sensing in practice** | Comcast may disclose WiFi Motion data to third parties without notice (law enforcement, subpoenas); Wi-Fi sensing works through walls | Through-wall sensing of neighbours is a privacy and false-alarm issue for any C5 deployment | [9to5mac](https://9to5mac.com/2026/08/18/comcast-just-turned-millions-of-xfinity-routers-into-motion-sensors/) |

---

## 9. Recommendations for this project's path to a real product

Each recommendation is tied to the evidence above. None of them changes the repo. They are proposals for the user to decide, following the D-number process.

1. **Keep the claims narrow; this is the main safe position.**
   - Safe after a C5 measurement exists: "detects presence"; "estimates 0 / 1 / 2+ people".
   - Experimental: single-person breathing.
   - Never: HR for more than one person; HR "accuracy"; "counts people" without a stated maximum and tolerance; any medical wording.
   - Evidence: no product with a published count and accuracy does multi-person HR (§1.1-1.2); one 20 MHz antenna cannot separate chests (§5.2); FDA wellness policy and the CDSCO draft (§8). This matches CLAUDE.md's rule never to call counting, HR, recognition or activity validated.
2. **Measure before anything else.**
   - Step 08 has never run on the C5, and no accuracy or occupancy log exists (CLAUDE.md).
   - Run the TESTING §4 sessions with references logged on the same clock: one Polar H10 per seated person (the reference Apple 2026 and Eight Sleep use), plus an LD2450 and/or Grid-EYE for counts and a written occupancy log.
   - BOM: §7.3, ₹14,183-25,322.
3. **Resolve the 11ax beamforming capture issue first (§4.2).**
   - With an AP that beamforms to the board, the shipped config (`beamformed = 0`) got almost no CSI, and `CSI_LLTF_ONLY` got none.
   - Options for the user: (a) disable beamformee on the STA (`he_su_beamformee_disabled`, `vht_su_…`, `vht_mu_…`), which restored 453-483 per 10 s; or (b) accept beamformed CSI, which also restored it, but the precoding may change what the CSI means (inference).
   - Log it in TESTING §5 and DECISIONS; it currently lives only in `~/ESP32C5_CSI_logs/2026-11-05_hw/`.
   - Re-test on the laptop hotspot and on a second 11ax router before choosing.
4. **Add a second C5 receiver before any counting claim.** Every Wi-Fi counting result with numbers uses ≥2 links or a multi-antenna card:
   - Wi-CaL: 4 links, median error 0.35-0.41; one link 0.55-0.82.
   - Bus: 2 receivers 94.86 %; one 77-90 %.
   - Torun: 3 RX antennas.

   Cost about ₹1,427. Caveat: more links are not automatically better; one well-placed link beat a 72-link mesh (preprint). Place each link through the activity area.
5. **Score counting the honest way.** Adopt Density's "Time Within Tolerance": exact for 1-3 people, ±1 for 4-7, ±2 for 8+, target ≥90 %, plus a separate empty-room false-PRESENT rate. This mirrors the project's own 0/1/2+ design (Density Waffle uses 0/1/2/3+). Source: [Density method](https://density.io/resources/the-truth-behind-99-accurate-occupancy-sensors).
6. **If multi-person vitals is a product requirement, add hardware. The C5 cannot do it alone.** Options in order of evidence:
   - (a) One single-person vitals sensor per bed or seat (the model of every cleared device: XK300, Sleepiz, Dozee, Withings).
   - (b) A MIMO FMCW radar with per-person range-angle cells. Research shows 2-16 people at 2-4 bpm MAE with ≥15-45° separation, sensors at about 2 m tilted 10° down (Sumi/Sakamoto; Benny; Eder). No priced product exists; Sakura Tech's claim is unquantified and "under development".
   - (c) Watch acoustic arrays (Tran, DF-Sense) and ESPARGOS (ESP32 phase-coherent arrays, EUR 350 core kit), which give separation from bandwidth or antennas.
   - In India, pick a radar whose stated band fits **61-61.5 GHz** (the C1001 does) or wait for DoT to act on TRAI's 57-66 GHz recommendation (§8).
7. **Do not adopt the MR60BHA2 or FP2 as an India reference without checking the band.** MR60BHA2 is 58-62 GHz (outside 61-61.5 GHz); the FP2's frequency was not found. COMPARISON.md:164 ("the Aqara FP2 senses breathing and heartbeat") is not supported by the FP2 manual or spec sheet; only a press-reported single-person Sleep mode exists. Propose a correction to the user.
8. **Gate any HR output on observability and on presence.** The C1001 invents vitals for 60 s after the person leaves (forum), and HEAR's abstention cut mmWave HR error from 17.9 to 1.6 bpm. Step 10's withholding design matches the literature; keep it, and keep the whose-vitals latch single-person.
9. **Write placement guidance into the README tutorial, from §4.1A and §4.2:**
   - The link crosses where people sit or walk, at body height (~1 m).
   - 1-1.5 m from walls; AP 3-8 m away with RSSI −40 to −70 dBm.
   - One AP on a fixed channel; ping allowed; BLE scan off.
   - Empty-room calibration, repeated after moving anything.
   - Keep fans, pets and vacuums out of the link.
   - These are the rules every vendor and the placement papers agree on.
10. **Research test worth one session:** SA-WiSense's cross-subcarrier CSI ratio, the only published single-antenna fix for Fresnel blind spots. It was shown at 40 MHz with 218 tones. Whether it works on the C5's 26 L-LTF tones at 50 Hz is unknown.
11. **Before any sale:**
    - WPC ETA per model (~₹10,000, 4-6 weeks).
    - BIS for the plug adaptor.
    - DPDP notice and one-year log retention by May 2027 for any identifiable data (gait ID, per-person HR).
    - A freedom-to-operate review: CSI breathing patents are dense, and Origin's >200 patents now belong to ADT.
12. **Competitive position (India):** the only Wi-Fi sensing at retail is WiZ (motion only, ₹1,740 per light), and no ISP offers Wi-Fi motion. The local contactless-vitals company (Dozee) sells single-bed BCG to hospitals. A measured, privacy-friendly 0/1/2+ room sensor at about ₹1,500 per room would be differentiated on price, but only once the accuracy exists.

---

## 10. Evidence limits

**Coverage was cut by budget.**
- The shared web-search budget ran out in every lane, often after 2-5 searches. Most checking used direct page fetches, PDF extraction, openFDA, ClinicalTrials.gov, Crossref and Europe PMC.
- "Not found" therefore often means "not found in a truncated search".
- Not searched at all: Xiaomi/Mi Home, Tuya docs, Hikvision/Dahua radars, ZTE/H3C APs, China Mobile, Panasonic/Murata products, LG/Samsung, Andar (ADT6101P maker, unverified), NXP in-cabin, the TI AWRL6432 page, TI Radar Toolbox multi-person vitals labs, Korean and Japanese ward radars, and Infineon home vitals.

**Not read in full.**
- Most papers were read as abstracts; full texts were read for ViMo, Guan, Benny 2025, Wall-Proximity, Lambrecht, the TU Delft thesis, SpaceBeat, the bus paper and Eder.
- No YouTube video was watched and no transcript fetched; Bilibili returned captchas.

**Vendor-only claims (not measured by anyone independent):**
- Every counting accuracy in §2.4: 95-99.9 %. No independent test of any named counter was found.
- FP2 (5 targets), FP400 (10), LD6001 (8), Minew (15), XY Sense (255), TI "up to 20" (its own test went to 6).
- Radar vitals "90 %" (undefined metric): MR60BHA2, LD6002. ME72MS02 ±5 bpm in 90 % of samples.
- Origin ">99 %"; Huawei "99 %" breathing; Milesight "99 %" fall (single person, no fans or pets).
- Sakura Tech's multi-person HR; Harman's per-occupant HR.
- Dozee HR MAE (vendor-authored); the Apple S12 white paper.

**Failed or blocked sources.**
- TI E2E (403): the claim that TI's stock vitals demo handles one track is snippet-only.
- Robu (403): LD2410 ₹379 unconfirmed.
- Tanotis (404): Hikvision ₹1,04,175 unconfirmed.
- MCCI, DBG (429): VS133 USD and FP2 alternative INR unconfirmed.
- Butlr support, XY Sense help, Emfit manual, the Withings India store, MarketsandMarkets India (403).
- The dot.gov.in gazette for GSR 271(E) (403).
- The TEC MTCTE page (TLS error).
- ACM DL pages (403): Wang 2016 placement numbers read only via Gu 2019.
- Shodhganga (bot check).

**Conflicts left open (shown, not resolved):**

| Item | Conflict | Sources |
|---|---|---|
| MR60BHA2 counting | Seeed firmware 1.6.4 counts "up to three ... reference only", but one user saw only 0 or 1 | [Seeed wiki](https://wiki.seeedstudio.com/getting_started_with_mr60bha2_mmwave_kit/), [forum](https://forum.seeedstudio.com/t/benchmark-results-for-mr60bha2-with-esp32c6-vs-polar-h10-heartbeat-accuracy-concerns/292559) |
| C1001 counting FAQ | Read as "We don't think this sensor can do anything similar" by one lane and as unanswered by another | [wiki](https://wiki.dfrobot.com/SKU_SEN0623_C1001_mmWave_Human_Detection_Sensor) |
| C1001 coverage | Product page: fall coverage "up to 11 m" at 2.7 m; DFRobot's own test: ~4 m fall circle (11 m is the presence range) | [DigiKey test report](https://www.digikey.com/en/maker/tutorials/2025/functional-test-report-of-dfrobot-c1001-mmwave-sensor) |
| TOMMY | "still presence via breathing on ESP32-C5 only" (wifi lane) vs "C5 support not found" (install lane) | [llms.txt](https://www.tommysense.com/llms.txt), [placement](https://www.tommysense.com/docs/device-placement) |
| Density | Waffle US$279 (product page) vs "<$150" tier (competitor page). Sensor type: radar (Open Area, Waffle) vs depth, Class 1 laser (Entry DPU); do not merge them | [Waffle](https://www.density.io/waffle), [competitors](https://density.io/competitors) |
| Everything Presence Pro | "in stock £67" (mmwave lane) vs "all sold out" (gap 4), same day | [shop](https://shop.everythingsmart.io/products/everything-presence-pro) |
| Butlr accuracy | 95 % (vendor blog) vs 85-92 % (trade article citing vendor) | [trade article](https://www.ai-smart-buildings.com/the-sensor-fusion-decision-tree-what-the-butlr-disruptive-partnership-signals-for-cre-occupancy-in-q2-2026/) |
| FP2 | No official wall height (forum 1.8-2.2 m; 2.5-2.8 m is the ceiling height for falls); sleep HR/BR press-only; whether two modes can run at once conflicts across reviews | [manual](https://www.123led.nl/pdf/LAQ00045.pdf), [forum](https://forum.aqara.com/t/boost-fp2-mmwave-radar-performance-accuracy-maximize-accuracy-pro-tips-for-fp2-fp1e-and-more/49552) |
| India 24 GHz band | 24.00-24.25 GHz (2024 amendment, secondary) vs 24.05-24.5 GHz (the vehicle-radar table quoted by guides) | [lexplosion](https://lexplosion.in/ministry-of-communications-revises-exemption-list-for-licensing-requirement-of-low-power-and-very-lower-power-short-range-radio-frequency-devices-or-wireless-equipment/) |
| WiCount | Card: ESP32-S3 TX; paper: home-router TX | [card](https://huggingface.co/datasets/RS2002/WiCount/raw/main/README.md), [paper](https://arxiv.org/html/2412.06861v4) |
| Custance zero-shot | "different environment" vs domains labelled by household size | [arXiv](https://arxiv.org/html/2601.02203v1) |
| Wi-Fi sensing market | US$1.2 B (analyst) vs "no substantial revenue" (Wi-Fi NOW) | [Market Intelo](https://marketintelo.com/report/wi-fi-sensing-market), `market_price.md` |
| Dozee bed count | 15.5K+ (site) vs other figures in older press | [dozeehealth.ai](https://dozeehealth.ai/) |
| Benny et al. 2511.21314 | Peer-reviewed (IEEE Sensors J 2025, theory lane) vs preprint (mmwave and gap-3 lanes); this report follows the theory lane | [arXiv](https://arxiv.org/abs/2511.21314) |
| Exchange rates | Lanes used 93.93 (2026-11-08) and 94.40 / 94.3996 (2026-11-16); conversions are labelled | `wifi_sensing.md`, `market_price.md`, `gap_1_alt_tech_nonrf_tags.md` |

**Other caveats.**
- Weak single-source items: VitalMon two-person HR (title says one person); Pulse-Fi "one user at a time" (search summary); Dozee COMSNETS 2.48 bpm (snippet); Millilab "four people" (says vital signs, not HR); Hex Home availability; Hue Bridge Pro US$139.99 (search summary); the C5 GPIO16-22 rule (from the 10-30 sweep, not re-checked against an Espressif datasheet).
- The 2026-11-05 beamforming result is one session with one AP, recorded only in the log folder; it has not been through the project's TESTING §5 review.
- Nothing in this report was measured on the ESP32-C5 for presence, counting or vitals accuracy. Every accuracy figure is someone else's.

*Report written 2026-11-16 from the lane notes in `~/ESP32C5_CSI_logs/2026-11-16_research/` (outside the repository). Nothing in the repository was changed.*
