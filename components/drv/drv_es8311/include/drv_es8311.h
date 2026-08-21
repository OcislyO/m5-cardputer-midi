#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DRV_ES8311_MIC_GAIN_0DB = 0,
    DRV_ES8311_MIC_GAIN_6DB,
    DRV_ES8311_MIC_GAIN_12DB,
    DRV_ES8311_MIC_GAIN_18DB,
    DRV_ES8311_MIC_GAIN_24DB,
    DRV_ES8311_MIC_GAIN_30DB,
    DRV_ES8311_MIC_GAIN_36DB,
    DRV_ES8311_MIC_GAIN_42DB,
} drv_es8311_mic_gain_t;

/**
 * @brief Bring up the ES8311 codec: registers it on the shared bsp_i2c bus,
 *        resets/powers it up, and configures it for 16-bit I2S (matching
 *        bsp_i2s) with its internal clock derived from the I2S BCLK line,
 *        since Cardputer-ADV does not wire a dedicated MCLK pin to the codec.
 *
 * @param sample_rate_hz Must be a rate the codec's clock-divider table
 *        supports at 16-bit/BCLK-derived clocking: 32000, 44100, 48000,
 *        64000, 88200 or 96000 Hz. Other rates return ESP_ERR_INVALID_ARG.
 */
esp_err_t drv_es8311_init(uint32_t sample_rate_hz);

/**
 * @brief Set DAC (speaker) output volume, 0-100%.
 */
esp_err_t drv_es8311_set_volume(uint8_t volume_pct);

/**
 * @brief Mute/unmute the DAC output.
 */
esp_err_t drv_es8311_set_mute(bool mute);

/**
 * @brief Enable the analog microphone input and set its PGA gain.
 */
esp_err_t drv_es8311_set_mic_gain(drv_es8311_mic_gain_t gain);

#ifdef __cplusplus
}
#endif
