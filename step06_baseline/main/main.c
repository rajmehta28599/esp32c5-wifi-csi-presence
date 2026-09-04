/*
 * STEP 06 - Empty-room calibration
 * ---------------------------------
 * Goal of this step: teach the board what "nobody here" looks like, and
 * work out the detection thresholds FROM THAT MEASUREMENT rather than from
 * a magic number copied off the internet. The result is written to NVS and
 * reused by Steps 07 and 08.
 *
 * *** RUN THIS WITH THE ROOM EMPTY. Start it, leave, close the door,   ***
 * *** and stay out for the whole ~35 seconds.                          ***
 *
 * THE TWO METRICS (Steps 07 and 08 use exactly these)
 *
 *   For every packet we build the normalised amplitude vector na[k]
 *   (Step 05), then keep two exponential moving averages of it:
 *
 *       fast[k]  time constant ~0.5 s   "what the channel looks like now"
 *       slow[k]  time constant ~60 s    "what the room normally looks like"
 *
 *   MOTION     M = mean over selected k of  |na[k] - fast[k]| / sigma[k]
 *              High when the channel is CHANGING - someone walking, an arm
 *              moving. This is the primary detector: it is what CSI is
 *              genuinely good at.
 *
 *   DEVIATION  D = mean over selected k of  |fast[k] - slow[k]| / sigma[k]
 *              High when the channel has SETTLED somewhere new - someone
 *              came in and is now standing still. Slower and less reliable,
 *              so Step 07 only lets it assert presence after it has been
 *              sustained for several seconds.
 *
 *   Dividing by sigma[k] - the empty-room noise of that individual
 *   subcarrier - is what stops a couple of naturally noisy subcarriers from
 *   dominating the average.
 *
 *   Note that D compares fast against a SLOW MOVING average, not against a
 *   frozen snapshot. That matters: a frozen baseline drifts out of date as
 *   the temperature changes, the router retunes, or furniture moves, and
 *   you would collect false alarms for the rest of the day. The slow EMA
 *   absorbs all of that automatically.
 *
 * WHAT CALIBRATION ACTUALLY MEASURES
 *   Phase A - how noisy is each subcarrier on its own?  -> sigma[k]
 *             and which subcarriers are worth using at all -> selected[k]
 *   Phase B - how much do M and D wobble with nobody there?
 *             -> m_mean/m_std, d_mean/d_std, which become the thresholds.
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
#include "ping/ping_sock.h"
#include "lwip/inet.h"

#include "csi_baseline.h"

static const char *TAG = "step06";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

#define TAU_FAST_S   0.5f
#define TAU_SLOW_S  60.0f
#define SIGMA_FLOOR  0.005f     /* never divide by ~0 */

/* Smoothing applied to the M and D metrics. THIS MUST BE IDENTICAL in
 * calibration phase B and in the live/detect path. If calibration measured
 * the RAW metric while the detector thresholds a SMOOTHED one, the stored
 * std would be too large and every later reading would look artificially
 * small - the detector would systematically under-trigger. */
#define METRIC_SMOOTH 0.15f
#define CSI_QUEUE_LEN 10

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
/*  Shape auto-lock (same idea as Step 05)                             */
/* ------------------------------------------------------------------ */
#define MAX_SHAPES 12
static uint16_t s_shape_len[MAX_SHAPES];
static uint32_t s_shape_cnt[MAX_SHAPES];
static int      s_shape_n = 0, s_observed = 0;
static bool     s_locked = false;
static uint16_t s_locked_len = 0;
static int      s_locked_nsub = 0;
static uint8_t  s_locked_fwi = 0;
static uint8_t  s_locked_ch = 0;

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

static void shape_lock(const csi_raw_t *r)
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
    s_locked_nsub = (s_locked_len - (s_locked_fwi ? 4 : 0)) / 2;
    if (s_locked_nsub > CSI_MAX_SUBC) s_locked_nsub = CSI_MAX_SUBC;
    s_locked = true;

    float share = total ? (100.0f * s_shape_cnt[best] / total) : 0.0f;
    ESP_LOGW(TAG, "shape locked: %u bytes, %d subcarriers, fwi=%u, ch=%u (%.1f%% dominant)",
             s_locked_len, s_locked_nsub, s_locked_fwi, s_locked_ch, share);
    if (share < 90.0f) {
        ESP_LOGE(TAG, "Only %.1f%% dominant. Enable 'Capture L-LTF only' in menuconfig.", share);
    }
}

