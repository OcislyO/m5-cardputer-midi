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
#define APP_CORE_TASK_EXIT_POLL_MS 10 // app_task_wait_stopped 的轮询间隔

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

    midi_kbd_app = app_midi_kbd_app_init();
    ESP_ERROR_CHECK(midi_kbd_app->init(midi_kbd_app));

    synth_app = app_synth_app_init();
    ESP_ERROR_CHECK(synth_app->init(synth_app));

    seq_app = app_seq_app_init();
    ESP_ERROR_CHECK(seq_app->init(seq_app));

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
    if (web_err != ESP_OK) {
        ESP_LOGW(TAG, "web ui not started: %s", esp_err_to_name(web_err));
    }

    vTaskDelay(pdMS_TO_TICKS(300));
    xTaskCreate(app_core_scale_demo_task, "app_core_scale_demo", 2048, NULL, 3, NULL);

    ESP_LOGI(TAG, "m5_midi app started");
}

// 调用一个 app 的 uninit，未运行的当作已经停好。
// 返回值表示这个 app 是否确实回到了 UNINIT。
static bool app_shutdown(app_t *app)
{
    if (app == NULL || app->state != APP_STATE_RUNNING) {
        return true;
    }
    return app->uninit(app) == ESP_OK;
}

void app_stop(void)
{
    // 与 app_start() 里的顺序相反：app_midi_kbd 会调 ui_app->command，
    // 所以必须先停它，再停 app_ui，否则键盘任务会踩到已经释放的 UI 对象。
    app_shutdown(web_app);
    app_shutdown(seq_app);
    app_shutdown(synth_app);

    // 反过来，如果键盘任务没能停下来（uninit 超时），就不要再释放 app_ui 的
    // UI 对象了：那个任务随时可能再调一次 command。宁可留着不回收。
    if (app_shutdown(midi_kbd_app)) {
        app_shutdown(ui_app);
    } else {
        ESP_LOGE(TAG, "app_midi_kbd 未停干净，跳过 app_ui 的回收，避免键盘任务操作已释放的 UI 对象");
    }

    ESP_LOGI(TAG, "m5_midi apps stopped");
}

bool app_task_wait_stopped(TaskHandle_t *handle, uint32_t timeout_ms)
{
    if (handle == NULL) {
        return true;
    }

    for (uint32_t waited = 0; waited < timeout_ms; waited += APP_CORE_TASK_EXIT_POLL_MS) {
        if (*handle == NULL) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(APP_CORE_TASK_EXIT_POLL_MS));
    }
    return *handle == NULL;
}
