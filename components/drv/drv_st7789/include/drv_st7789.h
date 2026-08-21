#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t width;         ///< Panel width in pixels
    uint16_t height;        ///< Panel height in pixels
    uint16_t x_offset;      ///< Column address offset (panel-dependent)
    uint16_t y_offset;      ///< Row address offset (panel-dependent)
    uint8_t  madctl;        ///< MADCTL value (orientation / RGB-BGR order)
    uint32_t spi_clock_hz;  ///< SPI clock for the panel device
} drv_st7789_config_t;

/**
 * @brief Bring up the ST7789 panel: configures the RST/DC/BL GPIOs, registers a
 *        device on the shared bsp_spi bus, runs the reset + init command sequence,
 *        and turns the backlight on at full brightness.
 */
esp_err_t drv_st7789_init(const drv_st7789_config_t *cfg);

/**
 * @brief Set the active drawing window (column/row address range, inclusive) for
 *        the pixel data sent by the next drv_st7789_send_data() call(s).
 */
esp_err_t drv_st7789_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

/**
 * @brief Stream raw pixel payload bytes to the panel (DC held high, i.e. "data").
 *        Typically big-endian RGB565 pixels for the window set with
 *        drv_st7789_set_window().
 */
esp_err_t drv_st7789_send_data(const uint8_t *data, size_t len);

/**
 * @brief Set backlight brightness, 0-100%.
 */
esp_err_t drv_st7789_set_backlight(uint8_t duty_pct);

#ifdef __cplusplus
}
#endif
