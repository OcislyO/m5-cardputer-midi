#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "app_synth_op.h"

#define MAX_TRACK_COUNT 4

typedef struct
{
    uint8_t midi_channel;
    app_synth_wave_t wave;
    app_synth_env_t env;
    uint8_t voice_count;
    uint8_t fm_metrix[MAX_OPERATOR_COUNT];
} app_synth_track_t;

extern app_synth_track_t track_list[MAX_TRACK_COUNT];

esp_err_t app_synth_track_init(void);

#ifdef __cplusplus
}
#endif