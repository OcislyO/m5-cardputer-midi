#include "app_synth_priv.h"
#include <math.h>

static int16_t s_tables[APP_SYNTH_WAVE_COUNT][APP_SYNTH_WT_SIZE];
static bool s_initialized = false;

void app_synth_wavetable_init(void)
{
    if (s_initialized) {
        return;
    }

    for (size_t i = 0; i < APP_SYNTH_WT_SIZE; i++) {
        float phase = (float)i / APP_SYNTH_WT_SIZE; // 0..1 through one cycle

        s_tables[APP_SYNTH_WAVE_SINE][i] = (int16_t)(sinf(2.0f * (float)M_PI * phase) * INT16_MAX);

        // Symmetric triangle: rises 0..1..0..-1..0 across the cycle.
        float tri = phase < 0.5f ? (4.0f * phase - 1.0f) : (3.0f - 4.0f * phase);
        s_tables[APP_SYNTH_WAVE_TRIANGLE][i] = (int16_t)(tri * INT16_MAX);

        // Falling sawtooth: +1 at phase 0, ramps linearly down to -1.
        s_tables[APP_SYNTH_WAVE_SAW][i] = (int16_t)((1.0f - 2.0f * phase) * INT16_MAX);

        s_tables[APP_SYNTH_WAVE_SQUARE][i] = (phase < 0.5f) ? INT16_MAX : -INT16_MAX;
    }

    s_initialized = true;
}

int32_t app_synth_wavetable_sample(app_synth_wave_t wave, uint32_t phase)
{
    uint32_t index = phase >> APP_SYNTH_WT_FRAC_BITS;
    uint32_t frac = phase & ((1u << APP_SYNTH_WT_FRAC_BITS) - 1);

    const int16_t *table = s_tables[wave];
    int32_t a = table[index];
    int32_t b = table[(index + 1) & (APP_SYNTH_WT_SIZE - 1)];

    return a + (((b - a) * (int32_t)frac) >> APP_SYNTH_WT_FRAC_BITS);
}
