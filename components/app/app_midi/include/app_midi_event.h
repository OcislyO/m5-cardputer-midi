#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Starts the task that reads sys_kbd's raw (unmapped) key events and
 *        turns the bottom two physical rows into piano keys, publishing a
 *        NOTE_ON/NOTE_OFF onto the MIDI bus (app_midi_bus_send, see
 *        app_midi_bus.h) for each press/release. Every other row is
 *        ignored. Brings up sys_kbd and the bus itself if they aren't
 *        already (both idempotent).
 *        Idempotent: safe to call again after the first successful call.
 */
esp_err_t app_midi_event_start(void);

#ifdef __cplusplus
}
#endif
