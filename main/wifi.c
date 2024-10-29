#include "wifi.h"
#include "sdkconfig.h"
#include "config.h"
#include <string.h>
#include "esp_log.h"
#include "tuya_cloud_types.h"

#ifndef MAC2STR
#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#endif

#define ESP_MAXIMUM_RETRY 4
#define WIFI_CONNECT_FAIL_COUNT_BEFORE_RESET 10
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define WIFI_AUTHFAIL_BIT BIT2
#define WIFI_NO_AP_FOUND_BIT BIT3

static const char *TAG = "wifi";

wifi_state_t wifi_state = WIFI_DISCONNECTED;

esp_netif_ip_info_t wifi_curr_ip;

uint32_t wifi_timeout_counter = 0;
wifi_ap_record_t wifi_ap_list[20];

static int s_retry_num = 0;
static EventGroupHandle_t s_wifi_event_group;
static EventGroupHandle_t ping_event_group;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

static uint8_t ap_started = 0;
static uint8_t sta_connecting = 0;

static uint32_t ping_time_array[10];
static uint32_t ping_idx;

static wifi_config_t wifi_sta_cfg = {
    .sta = {
        .threshold = {
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
        },
        .sae_h2e_identifier = {0},
        .sae_pwe_h2e = WPA3_SAE_PWE_HUNT_AND_PECK,
    },
};

static esp_err_t last_wifi_err = ESP_OK;

uint8_t wifi_init()
{
    esp_err_t err = ESP_OK;

    err = esp_netif_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_netif_init failed with 0x%X", err);
        return 0;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed with 0x%X", err);
        return 0;
    }

    esp_netif_t *netif = esp_netif_create_default_wifi_sta();

    err = esp_netif_set_hostname(netif, HOSTNAME);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to set hostname: 0x%X", err);
        return 0; // TODO: ???
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    err = esp_wifi_init(&cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_wifi_init failed with 0x%X", err);
        return 0;
    }

    esp_event_handler_instance_t instance_any_ip;
    esp_event_handler_instance_t instance_got_ip;

    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_ip);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_event_handler_instance_register failed with 0x%X", err);
        return 0;
    }

    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_event_handler_instance_register failed with 0x%X", err);
        return 0;
    }

    s_wifi_event_group = xEventGroupCreate();
    ping_event_group = xEventGroupCreate();

    return 1;
}

esp_err_t wifi_connect(const char *ssid, const char *pwd)
{
    esp_err_t err = ESP_OK;

    if (wifi_state == WIFI_CONNECTED)
    {
        return ESP_OK;
    }

    if (strlen(ssid) == 0 || strlen(pwd) == 0)
    {
        ESP_LOGI(TAG, "no WiFi SSID or password");
        return ESP_ERR_WIFI_MODE;
    }

    if (wifi_timeout_counter > WIFI_CONNECT_FAIL_COUNT_BEFORE_RESET)
    {
        ESP_LOGE(TAG, "too many wifi timeout (%ld): Hard reset", wifi_timeout_counter);
        // TODO: reset
        return ESP_ERR_WIFI_STATE;
    }

    wifi_state = WIFI_CONNECTING;
    s_retry_num = 0;

    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT | WIFI_AUTHFAIL_BIT | WIFI_NO_AP_FOUND_BIT);

    strncpy((char *)wifi_sta_cfg.sta.ssid, ssid, MIN(strlen(ssid), sizeof(app_cfg.wifi_ssid)));
    strncpy((char *)wifi_sta_cfg.sta.password, pwd, MIN(strlen(pwd), sizeof(app_cfg.wifi_password)));

    wifi_mode_t mode;

    err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_wifi_get_mode failed with 0x%X", err);

        wifi_state = WIFI_FAILED;

        wifi_disconnect();

        return err;
    }

    if (mode != WIFI_MODE_STA)
    {
        mode = WIFI_MODE_STA;

        if (ap_started)
        {
            ESP_LOGI(TAG, "wifi_connect: APSTA mode");
            mode = WIFI_MODE_APSTA;
        }

        err = esp_wifi_set_mode(mode);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_wifi_set_mode failed with 0x%X", err);

            wifi_state = WIFI_FAILED;

            wifi_disconnect();

            return err;
        }
    }

    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_wifi_set_config failed with 0x%X", err);
    }

    err = esp_wifi_start();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_wifi_start failed with 0x%X", err);

        wifi_state = WIFI_FAILED;

        wifi_disconnect();

        return err;
    }

    ESP_LOGI(TAG, "connecting to %s", (char *)wifi_sta_cfg.sta.ssid);

    if (sta_connecting == 0)
    {
        sta_connecting = 1;
        err = esp_wifi_connect();
        if (err != ESP_OK && err != ESP_ERR_WIFI_CONN)
        {
            ESP_LOGE(TAG, "esp_wifi_connect failed with 0x%X", err);

            wifi_state = WIFI_FAILED;

            wifi_disconnect();

            return err;
        }
    }

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT | WIFI_AUTHFAIL_BIT | WIFI_NO_AP_FOUND_BIT, pdFALSE, pdFALSE, WIFI_CONN_TIMEOUT_MS / portTICK_PERIOD_MS);

    sta_connecting = 0;

    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "Connected to ap SSID: %s", (char *)wifi_sta_cfg.sta.ssid);

        wifi_timeout_counter = 0;

        return ESP_OK;
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG, "failed to connect to SSID: %s", (char *)wifi_sta_cfg.sta.ssid);

        wifi_state = WIFI_FAILED;

        wifi_disconnect();

        return ESP_FAIL;
    }
    else if (bits & WIFI_AUTHFAIL_BIT)
    {
        ESP_LOGE(TAG, "failed to connect to SSID: %s auth fail", (char *)wifi_sta_cfg.sta.ssid);
        wifi_timeout_counter++;

        wifi_state = WIFI_FAILED;

        wifi_disconnect();

        return ESP_ERR_WIFI_PASSWORD;
    }
    else if (bits & WIFI_NO_AP_FOUND_BIT)
    {
        ESP_LOGE(TAG, "failed to connect to SSID:%s No AP found", (char *)wifi_sta_cfg.sta.ssid);
        wifi_timeout_counter++;

        wifi_state = WIFI_FAILED;

        wifi_disconnect();

        return ESP_ERR_WIFI_SSID;
    }
    else
    {
        ESP_LOGE(TAG, "failed to connect to SSID:%s Timeout", (char *)wifi_sta_cfg.sta.ssid);

        wifi_timeout_counter++;

        wifi_state = WIFI_FAILED;

        wifi_disconnect();

        return last_wifi_err == ESP_OK ? ESP_ERR_TIMEOUT : last_wifi_err;
    }
}

