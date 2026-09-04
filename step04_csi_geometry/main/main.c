/*
 * STEP 04 - CSI geometry probe  (ESP32-C5)
 * -----------------------------------------
 * Goal of this step: find out what your CSI data ACTUALLY looks like on
 * YOUR board, on YOUR channel, with YOUR router. Do not skip this step and
 * do not trust numbers from a blog post.
 *
 * WHY THIS STEP EXISTS
 *   Every ESP32 CSI tutorial says "64 subcarriers, 128 bytes". That number
 *   comes from the ESP32-classic L-LTF table in the ESP-IDF docs. The C5 is
 *   a Wi-Fi 6 part: depending on whether the router sends you 802.11g,
 *   802.11n (HT20) or 802.11ax (HE20) frames, you get a DIFFERENT number of
 *   subcarriers, and the buffer holds the enabled LTFs concatenated.
 *   Hard-coding the wrong length silently produces a detector that "runs"
 *   but detects nothing.
 *
 *   Note also that the C5's rx_ctrl is esp_wifi_rxctrl_t, NOT the classic
 *   wifi_pkt_rx_ctrl_t. Fields the old tutorials read - sig_mode, mcs, cwb,
 *   ant, secondary_channel - DO NOT EXIST on this chip. The C5 equivalent
 *   of "what PHY was this" is rx_ctrl.cur_bb_format.
 *
 * WHAT YOU DO WITH THE OUTPUT
 *   Let it run ~30 s in an empty room. It prints a histogram of every
 *   distinct CSI shape it saw. Pick the shape with the highest count -
 *   that is your dominant PHY - and note its subcarrier count. Step 05
 *   locks onto exactly that shape and ignores everything else.
 */

#include <string.h>
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

static const char *TAG = "step04";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_events;
static int                s_retry_count = 0;
static esp_ip4_addr_t     s_gateway;

/* ------------------------------------------------------------------ */
/*  One compact record per CSI packet. Metadata only - we do NOT copy  */
/*  the CSI payload here, we only need its shape.                      */
/* ------------------------------------------------------------------ */
typedef struct {
    uint16_t len;          /* info->len : bytes of CSI in this packet    */
    uint16_t est_len;      /* rx_ctrl.rx_channel_estimate_len            */
    uint8_t  est_vld;      /* rx_ctrl.rx_channel_estimate_info_vld       */
    uint8_t  bb_format;    /* rx_ctrl.cur_bb_format - which PHY          */
    uint8_t  fwi;          /* info->first_word_invalid                   */
    uint8_t  channel;      /* rx_ctrl.channel                            */
    int8_t   rssi;
    int8_t   noise_floor;
    int8_t   first8[8];    /* first 8 raw bytes, to see the I/Q layout   */
} csi_meta_t;

static QueueHandle_t s_meta_q;
static volatile uint32_t s_dropped = 0;

/* ------------------------------------------------------------------ */
/*  Histogram of distinct CSI shapes                                   */
/* ------------------------------------------------------------------ */
#define MAX_BINS 16
typedef struct {
    uint16_t len;
    uint8_t  bb_format;
    uint8_t  fwi;
    uint32_t count;
    int8_t   rssi_min, rssi_max;
} geom_bin_t;

static geom_bin_t s_bins[MAX_BINS];
static int        s_bin_count = 0;

static const char *bb_format_name(uint8_t f)
{
    switch (f) {
        case 0:  return "11b";
        case 1:  return "11g/11a";
        case 2:  return "HT (11n)";
        case 3:  return "VHT (11ac)";
        case 4:  return "HE SU (11ax)";
        case 5:  return "HE MU (11ax)";
        case 6:  return "HE ER-SU";
        case 7:  return "HE TB";
        case 11: return "VHT MU";
        default: return "unknown";
    }
}

