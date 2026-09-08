#include "app_event_bus.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include <stdbool.h>
#include <stddef.h>


#define APP_EVENT_BUS_MAX_SUBSCRIBERS 6

static const char *TAG = "app_event_bus";
static uint8_t Initialized = false;
static QueueHandle_t s_subscribers[EVENT_MAX_ID][APP_EVENT_BUS_MAX_SUBSCRIBERS];
static SemaphoreHandle_t s_lock[EVENT_MAX_ID];

esp_err_t app_event_bus_init(void)
{
    esp_err_t err = ESP_OK;
    if (Initialized != false)
        goto ret;

    for (size_t i = 0; i < EVENT_MAX_ID; i++)
    {
        s_lock[i] = xSemaphoreCreateMutex();
        if (s_lock[i] == NULL)
        {
            err = ESP_ERR_NO_MEM;
            goto free_lock;
        }
    }

    Initialized = true;
    goto ret;
    
    
    free_lock:
        for (size_t i = 0; i < EVENT_MAX_ID; i++)
        {
            if (s_lock[i])
                vSemaphoreDelete(s_lock[i]);
        }

    ret:
        return err;
}

esp_err_t app_event_bus_send(event_id id, const app_event_midi_t *event)
{
    if (s_lock[id] == NULL || event == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_lock[id], portMAX_DELAY);
    for (size_t i = 0; i < APP_EVENT_BUS_MAX_SUBSCRIBERS; i++) {
        if (s_subscribers[id][i] != NULL && xQueueSend(s_subscribers[id][i], event, 0) != pdTRUE) {
            ESP_LOGW(TAG, "subscriber %u queue full, dropping event", (unsigned)i);
        }
    }
    xSemaphoreGive(s_lock[id]);

    return ESP_OK;
}

esp_err_t app_event_bus_subscribe(event_id id, QueueHandle_t queue)
{
    if (s_lock[id] == NULL || queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_lock[id], portMAX_DELAY);

    bool already_subscribed = false;
    int free_slot = -1;
    for (size_t i = 0; i < APP_EVENT_BUS_MAX_SUBSCRIBERS && !already_subscribed; i++) {
        if (s_subscribers[id][i] == queue) {
            already_subscribed = true;
        } else if (s_subscribers[id][i] == NULL && free_slot < 0) {
            free_slot = (int)i;
        }
    }

    esp_err_t result;
    if (already_subscribed) {
        result = ESP_OK;
    } else if (free_slot >= 0) {
        s_subscribers[id][free_slot] = queue;
        result = ESP_OK;
    } else {
        result = ESP_ERR_NO_MEM;
    }

    xSemaphoreGive(s_lock[id]);
    return result;
}

esp_err_t app_event_bus_unsubscribe(event_id id, QueueHandle_t queue)
{
    if (s_lock[id] == NULL || queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_lock[id], portMAX_DELAY);
    for (size_t i = 0; i < APP_EVENT_BUS_MAX_SUBSCRIBERS; i++) {
        if (s_subscribers[id][i] == queue) {
            s_subscribers[id][i] = NULL;
            break;
        }
    }
    xSemaphoreGive(s_lock[id]);

    return ESP_OK;
}
