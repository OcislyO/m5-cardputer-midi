#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 12x12 单色图标，配合 sys_dsp_icon_register 使用：注册时 w/h 都传
// APP_UI_ICON_SIZE，位图不拷贝，所以这些数组必须常驻（放在这里的 const
// 数组会进 flash，只读，不会占 RAM）。
#define APP_UI_ICON_SIZE   12
#define APP_UI_ICON_STRIDE ((APP_UI_ICON_SIZE + 7) / 8) // 每行字节数
#define APP_UI_ICON_BYTES  (APP_UI_ICON_STRIDE * APP_UI_ICON_SIZE)

extern const uint8_t app_ui_icon_wifi[APP_UI_ICON_BYTES];
extern const uint8_t app_ui_icon_battery[APP_UI_ICON_BYTES];
extern const uint8_t app_ui_icon_play[APP_UI_ICON_BYTES];
extern const uint8_t app_ui_icon_pause[APP_UI_ICON_BYTES];
extern const uint8_t app_ui_icon_record[APP_UI_ICON_BYTES];
extern const uint8_t app_ui_icon_stop[APP_UI_ICON_BYTES];

#ifdef __cplusplus
}
#endif
