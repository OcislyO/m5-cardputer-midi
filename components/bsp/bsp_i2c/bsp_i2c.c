#include "bsp_i2c.h"
#include "esp_log.h"

static const char *TAG = "bsp_i2c";
static i2c_master_bus_handle_t s_i2c_bus = NULL;

esp_err_t bsp_i2c_init(void)
{
    if (s_i2c_bus != NULL) {
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = CONFIG_BSP_I2C_PORT,
        .sda_io_num = CONFIG_BSP_I2C_SDA_GPIO,
        .scl_io_num = CONFIG_BSP_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "i2c bus init done (port=%d sda=%d scl=%d)",
             CONFIG_BSP_I2C_PORT, CONFIG_BSP_I2C_SDA_GPIO, CONFIG_BSP_I2C_SCL_GPIO);
    return ESP_OK;
}

i2c_master_bus_handle_t bsp_i2c_get_bus_handle(void)
{
    return s_i2c_bus;
}

esp_err_t bsp_i2c_add_device(const i2c_device_config_t *dev_cfg, i2c_master_dev_handle_t *out_handle)
{
    if (s_i2c_bus == NULL) {
        ESP_LOGE(TAG, "i2c bus not initialized, call bsp_i2c_init() first");
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_bus_add_device(s_i2c_bus, dev_cfg, out_handle);
}

esp_err_t bsp_i2c_remove_device(i2c_master_dev_handle_t handle)
{
    return i2c_master_bus_rm_device(handle);
}
