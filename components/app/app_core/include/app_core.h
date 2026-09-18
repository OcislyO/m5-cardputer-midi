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

/**
 * @brief 关掉所有 app：按 init 的相反顺序依次 uninit，让每个 app 回收自己
 *        动态申请的资源。sys/bsp 那些共享服务没有 deinit，仍然保持运行，
 *        所以 app_stop() 之后可以再 app_start() 重新把所有 app 拉起来。
 */
void app_stop(void);

#ifdef __cplusplus
}
#endif
