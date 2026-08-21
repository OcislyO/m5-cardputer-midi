#include "sys_dsp.h"
#include "sys_dsp_priv.h"
#include <string.h>

static rect_t s_dirty_rects[SYS_DSP_MAX_DIRTY_RECTS];
static size_t s_dirty_count;
static SemaphoreHandle_t s_dirty_lock;

esp_err_t sys_dsp_dirty_init(void)
{
    if (s_dirty_lock == NULL) {
        s_dirty_lock = xSemaphoreCreateMutex();
        if (s_dirty_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

static void sys_dsp_dirty_add_locked(rect_t rect)
{
    static const rect_t panel = { 0, 0, SYS_DSP_PANEL_WIDTH, SYS_DSP_PANEL_HEIGHT };
    if (!sys_dsp_clip(&rect, &rect, &panel)) {
        return;
    }

    for (size_t i = 0; i < s_dirty_count; i++) {
        if (sys_dsp_rect_touches(&s_dirty_rects[i], &rect)) {
            sys_dsp_rect_union(&s_dirty_rects[i], &s_dirty_rects[i], &rect);
            return;
        }
    }

    if (s_dirty_count < SYS_DSP_MAX_DIRTY_RECTS) {
        s_dirty_rects[s_dirty_count++] = rect;
    } else {
        // List's full: fold into an existing entry rather than dropping a
        // dirty rect, which would leave stale pixels on screen permanently.
        sys_dsp_rect_union(&s_dirty_rects[0], &s_dirty_rects[0], &rect);
    }
}

void sys_dsp_invalidate(rect_t rect)
{
    xSemaphoreTake(s_dirty_lock, portMAX_DELAY);
    sys_dsp_dirty_add_locked(rect);
    xSemaphoreGive(s_dirty_lock);
}

size_t sys_dsp_dirty_pop_pending(rect_t out[SYS_DSP_MAX_DIRTY_RECTS])
{
    xSemaphoreTake(s_dirty_lock, portMAX_DELAY);
    size_t count = s_dirty_count;
    memcpy(out, s_dirty_rects, sizeof(rect_t) * count);
    s_dirty_count = 0;
    xSemaphoreGive(s_dirty_lock);
    return count;
}
