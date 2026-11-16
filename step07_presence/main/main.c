/*
 * STEP 07 - Human presence + motion detector
 * -------------------------------------------
 * Goal of this step: turn the two metrics from Step 06 into an actual
 * yes/no answer, with the kind of guards that stop it flickering.
 *
 * You must run Step 06 first. This firmware loads the calibration from NVS
 * and refuses to guess if it is missing or was made for a different CSI
 * shape.
 *
 * THE DECISION LOGIC
 *
 *   m_sigma = (M - m_mean) / m_std      how many standard deviations above
 *   d_sigma = (D - d_mean) / d_std      the empty room we currently are
 *
 *   MOTION branch (primary)
 *       trigger when m_sigma rises above CSI_MOTION_SIGMA
 *       release when it falls below CSI_MOTION_RELEASE   <- hysteresis
 *
 *   STATIC branch (secondary, deliberately hard to trigger)
 *       d_sigma must stay above CSI_STATIC_SIGMA continuously for
 *       CSI_STATIC_SUSTAIN_MS before it is allowed to assert presence.
 *       D also rises for slow environmental drift, so a momentary spike
 *       must not be enough.
 *
 *   PRESENT = motion OR sustained-static, then HELD for
 *   CSI_PRESENCE_HOLD_MS after the last trigger so that a person who
 *   stops moving for a moment does not blink the output.
 *
 * HONEST LIMITS - please read before judging the results
 *   * This detects a human who MOVES: walking, gesturing, shifting weight,
 *     standing up. That is what one antenna of Wi-Fi CSI genuinely gives you.
 *   * A perfectly motionless person is unreliable. The static branch helps
 *     but it is not a solved problem on this hardware.
 *   * It cannot tell WHICH human. Identity recognition is not achievable
 *     from single-antenna CSI amplitude - do not expect it.
 *   * It will also trigger on a large dog, a ceiling fan, or curtains
 *     moving in a draught. It senses changes in radio propagation, not people.
 *   * Recalibrate (rerun Step 06) if you move the board, move furniture,
 *     or change the router channel.
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

static const char *TAG = "step07";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

#define TAU_FAST_S    0.5f
#define TAU_SLOW_S   60.0f

/* MUST match the value Step 06 used when it measured m_mean/m_std, or the
 * thresholds are calibrated against a differently-smoothed signal. */
#define METRIC_SMOOTH 0.15f
#define CSI_QUEUE_LEN 10

/* Thresholds as floats, from the integer Kconfig values. */
#define THR_MOTION   (CONFIG_CSI_MOTION_SIGMA_X100  / 100.0f)
#define THR_RELEASE  (CONFIG_CSI_MOTION_RELEASE_X100 / 100.0f)
#define THR_STATIC   (CONFIG_CSI_STATIC_SIGMA_X100  / 100.0f)

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

static float s_amp[CSI_MAX_SUBC];
static float s_na[CSI_MAX_SUBC];
static float s_scratch[CSI_MAX_SUBC];
static float s_fast[CSI_MAX_SUBC];
static float s_slow[CSI_MAX_SUBC];

/* ------------------------------------------------------------------ */
/*  How the CSI bytes are packed (same as Steps 05/06)                 */
/*  Default: HT/HE-LTF, int8 (imag, real) pairs, 2 bytes/subcarrier.   */
/*  'Capture L-LTF only': the C5 delivers 12-bit samples in LE 16-bit  */
/*  words, 4 bytes/subcarrier. Step 06 stored nsub already decoded.    */
/* ------------------------------------------------------------------ */
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

/* ------------------------------------------------------------------ */
static void led_init(void)
{
#if CONFIG_CSI_LED_GPIO >= 0
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << CONFIG_CSI_LED_GPIO,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(CONFIG_CSI_LED_GPIO, 0);
    ESP_LOGI(TAG, "presence LED on GPIO %d", CONFIG_CSI_LED_GPIO);
#endif
}

