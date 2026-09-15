#include "app_web.h"
#include "app_web_wlan.h"
#include "sys_wlan.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "app_web";

// Largest request body the API accepts. Every call the page makes is a handful
// of numbers; the biggest is /api/op with a full envelope.
#define APP_WEB_BODY_MAX 256

#define APP_WEB_STUB_JSON "{\"error\":\"not implemented\"}"

// www/index.html, embedded by EMBED_FILES (which appends a NUL byte).
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

app_web_app_t app_web_app;
static httpd_handle_t s_server;

static esp_err_t app_web_app_init_cb(app_t *app);
static esp_err_t app_web_start(app_t *app);
static esp_err_t app_web_stop(app_t *app);
static size_t app_web_get_state(app_t *app, int state, void *out, uint8_t size);
static esp_err_t app_web_command(app_t *app, int16_t command, ...);

/* ------------------------------------------------------------------ *
 *  helpers
 * ------------------------------------------------------------------ */

static esp_err_t web_send_json(httpd_req_t *req, const char *status, const char *json)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

// Reads the whole request body into `buf`. The API handlers are stubs for now,
// so this mainly makes what the page sends visible in the monitor while the
// frontend is being worked on -- and it is what the real handlers will feed to
// the JSON parser.
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

// Common tail for every POST stub: pull the body in, log it, answer 501.
static esp_err_t web_stub_post(httpd_req_t *req)
{
    char body[APP_WEB_BODY_MAX];
    esp_err_t err = web_recv_body(req, body, sizeof(body));
    if (err == ESP_ERR_INVALID_SIZE) {
        return web_send_json(req, "413 Payload Too Large", "{\"error\":\"body too large\"}");
    }
    if (err != ESP_OK) {
        return web_send_json(req, "400 Bad Request", "{\"error\":\"read failed\"}");
    }

    ESP_LOGI(TAG, "POST %s %s", req->uri, body);
    return web_send_json(req, "501 Not Implemented", APP_WEB_STUB_JSON);
}

/* ------------------------------------------------------------------ *
 *  routes
 * ------------------------------------------------------------------ */

static esp_err_t web_index_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, (const char *)index_html_start, index_html_end - index_html_start - 1);
}

// TODO(backend): build the snapshot from track_list[] (app_synth_track.h):
// level, fm_metrix and, per operator, wave/level/coarse/env -- see the JSON
// shape in app_web.h. Needs app_synth in PRIV_REQUIRES.
static esp_err_t web_api_state_get(httpd_req_t *req)
{
    ESP_LOGI(TAG, "GET %s", req->uri);
    return web_send_json(req, "501 Not Implemented", APP_WEB_STUB_JSON);
}

// TODO(backend): { track, level } ->
// synth_app->command(synth_app, APP_SYNTH_SET_LEVEL, track, (double)level)
static esp_err_t web_api_track_post(httpd_req_t *req)
{
    return web_stub_post(req);
}

// TODO(backend): { track, op, wave?/level?/coarse?/env? } -> one command per
// field present: APP_SYNTH_SET_OP_WAVE / _OP_LEVEL / _OP_COARSE / _OP_ENV.
// The float arguments go through varargs as double.
static esp_err_t web_api_op_post(httpd_req_t *req)
{
    return web_stub_post(req);
}

// TODO(backend): { track, matrix[6] } -> for every (modulator i, carrier j)
// pair, APP_SYNTH_SET_ALGORITHM with flag = (matrix[i] >> j) & 1. Note the
// command's argument order is (track, carrier, modulator, flag).
static esp_err_t web_api_algorithm_post(httpd_req_t *req)
{
    return web_stub_post(req);
}

// TODO(backend): { track, note, velocity, on } -> app_event_bus_send(
// EVENT_MIDI_NOTE, &(app_event_midi_t){ ... }), so the audition button goes
// through the same path as the keyboard. Needs app_event_bus in PRIV_REQUIRES.
static esp_err_t web_api_note_post(httpd_req_t *req)
{
    return web_stub_post(req);
}

static const httpd_uri_t s_routes[] = {
    { .uri = "/",              .method = HTTP_GET,  .handler = web_index_get },
    { .uri = "/api/state",     .method = HTTP_GET,  .handler = web_api_state_get },
    { .uri = "/api/track",     .method = HTTP_POST, .handler = web_api_track_post },
    { .uri = "/api/op",        .method = HTTP_POST, .handler = web_api_op_post },
    { .uri = "/api/algorithm", .method = HTTP_POST, .handler = web_api_algorithm_post },
    { .uri = "/api/note",      .method = HTTP_POST, .handler = web_api_note_post },
};

