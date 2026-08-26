#include "app_synth.h"
#include "app_synth_priv.h"
#include "bsp_i2s.h"
#include "drv_es8311.h"
#include "esp_log.h"

static const char *TAG = "app_synth";
static bool s_initialized = false;

esp_err_t app_synth_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t err = bsp_i2s_init(APP_SYNTH_SAMPLE_RATE_HZ);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bsp_i2s_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = drv_es8311_init(APP_SYNTH_SAMPLE_RATE_HZ);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "drv_es8311_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = drv_es8311_set_mute(false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "drv_es8311_set_mute failed: %s", esp_err_to_name(err));
        return err;
    }

    err = drv_es8311_set_volume(APP_SYNTH_DEFAULT_VOLUME_PCT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "drv_es8311_set_volume failed: %s", esp_err_to_name(err));
        return err;
    }

    app_synth_wavetable_init();
    app_synth_voice_pool_init();

    err = app_synth_render_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "app_synth_render_start failed: %s", esp_err_to_name(err));
        return err;
    }

    err = app_synth_midi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "app_synth_midi_start failed: %s", esp_err_to_name(err));
        return err;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "synth init done (sr=%dHz, voices=%d)", APP_SYNTH_SAMPLE_RATE_HZ, APP_SYNTH_MAX_VOICES);
    return ESP_OK;
}

esp_err_t app_synth_note_on(uint8_t midi_note, uint8_t velocity)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    return app_synth_voice_note_on(midi_note, velocity);
}

esp_err_t app_synth_note_off(uint8_t midi_note)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    return app_synth_voice_note_off(midi_note);
}

esp_err_t app_synth_set_volume(uint8_t volume_pct)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    return drv_es8311_set_volume(volume_pct);
}
