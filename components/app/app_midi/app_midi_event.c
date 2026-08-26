#include "app_midi_event.h"
#include "app_midi_bus.h"
#include "sys_kbd.h"
#include "esp_log.h"
#include "freertos/task.h"
#include <stdbool.h>

#define APP_MIDI_EVENT_TASK_STACK 3072
#define APP_MIDI_EVENT_TASK_PRIO  4
#define APP_MIDI_EVENT_VELOCITY   100
#define APP_MIDI_EVENT_CHANNEL    0

// Bottom two physical rows (sys_kbd's row numbering, see sys_kbd_event_t.row)
// become piano keys: row 3 (the "z" row) is the lower octave-and-change, row
// 2 (the "a" row) one octave above it, both chromatic left to right. Every
// other row is ignored -- not a piano key. Row 2 col 1 (CAPSLOCK) and row 3
// col 1 (OPT) never reach here at all: sys_kbd reserves both outright for
// the raw/typing mode-switch chord, so those two note slots are simply
// silent gaps in an otherwise continuous run.
#define APP_MIDI_EVENT_ROW_LOWER       3
#define APP_MIDI_EVENT_ROW_UPPER       2
#define APP_MIDI_EVENT_BASE_NOTE_LOWER 48 // C3
#define APP_MIDI_EVENT_BASE_NOTE_UPPER 60 // C4

static const char *TAG = "app_midi_event";

uint8_t app_midi_kbd_map[2][14] = 
{
    {54,56,58,00,61,63,00,66,68,70,00,73,75,00},
    {53,55,57,59,60,62,64,65,67,69,71,72,74,76}
};

static void app_midi_event_task(void *arg)
{
    for (;;) {
        sys_kbd_raw_event_t raw;
        if (sys_kbd_read_raw_event(&raw, portMAX_DELAY) != ESP_OK) {
            continue;
        }

        int base_note = 0;
        int offset_note;
        if (raw.row == APP_MIDI_EVENT_ROW_LOWER) {
            offset_note = app_midi_kbd_map[1][raw.col];
        } else if (raw.row == APP_MIDI_EVENT_ROW_UPPER) {
            offset_note = app_midi_kbd_map[0][raw.col];
        } else {
            continue; // not a piano row
        }

        if (offset_note == 0)
            continue;

        app_midi_event_t evt = {
            .type = raw.pressed ? APP_MIDI_EVENT_NOTE_ON : APP_MIDI_EVENT_NOTE_OFF,
            .channel = APP_MIDI_EVENT_CHANNEL,
            .note = (uint8_t)(base_note + offset_note),
            .velocity = raw.pressed ? APP_MIDI_EVENT_VELOCITY : 0,
        };

        esp_err_t err = app_midi_bus_send(&evt);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "app_midi_bus_send failed: %s", esp_err_to_name(err));
        }
    }
}

esp_err_t app_midi_event_start(void)
{
    static bool started = false;
    if (started) {
        return ESP_OK;
    }

    esp_err_t err = app_midi_bus_init(); // idempotent -- don't assume anyone else has brought the bus up yet
    if (err != ESP_OK) {
        return err;
    }

    err = sys_kbd_init();
    if (err != ESP_OK) {
        return err;
    }

    if (xTaskCreate(app_midi_event_task, "app_midi_event", APP_MIDI_EVENT_TASK_STACK, NULL,
                     APP_MIDI_EVENT_TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    started = true;
    return ESP_OK;
}
