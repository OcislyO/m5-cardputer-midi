#include "app_web.h"
#include "app_web_wlan.h"
#include "app_synth.h"
#include "app_synth_track.h"
#include "app_event_bus.h"
#include "app_ui.h"
#include "sys_wlan.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "app_web";

// Largest request body the API accepts. Every call the page makes is a handful
// of numbers; the biggest is /api/op with a full envelope.
#define APP_WEB_BODY_MAX 256

// www/index.html, embedded by EMBED_FILES (which appends a NUL byte).
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

app_web_app_t app_web_app;
static httpd_handle_t s_server;

static esp_err_t app_web_app_init_cb(app_t *app);
static esp_err_t app_web_uninit(app_t *app);
static size_t app_web_get_state(app_t *app, int16_t state, void *out, uint8_t size);
static size_t app_web_get_data(struct app_s *app, int16_t data, void *out, uint8_t size, ...);
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

// Reads the whole request body into `buf`; the JSON handlers parse it from there.
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

// Reads the POST body and parses it. On failure the error response is already
// sent and NULL is returned -- the caller then just returns ESP_OK.
static cJSON *web_parse_body(httpd_req_t *req, char *buf, size_t size)
{
    esp_err_t err = web_recv_body(req, buf, size);
    if (err == ESP_ERR_INVALID_SIZE) {
        web_send_json(req, "413 Payload Too Large", "{\"error\":\"body too large\"}");
        return NULL;
    }
    if (err != ESP_OK) {
        web_send_json(req, "400 Bad Request", "{\"error\":\"read failed\"}");
        return NULL;
    }

    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        web_send_json(req, "400 Bad Request", "{\"error\":\"invalid json\"}");
        return NULL;
    }
    return root;
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

/* ------------------------------------------------------------------ *
 *  synth 参数读写：读走 synth_app->get_data()，写走 synth_app->command()，
 *  页面改动直接落在真实音色上，而不是只存在浏览器里。
 * ------------------------------------------------------------------ */

// GET /api/state -- 整个音色快照。"current" 恒为 0：设备不追踪页面选中了哪条
// 轨道（那只是浏览器里的 UI 状态），页面只在启动时读它一次。
static esp_err_t web_api_state_get(httpd_req_t *req)
{
    ESP_LOGI(TAG, "GET %s", req->uri);

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return web_send_json(req, "500 Internal Server Error", "{\"error\":\"out of memory\"}");
    }
    cJSON_AddNumberToObject(root, "current", 0);
    cJSON *tracks = cJSON_AddArrayToObject(root, "tracks");

    for (uint8_t t = 0; t < MAX_TRACK_COUNT; t++) {
        app_synth_track_t tr_snap;

        // 一次 get_data 拿走整条轨道的结构（APP_SYNTH_DATA_TRACK），字段直接
        // 按 app_synth_track_t 解释，不用逐参数读
        if (app_synth_get_track(t, &tr_snap, sizeof(tr_snap)) != sizeof(tr_snap)) {
            ESP_LOGE(TAG, "synth get_data failed for track %u", (unsigned)t);
            cJSON_Delete(root);
            return web_send_json(req, "500 Internal Server Error", "{\"error\":\"state read failed\"}");
        }

        cJSON *tr = cJSON_CreateObject();
        if (tr == NULL) {
            cJSON_Delete(root);
            return web_send_json(req, "500 Internal Server Error", "{\"error\":\"out of memory\"}");
        }
        cJSON_AddNumberToObject(tr, "level", tr_snap.voice_level);
        cJSON *mtx = cJSON_AddArrayToObject(tr, "matrix");
        cJSON *ops = cJSON_AddArrayToObject(tr, "ops");
        for (size_t i = 0; i < MAX_OPERATOR_COUNT; i++) {
            cJSON_AddItemToArray(mtx, cJSON_CreateNumber(tr_snap.fm_metrix[i]));

            cJSON *op = cJSON_CreateObject();
            cJSON *env = cJSON_CreateObject();
            if (op == NULL || env == NULL) {
                cJSON_Delete(root);
                return web_send_json(req, "500 Internal Server Error", "{\"error\":\"out of memory\"}");
            }
            cJSON_AddNumberToObject(op, "wave", tr_snap.op_wave[i]);
            cJSON_AddNumberToObject(op, "level", tr_snap.op_level[i]);
            cJSON_AddNumberToObject(op, "coarse", tr_snap.op_coarse[i]);
            cJSON_AddNumberToObject(env, "a", tr_snap.op_env[i].attack);
            cJSON_AddNumberToObject(env, "d", tr_snap.op_env[i].decay);
            cJSON_AddNumberToObject(env, "s", tr_snap.op_env[i].sustain);
            cJSON_AddNumberToObject(env, "r", tr_snap.op_env[i].release);
            cJSON_AddItemToObject(op, "env", env);
            cJSON_AddItemToArray(ops, op);
        }
        cJSON_AddItemToArray(tracks, tr);
    }

    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (text == NULL) {
        return web_send_json(req, "500 Internal Server Error", "{\"error\":\"out of memory\"}");
    }
    esp_err_t err = web_send_json(req, "200 OK", text);
    free(text);
    return err;
}

