#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Number of 16-bit mono samples per render period -- see
 *        sys_audio_mix/sys_audio_send_frame.
 */
#define SYS_AUDIO_FRAME_SAMPLES CONFIG_SYS_AUDIO_FRAME_SAMPLES


typedef struct {
    int16_t samples[SYS_AUDIO_FRAME_SAMPLES];
    bool valid; // true once some voice has mixed audio in since the last send
} sys_audio_frame_t;


/**
 * @brief Bring up the audio output path: brings up drv_es8311 (which in turn
 *        brings up bsp_i2s) at CONFIG_SYS_AUDIO_SAMPLE_RATE_HZ, and sets the
 *        codec to CONFIG_SYS_AUDIO_DEFAULT_VOLUME_PCT, unmuted.
 *        Idempotent: safe to call again after the first successful call.
 */
esp_err_t sys_audio_init(void);

/**
 * @brief Mix `src`'s SYS_AUDIO_FRAME_SAMPLES samples into the pending
 *        transmit frame: replaces it if nothing has been mixed in since the
 *        last sys_audio_send_frame, otherwise adds on top with saturation to
 *        int16 range. Call once per active voice, each render period, before
 *        sys_audio_send_frame.
 *        Not thread-safe -- call only from the single task that also calls
 *        sys_audio_send_frame; sys_audio does not lock the pending frame.
 */
void sys_audio_mix(const int16_t *src);

/**
 * @brief Blocking write of the pending transmit frame (see sys_audio_mix) to
 *        the speaker -- silence if no voice mixed anything in this period --
 *        then marks it empty again for the next period.
 *        Not thread-safe -- see sys_audio_mix.
 */
esp_err_t sys_audio_send_frame(void);

/**
 * @brief Set DAC (speaker) output volume, 0-100%.
 */
esp_err_t sys_audio_set_volume(uint8_t volume_pct);

/**
 * @brief Mute/unmute the speaker output.
 */
esp_err_t sys_audio_set_mute(bool mute);

#ifdef __cplusplus
}
#endif
