/*
 * ============================================================================
 *  STEP 08 - FINAL: ESP32-C5 Wi-Fi CSI human detector
 * ============================================================================
 *
 *  This is the complete system. Everything from Steps 02-07 in one firmware:
 *
 *    - joins your 2.4 GHz WiFi and locks the radio so CSI stays comparable
 *    - pings the gateway to generate a steady stream of received packets
 *    - captures CSI, locks onto one packet shape, extracts amplitude
 *    - normalises away the receiver's automatic gain control
 *    - calibrates itself against an empty room (automatically, on first boot)
 *    - stores that calibration in NVS and reloads it on every boot
 *    - reports PRESENT / EMPTY with a motion level, over serial as JSON
 *      and as a human-readable bar, plus an optional LED
 *    - recalibrates on a button hold, or on a menuconfig flag
 *
 *  FIRST BOOT ON A NEW BOARD
 *    There is no baseline in NVS, so it calibrates itself. You get a
 *    countdown (CSI_CAL_LEAVE_ROOM_S) to leave the room, then about 35
 *    seconds of measurement, then it starts detecting. Nothing else to do.
 *
 *  WHAT THIS DETECTS - please read this before judging the results
 *    * A human who MOVES: walking, gesturing, shifting weight, standing up.
 *      That is what single-antenna Wi-Fi CSI genuinely delivers.
 *    * A perfectly motionless person is UNRELIABLE. The static branch helps
 *      but do not depend on it.
 *    * It CANNOT tell which human it is. Identity recognition is not
 *      achievable from single-antenna CSI amplitude.
 *    * It also responds to a large dog, a ceiling fan, or curtains moving.
 *      It senses changes in radio propagation, not people specifically.
 *    * Recalibrate after moving the board, moving furniture, or changing
 *      the WiFi channel.
 *
 *  JSON OUTPUT (one line every CSI_JSON_PERIOD_MS)
 *    JSON {"t_ms":123456,"state":"detect","present":1,"motion_sd":7.4,
 *          "dev_sd":1.2,"level":148,"rssi":-46,"rate_hz":49.6,"nsub":52}
 * ============================================================================
 */

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_idf_version.h"

/* ESP-IDF v6.0 renamed the 20 MHz bandwidth enum from WIFI_BW_HT20 to
 * WIFI_BW20. This keeps the project building on both v5.5.x and v6.0.x. */
#if ESP_IDF_VERSION_MAJOR >= 6
#define CSI_WIFI_BW20 WIFI_BW20
#else
#define CSI_WIFI_BW20 WIFI_BW_HT20
#endif
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "ping/ping_sock.h"
#include "lwip/inet.h"

#include "csi_baseline.h"

static const char *TAG = "csi-human";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

#define TAU_FAST_S     0.5f
#define TAU_SLOW_S    60.0f
#define SIGMA_FLOOR    0.005f

/* Smoothing applied to M and D. THIS MUST BE IDENTICAL in calibration phase B
 * and in the detect path. Calibrating on the RAW metric while thresholding a
 * SMOOTHED one inflates the stored std, so every later reading looks smaller
 * than it should and the detector systematically under-triggers. */
#define METRIC_SMOOTH  0.15f
#define CSI_QUEUE_LEN 10

#define THR_MOTION   (CONFIG_CSI_MOTION_SIGMA_X100   / 100.0f)
#define THR_RELEASE  (CONFIG_CSI_MOTION_RELEASE_X100 / 100.0f)
#define THR_STATIC   (CONFIG_CSI_STATIC_SIGMA_X100   / 100.0f)

/* ------------------------------------------------------------------ */
typedef struct {
    uint16_t len;
    uint8_t  fwi;
    int8_t   rssi;
    uint8_t  channel;
    int8_t   buf[CSI_MAX_CSI_BYTES];
} csi_raw_t;

static QueueHandle_t      s_raw_q;
static EventGroupHandle_t s_wifi_events;
static int                s_retry_count = 0;
static esp_ip4_addr_t     s_gateway;
static volatile uint32_t  s_dropped = 0;
static esp_timer_handle_t s_reconnect_timer;
static bool               s_connected_once = false;
static esp_ping_handle_t  s_ping = NULL;
static esp_ip4_addr_t     s_ping_target;

static csi_baseline_t s_base;
static bool           s_have_baseline = false;

static float s_amp[CSI_MAX_SUBC];
static float s_na[CSI_MAX_SUBC];
static float s_scratch[CSI_MAX_SUBC];
static float s_fast[CSI_MAX_SUBC];
static float s_slow[CSI_MAX_SUBC];

static double s_a_mean[CSI_MAX_SUBC];
static double s_a_m2[CSI_MAX_SUBC];
static uint32_t s_a_n = 0;

static double s_m_mean, s_m_m2, s_d_mean, s_d_m2;
static uint32_t s_b_n = 0;

/* ------------------------------------------------------------------ */
/*  CSI callback - minimal work, runs in the WiFi task                 */
/* ------------------------------------------------------------------ */
static void csi_rx_callback(void *ctx, wifi_csi_info_t *info)
{
    if (!info || !info->buf || info->len == 0 || info->len > CSI_MAX_CSI_BYTES) {
        return;
    }
    csi_raw_t r;
    r.len     = info->len;
    r.fwi     = info->first_word_invalid ? 1 : 0;
    r.rssi    = info->rx_ctrl.rssi;
    r.channel = info->rx_ctrl.channel;
    memcpy(r.buf, info->buf, info->len);
    if (xQueueSend(s_raw_q, &r, 0) != pdTRUE) {
        s_dropped++;
    }
}

