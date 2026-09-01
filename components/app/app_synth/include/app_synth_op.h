#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "app_synth.h"
#include "app_synth_osc.h"

#define MAX_OPERATOR_COUNT 6

typedef struct
{
    app_synth_osc_t *osc;

    uint32_t phase;
    int32_t phase_inc;
} app_synth_op_t;

esp_err_t app_synth_op_set (app_synth_op_t *op, app_synth_osc_t *osc);
esp_err_t app_synth_op_modulate (app_synth_op_t *carrier, const app_synth_op_t *modulator);

#ifdef __cplusplus
}
#endif