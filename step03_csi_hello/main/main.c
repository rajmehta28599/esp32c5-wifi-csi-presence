/*
 * STEP 03 - First CSI capture  (ESP32-C5)
 * ----------------------------------------
 * Goal of this step: prove that CSI callbacks actually fire on your board,
 * and that they fire at a usable RATE. Nothing is interpreted yet - we only
 * count. If the counter stays at zero, stop here and fix it; every later
 * step depends on this working.
 *
 * TWO THINGS PEOPLE ALWAYS GET WRONG, both handled here:
 *
 *   1. CONFIG_ESP_WIFI_CSI_ENABLED defaults to 'n' in ESP-IDF.
 *      It is set in this project's sdkconfig.defaults. Without it,
 *      esp_wifi_set_csi() fails at runtime even though the code compiles.
 *
 *   2. CSI only exists for packets the chip RECEIVES. An idle station
 *      receives only beacons (~10/s) - far too slow. So we ping the
 *      gateway continuously; every reply is a reception.
 *
 *      ACK and legacy (11g) frames are deliberately NOT captured. On the
 *      C5 their L-LTF CSI arrives as 106 bytes of 12-bit samples packed in
 *      16-bit words, while HT/HE-LTF CSI is plain int8 - two incompatible
 *      layouts in one stream. The rate printed here is therefore the rate
 *      of HT/HE ping replies only, the same stream Steps 05-08 analyse.
 *      If it reads 0 while you ARE associated, your router answers pings
 *      with 11g frames: use 'Capture L-LTF only' from Step 05 on.
 *
 * NOTE ON THE CSI CONFIG STRUCT - this is the C5-specific part:
 *      On ESP32-C5 (a Wi-Fi 6 / MAC v3 part) wifi_csi_config_t is an alias
 *      for wifi_csi_acquire_config_t, a BITFIELD struct. The classic fields
 *      you see in every ESP32 tutorial - lltf_en, htltf_en, stbc_htltf2_en,
 *      ltf_merge_en, channel_filter_en, manu_scale, shift - DO NOT EXIST
 *      here and will not compile. Use the fields below.
 */

#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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

static const char *TAG = "step03";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_events;
static int                s_retry_count = 0;
static esp_ip4_addr_t     s_gateway;
static esp_timer_handle_t s_reconnect_timer;
static bool               s_connected_once = false;
static esp_ping_handle_t  s_ping = NULL;
static esp_ip4_addr_t     s_ping_target;

/* Counters touched from the WiFi task - keep them simple and volatile. */
static volatile uint32_t s_csi_count = 0;   /* total CSI callbacks      */
static volatile uint16_t s_last_len  = 0;   /* bytes in last CSI buffer */
static volatile int8_t   s_last_rssi = 0;

/* =================================================================== */
/*  CSI callback - runs in the WiFi driver task.                       */
/*  RULE: do the absolute minimum here. No printf, no floating point,  */
/*  no blocking. Real processing happens in later steps via a queue.   */
/* =================================================================== */
static void csi_rx_callback(void *ctx, wifi_csi_info_t *info)
{
    if (!info || !info->buf) {
        return;
    }
    s_csi_count++;
    s_last_len  = info->len;
    s_last_rssi = info->rx_ctrl.rssi;
}