// POST /api/track -- { "track": 0, "level": 0.8 }
static esp_err_t web_api_track_post(httpd_req_t *req)
{
    char body[APP_WEB_BODY_MAX];
    cJSON *root = web_parse_body(req, body, sizeof(body));
    if (root == NULL) {
        return ESP_OK;
    }

    const cJSON *track = cJSON_GetObjectItemCaseSensitive(root, "track");
    const cJSON *level = cJSON_GetObjectItemCaseSensitive(root, "level");
    bool bad = track == NULL || !cJSON_IsNumber(track) ||
               track->valueint < 0 || track->valueint >= MAX_TRACK_COUNT ||
               level == NULL || !cJSON_IsNumber(level) ||
               level->valuedouble < 0 || level->valuedouble > 1;
    if (bad) {
        cJSON_Delete(root);
        return web_send_json(req, "400 Bad Request", "{\"error\":\"invalid track or level\"}");
    }

    esp_err_t err = app_synth_set_track_level(track->valueint, (float)level->valuedouble);
    cJSON_Delete(root);
    if (err != ESP_OK) {
        return web_send_json(req, "500 Internal Server Error", "{\"error\":\"command failed\"}");
    }
    return web_send_json(req, "200 OK", "{\"ok\":true}");
}

// POST /api/op -- { "track": 0, "op": 3, ... } 只发改动过的字段：
// "wave" 0..3、"level" 0..1、"coarse" 0..31、"env" 的 {a,d,s,r} 四键齐发。
// 每个出现的字段各发一条命令。
static esp_err_t web_api_op_post(httpd_req_t *req)
{
    char body[APP_WEB_BODY_MAX];
    cJSON *root = web_parse_body(req, body, sizeof(body));
    if (root == NULL) {
        return ESP_OK;
    }

    const cJSON *track = cJSON_GetObjectItemCaseSensitive(root, "track");
    const cJSON *op = cJSON_GetObjectItemCaseSensitive(root, "op");
    const cJSON *wave = cJSON_GetObjectItemCaseSensitive(root, "wave");
    const cJSON *level = cJSON_GetObjectItemCaseSensitive(root, "level");
    const cJSON *coarse = cJSON_GetObjectItemCaseSensitive(root, "coarse");
    const cJSON *env = cJSON_GetObjectItemCaseSensitive(root, "env");
    const cJSON *env_a = NULL, *env_d = NULL, *env_s = NULL, *env_r = NULL;

    bool bad = track == NULL || !cJSON_IsNumber(track) ||
               track->valueint < 0 || track->valueint >= MAX_TRACK_COUNT ||
               op == NULL || !cJSON_IsNumber(op) ||
               op->valueint < 0 || op->valueint >= MAX_OPERATOR_COUNT;
    if (!bad && wave != NULL) {
        bad = !cJSON_IsNumber(wave) || wave->valueint < 0 || wave->valueint >= APP_SYNTH_WAVE_COUNT;
    }
    if (!bad && level != NULL) {
        bad = !cJSON_IsNumber(level) || level->valuedouble < 0 || level->valuedouble > 1;
    }
    if (!bad && coarse != NULL) {
        bad = !cJSON_IsNumber(coarse) || coarse->valueint < 0 || coarse->valueint > 31;
    }
    if (!bad && env != NULL) {
        bad = !cJSON_IsObject(env);
        env_a = cJSON_GetObjectItemCaseSensitive(env, "a");
        env_d = cJSON_GetObjectItemCaseSensitive(env, "d");
        env_s = cJSON_GetObjectItemCaseSensitive(env, "s");
        env_r = cJSON_GetObjectItemCaseSensitive(env, "r");
        if (!bad) {
            bad = env_a == NULL || !cJSON_IsNumber(env_a) ||
                  env_d == NULL || !cJSON_IsNumber(env_d) ||
                  env_s == NULL || !cJSON_IsNumber(env_s) ||
                  env_r == NULL || !cJSON_IsNumber(env_r);
        }
    }
    if (bad) {
        cJSON_Delete(root);
        return web_send_json(req, "400 Bad Request", "{\"error\":\"invalid op params\"}");
    }

    int t = (int)track->valueint;
    int o = (int)op->valueint;
    esp_err_t err = ESP_OK;
    if (err == ESP_OK && wave != NULL) {
        err = app_synth_set_op_wave(t, o, wave->valueint);
    }
    if (err == ESP_OK && level != NULL) {
        err = app_synth_set_op_level(t, o, (float)level->valuedouble);
    }
    if (err == ESP_OK && coarse != NULL) {
        err = app_synth_set_op_coarse(t, o, coarse->valueint);
    }
    if (err == ESP_OK && env != NULL) {
        err = app_synth_set_op_env(t, o, (float)env_a->valuedouble, (float)env_d->valuedouble,
                                   (float)env_s->valuedouble, (float)env_r->valuedouble);
    }
    cJSON_Delete(root);

    if (err != ESP_OK) {
        return web_send_json(req, "500 Internal Server Error", "{\"error\":\"command failed\"}");
    }
    return web_send_json(req, "200 OK", "{\"ok\":true}");
}

