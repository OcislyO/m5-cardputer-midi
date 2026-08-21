#include "bsp_gpio.h"
#include "esp_log.h"
#include <inttypes.h>
#include <stdbool.h>

static const char *TAG = "bsp_gpio";
static bool s_isr_service_installed = false;

// ESP32-S3 LEDC only has LEDC_LOW_SPEED_MODE, and 4 independent timers -- pairing
// one channel to one timer keeps every registered PWM output fully independent.
#define BSP_GPIO_PWM_SPEED_MODE   LEDC_LOW_SPEED_MODE
#define BSP_GPIO_PWM_DUTY_RES     LEDC_TIMER_10_BIT
#define BSP_GPIO_PWM_MAX_CHANNELS 4

static bool s_pwm_used[BSP_GPIO_PWM_MAX_CHANNELS];

esp_err_t bsp_gpio_init(void)
{
    if (s_isr_service_installed) {
        return ESP_OK;
    }

    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(err));
        return err;
    }

    s_isr_service_installed = true;
    ESP_LOGI(TAG, "gpio init done");
    return ESP_OK;
}

esp_err_t bsp_gpio_config(const gpio_config_t *cfg)
{
    return gpio_config(cfg);
}

esp_err_t bsp_gpio_isr_register(gpio_num_t pin, gpio_int_type_t intr_type,
                                 gpio_isr_t isr_handler, void *arg)
{
    if (!s_isr_service_installed) {
        ESP_LOGE(TAG, "gpio isr service not installed, call bsp_gpio_init() first");
        return ESP_ERR_INVALID_STATE;
    }

    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = intr_type,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed for gpio %d: %s", pin, esp_err_to_name(err));
        return err;
    }

    return gpio_isr_handler_add(pin, isr_handler, arg);
}

esp_err_t bsp_gpio_isr_unregister(gpio_num_t pin)
{
    return gpio_isr_handler_remove(pin);
}

static int bsp_gpio_pwm_alloc_slot(void)
{
    for (int i = 0; i < BSP_GPIO_PWM_MAX_CHANNELS; i++) {
        if (!s_pwm_used[i]) {
            return i;
        }
    }
    return -1;
}

esp_err_t bsp_gpio_pwm_register(gpio_num_t pin, uint32_t freq_hz, uint8_t duty_pct,
                                 ledc_channel_t *out_channel)
{
    if (duty_pct > 100 || out_channel == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int slot = bsp_gpio_pwm_alloc_slot();
    if (slot < 0) {
        ESP_LOGE(TAG, "no free PWM channel/timer slot (max %d)", BSP_GPIO_PWM_MAX_CHANNELS);
        return ESP_ERR_NO_MEM;
    }

    ledc_timer_config_t timer_cfg = {
        .speed_mode = BSP_GPIO_PWM_SPEED_MODE,
        .duty_resolution = BSP_GPIO_PWM_DUTY_RES,
        .timer_num = (ledc_timer_t)slot,
        .freq_hz = freq_hz,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config failed: %s", esp_err_to_name(err));
        return err;
    }

    uint32_t max_duty = (1u << BSP_GPIO_PWM_DUTY_RES) - 1;
    ledc_channel_config_t channel_cfg = {
        .gpio_num = pin,
        .speed_mode = BSP_GPIO_PWM_SPEED_MODE,
        .channel = (ledc_channel_t)slot,
        .timer_sel = (ledc_timer_t)slot,
        .duty = (max_duty * duty_pct) / 100,
        .hpoint = 0,
        .intr_type = LEDC_INTR_DISABLE,
    };
    err = ledc_channel_config(&channel_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config failed: %s", esp_err_to_name(err));
        return err;
    }

    s_pwm_used[slot] = true;
    *out_channel = (ledc_channel_t)slot;
    ESP_LOGI(TAG, "pwm registered on gpio=%d channel=%d freq=%" PRIu32 "Hz duty=%u%%",
             pin, slot, freq_hz, duty_pct);
    return ESP_OK;
}

esp_err_t bsp_gpio_pwm_set_duty(ledc_channel_t channel, uint8_t duty_pct)
{
    if (duty_pct > 100 || channel >= BSP_GPIO_PWM_MAX_CHANNELS || !s_pwm_used[channel]) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t max_duty = (1u << BSP_GPIO_PWM_DUTY_RES) - 1;
    uint32_t duty = (max_duty * duty_pct) / 100;

    esp_err_t err = ledc_set_duty(BSP_GPIO_PWM_SPEED_MODE, channel, duty);
    if (err != ESP_OK) {
        return err;
    }
    return ledc_update_duty(BSP_GPIO_PWM_SPEED_MODE, channel);
}

esp_err_t bsp_gpio_pwm_unregister(ledc_channel_t channel)
{
    if (channel >= BSP_GPIO_PWM_MAX_CHANNELS || !s_pwm_used[channel]) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ledc_stop(BSP_GPIO_PWM_SPEED_MODE, channel, 0);
    s_pwm_used[channel] = false;
    return err;
}
