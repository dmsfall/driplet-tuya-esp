#ifndef _APP_CONFIG_H_
#define _APP_CONFIG_H_

#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define HOSTNAME "drplt"

typedef enum
{
    STR = 0,
    UINT8 = 1,
    UINT16 = 2,
    UINT32 = 4,
    UINT64 = 8,
    BLOB = 14,
    BOOL
} app_type_t;

typedef enum
{
    PAIRING_NOT_PAIRED,
    PAIRING_BLE_PAIRING,
    PAIRING_WIFI_CONNECTING,
    PAIRING_PAIRED,
} pairing_state_t;

typedef struct
{
    char product_key[20];
    char uuid[30];
    char auth_key[40];
} tuya_cfg_t;

typedef struct
{
    uint8_t ready;

    char wifi_ssid[32];
    char wifi_password[64];

    uint8_t paired;
    pairing_state_t pairing_state;

    tuya_cfg_t tuya;
} app_cfg_t;

extern app_cfg_t app_cfg;

int8_t app_cfg_init();
int8_t app_cfg_erase();
int8_t app_cfg_read();
int8_t app_cfg_write();
uint8_t app_cfg_verify();
uint8_t app_factory_reset();

void app_cfg_print();

#endif
