#include "sys_audio.h"
#include "drv_es8311.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include <stddef.h>
#include <string.h>

static const char *TAG = "sys_audio";

static sys_audio_frame_t s_frame;
static const int16_t s_silence[SYS_AUDIO_FRAME_SAMPLES]; // zero-initialized, never written
static bool s_initialized = false;

esp_err_t sys_audio_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t err = drv_es8311_init(CONFIG_SYS_AUDIO_SAMPLE_RATE_HZ);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "drv_es8311_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = drv_es8311_set_volume(CONFIG_SYS_AUDIO_DEFAULT_VOLUME_PCT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "drv_es8311_set_volume failed: %s", esp_err_to_name(err));
        return err;
    }

    err = drv_es8311_set_mute(false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "drv_es8311_set_mute failed: %s", esp_err_to_name(err));
        return err;
    }

    s_initialized = true;

    ESP_LOGI(TAG, "audio init done (rate=%dHz volume=%d%% frame=%d samples)", CONFIG_SYS_AUDIO_SAMPLE_RATE_HZ,
             CONFIG_SYS_AUDIO_DEFAULT_VOLUME_PCT, SYS_AUDIO_FRAME_SAMPLES);
    return ESP_OK;
}

void sys_audio_mix(const int16_t *src)
{
    if (!s_frame.valid) {
        memcpy(s_frame.samples, src, sizeof(s_frame.samples));
        s_frame.valid = true;
        return;
    }

    for (size_t i = 0; i < SYS_AUDIO_FRAME_SAMPLES; i++) {
        int32_t sum = (int32_t)s_frame.samples[i] + (int32_t)src[i];
        if (sum > INT16_MAX) {
            sum = INT16_MAX;
        } else if (sum < INT16_MIN) {
            sum = INT16_MIN;
        }
        s_frame.samples[i] = (int16_t)sum;
    }
}

esp_err_t sys_audio_send_frame(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const int16_t *out = s_frame.valid ? s_frame.samples : s_silence;
    esp_err_t err = drv_es8311_write(out, SYS_AUDIO_FRAME_SAMPLES);
    s_frame.valid = false;
    return err;
}

esp_err_t sys_audio_set_volume(uint8_t volume_pct)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    return drv_es8311_set_volume(volume_pct);
}

esp_err_t sys_audio_set_mute(bool mute)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    return drv_es8311_set_mute(mute);
}
