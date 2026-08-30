#include "app_synth_env.h"

esp_err_t app_synth_env_set (app_synth_env_t *env, float A, float D, float S, float R) {
    env->attack = A;
    env->decay = D;
    env->sustain = S;
    env->release = R;

    return ESP_OK;
}