/* ------------------------------------------------------------------ */
/*  Working state                                                      */
/* ------------------------------------------------------------------ */
static float s_amp[CSI_MAX_SUBC];
static float s_na[CSI_MAX_SUBC];
static float s_scratch[CSI_MAX_SUBC];
static float s_fast[CSI_MAX_SUBC];
static float s_slow[CSI_MAX_SUBC];
static bool  s_ema_init = false;

/* Welford accumulators for phase A (per subcarrier) */
static double s_a_mean[CSI_MAX_SUBC];
static double s_a_m2[CSI_MAX_SUBC];
static uint32_t s_a_n = 0;

/* Welford accumulators for phase B (the two metrics) */
static double s_m_mean = 0, s_m_m2 = 0;
static double s_d_mean = 0, s_d_m2 = 0;
static uint32_t s_b_n = 0;

static csi_baseline_t s_base;   /* ~2 KB, deliberately static not stack */

typedef enum {
    CAL_WAIT_LOCK = 0,
    CAL_WARMUP,
    CAL_PHASE_A,
    CAL_SELECT,
    CAL_PHASE_B,
    CAL_SAVE,
    CAL_LIVE,
} cal_state_t;

static const char *cal_state_name(cal_state_t s)
{
    switch (s) {
        case CAL_WAIT_LOCK: return "locking CSI shape";
        case CAL_WARMUP:    return "warming up";
        case CAL_PHASE_A:   return "phase A - subcarrier noise";
        case CAL_SELECT:    return "selecting subcarriers";
        case CAL_PHASE_B:   return "phase B - metric noise floor";
        case CAL_SAVE:      return "saving";
        case CAL_LIVE:      return "live";
        default:            return "?";
    }
}

