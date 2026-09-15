#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "app_synth.h"
#include "app_synth_env.h"
#include "app_synth_op.h"

#define MAX_TRACK_COUNT 4

typedef struct
{
    uint8_t midi_channel;

    app_synth_wave_t op_wave[MAX_OPERATOR_COUNT];
    float op_level[MAX_OPERATOR_COUNT];
    uint8_t op_coarse[MAX_OPERATOR_COUNT];
    app_synth_env_t op_env[MAX_OPERATOR_COUNT];
    uint8_t fm_metrix[MAX_OPERATOR_COUNT];

    float voice_level;
    uint8_t voice_count;
} app_synth_track_t;

extern app_synth_track_t track_list[MAX_TRACK_COUNT];

esp_err_t app_synth_track_init(void);
esp_err_t app_synth_track_set_level(uint8_t track_id, float level);
esp_err_t app_synth_track_set_op_wave(uint8_t track_id, uint8_t op_id, uint8_t wave);
esp_err_t app_synth_track_set_op_level(uint8_t track_id, uint8_t op_id, float level);
esp_err_t app_synth_track_set_op_coarse(uint8_t track_id, uint8_t op_id, uint8_t coarse);
esp_err_t app_synth_track_set_op_env(uint8_t track_id, uint8_t op_id, float A, float D, float S, float R);
esp_err_t app_synth_track_set_op_algorithm(uint8_t track_id, uint8_t carrier_id, uint8_t modulater_id, uint8_t flag);

#ifdef __cplusplus
}
#endif