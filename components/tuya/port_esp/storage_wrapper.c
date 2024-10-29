#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "tuya_error_code.h"
#include "storage_interface.h"
#include "system_interface.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#define STORAGE_NAMESPACE "tuya_storage"

static const char *TAG = "tuya_storage_wrapper";

nvs_handle_t storage_handle;

int local_storage_init(void)
{
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &storage_handle);
    if (err != ESP_OK)
        return err;

    nvs_stats_t stats;

    ESP_ERROR_CHECK(nvs_get_stats(NULL, &stats));
    printf(
        "used entries: %3zu\t"
        "free entries: %3zu\t"
        "total entries: %3zu\t"
        "namespace count: %3zu\n",
        stats.used_entries,
        stats.free_entries,
        stats.total_entries,
        stats.namespace_count);

    nvs_iterator_t it = NULL;
    esp_err_t res = nvs_entry_find("nvs", STORAGE_NAMESPACE, NVS_TYPE_ANY, &it);

    ESP_LOGI(TAG, "nvs_entry_find: 0x%x", res);

    while (res == ESP_OK)
    {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info); // Can omit error check if parameters are guaranteed to be non-NULL

        size_t size = 0;

        nvs_get_blob(storage_handle, info.key, NULL, &size);
        printf("namespace: %s\tkey: %s\tType: %d\tSize: %d\n", info.namespace_name, info.key, info.type, size);

        res = nvs_entry_next(&it);
    }

    nvs_release_iterator(it);

    return OPRT_OK;
}

int local_storage_clear(void)
{
    ESP_LOGE(TAG, "local_storage_clear");

    esp_err_t err = nvs_flash_erase();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_flash_erase failed: 0x%x", err);

        return err;
    }

    return OPRT_OK;
}
int local_storage_set(const char *key, const uint8_t *buffer, size_t length)
{
    if (NULL == key || NULL == buffer)
    {
        return OPRT_INVALID_PARM;
    }

    ESP_LOGD(TAG, "set key:%s", key);

    esp_err_t err;

    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &storage_handle);
    if (err != ESP_OK)
        return err;

    err = nvs_set_blob(storage_handle, key, buffer, length);
    if (err != ESP_OK)
        return err;

    err = nvs_commit(storage_handle);
    if (err != ESP_OK)
        return err;

    nvs_close(storage_handle);

    return OPRT_OK;
}

int local_storage_get(const char *key, uint8_t *buffer, size_t *length)
{
    if (NULL == key || NULL == buffer || NULL == length)
    {
        return OPRT_INVALID_PARM;
    }

    ESP_LOGD(TAG, "get key:%s, len:%d", key, (int)*length);

    esp_err_t err;

    err = nvs_open(STORAGE_NAMESPACE, NVS_READONLY, &storage_handle);
    if (err != ESP_OK)
        return err;

    err = nvs_get_blob(storage_handle, key, NULL, length);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        return err;
    }

    ESP_LOGD(TAG, "get key:%s, xlen:%d", key, *length);

    err = nvs_get_blob(storage_handle, key, buffer, length);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
        return err;

    nvs_close(storage_handle);

    return OPRT_OK;
}

int local_storage_del(const char *key)
{
    ESP_LOGD("ta_storage", "key:%s", key);

    esp_err_t err;

    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &storage_handle);
    if (err != ESP_OK)
        return err;

    err = nvs_erase_key(storage_handle, key);

    ESP_LOGE(TAG, "local_storage_del: %s", key);

    if (err != ESP_OK)
        return err;

    nvs_close(storage_handle);

    return OPRT_OK;
}
