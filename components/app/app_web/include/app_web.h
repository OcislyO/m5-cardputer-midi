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
 * Handlers answer 501 with {"error":"not implemented"} until the backend is
 * wired up; the page detects that and stays usable as a local preview.
 */

typedef enum {
    APP_WEB_STATE_RUNNING = 0, // uint8_t: is the HTTP server up
    APP_WEB_STATE_MAX
} app_web_state_id_t;

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

/** @brief Build the app_t for the web UI. Call init()/start() on it, as with the other apps. */
app_t *app_web_app_init(void);

#ifdef __cplusplus
}
#endif
