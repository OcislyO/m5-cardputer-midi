#include "app_core.h"
#include "sys_nvs.h"
#include "bsp_i2c.h"
#include "bsp_spi.h"
#include "bsp_gpio.h"
#include "esp_log.h"
#include "sys_dsp.h"
#include "sys_bat.h"
#include "app_ui.h"

static const char *TAG = "app";

void app_start(void)
{
    ESP_ERROR_CHECK(sys_nvs_init());
    ESP_ERROR_CHECK(bsp_gpio_init());
    ESP_ERROR_CHECK(bsp_i2c_init());
    ESP_ERROR_CHECK(bsp_spi_init());

    ESP_ERROR_CHECK(sys_dsp_init());
    ESP_ERROR_CHECK(sys_bat_init());
    ESP_ERROR_CHECK(app_ui_start());

    // TODO: MIDI keyboard application logic goes here.
    ESP_LOGI(TAG, "m5_midi app started");
}
