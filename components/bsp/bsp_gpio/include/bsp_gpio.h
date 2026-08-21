#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One-time bsp_gpio setup (installs the shared GPIO ISR service).
 *        Safe to call more than once.
 */
esp_err_t bsp_gpio_init(void);

/**
 * @brief Configure a GPIO's mode/pulls/interrupt type. Thin wrapper over gpio_config().
 */
esp_err_t bsp_gpio_config(const gpio_config_t *cfg);

/**
 * @brief Configure `pin` as an interrupt input and register its ISR handler.
 *        bsp_gpio_init() must have been called first.
 */
esp_err_t bsp_gpio_isr_register(gpio_num_t pin, gpio_int_type_t intr_type,
                                 gpio_isr_t isr_handler, void *arg);

/**
 * @brief Remove a handler registered with bsp_gpio_isr_register().
 */
esp_err_t bsp_gpio_isr_unregister(gpio_num_t pin);

/**
 * @brief Drive `pin` as a simple PWM output (auto-allocates a free LEDC timer/channel).
 * @param pin         Output GPIO.
 * @param freq_hz     PWM frequency in Hz.
 * @param duty_pct    Initial duty cycle, 0-100.
 * @param out_channel Receives the allocated LEDC channel, needed for later duty updates.
 */
esp_err_t bsp_gpio_pwm_register(gpio_num_t pin, uint32_t freq_hz, uint8_t duty_pct,
                                 ledc_channel_t *out_channel);

/**
 * @brief Update the duty cycle of a channel from bsp_gpio_pwm_register().
 * @param duty_pct Duty cycle, 0-100.
 */
esp_err_t bsp_gpio_pwm_set_duty(ledc_channel_t channel, uint8_t duty_pct);

/**
 * @brief Stop a channel from bsp_gpio_pwm_register() and free its slot.
 */
esp_err_t bsp_gpio_pwm_unregister(ledc_channel_t channel);

#ifdef __cplusplus
}
#endif
