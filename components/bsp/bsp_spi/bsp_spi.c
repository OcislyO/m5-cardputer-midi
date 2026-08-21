#include "bsp_spi.h"
#include "esp_log.h"

static const char *TAG = "bsp_spi";
static bool s_spi_initialized = false;

spi_host_device_t bsp_spi_get_host(void)
{
    return (spi_host_device_t)CONFIG_BSP_SPI_HOST;
}

esp_err_t bsp_spi_init(void)
{
    if (s_spi_initialized) {
        return ESP_OK;
    }

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = CONFIG_BSP_SPI_MOSI_GPIO,
        .miso_io_num = -1,
        .sclk_io_num = CONFIG_BSP_SPI_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 0,
    };

    esp_err_t err = spi_bus_initialize(bsp_spi_get_host(), &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return err;
    }

    s_spi_initialized = true;
    ESP_LOGI(TAG, "spi bus init done (host=%d mosi=%d sclk=%d)",
              bsp_spi_get_host(), CONFIG_BSP_SPI_MOSI_GPIO, CONFIG_BSP_SPI_SCLK_GPIO);
    return ESP_OK;
}

esp_err_t bsp_spi_add_device(const spi_device_interface_config_t *dev_cfg, spi_device_handle_t *out_handle)
{
    if (!s_spi_initialized) {
        ESP_LOGE(TAG, "spi bus not initialized, call bsp_spi_init() first");
        return ESP_ERR_INVALID_STATE;
    }
    return spi_bus_add_device(bsp_spi_get_host(), dev_cfg, out_handle);
}

esp_err_t bsp_spi_remove_device(spi_device_handle_t handle)
{
    return spi_bus_remove_device(handle);
}
