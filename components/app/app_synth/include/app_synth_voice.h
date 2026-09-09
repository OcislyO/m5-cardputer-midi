#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "app_synth.h"
#include "app_synth_env.h"
#include "app_synth_op.h"
#include "app_synth_osc.h"
#include "app_synth_track.h"

#define MAX_VOICE_COUNT     20

typedef enum {
    VOICE_STATE_FREE = 0,   // 没有被占用
    VOICE_STATE_ON,         // 被触发中
    VOICE_STATE_OFF,        // 正在release
} voice_state_t;

typedef struct app_synth_voice_s
{
    app_synth_track_t *from_track;
    app_synth_op_t op[MAX_OPERATOR_COUNT];

    float freq;

    voice_state_t state;
} app_synth_voice_t;

extern app_synth_voice_t app_synth_voice_pool[MAX_VOICE_COUNT];

app_synth_voice_t *app_synth_voice_on(uint8_t track, float freq);
esp_err_t app_synth_voice_off(app_synth_voice_t *voice);
int16_t app_synth_voice_sample(app_synth_voice_t *voice);

#ifdef __cplusplus
}
#endif