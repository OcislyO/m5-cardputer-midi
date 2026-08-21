#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SYS_KBD_KEY_NONE = 0,
    SYS_KBD_KEY_ASCII,     ///< plain printable key; see sys_kbd_event_t.ascii
    SYS_KBD_KEY_TAB,
    SYS_KBD_KEY_SHIFT,
    SYS_KBD_KEY_CAPSLOCK,
    SYS_KBD_KEY_ENTER,
    SYS_KBD_KEY_CTRL,
    SYS_KBD_KEY_OPT,       ///< the "opt" (fn-like) modifier key
    SYS_KBD_KEY_ALT,
    SYS_KBD_KEY_BACKSPACE,
} sys_kbd_keycode_t;

typedef struct {
    sys_kbd_keycode_t keycode;
    char    ascii;   ///< resolved (shift-aware) character, valid when keycode == SYS_KBD_KEY_ASCII
    bool    pressed; ///< true = key down, false = key up
    uint8_t row;     ///< physical layout row, 0-3
    uint8_t col;     ///< physical layout column, 0-13
} sys_kbd_event_t;

/**
 * @brief Start the keyboard service: brings up drv_tca8418, wires an
 *        interrupt on its INT line, and starts a task that drains key events,
 *        remaps them from the TCA8418's electrical 7x8 matrix onto
 *        Cardputer's physical 4x14 key layout, resolves shift state, and
 *        pushes decoded events onto an internal queue.
 */
esp_err_t sys_kbd_init(void);

/**
 * @brief Pop the next decoded key event, blocking up to timeout ticks.
 */
esp_err_t sys_kbd_read_event(sys_kbd_event_t *out_event, TickType_t timeout);

#ifdef __cplusplus
}
#endif
