#include "tuya.h"
#include "config.h"
#include "wifi.h"
#include <string.h>
#include "esp_log.h"
#include "tuya_iot.h"
#include "tuya_wifi_provisioning.h"
#include "qrcode.h"

static const char *TAG = "tuya";

TaskHandle_t tuya_main_task_handle = NULL;
TaskHandle_t tuya_ble_pairing_task_handle = NULL;

static tuya_iot_client_t client = {0};
static tuya_event_id_t last_event = TUYA_EVENT_RESET;
static uint8_t new_event = 0;

static void tuya_link_app_task(void *pvParameters);
static void tuya_user_event_handler_on(tuya_iot_client_t *client, tuya_event_msg_t *event);
static void tuya_qrcode_print(const char *productkey, const char *uuid);

void tuya_dp_download(tuya_iot_client_t *client, const char *json_dps);
uint8_t tuya_wait_event(tuya_event_id_t event, uint32_t timeout);
void tuya_wifi_info_cb(wifi_info_t wifi_info);
void hardware_switch_set(bool value);

void tuya_init()
{
    ESP_LOGI(TAG, "initialization Tuya...");

    if (tuya_main_task_handle != NULL)
    {
        ESP_LOGI(TAG, "already init");
        tuya_iot_reset(&client);
        tuya_iot_start(&client);
        vTaskResume(tuya_main_task_handle);
    }

    xTaskCreate(tuya_link_app_task, "tuya_main", 6 * 1024, NULL, 3, &tuya_main_task_handle);

    ESP_LOGI(TAG, "initialized");
}

void tuya_deinit()
{
    if (tuya_main_task_handle != NULL)
    {
        ESP_LOGI(TAG, "deinitializing...");
        tuya_iot_stop(&client);
        tuya_iot_reset(&client);
        vTaskDelete(tuya_main_task_handle);

        tuya_main_task_handle = NULL;

        memset(&client, 0, sizeof(tuya_iot_client_t));
        ESP_LOGI(TAG, "deinitialized");
    }
}

void tuya_deactivate()
{
    ESP_LOGI(TAG, "deactivating...");
    tuya_iot_activated_data_remove(&client);
}

uint8_t tuya_reconnect()
{
    ESP_LOGI(TAG, "reconnecting...");

    return tuya_iot_reconnect(&client);
}

uint8_t tuya_stop()
{
    ESP_LOGI(TAG, "stopping...");

    return tuya_iot_stop(&client);
}

uint8_t tuya_is_configured()
{
    return !(strnlen(app_cfg.tuya.uuid, sizeof(app_cfg.tuya.uuid)) == 0 || strnlen(app_cfg.tuya.auth_key, sizeof(app_cfg.tuya.auth_key)) == 0);
}