static void led_set(bool on)
{
#if CONFIG_CSI_LED_GPIO >= 0
    gpio_set_level(CONFIG_CSI_LED_GPIO, on ? 1 : 0);
#else
    (void)on;
#endif
}

/* ------------------------------------------------------------------ */
/*  Detector task                                                      */
/* ------------------------------------------------------------------ */
static void detector_task(void *arg)
{
    csi_raw_t r;
    bool  ema_init   = false;
    bool  shape_ok   = false;

    float alpha_fast = 1.0f - expf(-1.0f / (s_base.rate_hz * TAU_FAST_S));
    float alpha_slow = 1.0f - expf(-1.0f / (s_base.rate_hz * TAU_SLOW_S));

    /* smoothed metrics */
    float live_m = 0.0f, live_d = 0.0f;
    bool  live_init = false;

    /* decision state */
    bool    motion_flag   = false;
    bool    present       = false;
    int64_t static_since  = 0;      /* when d first crossed, 0 = not crossed */
    int64_t last_trigger  = 0;
    int64_t present_since = 0;      /* start of the current PRESENT episode */
    bool    freeze_expired_logged = false;
    uint32_t accepted = 0, rejected = 0;

    /* no-CSI watchdog: without it a dropped link freezes the last
     * decision - possibly PRESENT, LED on - until packets come back. */
    int64_t last_accepted = esp_timer_get_time();
    bool    no_csi        = false;
    bool    reseed_fast   = false;

    int64_t next_print = esp_timer_get_time() + 250000;
    int64_t next_stat  = esp_timer_get_time() + 10000000;

    ESP_LOGI(TAG, "detector running: alpha_fast %.4f alpha_slow %.6f",
             alpha_fast, alpha_slow);
    ESP_LOGI(TAG, "thresholds: motion %.2f sd (release %.2f), static %.2f sd for %d ms",
             THR_MOTION, THR_RELEASE, THR_STATIC, CONFIG_CSI_STATIC_SUSTAIN_MS);

    while (1) {
        bool    got = xQueueReceive(s_raw_q, &r, pdMS_TO_TICKS(200)) == pdTRUE;
        int64_t now = esp_timer_get_time();

        if (!no_csi && (now - last_accepted) / 1000 >= CONFIG_CSI_NO_CSI_TIMEOUT_MS) {
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
        if (!got) {
            continue;
        }

        /* ---- the packet must match the shape we calibrated on ---- */
        if (r.len != s_base.csi_len) {
            rejected++;
            if (!shape_ok && rejected > 500 && accepted == 0) {
                ESP_LOGE(TAG, "Every packet has length %u but the baseline expects %u.",
                         r.len, s_base.csi_len);
                ESP_LOGE(TAG, "The channel or PHY changed. Rerun Step 06 to recalibrate.");
                rejected = 0;
            }
            continue;
        }
        shape_ok = true;
        accepted++;
        last_accepted = now;
        if (no_csi) {
            no_csi      = false;
            reseed_fast = true;     /* s_fast is stale after the gap */
            live_init   = false;
            ESP_LOGW(TAG, "CSI flowing again - detecting");
        }

        /* ---- amplitude, normalise (identical to Steps 05/06) ---- */
        int off  = s_base.fwi ? 4 : 0;
        int nsub = s_base.nsub;
        for (int k = 0; k < nsub; k++) {
            s_amp[k] = csi_amp(r.buf, off, k);
        }
        float med = median_of(s_amp, nsub, s_scratch);
        if (med < 1.0f) med = 1.0f;
        for (int k = 0; k < nsub; k++) {
            s_na[k] = s_amp[k] / med;
        }

        /* The fast EMA starts from the first packet; the slow one starts
         * from the stored EMPTY-ROOM mean, not from whatever the room looks
         * like at boot. Seeding it from the first packet meant a boot with
         * someone in the room took that person as "empty", and even an
         * empty boot left D inflated for minutes while a single-packet
         * seed decayed on a 60 s time constant. Step 06 seeds the same way
         * before measuring d_mean/d_std, so the thresholds match. */
        if (!ema_init) {
            for (int k = 0; k < nsub; k++) { s_fast[k] = s_na[k]; s_slow[k] = s_base.mu[k]; }
            ema_init = true;
            continue;
        }
        if (reseed_fast) {
            for (int k = 0; k < nsub; k++) { s_fast[k] = s_na[k]; }
            reseed_fast = false;
            continue;
        }

        /* Freeze the slow reference while PRESENT. If it keeps adapting,
         * it absorbs a person within about a minute (a still person then
         * reads "clear") and, worse, reports their DEPARTURE as a change
         * of the same size as their arrival - PRESENT for 1-2 minutes in
         * an empty room. The cap stops a genuine change made while someone
         * was present (a moved chair) from holding PRESENT forever.
         * Residual: a still person who never crosses a threshold is still
         * absorbed, on the 60 s time constant. */
        bool freeze_slow = present &&
            (CONFIG_CSI_SLOW_FREEZE_MAX_S == 0 ||
             (now - present_since) < (int64_t)CONFIG_CSI_SLOW_FREEZE_MAX_S * 1000000);
        if (present && !freeze_slow && !freeze_expired_logged) {
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

        /* ---- the two metrics ---- */
        float sm = 0.0f, sd = 0.0f;
        int   cnt = 0;
        for (int k = 0; k < nsub; k++) {
            if (!s_base.selected[k]) continue;
            float sg = s_base.sigma[k];
            sm += fabsf(s_na[k]   - s_fast[k]) / sg;
            sd += fabsf(s_fast[k] - s_slow[k]) / sg;
            cnt++;
        }
        if (cnt == 0) continue;

        float M = sm / cnt;
        float D = sd / cnt;

        if (!live_init) { live_m = M; live_d = D; live_init = true; }
        live_m += METRIC_SMOOTH * (M - live_m);
        live_d += METRIC_SMOOTH * (D - live_d);

        float m_sigma = (s_base.m_std > 0) ? (live_m - s_base.m_mean) / s_base.m_std : 0.0f;
        float d_sigma = (s_base.d_std > 0) ? (live_d - s_base.d_mean) / s_base.d_std : 0.0f;

        /* ---- MOTION branch, with hysteresis ---- */
        if (!motion_flag && m_sigma >= THR_MOTION) {
            motion_flag = true;
        } else if (motion_flag && m_sigma < THR_RELEASE) {
            motion_flag = false;
        }

        /* ---- STATIC branch, must be sustained ---- */
        bool static_flag = false;
        if (d_sigma >= THR_STATIC) {
            if (static_since == 0) {
                static_since = now;
            } else if ((now - static_since) / 1000 >= CONFIG_CSI_STATIC_SUSTAIN_MS) {
                static_flag = true;
            }
        } else {
            static_since = 0;
        }

        /* ---- combine + hold ---- */
        bool trigger = motion_flag || static_flag;
        if (trigger) {
            last_trigger = now;
        }
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
            ESP_LOGW(TAG, ">>> %s   (motion %.1f sd, deviation %.1f sd)",
                     present ? "HUMAN DETECTED" : "room clear",
                     m_sigma, d_sigma);
        }

        /* ---- live output ---- */
        if (now >= next_print) {
            next_print = now + 250000;    /* 4 Hz */

            int level = (int)(m_sigma * 100.0f / THR_MOTION);
            if (level < 0)   level = 0;
            if (level > 999) level = 999;

            char bar[41];
            int fill = (int)(m_sigma * 40.0f / (THR_MOTION * 2.0f));
            if (fill < 0)  fill = 0;
            if (fill > 40) fill = 40;
            memset(bar, '#', fill);
            memset(bar + fill, '.', 40 - fill);
            bar[40] = 0;

            printf("%-8s |%s| motion %6.1f sd  dev %6.1f sd  level %3d%%  rssi %d\n",
                   present ? "PRESENT" : "empty", bar,
                   m_sigma, d_sigma, level, r.rssi);
        }

        if (now >= next_stat) {
            next_stat = now + 10000000;
            ESP_LOGI(TAG, "stats: %" PRIu32 " accepted, %" PRIu32 " wrong-shape, %" PRIu32 " dropped",
                     accepted, rejected, s_dropped);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  ---- unchanged from Step 06: CSI enable, ping, WiFi ----           */
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
#else
    /* HT/HE-LTF only - no 11g L-LTF, no ACKs (12-bit packed on the C5,
     * see Step 05). */
    wifi_csi_config_t csi_cfg = {
        .enable = 1, .acquire_csi_legacy = 0, .acquire_csi_force_lltf = 0,
        .acquire_csi_ht20 = 1, .acquire_csi_ht40 = 0, .acquire_csi_vht = 0,
        .acquire_csi_su = 1, .acquire_csi_mu = 1, .acquire_csi_dcm = 0,
        .acquire_csi_beamformed = 0, .acquire_csi_he_stbc_mode = 2,
        .val_scale_cfg = 0, .dump_ack_en = 0, .reserved = 0,
    };
#endif
    ESP_ERROR_CHECK(esp_wifi_set_csi_config(&csi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(&csi_rx_callback, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_csi(true));
}

static void ping_noop(esp_ping_handle_t hdl, void *args) { }

/* Called on every GOT_IP (see Step 03): resume, or re-aim if the gateway
 * changed. */
static void ping_start(esp_ip4_addr_t target)
{
    if (s_ping) {
        if (s_ping_target.addr == target.addr) {
            esp_ping_start(s_ping);
            ESP_LOGI(TAG, "ping resumed");
            return;
        }
        esp_ping_stop(s_ping);
        esp_ping_delete_session(s_ping);
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
}

/* Reconnection policy as in Step 03: bounded retries for the first join,
 * forever (1, 2, 4 ... 30 s, timer-driven) after it. */
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

    ESP_LOGI(TAG, "=== Step 07: presence + motion detector ===");

    /* ---- refuse to run without a calibration ---- */
    ret = baseline_load(&s_base);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "");
        ESP_LOGE(TAG, "No usable calibration in NVS (%s).", esp_err_to_name(ret));
        ESP_LOGE(TAG, "Flash Step 06, run it in an EMPTY room, then flash this again.");
        ESP_LOGE(TAG, "Do not erase flash in between - that wipes the baseline.");
        ESP_LOGE(TAG, "");
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            ESP_LOGE(TAG, "waiting - no baseline. Run Step 06 first.");
        }
    }

    ESP_LOGI(TAG, "baseline loaded:");
    ESP_LOGI(TAG, "  csi_len %u, nsub %u, using %u subcarriers, ch %u, %.1f Hz",
             s_base.csi_len, s_base.nsub, s_base.n_selected,
             s_base.channel, s_base.rate_hz);
    ESP_LOGI(TAG, "  empty-room M %.4f +/- %.4f | D %.4f +/- %.4f",
             s_base.m_mean, s_base.m_std, s_base.d_mean, s_base.d_std);

    if (s_base.m_std <= 0.0f || s_base.n_selected < 8) {
        ESP_LOGE(TAG, "Baseline looks degenerate (std %.5f, %u subcarriers).",
                 s_base.m_std, s_base.n_selected);
        ESP_LOGE(TAG, "Rerun Step 06 - the room was probably not empty or the link dropped.");
    }

    led_init();

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
    /* The ping was already started by the GOT_IP handler. */

    xTaskCreate(detector_task, "detect", 10240, NULL, 4, NULL);
}
