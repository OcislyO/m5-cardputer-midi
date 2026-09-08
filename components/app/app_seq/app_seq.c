#include "app_seq.h"
#include "app_event_bus.h"
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
    APP_SEQ_TRACK_STOPPED = 0,
    APP_SEQ_TRACK_PLAYING,
    APP_SEQ_TRACK_RECORDING,
} app_seq_track_state_t;

typedef struct {
    bool active;
    uint8_t note;
    uint8_t velocity;
} app_seq_step_t;

typedef struct {
    app_seq_step_t steps[APP_SEQ_MAX_STEPS];
    uint8_t pos;

    uint16_t bpm;
    TickType_t step_interval_ticks;
    TickType_t last_step_tick;

    app_seq_track_state_t state;

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
} app_seq_track_t;

app_seq_app_t app_seq_app;

static app_seq_track_t s_seq[APP_SEQ_TRACK_COUNT];
static SemaphoreHandle_t s_lock;
static QueueHandle_t s_record_queue;
static TaskHandle_t s_task_handle;

static esp_err_t app_seq_init(app_t *app);
static esp_err_t app_seq_start(app_t *app);
static esp_err_t app_seq_stop(app_t *app);
static size_t app_seq_get_state(struct app_s *app, int state, void *out, uint8_t size);
static esp_err_t app_seq_command(app_t *app, int16_t command, ...);

static void app_seq_task(void *arg);
static TickType_t app_seq_step_interval_ticks(uint16_t bpm);

static esp_err_t app_seq_track_play(uint8_t track);
static esp_err_t app_seq_track_record(uint8_t track);
static esp_err_t app_seq_track_pause(uint8_t track);
static esp_err_t app_seq_track_set_tempo(uint8_t track, uint16_t bpm);

app_t *app_seq_app_init(void)
{
    app_seq_app.base.state = APP_STATE_UNINIT;
    app_seq_app.base.id = APP_ID_SEQ;
    app_seq_app.base.ctx = &app_seq_app;
    app_seq_app.base.init = app_seq_init;
    app_seq_app.base.start = app_seq_start;
    app_seq_app.base.stop = app_seq_stop;
    app_seq_app.base.get_state = app_seq_get_state;
    app_seq_app.base.command = app_seq_command;
    return &app_seq_app.base;
}

static esp_err_t app_seq_init(app_t *app)
{
    esp_err_t err = ESP_OK;
    if (app->state != APP_STATE_UNINIT)
        goto ret;

    err = app_event_bus_init(); // idempotent -- don't assume anyone else has brought the bus up yet
    if (err)
        goto ret;

    s_lock = xSemaphoreCreateMutex();
    if (! s_lock) {
        err = ESP_ERR_NO_MEM;
        goto ret;
    }

    s_record_queue = xQueueCreate(APP_SEQ_RECORD_QUEUE_LEN, sizeof(app_event_midi_t));
    if (! s_record_queue) {
        err = ESP_ERR_NO_MEM;
        goto ret;
    }

    err = app_event_bus_subscribe(EVENT_MIDI_NOTE, s_record_queue);
    if (err)
        goto ret;

    memset(s_seq, 0, sizeof(s_seq));
    for (uint8_t t = 0; t < APP_SEQ_TRACK_COUNT; t++) {
        s_seq[t].bpm = APP_SEQ_DEFAULT_BPM;
        s_seq[t].step_interval_ticks = app_seq_step_interval_ticks(APP_SEQ_DEFAULT_BPM);
    }

    app->state = APP_STATE_STOPED;

    ret:
        return err;
}

static esp_err_t app_seq_start(app_t *app)
{
    esp_err_t err = ESP_OK;
    if (app->state != APP_STATE_STOPED) {
        err = ESP_ERR_INVALID_STATE;
        goto ret;
    }

    if (xTaskCreate(app_seq_task, "app_seq", APP_SEQ_TASK_STACK, NULL, APP_SEQ_TASK_PRIO, &s_task_handle) != pdPASS) {
        err = ESP_ERR_NO_MEM;
        goto ret;
    }

    app->state = APP_STATE_RUNNING;

    ret:
        return err;
}

static esp_err_t app_seq_stop(app_t *app)
{
    esp_err_t err = ESP_OK;
    if (app->state != APP_STATE_RUNNING) {
        err = ESP_ERR_INVALID_STATE;
        goto ret;
    }

    vTaskDelete(s_task_handle); // 暂时使用强制删除占位，后期完善退出逻辑
    s_task_handle = NULL;
    app->state = APP_STATE_STOPED;

    ret:
        return err;
}

static size_t app_seq_get_state(struct app_s *app, int state, void *out, uint8_t size)
{
    size_t bytes = 0;

    if (app->id != app_seq_app.base.id)
        return bytes;

    if (state < APP_SEQ_STATE_TRACK_0 || state > APP_SEQ_STATE_TRACK_3)
        return bytes;

    uint8_t track = (uint8_t)(state - APP_SEQ_STATE_TRACK_0);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    app_seq_state_t snapshot = {
        .track = track,
        .transport = (app_seq_transport_t)s_seq[track].state,
        .pos = s_seq[track].pos,
        .bpm = s_seq[track].bpm,
    };
    xSemaphoreGive(s_lock);

    bytes = sizeof(snapshot);
    if (size < bytes)
        bytes = size;
    memcpy(out, &snapshot, bytes);

    return bytes;
}

