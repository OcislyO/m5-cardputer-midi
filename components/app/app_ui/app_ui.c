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

#define APP_UI_BAT_TEXT_W    36
#define APP_UI_BAT_MARGIN    4
#define APP_UI_BAT_PERIOD_MS 2000

app_ui_app_t app_ui_app;

static ui_obj_t *ui_root;
static ui_obj_t *bar;
static ui_obj_t *bar_bat_text;
static ui_obj_t *midi_backgraund;
static ui_obj_t *midi_kbd;
static ui_obj_t *midi__keys[2][14];

static ui_obj_t *shell_backgraund;
static ui_obj_t *shell_input_text;

static esp_err_t app_ui_init(app_t *app);
static esp_err_t app_ui_start(app_t *app);
static esp_err_t app_ui_stop(app_t *app);
static size_t app_ui_get_state(struct app_s *app, int state, void *out, uint8_t size);
static esp_err_t app_ui_command(app_t *app, int16_t command, ...);

static void app_ui_bat_task(void *arg);
static void app_ui_kbd_task(void *arg);

// Demo routine for sys_dsp's dirty-rect renderer: bounces a small box around
// the screen by moving it to the next position each tick.
#define APP_UI_DEMO_BOX_SIZE  10
#define APP_UI_DEMO_COLOR     0xF800 // red, RGB565
#define APP_UI_DEMO_PERIOD_MS 50

app_t *app_ui_app_init(void) {
    app_ui_app.base.state = APP_STATE_UNINIT;
    app_ui_app.base.id = APP_ID_UI;
    app_ui_app.base.ctx = &app_ui_app;
    app_ui_app.base.init = app_ui_init;
    app_ui_app.base.start = app_ui_start;
    app_ui_app.base.stop = app_ui_stop;
    app_ui_app.base.get_state = app_ui_get_state;
    app_ui_app.base.command = app_ui_command;
    return &app_ui_app.base;
}

static esp_err_t app_ui_init(app_t *app) {
    esp_err_t err = ESP_OK;
    if (app->state != APP_STATE_UNINIT)
        goto ret;

    err = sys_dsp_init();
    if (err)
        goto ret;
    
    app->state = APP_STATE_STOPED;

    ret:
        return err;
}

static esp_err_t app_ui_start(app_t *app) {
    esp_err_t err = ESP_OK;
    if (app->state != APP_STATE_STOPED)
    {
        err = ESP_ERR_INVALID_STATE;
        goto ret;
    }

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
        midi__keys[1][i] = sys_dsp_rect_register(midi_kbd, 17 * i + 1, 18, 15, 24, 0xe73c);
        if (midi__keys[1][i] == NULL)
        {
            return ESP_ERR_NO_MEM;
        }
    }

    for (size_t i = 0; i < 14; i++)
    {
        if (i == 3 || i == 6 || i == 10 || i == 13) continue;
        midi__keys[0][i] = sys_dsp_rect_register(midi_kbd, 17 * i + 1, 1, 15, 15, 0x39aa);
        if (midi__keys[0][i] == NULL)
        {
            return ESP_ERR_NO_MEM;
        }
    }

    shell_backgraund = sys_dsp_rect_register(ui_root, 0, 16, 240, 119, 0x2125);
    if (shell_backgraund == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    sys_dsp_obj_set_invalid(shell_backgraund, true);
    
    shell_input_text = sys_dsp_text_register(shell_backgraund, APP_UI_BAT_MARGIN, 99, 240 - APP_UI_BAT_MARGIN * 2, 16, ">>>", 0xbf9f);
    if (shell_input_text == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    sys_dsp_root_switch(ui_root);

    xTaskCreate(app_ui_bat_task, "app_ui_bat", 2048, NULL, 3, NULL);
    xTaskCreate(app_ui_kbd_task, "app_ui_kbd", 2048, NULL, 3, NULL);

    ret:
        return err;
}

static esp_err_t app_ui_stop(app_t *app) {return ESP_OK;}
static size_t app_ui_get_state(struct app_s *app, int state, void *out, uint8_t size) {return 0;}
static esp_err_t app_ui_command(app_t *app, int16_t command, ...) {
    esp_err_t err = ESP_OK;
    va_list args;
    va_start(args, command);

    const app_ui_cmd_id_t cmd = (app_ui_cmd_id_t)command;

    switch (cmd)
    {
    case APP_UI_SET_KEY:
        int key_number = va_arg(args, int);
        int color = va_arg(args, int);
        err = sys_dsp_rect_set_color(midi__keys[key_number / 14][key_number % 14], (uint16_t)color);
        break;
    
    default:
        err = ESP_ERR_INVALID_ARG;
    }

    va_end(args);

    return err;
}

static void app_ui_kbd_task(void *arg)
{
    char line[64] = { 0 };
    size_t len = 0;
    sys_kbd_mode_t ui_kbd_mode = 0xff;  // 无效值，初始值在任务中同步

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(50));
        if (ui_kbd_mode != sys_kbd_get_mode()) {
            ui_kbd_mode = sys_kbd_get_mode();
            if (ui_kbd_mode == SYS_KBD_MODE_RAW)
            {
                sys_dsp_obj_set_invalid(shell_backgraund, true);
                sys_dsp_obj_set_invalid(midi_backgraund, false);
            } else {
                sys_dsp_obj_set_invalid(shell_backgraund, false);
                sys_dsp_obj_set_invalid(midi_backgraund, true);
            }
        }

        if (ui_kbd_mode == SYS_KBD_MODE_RAW)
        {
            ;
        }
        else {
            sys_kbd_event_t evt;
            if (sys_kbd_read_event(&evt, 0) != ESP_OK || !evt.pressed) {
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
}



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
