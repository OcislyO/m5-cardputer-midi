#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bring up the synth engine: initializes the shared I2S bus and
 *        ES8311 codec for audio output, seeds the wavetables, starts the
 *        background task that renders active voices (see
 *        app_synth_note_on/off) and streams the mix out through bsp_i2s,
 *        and subscribes its own task to the MIDI bus (app_midi_bus.h) so it
 *        plays whatever NOTE_ON/OFF events show up there -- app_synth
 *        doesn't need anything else to drive it once this returns.
 */
esp_err_t app_synth_init(void);

/**
 * @brief Start sounding a MIDI note: allocates (retriggering if `midi_note`
 *        already has a voice, else reusing an idle one, else stealing the
 *        oldest) a polyphonic FM voice -- two operators, a sine carrier
 *        phase-modulated by a sine modulator -- tuned to `midi_note`'s pitch
 *        and scaled by `velocity`, then starts its envelope's attack stage.
 *        Per MIDI convention, velocity == 0 is treated as a note-off.
 */
esp_err_t app_synth_note_on(uint8_t midi_note, uint8_t velocity);

/**
 * @brief Begin releasing a sounding MIDI note: moves its voice's operators
 *        into their envelope's release stage. The voice keeps rendering
 *        (fading out) until the release finishes, then is freed for reuse.
 *        A no-op if `midi_note` has no active voice.
 */
esp_err_t app_synth_note_off(uint8_t midi_note);

/**
 * @brief Set the codec's output volume, 0-100%. Thin wrapper over
 *        drv_es8311_set_volume.
 */
esp_err_t app_synth_set_volume(uint8_t volume_pct);

#ifdef __cplusplus
}
#endif
