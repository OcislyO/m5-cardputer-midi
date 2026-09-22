#pragma once

#include "esp_err.h"
#include "sys_audio.h"
#include "app.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_SYNTH_SAMPLE_RATE   44100

#define APP_SYNTH_WT_SIZE      256 // samples per waveform cycle, a power of 2
#define APP_SYNTH_WT_FRAC_BITS 24  // low bits of a 32-bit phase used to interpolate between table entries; the remaining 8 = log2(APP_SYNTH_WT_SIZE) index the table

typedef struct app_synth_state_s {
    uint8_t master_level;
} app_synth_state_t;

typedef enum {
    APP_SYNTH_STATE_MASTER_LEVEL = 0,
    APP_SYNTH_STATE_MAX
} app_synth_state_id_t;

typedef enum {
    APP_SYNTH_DATA_TRACK_LEVEL = 0,
    APP_SYNTH_DATA_OP_WAVE,
    APP_SYNTH_DATA_OP_LEVEL,
    APP_SYNTH_DATA_OP_COARSE,
    APP_SYNTH_DATA_OP_ENV,
    APP_SYNTH_DATA_ALGORITHM,
    // 整条轨道（app_synth_track_t），参数：track_id。注意 get_data 的 size
    // 是 uint8_t（上限 255 字节），结构体再变大就得拆开读。
    APP_SYNTH_DATA_TRACK,
    APP_SYNTH_DATA_MAX
} app_synth_data_id_t;

typedef enum {
    APP_SYNTH_CMD_SET_MASTER_LEVEL = 0,
    APP_SYNTH_CMD_SET_TRACK_LEVEL,
    APP_SYNTH_CMD_SET_OP_WAVE,
    APP_SYNTH_CMD_SET_OP_LEVEL,
    APP_SYNTH_CMD_SET_OP_COARSE,
    APP_SYNTH_CMD_SET_OP_ENV,
    APP_SYNTH_CMD_SET_ALGORITHM,
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


/* ------------------------------------------------------------------ *
 *  app_t 接口的包装：一律通过 synth_app 走 command/get_data，调用方不用再
 *  手写枚举和变参。命令的 float 参数按变参约定以 double 传，读到数据返回
 *  实际拷贝的字节数（和 get_data 一致，调用方拿 sizeof 比对即可）。
 * ------------------------------------------------------------------ */

static inline esp_err_t app_synth_set_master_level(uint8_t master_level) {
    return synth_app->command(synth_app, APP_SYNTH_CMD_SET_MASTER_LEVEL, master_level);
}

static inline esp_err_t app_synth_set_track_level(int track, float level) {
    return synth_app->command(synth_app, APP_SYNTH_CMD_SET_TRACK_LEVEL, track, (double)level);
}

static inline esp_err_t app_synth_set_op_wave(int track, int op, int wave) {
    return synth_app->command(synth_app, APP_SYNTH_CMD_SET_OP_WAVE, track, op, wave);
}

static inline esp_err_t app_synth_set_op_level(int track, int op, float level) {
    return synth_app->command(synth_app, APP_SYNTH_CMD_SET_OP_LEVEL, track, op, (double)level);
}

static inline esp_err_t app_synth_set_op_coarse(int track, int op, int coarse) {
    return synth_app->command(synth_app, APP_SYNTH_CMD_SET_OP_COARSE, track, op, coarse);
}

static inline esp_err_t app_synth_set_op_env(int track, int op, float a, float d, float s, float r) {
    return synth_app->command(synth_app, APP_SYNTH_CMD_SET_OP_ENV, track, op,
                              (double)a, (double)d, (double)s, (double)r);
}

// 参数顺序是 (track, carrier, modulator, flag)：让 modulator 去调制 carrier
static inline esp_err_t app_synth_set_algorithm(int track, int carrier, int modulator, bool flag) {
    return synth_app->command(synth_app, APP_SYNTH_CMD_SET_ALGORITHM, track, carrier, modulator, (int)flag);
}

// out 要传 app_synth_track_t*，一次拿走整条轨道（结构体定义在 app_synth_track.h）
static inline size_t app_synth_get_track(int track, void *out, uint8_t size) {
    return synth_app->get_data(synth_app, APP_SYNTH_DATA_TRACK, out, size, track);
}

static inline size_t app_synth_get_track_level(int track, float *out, uint8_t size) {
    return synth_app->get_data(synth_app, APP_SYNTH_DATA_TRACK_LEVEL, out, size, track);
}

// op 传 0 且 size 传整列大小时一次读出所有算子，否则只读 op 这一个
static inline size_t app_synth_get_op_wave(int track, int op, app_synth_wave_t *out, uint8_t size) {
    return synth_app->get_data(synth_app, APP_SYNTH_DATA_OP_WAVE, out, size, track, op);
}

static inline size_t app_synth_get_op_level(int track, int op, float *out, uint8_t size) {
    return synth_app->get_data(synth_app, APP_SYNTH_DATA_OP_LEVEL, out, size, track, op);
}

static inline size_t app_synth_get_op_coarse(int track, int op, uint8_t *out, uint8_t size) {
    return synth_app->get_data(synth_app, APP_SYNTH_DATA_OP_COARSE, out, size, track, op);
}

// out 要传 app_synth_env_t*（定义在 app_synth_env.h，这里看不到）
static inline size_t app_synth_get_op_env(int track, int op, void *out, uint8_t size) {
    return synth_app->get_data(synth_app, APP_SYNTH_DATA_OP_ENV, out, size, track, op);
}

// out 收到的是 fm_metrix 的一段：op 是起始行号，op = 0 且 size = 6 即整张矩阵
static inline size_t app_synth_get_algorithm(int track, int op, uint8_t *out, uint8_t size) {
    return synth_app->get_data(synth_app, APP_SYNTH_DATA_ALGORITHM, out, size, track, op);
}

app_t *app_synth_app_init(void);

/**
 * @brief Linearly-interpolated wavetable lookup. `phase` is a Q8.24
 *        fixed-point value covering one full cycle (0 .. 0xFFFFFFFF).
 */
int16_t app_synth_wavetable_sample(app_synth_wave_t wave, uint32_t phase);

#ifdef __cplusplus
}
#endif
