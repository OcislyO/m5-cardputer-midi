#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "app_synth_op.h"
#include "app_synth_env.h"
#include "app_synth_track.h"

#define MAX_VOICE_COUNT     10

typedef struct
{
    app_synth_track_t *from_track;
    app_synth_op_t op[6];

    app_synth_osc_t osc;
    
    app_synth_env_state env_state;
    float level;
} app_synth_voice_t;

extern app_synth_voice_t app_synth_voice_pool[MAX_VOICE_COUNT];

app_synth_voice_t *app_synth_voice_on(uint8_t track, float freq);
esp_err_t app_synth_voice_off(app_synth_voice_t *voice);
int16_t app_synth_voice_sample(app_synth_voice_t *voice);

#ifdef __cplusplus
}
#endif