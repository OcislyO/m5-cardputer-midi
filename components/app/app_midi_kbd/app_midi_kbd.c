#include "app_midi_kbd.h"
#include "app_event_bus.h"
#include "sys_kbd.h"
#include "app_ui.h"
#include "esp_log.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <string.h>

#define APP_MIDI_KBD_TASK_STACK 3072
#define APP_MIDI_KBD_TASK_PRIO  4
#define APP_MIDI_KBD_VELOCITY   100
#define APP_MIDI_KBD_CHANNEL    0


#define APP_MIDI_KBD_ROW_LOWER       3
#define APP_MIDI_KBD_ROW_UPPER       2

static const char *TAG = "app_midi_kbd";

app_midi_kbd_app_t app_midi_kbd_app;

const uint8_t app_midi_kbd_map[2][14] = 
{
    {54,56,58,00,61,63,00,66,68,70,00,73,75,00},
    {53,55,57,59,60,62,64,65,67,69,71,72,74,76}
};

TaskHandle_t app_midi_kbd_task_handle;

static esp_err_t app_midi_kbd_init(app_t *app);
static esp_err_t app_midi_kbd_start(app_t *app);
static esp_err_t app_midi_kbd_stop(app_t *app);
static size_t app_midi_kbd_get_state(struct app_s *app, int state, void *out, uint8_t size);
static esp_err_t app_midi_kbd_command(app_t *app, int16_t command, ...);

static void app_midi_kbd_task(void *arg);

app_t *app_midi_kbd_app_init(void) {
    app_midi_kbd_app.base.state = APP_STATE_UNINIT;
    app_midi_kbd_app.base.id = APP_ID_MIDI_KBD;
    app_midi_kbd_app.base.ctx = &app_midi_kbd_app;
    app_midi_kbd_app.base.init = app_midi_kbd_init;
    app_midi_kbd_app.base.start = app_midi_kbd_start;
    app_midi_kbd_app.base.stop = app_midi_kbd_stop;
    app_midi_kbd_app.base.get_state = app_midi_kbd_get_state;
    app_midi_kbd_app.base.command = app_midi_kbd_command;

    app_midi_kbd_app.state.oct = 0;

    return &app_midi_kbd_app.base;
}

static esp_err_t app_midi_kbd_init(app_t *app) {
    if (app->state != APP_STATE_UNINIT) {
        return ESP_OK;
    }

    esp_err_t err = app_event_bus_init(); // idempotent -- don't assume anyone else has brought the bus up yet
    if (err != ESP_OK) {
        return err;
    }

    err = sys_kbd_init();
    if (err != ESP_OK) {
        return err;
    }

    
    app->state = APP_STATE_STOPED;
    return ESP_OK;
}
static esp_err_t app_midi_kbd_start(app_t *app) {
    esp_err_t err = ESP_OK;
    if (app->state != APP_STATE_STOPED) {
        goto ret;
    }

    if (xTaskCreate(app_midi_kbd_task, "app_midi_event", APP_MIDI_KBD_TASK_STACK, &app_midi_kbd_task_handle, APP_MIDI_KBD_TASK_PRIO, NULL) != pdPASS) {
        err = ESP_ERR_NO_MEM;
    }


    ret:
        return err;
}
static esp_err_t app_midi_kbd_stop(app_t *app) {
    esp_err_t err = ESP_OK;
    return err;
}
static size_t app_midi_kbd_get_state(struct app_s *app, int state, void *out, uint8_t size) {
    size_t bytes = 0;

    if (app->id != app_midi_kbd_app.base.id)
        return bytes;

    if (state >= APP_MIDI_KBD_STATE_MAX)
        return bytes;

    switch (state)
    {
    case APP_MIDI_KBD_OCT:
        bytes = sizeof(app_midi_kbd_app.state.oct);
        if (size < bytes)
            bytes = size;
        memcpy(out, &app_midi_kbd_app.state.oct, bytes);
        break;
    
    default:
        break;
    }

    return bytes;
}

static esp_err_t app_midi_kbd_command(app_t *app, int16_t command, ...) {
    esp_err_t err = ESP_OK;
    return err;
}

static void app_midi_kbd_task(void *arg)
{
    app_midi_kbd_state_t *state = &app_midi_kbd_app.state;

    for (;;) {
        sys_kbd_raw_event_t raw;
        if (sys_kbd_read_raw_event(&raw, portMAX_DELAY) != ESP_OK) {
            continue;
        }

        int base_note = state->oct * 12;
        int offset_note = 0;

        if (raw.pressed)
        {
            if (raw.row == APP_MIDI_KBD_ROW_LOWER || raw.row == APP_MIDI_KBD_ROW_UPPER) {
                offset_note = app_midi_kbd_map[raw.row - 2][raw.col];
                state->key_tab[raw.row - 2][raw.col] = offset_note + base_note;
                ui_app->command(ui_app, APP_UI_SET_KEY, (raw.row - 2) * 14 + raw.col, 0xfdea);
            } else if (raw.row == 0) {
                if (raw.col == 11) {
                    state->oct = state->oct - 1 < -3 ? -3 : state->oct - 1;
                }
                if (raw.col == 12) {
                    state->oct = state->oct + 1 > 3 ? 3 : state->oct + 1;
                }
            }
        } else {
            if (raw.row == APP_MIDI_KBD_ROW_LOWER || raw.row == APP_MIDI_KBD_ROW_UPPER) {
                if (state->key_tab[raw.row - 2][raw.col])
                {
                    offset_note = state->key_tab[raw.row - 2][raw.col];
                    base_note = 0;
                    state->key_tab[raw.row - 2][raw.col] = 0;
                } else {
                    offset_note = app_midi_kbd_map[raw.row - 2][raw.col];
                }
                ui_app->command(ui_app, APP_UI_SET_KEY, (raw.row - 2) * 14 + raw.col, raw.row - 2 ? 0xe73c : 0x39aa);
            }
        }

        /* 产生midi事件 */
        if (offset_note == 0)
            continue;

        app_event_midi_t evt = {
            .type = raw.pressed ? APP_MIDI_EVENT_NOTE_ON : APP_MIDI_EVENT_NOTE_OFF,
            .channel = APP_MIDI_KBD_CHANNEL,
            .note = (uint8_t)(base_note + offset_note),
            .velocity = raw.pressed ? APP_MIDI_KBD_VELOCITY : 0,
        };

        esp_err_t err = app_event_bus_send(EVENT_MIDI_NOTE, &evt);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "app_midi_bus_send failed: %s", esp_err_to_name(err));
        }
    }
}
