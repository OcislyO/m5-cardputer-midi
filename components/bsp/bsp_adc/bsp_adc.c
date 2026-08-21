#include "bsp_adc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

static const char *TAG = "bsp_adc";

static adc_oneshot_unit_handle_t s_unit;
static adc_cali_handle_t s_cali;
static adc_unit_t s_unit_id;
static adc_channel_t s_channel;
static bool s_has_cali = false;
static bool s_initialized = false;

esp_err_t bsp_adc_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t err = adc_oneshot_io_to_channel(CONFIG_BSP_ADC_VBAT_GPIO, &s_unit_id, &s_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio %d has no ADC channel: %s", CONFIG_BSP_ADC_VBAT_GPIO, esp_err_to_name(err));
        return err;
    }

    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = s_unit_id,
    };
    err = adc_oneshot_new_unit(&unit_cfg, &s_unit);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit failed: %s", esp_err_to_name(err));
        return err;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = (adc_atten_t)CONFIG_BSP_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(s_unit, s_channel, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_config_channel failed: %s", esp_err_to_name(err));
        return err;
    }

    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = s_unit_id,
        .chan = s_channel,
        .atten = (adc_atten_t)CONFIG_BSP_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "adc calibration unavailable, raw reads only (%s)", esp_err_to_name(err));
        s_has_cali = false;
    } else {
        s_has_cali = true;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "adc init done (gpio=%d unit=%d channel=%d atten=%d cali=%d)",
             CONFIG_BSP_ADC_VBAT_GPIO, s_unit_id, s_channel, CONFIG_BSP_ADC_ATTEN, s_has_cali);
    return ESP_OK;
}

esp_err_t bsp_adc_read_raw(int *out_raw)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    return adc_oneshot_read(s_unit, s_channel, out_raw);
}

esp_err_t bsp_adc_read_voltage_mv(int *out_mv)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_has_cali) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return adc_oneshot_get_calibrated_result(s_unit, s_cali, s_channel, out_mv);
}
