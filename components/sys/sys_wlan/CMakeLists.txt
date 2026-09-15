idf_component_register(SRCS "sys_wlan.c" "sys_wlan_nvs.c"
                        INCLUDE_DIRS "include"
                        REQUIRES esp_wifi esp_netif
                        PRIV_REQUIRES esp_event esp_timer esp_hw_support nvs_flash sys_nvs)
