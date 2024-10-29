#include <string.h>
#include "config.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#define STORAGE_NAMESPACE "config"

static const char *TAG = "config";

struct app_cfg_item_t
{
    const char *label;
    app_type_t type;
    void *value;
    size_t size;
    nvs_handle_t *handle;
};

app_cfg_t app_cfg = {0};

static nvs_handle_t cfg_handle = 0;
static struct app_cfg_item_t app_cfg_items[] = {
    {"ready", UINT8, &app_cfg.ready, sizeof(app_cfg.ready), &cfg_handle},
    {"wifi_ssid", STR, &app_cfg.wifi_ssid, sizeof(app_cfg.wifi_ssid), &cfg_handle},
    {"wifi_password", STR, &app_cfg.wifi_password, sizeof(app_cfg.wifi_password), &cfg_handle},
    {"paired", UINT8, &app_cfg.paired, sizeof(app_cfg.paired), &cfg_handle},
    {"pairing_state", UINT8, &app_cfg.pairing_state, sizeof(app_cfg.pairing_state), &cfg_handle},
    {"tuya", BLOB, &app_cfg.tuya, sizeof(app_cfg.tuya), &cfg_handle},
};
static const int32_t app_cfg_items_size = sizeof(app_cfg_items) / sizeof(app_cfg_items[0]);

int8_t app_cfg_init()
{
    uint8_t ready = 1;
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        err = nvs_flash_erase();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "nvs_flash_erase failed with %s(0x%X)", esp_err_to_name(err), err);

            return 1;
        }

        err = nvs_flash_init();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "nvs_flash_init failed with %s(0x%X)", esp_err_to_name(err), err);

            return 1;
        }

        ready = 0;
    }

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "unable to init NVS flash %s(0x%X)", esp_err_to_name(err), err);

        return 1;
    }

    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &cfg_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "unable to open NVS flash %s(0x%X)", esp_err_to_name(err), err);
    }

    app_cfg_read();

    if (ready == 0 || app_cfg.ready == 0)
    {
        ESP_LOGW(TAG, "config not found, creating default config");

        app_cfg_erase();
    }

    app_cfg_write();
    app_cfg_read();

    if (app_cfg.ready)
    {
        ESP_LOGI(TAG, "config successfully initialized");
    }
    else
    {
        ESP_LOGE(TAG, "config not initialized");
    }

    return 0;
}

int8_t app_cfg_erase()
{
    app_cfg_t default_cfg = {
        .ready = 1,
        .paired = 0, // TODO: enable on boot on button press
        .pairing_state = PAIRING_NOT_PAIRED,
        .tuya = {
            .product_key = "wxtwxnwrbbbazfpd",              // TODO: move that to secure place
            .auth_key = "0gR5VzFt5lqNAZgK2SvlXf9kMo3Gv0kg", // TODO: move that to secure place
            .uuid = "uuid1d0d070070e5e1fc",                 // TODO: move that to secure place
        },
    };

    app_cfg = default_cfg;

    return 0;
}

int8_t app_cfg_read()
{
    esp_err_t err = 0;
    size_t total_read = 0;

    for (int i = 0; i < app_cfg_items_size; i++)
    {
        size_t item_read = 0;

        switch (app_cfg_items[i].type)
        {
        case STR:
            err = nvs_get_str(*app_cfg_items[i].handle, app_cfg_items[i].label, (char *)app_cfg_items[i].value, &app_cfg_items[i].size);

            break;
        case UINT8:
            err = nvs_get_u8(*app_cfg_items[i].handle, app_cfg_items[i].label, (uint8_t *)app_cfg_items[i].value);
            item_read = sizeof(uint8_t);

            break;
        case UINT16:
            err = nvs_get_u16(*app_cfg_items[i].handle, app_cfg_items[i].label, (uint16_t *)app_cfg_items[i].value);
            item_read = sizeof(uint16_t);

            break;
        case UINT32:
            err = nvs_get_u32(*app_cfg_items[i].handle, app_cfg_items[i].label, (uint32_t *)app_cfg_items[i].value);
            item_read = sizeof(uint32_t);

            break;
        case UINT64:
            err = nvs_get_u64(*app_cfg_items[i].handle, app_cfg_items[i].label, (uint64_t *)app_cfg_items[i].value);
            item_read = sizeof(uint64_t);

            break;
        case BLOB:
            err = nvs_get_blob(*app_cfg_items[i].handle, app_cfg_items[i].label, app_cfg_items[i].value, &app_cfg_items[i].size);

            break;
        default:
            break;
        }
        if (err == ESP_ERR_NVS_NOT_FOUND)
        {
            ESP_LOGE(TAG, "error reading %s: value not found", app_cfg_items[i].label);

            continue;
        }
        else if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "error (0x%x %s) reading %s", err, esp_err_to_name(err), app_cfg_items[i].label);

            continue;
        }

        total_read += item_read;
    }

    ESP_LOGI(TAG, "config read %d bytes", total_read);

    return 0;
}