/* ------------------------------------------------------------------ */
/*  How the CSI bytes are packed                                       */
/*                                                                     */
/*  Default mode captures only HT-LTF / HE-LTF (11n / 11ax ping        */
/*  replies): int8 (imag, real) pairs, 2 bytes per subcarrier.         */
/*  'Capture L-LTF only' forces the L-LTF, which the C5 delivers as    */
/*  12-bit samples in little-endian 16-bit words: 4 bytes per          */
/*  subcarrier, 26 subcarriers in the 104 valid bytes of a 106-byte    */
/*  frame. Read as int8 pairs, those bytes give numbers of plausible   */
/*  size that mean nothing. esp-radar decodes C5 L-LTF the same way;   */
/*  lltf_bit_mode stays at its 12-bit default (see Step 05).           */
/* ------------------------------------------------------------------ */
#if CONFIG_CSI_LLTF_ONLY
#define CSI_BYTES_PER_SUBC 4
#else
#define CSI_BYTES_PER_SUBC 2
#endif

static int csi_nsub(uint16_t len, uint8_t fwi)
{
    int off   = fwi ? 4 : 0;
    int valid = (CSI_BYTES_PER_SUBC == 4) ? (len & ~3) : len;
    int n     = (valid - off) / CSI_BYTES_PER_SUBC;
    return n > 0 ? n : 0;
}

static inline float csi_amp(const int8_t *buf, int off, int k)
{
#if CONFIG_CSI_LLTF_ONLY
    const uint8_t *u = (const uint8_t *)buf + off + 4 * k;
    float im = (float)((int16_t)((uint16_t)(u[0] | (u[1] << 8)) << 4) >> 4);
    float re = (float)((int16_t)((uint16_t)(u[2] | (u[3] << 8)) << 4) >> 4);
#else
    float im = (float)buf[off + 2 * k];
    float re = (float)buf[off + 2 * k + 1];
#endif
    return sqrtf(im * im + re * re);
}

/* ------------------------------------------------------------------ */
static float median_of(const float *src, int n, float *scratch)
{
    if (n <= 0) return 0.0f;
    memcpy(scratch, src, n * sizeof(float));
    for (int i = 1; i < n; i++) {
        float v = scratch[i];
        int j = i - 1;
        while (j >= 0 && scratch[j] > v) { scratch[j + 1] = scratch[j]; j--; }
        scratch[j + 1] = v;
    }
    return (n & 1) ? scratch[n / 2] : 0.5f * (scratch[n / 2 - 1] + scratch[n / 2]);
}

/* ------------------------------------------------------------------ */
/*  NVS                                                                */
/* ------------------------------------------------------------------ */
static esp_err_t baseline_load(csi_baseline_t *b)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CSI_NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t sz = sizeof(*b);
    err = nvs_get_blob(h, CSI_NVS_KEY, b, &sz);
    nvs_close(h);
    if (err != ESP_OK) return err;
    if (sz != sizeof(*b) || b->magic != CSI_BASELINE_MAGIC ||
        b->version != CSI_BASELINE_VERSION) {
        return ESP_ERR_INVALID_VERSION;
    }
    return ESP_OK;
}

static esp_err_t baseline_save(const csi_baseline_t *b)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CSI_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, CSI_NVS_KEY, b, sizeof(*b));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

/* ------------------------------------------------------------------ */
/*  LED + button                                                       */
/* ------------------------------------------------------------------ */
static void led_init(void)
{
#if CONFIG_CSI_LED_GPIO >= 0
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << CONFIG_CSI_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT, .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE, .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    ESP_LOGI(TAG, "presence LED on GPIO %d", CONFIG_CSI_LED_GPIO);
#endif
}

static void led_set(bool on)
{
#if CONFIG_CSI_LED_GPIO >= 0
#if CONFIG_CSI_LED_ACTIVE_LOW
    gpio_set_level(CONFIG_CSI_LED_GPIO, on ? 0 : 1);
#else
    gpio_set_level(CONFIG_CSI_LED_GPIO, on ? 1 : 0);
#endif
#else
    (void)on;
#endif
}

static void button_init(void)
{
#if CONFIG_CSI_BUTTON_GPIO >= 0
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << CONFIG_CSI_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE, .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    ESP_LOGI(TAG, "recalibrate button on GPIO %d (hold 2 s)", CONFIG_CSI_BUTTON_GPIO);
#endif
}

/* Returns true once, after the button has been held for 2 seconds. */
static bool button_held_2s(void)
{
#if CONFIG_CSI_BUTTON_GPIO >= 0
    static int64_t down_since = 0;
    static bool    fired = false;

    if (gpio_get_level(CONFIG_CSI_BUTTON_GPIO) == 0) {   /* active low */
        int64_t now = esp_timer_get_time();
        if (down_since == 0) {
            down_since = now;
        } else if (!fired && (now - down_since) > 2000000) {
            fired = true;
            return true;
        }
    } else {
        down_since = 0;
        fired = false;
    }
#endif
    return false;
}

/* ------------------------------------------------------------------ */
/*  Shape auto-lock                                                    */
/* ------------------------------------------------------------------ */
#define MAX_SHAPES 12
static uint16_t s_shape_len[MAX_SHAPES];
static uint32_t s_shape_cnt[MAX_SHAPES];
static int      s_shape_n = 0, s_observed = 0;
static uint16_t s_locked_len = 0;
static int      s_locked_nsub = 0;
static uint8_t  s_locked_fwi = 0, s_locked_ch = 0;

static void shape_observe(const csi_raw_t *r)
{
    for (int i = 0; i < s_shape_n; i++) {
        if (s_shape_len[i] == r->len) { s_shape_cnt[i]++; return; }
    }
    if (s_shape_n < MAX_SHAPES) {
        s_shape_len[s_shape_n] = r->len;
        s_shape_cnt[s_shape_n] = 1;
        s_shape_n++;
    }
}

