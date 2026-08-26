#include "app_synth_priv.h"
#include "app_synth.h"
#include "app_midi_bus.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <stdbool.h>

#define APP_SYNTH_MIDI_QUEUE_LEN  16
#define APP_SYNTH_MIDI_TASK_STACK 2048
#define APP_SYNTH_MIDI_TASK_PRIO  4

static QueueHandle_t s_queue;

static void app_synth_midi_task(void *arg)
{
    for (;;) {
        app_midi_event_t evt;
        if (xQueueReceive(s_queue, &evt, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (evt.type) {
        case APP_MIDI_EVENT_NOTE_ON:
            app_synth_note_on(evt.note, evt.velocity);
            break;
        case APP_MIDI_EVENT_NOTE_OFF:
            app_synth_note_off(evt.note);
            break;
        }
    }
}

esp_err_t app_synth_midi_start(void)
{
    static bool started = false;
    if (started) {
        return ESP_OK;
    }

    esp_err_t err = app_midi_bus_init(); // idempotent -- don't assume anyone else has brought the bus up yet
    if (err != ESP_OK) {
        return err;
    }

    s_queue = xQueueCreate(APP_SYNTH_MIDI_QUEUE_LEN, sizeof(app_midi_event_t));
    if (s_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    err = app_midi_bus_subscribe(s_queue);
    if (err != ESP_OK) {
        return err;
    }

    if (xTaskCreate(app_synth_midi_task, "app_synth_midi", APP_SYNTH_MIDI_TASK_STACK, NULL,
                     APP_SYNTH_MIDI_TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    started = true;
    return ESP_OK;
}
