#pragma once

#include "esp_err.h"
#include "sys_audio.h"
#include "app.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_SYNTH_SAMPLE_RATE   44100

#define APP_SYNTH_WT_SIZE      256 // samples per waveform cycle, a power of 2
#define APP_SYNTH_WT_FRAC_BITS 24  // low bits of a 32-bit phase used to interpolate between table entries; the remaining 8 = log2(APP_SYNTH_WT_SIZE) index the table

typedef struct app_synth_state_s {
    uint8_t flag;
} app_synth_state_t;

typedef enum {
    APP_SYNTH_FLAG = 0,
    APP_SYNTH_STATE_MAX
} app_synth_state_id_t;

typedef enum {
    APP_SYNTH_SET_ENV = 0,
    APP_SYNTH_SET_ALGORITHM,
    APP_SYNTH_SET_TRACK_WAVE,
    APP_SYNTH_CMD_MAX
} app_synth_cmd_id_t;

typedef struct app_synth_app_s {
    app_t base;
    app_synth_state_t state;
} app_synth_app_t;

typedef enum {
    APP_SYNTH_WAVE_SINE = 0,
    APP_SYNTH_WAVE_TRIANGLE,
    APP_SYNTH_WAVE_SAW,
    APP_SYNTH_WAVE_SQUARE,
    APP_SYNTH_WAVE_COUNT,
} app_synth_wave_t;


app_t *app_synth_app_init(void);

/**
 * @brief Linearly-interpolated wavetable lookup. `phase` is a Q8.24
 *        fixed-point value covering one full cycle (0 .. 0xFFFFFFFF).
 */
int16_t app_synth_wavetable_sample(app_synth_wave_t wave, uint32_t phase);

#ifdef __cplusplus
}
#endif