/* ------------------------------------------------------------------ */
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
/*  Calibration task                                                   */
/* ------------------------------------------------------------------ */
static void calibration_task(void *arg)
{
    csi_raw_t   r;
    cal_state_t state = CAL_WAIT_LOCK;
    int64_t     phase_start = 0;
    uint32_t    phase_packets = 0;
    float       alpha_fast = 0.1f, alpha_slow = 0.001f;
    float       rate_hz = 50.0f;
    float       live_m = 0.0f, live_d = 0.0f;
    bool        live_init = false;
    int64_t     next_tick = esp_timer_get_time() + 1000000;

    while (1) {
        if (xQueueReceive(s_raw_q, &r, pdMS_TO_TICKS(200)) != pdTRUE) {
            goto tick;
        }

        /* ---------------- shape lock ---------------- */
        if (!s_locked) {
            shape_observe(&r);
            if (++s_observed >= CONFIG_CSI_LOCK_PACKETS) {
                shape_lock(&r);
                state       = CAL_WARMUP;
                phase_start = esp_timer_get_time();
                phase_packets = 0;
                ESP_LOGW(TAG, "");
                ESP_LOGW(TAG, "*** LEAVE THE ROOM NOW. Calibrating for ~%d seconds. ***",
                         CONFIG_CSI_CAL_WARMUP_S + CONFIG_CSI_CAL_PHASE_A_S
                         + CONFIG_CSI_CAL_PHASE_B_S);
                ESP_LOGW(TAG, "");
            }
            goto tick;
        }
        if (r.len != s_locked_len) {
            goto tick;   /* wrong shape - ignore */
        }

        /* ---------------- amplitude + normalise ---------------- */
        int off  = s_locked_fwi ? 4 : 0;
        int nsub = s_locked_nsub;
        for (int k = 0; k < nsub; k++) {
            float im = (float)r.buf[off + 2 * k];
            float re = (float)r.buf[off + 2 * k + 1];
            s_amp[k] = sqrtf(im * im + re * re);
        }
        float med = median_of(s_amp, nsub, s_scratch);
        if (med < 1.0f) med = 1.0f;
        for (int k = 0; k < nsub; k++) {
            s_na[k] = s_amp[k] / med;
        }

        /* ---------------- EMAs ---------------- */
        if (!s_ema_init) {
            for (int k = 0; k < nsub; k++) { s_fast[k] = s_na[k]; s_slow[k] = s_na[k]; }
            s_ema_init = true;
        } else {
            for (int k = 0; k < nsub; k++) {
                s_fast[k] += alpha_fast * (s_na[k] - s_fast[k]);
                s_slow[k] += alpha_slow * (s_na[k] - s_slow[k]);
            }
        }

        phase_packets++;
        float elapsed = (esp_timer_get_time() - phase_start) / 1e6f;

        /* ---------------- state machine ---------------- */
        switch (state) {

        case CAL_WARMUP:
            if (elapsed >= CONFIG_CSI_CAL_WARMUP_S) {
                /* Now we know the real packet rate -> proper EMA constants. */
                rate_hz = phase_packets / elapsed;
                if (rate_hz < 1.0f) rate_hz = 1.0f;
                alpha_fast = 1.0f - expf(-1.0f / (rate_hz * TAU_FAST_S));
                alpha_slow = 1.0f - expf(-1.0f / (rate_hz * TAU_SLOW_S));

                ESP_LOGI(TAG, "packet rate %.1f Hz -> alpha_fast %.4f, alpha_slow %.6f",
                         rate_hz, alpha_fast, alpha_slow);

                memset(s_a_mean, 0, sizeof(s_a_mean));
                memset(s_a_m2, 0, sizeof(s_a_m2));
                s_a_n = 0;
                state = CAL_PHASE_A;
                phase_start = esp_timer_get_time();
                phase_packets = 0;
                ESP_LOGI(TAG, ">>> phase A: measuring per-subcarrier noise");
            }
            break;

        case CAL_PHASE_A: {
            /* Welford, one accumulator per subcarrier. */
            s_a_n++;
            for (int k = 0; k < nsub; k++) {
                double x  = s_na[k];
                double d1 = x - s_a_mean[k];
                s_a_mean[k] += d1 / s_a_n;
                s_a_m2[k]   += d1 * (x - s_a_mean[k]);
            }
            if (elapsed >= CONFIG_CSI_CAL_PHASE_A_S) {
                state = CAL_SELECT;
            }
            break;
        }

        case CAL_SELECT: {
            int n_sel = 0;
            float min_amp = CONFIG_CSI_SELECT_MIN_AMP / 100.0f;

            for (int k = 0; k < nsub; k++) {
                s_base.mu[k]    = (float)s_a_mean[k];
                s_base.sigma[k] = (s_a_n > 1)
                                ? sqrtf((float)(s_a_m2[k] / (s_a_n - 1)))
                                : 0.0f;
                if (s_base.sigma[k] < SIGMA_FLOOR) {
                    s_base.sigma[k] = SIGMA_FLOOR;
                }
                /* Drop null / guard subcarriers: they sit near zero and
                 * carry no channel information, only noise.            */
                s_base.selected[k] = (s_base.mu[k] >= min_amp) ? 1 : 0;
                if (s_base.selected[k]) n_sel++;
            }
            s_base.n_selected = n_sel;

            ESP_LOGI(TAG, ">>> selected %d of %d subcarriers (mean amp >= %.2f)",
                     n_sel, nsub, min_amp);
            if (n_sel < 8) {
                ESP_LOGE(TAG, "Only %d usable subcarriers - that is too few.", n_sel);
                ESP_LOGE(TAG, "Lower CSI_SELECT_MIN_AMP, or move closer to the router.");
            }

            s_m_mean = s_m_m2 = s_d_mean = s_d_m2 = 0;
            s_b_n = 0;
            state = CAL_PHASE_B;
            phase_start = esp_timer_get_time();
            phase_packets = 0;
            ESP_LOGI(TAG, ">>> phase B: measuring the empty-room noise floor of M and D");
            break;
        }

        case CAL_PHASE_B: {
            float sm = 0.0f, sd = 0.0f;
            int   cnt = 0;
            for (int k = 0; k < nsub; k++) {
                if (!s_base.selected[k]) continue;
                float sg = s_base.sigma[k];
                sm += fabsf(s_na[k]   - s_fast[k]) / sg;
                sd += fabsf(s_fast[k] - s_slow[k]) / sg;
                cnt++;
            }
            if (cnt > 0) {
                float M = sm / cnt;
                float D = sd / cnt;

                /* Smooth FIRST, then accumulate - the detector thresholds
                 * the smoothed value, so the baseline must describe the
                 * smoothed value too. */
                if (!live_init) { live_m = M; live_d = D; live_init = true; }
                live_m += METRIC_SMOOTH * (M - live_m);
                live_d += METRIC_SMOOTH * (D - live_d);

                s_b_n++;
                double d1 = live_m - s_m_mean;
                s_m_mean += d1 / s_b_n;
                s_m_m2   += d1 * (live_m - s_m_mean);

                double d2 = live_d - s_d_mean;
                s_d_mean += d2 / s_b_n;
                s_d_m2   += d2 * (live_d - s_d_mean);
            }
            if (elapsed >= CONFIG_CSI_CAL_PHASE_B_S) {
                state = CAL_SAVE;
            }
            break;
        }

        case CAL_SAVE: {
            s_base.magic      = CSI_BASELINE_MAGIC;
            s_base.version    = CSI_BASELINE_VERSION;
            s_base.csi_len    = s_locked_len;
            s_base.nsub       = nsub;
            s_base.fwi        = s_locked_fwi;
            s_base.channel    = s_locked_ch;
            s_base.rate_hz    = rate_hz;
            s_base.m_mean     = (float)s_m_mean;
            s_base.m_std      = (s_b_n > 1) ? sqrtf((float)(s_m_m2 / (s_b_n - 1))) : 0.0f;
            s_base.d_mean     = (float)s_d_mean;
            s_base.d_std      = (s_b_n > 1) ? sqrtf((float)(s_d_m2 / (s_b_n - 1))) : 0.0f;

            esp_err_t err = baseline_save(&s_base);

            ESP_LOGW(TAG, "");
            ESP_LOGW(TAG, "===================================================");
            ESP_LOGW(TAG, "  CALIBRATION COMPLETE  (%s)", esp_err_to_name(err));
            ESP_LOGW(TAG, "===================================================");
            ESP_LOGW(TAG, "  CSI length        : %u bytes", s_base.csi_len);
            ESP_LOGW(TAG, "  subcarriers       : %u  (using %u)",
                     s_base.nsub, s_base.n_selected);
            ESP_LOGW(TAG, "  channel           : %u", s_base.channel);
            ESP_LOGW(TAG, "  packet rate       : %.1f Hz", s_base.rate_hz);
            ESP_LOGW(TAG, "  motion  M (empty) : mean %.4f  std %.4f",
                     s_base.m_mean, s_base.m_std);
            ESP_LOGW(TAG, "  deviate D (empty) : mean %.4f  std %.4f",
                     s_base.d_mean, s_base.d_std);
            ESP_LOGW(TAG, "  -> Step 07 will threshold at about");
            ESP_LOGW(TAG, "       M > %.4f   (mean + 5 std)",
                     s_base.m_mean + 5.0f * s_base.m_std);
            ESP_LOGW(TAG, "       D > %.4f   (mean + 5 std, sustained)",
                     s_base.d_mean + 5.0f * s_base.d_std);
            ESP_LOGW(TAG, "===================================================");
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "NVS save FAILED - Step 07 will not find a baseline.");
            } else {
                ESP_LOGW(TAG, "Saved to NVS. Steps 07 and 08 will load it on boot.");
            }
            ESP_LOGW(TAG, "");
            ESP_LOGW(TAG, "NOW WALK BACK IN and watch M jump. That is your proof");
            ESP_LOGW(TAG, "the pipeline works before you add any decision logic.");
            ESP_LOGW(TAG, "");

            state = CAL_LIVE;
            phase_start = esp_timer_get_time();
            break;
        }

        case CAL_LIVE: {
            /* Keep computing M and D so the user can sanity-check by
             * walking around. No decision logic yet - that is Step 07. */
            float sm = 0.0f, sd = 0.0f;
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
                if (!live_init) { live_m = M; live_d = D; live_init = true; }
                live_m += METRIC_SMOOTH * (M - live_m);
                live_d += METRIC_SMOOTH * (D - live_d);

                if (esp_timer_get_time() >= next_tick) {
                    next_tick = esp_timer_get_time() + 250000;   /* 4 Hz */
                    float m_sig = (s_base.m_std > 0)
                                ? (live_m - s_base.m_mean) / s_base.m_std : 0.0f;
                    float d_sig = (s_base.d_std > 0)
                                ? (live_d - s_base.d_mean) / s_base.d_std : 0.0f;

                    /* Simple text bar so it is readable on a plain monitor. */
                    char bar[41];
                    int fill = (int)(m_sig * 2.0f);
                    if (fill < 0)  fill = 0;
                    if (fill > 40) fill = 40;
                    memset(bar, '#', fill);
                    memset(bar + fill, '.', 40 - fill);
                    bar[40] = 0;

                    printf("M %7.4f (%+6.1f sd) | D %7.4f (%+6.1f sd) |%s|\n",
                           live_m, m_sig, live_d, d_sig, bar);
                }
            }
            break;
        }

        default:
            break;
        }

