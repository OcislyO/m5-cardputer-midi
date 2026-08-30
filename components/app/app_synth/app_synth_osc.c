#include "app_synth_osc.h"

esp_err_t app_synth_osc_set (app_synth_osc_t *osc, app_synth_wave_t wave, float freq) {
    if (! osc)
        return ESP_ERR_INVALID_ARG;

    osc->wave = wave;
    osc->freq = freq;
    osc->phase_inc = (0x100000000 / APP_SYNTH_SAMPLE_RATE) * freq;

    return ESP_OK;
}
