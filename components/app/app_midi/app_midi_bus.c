#include "app_midi_bus.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include <stdbool.h>
#include <stddef.h>

// Small and fixed: today's only subscriber is app_midi's own app_synth
// consumer task; room for a couple more (a future USB-MIDI OUT, a UI note
// display, ...) without needing a dynamically-sized table.
#define APP_MIDI_BUS_MAX_SUBSCRIBERS 4

static const char *TAG = "app_midi_bus";
static QueueHandle_t s_subscribers[APP_MIDI_BUS_MAX_SUBSCRIBERS];
static SemaphoreHandle_t s_lock;

esp_err_t app_midi_bus_init(void)
{
    if (s_lock != NULL) {
        return ESP_OK;
    }

    s_lock = xSemaphoreCreateMutex();
    return (s_lock != NULL) ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t app_midi_bus_send(const app_midi_event_t *event)
{
    if (s_lock == NULL || event == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (size_t i = 0; i < APP_MIDI_BUS_MAX_SUBSCRIBERS; i++) {
        if (s_subscribers[i] != NULL && xQueueSend(s_subscribers[i], event, 0) != pdTRUE) {
            ESP_LOGW(TAG, "subscriber %u queue full, dropping event", (unsigned)i);
        }
    }
    xSemaphoreGive(s_lock);

    return ESP_OK;
}

esp_err_t app_midi_bus_subscribe(QueueHandle_t queue)
{
    if (s_lock == NULL || queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);

    bool already_subscribed = false;
    int free_slot = -1;
    for (size_t i = 0; i < APP_MIDI_BUS_MAX_SUBSCRIBERS && !already_subscribed; i++) {
        if (s_subscribers[i] == queue) {
            already_subscribed = true;
        } else if (s_subscribers[i] == NULL && free_slot < 0) {
            free_slot = (int)i;
        }
    }

    esp_err_t result;
    if (already_subscribed) {
        result = ESP_OK;
    } else if (free_slot >= 0) {
        s_subscribers[free_slot] = queue;
        result = ESP_OK;
    } else {
        result = ESP_ERR_NO_MEM;
    }

    xSemaphoreGive(s_lock);
    return result;
}

esp_err_t app_midi_bus_unsubscribe(QueueHandle_t queue)
{
    if (s_lock == NULL || queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (size_t i = 0; i < APP_MIDI_BUS_MAX_SUBSCRIBERS; i++) {
        if (s_subscribers[i] == queue) {
            s_subscribers[i] = NULL;
            break;
        }
    }
    xSemaphoreGive(s_lock);

    return ESP_OK;
}
