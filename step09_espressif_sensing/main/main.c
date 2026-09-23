/* =====================================================================
 *  Step 09 - Espressif's own sensing stack, as a BENCHMARK
 * =====================================================================
 *
 *  This step deliberately contains none of this project's own detection
 *  code. It runs Espressif's `esp_wifi_sensing` FSM (which sits on top of
 *  `esp-radar`) against the same board, the same router and the same room,
 *  so that Step 08's numbers have something honest to be compared with.
 *
 *  Why it is a separate step and not folded into Step 08:
 *  `esp_wifi_set_csi_rx_cb()` has ONE callback slot, last writer wins.
 *  `esp_wifi_sensing` registers its own. Linking both into one firmware
 *  would silently disable whichever registered first - a bug that looks
 *  like "the detector stopped working" and has no visible cause.
 *
 *  What to compare, once both are flashed:
 *    - Step 08's motion score      vs  `jitter_value` here
 *    - Step 08's presence branch   vs  `wander_value` / presence_someone
 *    - Step 08's single threshold  vs  enter_level / exit_level (two of them)
 *    - Step 06's 45 s calibration  vs  init_stage reaching its final value
 *
 *  See docs/REFERENCES.md section 2.1 for the component's documented
 *  behaviour, and section 6 for why each of those comparisons matters.
 * ===================================================================== */

#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"        /* MACSTR / MAC2STR */
#include "esp_idf_version.h"
#include "nvs_flash.h"
#include "esp_wifi_sensing.h"

/* v6.0 renamed the bandwidth enum; keep the same shim the other steps use. */
#if ESP_IDF_VERSION_MAJOR >= 6
#define CSI_WIFI_BW20 WIFI_BW20
#else
#define CSI_WIFI_BW20 WIFI_BW_HT20
#endif

static const char *TAG = "step09";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_events;
static int                s_retry_count;

/* =================================================================== */
/*  WiFi bring-up - identical to Steps 02-08 on purpose                */
/* =================================================================== */
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

    /* Same radio lock as the rest of the project - 2.4 GHz, HT20, no power
     * save. The 5 GHz CSI hazard (docs/REFERENCES.md 1.3) applies here too. */
    esp_wifi_set_band_mode(WIFI_BAND_MODE_2G_ONLY);
    esp_wifi_set_bandwidth(WIFI_IF_STA, CSI_WIFI_BW20);
    esp_wifi_set_ps(WIFI_PS_NONE);
}

/* =================================================================== */
/*  Sensing FSM callbacks                                              */
/* =================================================================== */
static void sensing_event_cb(esp_wifi_sensing_fsm_handle_t handle,
                             const uint8_t peer_mac[6],
                             esp_wifi_sensing_fsm_event_t event,
                             uint32_t data, void *user_data)
{
    ESP_LOGW(TAG, ">>> %s  peer " MACSTR "  (data %" PRIu32 ")",
             event == ESP_WIFI_SENSING_FSM_EVENT_ACTIVE ? "MOTION  ACTIVE"
                                                        : "quiet   INACTIVE",
             MAC2STR(peer_mac), data);
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

    ESP_LOGI(TAG, "=== Step 09: Espressif esp_wifi_sensing benchmark ===");
    wifi_init_sta();

    EventBits_t bits = xEventGroupWaitBits(s_wifi_events,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, portMAX_DELAY);
    if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "WiFi failed - rerun Step 02 and fix credentials.");
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }

    /* The sensing channel is identified by the peer's MAC. For the
     * router-ping topology that peer is the AP itself. */
    wifi_ap_record_t ap;
    ESP_ERROR_CHECK(esp_wifi_sta_get_ap_info(&ap));
    ESP_LOGI(TAG, "sensing peer = AP " MACSTR " on channel %d, rssi %d dBm",
             MAC2STR(ap.bssid), ap.primary, ap.rssi);

    esp_wifi_sensing_fsm_config_t cfg = DEFAULT_ESP_WIFI_SENSING_FSM_CONFIG();
    esp_wifi_sensing_fsm_handle_t fsm = NULL;
    ESP_ERROR_CHECK(esp_wifi_sensing_fsm_create(&cfg, &fsm));
    ESP_ERROR_CHECK(esp_wifi_sensing_fsm_add_channel(fsm, ap.bssid));

    ESP_ERROR_CHECK(esp_wifi_sensing_fsm_register_event_cb(
        fsm, ESP_WIFI_SENSING_FSM_EVENT_ACTIVE, sensing_event_cb, NULL));
    ESP_ERROR_CHECK(esp_wifi_sensing_fsm_register_event_cb(
        fsm, ESP_WIFI_SENSING_FSM_EVENT_INACTIVE, sensing_event_cb, NULL));

    /* This is the component's own equivalent of Step 02's ping task. */
    ESP_ERROR_CHECK(esp_wifi_sensing_fsm_ping_router_start(fsm));

    uint32_t ping_hz = 0;
    esp_wifi_sensing_fsm_get_ping_frequency_hz(fsm, &ping_hz);

    esp_wifi_sensing_fsm_channel_config_t ch = { 0 };
    esp_wifi_sensing_fsm_get_channel_config(fsm, ap.bssid, &ch);
    ESP_LOGI(TAG, "defaults: ping %" PRIu32 " Hz | sensitivity %.3f | "
                  "presence_sensitivity %.3f | active_jitter_min %.3f | "
                  "active_filter_ms %" PRIu32,
             ping_hz, ch.sensitivity, ch.presence_sensitivity,
             ch.active_jitter_min, ch.active_filter_ms);
    ESP_LOGI(TAG, "leave the room - the FSM learns its own baseline first.");

    /* ---- One diagnostic line per second, same cadence as Step 08 ---- */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        esp_wifi_sensing_fsm_state_t state;
        esp_wifi_sensing_fsm_channel_diag_t d;

        if (esp_wifi_sensing_fsm_get_state(fsm, ap.bssid, &state) != ESP_OK ||
            esp_wifi_sensing_fsm_get_channel_diag(fsm, ap.bssid, &d) != ESP_OK) {
            ESP_LOGE(TAG, "channel diag unavailable - is the peer still associated?");
            continue;
        }

        ESP_LOGI(TAG,
                 "%-8s | jitter %8.3f | wander %8.3f | smooth %6" PRIu32
                 " | enter %6" PRIu32 " | exit %6" PRIu32
                 " | init_stage %d | someone %d | heap %" PRIu32 " B",
                 state == ESP_WIFI_SENSING_FSM_STATE_ACTIVE   ? "ACTIVE"
                 : state == ESP_WIFI_SENSING_FSM_STATE_DEBOUNCE ? "DEBOUNCE"
                                                                : "inactive",
                 d.jitter_value, d.wander_value, d.smooth_scaled,
                 d.enter_level_scaled, d.exit_level_scaled,
                 (int)d.init_stage, (int)d.presence_someone_status,
                 esp_get_free_heap_size());
    }
}
