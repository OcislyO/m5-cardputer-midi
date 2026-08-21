#include "drv_bmi270.h"
#include "bmi270.h"
#include "bsp_i2c.h"
#include "esp_log.h"

static const char *TAG = "drv_bmi270";

static bmi270_handle_t *s_handle;

esp_err_t drv_bmi270_init(void)
{
    if (s_handle != NULL) {
        return ESP_OK;
    }

    bmi270_driver_config_t drv_cfg = {
        .addr = CONFIG_DRV_BMI270_I2C_ADDR,
        .interface = BMI270_USE_I2C,
        .i2c_bus = bsp_i2c_get_bus_handle(),
    };
    esp_err_t err = bmi270_create(&drv_cfg, &s_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bmi270_create failed: %s", esp_err_to_name(err));
        return err;
    }

    bmi270_config_t meas_cfg = {
        .acce_odr = BMI270_ACC_ODR_100_HZ,
        .acce_range = BMI270_ACC_RANGE_4_G,
        .gyro_odr = BMI270_GYR_ODR_100_HZ,
        .gyro_range = BMI270_GYR_RANGE_1000_DPS,
    };
    err = bmi270_start(s_handle, &meas_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bmi270_start failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "init done (addr=0x%02x)", CONFIG_DRV_BMI270_I2C_ADDR);
    return ESP_OK;
}

esp_err_t drv_bmi270_get_accel(float *x, float *y, float *z)
{
    if (s_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return bmi270_get_acce_data(s_handle, x, y, z);
}

esp_err_t drv_bmi270_get_gyro(float *x, float *y, float *z)
{
    if (s_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return bmi270_get_gyro_data(s_handle, x, y, z);
}