// POST /api/algorithm -- { "track": 0, "matrix": [3,0,8,0,32,0] } 整张
// fm_metrix：第 i 项的 bit j = "算子 i 调制算子 j"。引擎只走 j >= i（页面
// 左下角本来就不可编辑），所以只下发这些格子；命令的参数顺序是
// (track, carrier, modulator, flag)。
static esp_err_t web_api_algorithm_post(httpd_req_t *req)
{
    char body[APP_WEB_BODY_MAX];
    cJSON *root = web_parse_body(req, body, sizeof(body));
    if (root == NULL) {
        return ESP_OK;
    }

    const cJSON *track = cJSON_GetObjectItemCaseSensitive(root, "track");
    const cJSON *matrix = cJSON_GetObjectItemCaseSensitive(root, "matrix");
    bool bad = track == NULL || !cJSON_IsNumber(track) ||
               track->valueint < 0 || track->valueint >= MAX_TRACK_COUNT ||
               matrix == NULL || !cJSON_IsArray(matrix) ||
               cJSON_GetArraySize(matrix) != MAX_OPERATOR_COUNT;
    for (int i = 0; !bad && i < MAX_OPERATOR_COUNT; i++) {
        const cJSON *row = cJSON_GetArrayItem(matrix, i);
        bad = row == NULL || !cJSON_IsNumber(row) ||
              row->valueint < 0 || row->valueint > 0xff;
    }
    if (bad) {
        cJSON_Delete(root);
        return web_send_json(req, "400 Bad Request", "{\"error\":\"invalid matrix\"}");
    }

    esp_err_t err = ESP_OK;
    for (int i = 0; err == ESP_OK && i < MAX_OPERATOR_COUNT; i++) {
        uint8_t row = (uint8_t)cJSON_GetArrayItem(matrix, i)->valueint;
        for (int j = i; j < MAX_OPERATOR_COUNT; j++) {
            err = app_synth_set_algorithm(track->valueint, j, i, (row >> j) & 1);
        }
    }
    cJSON_Delete(root);

    if (err != ESP_OK) {
        return web_send_json(req, "500 Internal Server Error", "{\"error\":\"command failed\"}");
    }
    return web_send_json(req, "200 OK", "{\"ok\":true}");
}