esp_err_t wifi_disconnect(void)
{
    esp_err_t err = ESP_OK;

    if (wifi_state == WIFI_DISCONNECTED)
    {
        ESP_LOGD(TAG, "wifi already disconnected");

        return ESP_OK;
    }

    err = esp_wifi_disconnect();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_wifi_disconnect failed with 0x%X", err);
    }

    if (!ap_started)
    {
        ESP_LOGI(TAG, "disconnected");

        err = esp_wifi_stop();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_wifi_stop failed with 0x%X", err);
        }
    }
    else
    {
        ESP_LOGI(TAG, "keep WiFi in AP mode");
    }

    wifi_state = WIFI_DISCONNECTED;

    return err;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{

    ESP_LOGI(TAG, "got event: event_base: %s, event_id: %ld", event_base, event_id);

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START && wifi_state != WIFI_DISCONNECTED)
    {
        sta_connecting = 1;
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED && wifi_state != WIFI_DISCONNECTED)
    {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGI(TAG, "disconnected from SSID:%s, reason:%u, rssi%d", (char *)event->ssid, event->reason, event->rssi);
        if (s_retry_num < ESP_MAXIMUM_RETRY)
        {
            wifi_state = WIFI_CONNECTING;
            sta_connecting = 1;
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "Retry to connect to the AP: %d/%d", s_retry_num, ESP_MAXIMUM_RETRY);
        }
        else
        {
            ESP_LOGW(TAG, "Reason: %d", event->reason);
            last_wifi_err = event->reason;
            switch (event->reason)
            {
            case WIFI_REASON_AUTH_FAIL:
            case WIFI_REASON_CONNECTION_FAIL:
            case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
                ESP_LOGE(TAG, "Auth fail");
                xEventGroupSetBits(s_wifi_event_group, WIFI_AUTHFAIL_BIT);
                break;
            case WIFI_REASON_NO_AP_FOUND:
                ESP_LOGE(TAG, "No AP found");
                xEventGroupSetBits(s_wifi_event_group, WIFI_NO_AP_FOUND_BIT);
                break;
            default:
                ESP_LOGE(TAG, "Connect to the AP fail");
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                break;
            }
            wifi_state = WIFI_FAILED;
        }
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED)
    {
        ESP_LOGI(TAG, "Connected");
        wifi_state = WIFI_CONNECTED;
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "IP:" IPSTR, IP2STR(&event->ip_info.ip));
        wifi_curr_ip = event->ip_info;
        s_retry_num = 0;
        wifi_state = WIFI_CONNECTED;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
    else if (event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
    else
    {
        ESP_LOGE(TAG, "unknown event: event_base: %s, event_id: %ld", event_base, event_id);
    }
}
