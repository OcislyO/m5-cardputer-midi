#include "sys_dsp.h"
#include "sys_dsp_priv.h"
#include "freertos/task.h"

#define SYS_DSP_RENDER_PERIOD_MS   20
#define SYS_DSP_RENDER_TASK_STACK  4096
#define SYS_DSP_RENDER_TASK_PRIO   4

static TaskHandle_t s_render_task;

static void sys_dsp_render_task(void *arg);

esp_err_t sys_dsp_render_start(void)
{
    if (s_render_task == NULL) {
        if (xTaskCreate(sys_dsp_render_task, "sys_dsp_render", SYS_DSP_RENDER_TASK_STACK,
                         NULL, SYS_DSP_RENDER_TASK_PRIO, &s_render_task) != pdPASS) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

esp_err_t sys_dsp_render(ui_obj_t *obj)
{
    if (obj == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    sys_dsp_invalidate(obj->rect);
    return ESP_OK;
}

// Paints obj into band (if it overlaps), then its children, then its next
// sibling -- parent-before-children, later-sibling-on-top, per ui_obj_t.
static void sys_dsp_paint_node(ui_obj_t *obj, rect_t *band)
{
    if (obj == NULL) {
        return;
    }

    rect_t clip;
    if (obj->draw != NULL && sys_dsp_clip(&clip, &obj->rect, band)) {
        obj->draw(obj, band);
    }

    sys_dsp_paint_node(obj->child, band);
    sys_dsp_paint_node(obj->next, band);
}

static void sys_dsp_render_band(rect_t *band)
{
    xSemaphoreTake(s_tree_lock, portMAX_DELAY);
    sys_dsp_paint_node(s_active_root, band);
    xSemaphoreGive(s_tree_lock);

    if (drv_st7789_set_window(band->x, band->y, band->x + band->w - 1, band->y + band->h - 1) != ESP_OK) {
        return;
    }
    drv_st7789_send_data((uint8_t *)sys_dsp_send_buff, (size_t)band->w * band->h * sizeof(uint16_t));
}

// Splits region into row-bands so each one fits the SYS_DSP_MAX_RENDER_PIXELS
// budget, then renders + flushes each band in turn.
static void sys_dsp_render_region(rect_t region)
{
    uint16_t rows_per_band = region.h;
    if ((uint32_t)region.w * region.h > SYS_DSP_MAX_RENDER_PIXELS) {
        rows_per_band = SYS_DSP_MAX_RENDER_PIXELS / region.w;
        if (rows_per_band == 0) {
            rows_per_band = 1;
        }
    }

    for (uint16_t y = 0; y < region.h; y += rows_per_band) {
        rect_t band = region;
        band.y = region.y + y;
        band.h = (y + rows_per_band < region.h) ? rows_per_band : region.h - y;
        sys_dsp_render_band(&band);
    }
}

// Coalesces overlapping/adjacent rects in place until no further merge is
// possible. rects/count describe a local, unlocked scratch copy.
static void sys_dsp_coalesce(rect_t *rects, size_t *count)
{
    bool merged;
    do {
        merged = false;
        for (size_t i = 0; i < *count && !merged; i++) {
            for (size_t j = i + 1; j < *count; j++) {
                if (sys_dsp_rect_touches(&rects[i], &rects[j])) {
                    sys_dsp_rect_union(&rects[i], &rects[i], &rects[j]);
                    rects[j] = rects[*count - 1];
                    (*count)--;
                    merged = true;
                    break;
                }
            }
        }
    } while (merged);
}

static void sys_dsp_render_task(void *arg)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(SYS_DSP_RENDER_PERIOD_MS));

        rect_t pending[SYS_DSP_MAX_DIRTY_RECTS];
        size_t pending_count = sys_dsp_dirty_pop_pending(pending);
        if (pending_count == 0) {
            continue;
        }

        sys_dsp_coalesce(pending, &pending_count);

        for (size_t i = 0; i < pending_count; i++) {
            sys_dsp_render_region(pending[i]);
        }
    }
}
