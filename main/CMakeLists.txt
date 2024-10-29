idf_component_register(SRCS "main.c" "config.c" "wifi.c" "tuya.c"
                    INCLUDE_DIRS "."
                    INCLUDE_DIRS "include"
                    REQUIRES driver esp_netif esp_wifi esp_event qrcode tuya)
