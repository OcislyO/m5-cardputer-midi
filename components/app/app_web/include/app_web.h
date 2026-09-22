#pragma once

#include "app.h"
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Web UI for the FM synth: four tracks, six operators each.
 *
 * The page (www/index.html, embedded into the binary) talks to the device
 * through this REST contract. Ids are 0-based; levels are 0..1 floats and the
 * envelope rates are the per-sample increments app_synth_env_t stores, so the
 * values map 1:1 onto app_synth_track_set_*():
 *
 *   GET  /                 the page itself
 *   GET  /api/state        whole snapshot:
 *                          { "current": 0,
 *                            "tracks": [ { "level": 1.0,
 *                                          "matrix": [3,0,8,0,32,0],
 *                                          "ops": [ { "wave": 0, "level": 0.5,
 *                                                     "coarse": 2,
 *                                                     "env": {"a":0.001,"d":0.0001,
 *                                                             "s":0.5,"r":0.0001} }, ... ] }, ... ] }
 *                          ("current" is always 0: the device doesn't track
 *                          the page's track selection, which is browser UI
 *                          state read once at boot)
 *   POST /api/track        { "track": 0, "level": 0.8 }
 *   POST /api/op           { "track": 0, "op": 3, ... } -- only the changed
 *                          fields are sent: "wave" 0..3, "level" 0..1,
 *                          "coarse" 0..31 (0 means x0.5),
 *                          "env": { "a", "d", "s", "r" }
 *   POST /api/algorithm    { "track": 0, "matrix": [3,0,8,0,32,0] }, i.e. the
 *                          whole fm_metrix: bit j of entry i means "operator i
 *                          modulates operator j" (the engine only walks j >= i,
 *                          and i == j is feedback)
 *   POST /api/note         { "track": 0, "note": 60, "velocity": 100, "on": true }
 *                          audition button, publishes on the MIDI bus
 *
 * The page reads the snapshot once at boot and pushes every edit as a POST;
 * the backend routes both directions through synth_app (get_data to read,
 * command to write), so the page always edits the live synth state.
 */

typedef enum {
    APP_WEB_STATE_RUNNING = 0, // uint8_t: is the HTTP server up
    APP_WEB_STATE_MAX
} app_web_state_id_t;

typedef enum {
    // 能打开这个网页的地址，出参 buffer 至少 SYS_WLAN_IP_STR_LEN 字节。取的是
    // "站点优先、没连上就回退 SoftAP"之后的结果，和启动日志里打印的 URL 同一套
    // 判断，所以拼出来的 URL 一定打得开；两个接口都没起来就是空串。
    APP_WEB_DATA_IP = 0,
    APP_WEB_DATA_MAX
} app_web_data_id_t;

typedef enum {
    APP_WEB_CMD_MAX = 0 // no commands yet
} app_web_cmd_id_t;

typedef struct app_web_state_s {
    uint8_t running;
} app_web_state_t;

typedef struct app_web_app_s {
    app_t base;
    app_web_state_t state;
} app_web_app_t;

/* ------------------------------------------------------------------ *
 *  app_t 接口的包装：通过 web_app 走 get_data，调用方不用再手写枚举。
 * ------------------------------------------------------------------ */

/**
 * @brief 读能打开网页的地址，例如 "192.168.4.1"。
 *
 * @param out  调用方的 buffer，至少 SYS_WLAN_IP_STR_LEN 字节。
 * @param size 该 buffer 的容量；装不下就截断，串仍然以 '\0' 结尾。
 * @return 串长（不含结尾的 '\0'），0 表示还没有地址，或者 web_app 还没建起来
 *         / 参数不合法 —— 前一种情况 out 里是空串，一样可以直接显示。
 */
static inline size_t app_web_get_ip(char *out, uint8_t size) {
    if (web_app == NULL) {
        return 0;
    }
    return web_app->get_data(web_app, APP_WEB_DATA_IP, out, size);
}

/** @brief Build the app_t for the web UI. Call init() on it, as with the other apps. */
app_t *app_web_app_init(void);

#ifdef __cplusplus
}
#endif
