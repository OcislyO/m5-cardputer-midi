#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the battery monitor: brings up bsp_adc and periodically samples
 *        the VBAT-sense channel (which reads 1/2 of the actual battery
 *        voltage), filters it, and converts it to an approximate state of
 *        charge.
 */
esp_err_t sys_bat_init(void);

/**
 * @brief Get the latest filtered battery level, 0-100%.
 * @return ESP_ERR_INVALID_STATE if no sample has completed yet.
 */
esp_err_t sys_bat_get_percent(uint8_t *out_percent);

#ifdef __cplusplus
}
#endif
