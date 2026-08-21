#include "sys_kbd.h"
#include "drv_tca8418.h"
#include "bsp_gpio.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define SYS_KBD_QUEUE_LEN   16
#define SYS_KBD_TASK_STACK  3072
#define SYS_KBD_TASK_PRIO   5

static const char *TAG = "sys_kbd";

// Cardputer's physical 56-key layout (4 rows x 14 cols), as wired through the
// TCA8418. NULL/0 cells hold a non-ASCII key; see s_keycode_map for those.
static const sys_kbd_keycode_t s_keycode_map[4][14] = {
    { SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII,
      SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII,
      SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII, SYS_KBD_KEY_BACKSPACE },
    { SYS_KBD_KEY_TAB,   SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII,
      SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII,
      SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII },
    { SYS_KBD_KEY_SHIFT, SYS_KBD_KEY_CAPSLOCK, SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII,
      SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII,
      SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ENTER },
    { SYS_KBD_KEY_CTRL,  SYS_KBD_KEY_OPT,   SYS_KBD_KEY_ALT,   SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII,
      SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII,
      SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII, SYS_KBD_KEY_ASCII },
};

static const char s_ascii_map[4][14] = {
    { '`', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0 },
    { 0,   'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\\' },
    { 0,   0,   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', 0 },
    { 0,   0,   0,   'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', ' ' },
};

static const char s_ascii_map_shift[4][14] = {
    { '~', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 0 },
    { 0,   'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '|' },
    { 0,   0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', 0 },
    { 0,   0,   0,   'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', ' ' },
};

static QueueHandle_t s_evt_queue;
static TaskHandle_t s_task_handle;
static bool s_shift_held = false;

// Remaps the TCA8418's 7x8 electrical matrix (row 0-6, col 0-7) onto
// Cardputer's 4x14 physical key layout.
static void sys_kbd_electrical_to_physical(uint8_t tca_row, uint8_t tca_col, uint8_t *out_row, uint8_t *out_col)
{
    *out_col = tca_row * 2 + ((tca_col > 3) ? 1 : 0);
    *out_row = (tca_col + 4) % 4;
}

static void IRAM_ATTR sys_kbd_isr(void *arg)
{
    BaseType_t higher_prio_task_woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_task_handle, &higher_prio_task_woken);
    portYIELD_FROM_ISR(higher_prio_task_woken);
}

static void sys_kbd_task(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        drv_tca8418_key_event_t raw;
        bool has_event;
        while (drv_tca8418_read_event(&raw, &has_event) == ESP_OK && has_event) {
            uint8_t row, col;
            sys_kbd_electrical_to_physical(raw.row, raw.col, &row, &col);
            if (row >= 4 || col >= 14) {
                continue;
            }

            sys_kbd_keycode_t keycode = s_keycode_map[row][col];
            if (keycode == SYS_KBD_KEY_SHIFT) {
                s_shift_held = raw.pressed;
            }

            sys_kbd_event_t evt = {
                .keycode = keycode,
                .ascii = (keycode == SYS_KBD_KEY_ASCII)
                             ? (s_shift_held ? s_ascii_map_shift[row][col] : s_ascii_map[row][col])
                             : 0,
                .pressed = raw.pressed,
                .row = row,
                .col = col,
            };
            if (xQueueSend(s_evt_queue, &evt, 0) != pdTRUE) {
                ESP_LOGW(TAG, "event queue full, dropping event");
            }
        }

        drv_tca8418_clear_interrupt();
    }
}

esp_err_t sys_kbd_init(void)
{
    if (s_evt_queue != NULL) {
        return ESP_OK;
    }

    esp_err_t err = bsp_gpio_init();
    if (err != ESP_OK) {
        return err;
    }

    err = drv_tca8418_init();
    if (err != ESP_OK) {
        return err;
    }

    s_evt_queue = xQueueCreate(SYS_KBD_QUEUE_LEN, sizeof(sys_kbd_event_t));
    if (s_evt_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(sys_kbd_task, "sys_kbd", SYS_KBD_TASK_STACK, NULL, SYS_KBD_TASK_PRIO, &s_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "failed to create sys_kbd task");
        return ESP_ERR_NO_MEM;
    }

    err = bsp_gpio_isr_register((gpio_num_t)CONFIG_DRV_TCA8418_INT_GPIO, GPIO_INTR_NEGEDGE, sys_kbd_isr, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bsp_gpio_isr_register failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "keyboard service init done (int_gpio=%d)", CONFIG_DRV_TCA8418_INT_GPIO);
    return ESP_OK;
}

esp_err_t sys_kbd_read_event(sys_kbd_event_t *out_event, TickType_t timeout)
{
    if (s_evt_queue == NULL || out_event == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return (xQueueReceive(s_evt_queue, out_event, timeout) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}
