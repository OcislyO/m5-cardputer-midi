#pragma once

// Internal state shared between app_synth's translation units. Not a public
// header -- lives outside include/, application code must not use it.

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Guards app_synth_voice_pool[] and track_list[] (from_track, env_state,
// level, osc/op, voice_count): app_synth_task allocates/releases voices
// (app_synth_voice_on/off, which take/give this themselves) while
// app_synth_engine_task concurrently walks the same pool every render
// period to advance envelopes and generate samples. Created in
// app_synth_init(), before either task starts.
extern SemaphoreHandle_t s_synth_lock;
