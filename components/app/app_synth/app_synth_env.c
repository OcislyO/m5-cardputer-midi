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

void app_synth_env_update (app_synth_voice_t *voice) {
    switch (voice->env_state)
    {
    case env_state_attack:
        voice->level += voice->from_track->env.attack;
        if (voice->level > 1)
            voice->env_state = env_state_decay;
        break;
        
    case env_state_decay:
        voice->level -= voice->from_track->env.decay;
        if (voice->level <= voice->from_track->env.sustain) {
            voice->level = voice->from_track->env.sustain;
            voice->env_state = env_state_sustain;
        }
            
        break;
        
    case env_state_sustain:
        /* code */
        break;
        
    case env_state_release:
        voice->level -= voice->from_track->env.release;
        if (voice->level <= 0) {
            voice->level = 0;
            voice->env_state = env_state_idle;  // 标记为以释放
            voice->from_track->voice_count -= 1;
        }
        break;
    
    default:
        break;
    }
}