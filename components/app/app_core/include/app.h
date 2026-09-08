#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_ID_MIDI_KBD = 0,
    APP_ID_SYNTH,
    APP_ID_SEQ,
    APP_ID_UI,
    APP_ID_WEB,
    APP_ID_SHELL,
    APP_ID_MAX
} app_id_t;

typedef enum {
    APP_STATE_UNINIT = 0,
    APP_STATE_STOPED,
    APP_STATE_RUNNING,
} app_state_t;

typedef struct app_s
{
    app_id_t id;
    app_state_t state;

    void *ctx;

    esp_err_t (*init)(struct app_s *app);
    esp_err_t (*start)(struct app_s *app);
    esp_err_t (*stop)(struct app_s *app);

    esp_err_t (*command)(struct app_s *app, int16_t cmd, ...);

    size_t (*get_state)(struct app_s *app, int state, void *out, uint8_t size);
} app_t;

extern app_t *ui_app;
extern app_t *midi_kbd_app;
extern app_t *synth_app;
extern app_t *seq_app;

#ifdef __cplusplus
}
#endif