#pragma once

#include "esp_err.h"
#include "driver/i2s_common.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bring up the shared Cardputer I2S bus in standard mode: 16-bit mono,
 *        TX (speaker, DSDIN) and RX (mic, ASDOUT) channels both enabled, sharing
 *        BCLK/WS. Pins come from Kconfig; only the sample rate is caller-supplied.
 */
esp_err_t bsp_i2s_init(uint32_t sample_rate_hz);

/**
 * @brief Blocking write of 16-bit mono samples to the TX (speaker) channel.
 */
esp_err_t bsp_i2s_write(const void *data, size_t len, size_t *bytes_written, uint32_t timeout_ms);

/**
 * @brief Blocking read of 16-bit mono samples from the RX (mic) channel.
 */
esp_err_t bsp_i2s_read(void *data, size_t len, size_t *bytes_read, uint32_t timeout_ms);

/**
 * @brief Register a callback fired (from ISR context) each time a TX DMA buffer
 *        finishes sending. Pass NULL to clear a previously registered callback.
 */
esp_err_t bsp_i2s_register_tx_done_cb(i2s_isr_callback_t cb, void *user_ctx);

/**
 * @brief Register a callback fired (from ISR context) each time an RX DMA buffer
 *        finishes receiving. Pass NULL to clear a previously registered callback.
 */
esp_err_t bsp_i2s_register_rx_done_cb(i2s_isr_callback_t cb, void *user_ctx);

#ifdef __cplusplus
}
#endif
