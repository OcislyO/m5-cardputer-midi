#include "app_synth_priv.h"
#include <math.h>
#include <string.h>

// Default 2-op FM patch -- a sine carrier phase-modulated by a sine
// modulator at twice its frequency. Not (yet) caller-configurable; per-note
// tuning is limited to pitch and velocity.
#define APP_SYNTH_MOD_RATIO       2.0f
#define APP_SYNTH_MOD_INDEX_DEPTH 0x18000000u // ~0.09 turn peak deviation at full modulator amplitude

#define APP_SYNTH_ENV_ATTACK_MS   5
#define APP_SYNTH_ENV_DECAY_MS    120
#define APP_SYNTH_ENV_SUSTAIN_Q16 (65536 * 60 / 100) // 60%
#define APP_SYNTH_ENV_RELEASE_MS  200

app_synth_voice_t s_app_synth_voices[APP_SYNTH_MAX_VOICES];
SemaphoreHandle_t s_app_synth_voice_lock;

static uint32_t s_age_counter;

void app_synth_voice_pool_init(void)
{
    if (s_app_synth_voice_lock != NULL) {
        return;
    }

    memset(s_app_synth_voices, 0, sizeof(s_app_synth_voices));
    s_app_synth_voice_lock = xSemaphoreCreateMutex();
}

// Caller must hold s_app_synth_voice_lock.
static app_synth_voice_t *app_synth_voice_find(uint8_t note)
{
    for (size_t i = 0; i < APP_SYNTH_MAX_VOICES; i++) {
        if (s_app_synth_voices[i].active && s_app_synth_voices[i].note == note) {
            return &s_app_synth_voices[i];
        }
    }
    return NULL;
}

// Picks an idle voice if one exists, else steals the oldest allocation
// (lowest age) regardless of stage. Caller must hold s_app_synth_voice_lock.
static app_synth_voice_t *app_synth_voice_acquire(void)
{
    app_synth_voice_t *oldest = &s_app_synth_voices[0];

    for (size_t i = 0; i < APP_SYNTH_MAX_VOICES; i++) {
        if (!s_app_synth_voices[i].active) {
            return &s_app_synth_voices[i];
        }
        if (s_app_synth_voices[i].age < oldest->age) {
            oldest = &s_app_synth_voices[i];
        }
    }
    return oldest;
}

esp_err_t app_synth_voice_note_on(uint8_t note, uint8_t velocity)
{
    if (velocity == 0) {
        return app_synth_voice_note_off(note); // MIDI convention: velocity 0 == note off
    }

    float freq = 440.0f * powf(2.0f, ((float)note - 69.0f) / 12.0f);

    xSemaphoreTake(s_app_synth_voice_lock, portMAX_DELAY);

    app_synth_voice_t *v = app_synth_voice_find(note); // retrigger if already sounding
    if (v == NULL) {
        v = app_synth_voice_acquire();
    }

    v->active = true;
    v->note = note;
    v->age = ++s_age_counter;
    v->algorithm = APP_SYNTH_ALGO_SERIAL;

    app_synth_op_t *car = &v->ops[0];
    car->wave = APP_SYNTH_WAVE_SINE;
    car->phase = 0;
    car->phase_inc = app_synth_freq_to_phase_inc(freq);
    car->level = (int32_t)velocity * 65536 / 127;
    car->mod_index = 0; // unused: nothing modulates a carrier in this 2-op algorithm
    app_synth_env_trigger(&car->env, APP_SYNTH_ENV_ATTACK_MS, APP_SYNTH_ENV_DECAY_MS, APP_SYNTH_ENV_SUSTAIN_Q16);

    app_synth_op_t *mod = &v->ops[1];
    mod->wave = APP_SYNTH_WAVE_SINE;
    mod->phase = 0;
    mod->phase_inc = app_synth_freq_to_phase_inc(freq * APP_SYNTH_MOD_RATIO);
    mod->level = 65536; // full scale -- its own envelope still shapes brightness over the note
    mod->mod_index = APP_SYNTH_MOD_INDEX_DEPTH;
    app_synth_env_trigger(&mod->env, APP_SYNTH_ENV_ATTACK_MS, APP_SYNTH_ENV_DECAY_MS, APP_SYNTH_ENV_SUSTAIN_Q16);

    xSemaphoreGive(s_app_synth_voice_lock);
    return ESP_OK;
}

esp_err_t app_synth_voice_note_off(uint8_t note)
{
    xSemaphoreTake(s_app_synth_voice_lock, portMAX_DELAY);

    app_synth_voice_t *v = app_synth_voice_find(note);
    if (v != NULL) {
        for (size_t i = 0; i < APP_SYNTH_OPS_PER_VOICE; i++) {
            app_synth_env_release(&v->ops[i].env, APP_SYNTH_ENV_RELEASE_MS);
        }
    }

    xSemaphoreGive(s_app_synth_voice_lock);
    return ESP_OK; // no active voice for `note` is not an error -- e.g. already stolen
}

int32_t app_synth_voice_process(app_synth_voice_t *v)
{
    app_synth_op_t *car = &v->ops[0];
    app_synth_op_t *mod = &v->ops[1];

    switch (v->algorithm) {
    case APP_SYNTH_ALGO_SERIAL: {
        int32_t mod_out = app_synth_op_process(mod, 0);
        int32_t phase_mod = (int32_t)(((int64_t)mod_out * mod->mod_index) >> 15);
        return app_synth_op_process(car, phase_mod);
    }

    case APP_SYNTH_ALGO_PARALLEL:
        return app_synth_op_process(car, 0) + app_synth_op_process(mod, 0);
    }

    return 0;
}