static float shape_lock(const csi_raw_t *r)
{
    int best = 0;
    uint32_t total = 0;
    for (int i = 0; i < s_shape_n; i++) {
        if (s_shape_cnt[i] > s_shape_cnt[best]) best = i;
        total += s_shape_cnt[i];
    }
    s_locked_len  = s_shape_len[best];
    s_locked_fwi  = r->fwi;
    s_locked_ch   = r->channel;
    s_locked_nsub = csi_nsub(s_locked_len, s_locked_fwi);
    if (s_locked_nsub > CSI_MAX_SUBC) s_locked_nsub = CSI_MAX_SUBC;
    return total ? (100.0f * s_shape_cnt[best] / total) : 0.0f;
}

/* ------------------------------------------------------------------ */
/*  Main pipeline                                                      */
/* ------------------------------------------------------------------ */
typedef enum {
    PH_LOCK = 0,      /* work out the CSI shape                        */
    PH_LEAVE,         /* countdown so the user can leave the room      */
    PH_WARMUP,        /* settle the EMAs, measure the packet rate      */
    PH_A,             /* per-subcarrier noise -> sigma[k]              */
    PH_SELECT,        /* choose usable subcarriers                     */
    PH_B,             /* empty-room noise floor of M and D             */
    PH_SAVE,          /* write the baseline to NVS                     */
    PH_SETTLE,        /* warm the EMAs before detecting                */
    PH_DETECT,        /* the actual job                                */
} phase_t;

static const char *phase_name(phase_t p)
{
    switch (p) {
        case PH_LOCK:   return "lock";
        case PH_LEAVE:  return "leave-room";
        case PH_WARMUP: return "warmup";
        case PH_A:      return "calib-A";
        case PH_SELECT: return "select";
        case PH_B:      return "calib-B";
        case PH_SAVE:   return "save";
        case PH_SETTLE: return "settle";
        case PH_DETECT: return "detect";
        default:        return "?";
    }
}

