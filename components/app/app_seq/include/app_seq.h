#pragma once

#include "esp_err.h"
#include "app.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// One sequencer per app_synth track/MIDI channel -- app_seq doesn't depend
// on app_synth itself (it only talks to the event bus, like any other
// producer/consumer), but the two share the same track numbering by
// convention, so playing back track N naturally targets synth track N.
#define APP_SEQ_TRACK_COUNT 4

#define APP_SEQ_STEPS_PER_BEAT 4  // a step is a 16th note
#define APP_SEQ_MAX_STEPS      64 // 4 bars of 4/4 at 16 steps/bar

#define APP_SEQ_DEFAULT_BPM 120
#define APP_SEQ_MIN_BPM     20
#define APP_SEQ_MAX_BPM     300

// Mirrors app_seq's internal per-track transport state.
typedef enum {
    APP_SEQ_TRANSPORT_STOPPED = 0,
    APP_SEQ_TRANSPORT_PLAYING,
    APP_SEQ_TRANSPORT_RECORDING,
} app_seq_transport_t;

typedef struct app_seq_state_s {
    uint8_t track;
    app_seq_transport_t transport;
    uint8_t pos;
    uint16_t bpm;
} app_seq_state_t;

// One state id per track -- get_state(app, APP_SEQ_STATE_TRACK_0 + track,
// out, size) copies an app_seq_state_t snapshot of that track into `out`.
typedef enum {
    APP_SEQ_STATE_TRACK_0 = 0,
    APP_SEQ_STATE_TRACK_1,
    APP_SEQ_STATE_TRACK_2,
    APP_SEQ_STATE_TRACK_3,
    APP_SEQ_STATE_MAX
} app_seq_state_id_t;

typedef enum {
    APP_SEQ_CMD_PLAY = 0,
    APP_SEQ_CMD_RECORD,
    APP_SEQ_CMD_PAUSE,
    APP_SEQ_CMD_SET_TEMPO,
    APP_SEQ_CMD_MAX
} app_seq_cmd_id_t;

typedef struct app_seq_app_s {
    app_t base;
    app_seq_state_t state;
} app_seq_app_t;

app_t *app_seq_app_init(void);

#ifdef __cplusplus
}
#endif
