#include "app_core.h"
#include "sys_nvs.h"
#include "bsp_i2c.h"
#include "bsp_spi.h"
#include "bsp_gpio.h"
#include "esp_log.h"
#include "sys_dsp.h"
#include "sys_bat.h"
#include "sys_audio.h"
#include "sys_usb.h"
#include "sys_wlan.h"
#include "app_ui.h"
#include "app_event_bus.h"
#include "app_midi_kbd.h"
#include "app_synth.h"
#include "app_seq.h"
#include "app_web.h"
#include "freertos/task.h"

static const char *TAG = "app";

#define APP_CORE_SCALE_NOTE_MS 300
#define APP_CORE_SCALE_VELOCITY 100
#define APP_CORE_WLAN_TIMEOUT_MS 15000

app_t *ui_app;
app_t *midi_kbd_app;
app_t *synth_app;
app_t *seq_app;
app_t *web_app;

// One-shot demo: plays a C major scale (C4-C5) by publishing directly to the
// MIDI bus, to exercise app_synth's own bus subscription end to end without
// needing a real MIDI source yet.
static void app_core_scale_demo_task(void *arg)
{
    static const uint8_t notes[] = { 60, 62, 64, 65, 67, 69, 71, 72 }; // C D E F G A B C

    for (size_t i = 0; i < sizeof(notes) / sizeof(notes[0]); i++) {
        app_event_midi_t on = {
            .type = APP_MIDI_EVENT_NOTE_ON, .channel = 0, .note = notes[i], .velocity = APP_CORE_SCALE_VELOCITY
        };
        app_event_bus_send(EVENT_MIDI_NOTE, &on);

        vTaskDelay(pdMS_TO_TICKS(APP_CORE_SCALE_NOTE_MS));

        app_event_midi_t off = { .type = APP_MIDI_EVENT_NOTE_OFF, .channel = 0, .note = notes[i], .velocity = 0 };
        app_event_bus_send(EVENT_MIDI_NOTE, &off);
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
    // ESP_ERROR_CHECK(sys_usb_init());

    // app_synth, app_midi_event, and app_seq are independent consumers/
    // producers on the MIDI bus -- none of them know about each other, so
    // order between them doesn't matter, only that the bus itself exists
    // first.
    ESP_ERROR_CHECK(app_event_bus_init());

    ui_app = app_ui_app_init();
    ESP_ERROR_CHECK(ui_app->init(ui_app));
    ESP_ERROR_CHECK(ui_app->start(ui_app));

    midi_kbd_app = app_midi_kbd_app_init();
    ESP_ERROR_CHECK(midi_kbd_app->init(midi_kbd_app));
    ESP_ERROR_CHECK(midi_kbd_app->start(midi_kbd_app));

    synth_app = app_synth_app_init();
    ESP_ERROR_CHECK(synth_app->init(synth_app));
    ESP_ERROR_CHECK(synth_app->start(synth_app));

    seq_app = app_seq_app_init();
    ESP_ERROR_CHECK(seq_app->init(seq_app));
    ESP_ERROR_CHECK(seq_app->start(seq_app));

    // Radio and web UI come up last: the keyboard and synth should already be
    // playable, and app_web only needs an address by the time it logs its URL.
    ESP_ERROR_CHECK(sys_wlan_init());
    // With no stored credentials (and no CONFIG_SYS_WLAN_STA_SSID) this falls
    // straight through to the "Cardputer-XXXX" softap, so the page is always
    // reachable -- a failure here still leaves the instrument usable.
    esp_err_t wlan_err = sys_wlan_auto_connect(APP_CORE_WLAN_TIMEOUT_MS, true);
    if (wlan_err != ESP_OK) {
        ESP_LOGW(TAG, "wlan not up: %s", esp_err_to_name(wlan_err));
    }

    // Same reasoning as the radio: a Cardputer that cannot serve the page is
    // still an instrument, so don't take the whole app down with it.
    web_app = app_web_app_init();
    esp_err_t web_err = web_app->init(web_app);
    if (web_err == ESP_OK) {
        web_err = web_app->start(web_app);
    }
    if (web_err != ESP_OK) {
        ESP_LOGW(TAG, "web ui not started: %s", esp_err_to_name(web_err));
    }

    vTaskDelay(pdMS_TO_TICKS(300));
    xTaskCreate(app_core_scale_demo_task, "app_core_scale_demo", 2048, NULL, 3, NULL);

    ESP_LOGI(TAG, "m5_midi app started");
}
