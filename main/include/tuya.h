#ifndef _TUYA_H_
#define _TUYA_H_

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tuya_iot.h"

extern TaskHandle_t tuya_main_task_handle;
extern TaskHandle_t tuya_ble_pairing_task_handle;

extern void tuya_init();
extern void tuya_deinit();
extern void tuya_deactivate();
extern uint8_t tuya_reconnect();
extern uint8_t tuya_stop();
extern uint8_t tuya_is_configured();

extern void tuya_pairing_task(void *pvParameters);

#endif