/* =================================================================== */
/*  Enable CSI on the C5                                               */
/* =================================================================== */
static void csi_start(void)
{
    /* On C5 this type resolves to wifi_csi_acquire_config_t (bitfields). */
    wifi_csi_config_t csi_cfg = {
        .enable                   = 1,  /* master enable                    */
        .acquire_csi_legacy       = 0,  /* no 11g L-LTF: 12-bit packed on C5 */
        .acquire_csi_force_lltf   = 0,
        .acquire_csi_ht20         = 1,  /* HT-LTF from 802.11n HT20 frames  */
        .acquire_csi_ht40         = 0,  /* we lock to HT20, so not needed   */
        .acquire_csi_vht          = 0,
        .acquire_csi_su           = 1,  /* HE-LTF from Wi-Fi 6 SU frames    */
        .acquire_csi_mu           = 1,  /* HE-LTF from Wi-Fi 6 MU frames    */
        .acquire_csi_dcm          = 0,
        .acquire_csi_beamformed   = 0,
        .acquire_csi_he_stbc_mode = 2,  /* sample evenly across HE-LTF1/2   */
        .val_scale_cfg            = 0,  /* fixed scaling (see Step 04)      */
        .dump_ack_en              = 0,  /* ACKs are L-LTF too - see header  */
        .reserved                 = 0,
    };

    ESP_ERROR_CHECK(esp_wifi_set_csi_config(&csi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(&csi_rx_callback, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_csi(true));

    ESP_LOGI(TAG, "CSI enabled");
}

/* =================================================================== */
/*  Continuous ping = our CSI traffic source                            */
/* =================================================================== */
static void ping_success_cb(esp_ping_handle_t hdl, void *args) { /* quiet */ }
static void ping_timeout_cb(esp_ping_handle_t hdl, void *args) { /* quiet */ }
static void ping_end_cb(esp_ping_handle_t hdl, void *args)     { /* quiet */ }

/* Called on every GOT_IP, so it must cope with a session that already
 * exists: same gateway -> just resume it, new gateway -> replace it. */
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
    cfg.count       = ESP_PING_COUNT_INFINITE;      /* never stop        */
    cfg.interval_ms = CONFIG_CSI_PING_INTERVAL_MS;  /* 20 ms -> ~50 Hz   */
    /* NOT the 1000 ms default! The ping loop blocks in recvfrom() for the
     * whole timeout when a reply is lost, so a 1000 ms timeout turns one
     * dropped packet into a full second with zero CSI. Keep it tight.   */
    cfg.timeout_ms  = CONFIG_CSI_PING_INTERVAL_MS * 2;
    cfg.data_size   = 32;
    cfg.task_prio   = 2;

    esp_ping_callbacks_t cbs = {
        .on_ping_success = ping_success_cb,
        .on_ping_timeout = ping_timeout_cb,
        .on_ping_end     = ping_end_cb,
        .cb_args         = NULL,
    };

    /* Not ESP_ERROR_CHECK: this now runs from the event handler on every
     * reconnect, and aborting there on a transient failure would reboot. */
    esp_err_t err = esp_ping_new_session(&cfg, &cbs, &s_ping);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ping session failed: %s", esp_err_to_name(err));
        s_ping = NULL;
        return;
    }
    s_ping_target = target;
    esp_ping_start(s_ping);

    ESP_LOGI(TAG, "pinging " IPSTR " every %d ms to generate CSI traffic",
             IP2STR(&target), CONFIG_CSI_PING_INTERVAL_MS);
}

/* =================================================================== */
/*  WiFi bring-up (same as Step 02)                                     */
/*                                                                     */
/*  First join: CONFIG_CSI_WIFI_MAX_RETRY attempts, then WIFI_FAIL_BIT */
/*  so app_main can report bad credentials and reboot.                 */
/*  After the first join: never give up. A router reboot is off the    */
/*  air for 1-3 minutes, and a board that stops retrying stays offline */
/*  until someone power-cycles it. Retries back off 1, 2, 4 ... 30 s   */
/*  and run from a timer, because a vTaskDelay inside this handler     */
/*  stalls every other event on the default event loop. The ping is    */
/*  paused while the link is down so it does not flood the console     */
/*  with send errors, and resumed (or re-aimed) on the next GOT_IP.    */
/* =================================================================== */
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

    /* C5 radio lock - after start(), see Step 02 for the reasoning. */
    esp_wifi_set_band_mode(WIFI_BAND_MODE_2G_ONLY);
    esp_wifi_set_bandwidth(WIFI_IF_STA, CSI_WIFI_BW20);
    esp_wifi_set_ps(WIFI_PS_NONE);
}

/* =================================================================== */
void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "=== Step 03: first CSI capture ===");
    wifi_init_sta();

    EventBits_t bits = xEventGroupWaitBits(s_wifi_events,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, portMAX_DELAY);
    if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "WiFi failed - rerun Step 02 and fix credentials.");
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }

    csi_start();
    /* The ping was already started by the GOT_IP handler. */

    /* ---- Report the CSI rate once per second ---- */
    uint32_t prev    = 0;
    int64_t  prev_us = esp_timer_get_time();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        uint32_t now     = s_csi_count;
        int64_t  now_us  = esp_timer_get_time();
        uint32_t delta   = now - prev;
        float    seconds = (now_us - prev_us) / 1e6f;
        prev    = now;
        prev_us = now_us;

        wifi_ap_record_t ap;
        int ch = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) ? ap.primary : -1;

        /* Free heap is printed alongside the rate on purpose: it is the
         * number you spend when raising CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM
         * / _DYNAMIC_RX_BUFFER_NUM. Read it under load before raising them -
         * see the note at the end of sdkconfig.defaults. */
        ESP_LOGI(TAG,
                 "CSI packets: %6" PRIu32 " total | %5.1f /s | last len %u B | rssi %d dBm | ch %d | heap %" PRIu32 " B",
                 now, delta / seconds, s_last_len, s_last_rssi, ch,
                 esp_get_free_heap_size());

        if (now == 0) {
            ESP_LOGE(TAG, "NO CSI YET. Check: (a) CONFIG_ESP_WIFI_CSI_ENABLED=y,");
            ESP_LOGE(TAG, "(b) you are associated, (c) the ping target is reachable.");
        }
    }
}
