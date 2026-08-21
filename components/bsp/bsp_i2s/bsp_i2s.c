#include "bsp_i2s.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include <inttypes.h>

static const char *TAG = "bsp_i2s";

static i2s_chan_handle_t s_tx_chan;
static i2s_chan_handle_t s_rx_chan;
static bool s_initialized = false;

esp_err_t bsp_i2s_init(uint32_t sample_rate_hz)
{
    if (s_initialized) {
        return ESP_OK;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&chan_cfg, &s_tx_chan, &s_rx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(err));
        return err;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate_hz),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)CONFIG_BSP_I2S_SCK_GPIO,
            .ws = (gpio_num_t)CONFIG_BSP_I2S_LRCK_GPIO,
            .dout = (gpio_num_t)CONFIG_BSP_I2S_DSDIN_GPIO,
            .din = (gpio_num_t)CONFIG_BSP_I2S_ASDOUT_GPIO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    err = i2s_channel_init_std_mode(s_tx_chan, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tx channel init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = i2s_channel_init_std_mode(s_rx_chan, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rx channel init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = i2s_channel_enable(s_tx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tx channel enable failed: %s", esp_err_to_name(err));
        return err;
    }

    err = i2s_channel_enable(s_rx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rx channel enable failed: %s", esp_err_to_name(err));
        return err;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "i2s init done (rate=%" PRIu32 " sck=%d lrck=%d dsdin=%d asdout=%d)",
             sample_rate_hz, CONFIG_BSP_I2S_SCK_GPIO, CONFIG_BSP_I2S_LRCK_GPIO,
             CONFIG_BSP_I2S_DSDIN_GPIO, CONFIG_BSP_I2S_ASDOUT_GPIO);
    return ESP_OK;
}

esp_err_t bsp_i2s_write(const void *data, size_t len, size_t *bytes_written, uint32_t timeout_ms)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2s_channel_write(s_tx_chan, data, len, bytes_written, timeout_ms);
}

esp_err_t bsp_i2s_read(void *data, size_t len, size_t *bytes_read, uint32_t timeout_ms)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2s_channel_read(s_rx_chan, data, len, bytes_read, timeout_ms);
}

esp_err_t bsp_i2s_register_tx_done_cb(i2s_isr_callback_t cb, void *user_ctx)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    i2s_event_callbacks_t cbs = {
        .on_sent = cb,
    };
    return i2s_channel_register_event_callback(s_tx_chan, &cbs, user_ctx);
}

esp_err_t bsp_i2s_register_rx_done_cb(i2s_isr_callback_t cb, void *user_ctx)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    i2s_event_callbacks_t cbs = {
        .on_recv = cb,
    };
    return i2s_channel_register_event_callback(s_rx_chan, &cbs, user_ctx);
}