#define APP_WEB_ROUTE_COUNT (sizeof(s_routes) / sizeof(s_routes[0]))

/* ------------------------------------------------------------------ *
 *  app_t plumbing
 * ------------------------------------------------------------------ */

app_t *app_web_app_init(void)
{
    app_web_app.base.state = APP_STATE_UNINIT;
    app_web_app.base.id = APP_ID_WEB;
    app_web_app.base.ctx = &app_web_app;
    app_web_app.base.init = app_web_app_init_cb;
    app_web_app.base.start = app_web_start;
    app_web_app.base.stop = app_web_stop;
    app_web_app.base.get_state = app_web_get_state;
    app_web_app.base.command = app_web_command;
    return &app_web_app.base;
}

static esp_err_t app_web_app_init_cb(app_t *app)
{
    if (app->state != APP_STATE_UNINIT) {
        return ESP_OK;
    }

    app_web_app.state.running = 0;
    app->state = APP_STATE_STOPED;
    return ESP_OK;
}

// Logs where the page can be reached, on whichever interface sys_wlan has up.
static void app_web_log_urls(void)
{
    sys_wlan_status_t status;
    if (sys_wlan_get_status(&status) != ESP_OK) {
        ESP_LOGW(TAG, "wlan not initialized -- nothing can reach the server yet");
        return;
    }

    bool reachable = false;
    if (status.sta_ip[0] != '\0') {
        ESP_LOGI(TAG, "http://%s:%d/", status.sta_ip, CONFIG_APP_WEB_PORT);
        reachable = true;
    }
    if (status.ap_active && status.ap_ip[0] != '\0') {
        ESP_LOGI(TAG, "http://%s:%d/ (softap \"%s\")", status.ap_ip, CONFIG_APP_WEB_PORT, status.ap_ssid);
        reachable = true;
    }
    if (!reachable) {
        ESP_LOGW(TAG, "no address yet -- connect with sys_wlan_connect() or start the softap");
    }
}

static esp_err_t app_web_start(app_t *app)
{
    if (app->state != APP_STATE_STOPED) {
        return ESP_ERR_INVALID_STATE;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = CONFIG_APP_WEB_PORT;
    config.stack_size = CONFIG_APP_WEB_TASK_STACK;
    config.max_open_sockets = CONFIG_APP_WEB_MAX_SOCKETS;
    config.max_uri_handlers = APP_WEB_ROUTE_COUNT + APP_WEB_WLAN_ROUTE_COUNT + 3;
    config.lru_purge_enable = true; // a browser tab left open must not wedge the server

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    for (size_t i = 0; i < APP_WEB_ROUTE_COUNT; i++) {
        err = httpd_register_uri_handler(s_server, &s_routes[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "registering %s failed: %s", s_routes[i].uri, esp_err_to_name(err));
            httpd_stop(s_server);
            s_server = NULL;
            return err;
        }
    }

    err = app_web_wlan_register(s_server);
    if (err != ESP_OK) {
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }
    app_web_app.state.running = 1;
    app->state = APP_STATE_RUNNING;
    ESP_LOGI(TAG, "web ui up on port %d (%u routes)", CONFIG_APP_WEB_PORT,
             (unsigned)(APP_WEB_ROUTE_COUNT + APP_WEB_WLAN_ROUTE_COUNT));
    app_web_log_urls();
    return ESP_OK;
}

static esp_err_t app_web_stop(app_t *app)
{
    if (app->state != APP_STATE_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = httpd_stop(s_server);
    s_server = NULL;
    app_web_app.state.running = 0;
    app->state = APP_STATE_STOPED;
    return err;
}

static size_t app_web_get_state(app_t *app, int state, void *out, uint8_t size)
{
    size_t bytes = 0;

    if (app->id != app_web_app.base.id) {
        return bytes;
    }
    if (state < 0 || state >= APP_WEB_STATE_MAX) {
        return bytes;
    }

    switch (state) {
    case APP_WEB_STATE_RUNNING:
        bytes = sizeof(app_web_app.state.running);
        if (size < bytes) {
            bytes = size;
        }
        memcpy(out, &app_web_app.state.running, bytes);
        break;

    default:
        break;
    }
    return bytes;
}

static esp_err_t app_web_command(app_t *app, int16_t command, ...)
{
    return ESP_ERR_NOT_SUPPORTED; // no commands yet
}
