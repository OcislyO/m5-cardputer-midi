#include "app_synth_priv.h"
#include <math.h>

// Never let a *_step end up 0 -- that would freeze the envelope in its
// current stage forever instead of (slowly) finishing it, which would
// otherwise happen for a stage requested with an implausibly long duration.
static inline int32_t app_synth_ms_to_samples(uint32_t ms)
{
    int32_t samples = (int32_t)((uint64_t)ms * APP_SYNTH_SAMPLE_RATE_HZ / 1000);
    return samples > 0 ? samples : 1;
}

void app_synth_env_trigger(app_synth_env_t *env, uint32_t attack_ms, uint32_t decay_ms, int32_t sustain_q16)
{
    env->stage = APP_SYNTH_ENV_ATTACK;
    env->level = 0;
    env->sustain_level = sustain_q16;

    env->attack_step = 65536 / app_synth_ms_to_samples(attack_ms);
    if (env->attack_step < 1) {
        env->attack_step = 1;
    }

    env->decay_step = -(65536 - sustain_q16) / app_synth_ms_to_samples(decay_ms);
    if (env->decay_step > -1) {
        env->decay_step = -1;
    }
}

void app_synth_env_release(app_synth_env_t *env, uint32_t release_ms)
{
    if (env->stage == APP_SYNTH_ENV_IDLE) {
        return;
    }

    env->release_step = -env->level / app_synth_ms_to_samples(release_ms);
    if (env->release_step > -1) {
        env->release_step = -1;
    }
    env->stage = APP_SYNTH_ENV_RELEASE;
}

int32_t app_synth_env_step(app_synth_env_t *env)
{
    switch (env->stage) {
    case APP_SYNTH_ENV_ATTACK:
        env->level += env->attack_step;
        if (env->level >= 65536) {
            env->level = 65536;
            env->stage = APP_SYNTH_ENV_DECAY;
        }
        break;

    case APP_SYNTH_ENV_DECAY:
        env->level += env->decay_step;
        if (env->level <= env->sustain_level) {
            env->level = env->sustain_level;
            env->stage = APP_SYNTH_ENV_SUSTAIN;
        }
        break;

    case APP_SYNTH_ENV_SUSTAIN:
        break;

    case APP_SYNTH_ENV_RELEASE:
        env->level += env->release_step;
        if (env->level <= 0) {
            env->level = 0;
            env->stage = APP_SYNTH_ENV_IDLE;
        }
        break;

    case APP_SYNTH_ENV_IDLE:
    default:
        env->level = 0;
        break;
    }

    return env->level;
}

uint32_t app_synth_freq_to_phase_inc(float freq_hz)
{
    return (uint32_t)(freq_hz * (4294967296.0f / APP_SYNTH_SAMPLE_RATE_HZ));
}

int32_t app_synth_op_process(app_synth_op_t *op, int32_t phase_mod)
{
    int32_t env_level = app_synth_env_step(&op->env);
    if (op->env.stage == APP_SYNTH_ENV_IDLE) {
        return 0; // phase left untouched -- resumes cleanly from 0 on the next trigger
    }

    uint32_t sample_phase = op->phase + (uint32_t)phase_mod;
    int32_t raw = app_synth_wavetable_sample(op->wave, sample_phase);
    op->phase += op->phase_inc; // own frequency only -- phase_mod is a transient offset for this sample's lookup, not a change to the accumulator

    int32_t out = (int32_t)(((int64_t)raw * env_level) >> 16);
    out = (int32_t)(((int64_t)out * op->level) >> 16);
    return out;
}
