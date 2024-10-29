#ifndef _WIFI_H_
#define _WIFI_H_

#include "esp_wifi.h"

#define WIFI_CONN_TIMEOUT_MS 10000

typedef enum
{
    WIFI_DISCONNECTED,
    WIFI_STARTED,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_FAILED
} wifi_state_t;

extern wifi_state_t wifi_state;
extern wifi_ap_record_t wifi_ap_list[20];
extern esp_netif_ip_info_t wifi_curr_ip;

extern uint8_t wifi_init();
extern esp_err_t wifi_connect(const char *ssid, const char *pwd);
extern esp_err_t wifi_disconnect();

#endif
