// WLAN provisioning API for app_web: the page shows the link/AP state, scans
// for networks and starts a station connection. All radio logic lives in
// sys_wlan; this file only translates HTTP <-> sys_wlan calls.
#include "app_web_wlan.h"
#include "sys_wlan.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "app_web_wlan";

// Largest connect body: {"ssid":<32>,"password":<64>} plus JSON noise and a
// little room for escaping.
#define APP_WEB_WLAN_BODY_MAX 160
#define APP_WEB_WLAN_SCAN_MAX  32

static esp_err_t web_send_json(httpd_req_t *req, const char *status, const char *json)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t web_recv_body(httpd_req_t *req, char *buf, size_t size)
{
    int total = req->content_len;
    if (total <= 0) {
        buf[0] = '\0';
        return ESP_OK;
    }
    if ((size_t)total >= size) {
        ESP_LOGW(TAG, "%s: body too large (%d bytes)", req->uri, total);
        return ESP_ERR_INVALID_SIZE;
    }

    int received = 0;
    while (received < total) {
        int ret = httpd_req_recv(req, buf + received, total - received);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (ret <= 0) {
            return ESP_FAIL;
        }
        received += ret;
    }
    buf[received] = '\0';
    return ESP_OK;
}

static const char *wlan_state_name(sys_wlan_state_t state)
{
    switch (state) {
    case SYS_WLAN_STATE_CONNECTING:
        return "connecting";
    case SYS_WLAN_STATE_CONNECTED:
        return "connected";
    case SYS_WLAN_STATE_DISCONNECTED:
        return "disconnected";
    case SYS_WLAN_STATE_STOPPED:
    default:
        return "stopped";
    }
}

// GET /api/wlan/status -- one snapshot for the status pill and the settings
// dialog; the page polls it while a connect attempt is in flight.
static esp_err_t web_wlan_status_get(httpd_req_t *req)
{
    sys_wlan_status_t status;
    esp_err_t err = sys_wlan_get_status(&status);
    if (err != ESP_OK) {
        return web_send_json(req, "500 Internal Server Error", "{\"error\":\"wlan not ready\"}");
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return web_send_json(req, "500 Internal Server Error", "{\"error\":\"out of memory\"}");
    }
    cJSON_AddStringToObject(root, "state", wlan_state_name(status.state));
    cJSON_AddStringToObject(root, "sta_ssid", status.sta_ssid);
    cJSON_AddStringToObject(root, "sta_ip", status.sta_ip);
    cJSON_AddNumberToObject(root, "sta_rssi", status.sta_rssi);
    cJSON_AddBoolToObject(root, "ap_active", status.ap_active);
    cJSON_AddStringToObject(root, "ap_ssid", status.ap_ssid);
    cJSON_AddStringToObject(root, "ap_ip", status.ap_ip);
    cJSON_AddNumberToObject(root, "ap_clients", status.ap_clients);

    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (text == NULL) {
        return web_send_json(req, "500 Internal Server Error", "{\"error\":\"out of memory\"}");
    }
    err = web_send_json(req, "200 OK", text);
    free(text);
    return err;
}