void tuya_pairing_task(void *pvParameters)
{
    app_cfg.pairing_state = PAIRING_BLE_PAIRING;

    tuya_init();

    while (app_cfg.pairing_state != PAIRING_WIFI_CONNECTING)
    {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    if (tuya_wait_event(TUYA_EVENT_MQTT_CONNECTED, 30000))
    {
        ESP_LOGI(TAG, "pairing failed: timeout, current state: %s", EVENT_ID2STR(last_event));
        tuya_stop();
        vTaskDelete(NULL);
    }

    app_cfg.pairing_state = PAIRING_PAIRED;
    app_cfg.paired = 1;

    app_cfg_write();
    ESP_LOGI(TAG, "pairing state: %d", app_cfg.pairing_state);
    esp_restart();
    vTaskDelete(NULL);
}

uint8_t tuya_wait_event(tuya_event_id_t event, uint32_t timeout)
{
    uint32_t t = xTaskGetTickCount() * portTICK_PERIOD_MS + timeout;

    while (xTaskGetTickCount() * portTICK_PERIOD_MS < t)
    {
        if (new_event == 1)
        {
            new_event = 0;

            if (last_event == event)
            {
                return 0;
            }
        }

        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    return 1;
}

static void tuya_link_app_task(void *pvParameters)
{
    int ret = OPRT_OK;
    tuya_iot_config_t config = {
        .software_ver = "1.0.0", // TODO: add ver
        .productkey = app_cfg.tuya.product_key,
        .uuid = app_cfg.tuya.uuid,
        .authkey = app_cfg.tuya.auth_key,
        .storage_namespace = "tuya",
        .event_handler = tuya_user_event_handler_on};

    ret = tuya_iot_init(&client, &config);

    assert(ret == OPRT_OK);
    tuya_iot_start(&client);

    if (app_cfg.pairing_state == PAIRING_BLE_PAIRING)
    {
        ESP_LOGI(TAG, "ble pairing...");
        tuya_deactivate();
        tuya_wifi_provisioning(&client, WIFI_PROVISIONING_MODE_BLE, &tuya_wifi_info_cb);

        // TODO: int tuya_wifi_provisioning_stop(tuya_iot_client_t *client);
    }

    for (;;)
    {
        tuya_iot_yield(&client);
    }
}

/* Tuya SDK event callback */
static void tuya_user_event_handler_on(tuya_iot_client_t *client, tuya_event_msg_t *event)
{
    switch (event->id)
    {
    case TUYA_EVENT_BIND_START:
        if (app_cfg.pairing_state != PAIRING_BLE_PAIRING)
        {
            tuya_qrcode_print(client->config.productkey, client->config.uuid);
        }
        break;

    case TUYA_EVENT_MQTT_CONNECTED:
        ESP_LOGI(TAG, "device MQTT connected");
        break;

    case TUYA_EVENT_DP_RECEIVE:
        ESP_LOGI(TAG, "dp receive");
        tuya_dp_download(client, (const char *)event->value.asString);
        break;

    case TUYA_EVENT_RESET:
        ESP_LOGI(TAG, "reset");

        app_factory_reset();

        break;

    case TUYA_EVENT_ACTIVATE_SUCCESSED:
        ESP_LOGI(TAG, "activated");
        break;

    default:
        break;
    }

    new_event = 1;
    last_event = event->id;
}

void tuya_dp_download(tuya_iot_client_t *client, const char *json_dps)
{
    ESP_LOGI(TAG, "data point download value: %s", json_dps);

    cJSON *dps = cJSON_Parse(json_dps);
    if (dps == NULL)
    {
        ESP_LOGE(TAG, "JSON parsing error, exit");

        return;
    }

    // TODO: Here you can write your own logic

    cJSON *switch_obj = cJSON_GetObjectItem(dps, "101");
    if (cJSON_IsTrue(switch_obj))
    {
        hardware_switch_set(true);
    }
    else if (cJSON_IsFalse(switch_obj))
    {
        hardware_switch_set(false);
    }

    cJSON_Delete(dps);
    tuya_iot_dp_report_json(client, json_dps);
}

void tuya_wifi_info_cb(wifi_info_t wifi_info)
{
    ESP_LOGI(TAG, "got ble callback");

    strncpy(app_cfg.wifi_ssid, (char *)wifi_info.ssid, sizeof(app_cfg.wifi_ssid));
    strncpy(app_cfg.wifi_password, (char *)wifi_info.pwd, sizeof(app_cfg.wifi_password));

    app_cfg.pairing_state = PAIRING_WIFI_CONNECTING;

    app_cfg_write();

    esp_err_t err = wifi_connect((const char *)wifi_info.ssid, (const char *)wifi_info.pwd);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "pairing error: unable to connect to wifi");
        return;
    }

    ESP_LOGI(TAG, "successfully paired");

    return;
}

static void tuya_qrcode_print(const char *productkey, const char *uuid)
{
    ESP_LOGI(TAG, "https://smartapp.tuya.com/s/p?p=%s&uuid=%s&v=2.0", productkey, uuid);

    char urlbuf[255];

    sprintf(urlbuf, "https://smartapp.tuya.com/s/p?p=%s&uuid=%s&v=2.0", productkey, uuid);
    qrcode_display(urlbuf);

    ESP_LOGI(TAG, "(Use this URL to generate a static QR code for the Tuya APP scan code binding)");
}

/* Hardware switch control function */
void hardware_switch_set(bool value)
{
    if (value == true)
    {
        ESP_LOGI(TAG, "switch ON");
    }
    else
    {
        ESP_LOGI(TAG, "switch OFF");
    }
}
