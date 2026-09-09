#include "app_synth_track.h"
#include "app_synth_env.h"
#include "app_synth_voice.h"
#include "app_synth.h"


esp_err_t app_synth_env_set (app_synth_env_t *env, float A, float D, float S, float R) {
    env->attack = A;
    env->decay = D;
    env->sustain = S;
    env->release = R;

    return ESP_OK;
}

void app_synth_env_update (app_synth_op_t *op) {
    switch (op->env_state)
    {
    case env_state_attack:
        op->env_level += op->env->attack;
        if (op->env_level > 1)
            op->env_state = env_state_decay;
        break;
        
    case env_state_decay:
        op->env_level -= op->env->decay;
        if (op->env_level <= op->env->sustain) {
            op->env_level = op->env->sustain;
            op->env_state = env_state_sustain;
        }
            
        break;
        
    case env_state_sustain:
        /* code */
        break;
        
    case env_state_release:
        op->env_level -= op->env->release;
        if (op->env_level <= 0) {
            op->env_level = 0;
            op->env_state = env_state_idle;  // 标记为以释放
        }
        break;
    
    default:
        break;
    }
}