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

static ui_obj_t *s_root;
static ui_obj_t *s_screen;
static ui_obj_t *s_text;
static ui_obj_t *s_tick_text;
static ui_obj_t *s_kbd_text;
static ui_obj_t *s_bat_text;

// Demo routine for sys_dsp's dirty-rect renderer: bounces a small box around
// the screen by moving it to the next position each tick.
#define APP_UI_DEMO_BOX_SIZE  10
#define APP_UI_DEMO_COLOR     0xF800 // red, RGB565
#define APP_UI_DEMO_PERIOD_MS 50

static void app_ui_demo_task(void *arg)
{
    int16_t x = 0, y = 0;
    int16_t dx = 2, dy = 2;
    uint32_t tick = 0;

    ui_obj_t *box = sys_dsp_rect_register(s_screen, x, y, APP_UI_DEMO_BOX_SIZE, APP_UI_DEMO_BOX_SIZE, APP_UI_DEMO_COLOR);
    // Nested under box, not s_screen: rides along with it as it moves,
    // demonstrating that a moved parent carries its children with it.
    sys_dsp_text_register(box, 0, 1, 10, 8, "hi", APP_UI_TEXT_COLOR);

    for (;;) {
        char tick_str[16];
        snprintf(tick_str, sizeof(tick_str), "tick: %lu", (unsigned long)tick++);
        sys_dsp_text_set_text(s_tick_text, tick_str);

        x += dx;
        if (x < 0) {
            x = 0;
            dx = -dx;
        } else if (x + APP_UI_DEMO_BOX_SIZE > APP_UI_PANEL_WIDTH) {
            x = APP_UI_PANEL_WIDTH - APP_UI_DEMO_BOX_SIZE;
            dx = -dx;
        }

        y += dy;
        if (y < 0) {
            y = 0;
            dy = -dy;
        } else if (y + APP_UI_DEMO_BOX_SIZE > APP_UI_PANEL_HEIGHT) {
            y = APP_UI_PANEL_HEIGHT - APP_UI_DEMO_BOX_SIZE;
            dy = -dy;
        }

        sys_dsp_obj_move(box, x, y);

        vTaskDelay(pdMS_TO_TICKS(APP_UI_DEMO_PERIOD_MS));
    }
}

// Demo for sys_kbd: echoes typed characters into s_kbd_text. Backspace
// erases, Enter clears the line. Non-ASCII/non-Enter/non-Backspace keys
// (shift, ctrl, ...) don't change what's shown, so they're skipped.
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

        sys_dsp_text_set_text(s_kbd_text, line);
    }
}

// Shows the battery percentage top-right, refreshed off sys_bat's own
// sample -- polling faster than SYS_BAT_SAMPLE_PERIOD_US wouldn't show
// anything new, but is cheap and keeps this simple.
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
        sys_dsp_text_set_text(s_bat_text, bat_str);

        vTaskDelay(pdMS_TO_TICKS(APP_UI_BAT_PERIOD_MS));
    }
}

esp_err_t app_ui_start(void)
{
    esp_err_t err = sys_dsp_init();
    if (err != ESP_OK) {
        return err;
    }

    err = sys_kbd_init();
    if (err != ESP_OK) {
        return err;
    }

    err = sys_bat_init();
    if (err != ESP_OK) {
        return err;
    }

    s_root = sys_dsp_root_create();
    if (s_root == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_screen = sys_dsp_rect_register(s_root, 0, 0, APP_UI_PANEL_WIDTH, APP_UI_PANEL_HEIGHT, APP_UI_BG_COLOR);
    if (s_screen == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_text = sys_dsp_text_register(s_screen, 60, 60, 120, 16, "Hello World", APP_UI_TEXT_COLOR);
    if (s_text == NULL) {
        return ESP_ERR_NO_MEM;
    }

    // Demo for sys_dsp_text_set_text: relabeled every tick by app_ui_demo_task.
    s_tick_text = sys_dsp_text_register(s_screen, 60, 80, 120, 16, "tick: 0", APP_UI_TEXT_COLOR);
    if (s_tick_text == NULL) {
        return ESP_ERR_NO_MEM;
    }

    // Demo for sys_kbd: echoes typed characters, relabeled by app_ui_kbd_task.
    s_kbd_text = sys_dsp_text_register(s_screen, 20, 105, 200, 16, "", APP_UI_TEXT_COLOR);
    if (s_kbd_text == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_bat_text = sys_dsp_text_register(s_screen, APP_UI_PANEL_WIDTH - APP_UI_BAT_TEXT_W - APP_UI_BAT_MARGIN,
                                        APP_UI_BAT_MARGIN, APP_UI_BAT_TEXT_W, 16, "--%", APP_UI_TEXT_COLOR);
    if (s_bat_text == NULL) {
        return ESP_ERR_NO_MEM;
    }

    sys_dsp_root_switch(s_root);

    xTaskCreate(app_ui_demo_task, "app_ui_demo", 2048, NULL, 3, NULL);
    xTaskCreate(app_ui_kbd_task, "app_ui_kbd", 3072, NULL, 3, NULL);
    xTaskCreate(app_ui_bat_task, "app_ui_bat", 2048, NULL, 3, NULL);

    return ESP_OK;
}
