#pragma once

// Internal state/helpers shared between app_synth's translation units. Not a
// public header -- lives outside include/, application code must not use it.

#include "app_synth.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdbool.h>
#include <stddef.h>

#define APP_SYNTH_SAMPLE_RATE_HZ    44100
#define APP_SYNTH_DEFAULT_VOLUME_PCT 80 // drv_es8311_init() never touches the DAC volume register -- app_synth_init() must set an explicit level or output level is whatever the codec's power-on-reset value happens to be
#define APP_SYNTH_FRAME_SAMPLES  256 // samples rendered/written to bsp_i2s per pass
#define APP_SYNTH_I2S_TIMEOUT_MS 1000

#define APP_SYNTH_MAX_VOICES    8
#define APP_SYNTH_OPS_PER_VOICE 2 // ops[0] = carrier (voice output), ops[1] = modulator

// A voice's two operators sum to this many bits of extra headroom before the
// final int16 clip, so a handful of full-velocity voices overlapping doesn't
// immediately clip -- see app_synth_render.c.
#define APP_SYNTH_MIX_SHIFT 2

// --- wavetable (app_synth_wavetable.c) ----------------------------------

#define APP_SYNTH_WT_SIZE      256 // samples per waveform cycle, a power of 2
#define APP_SYNTH_WT_FRAC_BITS 24  // low bits of a 32-bit phase used to interpolate between table entries; the remaining 8 = log2(APP_SYNTH_WT_SIZE) index the table

typedef enum {
    APP_SYNTH_WAVE_SINE = 0,
    APP_SYNTH_WAVE_TRIANGLE,
    APP_SYNTH_WAVE_SAW,
    APP_SYNTH_WAVE_SQUARE,
    APP_SYNTH_WAVE_COUNT,
} app_synth_wave_t;

// Fills the built-in waveform tables. Idempotent: safe to call again after
// the first successful call.
void app_synth_wavetable_init(void);

// Linearly-interpolated table lookup. `phase` is the full 32-bit
// phase-accumulator value (see app_synth_op_t.phase) -- this is what lets a
// modulator's output be added straight into a carrier's phase for FM/PM
// (see app_synth_voice_process) without any unit conversion.
int32_t app_synth_wavetable_sample(app_synth_wave_t wave, uint32_t phase);

// --- envelope + FM operator (app_synth_op.c) ----------------------------

typedef enum {
    APP_SYNTH_ENV_IDLE = 0,
    APP_SYNTH_ENV_ATTACK,
    APP_SYNTH_ENV_DECAY,
    APP_SYNTH_ENV_SUSTAIN,
    APP_SYNTH_ENV_RELEASE,
} app_synth_env_stage_t;

// A linear-segment AD(S)R envelope, advanced one sample at a time by
// app_synth_env_step(). level and sustain_level are Q16 (0..1<<16); the
// *_step fields are the per-sample delta for their stage, precomputed by
// app_synth_env_trigger/release from a duration in ms so the stage always
// takes the same wall-clock time regardless of sample rate.
typedef struct {
    app_synth_env_stage_t stage;
    int32_t level;
    int32_t attack_step;
    int32_t decay_step;
    int32_t sustain_level;
    int32_t release_step;
} app_synth_env_t;

// Resets env to the start of its attack stage, computing attack_step and
// decay_step from the requested durations so attack reaches full scale (Q16
// 1<<16) in attack_ms and decay reaches sustain_q16 in decay_ms.
void app_synth_env_trigger(app_synth_env_t *env, uint32_t attack_ms, uint32_t decay_ms, int32_t sustain_q16);

// Moves env into its release stage, computing release_step from its
// *current* level so it always reaches 0 in release_ms regardless of which
// stage (or level) it was released from. No-op if env is already idle.
void app_synth_env_release(app_synth_env_t *env, uint32_t release_ms);

// Advances env by one sample, applying the current stage's step and
// handling the transition into the next stage when a boundary is crossed.
// Returns the new level (also readable afterwards as env->level).
int32_t app_synth_env_step(app_synth_env_t *env);

