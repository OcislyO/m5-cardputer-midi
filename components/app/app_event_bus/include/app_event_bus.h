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
} app_event_midi_type_t;

typedef struct {
    app_event_midi_type_t type;
    uint8_t channel;  // 0-15, per MIDI 1.0 -- see app_midi_note_on's doc for why nothing downstream uses it yet
    uint8_t note;     // 0-127
    uint8_t velocity; // 0-127; 0 on a NOTE_OFF (release velocity isn't modeled)
} app_event_midi_t;


typedef enum {
    EVENT_MIDI_NOTE = 0,
    EVENT_MAX_ID,
} event_id;


esp_err_t app_event_bus_init(void);


esp_err_t app_event_bus_send(event_id id, const app_event_midi_t *event);


esp_err_t app_event_bus_subscribe(event_id id, QueueHandle_t queue);


esp_err_t app_event_bus_unsubscribe(event_id id, QueueHandle_t queue);

#ifdef __cplusplus
}
#endif
