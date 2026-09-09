#include "app_synth_env.h"
#include "app_synth_osc.h"
#include "app_synth_track.h"
#include "app_synth_voice.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

app_synth_voice_t app_synth_voice_pool[MAX_VOICE_COUNT];
extern app_synth_voice_t *note_map[MAX_TRACK_COUNT][128];  // 记录note->voice

static app_synth_voice_t *app_synth_voice_alloc() {
    uint8_t min_level_index = 0;
    int8_t min_release_index = -1;
    for (size_t i = 0; i < MAX_VOICE_COUNT; i++)
    {
        if (app_synth_voice_pool[i].state == VOICE_STATE_FREE)
            return &app_synth_voice_pool[i];

        if (min_release_index < 0)
        {
            if (app_synth_voice_pool[i].state == VOICE_STATE_OFF)
                min_release_index = i;  // 标记正在释放的音

            if (app_synth_voice_pool[i].freq < app_synth_voice_pool[min_level_index].freq) {
                min_level_index = i;  // 标记频率最低的音
            }
        } else {
            if (app_synth_voice_pool[i].freq < app_synth_voice_pool[min_release_index].freq && 
                app_synth_voice_pool[i].state == VOICE_STATE_OFF)
                min_release_index = i; // 标记正在释放的最低频率
        }
    }

    app_synth_voice_t *voice;
    if (min_release_index < 0)
        voice = &app_synth_voice_pool[min_level_index];  // 没找到正在释放的就用最低频率
    else
        voice = &app_synth_voice_pool[min_release_index];

    for (size_t i = 0; i < 128; i++)
    {
        if (note_map[voice->from_track->midi_channel][i] == voice)
            note_map[voice->from_track->midi_channel][i] = NULL;  // 偷音前从note map解除绑定，防止错误关闭新的voice。
    }
    
    voice->from_track->voice_count -= 1;
    voice->state = VOICE_STATE_FREE;  // 池里没有空闲的voice就释放正在off的。
    return voice;
}


/// @brief free后并不会直接释放掉，而是包络进入release，当level归零后由engine释放
/// @param track_id 
/// @param voice 
/// @return 
static esp_err_t app_synth_voice_free(app_synth_voice_t *voice) {
    voice->state = VOICE_STATE_OFF;

    return ESP_OK;
}

app_synth_voice_t *app_synth_voice_on(uint8_t track, float freq) {

    app_synth_voice_t *voice = app_synth_voice_alloc();
    if (voice) {
        voice->from_track = &track_list[track];
        voice->freq = freq;
        for (size_t i = 0; i < MAX_OPERATOR_COUNT; i++)
        {
            app_synth_op_set(&voice->op[i], 
                &voice->from_track->op_env[i], 
                voice->from_track->op_wave[i], 
                voice->freq, 
                voice->from_track->op_coarse[i], 
                voice->from_track->op_level[i]);
        }

        voice->state = VOICE_STATE_ON;
        voice->from_track->voice_count += 1;
    }

    return voice;
}

esp_err_t app_synth_voice_off(app_synth_voice_t *voice) {
    esp_err_t err = app_synth_voice_free(voice);
    return err;
}


int16_t app_synth_voice_sample(app_synth_voice_t *voice) {
    uint8_t env_sum = 0;
    int32_t sample = 0;
    uint8_t out_count = 0;
    for (size_t i = 0; i < MAX_OPERATOR_COUNT; i++)
    {
        voice->op[i].phase += voice->op[i].phase_inc;  // 算子步进

        if (! voice->from_track->fm_metrix[i]) {
            //输出
            int32_t value = app_synth_wavetable_sample(voice->op[i].osc.wave, voice->op[i].phase);
            value *= voice->op[i].env_level;
            value *= voice->op[i].level;
            sample += value;
            out_count ++;
        }

        for (size_t j = i; j < MAX_OPERATOR_COUNT; j++)
        {   // 调制
            if (voice->from_track->fm_metrix[i] & (0x01 << j))
                app_synth_op_modulate(&voice->op[j], &voice->op[i]);
        }

        if (voice->state == VOICE_STATE_OFF) {
            voice->op[i].env_state = env_state_release;
        }

        app_synth_env_update(&voice->op[i]);  // 更新算子的包络
        env_sum += (uint8_t)voice->op->env_state;
    }
    if (env_sum == 0) {
        voice->state = VOICE_STATE_FREE;
        voice->from_track->voice_count -= 1;
    }
        
    sample /= out_count;
    return (int16_t)sample;
}