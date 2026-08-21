#include "drv_tca8418.h"
#include "bsp_i2c.h"
#include "esp_log.h"

// TCA8418 register map (TI datasheet)
#define TCA8418_REG_CFG         0x01
#define TCA8418_REG_INT_STAT    0x02
#define TCA8418_REG_KEY_LCK_EC  0x03
#define TCA8418_REG_KEY_EVENT_A 0x04
#define TCA8418_REG_KP_GPIO1    0x1D
#define TCA8418_REG_KP_GPIO2    0x1E
#define TCA8418_REG_KP_GPIO3    0x1F

#define TCA8418_CFG_AI     0x01 // auto-increment on multi-byte reads
#define TCA8418_CFG_KE_IEN 0x80 // key-event interrupt enable

#define TCA8418_INT_STAT_K_INT 0x01

#define TCA8418_KEY_LCK_EC_COUNT_MASK 0x0F

// TCA8418 always uses a 10-wide internal column stride when encoding key
// numbers, regardless of how many columns are actually wired up.
#define TCA8418_KEY_NUM_COL_STRIDE 10

static const char *TAG = "drv_tca8418";

static i2c_master_dev_handle_t s_dev;
static bool s_initialized = false;

static esp_err_t tca8418_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), -1);
}

static esp_err_t tca8418_read_reg(uint8_t reg, uint8_t *out_val)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, out_val, 1, -1);
}

static esp_err_t tca8418_flush_fifo(void)
{
    uint8_t lck_ec;
    esp_err_t err = tca8418_read_reg(TCA8418_REG_KEY_LCK_EC, &lck_ec);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t pending = lck_ec & TCA8418_KEY_LCK_EC_COUNT_MASK;
    for (uint8_t i = 0; i < pending; i++) {
        uint8_t discard;
        err = tca8418_read_reg(TCA8418_REG_KEY_EVENT_A, &discard);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t drv_tca8418_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = CONFIG_DRV_TCA8418_I2C_ADDR,
        .scl_speed_hz = CONFIG_DRV_TCA8418_I2C_SPEED_HZ,
    };
    esp_err_t err = bsp_i2c_add_device(&dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bsp_i2c_add_device failed: %s", esp_err_to_name(err));
        return err;
    }

    uint8_t row_mask = (1u << CONFIG_DRV_TCA8418_ROWS) - 1;
    err = tca8418_write_reg(TCA8418_REG_KP_GPIO1, row_mask);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t col0_7_mask = (1u << (CONFIG_DRV_TCA8418_COLS > 8 ? 8 : CONFIG_DRV_TCA8418_COLS)) - 1;
    err = tca8418_write_reg(TCA8418_REG_KP_GPIO2, col0_7_mask);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t col8_9_mask = (CONFIG_DRV_TCA8418_COLS > 8) ? ((1u << (CONFIG_DRV_TCA8418_COLS - 8)) - 1) : 0;
    err = tca8418_write_reg(TCA8418_REG_KP_GPIO3, col8_9_mask);
    if (err != ESP_OK) {
        return err;
    }

    err = tca8418_flush_fifo();
    if (err != ESP_OK) {
        return err;
    }

    err = tca8418_write_reg(TCA8418_REG_INT_STAT, TCA8418_INT_STAT_K_INT);
    if (err != ESP_OK) {
        return err;
    }

    err = tca8418_write_reg(TCA8418_REG_CFG, TCA8418_CFG_AI | TCA8418_CFG_KE_IEN);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cfg write failed: %s", esp_err_to_name(err));
        return err;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "init done (addr=0x%02x rows=%d cols=%d int_gpio=%d)",
             CONFIG_DRV_TCA8418_I2C_ADDR, CONFIG_DRV_TCA8418_ROWS,
             CONFIG_DRV_TCA8418_COLS, CONFIG_DRV_TCA8418_INT_GPIO);
    return ESP_OK;
}

esp_err_t drv_tca8418_read_event(drv_tca8418_key_event_t *out_event, bool *out_has_event)
{
    if (!s_initialized || out_event == NULL || out_has_event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t lck_ec;
    esp_err_t err = tca8418_read_reg(TCA8418_REG_KEY_LCK_EC, &lck_ec);
    if (err != ESP_OK) {
        return err;
    }

    if ((lck_ec & TCA8418_KEY_LCK_EC_COUNT_MASK) == 0) {
        *out_has_event = false;
        return ESP_OK;
    }

    uint8_t raw;
    err = tca8418_read_reg(TCA8418_REG_KEY_EVENT_A, &raw);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t key_num = raw & 0x7F;
    if (key_num == 0) {
        *out_has_event = false;
        return ESP_OK;
    }

    out_event->pressed = (raw & 0x80) != 0;
    out_event->row = (key_num - 1) / TCA8418_KEY_NUM_COL_STRIDE;
    out_event->col = (key_num - 1) % TCA8418_KEY_NUM_COL_STRIDE;
    *out_has_event = true;
    return ESP_OK;
}

esp_err_t drv_tca8418_clear_interrupt(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    return tca8418_write_reg(TCA8418_REG_INT_STAT, TCA8418_INT_STAT_K_INT);
}