tick:
        if (esp_timer_get_time() >= next_tick && state != CAL_LIVE) {
            next_tick = esp_timer_get_time() + 1000000;
            float elapsed = phase_start ? (esp_timer_get_time() - phase_start) / 1e6f : 0.0f;
            ESP_LOGI(TAG, "[%s] %.0f s, %" PRIu32 " packets, %" PRIu32 " dropped",
                     cal_state_name(state), elapsed, phase_packets, s_dropped);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  ---- unchanged from Step 05: CSI enable, ping, WiFi ----           */
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
    ESP_LOGW(TAG, "CSI mode: L-LTF only");
#else
    wifi_csi_config_t csi_cfg = {
        .enable = 1, .acquire_csi_legacy = 1, .acquire_csi_force_lltf = 0,
        .acquire_csi_ht20 = 1, .acquire_csi_ht40 = 0, .acquire_csi_vht = 0,
        .acquire_csi_su = 1, .acquire_csi_mu = 1, .acquire_csi_dcm = 0,
        .acquire_csi_beamformed = 0, .acquire_csi_he_stbc_mode = 2,
        .val_scale_cfg = 0, .dump_ack_en = 1, .reserved = 0,
    };
    ESP_LOGW(TAG, "CSI mode: all LTFs, auto-lock");
#endif
    ESP_ERROR_CHECK(esp_wifi_set_csi_config(&csi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(&csi_rx_callback, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_csi(true));
}

static void ping_noop(esp_ping_handle_t hdl, void *args) { }

static void ping_start(esp_ip4_addr_t target)
{
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
    esp_ping_handle_t ping;
    ESP_ERROR_CHECK(esp_ping_new_session(&cfg, &cbs, &ping));
    ESP_ERROR_CHECK(esp_ping_start(ping));
    ESP_LOGI(TAG, "ping -> " IPSTR " every %d ms", IP2STR(&target), CONFIG_CSI_PING_INTERVAL_MS);
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)data;
        if (s_retry_count < CONFIG_CSI_WIFI_MAX_RETRY) {
            s_retry_count++;
            ESP_LOGW(TAG, "disconnected (reason %d), retry %d/%d",
                     d->reason, s_retry_count, CONFIG_CSI_WIFI_MAX_RETRY);
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_wifi_events, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        s_gateway = e->ip_info.gw;
        ESP_LOGI(TAG, "got IP " IPSTR ", gateway " IPSTR,
                 IP2STR(&e->ip_info.ip), IP2STR(&e->ip_info.gw));
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void)
{
    s_wifi_events = xEventGroupCreate();
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

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

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

    ESP_LOGI(TAG, "=== Step 06: empty-room calibration ===");
    ESP_LOGW(TAG, "Make sure the room is EMPTY before the countdown starts.");

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
        ESP_LOGE(TAG, "WiFi failed - go back to Step 02.");
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }

    csi_start();
    ping_start(s_gateway);

    xTaskCreate(calibration_task, "cal", 10240, NULL, 4, NULL);
}