static void hist_add(const csi_meta_t *m)
{
    for (int i = 0; i < s_bin_count; i++) {
        if (s_bins[i].len == m->len &&
            s_bins[i].bb_format == m->bb_format &&
            s_bins[i].fwi == m->fwi) {
            s_bins[i].count++;
            if (m->rssi < s_bins[i].rssi_min) s_bins[i].rssi_min = m->rssi;
            if (m->rssi > s_bins[i].rssi_max) s_bins[i].rssi_max = m->rssi;
            return;
        }
    }
    if (s_bin_count < MAX_BINS) {
        s_bins[s_bin_count].len       = m->len;
        s_bins[s_bin_count].bb_format = m->bb_format;
        s_bins[s_bin_count].fwi       = m->fwi;
        s_bins[s_bin_count].count     = 1;
        s_bins[s_bin_count].rssi_min  = m->rssi;
        s_bins[s_bin_count].rssi_max  = m->rssi;
        s_bin_count++;
    }
}

/* ------------------------------------------------------------------ */
/*  CSI callback - still minimal. Copy metadata, post, return.         */
/* ------------------------------------------------------------------ */
static void csi_rx_callback(void *ctx, wifi_csi_info_t *info)
{
    if (!info || !info->buf || info->len == 0) {
        return;
    }

    csi_meta_t m = { 0 };
    m.len         = info->len;
    m.est_len     = info->rx_ctrl.rx_channel_estimate_len;
    m.est_vld     = info->rx_ctrl.rx_channel_estimate_info_vld;
    m.bb_format   = info->rx_ctrl.cur_bb_format;
    m.fwi         = info->first_word_invalid ? 1 : 0;
    m.channel     = info->rx_ctrl.channel;
    m.rssi        = info->rx_ctrl.rssi;
    m.noise_floor = info->rx_ctrl.noise_floor;

    int n = info->len < 8 ? info->len : 8;
    memcpy(m.first8, info->buf, n);

    /* Never block the WiFi task. If the consumer is behind, drop. */
    if (xQueueSend(s_meta_q, &m, 0) != pdTRUE) {
        s_dropped++;
    }
}

