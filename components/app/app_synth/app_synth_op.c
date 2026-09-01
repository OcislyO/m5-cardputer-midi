#include "app_synth_osc.h"
#include "app_synth_op.h"
#include "app_synth.h"

esp_err_t app_synth_op_set (app_synth_op_t *op, app_synth_osc_t *osc) {
    op->osc = osc;
    op->phase = 0;
    op->phase_inc = osc->phase_inc;

    return ESP_OK;
}

esp_err_t app_synth_op_modulate (app_synth_op_t *carrier, const app_synth_op_t *modulator) {
    if (carrier == NULL ||
        modulator == NULL ||
        carrier->osc == NULL ||
        modulator->osc == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // 取得 modulator 当前输出。
    int16_t mod = app_synth_wavetable_sample(modulator->osc->wave, modulator->phase);

    // modulation 必须和 phase_inc 使用相同单位。
    int32_t fm = mod << 15;

    // 每次都从基础 phase_inc 重新计算。
    carrier->phase_inc = carrier->osc->phase_inc + fm;

    return ESP_OK;
}
