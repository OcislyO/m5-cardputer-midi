#include "drv_st7789.h"
#include "bsp_spi.h"
#include "bsp_gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
// ST7789 command set (subset used here)
#define ST7789_CMD_SWRESET 0x01
#define ST7789_CMD_SLPOUT  0x11
#define ST7789_CMD_COLMOD  0x3A
#define ST7789_CMD_MADCTL  0x36
#define ST7789_CMD_INVON   0x21
#define ST7789_CMD_NORON   0x13
#define ST7789_CMD_DISPON  0x29
#define ST7789_CMD_CASET   0x2A
#define ST7789_CMD_RASET   0x2B
#define ST7789_CMD_RAMWR   0x2C

#define ST7789_COLMOD_16BPP 0x55

static const char *TAG = "drv_st7789";

static spi_device_handle_t s_spi_dev;
static ledc_channel_t s_bl_channel;
static drv_st7789_config_t s_cfg;
static bool s_initialized = false;

esp_err_t drv_st7789_fill(uint16_t rgb);

static esp_err_t st7789_write(bool is_data, const uint8_t *bytes, size_t len)
{
    if (len == 0) {
        return ESP_OK;
    }

    esp_err_t err = gpio_set_level((gpio_num_t)CONFIG_DRV_ST7789_DC_GPIO, is_data ? 1 : 0);
    if (err != ESP_OK) {
        return err;
    }

    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = bytes,
    };
    return spi_device_transmit(s_spi_dev, &t);
}

static esp_err_t st7789_send_cmd(uint8_t cmd)
{
    return st7789_write(false, &cmd, 1);
}

static esp_err_t st7789_send_cmd_data(uint8_t cmd, const uint8_t *data, size_t len)
{
    esp_err_t err = st7789_send_cmd(cmd);
    if (err != ESP_OK) {
        return err;
    }
    return st7789_write(true, data, len);
}

static esp_err_t st7789_reset(void)
{
    esp_err_t err = gpio_set_level((gpio_num_t)CONFIG_DRV_ST7789_RST_GPIO, 0);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    err = gpio_set_level((gpio_num_t)CONFIG_DRV_ST7789_RST_GPIO, 1);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(150));
    return ESP_OK;
}

esp_err_t drv_st7789_init(const drv_st7789_config_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_initialized) {
        return ESP_OK;
    }
    s_cfg = *cfg;

    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << CONFIG_DRV_ST7789_RST_GPIO) | (1ULL << CONFIG_DRV_ST7789_DC_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = bsp_gpio_config(&io_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rst/dc gpio_config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = bsp_gpio_pwm_register((gpio_num_t)CONFIG_DRV_ST7789_BL_GPIO, 5000, 0, &s_bl_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "backlight pwm register failed: %s", esp_err_to_name(err));
        return err;
    }

    spi_device_interface_config_t dev_cfg = {
        .mode = 0,
        .clock_speed_hz = s_cfg.spi_clock_hz,
        .spics_io_num = CONFIG_BSP_SPI_CS_GPIO,
        .queue_size = 7,
    };
    err = bsp_spi_add_device(&dev_cfg, &s_spi_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bsp_spi_add_device failed: %s", esp_err_to_name(err));
        return err;
    }

    err = st7789_reset();
    if (err != ESP_OK) {
        return err;
    }

    err = st7789_send_cmd(ST7789_CMD_SWRESET);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(150));

    err = st7789_send_cmd(ST7789_CMD_SLPOUT);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(120));

    uint8_t colmod = ST7789_COLMOD_16BPP;
    err = st7789_send_cmd_data(ST7789_CMD_COLMOD, &colmod, 1);
    if (err != ESP_OK) {
        return err;
    }

    err = st7789_send_cmd_data(ST7789_CMD_MADCTL, &s_cfg.madctl, 1);
    if (err != ESP_OK) {
        return err;
    }

    err = st7789_send_cmd(ST7789_CMD_INVON);
    if (err != ESP_OK) {
        return err;
    }

    err = st7789_send_cmd(ST7789_CMD_NORON);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    err = st7789_send_cmd(ST7789_CMD_DISPON);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    s_initialized = true;

    drv_st7789_fill(0x0000);

    err = drv_st7789_set_backlight(100);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "init done (%ux%u, rst=%d dc=%d bl=%d)",
             s_cfg.width, s_cfg.height, CONFIG_DRV_ST7789_RST_GPIO,
             CONFIG_DRV_ST7789_DC_GPIO, CONFIG_DRV_ST7789_BL_GPIO);
    return ESP_OK;
}

esp_err_t drv_st7789_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    x0 += s_cfg.x_offset;
    x1 += s_cfg.x_offset;
    y0 += s_cfg.y_offset;
    y1 += s_cfg.y_offset;

    uint8_t caset[4] = { x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF };
    esp_err_t err = st7789_send_cmd_data(ST7789_CMD_CASET, caset, sizeof(caset));
    if (err != ESP_OK) {
        return err;
    }

    uint8_t raset[4] = { y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF };
    err = st7789_send_cmd_data(ST7789_CMD_RASET, raset, sizeof(raset));
    if (err != ESP_OK) {
        return err;
    }

    return st7789_send_cmd(ST7789_CMD_RAMWR);
}

esp_err_t drv_st7789_send_data(const uint8_t *data, size_t len)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    return st7789_write(true, data, len);
}

esp_err_t drv_st7789_set_backlight(uint8_t duty_pct)
{
    return bsp_gpio_pwm_set_duty(s_bl_channel, duty_pct);
}

esp_err_t drv_st7789_fill(uint16_t rgb) {
    uint16_t buff[240 * 5];
    rgb = (rgb << 8) + (rgb >> 8);
    for (size_t i = 0; i < 240 * 5; i++)
    {
        buff[i] = rgb;
    }
    
    for (size_t i = 0; i < 135; i += 5)
    {
        drv_st7789_set_window(0, i, 239, i + 5);
        drv_st7789_send_data((uint8_t *)buff, sizeof(buff));
    }
    
    
    return ESP_OK;
}