static void pipeline_task(void *arg)
{
    csi_raw_t r;
    phase_t   phase = PH_LOCK;
    int64_t   phase_start = esp_timer_get_time();
    uint32_t  phase_packets = 0;

    float alpha_fast = 0.1f, alpha_slow = 0.001f;
    float rate_hz    = 1000.0f / CONFIG_CSI_PING_INTERVAL_MS;
    bool  ema_init   = false;

    float live_m = 0.0f, live_d = 0.0f;
    bool  live_init = false;

    bool     motion_flag = false, present = false;
    int64_t  static_since = 0, last_trigger = 0;
    int64_t  present_since = 0;         /* start of the current PRESENT episode */
    bool     freeze_expired_logged = false;
    uint32_t accepted = 0, wrong_shape = 0;

    /* no-CSI watchdog: without it a dropped link or a CSI shape change
     * freezes the last decision - possibly PRESENT, LED on - forever. */
    int64_t  last_accepted = esp_timer_get_time();
    bool     no_csi = false, reseed_fast = false;

    int64_t next_json  = esp_timer_get_time() + 1000000;
    int64_t next_human = esp_timer_get_time() + 250000;
    int64_t next_log   = esp_timer_get_time() + 1000000;

    while (1) {
        /* ---- recalibrate on a long button press ----
         * phase is tested FIRST so the hold timer only advances while we are
         * actually detecting. Otherwise a hold that crosses 2 s during a
         * calibration phase would latch 'fired' and be silently swallowed,
         * and no later press would produce another event until release. */
        if (phase == PH_DETECT && button_held_2s()) {
            ESP_LOGW(TAG, "button held - recalibrating. LEAVE THE ROOM.");
            present      = false;
            motion_flag  = false;
            static_since = 0;
            last_trigger = 0;
            led_set(false);
            /* Re-seed the EMAs from the next packet. Carrying s_fast/s_slow
             * over from the old room state would make phase A's statistics
             * start from a warm-but-wrong reference and phase B measure
             * deviation against a pre-recalibration baseline. */
            ema_init  = false;
            live_init = false;
            phase = PH_LEAVE;
            phase_start = esp_timer_get_time();
            phase_packets = 0;
        }

        if (xQueueReceive(s_raw_q, &r, pdMS_TO_TICKS(100)) != pdTRUE) {
            goto periodic;
        }

        /* ================= PH_LOCK ================= */
        if (phase == PH_LOCK) {
            shape_observe(&r);
            if (++s_observed >= CONFIG_CSI_LOCK_PACKETS) {
                float share = shape_lock(&r);
                ESP_LOGW(TAG, "CSI shape locked: %u bytes, %d subcarriers, fwi=%u, ch=%u (%.1f%%)",
                         s_locked_len, s_locked_nsub, s_locked_fwi, s_locked_ch, share);
                if (share < 90.0f) {
                    ESP_LOGE(TAG, "Only %.1f%% dominant - enable 'Capture L-LTF only'.", share);
                }

                /* Does the stored baseline still apply? A baseline pictures
                 * one channel's subcarriers, so one from another channel is
                 * stale even when the shape matches (docs/DECISIONS.md D-168;
                 * channel 0 on either side = not known, accepted). */
                bool shape_ok = s_have_baseline &&
                                s_base.csi_len == s_locked_len &&
                                s_base.nsub    == (uint16_t)s_locked_nsub &&
                                s_base.fwi     == s_locked_fwi &&
                                s_base.n_selected >= 8 &&
                                s_base.m_std > 0.0f;
                bool same_ch  = s_base.channel == 0 || s_locked_ch == 0 ||
                                s_base.channel == s_locked_ch;
                bool usable   = shape_ok && same_ch;

#if CONFIG_CSI_FORCE_RECALIBRATE
                if (usable) {
                    ESP_LOGW(TAG, "CSI_FORCE_RECALIBRATE is set - ignoring stored baseline.");
                }
                usable = false;
#endif
                if (usable) {
                    rate_hz    = s_base.rate_hz;
                    alpha_fast = 1.0f - expf(-1.0f / (rate_hz * TAU_FAST_S));
                    alpha_slow = 1.0f - expf(-1.0f / (rate_hz * TAU_SLOW_S));
                    ESP_LOGW(TAG, "baseline OK (%u subcarriers, %.1f Hz) - settling then detecting",
                             s_base.n_selected, rate_hz);
                    phase = PH_SETTLE;
                } else {
                    if (!s_have_baseline) {
                        ESP_LOGW(TAG, "no stored baseline - calibrating");
                    } else if (shape_ok && !same_ch) {
                        ESP_LOGW(TAG, "stored baseline was calibrated on channel %u, now channel %u - "
                                      "recalibrating", s_base.channel, s_locked_ch);
                    } else if (shape_ok) {
                        /* Only reachable with CONFIG_CSI_FORCE_RECALIBRATE. */
                        ESP_LOGW(TAG, "recalibrating (CSI_FORCE_RECALIBRATE)");
                    } else {
                        ESP_LOGW(TAG, "stored baseline does not match this CSI shape - recalibrating");
                    }
                    phase = PH_LEAVE;
                }
                phase_start = esp_timer_get_time();
                phase_packets = 0;
            }
            goto periodic;
        }

        /* Every phase past LOCK requires the locked shape. */
        if (r.len != s_locked_len) {
            wrong_shape++;
            goto periodic;
        }
        accepted++;
        phase_packets++;
        last_accepted = esp_timer_get_time();
        if (no_csi) {
            no_csi      = false;
            reseed_fast = true;     /* s_fast is stale after the gap */
            live_init   = false;
            ESP_LOGW(TAG, "CSI flowing again - detecting");
        }

        /* ---- amplitude + normalise ---- */
        {
            int off  = s_locked_fwi ? 4 : 0;
            int nsub = s_locked_nsub;
            for (int k = 0; k < nsub; k++) {
                s_amp[k] = csi_amp(r.buf, off, k);
            }
            float med = median_of(s_amp, nsub, s_scratch);
            if (med < 1.0f) med = 1.0f;
            for (int k = 0; k < nsub; k++) {
                s_na[k] = s_amp[k] / med;
            }

            /* Seeding: the fast EMA always starts from the first packet.
             * The slow EMA - the empty-room reference the static branch
             * compares against - starts from the STORED empty-room mean
             * when a baseline was just accepted (PH_SETTLE). Seeding it
             * from the first packet meant a boot with someone in the room
             * took that person as "empty", and even an empty boot left D
             * inflated for minutes while a one-packet seed decayed on a
             * 60 s time constant. A fresh calibration re-seeds it from the
             * new mu in PH_SELECT, before phase B measures d_mean/d_std. */
            if (!ema_init) {
                for (int k = 0; k < nsub; k++) {
                    s_fast[k] = s_na[k];
                    s_slow[k] = (phase == PH_SETTLE) ? s_base.mu[k] : s_na[k];
                }
                ema_init = true;
            } else if (reseed_fast) {
                for (int k = 0; k < nsub; k++) { s_fast[k] = s_na[k]; }
                reseed_fast = false;
            } else {
                /* Freeze the slow reference while PRESENT. If it keeps
                 * adapting, it absorbs a person within about a minute (a
                 * still person then reads "clear") and reports their
                 * DEPARTURE as a change as large as their arrival -
                 * PRESENT for 1-2 minutes in an empty room. The cap stops
                 * a genuine change made while someone was present (a moved
                 * chair) from holding PRESENT forever. Residual: a still
                 * person who never crosses a threshold is still absorbed,
                 * on the 60 s time constant. */
                int64_t now_us = esp_timer_get_time();
                bool freeze_slow = (phase == PH_DETECT) && present &&
                    (CONFIG_CSI_SLOW_FREEZE_MAX_S == 0 ||
                     (now_us - present_since) < (int64_t)CONFIG_CSI_SLOW_FREEZE_MAX_S * 1000000);
                if (phase == PH_DETECT && present && !freeze_slow && !freeze_expired_logged) {
                    freeze_expired_logged = true;
                    ESP_LOGW(TAG, "PRESENT for over %d s - letting the empty-room reference adapt again",
                             CONFIG_CSI_SLOW_FREEZE_MAX_S);
                }
                for (int k = 0; k < nsub; k++) {
                    s_fast[k] += alpha_fast * (s_na[k] - s_fast[k]);
                    if (!freeze_slow) {
                        s_slow[k] += alpha_slow * (s_na[k] - s_slow[k]);
                    }
                }
            }
        }

        float elapsed = (esp_timer_get_time() - phase_start) / 1e6f;
        int   nsub    = s_locked_nsub;

        switch (phase) {

        /* ================= PH_LEAVE ================= */
        case PH_LEAVE:
            if (elapsed >= CONFIG_CSI_CAL_LEAVE_ROOM_S) {
                phase = PH_WARMUP;
                phase_start = esp_timer_get_time();
                phase_packets = 0;
                ESP_LOGW(TAG, "calibration starting - stay out of the room");
            }
            break;

        /* ================= PH_WARMUP ================= */
        case PH_WARMUP:
            if (elapsed >= CONFIG_CSI_CAL_WARMUP_S) {
                rate_hz = phase_packets / elapsed;
                if (rate_hz < 1.0f) rate_hz = 1.0f;
                alpha_fast = 1.0f - expf(-1.0f / (rate_hz * TAU_FAST_S));
                alpha_slow = 1.0f - expf(-1.0f / (rate_hz * TAU_SLOW_S));
                ESP_LOGI(TAG, "measured %.1f CSI packets/s", rate_hz);

                memset(s_a_mean, 0, sizeof(s_a_mean));
                memset(s_a_m2, 0, sizeof(s_a_m2));
                s_a_n = 0;
                phase = PH_A;
                phase_start = esp_timer_get_time();
                phase_packets = 0;
            }
            break;

        /* ================= PH_A ================= */
        case PH_A:
            s_a_n++;
            for (int k = 0; k < nsub; k++) {
                double x  = s_na[k];
                double d1 = x - s_a_mean[k];
                s_a_mean[k] += d1 / s_a_n;
                s_a_m2[k]   += d1 * (x - s_a_mean[k]);
            }
            if (elapsed >= CONFIG_CSI_CAL_PHASE_A_S) {
                phase = PH_SELECT;
            }
            break;

        /* ================= PH_SELECT ================= */
        case PH_SELECT: {
            int   n_sel   = 0;
            float min_amp = CONFIG_CSI_SELECT_MIN_AMP / 100.0f;
            for (int k = 0; k < nsub; k++) {
                s_base.mu[k]    = (float)s_a_mean[k];
                s_base.sigma[k] = (s_a_n > 1) ? sqrtf((float)(s_a_m2[k] / (s_a_n - 1))) : 0.0f;
                if (s_base.sigma[k] < SIGMA_FLOOR) s_base.sigma[k] = SIGMA_FLOOR;
                s_base.selected[k] = (s_base.mu[k] >= min_amp) ? 1 : 0;
                if (s_base.selected[k]) n_sel++;
            }
            s_base.n_selected = n_sel;

            /* Re-seed the slow EMA at the empty-room mean before phase B.
             * Until now it has been dragging along whatever it saw first -
             * often the user, still walking out - and on a 60 s time
             * constant that leftover would inflate D through phase B and
             * bake a too-high d_mean/d_std into the baseline. */
            for (int k = 0; k < nsub; k++) {
                s_slow[k] = s_base.mu[k];
            }
            ESP_LOGI(TAG, "using %d of %d subcarriers", n_sel, nsub);
            if (n_sel < 8) {
                ESP_LOGE(TAG, "only %d usable subcarriers - lower CSI_SELECT_MIN_AMP", n_sel);
            }
            s_m_mean = s_m_m2 = s_d_mean = s_d_m2 = 0;
            s_b_n = 0;
            phase = PH_B;
            phase_start = esp_timer_get_time();
            phase_packets = 0;
            break;
        }

        /* ================= PH_B ================= */
        case PH_B: {
            float sm = 0, sd = 0;
            int   cnt = 0;
            for (int k = 0; k < nsub; k++) {
                if (!s_base.selected[k]) continue;
                float sg = s_base.sigma[k];
                sm += fabsf(s_na[k]   - s_fast[k]) / sg;
                sd += fabsf(s_fast[k] - s_slow[k]) / sg;
                cnt++;
            }
            if (cnt > 0) {
                float M = sm / cnt, D = sd / cnt;
                /* smooth first, then accumulate - see METRIC_SMOOTH above */
                if (!live_init) { live_m = M; live_d = D; live_init = true; }
                live_m += METRIC_SMOOTH * (M - live_m);
                live_d += METRIC_SMOOTH * (D - live_d);
                s_b_n++;
                double e1 = live_m - s_m_mean;
                s_m_mean += e1 / s_b_n; s_m_m2 += e1 * (live_m - s_m_mean);
                double e2 = live_d - s_d_mean;
                s_d_mean += e2 / s_b_n; s_d_m2 += e2 * (live_d - s_d_mean);
            }
            if (elapsed >= CONFIG_CSI_CAL_PHASE_B_S) {
                phase = PH_SAVE;
            }
            break;
        }

        /* ================= PH_SAVE ================= */
        case PH_SAVE: {
            s_base.magic   = CSI_BASELINE_MAGIC;
            s_base.version = CSI_BASELINE_VERSION;
            s_base.csi_len = s_locked_len;
            s_base.nsub    = nsub;
            s_base.fwi     = s_locked_fwi;
            /* The channel this calibration ran on. s_locked_ch is only the
             * boot channel: Step 08 does not follow a channel change after
             * lock, and PH_LOCK compares this stamp (D-168). */
            s_base.channel = r.channel ? r.channel : s_locked_ch;
            s_base.rate_hz = rate_hz;
            s_base.m_mean  = (float)s_m_mean;
            s_base.m_std   = (s_b_n > 1) ? sqrtf((float)(s_m_m2 / (s_b_n - 1))) : 0.0f;
            s_base.d_mean  = (float)s_d_mean;
            s_base.d_std   = (s_b_n > 1) ? sqrtf((float)(s_d_m2 / (s_b_n - 1))) : 0.0f;

            esp_err_t err = baseline_save(&s_base);
            s_have_baseline = (err == ESP_OK);

            ESP_LOGW(TAG, "=================================================");
            ESP_LOGW(TAG, " CALIBRATED (%s)", esp_err_to_name(err));
            ESP_LOGW(TAG, "   subcarriers %u/%u, %.1f Hz, channel %u",
                     s_base.n_selected, s_base.nsub, s_base.rate_hz, s_base.channel);
            ESP_LOGW(TAG, "   empty M %.4f +/- %.4f   D %.4f +/- %.4f",
                     s_base.m_mean, s_base.m_std, s_base.d_mean, s_base.d_std);
            ESP_LOGW(TAG, "   trigger at M > %.4f, D > %.4f sustained %d ms",
                     s_base.m_mean + THR_MOTION * s_base.m_std,
                     s_base.d_mean + THR_STATIC * s_base.d_std,
                     CONFIG_CSI_STATIC_SUSTAIN_MS);
            ESP_LOGW(TAG, " DETECTING NOW - walk in and watch it fire.");
            ESP_LOGW(TAG, "=================================================");

            if (s_base.m_std <= 0.0f) {
                ESP_LOGE(TAG, "m_std is zero - calibration did not see enough packets.");
            }

            live_init = false;
            phase = PH_DETECT;
            phase_start = esp_timer_get_time();
            break;
        }

        /* ================= PH_SETTLE ================= */
        case PH_SETTLE:
            if (elapsed >= CONFIG_CSI_CAL_WARMUP_S) {
                ESP_LOGW(TAG, "detecting");
                live_init = false;
                phase = PH_DETECT;
                phase_start = esp_timer_get_time();
            }
            break;

        /* ================= PH_DETECT ================= */
        case PH_DETECT: {
            float sm = 0, sd = 0;
            int   cnt = 0;
            for (int k = 0; k < nsub; k++) {
                if (!s_base.selected[k]) continue;
                float sg = s_base.sigma[k];
                sm += fabsf(s_na[k]   - s_fast[k]) / sg;
                sd += fabsf(s_fast[k] - s_slow[k]) / sg;
                cnt++;
            }
            if (cnt == 0) break;

            float M = sm / cnt, D = sd / cnt;
            if (!live_init) { live_m = M; live_d = D; live_init = true; }
            live_m += METRIC_SMOOTH * (M - live_m);
            live_d += METRIC_SMOOTH * (D - live_d);

            float m_sigma = (s_base.m_std > 0) ? (live_m - s_base.m_mean) / s_base.m_std : 0.0f;
            float d_sigma = (s_base.d_std > 0) ? (live_d - s_base.d_mean) / s_base.d_std : 0.0f;

            int64_t now = esp_timer_get_time();

            /* motion branch, hysteresis */
            if (!motion_flag && m_sigma >= THR_MOTION)      motion_flag = true;
            else if (motion_flag && m_sigma < THR_RELEASE)  motion_flag = false;

            /* static branch, must be sustained */
            bool static_flag = false;
            if (d_sigma >= THR_STATIC) {
                if (static_since == 0) static_since = now;
                else if ((now - static_since) / 1000 >= CONFIG_CSI_STATIC_SUSTAIN_MS) static_flag = true;
            } else {
                static_since = 0;
            }

            bool trigger = motion_flag || static_flag;
            if (trigger) last_trigger = now;

            bool new_present = trigger ||
                (present && last_trigger &&
                 (now - last_trigger) / 1000 < CONFIG_CSI_PRESENCE_HOLD_MS);

            if (new_present != present) {
                present = new_present;
                if (present) {
                    present_since = now;
                    freeze_expired_logged = false;
                }
                led_set(present);
                ESP_LOGW(TAG, ">>> %s  (motion %.1f sd, deviation %.1f sd)",
                         present ? "HUMAN DETECTED" : "room clear", m_sigma, d_sigma);
            }

            int level = (int)(m_sigma * 100.0f / THR_MOTION);
            if (level < 0)   level = 0;
            if (level > 999) level = 999;

#if CONFIG_CSI_JSON_PERIOD_MS > 0
            if (now >= next_json) {
                next_json = now + CONFIG_CSI_JSON_PERIOD_MS * 1000LL;
                printf("JSON {\"t_ms\":%" PRIu32 ",\"state\":\"detect\",\"present\":%d,"
                       "\"motion_sd\":%.2f,\"dev_sd\":%.2f,\"level\":%d,"
                       "\"rssi\":%d,\"rate_hz\":%.1f,\"nsub\":%u}\n",
                       (uint32_t)(now / 1000), present ? 1 : 0,
                       m_sigma, d_sigma, level, r.rssi, rate_hz, s_base.n_selected);
            }
#endif

#if CONFIG_CSI_HUMAN_READABLE
            if (now >= next_human) {
                next_human = now + 250000;
                char bar[41];
                int fill = (int)(m_sigma * 40.0f / (THR_MOTION * 2.0f));
                if (fill < 0)  fill = 0;
                if (fill > 40) fill = 40;
                memset(bar, '#', fill);
                memset(bar + fill, '.', 40 - fill);
                bar[40] = 0;
                printf("%-8s |%s| motion %6.1f sd  dev %6.1f sd  level %3d%%  rssi %d\n",
                       present ? "PRESENT" : "empty", bar, m_sigma, d_sigma, level, r.rssi);
            }
#endif
            break;
        }

        default:
            break;
        }

periodic:
        /* ---- no-CSI watchdog (detect phase only) ---- */
        if (phase == PH_DETECT && !no_csi &&
            (esp_timer_get_time() - last_accepted) / 1000 >= CONFIG_CSI_NO_CSI_TIMEOUT_MS) {
            no_csi       = true;
            motion_flag  = false;
            static_since = 0;
            last_trigger = 0;
            if (present) {
                present = false;
                led_set(false);
            }
            ESP_LOGW(TAG, ">>> no usable CSI for %d ms - link down or CSI shape changed. "
                          "Presence UNKNOWN, reporting clear.", CONFIG_CSI_NO_CSI_TIMEOUT_MS);
        }
#if CONFIG_CSI_JSON_PERIOD_MS > 0
        if (no_csi && esp_timer_get_time() >= next_json) {
            next_json = esp_timer_get_time() + CONFIG_CSI_JSON_PERIOD_MS * 1000LL;
            printf("JSON {\"t_ms\":%" PRIu32 ",\"state\":\"no_csi\",\"present\":0}\n",
                   (uint32_t)(esp_timer_get_time() / 1000));
        }
#endif

        if (esp_timer_get_time() >= next_log) {
            next_log = esp_timer_get_time() + 1000000;
            if (phase != PH_DETECT) {
                float el = (esp_timer_get_time() - phase_start) / 1e6f;
                int remain = 0;
                switch (phase) {
                    case PH_LOCK:   remain = 0; break;
                    case PH_LEAVE:  remain = CONFIG_CSI_CAL_LEAVE_ROOM_S - (int)el; break;
                    case PH_WARMUP: remain = CONFIG_CSI_CAL_WARMUP_S     - (int)el; break;
                    case PH_A:      remain = CONFIG_CSI_CAL_PHASE_A_S    - (int)el; break;
                    case PH_B:      remain = CONFIG_CSI_CAL_PHASE_B_S    - (int)el; break;
                    case PH_SETTLE: remain = CONFIG_CSI_CAL_WARMUP_S     - (int)el; break;
                    default: break;
                }
                if (phase == PH_LOCK) {
                    ESP_LOGI(TAG, "[%s] %d/%d packets",
                             phase_name(phase), s_observed, CONFIG_CSI_LOCK_PACKETS);
                } else {
                    ESP_LOGI(TAG, "[%s] %d s left, %" PRIu32 " packets",
                             phase_name(phase), remain > 0 ? remain : 0, phase_packets);
                }
#if CONFIG_CSI_JSON_PERIOD_MS > 0
                printf("JSON {\"t_ms\":%" PRIu32 ",\"state\":\"%s\",\"present\":0,"
                       "\"remaining_s\":%d}\n",
                       (uint32_t)(esp_timer_get_time() / 1000),
                       phase_name(phase), remain > 0 ? remain : 0);
#endif
            } else if (wrong_shape > accepted && accepted > 0) {
                ESP_LOGW(TAG, "most packets have the wrong CSI shape (%" PRIu32 " vs %" PRIu32 ")",
                         wrong_shape, accepted);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/*  CSI enable / ping / WiFi                                           */
/* ------------------------------------------------------------------ */
static void csi_start(void)
{
#if CONFIG_CSI_LLTF_ONLY
    wifi_csi_config_t csi_cfg = {
        .enable = 1, .acquire_csi_legacy = 1, .acquire_csi_force_lltf = 1,
        .acquire_csi_ht20 = 0, .acquire_csi_ht40 = 0, .acquire_csi_vht = 0,
        .acquire_csi_su = 0, .acquire_csi_mu = 0, .acquire_csi_dcm = 0,
        .acquire_csi_beamformed = 0, .acquire_csi_he_stbc_mode = 0,
        .val_scale_cfg = 0, .dump_ack_en = 1, .reserved = 0,
    };
    ESP_LOGI(TAG, "CSI: L-LTF only (12-bit samples)");
#else
    /* HT-LTF / HE-LTF only, both plain int8. Legacy (11g) frames and ACKs
     * are left out: on the C5 their L-LTF is 12-bit packed, and mixing
     * them in gave a ~50/50 two-shape stream in which the lock often
     * picked the 106-byte ACK shape. Trade-off: a router that answers
     * pings with 11g frames now yields no CSI - Step 03 shows that, and
     * 'Capture L-LTF only' is the fallback. */
    wifi_csi_config_t csi_cfg = {
        .enable = 1, .acquire_csi_legacy = 0, .acquire_csi_force_lltf = 0,
        .acquire_csi_ht20 = 1, .acquire_csi_ht40 = 0, .acquire_csi_vht = 0,
        .acquire_csi_su = 1, .acquire_csi_mu = 1, .acquire_csi_dcm = 0,
        .acquire_csi_beamformed = 0, .acquire_csi_he_stbc_mode = 2,
        .val_scale_cfg = 0, .dump_ack_en = 0, .reserved = 0,
    };
    ESP_LOGI(TAG, "CSI: HT/HE-LTF, auto-lock");
#endif
    ESP_ERROR_CHECK(esp_wifi_set_csi_config(&csi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(&csi_rx_callback, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_csi(true));
}

static void ping_noop(esp_ping_handle_t hdl, void *args) { }

/* Called on every GOT_IP: resume the session, or re-aim it if the gateway
 * changed. Errors are logged, not ESP_ERROR_CHECKed - this runs from the
 * event handler, and aborting there on a transient failure would reboot. */
static void ping_start(esp_ip4_addr_t target)
{
    if (s_ping) {
        if (s_ping_target.addr == target.addr) {
            esp_ping_start(s_ping);
            ESP_LOGI(TAG, "ping resumed");
            return;
        }
        esp_ping_stop(s_ping);
        esp_ping_delete_session(s_ping);    /* its task exits by itself */
        s_ping = NULL;
    }

    ip_addr_t target_addr = { 0 };
    target_addr.type = IPADDR_TYPE_V4;
    target_addr.u_addr.ip4.addr = target.addr;

    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    cfg.target_addr = target_addr;
    cfg.count       = ESP_PING_COUNT_INFINITE;
    cfg.interval_ms = CONFIG_CSI_PING_INTERVAL_MS;
    cfg.timeout_ms  = CONFIG_CSI_PING_INTERVAL_MS * 2;
    cfg.data_size   = 32;
    cfg.task_prio   = 2;

    esp_ping_callbacks_t cbs = {
        .on_ping_success = ping_noop, .on_ping_timeout = ping_noop,
        .on_ping_end = ping_noop, .cb_args = NULL,
    };
    esp_err_t err = esp_ping_new_session(&cfg, &cbs, &s_ping);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ping session failed: %s", esp_err_to_name(err));
        s_ping = NULL;
        return;
    }
    s_ping_target = target;
    esp_ping_start(s_ping);
    ESP_LOGI(TAG, "ping -> " IPSTR " every %d ms", IP2STR(&target), CONFIG_CSI_PING_INTERVAL_MS);
}

/* ------------------------------------------------------------------ */
/*  Reconnection policy                                                */
/*  First join: CONFIG_CSI_WIFI_MAX_RETRY attempts, then WIFI_FAIL_BIT */
/*  so app_main can report bad credentials and reboot.                 */
/*  After the first join: never give up. A router reboot or firmware   */
/*  update is off the air for 1-3 minutes, and a board that stops      */
/*  retrying stays offline until someone power-cycles it. Retries back */
/*  off 1, 2, 4 ... 30 s and run from a timer, because a vTaskDelay in */
/*  this handler stalls every other event on the default loop. The     */
/*  ping pauses while the link is down (no send-error flood) and is    */
/*  resumed or re-aimed on the next GOT_IP.                            */
/* ------------------------------------------------------------------ */
static void reconnect_cb(void *arg)
{
    esp_wifi_connect();
}

static void schedule_reconnect(uint32_t delay_ms)
{
    esp_timer_stop(s_reconnect_timer);          /* harmless if idle */
    esp_timer_start_once(s_reconnect_timer, (uint64_t)delay_ms * 1000);
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)data;
        if (s_ping) {
            esp_ping_stop(s_ping);
        }
        if (s_connected_once) {
            uint32_t delay_s = 1u << (s_retry_count < 5 ? s_retry_count : 5);
            if (delay_s > 30) delay_s = 30;
            s_retry_count++;
            ESP_LOGW(TAG, "link lost (reason %d), reconnect attempt %d in %" PRIu32 " s",
                     d->reason, s_retry_count, delay_s);
            schedule_reconnect(delay_s * 1000);
        } else if (s_retry_count < CONFIG_CSI_WIFI_MAX_RETRY) {
            s_retry_count++;
            ESP_LOGW(TAG, "disconnected (reason %d), retry %d/%d",
                     d->reason, s_retry_count, CONFIG_CSI_WIFI_MAX_RETRY);
            schedule_reconnect(1000);
        } else {
            xEventGroupSetBits(s_wifi_events, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        s_gateway = e->ip_info.gw;
        ESP_LOGI(TAG, "got IP " IPSTR ", gateway " IPSTR,
                 IP2STR(&e->ip_info.ip), IP2STR(&e->ip_info.gw));
        s_retry_count = 0;
        s_connected_once = true;
        ping_start(s_gateway);
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void)
{
    s_wifi_events = xEventGroupCreate();

    const esp_timer_create_args_t rt = {
        .callback = reconnect_cb, .name = "wifi_reconnect",
    };
    ESP_ERROR_CHECK(esp_timer_create(&rt, &s_reconnect_timer));

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    wifi_config_t wc = { 0 };
    strncpy((char *)wc.sta.ssid, CONFIG_CSI_WIFI_SSID, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, CONFIG_CSI_WIFI_PASSWORD, sizeof(wc.sta.password) - 1);
    /* Do not offer to be a beamformee (DECISIONS D-177). An 11ax AP that
     * may beamform to us steers every frame, and the CSI config below drops
     * beamformed frames (acquire_csi_beamformed = 0): on an office AP on
     * 2026-11-05 that left ~0 CSI/s, and with these bits ~46/s. Capturing
     * beamformed frames instead would mix the AP's steering matrix into the
     * channel, and it changes whenever the AP re-sounds. */
    wc.sta.he_su_beamformee_disabled  = 1;
    wc.sta.vht_su_beamformee_disabled = 1;
    wc.sta.vht_mu_beamformee_disabled = 1;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Keep CSI comparable: 2.4 GHz, 20 MHz, radio always awake. */
    esp_wifi_set_band_mode(WIFI_BAND_MODE_2G_ONLY);
    esp_wifi_set_bandwidth(WIFI_IF_STA, CSI_WIFI_BW20);
    esp_wifi_set_ps(WIFI_PS_NONE);
}

/* ------------------------------------------------------------------ */
void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "=========================================");
    ESP_LOGI(TAG, " ESP32-C5 Wi-Fi CSI human detector");
    ESP_LOGI(TAG, "=========================================");

    ret = baseline_load(&s_base);
    s_have_baseline = (ret == ESP_OK);
    if (s_have_baseline) {
        ESP_LOGI(TAG, "baseline found: %u bytes CSI, %u/%u subcarriers, ch %u, %.1f Hz",
                 s_base.csi_len, s_base.n_selected, s_base.nsub,
                 s_base.channel, s_base.rate_hz);
    } else {
        ESP_LOGW(TAG, "no baseline (%s) - will calibrate automatically",
                 esp_err_to_name(ret));
    }

    led_init();
    led_set(false);
    button_init();

    s_raw_q = xQueueCreate(CSI_QUEUE_LEN, sizeof(csi_raw_t));
    if (!s_raw_q) {
        ESP_LOGE(TAG, "queue alloc failed");
        return;
    }

    wifi_init_sta();

    EventBits_t bits = xEventGroupWaitBits(s_wifi_events,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, portMAX_DELAY);
    if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "WiFi failed. Check SSID/password (must be 2.4 GHz).");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }

    csi_start();
    /* The ping was already started by the GOT_IP handler. */

    xTaskCreate(pipeline_task, "csi_pipeline", 12288, NULL, 4, NULL);
}
