idf_component_register(SRCS "app_web.c" "app_web_wlan.c"
                        INCLUDE_DIRS "include"
                        REQUIRES app_core
                        PRIV_REQUIRES esp_http_server sys_wlan json esp_timer app_synth app_event_bus
                        EMBED_FILES "www/index.html")
