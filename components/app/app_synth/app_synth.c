#include "app_synth.h"
#include "app_synth_voice.h"
#include "app_synth_track.h"
#include "app_midi_bus.h"
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>


#define APP_SYNTH_TASK_STACK 3072
#define APP_SYNTH_TASK_PRIO  5

#define APP_SYNTH_ENGINE_TASK_STACK 3072
#define APP_SYNTH_ENGINE_TASK_PRIO  4


static int16_t s_wavetables[APP_SYNTH_WAVE_COUNT][APP_SYNTH_WT_SIZE];
static bool s_initialized = false;

app_synth_voice_t *note_map[MAX_TRACK_COUNT][128];  // 记录note->voice

sys_audio_frame_t app_synth_frame;

QueueHandle_t app_synth_midi_queue;

static void app_synth_task(void *arg);
static void app_synth_engine_task(void *arg);

esp_err_t app_synth_init(void)
{
    esp_err_t ret = ESP_OK;
    if (s_initialized)
        return ret;

    ret = sys_audio_init();
    if (ret)
        return ret;

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

    memset(note_map, 0, sizeof(note_map));

    app_synth_track_init();

    app_synth_midi_queue = xQueueCreate(10, sizeof(app_midi_event_t));
    if (! app_synth_midi_queue) {
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(app_synth_task, "app_synth", APP_SYNTH_TASK_STACK, NULL,
                     APP_SYNTH_TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(app_synth_engine_task, "app_synth_engine", APP_SYNTH_ENGINE_TASK_STACK, NULL,
                     APP_SYNTH_ENGINE_TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    return ESP_OK;
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

static void app_synth_task(void *arg) {
    app_midi_event_t event;
    app_synth_voice_t *voice;
    float freq;

    app_midi_bus_subscribe(app_synth_midi_queue);

    for (;;)
    {
        xQueueReceive(app_synth_midi_queue, &event, portMAX_DELAY);
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
    int16_t sample;
    for (;;)
    {
        for (size_t i = 0; i < SYS_AUDIO_FRAME_SAMPLES; i++)
        {
            for (size_t j = 0; j < MAX_VOICE_COUNT; j++)
            {
                voice = &app_synth_voice_pool[j];

                /*env更新*/
                if (voice->env_state == env_state_idle)
                    continue;
                switch (voice->env_state)
                {
                case env_state_attack:
                    voice->level += voice->from_track->env.attack;
                    if (voice->level > 1)
                        voice->env_state = env_state_decay;
                    break;
                    
                case env_state_decay:
                    voice->level -= voice->from_track->env.decay;
                    if (voice->level <= voice->from_track->env.sustain) {
                        voice->level = voice->from_track->env.sustain;
                        voice->env_state = env_state_sustain;
                    }
                        
                    break;
                    
                case env_state_sustain:
                    /* code */
                    break;
                    
                case env_state_release:
                    voice->level -= voice->from_track->env.release;
                    if (voice->level <= 0) {
                        voice->level = 0;
                        voice->env_state = env_state_idle;  // 标记为以释放
                        voice->from_track->voice_count -= 1;
                    }
                    break;
                
                default:
                    break;
                }
                
                if (voice->level)
                {
                    sample += app_synth_voice_sample(voice) * voice->level;
                }
            }
            int32_t temp = app_synth_frame.samples[i] + sample;
            sample = 0;
            if (temp > 0x7fff)
                temp = 0x7fff;
                
            if (temp < -32768)
                temp = -32768;
            app_synth_frame.samples[i] = (uint16_t)temp;
        }
        sys_audio_mix(s_wavetables[0]);
        // sys_audio_mix(app_synth_frame.samples);
        memset(app_synth_frame.samples, 0, 512);
        sys_audio_send_frame();
    }
}