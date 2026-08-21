#include "sys_dsp.h"
#include "sys_dsp_priv.h"

uint16_t sys_dsp_send_buff[SYS_DSP_MAX_RENDER_PIXELS];

esp_err_t sys_dsp_init(void)
{
    drv_st7789_config_t cfg = {
        .width = SYS_DSP_PANEL_WIDTH,
        .height = SYS_DSP_PANEL_HEIGHT,
        .x_offset = 40,
        .y_offset = 53,
        .madctl = 0x60,        ///< MADCTL value (orientation / RGB-BGR order)
        .spi_clock_hz = 40000000,
    };

    esp_err_t err = drv_st7789_init(&cfg);
    if (err != ESP_OK) {
        return err;
    }

    err = sys_dsp_obj_init();
    if (err != ESP_OK) {
        return err;
    }

    err = sys_dsp_dirty_init();
    if (err != ESP_OK) {
        return err;
    }

    return sys_dsp_render_start();
}