int8_t app_cfg_write()
{
    esp_err_t err = 0;
    size_t total_written = 0;

    for (int i = 0; i < app_cfg_items_size; i++)
    {
        size_t item_written = 0;

        switch (app_cfg_items[i].type)
        {
        case STR:
            err = nvs_set_str(*app_cfg_items[i].handle, app_cfg_items[i].label, (char *)app_cfg_items[i].value);
            item_written = strlen((char *)app_cfg_items[i].value);

            break;
        case UINT8:
            err = nvs_set_u8(*app_cfg_items[i].handle, app_cfg_items[i].label, *(uint8_t *)app_cfg_items[i].value);
            item_written = sizeof(uint8_t);

            break;
        case UINT16:
            err = nvs_set_u16(*app_cfg_items[i].handle, app_cfg_items[i].label, *(uint16_t *)app_cfg_items[i].value);
            item_written = sizeof(uint16_t);

            break;
        case UINT32:
            err = nvs_set_u32(*app_cfg_items[i].handle, app_cfg_items[i].label, *(uint32_t *)app_cfg_items[i].value);
            item_written = sizeof(uint32_t);

            break;
        case UINT64:
            err = nvs_set_u64(*app_cfg_items[i].handle, app_cfg_items[i].label, *(uint64_t *)app_cfg_items[i].value);
            item_written = sizeof(uint64_t);

            break;

        case BLOB:
            err = nvs_set_blob(*app_cfg_items[i].handle, app_cfg_items[i].label, app_cfg_items[i].value, app_cfg_items[i].size);
            item_written = app_cfg_items[i].size;

            break;
        default:
            break;
        }
        if (err == ESP_ERR_NVS_READ_ONLY)
        {
            ESP_LOGE(TAG, "error writing %s: read-only partition (enable write with 'rw' command)", app_cfg_items[i].label);

            continue;
        }
        if (err == ESP_ERR_NVS_NOT_ENOUGH_SPACE)
        {
            ESP_LOGE(TAG, "error writing %s: not enough space", app_cfg_items[i].label);

            continue;
        }
        else if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "error (0x%x %s) writing %s", err, esp_err_to_name(err), app_cfg_items[i].label);

            continue;
        }

        err = nvs_commit(*app_cfg_items[i].handle);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "error (%s) committing %s", esp_err_to_name(err), app_cfg_items[i].label);
            continue;
        }

        total_written += item_written;
    }

    ESP_LOGI(TAG, "config written %d bytes", total_written);

    return 0;
}

uint8_t app_cfg_verify()
{
    if (strlen(app_cfg.wifi_ssid) == 0 || strlen(app_cfg.wifi_password) == 0)
    {
        return 1;
    }

    if (strlen(app_cfg.tuya.product_key) == 0 || strlen(app_cfg.tuya.uuid) == 0 || strlen(app_cfg.tuya.auth_key) == 0 || app_cfg.pairing_state != PAIRING_PAIRED)
    {
        return 1;
    }

    return 0;
}

void hard_reset()
{
    gpio_set_direction((gpio_num_t)15, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)15, 0);
    esp_restart();
}

uint8_t app_factory_reset()
{
    esp_err_t err = nvs_flash_erase();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "failed to erase NVS (%s 0x%x)", esp_err_to_name(err), err);
    }
    else
    {
        ESP_LOGI(TAG, "NVS erased");
    }

    app_cfg_init();
    app_cfg_erase();
    app_cfg_write();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    hard_reset();

    return 0;
}

void app_cfg_print()
{
    ESP_LOGI(TAG, "current config:");
    ESP_LOGI(TAG, "ready: %d", app_cfg.ready);
    ESP_LOGI(TAG, "wifi_ssid: %s", app_cfg.wifi_ssid);
    ESP_LOGI(TAG, "wifi password: %s", app_cfg.wifi_password);
    ESP_LOGI(TAG, "paired: %d", app_cfg.paired);
    ESP_LOGI(TAG, "pairing state: %d", app_cfg.pairing_state);
    ESP_LOGI(TAG, "tuya:");
    ESP_LOGI(TAG, "product key: %s", app_cfg.tuya.product_key);
    ESP_LOGI(TAG, "device uuid: %s", app_cfg.tuya.uuid);
    ESP_LOGI(TAG, "auth key: %s", app_cfg.tuya.auth_key);
}