// POST /api/note -- { "track": 0, "note": 60, "velocity": 100, "on": true }
// 试听按钮：发到 MIDI 总线，走和键盘完全相同的路径。
static esp_err_t web_api_note_post(httpd_req_t *req)
{
    char body[APP_WEB_BODY_MAX];
    cJSON *root = web_parse_body(req, body, sizeof(body));
    if (root == NULL) {
        return ESP_OK;
    }

    const cJSON *track = cJSON_GetObjectItemCaseSensitive(root, "track");
    const cJSON *note = cJSON_GetObjectItemCaseSensitive(root, "note");
    const cJSON *velocity = cJSON_GetObjectItemCaseSensitive(root, "velocity");
    const cJSON *on = cJSON_GetObjectItemCaseSensitive(root, "on");
    bool bad = track == NULL || !cJSON_IsNumber(track) ||
               track->valueint < 0 || track->valueint >= MAX_TRACK_COUNT ||
               note == NULL || !cJSON_IsNumber(note) ||
               note->valueint < 0 || note->valueint > 127 ||
               velocity == NULL || !cJSON_IsNumber(velocity) ||
               velocity->valueint < 0 || velocity->valueint > 127 ||
               on == NULL || !cJSON_IsBool(on);
    if (bad) {
        cJSON_Delete(root);
        return web_send_json(req, "400 Bad Request", "{\"error\":\"invalid note\"}");
    }

    app_event_midi_t evt = {
        .type = cJSON_IsTrue(on) ? APP_MIDI_EVENT_NOTE_ON : APP_MIDI_EVENT_NOTE_OFF,
        .channel = (uint8_t)track->valueint,
        .note = (uint8_t)note->valueint,
        .velocity = (uint8_t)velocity->valueint,
    };
    esp_err_t err = app_event_bus_send(EVENT_MIDI_NOTE, &evt);
    cJSON_Delete(root);

    if (err != ESP_OK) {
        return web_send_json(req, "500 Internal Server Error", "{\"error\":\"bus send failed\"}");
    }
    return web_send_json(req, "200 OK", "{\"ok\":true}");
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
    app_web_app.base.uninit = app_web_uninit;
    app_web_app.base.get_state = app_web_get_state;
    app_web_app.base.get_data = app_web_get_data;
    app_web_app.base.command = app_web_command;
    return &app_web_app.base;
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

static esp_err_t app_web_app_init_cb(app_t *app)
{
    if (app->state != APP_STATE_UNINIT) {
        return ESP_OK;
    }

    app_web_app.state.running = 0;

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

    ui_app->command(ui_app, APP_UI_CMD_UPDATE_IP);

    return ESP_OK;
}

static esp_err_t app_web_uninit(app_t *app)
{
    if (app->state != APP_STATE_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = httpd_stop(s_server);
    s_server = NULL;
    app_web_app.state.running = 0;
    app->state = APP_STATE_UNINIT;
    return err;
}

static size_t app_web_get_state(app_t *app, int16_t state, void *out, uint8_t size)
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

// get_data：目前只有"网页的访问地址"这一项。地址是现读的，每次进来问一次
// sys_wlan，不缓存 —— 缓存就得跟着 WLAN 事件更新，为此把 sys_wlan 的事件回调
// 接进来（跑在系统事件任务里）不值当。代价是 sys_wlan_get_status() 会去问
// WiFi 驱动要 RSSI 和 AP 客户端列表，比一次内存拷贝贵，所以按秒级频率读就行，
// 别放进每帧刷新的循环。
static size_t app_web_get_data(struct app_s *app, int16_t data, void *out, uint8_t size, ...)
{
    size_t len = 0;
    sys_wlan_status_t status;
    const char *ip;

    if (app->id != app_web_app.base.id || out == NULL) {
        return 0;
    }
    if (data < 0 || data >= APP_WEB_DATA_MAX || size == 0) {
        return 0;
    }

    switch (data)
    {
    case APP_WEB_DATA_IP:
        // 和 app_web_log_urls() 同一套判断：站点优先，站点没连上才回退 SoftAP
        // 的地址，两个都没有就是空串。
        if (sys_wlan_get_status(&status) != ESP_OK) {
            status.sta_ip[0] = '\0'; // WiFi 还没初始化，按"还没有地址"报
            status.ap_ip[0] = '\0';
        }
        ip = status.sta_ip[0] != '\0' ? status.sta_ip : status.ap_ip;

        // 这一项回报的是串长而不是"拷了多少字节"：字符串上用 sizeof 比对本来
        // 就没意义，返回串长调用方才能用 == 0 判断"还没地址"。串一定以 '\0'
        // 结尾，size 装不下就截断。
        len = strlen(ip);
        if (len > (size_t)size - 1) {
            len = (size_t)size - 1;
        }
        memcpy(out, ip, len);
        ((char *)out)[len] = '\0';
        break;

    default:
        break;
    }

    return len;
}

static esp_err_t app_web_command(app_t *app, int16_t command, ...)
{
    return ESP_ERR_NOT_SUPPORTED; // no commands yet
}