// One FM operator: a phase-accumulator oscillator reading
// app_synth_wavetable, shaped by its own envelope and output level. Used
// either as a voice's audible carrier or as a phase modulator feeding the
// next operator -- see app_synth_voice_t.algorithm and
// app_synth_voice_process().
typedef struct {
    app_synth_wave_t wave;
    uint32_t phase;     // Q0.32 fixed-point phase accumulator; wraps naturally on overflow, which is exactly phase wrapping every cycle
    uint32_t phase_inc; // per-sample phase increment, i.e. this operator's frequency expressed in phase units (see app_synth_freq_to_phase_inc)
    int32_t level;      // Q16 output scale, 0..65536; multiplies this operator's own envelope*wavetable output
    int32_t mod_index;  // peak phase deviation, in the same units as `phase`, this operator imparts on the next operator when used as a modulator (0x100000000 == a full extra turn at full modulator amplitude); unused when this operator is a carrier
    app_synth_env_t env;
} app_synth_op_t;

// Converts a frequency to a phase_inc for APP_SYNTH_SAMPLE_RATE_HZ.
uint32_t app_synth_freq_to_phase_inc(float freq_hz);

// Renders one sample from `op`: advances its envelope, looks up the
// wavetable at `op->phase + phase_mod` (the incoming phase-modulation
// offset from a preceding operator, or 0 for an unmodulated operator),
// advances op->phase by its own phase_inc, and returns the envelope- and
// level-scaled result.
int32_t app_synth_op_process(app_synth_op_t *op, int32_t phase_mod);

// --- voice pool / FM synthesis (app_synth_voice.c) ----------------------

// How a voice's two operators combine into its output sample -- the "FM
// algorithm". Only two are offered (this is a 2-operator engine, not a
// full 4/6-op DX7-style matrix), but the split keeps room to add more.
typedef enum {
    APP_SYNTH_ALGO_SERIAL = 0, // ops[1] (modulator) phase-modulates ops[0] (carrier); ops[0]'s output is the voice's output
    APP_SYNTH_ALGO_PARALLEL,   // ops[0] and ops[1] both run as independent, unmodulated carriers and are summed
} app_synth_algo_t;

typedef struct {
    bool active; // has (or is finishing releasing) an assigned MIDI note -- see app_synth_render.c, which clears this once every op's envelope goes idle
    uint8_t note;
    uint32_t age; // allocation order (from a monotonic counter), oldest is stolen first when every voice is active
    app_synth_algo_t algorithm;
    app_synth_op_t ops[APP_SYNTH_OPS_PER_VOICE];
} app_synth_voice_t;

extern app_synth_voice_t s_app_synth_voices[APP_SYNTH_MAX_VOICES];
extern SemaphoreHandle_t s_app_synth_voice_lock;

// Zeroes the voice pool (all voices idle/inactive). Idempotent: safe to
// call again after the first successful call.
void app_synth_voice_pool_init(void);

esp_err_t app_synth_voice_note_on(uint8_t note, uint8_t velocity);
esp_err_t app_synth_voice_note_off(uint8_t note);

// Renders one sample of `v` per its algorithm. Caller must hold
// s_app_synth_voice_lock.
int32_t app_synth_voice_process(app_synth_voice_t *v);

// --- renderer (app_synth_render.c) --------------------------------------

// Starts the background task that mixes all active voices into a buffer and
// writes it to bsp_i2s. Idempotent: safe to call again after the first
// successful call.
esp_err_t app_synth_render_start(void);

// --- MIDI bus consumer (app_synth_midi.c) -------------------------------

// Subscribes a queue to the MIDI bus (app_midi_bus.h) and starts the task
// that turns each received NOTE_ON/NOTE_OFF into an app_synth_note_on/off
// call -- this is the only thing that ties app_synth to MIDI at all; the
// voice/operator/wavetable/render code above knows nothing about it.
// Idempotent: safe to call again after the first successful call.
esp_err_t app_synth_midi_start(void);
