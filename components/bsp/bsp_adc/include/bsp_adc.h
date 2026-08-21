#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bring up the ADC unit and configure the VBAT sense channel (GPIO and
 *        attenuation from Kconfig). Also attempts to create a calibration
 *        scheme so readings can be converted to millivolts; if the running
 *        chip has no calibration data, bsp_adc_read_voltage_mv() will return
 *        ESP_ERR_NOT_SUPPORTED and only raw reads remain available.
 */
esp_err_t bsp_adc_init(void);

/**
 * @brief Read the raw ADC conversion result on the VBAT sense channel.
 */
esp_err_t bsp_adc_read_raw(int *out_raw);

/**
 * @brief Read the calibrated voltage (mV) on the VBAT sense channel.
 */
esp_err_t bsp_adc_read_voltage_mv(int *out_mv);

#ifdef __cplusplus
}
#endif
