#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t row;
    uint8_t col;
    bool    pressed; ///< true = key down, false = key up
} drv_tca8418_key_event_t;

/**
 * @brief Bring up the TCA8418 keypad scan IC: registers it on the shared
 *        bsp_i2c bus, configures the row/col matrix (Kconfig-defined size),
 *        flushes any stale FIFO entries, and enables the key-event interrupt.
 */
esp_err_t drv_tca8418_init(void);

/**
 * @brief Pop the oldest pending key event out of the TCA8418's FIFO.
 * @param out_event    Receives the decoded event; only valid if *out_has_event is true.
 * @param out_has_event Set to false when the FIFO is empty (not an error).
 */
esp_err_t drv_tca8418_read_event(drv_tca8418_key_event_t *out_event, bool *out_has_event);

/**
 * @brief Clear the interrupt status register, deasserting the INT line once
 *        the FIFO has been drained.
 */
esp_err_t drv_tca8418_clear_interrupt(void);

#ifdef __cplusplus
}
#endif
