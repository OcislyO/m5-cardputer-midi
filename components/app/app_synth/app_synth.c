#include "app_synth.h"
#include "app_synth_track.h"
#include "app_synth_voice.h"
#include "app_event_bus.h"
#include "app_midi_kbd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "esp_log.h"

#define APP_SYNTH_ENGINE_TASK_STACK 3072
#define APP_SYNTH_ENGINE_TASK_PRIO  4

app_synth_app_t app_synth_app;

static int16_t s_wavetables[APP_SYNTH_WAVE_COUNT][APP_SYNTH_WT_SIZE];

app_synth_voice_t *note_map[MAX_TRACK_COUNT][128];  // 记录note->voice

sys_audio_frame_t app_synth_frame;

QueueHandle_t app_synth_midi_queue;

static esp_err_t app_synth_init(app_t *app);
static esp_err_t app_synth_start(app_t *app);
static esp_err_t app_synth_stop(app_t *app);
static size_t app_synth_get_state(struct app_s *app, int state, void *out, uint8_t size);
static esp_err_t app_synth_command(app_t *app, int16_t command, ...);

static void app_synth_wavetable_generate(void);

TaskHandle_t app_synth_engine_task_handle;
static void app_synth_engine_task(void *arg);

app_t *app_synth_app_init() {
    app_synth_app.base.state = APP_STATE_UNINIT;
    app_synth_app.base.id = APP_ID_SYNTH;
    app_synth_app.base.ctx = &app_synth_app;
    app_synth_app.base.init = app_synth_init;
    app_synth_app.base.start = app_synth_start;
    app_synth_app.base.stop = app_synth_stop;
    app_synth_app.base.get_state = app_synth_get_state;
    app_synth_app.base.command = app_synth_command;
    return &app_synth_app.base;
}

static esp_err_t app_synth_init(app_t *app) {
    esp_err_t err = ESP_OK;
    if (app->state != APP_STATE_UNINIT)
        goto ret;

    err = sys_audio_init();
    if (err)
        goto ret;

    app_synth_wavetable_generate();

    memset(note_map, 0, sizeof(note_map));

    app_synth_track_init();

    app_synth_midi_queue = xQueueCreate(10, sizeof(app_event_midi_t));
    if (! app_synth_midi_queue) {
        err =  ESP_ERR_NO_MEM;
        goto ret;
    }

    app->state = APP_STATE_STOPED;

    ret:
        return err;
}

static esp_err_t app_synth_start(app_t *app) {
    esp_err_t err = ESP_OK;
    if (app->state != APP_STATE_STOPED)
    {
        err = ESP_ERR_INVALID_STATE;
        goto ret;
    }

    if (xTaskCreate(app_synth_engine_task, "app_synth_engine", APP_SYNTH_ENGINE_TASK_STACK, NULL, APP_SYNTH_ENGINE_TASK_PRIO, &app_synth_engine_task_handle) != pdPASS) {
        err =  ESP_ERR_NO_MEM;
    }

    ret:
        return err;
}

static esp_err_t app_synth_stop(app_t *app) {
    esp_err_t err = ESP_OK;
    if (app->state != APP_STATE_RUNNING)
    {
        err = ESP_ERR_INVALID_STATE;
        goto ret;
    }

    vTaskDelete(app_synth_engine_task_handle);  // 暂时使用强制删除占位后期完善退出逻辑
    

    ret:
        return err;
}

static size_t app_synth_get_state(struct app_s *app, int state, void *out, uint8_t size) {
    size_t bytes = 0;

    if (app->id != app_synth_app.base.id)
        return bytes;

    if (state >= APP_SYNTH_STATE_MAX || state < 0)
        return bytes;

    switch (state)
    {
    case APP_SYNTH_FLAG:
        bytes = sizeof(app_synth_app.state.flag);
        if (size < bytes)
            bytes = size;
        memcpy(out, &app_synth_app.state.flag, bytes);
        break;
    
    default:
        break;
    }
    return bytes;
}

static esp_err_t app_synth_command(app_t *app, int16_t command, ...) {
    esp_err_t err = ESP_OK;
    va_list args;
    va_start(args, command);

    const app_synth_cmd_id_t cmd = (app_synth_cmd_id_t)command;

    switch (cmd)
    {
    case APP_SYNTH_SET_ENV:
        /* code */
        break;
    
    default:
        break;
    }

    va_end(args);

    return err;
}

