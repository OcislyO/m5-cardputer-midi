#include "app_seq.h"
#include "app_midi_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <stdbool.h>
#include <string.h>

#define APP_SEQ_TASK_STACK 3072
#define APP_SEQ_TASK_PRIO   3
#define APP_SEQ_POLL_MS     5

// How many pending MIDI events the shared record subscription can buffer
// between polls -- generous relative to how fast a human can hit keys.
#define APP_SEQ_RECORD_QUEUE_LEN 16

typedef enum {
    APP_SEQ_STATE_STOPPED = 0,
    APP_SEQ_STATE_PLAYING,
    APP_SEQ_STATE_RECORDING,
} app_seq_state_t;

typedef struct {
    bool active;
    uint8_t note;
    uint8_t velocity;
} app_seq_step_t;

typedef struct {
    app_seq_step_t steps[APP_SEQ_MAX_STEPS];
    uint8_t pos;

    TickType_t step_interval_ticks;
    TickType_t last_step_tick;

    app_seq_state_t state;

    // The note playback currently has sounding (held since it entered the
    // step, released when the next step is reached).
    bool note_held;
    uint8_t held_note;

    // Most recent NOTE_ON seen on the bus since this track last consumed
    // one at a step boundary -- each track tracks this independently since
    // tracks can run at different tempos.
    bool pending_valid;
    uint8_t pending_note;
    uint8_t pending_velocity;
} app_seq_t;

static app_seq_t s_seq[APP_SEQ_TRACK_COUNT];
static SemaphoreHandle_t s_lock;
static QueueHandle_t s_record_queue;
static bool s_initialized = false;

static void app_seq_task(void *arg);

static TickType_t app_seq_step_interval_ticks(uint16_t bpm)
{
    uint32_t step_ms = 60000u / bpm / APP_SEQ_STEPS_PER_BEAT;
    if (step_ms == 0) {
        step_ms = 1;
    }
    return pdMS_TO_TICKS(step_ms);
}

esp_err_t app_seq_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t err = app_midi_bus_init(); // idempotent -- don't assume anyone else has brought the bus up yet
    if (err != ESP_OK) {
        return err;
    }

    s_lock = xSemaphoreCreateMutex();
    if (! s_lock) {
        return ESP_ERR_NO_MEM;
    }

    s_record_queue = xQueueCreate(APP_SEQ_RECORD_QUEUE_LEN, sizeof(app_midi_event_t));
    if (! s_record_queue) {
        return ESP_ERR_NO_MEM;
    }

    err = app_midi_bus_subscribe(s_record_queue);
    if (err != ESP_OK) {
        return err;
    }

    memset(s_seq, 0, sizeof(s_seq));
    for (uint8_t t = 0; t < APP_SEQ_TRACK_COUNT; t++) {
        s_seq[t].step_interval_ticks = app_seq_step_interval_ticks(APP_SEQ_DEFAULT_BPM);
    }

    if (xTaskCreate(app_seq_task, "app_seq", APP_SEQ_TASK_STACK, NULL, APP_SEQ_TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    return ESP_OK;
}

esp_err_t app_seq_record(uint8_t track)
{
    if (track >= APP_SEQ_TRACK_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_seq[track].state = APP_SEQ_STATE_RECORDING;
    s_seq[track].last_step_tick = xTaskGetTickCount();
    xSemaphoreGive(s_lock);

    return ESP_OK;
}

esp_err_t app_seq_play(uint8_t track)
{
    if (track >= APP_SEQ_TRACK_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_seq[track].state = APP_SEQ_STATE_PLAYING;
    s_seq[track].last_step_tick = xTaskGetTickCount();
    xSemaphoreGive(s_lock);

    return ESP_OK;
}

esp_err_t app_seq_pause(uint8_t track)
{
    if (track >= APP_SEQ_TRACK_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);

    app_seq_t *seq = &s_seq[track];
    seq->state = APP_SEQ_STATE_STOPPED;
    if (seq->note_held) {
        app_midi_event_t off = {
            .type = APP_MIDI_EVENT_NOTE_OFF, .channel = track, .note = seq->held_note, .velocity = 0
        };
        app_midi_bus_send(&off);
        seq->note_held = false;
    }

    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t app_seq_set_tempo(uint8_t track, uint16_t bpm)
{
    if (track >= APP_SEQ_TRACK_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (bpm < APP_SEQ_MIN_BPM || bpm > APP_SEQ_MAX_BPM) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_seq[track].step_interval_ticks = app_seq_step_interval_ticks(bpm);
    xSemaphoreGive(s_lock);

    return ESP_OK;
}

static void app_seq_task(void *arg)
{
    app_midi_event_t evt;

    for (;;) {
        bool have_new_note = false;
        uint8_t new_note = 0, new_velocity = 0;

        // Drain whatever arrived since the last poll; last NOTE_ON wins if
        // several came in within one poll period.
        while (xQueueReceive(s_record_queue, &evt, 0) == pdTRUE) {
            if (evt.type == APP_MIDI_EVENT_NOTE_ON) {
                new_note = evt.note;
                new_velocity = evt.velocity;
                have_new_note = true;
            }
        }

        TickType_t now = xTaskGetTickCount();

        xSemaphoreTake(s_lock, portMAX_DELAY);
        for (uint8_t t = 0; t < APP_SEQ_TRACK_COUNT; t++) {
            app_seq_t *seq = &s_seq[t];

            if (have_new_note) {
                seq->pending_note = new_note;
                seq->pending_velocity = new_velocity;
                seq->pending_valid = true;
            }

            if (seq->state == APP_SEQ_STATE_STOPPED) {
                continue;
            }
            if ((TickType_t)(now - seq->last_step_tick) < seq->step_interval_ticks) {
                continue;
            }
            seq->last_step_tick = now;

            if (seq->note_held) {
                app_midi_event_t off = {
                    .type = APP_MIDI_EVENT_NOTE_OFF, .channel = t, .note = seq->held_note, .velocity = 0
                };
                app_midi_bus_send(&off);
                seq->note_held = false;
            }

            if (seq->state == APP_SEQ_STATE_RECORDING) {
                seq->steps[seq->pos].active = seq->pending_valid;
                if (seq->pending_valid) {
                    seq->steps[seq->pos].note = seq->pending_note;
                    seq->steps[seq->pos].velocity = seq->pending_velocity;
                }
            }
            seq->pending_valid = false;

            if (seq->steps[seq->pos].active) {
                app_midi_event_t on = {
                    .type = APP_MIDI_EVENT_NOTE_ON,
                    .channel = t,
                    .note = seq->steps[seq->pos].note,
                    .velocity = seq->steps[seq->pos].velocity,
                };
                app_midi_bus_send(&on);
                seq->held_note = seq->steps[seq->pos].note;
                seq->note_held = true;
            }

            seq->pos = (seq->pos + 1) % APP_SEQ_MAX_STEPS;
        }
        xSemaphoreGive(s_lock);

        vTaskDelay(pdMS_TO_TICKS(APP_SEQ_POLL_MS));
    }
}
