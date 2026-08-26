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
 * @brief One physical key transition, with no keycode/ASCII/shift resolution
 *        applied -- just "this row/col went down/up". See sys_kbd_read_raw_event.
 */
typedef struct {
    uint8_t row;     ///< physical layout row, 0-3 -- same numbering as sys_kbd_event_t.row
    uint8_t col;     ///< physical layout column, 0-13
    bool    pressed; ///< true = key down, false = key up
} sys_kbd_raw_event_t;

/**
 * @brief Which of sys_kbd's two event queues newly-scanned keys are
 *        currently routed to. See sys_kbd_read_event/sys_kbd_read_raw_event.
 */
typedef enum {
    SYS_KBD_MODE_TYPING = 0, ///< default: keys are decoded (keycode/ASCII/shift) and delivered via sys_kbd_read_event
    SYS_KBD_MODE_RAW,        ///< keys are delivered unmapped (row/col only) via sys_kbd_read_raw_event
} sys_kbd_mode_t;

/**
 * @brief Start the keyboard service: brings up drv_tca8418, wires an
 *        interrupt on its INT line, and starts a task that drains key events,
 *        remaps them from the TCA8418's electrical 7x8 matrix onto
 *        Cardputer's physical 4x14 key layout, and dispatches each one to
 *        whichever of sys_kbd_read_event/sys_kbd_read_raw_event matches the
 *        current mode (see sys_kbd_get_mode) -- starts in SYS_KBD_MODE_TYPING.
 *        OPT and CAPSLOCK are reserved as the mode-switch chord (hold one,
 *        press the other) and are never delivered to either queue.
 */
esp_err_t sys_kbd_init(void);

/**
 * @brief Pop the next decoded key event, blocking up to timeout ticks.
 *        Only receives events while sys_kbd is in SYS_KBD_MODE_TYPING.
 */
esp_err_t sys_kbd_read_event(sys_kbd_event_t *out_event, TickType_t timeout);

/**
 * @brief Pop the next raw key transition, blocking up to timeout ticks.
 *        Only receives events while sys_kbd is in SYS_KBD_MODE_RAW.
 */
esp_err_t sys_kbd_read_raw_event(sys_kbd_raw_event_t *out_event, TickType_t timeout);

/**
 * @brief Current mode -- see sys_kbd_mode_t. Toggled by the OPT+CAPSLOCK
 *        chord; there is no programmatic setter.
 */
sys_kbd_mode_t sys_kbd_get_mode(void);

#ifdef __cplusplus
}
#endif
