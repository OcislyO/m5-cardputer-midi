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

// Forward declarations only: app_synth_env_update() only needs a pointer to
// each of these. Do NOT #include "app_synth_op.h" or "app_synth_voice.h" --
// app_synth_op_t embeds an app_synth_env_t* and an app_synth_env_state *by
// value*, and app_synth_voice_t embeds app_synth_op_t by value, so both
// headers depend on this one; including either back here would create a
// genuine circular #include.
typedef struct app_synth_op_s app_synth_op_t;
typedef struct app_synth_voice_s app_synth_voice_t;

esp_err_t app_synth_env_set (app_synth_env_t *env, float A, float D, float S, float R);
void app_synth_env_update (app_synth_op_t *op);

#ifdef __cplusplus
}
#endif