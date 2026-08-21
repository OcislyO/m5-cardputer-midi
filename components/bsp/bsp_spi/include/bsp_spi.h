#pragma once

#include "esp_err.h"
#include "driver/spi_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the shared Cardputer SPI bus (MOSI/SCLK from Kconfig, MISO unused).
 *        Safe to call more than once.
 */
esp_err_t bsp_spi_init(void);

/**
 * @brief Get the SPI host used by the shared bus.
 */
spi_host_device_t bsp_spi_get_host(void);

/**
 * @brief Register a device on the shared SPI bus.
 * @param dev_cfg    Device config (caller sets mode/clock_speed_hz/spics_io_num/queue_size;
 *                    use CONFIG_BSP_SPI_CS_GPIO for spics_io_num unless the device has its
 *                    own dedicated CS line).
 * @param out_handle Receives the new device handle.
 */
esp_err_t bsp_spi_add_device(const spi_device_interface_config_t *dev_cfg, spi_device_handle_t *out_handle);

/**
 * @brief Unregister a device previously added with bsp_spi_add_device().
 */
esp_err_t bsp_spi_remove_device(spi_device_handle_t handle);

#ifdef __cplusplus
}
#endif
