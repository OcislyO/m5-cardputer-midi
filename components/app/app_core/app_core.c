#include "app_core.h"
#include "sys_nvs.h"
#include "bsp_i2c.h"
#include "bsp_spi.h"
#include "bsp_gpio.h"
#include "esp_log.h"
#include "sys_dsp.h"
#include "sys_bat.h"
#include "sys_audio.h"
#include "app_ui.h"
#include "app_midi_bus.h"
#include "app_midi_event.h"
#include "app_synth.h"
#include "app_seq.h"
#include "freertos/task.h"

static const char *TAG = "app";

#define APP_CORE_SCALE_NOTE_MS 300
#define APP_CORE_SCALE_VELOCITY 100

// One-shot demo: plays a C major scale (C4-C5) by publishing directly to the
// MIDI bus, to exercise app_synth's own bus subscription end to end without
// needing a real MIDI source yet.
static void app_core_scale_demo_task(void *arg)
{
    static const uint8_t notes[] = { 60, 62, 64, 65, 67, 69, 71, 72 }; // C D E F G A B C

    for (size_t i = 0; i < sizeof(notes) / sizeof(notes[0]); i++) {
        app_midi_event_t on = {
            .type = APP_MIDI_EVENT_NOTE_ON, .channel = 0, .note = notes[i], .velocity = APP_CORE_SCALE_VELOCITY
        };
        app_midi_bus_send(&on);

        vTaskDelay(pdMS_TO_TICKS(APP_CORE_SCALE_NOTE_MS));

        app_midi_event_t off = { .type = APP_MIDI_EVENT_NOTE_OFF, .channel = 0, .note = notes[i], .velocity = 0 };
        app_midi_bus_send(&off);
    }

    vTaskDelete(NULL);
}

void app_start(void)
{
    ESP_ERROR_CHECK(sys_nvs_init());
    ESP_ERROR_CHECK(bsp_gpio_init());
    ESP_ERROR_CHECK(bsp_i2c_init());
    ESP_ERROR_CHECK(bsp_spi_init());

    ESP_ERROR_CHECK(sys_dsp_init());
    ESP_ERROR_CHECK(sys_bat_init());
    ESP_ERROR_CHECK(sys_audio_init());

    // app_synth, app_midi_event, and app_seq are independent consumers/
    // producers on the MIDI bus -- none of them know about each other, so
    // order between them doesn't matter, only that the bus itself exists
    // first.
    ESP_ERROR_CHECK(app_midi_bus_init());
    ESP_ERROR_CHECK(app_midi_event_start());
    ESP_ERROR_CHECK(app_ui_midi_start());
    ESP_ERROR_CHECK(app_synth_init());
    ESP_ERROR_CHECK(app_seq_init());

    xTaskCreate(app_core_scale_demo_task, "app_core_scale_demo", 2048, NULL, 3, NULL);

    ESP_LOGI(TAG, "m5_midi app started");
}
