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

// Forward declaration only: app_synth_env_update() only needs a pointer here.
// Do NOT #include "app_synth_voice.h" -- app_synth_voice_t itself embeds an
// app_synth_env_state by value, so that header depends on this one; including
// it back here would create a genuine circular #include.
typedef struct app_synth_voice_s app_synth_voice_t;

esp_err_t app_synth_env_set (app_synth_env_t *env, float A, float D, float S, float R);
void app_synth_env_update (app_synth_voice_t *voice);

#ifdef __cplusplus
}
#endif