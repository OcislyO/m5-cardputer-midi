#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "app_synth.h"

typedef struct
{
    float attack;
    float decay;
    float sustain;
    float release;
} app_synth_env_t;

typedef enum {
    env_state_idle = 0,
    env_state_attack,
    env_state_decay,
    env_state_sustain,
    env_state_release
} app_synth_env_state;

esp_err_t app_synth_env_set (app_synth_env_t *env, float A, float D, float S, float R);

#ifdef __cplusplus
}
#endif