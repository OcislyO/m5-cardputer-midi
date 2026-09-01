#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "app_synth.h"

//phase不是真实的物理角度，而是wave tab的索引，即物理相位=phase/256（波表长度）*2pi
typedef struct {
    app_synth_wave_t wave;
    float freq;
    int32_t phase_inc;
} app_synth_osc_t;

esp_err_t app_synth_osc_set (app_synth_osc_t *osc, app_synth_wave_t wave, float freq);

#ifdef __cplusplus
}
#endif