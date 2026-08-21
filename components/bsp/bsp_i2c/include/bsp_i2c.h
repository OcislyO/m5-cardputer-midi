#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the shared Cardputer I2C master bus (SDA/SCL from Kconfig).
 *        Safe to call more than once.
 */
esp_err_t bsp_i2c_init(void);

/**
 * @brief Get the shared I2C master bus handle.
 * @return Bus handle, or NULL if bsp_i2c_init() has not been called yet.
 */
i2c_master_bus_handle_t bsp_i2c_get_bus_handle(void);

/**
 * @brief Register a device on the shared I2C bus.
 * @param dev_cfg    Device address/speed config (caller sets scl_speed_hz).
 * @param out_handle Receives the new device handle.
 */
esp_err_t bsp_i2c_add_device(const i2c_device_config_t *dev_cfg, i2c_master_dev_handle_t *out_handle);

/**
 * @brief Unregister a device previously added with bsp_i2c_add_device().
 */
esp_err_t bsp_i2c_remove_device(i2c_master_dev_handle_t handle);

#ifdef __cplusplus
}
#endif
