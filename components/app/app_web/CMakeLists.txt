# TODO(backend): add app_synth (and app_event_bus for /api/note) to
# PRIV_REQUIRES when the API handlers start driving the synth.
idf_component_register(SRCS "app_web.c" "app_web_wlan.c"
                        INCLUDE_DIRS "include"
                        REQUIRES app_core
                        PRIV_REQUIRES esp_http_server sys_wlan json esp_timer
                        EMBED_FILES "www/index.html")
