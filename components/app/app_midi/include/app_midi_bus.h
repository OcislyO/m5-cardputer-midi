#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_MIDI_EVENT_NOTE_ON = 0,
    APP_MIDI_EVENT_NOTE_OFF,
} app_midi_event_type_t;

typedef struct {
    app_midi_event_type_t type;
    uint8_t channel;  // 0-15, per MIDI 1.0 -- see app_midi_note_on's doc for why nothing downstream uses it yet
    uint8_t note;     // 0-127
    uint8_t velocity; // 0-127; 0 on a NOTE_OFF (release velocity isn't modeled)
} app_midi_event_t;

/**
 * @brief Bring up the MIDI bus: a fan-out dispatcher sitting between
 *        whatever produces MIDI events (app_midi_event's keyboard mapper
 *        today; a future USB-MIDI IN could call app_midi_bus_send directly)
 *        and whatever consumes them (app_midi subscribes one queue here to
 *        drive app_synth; a future USB-MIDI OUT would subscribe its own).
 *        Producers and consumers never need to know about each other.
 *        Idempotent: safe to call again after the first successful call.
 */
esp_err_t app_midi_bus_init(void);

/**
 * @brief Publish `event` to every currently-subscribed queue
 *        (app_midi_bus_subscribe). Non-blocking: a consumer whose queue is
 *        full misses the event (logged, not fatal) rather than stalling the
 *        producer or the bus's other consumers.
 */
esp_err_t app_midi_bus_send(const app_midi_event_t *event);

/**
 * @brief Register `queue` to receive a copy of every event passed to
 *        app_midi_bus_send from now on. `queue` must be sized to hold
 *        app_midi_event_t elements and stay valid for as long as it's
 *        subscribed. Subscribing the same queue twice is a no-op.
 * @return ESP_ERR_NO_MEM if the subscriber table is already full.
 */
esp_err_t app_midi_bus_subscribe(QueueHandle_t queue);

/**
 * @brief Undo app_midi_bus_subscribe. No-op (returns ESP_OK) if `queue`
 *        isn't currently subscribed.
 */
esp_err_t app_midi_bus_unsubscribe(QueueHandle_t queue);

#ifdef __cplusplus
}
#endif