static void app_synth_wavetable_generate(void) {
    for (size_t i = 0; i < APP_SYNTH_WT_SIZE; i++) {
        float phase = (float)i / APP_SYNTH_WT_SIZE; // 0..1 through one cycle

        s_wavetables[APP_SYNTH_WAVE_SINE][i] = (int16_t)(sinf(2.0f * (float)M_PI * phase) * INT16_MAX);

        // Symmetric triangle: rises 0..1..0..-1..0 across the cycle.
        float tri = phase < 0.5f ? (4.0f * phase - 1.0f) : (3.0f - 4.0f * phase);
        s_wavetables[APP_SYNTH_WAVE_TRIANGLE][i] = (int16_t)(tri * INT16_MAX);

        // Falling sawtooth: +1 at phase 0, ramps linearly down to -1.
        s_wavetables[APP_SYNTH_WAVE_SAW][i] = (int16_t)((1.0f - 2.0f * phase) * INT16_MAX);

        s_wavetables[APP_SYNTH_WAVE_SQUARE][i] = (phase < 0.5f) ? INT16_MAX : -INT16_MAX;
    }
}

int16_t app_synth_wavetable_sample(app_synth_wave_t wave, uint32_t phase)
{
    uint8_t index = (uint8_t)(phase >> APP_SYNTH_WT_FRAC_BITS);
    uint8_t frac = (uint8_t)(phase >> 16);  // 只取bit17~24补偿精度减少计算量

    const int16_t *table = s_wavetables[wave];
    int16_t a = table[index];
    int16_t b = table[(index + 1) & (APP_SYNTH_WT_SIZE - 1)];

    return a + (((b - a) * frac) >> 8);
}

static void app_synth_event_receive(void) {
    app_event_midi_t event;
    app_synth_voice_t *voice;
    float freq;

    while (xQueueReceive(app_synth_midi_queue, &event, 0))  // 收到数据继续接收，没收到返回pdfalse退出循环
    {
        
        if (event.channel >= MAX_TRACK_COUNT)
        {
            continue;
        }
        
        if (event.type == APP_MIDI_EVENT_NOTE_ON) {
            freq = 440.0f * powf(2.0f, (event.note - 69) / 12.0f);
            voice = app_synth_voice_on(event.channel, freq);
            if (! voice)
                continue;
            
            note_map[event.channel][event.note] = voice;

        } else if (event.type == APP_MIDI_EVENT_NOTE_OFF) {
            voice = note_map[event.channel][event.note];
            if (! voice)
                continue;

            app_synth_voice_off(voice);
            note_map[event.channel][event.note] = 0;
        }
    }
}

static void app_synth_engine_task(void *arg) {
    app_synth_voice_t *voice;
    int32_t sample[MAX_TRACK_COUNT];
    uint32_t tick = 0;

    app_event_bus_subscribe(EVENT_MIDI_NOTE, app_synth_midi_queue);

    for (;;)
    {
        app_synth_event_receive();
        tick = xTaskGetTickCount();
        for (size_t frame_index = 0; frame_index < SYS_AUDIO_FRAME_SAMPLES; frame_index++)
        {
            for (size_t voice_index = 0; voice_index < MAX_VOICE_COUNT; voice_index++)
            {
                voice = &app_synth_voice_pool[voice_index];

                if (voice->state == VOICE_STATE_FREE)
                    continue;

                sample[voice->from_track->midi_channel] += app_synth_voice_sample(voice);
            }

            int32_t temp = 0;
            for (size_t track_index = 0; track_index < MAX_TRACK_COUNT; track_index++)
            {
                temp += sample[track_index] * track_list[track_index].voice_level;
                sample[track_index] = 0;
            }

            if (temp > 0x7fff)
                temp = 0x7fff;

            if (temp < -32768)
                temp = -32768;

            app_synth_frame.samples[frame_index] = (int16_t)temp;
        }

        sys_audio_mix(app_synth_frame.samples);
        memset(app_synth_frame.samples, 0, 512);
        ESP_LOGI("eg", "%ld", xTaskGetTickCount() - tick);
        
        sys_audio_send_frame();
    }
}