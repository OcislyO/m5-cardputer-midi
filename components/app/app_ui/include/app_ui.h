#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Test screen: brings up the display, renders a full-screen
 *        solid-color window with a "Hello World" text box on top, and starts
 *        a background task that bounces a small box around the screen to
 *        exercise sys_dsp's dirty-rect renderer.
 */
esp_err_t app_ui_start(void);

#ifdef __cplusplus
}
#endif