static esp_err_t app_seq_command(app_t *app, int16_t command, ...)
{
    esp_err_t err = ESP_OK;
    va_list args;
    va_start(args, command);

    const app_seq_cmd_id_t cmd = (app_seq_cmd_id_t)command;
    int track;

    switch (cmd)
    {
    case APP_SEQ_CMD_PLAY:
        track = va_arg(args, int);
        err = app_seq_track_play((uint8_t)track);
        break;
    case APP_SEQ_CMD_RECORD:
        track = va_arg(args, int);
        err = app_seq_track_record((uint8_t)track);
        break;
    case APP_SEQ_CMD_PAUSE:
        track = va_arg(args, int);
        err = app_seq_track_pause((uint8_t)track);
        break;
    case APP_SEQ_CMD_SET_TEMPO:
        track = va_arg(args, int);
        int bpm = va_arg(args, int);
        err = app_seq_track_set_tempo((uint8_t)track, (uint16_t)bpm);
        break;
    default:
        err = ESP_ERR_INVALID_ARG;
    }

    va_end(args);

    return err;
}

static TickType_t app_seq_step_interval_ticks(uint16_t bpm)
{
    uint32_t step_ms = 60000u / bpm / APP_SEQ_STEPS_PER_BEAT;
    if (step_ms == 0) {
        step_ms = 1;
    }
    return pdMS_TO_TICKS(step_ms);
}

static esp_err_t app_seq_track_record(uint8_t track)
{
    if (track >= APP_SEQ_TRACK_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_seq[track].state = APP_SEQ_TRACK_RECORDING;
    s_seq[track].last_step_tick = xTaskGetTickCount();
    xSemaphoreGive(s_lock);

    return ESP_OK;
}

static esp_err_t app_seq_track_play(uint8_t track)
{
    if (track >= APP_SEQ_TRACK_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_seq[track].state = APP_SEQ_TRACK_PLAYING;
    s_seq[track].last_step_tick = xTaskGetTickCount();
    xSemaphoreGive(s_lock);

    return ESP_OK;
}

static esp_err_t app_seq_track_pause(uint8_t track)
{
    if (track >= APP_SEQ_TRACK_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);

    app_seq_track_t *seq = &s_seq[track];
    seq->state = APP_SEQ_TRACK_STOPPED;
    if (seq->note_held) {
        app_event_midi_t off = {
            .type = APP_MIDI_EVENT_NOTE_OFF, .channel = track, .note = seq->held_note, .velocity = 0
        };
        app_event_bus_send(EVENT_MIDI_NOTE, &off);
        seq->note_held = false;
    }

    xSemaphoreGive(s_lock);
    return ESP_OK;
}

static esp_err_t app_seq_track_set_tempo(uint8_t track, uint16_t bpm)
{
    if (track >= APP_SEQ_TRACK_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (bpm < APP_SEQ_MIN_BPM || bpm > APP_SEQ_MAX_BPM) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_seq[track].bpm = bpm;
    s_seq[track].step_interval_ticks = app_seq_step_interval_ticks(bpm);
    xSemaphoreGive(s_lock);

    return ESP_OK;
}

static void app_seq_task(void *arg)
{
    app_event_midi_t evt;

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
            app_seq_track_t *seq = &s_seq[t];

            if (have_new_note) {
                seq->pending_note = new_note;
                seq->pending_velocity = new_velocity;
                seq->pending_valid = true;
            }

            if (seq->state == APP_SEQ_TRACK_STOPPED) {
                continue;
            }
            if ((TickType_t)(now - seq->last_step_tick) < seq->step_interval_ticks) {
                continue;
            }
            seq->last_step_tick = now;

            if (seq->note_held) {
                app_event_midi_t off = {
                    .type = APP_MIDI_EVENT_NOTE_OFF, .channel = t, .note = seq->held_note, .velocity = 0
                };
                app_event_bus_send(EVENT_MIDI_NOTE, &off);
                seq->note_held = false;
            }

            if (seq->state == APP_SEQ_TRACK_RECORDING) {
                seq->steps[seq->pos].active = seq->pending_valid;
                if (seq->pending_valid) {
                    seq->steps[seq->pos].note = seq->pending_note;
                    seq->steps[seq->pos].velocity = seq->pending_velocity;
                }
            }
            seq->pending_valid = false;

            if (seq->steps[seq->pos].active) {
                app_event_midi_t on = {
                    .type = APP_MIDI_EVENT_NOTE_ON,
                    .channel = t,
                    .note = seq->steps[seq->pos].note,
                    .velocity = seq->steps[seq->pos].velocity,
                };
                app_event_bus_send(EVENT_MIDI_NOTE, &on);
                seq->held_note = seq->steps[seq->pos].note;
                seq->note_held = true;
            }

            seq->pos = (seq->pos + 1) % APP_SEQ_MAX_STEPS;
        }
        xSemaphoreGive(s_lock);

        vTaskDelay(pdMS_TO_TICKS(APP_SEQ_POLL_MS));
    }
}
