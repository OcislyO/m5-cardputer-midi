#include "app_synth_env.h"
#include "app_synth_osc.h"
#include "app_synth_track.h"
#include "app_synth_voice.h"
#include "app_synth_priv.h"
#include "freertos/semphr.h"

app_synth_voice_t app_synth_voice_pool[MAX_VOICE_COUNT];
extern app_synth_voice_t *note_map[MAX_TRACK_COUNT][128];  // 记录note->voice

// Caller must hold s_synth_lock -- only called from app_synth_voice_on(),
// which takes it.
static app_synth_voice_t *app_synth_voice_alloc() {
    uint8_t min_level_index = 0;
    for (size_t i = 0; i < MAX_VOICE_COUNT; i++)
    {
        if (app_synth_voice_pool[i].env_state == env_state_idle)
            return &app_synth_voice_pool[i];

        if (app_synth_voice_pool[i].level < app_synth_voice_pool[min_level_index].level) {
            min_level_index = i;
        }
    }

    app_synth_voice_t *voice = &app_synth_voice_pool[min_level_index];

    for (size_t i = 0; i < 128; i++)
    {
        if (note_map[voice->from_track->midi_channel][i] == voice)
            note_map[voice->from_track->midi_channel][i] = NULL;  // 偷音前从note map解除绑定，防止错误关闭新的voice。
    }
    
    voice->from_track->voice_count -= 1;
    voice->env_state = env_state_idle;  // 池里没有空闲的voice就释放掉声音最小的。
    return voice;
}


/// @brief free后并不会直接释放掉，而是包络进入release，当level归零后由engine释放
/// @param track_id 
/// @param voice 
/// @return 
static esp_err_t app_synth_voice_free(app_synth_voice_t *voice) {
    voice->env_state = env_state_release;

    return ESP_OK;
}

app_synth_voice_t *app_synth_voice_on(uint8_t track, float freq) {
    xSemaphoreTake(s_synth_lock, portMAX_DELAY);

    app_synth_voice_t *voice = app_synth_voice_alloc();
    if (voice) {
        voice->from_track = &track_list[track];
        app_synth_osc_set(&voice->osc,voice->from_track->wave, freq);
        for (size_t i = 0; i < MAX_OPERATOR_COUNT; i++)
        {
            app_synth_op_set(&voice->op[i], &voice->osc);
        }

        voice->level = 0;
        voice->env_state = env_state_attack;
        voice->from_track->voice_count += 1;
    }

    xSemaphoreGive(s_synth_lock);
    return voice;
}

esp_err_t app_synth_voice_off(app_synth_voice_t *voice) {
    xSemaphoreTake(s_synth_lock, portMAX_DELAY);
    esp_err_t err = app_synth_voice_free(voice);
    xSemaphoreGive(s_synth_lock);
    return err;
}


int16_t app_synth_voice_sample(app_synth_voice_t *voice) {
    int32_t sample = 0;
    for (size_t i = 0; i < MAX_OPERATOR_COUNT; i++)
    {
        voice->op[i].phase += voice->op[i].phase_inc;  // 算子步进

        if (! voice->from_track->fm_metrix[i]) {
            //输出
            int16_t value = app_synth_wavetable_sample(voice->from_track->wave, voice->op[i].phase);
            sample += value;
        }

        for (size_t j = i; j < MAX_OPERATOR_COUNT; j++)
        {
            if (voice->from_track->fm_metrix[i] & (0x01 << j))
                app_synth_op_modulate(&voice->op[j], &voice->op[i]);
        }
    }
    sample /= 6;
    return (int16_t)sample;
}