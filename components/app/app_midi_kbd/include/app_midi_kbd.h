#pragma once

#include "esp_err.h"
#include "app.h"

#ifdef __cplusplus
extern "C" {
#endif


typedef struct app_midi_kbd_state_s {
    int8_t oct;
    uint8_t key_tab[2][14];  // 记录按下时的note，松开时优先关闭记录的音
} app_midi_kbd_state_t;

typedef enum {
    APP_MIDI_KBD_OCT = 0,
    APP_MIDI_KBD_STATE_MAX
} app_midi_kbd_state_id_t;

typedef enum {
    APP_MIDI_KBD_SET_OCT = 0,
    APP_MIDI_KBD_CMD_MAX
} app_midi_kbd_cmd_id_t;

typedef struct app_midi_kbd_cmd_s {
    app_midi_kbd_cmd_id_t cmd;
    void *arg;
} app_midi_kbd_cmd_t;

typedef struct app_midi_kbd_app_s {
    app_t base;
    app_midi_kbd_state_t state;
} app_midi_kbd_app_t;


app_t *app_midi_kbd_app_init(void);

#ifdef __cplusplus
}
#endif
