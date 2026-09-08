#pragma once

#include "esp_err.h"
#include "app.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct app_ui_state_s {
    uint8_t oct;
} app_ui_state_t;

typedef enum {
    APP_UI_OCT = 0,
    APP_UI_STATE_MAX
} app_ui_state_id_t;

typedef enum {
    APP_UI_SET_OCT = 0,
    APP_UI_SET_KEY,
    APP_UI_CMD_MAX
} app_ui_cmd_id_t;

typedef struct app_ui_cmd_s {
    app_ui_cmd_id_t cmd;
    void *arg;
} app_ui_cmd_t;

typedef struct app_ui_app_s {
    app_t base;
    app_ui_state_t state;
} app_ui_app_t;


app_t *app_ui_app_init(void);

#ifdef __cplusplus
}
#endif
