#include "app_synth_track.h"

app_synth_track_t track_list[MAX_TRACK_COUNT];

esp_err_t app_synth_track_init() {
    for (size_t i = 0; i < MAX_TRACK_COUNT; i++)
    {
        app_synth_env_set(&track_list[i].env, 0.001, 0.001, 0.3, 0.001);
        track_list[i].midi_channel = i;
        track_list[i].fm_metrix[0] = 0x00;
        track_list[i].fm_metrix[1] = 0x00;
        track_list[i].fm_metrix[2] = 0x00;
        track_list[i].fm_metrix[3] = 0x00;
        track_list[i].fm_metrix[4] = 0x00;
        // track_list[i].fm_metrix[3] = 0x01 << 4;
        // track_list[i].fm_metrix[4] = 0x01 << 5;
        track_list[i].fm_metrix[5] = 0x00;
    }
    
    return ESP_OK;
}