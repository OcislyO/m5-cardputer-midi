#include "app_synth_env.h"
#include "app_synth_track.h"

app_synth_track_t track_list[MAX_TRACK_COUNT];

esp_err_t app_synth_track_init() {
    for (size_t i = 0; i < MAX_TRACK_COUNT; i++)
    {
        track_list[i].voice_count = 0;
        track_list[i].midi_channel = i;

        for (size_t j = 0; j < MAX_OPERATOR_COUNT; j++)
        {
            app_synth_env_set(&track_list[i].op_env[j], 0.001, 0.0001, 0.5, 0.0001);
            track_list[i].op_wave[j] = APP_SYNTH_WAVE_SINE;
            track_list[i].op_coarse[j] = 1;
            track_list[i].op_level[j] = 0.5;
        }

        track_list[i].voice_level = 0;
        track_list[i].fm_metrix[0] = 0x00;
        track_list[i].fm_metrix[1] = 0x00;
        track_list[i].fm_metrix[2] = 0x01 << 3;
        track_list[i].fm_metrix[3] = 0x00;
        track_list[i].fm_metrix[4] = 0x01 << 4 | 0x01 << 5;
        track_list[i].fm_metrix[5] = 0x00;
    }
    // example
    track_list[0].voice_level = 1;
    app_synth_env_set(&track_list[0].op_env[0], 0.001, 0.0001, 0.5, 0.0001);
    app_synth_env_set(&track_list[0].op_env[1], 0.001, 0.0001, 0.5, 0.0003);
    app_synth_env_set(&track_list[0].op_env[2], 0.001, 0.0001, 0.5, 0.0001);
    app_synth_env_set(&track_list[0].op_env[3], 0.001, 0.0001, 0.5, 0.0001);
    app_synth_env_set(&track_list[0].op_env[4], 0.001, 0.0001, 0.5, 0.0001);
    app_synth_env_set(&track_list[0].op_env[5], 0.001, 0.0001, 0.5, 0.0001);

    track_list[0].fm_metrix[0] = 0x01 << 0 | 0x01 << 1;
    track_list[0].fm_metrix[1] = 0x00;
    track_list[0].fm_metrix[2] = 0x01 << 3;
    track_list[0].fm_metrix[3] = 0x00;
    track_list[0].fm_metrix[4] = 0x01 << 5;
    track_list[0].fm_metrix[5] = 0x00;

    track_list[0].op_coarse[0] = 2;
    track_list[0].op_coarse[1] = 1;
    track_list[0].op_coarse[2] = 7;
    track_list[0].op_coarse[3] = 1;
    track_list[0].op_coarse[4] = 12;
    track_list[0].op_coarse[5] = 1;

    track_list[0].op_level[0] = 0.5;
    track_list[0].op_level[1] = 0.7;
    track_list[0].op_level[2] = 0.6;
    track_list[0].op_level[3] = 0.8;
    track_list[0].op_level[4] = 0.5;
    track_list[0].op_level[5] = 0.7;
    
    return ESP_OK;
}

esp_err_t app_synth_track_set_level(uint8_t track_id, float level) {
    if (track_id >= MAX_TRACK_COUNT)
        return ESP_ERR_INVALID_ARG;
    
    if (level < 0)
        return ESP_ERR_INVALID_ARG;
    
    if (level > 1) {
        level = 1.0f;
    }
    
    track_list[track_id].voice_level = level;

    return ESP_OK;
}

esp_err_t app_synth_track_set_op_wave(uint8_t track_id, uint8_t op_id, uint8_t wave) {
    if (track_id >= MAX_TRACK_COUNT)
        return ESP_ERR_INVALID_ARG;

    if (op_id >= MAX_OPERATOR_COUNT)
        return ESP_ERR_INVALID_ARG;

    if (wave >= APP_SYNTH_WAVE_COUNT)
        return ESP_ERR_INVALID_ARG;

    track_list[track_id].op_wave[op_id] = (app_synth_wave_t)wave;

    return ESP_OK;
}

esp_err_t app_synth_track_set_op_level(uint8_t track_id, uint8_t op_id, float level) {
    if (track_id >= MAX_TRACK_COUNT)
        return ESP_ERR_INVALID_ARG;

    if (op_id >= MAX_OPERATOR_COUNT)
        return ESP_ERR_INVALID_ARG;

    if (level < 0)
        return ESP_ERR_INVALID_ARG;
    
    if (level > 1) {
        level = 1.0f;
    }

    track_list[track_id].op_level[op_id] = level;

    return ESP_OK;
}

esp_err_t app_synth_track_set_op_coarse(uint8_t track_id, uint8_t op_id, uint8_t coarse) {
    if (track_id >= MAX_TRACK_COUNT)
        return ESP_ERR_INVALID_ARG;

    if (op_id >= MAX_OPERATOR_COUNT)
        return ESP_ERR_INVALID_ARG;

    if (coarse > 31) {
        coarse = 31;
    }

    track_list[track_id].op_coarse[op_id] = coarse;

    return ESP_OK;
}

esp_err_t app_synth_track_set_op_env(uint8_t track_id, uint8_t op_id, float A, float D, float S, float R) {
    if (track_id >= MAX_TRACK_COUNT)
        return ESP_ERR_INVALID_ARG;

    if (op_id >= MAX_OPERATOR_COUNT)
        return ESP_ERR_INVALID_ARG;

    if (A <= 0) {
        A = 0.000001;
    }

    if (A > 1) {
        A = 1;
    }

    if (D <= 0) {
        D = 0.000001;
    }

    if (D > 1) {
        D = 1;
    }

    if (S <= 0) {
        S = 0.000001;
    }

    if (S > 1) {
        S = 1;
    }

    if (R <= 0) {
        R = 0.000001;
    }

    if (R > 1) {
        R = 1;
    }

    app_synth_env_set(&track_list[track_id].op_env[op_id], A, D, S, R);

    return ESP_OK;
}

esp_err_t app_synth_track_set_op_algorithm(uint8_t track_id, uint8_t carrier_id, uint8_t modulater_id, uint8_t flag) {
    if (track_id >= MAX_TRACK_COUNT)
        return ESP_ERR_INVALID_ARG;

    if (carrier_id >= MAX_OPERATOR_COUNT)
        return ESP_ERR_INVALID_ARG;

    if (modulater_id >= MAX_OPERATOR_COUNT)
        return ESP_ERR_INVALID_ARG;

    if (flag) {
        track_list[track_id].fm_metrix[modulater_id] |= 0x01 << carrier_id;
    } else {
        track_list[track_id].fm_metrix[modulater_id] &= ~(0x01 << carrier_id);
    }

    return ESP_OK;
}