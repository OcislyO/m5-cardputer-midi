#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// One sequencer per app_synth track/MIDI channel -- app_seq doesn't depend
// on app_synth (it only talks to the MIDI bus, same as any other
// producer/consumer), but the two share the same track numbering by
// convention, so playing back track N naturally targets synth track N.
#define APP_SEQ_TRACK_COUNT 4

#define APP_SEQ_STEPS_PER_BEAT 4  // a step is a 16th note
#define APP_SEQ_MAX_STEPS      64 // 4 bars of 4/4 at 16 steps/bar

#define APP_SEQ_DEFAULT_BPM 120
#define APP_SEQ_MIN_BPM     20
#define APP_SEQ_MAX_BPM     300

/**
 * @brief Bring up app_seq: APP_SEQ_TRACK_COUNT independent sequencers, each
 *        with its own APP_SEQ_MAX_STEPS-step loop, all starting stopped, at
 *        APP_SEQ_DEFAULT_BPM, empty. Idempotent: safe to call again after
 *        the first successful call.
 */
esp_err_t app_seq_init(void);

/**
 * @brief Arm recording on `track`'s sequencer and (re)start its transport --
 *        from step 0 the first time, or wherever app_seq_pause() last left
 *        it. While running, each step captures the most recent NOTE_ON seen
 *        on the MIDI bus since the previous step (last-key-wins if more
 *        than one arrived; not filtered by the event's own channel, since
 *        every current producer plays on channel 0), overwriting that
 *        step's slot -- a step with nothing played in its window is left/
 *        becomes empty. Recording and playback share one transport: like
 *        app_seq_play(), every step (re)plays what it holds as the
 *        transport passes over it, so you hear the pattern build up as you
 *        record.
 */
esp_err_t app_seq_record(uint8_t track);

/**
 * @brief (Re)start `track`'s transport in playback mode -- from step 0 the
 *        first time, or wherever app_seq_pause() last left it. Entering a
 *        step first sends NOTE_OFF for whatever note the previous step left
 *        held, then -- if the new step holds a note -- sends NOTE_ON for it
 *        on `track`'s channel; the note keeps sounding until the following
 *        step. Loops forever over APP_SEQ_MAX_STEPS.
 */
esp_err_t app_seq_play(uint8_t track);

/**
 * @brief Stop `track`'s transport in place: position, recorded steps, and
 *        tempo are all left untouched, so app_seq_play()/app_seq_record()
 *        pick back up from here. Releases any note currently held by
 *        playback.
 */
esp_err_t app_seq_pause(uint8_t track);

/**
 * @brief Set `track`'s tempo. A step is a 16th note at this BPM (4 steps
 *        per beat), so the full 64-step loop is 4 bars of 4/4. Takes effect
 *        on the next step, whether or not the transport is currently
 *        running.
 * @return ESP_ERR_INVALID_ARG if `bpm` is outside
 *         [APP_SEQ_MIN_BPM, APP_SEQ_MAX_BPM].
 */
esp_err_t app_seq_set_tempo(uint8_t track, uint16_t bpm);

#ifdef __cplusplus
}
#endif
