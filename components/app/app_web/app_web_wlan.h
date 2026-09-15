#pragma once
#include "esp_http_server.h"

#define APP_WEB_WLAN_ROUTE_COUNT 3

/**
 * @brief Register the WLAN provisioning routes (status, scan, connect) on the
 *        server app_web started.
 */
esp_err_t app_web_wlan_register(httpd_handle_t server);