/* ------------------------------------------------------------------ */
static void csi_start(void)
{
    wifi_csi_config_t csi_cfg = {
        .enable                   = 1,
        .acquire_csi_legacy       = 1,
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
        .dump_ack_en              = 1,
        .reserved                 = 0,
    };

    ESP_ERROR_CHECK(esp_wifi_set_csi_config(&csi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(&csi_rx_callback, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_csi(true));
    ESP_LOGI(TAG, "CSI enabled (all LTF types, so we can see every shape)");
}

/* ------------------------------------------------------------------ */
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
    /* NOT the 1000 ms default! The ping loop blocks in recvfrom() for the
     * whole timeout when a reply is lost, so a 1000 ms timeout turns one
     * dropped packet into a full second with zero CSI. Keep it tight.   */
    cfg.timeout_ms  = CONFIG_CSI_PING_INTERVAL_MS * 2;
    cfg.data_size   = 32;
    cfg.task_prio   = 2;

    esp_ping_callbacks_t cbs = {
        .on_ping_success = ping_noop,
        .on_ping_timeout = ping_noop,
        .on_ping_end     = ping_noop,
        .cb_args         = NULL,
    };

    esp_ping_handle_t ping;
    ESP_ERROR_CHECK(esp_ping_new_session(&cfg, &cbs, &ping));
    ESP_ERROR_CHECK(esp_ping_start(ping));
    ESP_LOGI(TAG, "ping -> " IPSTR " every %d ms",
             IP2STR(&target), CONFIG_CSI_PING_INTERVAL_MS);
}

/* ------------------------------------------------------------------ */
/*  Consumer task: build the histogram, print a report periodically.   */
/* ------------------------------------------------------------------ */
static void geometry_task(void *arg)
{
    csi_meta_t m;
    uint32_t   total = 0;
    bool       printed_sample = false;
    int64_t    next_report_us = esp_timer_get_time() + 5000000;

    while (1) {
        if (xQueueReceive(s_meta_q, &m, pdMS_TO_TICKS(200)) == pdTRUE) {
            total++;
            hist_add(&m);

            /* Print one fully-decoded packet so you can see the layout. */
            if (!printed_sample) {
                printed_sample = true;
                ESP_LOGI(TAG, "--------- first CSI packet, decoded ---------");
                ESP_LOGI(TAG, "  info->len                    = %u bytes", m.len);
                ESP_LOGI(TAG, "  rx_channel_estimate_len      = %u", m.est_len);
                ESP_LOGI(TAG, "  rx_channel_estimate_info_vld = %u", m.est_vld);
                ESP_LOGI(TAG, "  cur_bb_format                = %u (%s)",
                         m.bb_format, bb_format_name(m.bb_format));
                ESP_LOGI(TAG, "  first_word_invalid           = %u", m.fwi);
                ESP_LOGI(TAG, "  channel / rssi / noise       = %u / %d / %d",
                         m.channel, m.rssi, m.noise_floor);
                ESP_LOGI(TAG, "  raw bytes [0..7]             = %d %d %d %d %d %d %d %d",
                         m.first8[0], m.first8[1], m.first8[2], m.first8[3],
                         m.first8[4], m.first8[5], m.first8[6], m.first8[7]);
                ESP_LOGI(TAG, "  layout: pairs of (imag, real), int8 each");
                ESP_LOGI(TAG, "  -> subcarrier 0 = imag %d, real %d",
                         m.first8[0], m.first8[1]);
                ESP_LOGI(TAG, "--------------------------------------------");
            }
        }

        if (esp_timer_get_time() >= next_report_us) {
            next_report_us = esp_timer_get_time() + 5000000;

            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "===== CSI GEOMETRY HISTOGRAM (%" PRIu32 " packets, %" PRIu32 " dropped) =====",
                     total, s_dropped);
            ESP_LOGI(TAG, "  bytes  subcarriers  fwi  PHY format      count   share  rssi range");

            for (int i = 0; i < s_bin_count; i++) {
                /* Each subcarrier = 2 bytes (imag, real).
                 * If first_word_invalid, the first 4 bytes are garbage and
                 * must be skipped -> 2 fewer usable subcarriers.          */
                int usable_bytes = s_bins[i].len - (s_bins[i].fwi ? 4 : 0);
                int subcarriers  = usable_bytes / 2;
                float share = total ? (100.0f * s_bins[i].count / total) : 0.0f;

                ESP_LOGI(TAG, "  %5u  %11d  %3u  %-14s %6" PRIu32 "  %5.1f%%  %d..%d dBm",
                         s_bins[i].len, subcarriers, s_bins[i].fwi,
                         bb_format_name(s_bins[i].bb_format),
                         s_bins[i].count, share,
                         s_bins[i].rssi_min, s_bins[i].rssi_max);
            }

            /* Point at the winner so the user knows what to use next. */
            int best = -1;
            uint32_t best_count = 0;
            for (int i = 0; i < s_bin_count; i++) {
                if (s_bins[i].count > best_count) {
                    best_count = s_bins[i].count;
                    best = i;
                }
            }
            if (best >= 0) {
                int usable = s_bins[best].len - (s_bins[best].fwi ? 4 : 0);
                ESP_LOGW(TAG, ">>> DOMINANT SHAPE: %u bytes, %d subcarriers, %s",
                         s_bins[best].len, usable / 2,
                         bb_format_name(s_bins[best].bb_format));
                ESP_LOGW(TAG, ">>> Write these two numbers down. Step 05 uses them.");
            }
            ESP_LOGI(TAG, "");
        }
    }
}

/* ------------------------------------------------------------------ */
/*  WiFi bring-up (identical to Step 03)                               */
/* ------------------------------------------------------------------ */
static void event_handler(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
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

    ESP_LOGI(TAG, "=== Step 04: CSI geometry probe ===");
    ESP_LOGI(TAG, "Leave the room empty and let this run for ~30 seconds.");

    s_meta_q = xQueueCreate(64, sizeof(csi_meta_t));
    if (!s_meta_q) {
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

    xTaskCreate(geometry_task, "geometry", 5120, NULL, 4, NULL);
}
