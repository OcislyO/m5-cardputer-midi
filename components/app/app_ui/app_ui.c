#include "app_ui.h"
#include "sys_dsp.h"
#include "sys_kbd.h"
#include "sys_bat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

#define APP_UI_PANEL_WIDTH  240
#define APP_UI_PANEL_HEIGHT 135

#define APP_UI_BG_COLOR   0x801F // solid blue, RGB565
#define APP_UI_TEXT_COLOR 0xFFFF // white


static ui_obj_t *ui_root;
static ui_obj_t *bar;
static ui_obj_t *bar_bat_text;
static ui_obj_t *midi_backgraund;
static ui_obj_t *midi_kbd;
static ui_obj_t *midi_white_key[14];
static ui_obj_t *midi_black_key[10];

static ui_obj_t *shell_backgraund;
static ui_obj_t *shell_input_text;

// Demo routine for sys_dsp's dirty-rect renderer: bounces a small box around
// the screen by moving it to the next position each tick.
#define APP_UI_DEMO_BOX_SIZE  10
#define APP_UI_DEMO_COLOR     0xF800 // red, RGB565
#define APP_UI_DEMO_PERIOD_MS 50


static void app_ui_kbd_task(void *arg)
{
    char line[64] = { 0 };
    size_t len = 0;

    for (;;) {
        sys_kbd_event_t evt;
        if (sys_kbd_read_event(&evt, portMAX_DELAY) != ESP_OK || !evt.pressed) {
            continue;
        }

        if (evt.keycode == SYS_KBD_KEY_ASCII) {
            if (len < sizeof(line) - 1) {
                line[len++] = evt.ascii;
                line[len] = '\0';
            }
        } else if (evt.keycode == SYS_KBD_KEY_BACKSPACE) {
            if (len > 0) {
                line[--len] = '\0';
            }
        } else if (evt.keycode == SYS_KBD_KEY_ENTER) {
            len = 0;
            line[0] = '\0';
        } else {
            continue;
        }

        sys_dsp_text_set_text(shell_input_text, line);
    }
}

#define APP_UI_BAT_TEXT_W    36
#define APP_UI_BAT_MARGIN    4
#define APP_UI_BAT_PERIOD_MS 2000

static void app_ui_bat_task(void *arg)
{
    for (;;) {
        char bat_str[8];
        uint8_t percent;
        if (sys_bat_get_percent(&percent) == ESP_OK) {
            snprintf(bat_str, sizeof(bat_str), "%u%%", percent);
        } else {
            snprintf(bat_str, sizeof(bat_str), "--%%");
        }
        sys_dsp_text_set_text(bar_bat_text, bat_str);

        vTaskDelay(pdMS_TO_TICKS(APP_UI_BAT_PERIOD_MS));
    }
}

esp_err_t app_ui_midi_start() {
    ui_root = sys_dsp_root_create();
    if (ui_root == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    bar = sys_dsp_rect_register(ui_root, 0, 0, 240, 135, 0x41d0);
    if (bar == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    bar_bat_text = sys_dsp_text_register(bar, APP_UI_PANEL_WIDTH - APP_UI_BAT_TEXT_W - APP_UI_BAT_MARGIN,
                                        APP_UI_BAT_MARGIN, APP_UI_BAT_TEXT_W, 12, "--%", APP_UI_TEXT_COLOR);
    if (bar_bat_text == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    midi_backgraund = sys_dsp_rect_register(ui_root, 0, 16, 240, 119, 0xa4f8);
    if (midi_backgraund == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    midi_kbd = sys_dsp_rect_register(midi_backgraund, 0, 77, 240, 42, 0x9cf3);
    if (midi_kbd == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    
    for (size_t i = 0; i < 14; i++)
    {
        midi_white_key[i] = sys_dsp_rect_register(midi_kbd, 17 * i + 1, 18, 15, 24, 0xe73c);
        if (midi_white_key[i] == NULL)
        {
            return ESP_ERR_NO_MEM;
        }
    }

    uint8_t x = 0;
    for (size_t i = 0; i < 10; i++)
    {
        if (x == 3 || x == 6 || x == 10) x ++;
        midi_black_key[i] = sys_dsp_rect_register(midi_kbd, 17 * x + 1, 1, 15, 15, 0x39aa);
        if (midi_white_key[i] == NULL)
        {
            return ESP_ERR_NO_MEM;
        }
        x ++;
    }

    sys_dsp_root_switch(ui_root);

    xTaskCreate(app_ui_bat_task, "app_ui_bat", 2048, NULL, 3, NULL);

    return ESP_OK;
}
