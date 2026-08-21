#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "freertos/FreeRTOS.h"
/**
 * @brief Application entry point, called from app_main().
 *        Brings up sys/bsp services and starts the MIDI keyboard app.
 */
void app_start(void);

#ifdef __cplusplus
}
#endif
