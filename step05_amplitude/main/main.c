/*
 * STEP 05 - Raw CSI  ->  clean per-subcarrier amplitude
 * ------------------------------------------------------
 * Goal of this step: turn the raw byte buffer into a stable, comparable
 * amplitude vector, and stream it as CSV so you can SEE it on a plot.
 *
 * THREE PROBLEMS THIS STEP SOLVES. Each one silently ruins a detector.
 *
 * 1. THE SHAPE KEEPS CHANGING.
 *    Your router mixes 11g, HT and HE frames, so consecutive packets can
 *    carry different numbers of subcarriers. Averaging across shapes is
 *    meaningless. Fix: watch the first CONFIG_CSI_LOCK_PACKETS packets,
 *    lock onto the most common length, discard everything else.
 *
 * 2. THE RADIO'S AGC RESCALES EVERYTHING.
 *    The receiver's automatic gain control changes gain packet to packet.
 *    That multiplies every subcarrier by a random-looking constant, which
 *    looks exactly like "the whole channel changed" to a variance detector.
 *    Fix: divide each packet by its own MEDIAN amplitude. Motion changes the
 *    SHAPE of the amplitude curve across subcarriers; AGC changes only its
 *    overall scale. Dividing by the median removes the scale and keeps the
 *    shape. Median, not mean, because null/guard subcarriers sit near zero
 *    and drag a mean around as their count shifts with PHY format.
 *
 * 3. THE FIRST FOUR BYTES CAN BE GARBAGE.
 *    If first_word_invalid is set, bytes 0..3 are invalid due to a hardware
 *    limitation. Skip them - do not average them in.
 *
 * DIAGNOSTIC TO WATCH
 *    Once per second this prints the median amplitude (the divisor itself)
 *    with its min/max over that second. On a static, empty scene it should
 *    be roughly steady. If it jumps by 2x or 4x between packets, the
 *    hardware is auto-scaling hard - which is fine, the normalisation is
 *    handling it, but you should know that is what you are looking at.
 *
 * CSV FORMAT (one line per accepted packet, decimated):
 *    CSI,<seq>,<ms>,<rssi>,<nsub>,<median_x100>,<a0>,<a1>,...,<aN-1>
 * where each aK is the normalised amplitude x 100 (so 100 == the median).
 * Feed it to tools/csi_monitor.py to get a live waterfall plot.
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
#include "ping/ping_sock.h"
#include "lwip/inet.h"

static const char *TAG = "step05";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

/* Biggest CSI buffer we will accept. HE20 with several LTFs concatenated
 * is the worst case; anything longer is dropped rather than truncated. */
#define MAX_CSI_BYTES  768
#define MAX_SUBC       (MAX_CSI_BYTES / 2)
#define CSI_QUEUE_LEN  10