// GET /api/wlan/scan -- blocks a few seconds while the radio scans; the page
// uses a generous timeout for exactly that reason.
static esp_err_t web_wlan_scan_get(httpd_req_t *req)
{
    sys_wlan_ap_t aps[APP_WEB_WLAN_SCAN_MAX];
    size_t count = 0;

    esp_err_t err = sys_wlan_scan(aps, APP_WEB_WLAN_SCAN_MAX, &count);
    if (err == ESP_ERR_INVALID_STATE) {
        return web_send_json(req, "409 Conflict", "{\"error\":\"scan already running\"}");
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "scan failed: %s", esp_err_to_name(err));
        return web_send_json(req, "500 Internal Server Error", "{\"error\":\"scan failed\"}");
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return web_send_json(req, "500 Internal Server Error", "{\"error\":\"out of memory\"}");
    }
    cJSON *list = cJSON_AddArrayToObject(root, "aps");
    for (size_t i = 0; i < count; i++) {
        // The raw scan repeats one AP per channel; only the strongest entry
        // per SSID matters for the list the page shows.
        bool duplicate = false;
        for (size_t j = 0; j < i; j++) {
            if (strcmp(aps[j].ssid, aps[i].ssid) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }

        cJSON *ap = cJSON_CreateObject();
        if (ap == NULL) {
            continue;
        }
        cJSON_AddStringToObject(ap, "ssid", aps[i].ssid);
        cJSON_AddNumberToObject(ap, "rssi", aps[i].rssi);
        cJSON_AddNumberToObject(ap, "channel", aps[i].channel);
        cJSON_AddNumberToObject(ap, "auth", aps[i].authmode);
        cJSON_AddBoolToObject(ap, "open", aps[i].authmode == WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(list, ap);
    }

    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (text == NULL) {
        return web_send_json(req, "500 Internal Server Error", "{\"error\":\"out of memory\"}");
    }
    err = web_send_json(req, "200 OK", text);
    free(text);
    return err;
}

// POST /api/wlan/connect -- { "ssid": "...", "password": "..." }. Credentials
// are always saved, so the next boot connects without the portal. The response
// comes back immediately; the page follows the attempt via /api/wlan/status.
static esp_err_t web_wlan_connect_post(httpd_req_t *req)
{
    char body[APP_WEB_WLAN_BODY_MAX];
    esp_err_t err = web_recv_body(req, body, sizeof(body));
    if (err == ESP_ERR_INVALID_SIZE) {
        return web_send_json(req, "413 Payload Too Large", "{\"error\":\"body too large\"}");
    }
    if (err != ESP_OK) {
        return web_send_json(req, "400 Bad Request", "{\"error\":\"read failed\"}");
    }

    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        return web_send_json(req, "400 Bad Request", "{\"error\":\"invalid json\"}");
    }

    const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(root, "ssid");
    const cJSON *password = cJSON_GetObjectItemCaseSensitive(root, "password");
    bool bad = ssid == NULL || !cJSON_IsString(ssid) ||
               ssid->valuestring[0] == '\0' || strlen(ssid->valuestring) > SYS_WLAN_SSID_MAX_LEN;
    if (!bad && password != NULL) {
        bad = !cJSON_IsString(password) || strlen(password->valuestring) > SYS_WLAN_PASSWORD_MAX_LEN;
    }
    if (bad) {
        cJSON_Delete(root);
        return web_send_json(req, "400 Bad Request", "{\"error\":\"invalid ssid or password\"}");
    }

    ESP_LOGI(TAG, "connect request for \"%s\"", ssid->valuestring);
    const char *pass = (password != NULL) ? password->valuestring : "";
    err = sys_wlan_connect(ssid->valuestring, pass, true);
    cJSON_Delete(root);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "connect failed to start: %s", esp_err_to_name(err));
        return web_send_json(req, "500 Internal Server Error", "{\"error\":\"connect failed\"}");
    }
    return web_send_json(req, "200 OK", "{\"state\":\"connecting\"}");
}

static const httpd_uri_t s_wlan_routes[] = {
    { .uri = "/api/wlan/status",  .method = HTTP_GET,  .handler = web_wlan_status_get },
    { .uri = "/api/wlan/scan",    .method = HTTP_GET,  .handler = web_wlan_scan_get },
    { .uri = "/api/wlan/connect", .method = HTTP_POST, .handler = web_wlan_connect_post },
};

esp_err_t app_web_wlan_register(httpd_handle_t server)
{
    for (size_t i = 0; i < APP_WEB_WLAN_ROUTE_COUNT; i++) {
        esp_err_t err = httpd_register_uri_handler(server, &s_wlan_routes[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "registering %s failed: %s", s_wlan_routes[i].uri, esp_err_to_name(err));
            // Drop what was registered so far, so a retry starts clean.
            for (size_t j = 0; j < i; j++) {
                httpd_unregister_uri_handler(server, s_wlan_routes[j].uri, s_wlan_routes[j].method);
            }
            return err;
        }
    }
    ESP_LOGI(TAG, "%u wlan routes registered", (unsigned)APP_WEB_WLAN_ROUTE_COUNT);
    return ESP_OK;
}
