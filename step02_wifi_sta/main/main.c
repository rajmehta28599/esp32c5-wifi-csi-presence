/*
 * STEP 02 - Join your 2.4 GHz WiFi as a station  (ESP32-C5)
 * ---------------------------------------------------------
 * Goal of this step: get an IP address, and LOCK the radio so that the
 * CSI we capture in later steps always has the same shape.
 *
 * Why the locking matters (this is the #1 thing people get wrong):
 *   The number of CSI subcarriers depends on the channel bandwidth and the
 *   PHY format. The C5 is a dual-band Wi-Fi 6 part - if you let it roam to
 *   5 GHz or negotiate HE40/HE80, the CSI array length changes underneath
 *   you and every variance-based detector you build will produce garbage.
 *   So: 2.4 GHz only, 20 MHz only, power-save off.
 *
 * Configure your WiFi first:
 *      idf.py menuconfig
 *      -> ESP32-C5 CSI Project Configuration -> WiFi SSID / WiFi Password
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
#include "nvs_flash.h"

static const char *TAG = "step02";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_events;
static int s_retry_count = 0;

/* ------------------------------------------------------------------ */
/*  WiFi / IP event handler                                            */
/* ------------------------------------------------------------------ */
static void event_handler(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "station started, connecting...");
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
            ESP_LOGE(TAG, "giving up after %d retries (last reason %d)",
                     s_retry_count, d->reason);
            xEventGroupSetBits(s_wifi_events, WIFI_FAIL_BIT);
        }

    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "got IP     : " IPSTR, IP2STR(&e->ip_info.ip));
        ESP_LOGI(TAG, "gateway    : " IPSTR, IP2STR(&e->ip_info.gw));
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

/* ------------------------------------------------------------------ */
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

    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid, CONFIG_CSI_WIFI_SSID,
            sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, CONFIG_CSI_WIFI_PASSWORD,
            sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* ---- C5-specific radio locking. MUST come after esp_wifi_start() ---- */

    /* 1. 2.4 GHz only. On the dual-band C5 this also stops the scanner from
     *    wandering onto 5 GHz channels where the CSI shape differs.        */
    esp_err_t err = esp_wifi_set_band_mode(WIFI_BAND_MODE_2G_ONLY);
    ESP_LOGI(TAG, "band mode 2.4GHz-only : %s", esp_err_to_name(err));

    /* 2. 20 MHz only. A Wi-Fi 6 interface only accepts HT20 here anyway,
     *    but setting it explicitly documents the intent and fails loudly
     *    if the band mode above did not take.                              */
    err = esp_wifi_set_bandwidth(WIFI_IF_STA, CSI_WIFI_BW20);
    ESP_LOGI(TAG, "bandwidth HT20        : %s", esp_err_to_name(err));

    /* 3. No power save. The radio must be awake to receive every frame.    */
    err = esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_LOGI(TAG, "power save OFF        : %s", esp_err_to_name(err));

    ESP_LOGI(TAG, "connecting to SSID \"%s\" ...", CONFIG_CSI_WIFI_SSID);
}

/* ------------------------------------------------------------------ */
void app_main(void)
{
    /* NVS is required by the WiFi driver to store calibration data. */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "=== Step 02: WiFi station ===");
    wifi_init_sta();

    EventBits_t bits = xEventGroupWaitBits(s_wifi_events,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, portMAX_DELAY);
    if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Could not connect. Check SSID/password in menuconfig.");
        ESP_LOGE(TAG, "Remember: the network MUST be 2.4 GHz for this project.");
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }

    /* ---- Report what we actually ended up on ---- */
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        ESP_LOGI(TAG, "---------------------------------------------");
        ESP_LOGI(TAG, "connected to  : %s", (char *)ap.ssid);
        ESP_LOGI(TAG, "BSSID         : %02x:%02x:%02x:%02x:%02x:%02x",
                 ap.bssid[0], ap.bssid[1], ap.bssid[2],
                 ap.bssid[3], ap.bssid[4], ap.bssid[5]);
        ESP_LOGI(TAG, "channel       : %d (secondary %d)", ap.primary, ap.second);
        ESP_LOGI(TAG, "RSSI          : %d dBm", ap.rssi);
        ESP_LOGI(TAG, "---------------------------------------------");
        if (ap.primary > 14) {
            ESP_LOGE(TAG, "You are on a 5 GHz channel! Force your router or the");
            ESP_LOGE(TAG, "board onto 2.4 GHz before continuing to Step 03.");
        }
    }

    /* Keep printing link quality so you can see the link is alive and
     * see how much RSSI already moves when someone walks past.          */
    while (1) {
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            ESP_LOGI(TAG, "link alive: ch=%d  rssi=%d dBm", ap.primary, ap.rssi);
        } else {
            ESP_LOGW(TAG, "link down");
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
