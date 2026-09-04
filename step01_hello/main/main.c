/*
 * STEP 01 - Hello / Board sanity check  (ESP32-C5)
 * ------------------------------------------------
 * Goal of this step: prove that your toolchain, your USB cable and your
 * board all work TOGETHER. No Wi-Fi, no CSI yet. If this step does not
 * print, nothing later in this project can possibly work.
 *
 * What you should see: a banner, then a heartbeat line every second.
 */

#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_idf_version.h"
#include "esp_mac.h"
#include "esp_log.h"

static const char *TAG = "step01";

void app_main(void)
{
    /* ---- 1. Who am I? ---------------------------------------------- */
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    ESP_LOGI(TAG, "=============================================");
    ESP_LOGI(TAG, " ESP32-C5 CSI project - Step 01 (sanity)");
    ESP_LOGI(TAG, "=============================================");
    ESP_LOGI(TAG, "IDF version   : %s", esp_get_idf_version());
    ESP_LOGI(TAG, "CPU cores     : %d", chip.cores);
    ESP_LOGI(TAG, "Silicon rev   : v%d.%d", chip.revision / 100, chip.revision % 100);
    ESP_LOGI(TAG, "Flash size    : %" PRIu32 " MB", flash_size / (1024 * 1024));
    ESP_LOGI(TAG, "Free heap     : %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "STA MAC       : %02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    /* These two tell you the radio really is a dual-band Wi-Fi 6 part. */
    ESP_LOGI(TAG, "Has 2.4G WiFi : %s", (chip.features & CHIP_FEATURE_WIFI_BGN) ? "yes" : "no");
    ESP_LOGI(TAG, "Has BLE       : %s", (chip.features & CHIP_FEATURE_BLE) ? "yes" : "no");

    /* ---- 2. Heartbeat ---------------------------------------------- */
    ESP_LOGI(TAG, "Step 01 OK. Heartbeat starting - press Ctrl+] to quit monitor.");

    uint32_t tick = 0;
    while (1) {
        ESP_LOGI(TAG, "heartbeat %" PRIu32 "  (free heap %" PRIu32 ")",
                 tick++, esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