/* ------------------------------------------------------------------ */
/*  Raw packet handed from the WiFi task to our worker task.           */
/* ------------------------------------------------------------------ */
typedef struct {
    uint16_t len;
    uint8_t  fwi;
    uint8_t  bb_format;
    int8_t   rssi;
    uint8_t  channel;
    uint32_t t_ms;
    int8_t   buf[MAX_CSI_BYTES];
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

/* ------------------------------------------------------------------ */
/*  CSI callback. Copy and get out - no maths in the WiFi task.        */
/* ------------------------------------------------------------------ */
static void csi_rx_callback(void *ctx, wifi_csi_info_t *info)
{
    if (!info || !info->buf || info->len == 0 || info->len > MAX_CSI_BYTES) {
        return;
    }

    csi_raw_t r;
    r.len       = info->len;
    r.fwi       = info->first_word_invalid ? 1 : 0;
    r.bb_format = info->rx_ctrl.cur_bb_format;
    r.rssi      = info->rx_ctrl.rssi;
    r.channel   = info->rx_ctrl.channel;
    r.t_ms      = (uint32_t)(esp_timer_get_time() / 1000);
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
/*  size that mean nothing - Step 04 shows both readings side by side. */
/*  esp-radar decodes C5 L-LTF exactly this way. lltf_bit_mode is left */
/*  at its 12-bit default on purpose: it only exists in newer IDF      */
/*  headers, and the one public report of setting it saw no change.   */
/* ------------------------------------------------------------------ */
#if CONFIG_CSI_LLTF_ONLY
#define CSI_BYTES_PER_SUBC 4
#else
#define CSI_BYTES_PER_SUBC 2
#endif

/* Subcarriers in a buffer of this length. off = 4 bytes skipped when
 * first_word_invalid is set (one 12-bit subcarrier, two int8 ones). */
static int csi_nsub(uint16_t len, uint8_t fwi)
{
    int off   = fwi ? 4 : 0;
    int valid = (CSI_BYTES_PER_SUBC == 4) ? (len & ~3) : len;
    int n     = (valid - off) / CSI_BYTES_PER_SUBC;
    return n > 0 ? n : 0;
}

/* Amplitude of subcarrier k, starting off bytes into the buffer. */
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
/*  Median of a float array (destroys a scratch copy).                 */
/*  n <= 384, insertion sort is plenty fast and has no allocation.     */
/* ------------------------------------------------------------------ */
static float median_of(const float *src, int n, float *scratch)
{
    if (n <= 0) {
        return 0.0f;
    }
    memcpy(scratch, src, n * sizeof(float));
    for (int i = 1; i < n; i++) {
        float v = scratch[i];
        int j = i - 1;
        while (j >= 0 && scratch[j] > v) {
            scratch[j + 1] = scratch[j];
            j--;
        }
        scratch[j + 1] = v;
    }
    return (n & 1) ? scratch[n / 2]
                   : 0.5f * (scratch[n / 2 - 1] + scratch[n / 2]);
}

/* ------------------------------------------------------------------ */
/*  Shape auto-lock                                                    */
/* ------------------------------------------------------------------ */
#define MAX_SHAPES 12
static uint16_t s_shape_len[MAX_SHAPES];
static uint32_t s_shape_cnt[MAX_SHAPES];
static int      s_shape_n      = 0;
static uint32_t s_observed     = 0;
static bool     s_locked       = false;
static uint16_t s_locked_len   = 0;
static int      s_locked_nsub  = 0;
static uint8_t  s_locked_fwi   = 0;

static void shape_observe(const csi_raw_t *r)
{
    for (int i = 0; i < s_shape_n; i++) {
        if (s_shape_len[i] == r->len) {
            s_shape_cnt[i]++;
            return;
        }
    }
    if (s_shape_n < MAX_SHAPES) {
        s_shape_len[s_shape_n] = r->len;
        s_shape_cnt[s_shape_n] = 1;
        s_shape_n++;
    }
}

static void shape_lock(uint8_t fwi)
{
    int best = 0;
    for (int i = 1; i < s_shape_n; i++) {
        if (s_shape_cnt[i] > s_shape_cnt[best]) {
            best = i;
        }
    }
    uint32_t total = 0;
    for (int i = 0; i < s_shape_n; i++) {
        total += s_shape_cnt[i];
    }

    s_locked_len  = s_shape_len[best];
    s_locked_fwi  = fwi;
    s_locked_nsub = csi_nsub(s_locked_len, fwi);
    s_locked      = true;

    float share = total ? (100.0f * s_shape_cnt[best] / total) : 0.0f;

    ESP_LOGW(TAG, "=============================================");
    ESP_LOGW(TAG, " CSI SHAPE LOCKED");
    ESP_LOGW(TAG, "   length      : %u bytes", s_locked_len);
    ESP_LOGW(TAG, "   subcarriers : %d", s_locked_nsub);
    ESP_LOGW(TAG, "   first_word_invalid: %u (skipping %d bytes)",
             s_locked_fwi, s_locked_fwi ? 4 : 0);
    ESP_LOGW(TAG, "   dominance   : %.1f%% of %" PRIu32 " packets", share, total);
    if (share < 90.0f) {
        ESP_LOGE(TAG, "   *** Only %.1f%% dominant - your traffic is mixed.", share);
        ESP_LOGE(TAG, "   *** Turn ON 'Capture L-LTF only' in menuconfig and reflash.");
    }
    ESP_LOGW(TAG, "=============================================");
}

/* ------------------------------------------------------------------ */
/*  Worker task: amplitude -> normalise -> CSV                          */
/* ------------------------------------------------------------------ */
static float s_amp[MAX_SUBC];
static float s_scratch[MAX_SUBC];
static char  s_line[MAX_SUBC * 5 + 96];

static void amplitude_task(void *arg)
{
    csi_raw_t r;
    uint32_t  seq = 0, accepted = 0, rejected = 0, decim = 0;

    /* per-second scaling diagnostic */
    float   med_min = 1e9f, med_max = 0.0f, med_sum = 0.0f;
    uint32_t med_n = 0;
    int64_t next_report = esp_timer_get_time() + 1000000;

    while (1) {
        if (xQueueReceive(s_raw_q, &r, pdMS_TO_TICKS(200)) != pdTRUE) {
            goto report;
        }
        seq++;

        /* ---- phase 1: learn the dominant shape ---- */
        if (!s_locked) {
            shape_observe(&r);
            if (++s_observed >= (uint32_t)CONFIG_CSI_LOCK_PACKETS) {
                shape_lock(r.fwi);
            }
            goto report;
        }

        /* ---- phase 2: accept only the locked shape ---- */
        if (r.len != s_locked_len) {
            rejected++;
            goto report;
        }
        accepted++;

        /* ---- amplitude per subcarrier ----
         * int8 [imag, real] pairs, or 12-bit words in L-LTF-only mode -
         * see csi_amp(). Skip the first 4 bytes when first_word_invalid
         * is set.                                                      */
        int off  = r.fwi ? 4 : 0;
        int nsub = csi_nsub(r.len, r.fwi);
        if (nsub > MAX_SUBC) {
            nsub = MAX_SUBC;
        }

        for (int k = 0; k < nsub; k++) {
            s_amp[k] = csi_amp(r.buf, off, k);
        }

        /* ---- normalise away the AGC ---- */
        float med = median_of(s_amp, nsub, s_scratch);
        if (med < 1.0f) {
            med = 1.0f;      /* guard: an all-zero packet */
        }

        if (med < med_min) med_min = med;
        if (med > med_max) med_max = med;
        med_sum += med;
        med_n++;

        /* ---- emit CSV, decimated ---- */
        if (++decim >= (uint32_t)CONFIG_CSI_CSV_DECIMATE) {
            decim = 0;
            int p = snprintf(s_line, sizeof(s_line),
                             "CSI,%" PRIu32 ",%" PRIu32 ",%d,%d,%d",
                             seq, r.t_ms, r.rssi, nsub, (int)(med * 100));
            for (int k = 0; k < nsub && p < (int)sizeof(s_line) - 8; k++) {
                int norm = (int)(100.0f * s_amp[k] / med);
                p += snprintf(s_line + p, sizeof(s_line) - p, ",%d", norm);
            }
            puts(s_line);
        }

report:
        if (esp_timer_get_time() >= next_report) {
            next_report = esp_timer_get_time() + 1000000;
            if (!s_locked) {
                ESP_LOGI(TAG, "learning shape... %" PRIu32 "/%d packets, %d distinct lengths",
                         s_observed, CONFIG_CSI_LOCK_PACKETS, s_shape_n);
            } else {
                float med_avg = med_n ? med_sum / med_n : 0.0f;
                float spread  = (med_min > 0 && med_n) ? med_max / med_min : 0.0f;
                ESP_LOGI(TAG,
                         "acc %" PRIu32 " rej %" PRIu32 " drop %" PRIu32
                         " | nsub %d | median amp avg %.1f (min %.1f max %.1f, spread %.2fx)",
                         accepted, rejected, s_dropped, s_locked_nsub,
                         med_avg, med_n ? med_min : 0.0f, med_max, spread);
                if (med_n && spread > 3.0f) {
                    ESP_LOGW(TAG, "  ^ large scale spread: the AGC is moving a lot.");
                    ESP_LOGW(TAG, "    Normalisation is absorbing it - this is expected");
                    ESP_LOGW(TAG, "    if the link is weak. Move closer to the router if");
                    ESP_LOGW(TAG, "    detection later turns out noisy.");
                }
                med_min = 1e9f; med_max = 0.0f; med_sum = 0.0f; med_n = 0;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
static void csi_start(void)
{
#if CONFIG_CSI_LLTF_ONLY
    /* One shape for every packet: only the legacy long training field,
     * which every 802.11 preamble carries regardless of PHY. ACKs are
     * L-LTF too, so they add frames without adding a second shape.
     * On the C5 this arrives 12-bit packed - csi_amp() decodes it.    */
    wifi_csi_config_t csi_cfg = {
        .enable                   = 1,
        .acquire_csi_legacy       = 1,
        .acquire_csi_force_lltf   = 1,   /* force L-LTF even on HT/HE frames */
        .acquire_csi_ht20         = 0,
        .acquire_csi_ht40         = 0,
        .acquire_csi_vht          = 0,
        .acquire_csi_su           = 0,
        .acquire_csi_mu           = 0,
        .acquire_csi_dcm          = 0,
        .acquire_csi_beamformed   = 0,
        .acquire_csi_he_stbc_mode = 0,
        .val_scale_cfg            = 0,
        .dump_ack_en              = 1,
        .reserved                 = 0,
    };
    ESP_LOGW(TAG, "CSI mode: L-LTF ONLY (fixed shape, 12-bit samples)");
#else
    /* HT-LTF / HE-LTF only, both plain int8. Legacy (11g) frames and ACKs
     * are left out because on the C5 their L-LTF is 12-bit packed: mixing
     * them in gave a ~50/50 two-shape stream in which the lock often
     * picked the 106-byte ACK shape and then decoded it wrongly.
     * Trade-off: a router that answers pings with 11g frames now yields
     * no CSI at all - Step 03's rate shows that at once, and 'Capture
     * L-LTF only' is the fallback.                                     */
    wifi_csi_config_t csi_cfg = {
        .enable                   = 1,
        .acquire_csi_legacy       = 0,
        .acquire_csi_force_lltf   = 0,
        .acquire_csi_ht20         = 1,
        .acquire_csi_ht40         = 0,
        .acquire_csi_vht          = 0,
        .acquire_csi_su           = 1,
        .acquire_csi_mu           = 1,
        .acquire_csi_dcm          = 0,
        .acquire_csi_beamformed   = 0,
        .acquire_csi_he_stbc_mode = 2,
        .val_scale_cfg            = 0,
        .dump_ack_en              = 0,
        .reserved                 = 0,
    };
    ESP_LOGW(TAG, "CSI mode: HT/HE-LTF, auto-locking to the dominant shape");
#endif

    ESP_ERROR_CHECK(esp_wifi_set_csi_config(&csi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(&csi_rx_callback, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_csi(true));
}

/* ------------------------------------------------------------------ */
/*  ---- unchanged from Step 04: ping source + WiFi bring-up ----      */
/* ------------------------------------------------------------------ */
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
    cfg.timeout_ms  = CONFIG_CSI_PING_INTERVAL_MS * 2;  /* never the 1000 ms default */
    cfg.data_size   = 32;
    cfg.task_prio   = 2;

    esp_ping_callbacks_t cbs = {
        .on_ping_success = ping_noop,
        .on_ping_timeout = ping_noop,
        .on_ping_end     = ping_noop,
        .cb_args         = NULL,
    };

    esp_err_t err = esp_ping_new_session(&cfg, &cbs, &s_ping);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ping session failed: %s", esp_err_to_name(err));
        s_ping = NULL;
        return;
    }
    s_ping_target = target;
    esp_ping_start(s_ping);
    ESP_LOGI(TAG, "ping -> " IPSTR " every %d ms",
             IP2STR(&target), CONFIG_CSI_PING_INTERVAL_MS);
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

static void event_handler(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
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
    strncpy((char *)wc.sta.password, CONFIG_CSI_WIFI_PASSWORD,
            sizeof(wc.sta.password) - 1);

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

    ESP_LOGI(TAG, "=== Step 05: amplitude + normalisation ===");

    s_raw_q = xQueueCreate(CSI_QUEUE_LEN, sizeof(csi_raw_t));
    if (!s_raw_q) {
        ESP_LOGE(TAG, "queue alloc failed (need ~%d bytes)",
                 CSI_QUEUE_LEN * (int)sizeof(csi_raw_t));
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

    /* Big stack: the worker does floating point and snprintf. */
    xTaskCreate(amplitude_task, "amp", 8192, NULL, 4, NULL);
}
