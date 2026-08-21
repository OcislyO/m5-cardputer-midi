#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bring up the BMI270 6-axis IMU on the shared bsp_i2c bus and start
 *        accelerometer + gyroscope measurement (100Hz, +-4g / +-1000dps).
 */
esp_err_t drv_bmi270_init(void);

/**
 * @brief Read the latest accelerometer sample, in g.
 */
esp_err_t drv_bmi270_get_accel(float *x, float *y, float *z);

/**
 * @brief Read the latest gyroscope sample, in degrees/second.
 */
esp_err_t drv_bmi270_get_gyro(float *x, float *y, float *z);

#ifdef __cplusplus
}
#endif
