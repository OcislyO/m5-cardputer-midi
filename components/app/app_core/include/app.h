#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_ID_MIDI_KBD = 0,
    APP_ID_SYNTH,
    APP_ID_SEQ,
    APP_ID_UI,
    APP_ID_WEB,
    APP_ID_SHELL,
    APP_ID_MAX
} app_id_t;

typedef enum {
    APP_STATE_UNINIT = 0, // 未初始化，或已被 uninit
    APP_STATE_RUNNING,    // init 完成，正在运行
} app_state_t;

typedef struct app_s
{
    app_id_t id;
    app_state_t state;

    void *ctx;

    esp_err_t (*init)(struct app_s *app);   // 初始化并直接启动，成功后 state 为 RUNNING
    /**
     * 停止任务并回收本 app 动态申请的资源（队列、锁、UI 对象…），成功后
     * state 回到 UNINIT，可以再次 init。
     *
     * 任务不强制删除：app 把运行标志置 false，任务在自己的循环里看到后自行
     * 释放并 vTaskDelete(NULL)，否则可能在持有 sys_dsp 的树锁等情况下被打断。
     * 这样任务就不会再去碰它持有的资源，uninit 才敢释放。
     *
     * 多个 app 之间有调用关系时（如 app_midi_kbd 会调 ui_app->command），
     * uninit 的顺序必须和 init 相反：先关被依赖的，再关依赖别人的。
     */
    esp_err_t (*uninit)(struct app_s *app);

    esp_err_t (*command)(struct app_s *app, int16_t cmd, ...);

    size_t (*get_state)(struct app_s *app, int16_t state, void *out, uint8_t size);
    size_t (*get_data)(struct app_s *app, int16_t data, void *out, uint8_t size, ...);
} app_t;

/**
 * @brief 等待本 app 的任务真正退出，供 uninit 使用。
 *
 * app 的约定是：任务在循环条件（各自的运行标志）变为 false 后，先把传递给
 * xTaskCreate 的那个句柄清成 NULL，再调用 vTaskDelete(NULL)。所以把标志置
 * false 之后调用本函数等到句柄变 NULL，就说明该任务已经走完最后一行代码、
 * 不会再碰它持有的队列/锁，uninit 这时才能安全释放这些资源。
 *
 * @param handle     任务句柄的地址，任务自己会在退出前写 NULL
 * @param timeout_ms 最长等待时间
 * @return true 任务已退出；false 超时（任务仍在运行，此时不能释放它的资源）
 */
bool app_task_wait_stopped(TaskHandle_t *handle, uint32_t timeout_ms);

extern app_t *ui_app;
extern app_t *midi_kbd_app;
extern app_t *synth_app;
extern app_t *seq_app;
extern app_t *web_app;

#ifdef __cplusplus
}
#endif