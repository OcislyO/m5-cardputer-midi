#include "app_synth_priv.h"
#include "bsp_i2s.h"
#include "esp_log.h"
#include "freertos/task.h"

#define APP_SYNTH_RENDER_TASK_STACK 4096
#define APP_SYNTH_RENDER_TASK_PRIO  5 // audio-rate; must not be starved by UI/keyboard tasks

static const char *TAG = "app_synth_render";

static void app_synth_render_task(void *arg)
{
    static int16_t buf[APP_SYNTH_FRAME_SAMPLES];

    for (;;) {
        xSemaphoreTake(s_app_synth_voice_lock, portMAX_DELAY);

        for (size_t i = 0; i < APP_SYNTH_FRAME_SAMPLES; i++) {
            int32_t mix = 0;
            for (size_t v = 0; v < APP_SYNTH_MAX_VOICES; v++) {
                if (s_app_synth_voices[v].active) {
                    mix += app_synth_voice_process(&s_app_synth_voices[v]);
                }
            }

            mix >>= APP_SYNTH_MIX_SHIFT;
            if (mix > INT16_MAX) {
                mix = INT16_MAX;
            } else if (mix < INT16_MIN) {
                mix = INT16_MIN;
            }
            buf[i] = (int16_t)mix;
        }

        // A voice's ops all reach APP_SYNTH_ENV_IDLE together in practice
        // (note-off releases both at once), but check every op so a stray
        // mismatch can't leave a silently-idle voice permanently occupied.
        for (size_t v = 0; v < APP_SYNTH_MAX_VOICES; v++) {
            app_synth_voice_t *voice = &s_app_synth_voices[v];
            if (!voice->active) {
                continue;
            }
            bool all_idle = true;
            for (size_t i = 0; i < APP_SYNTH_OPS_PER_VOICE; i++) {
                if (voice->ops[i].env.stage != APP_SYNTH_ENV_IDLE) {
                    all_idle = false;
                    break;
                }
            }
            if (all_idle) {
                voice->active = false;
            }
        }

        xSemaphoreGive(s_app_synth_voice_lock);

        size_t written;
        esp_err_t err = bsp_i2s_write(buf, sizeof(buf), &written, APP_SYNTH_I2S_TIMEOUT_MS);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "bsp_i2s_write failed: %s", esp_err_to_name(err));
        }
    }
}

esp_err_t app_synth_render_start(void)
{
    static bool started = false;
    if (started) {
        return ESP_OK;
    }

    if (xTaskCreate(app_synth_render_task, "app_synth_render", APP_SYNTH_RENDER_TASK_STACK,
                     NULL, APP_SYNTH_RENDER_TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    started = true;
    return ESP_OK;
}
