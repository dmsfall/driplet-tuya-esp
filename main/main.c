#include <inttypes.h>
#include "config.h"
#include "wifi.h"
#include "tuya.h"
#include "esp_chip_info.h"
#include "esp_log.h"

static const char *TAG = "app";

TaskHandle_t main_task_handle = NULL;

void pairing_start()
{
    ESP_LOGI(TAG, "pairing...");

    if (tuya_is_configured() == false)
    {
        ESP_LOGI(TAG, "tuya not configured, skipping");

        esp_restart();

        return;
    }

    xTaskCreate(tuya_pairing_task, "tuya_pairing_task", 8 * 1024, NULL, 3, NULL);
}

void main_task(void *pvParameters)
{
    ESP_LOGI(TAG, "main task active");

    for (;;)
    {
        // TODO: Print some diagnostic info here, check OTA or something like this.
        ESP_LOGI(TAG, "i'm fine!");
        ESP_LOGI(TAG, "free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "starting DripLet...");
    ESP_LOGI(TAG, "free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "IDF version: %s", esp_get_idf_version());

    app_cfg_init();

    // TODO: remove this (always pairing, factory settings)
    // app_cfg_erase();
    // app_cfg_write();

    app_cfg_print();

    uint8_t rc = wifi_init();
    if (rc == 0)
    {
        ESP_LOGE(TAG, "error while wifi init");
        return;
    }

    if (app_cfg_verify() || app_cfg.paired == 0)
    {
        if (app_cfg.paired == 0)
        {
            ESP_LOGI(TAG, "pairing mode");

            pairing_start();

            // Waiting for pair restart
            vTaskDelay(portMAX_DELAY);
        }

        ESP_LOGW(TAG, "no application config found");

        for (;;)
        {
            ESP_LOGW(TAG, "waiting for pairing button pressed");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }

    ESP_LOGI(TAG, "config valid");

    if (app_cfg.pairing_state != PAIRING_PAIRED)
    {
        ESP_LOGW(TAG, "tuya not paired");
    }

    esp_err_t err = wifi_connect(app_cfg.wifi_ssid, app_cfg.wifi_password);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "error while connecting to wifi");

        for (int i = 3; i >= 0; i--)
        {
            ESP_LOGI(TAG, "restaring in %ds", i);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }

        esp_restart();
    }

    tuya_init();

    // Waiting for Tuya startup
    // TODO: maybe wait for some event from Tuya?
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    xTaskCreate(main_task, "main_task_handle", 16 * 1024, NULL, 2, &main_task_handle);
}
