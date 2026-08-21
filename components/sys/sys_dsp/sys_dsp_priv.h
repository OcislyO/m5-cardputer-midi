#pragma once

// Internal state/helpers shared between sys_dsp's translation units. Not a
// public header -- lives outside include/, application code must not use it.

#include "sys_dsp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdbool.h>
#include <stddef.h>

#define SYS_DSP_PANEL_WIDTH  240
#define SYS_DSP_PANEL_HEIGHT 135

// One SPI burst never carries more than this many pixels -- keeps a single
// render pass well within sys_dsp_send_buff and off the SPI bus long enough
// to starve other tasks.
#define SYS_DSP_MAX_RENDER_PIXELS 2046
extern uint16_t sys_dsp_send_buff[SYS_DSP_MAX_RENDER_PIXELS];

#define SYS_DSP_MAX_DIRTY_RECTS 8


// --- rect helpers -----------------------------------------------------
//
// Small and pure enough to share as `static inline`: each TU that includes
// this header gets its own copy, no extra translation unit needed, and an
// unused one (not every TU needs all three) is silently dropped.

// Intersects a and b into *out; returns the intersection's pixel area (0 if
// they don't overlap, and *out is zeroed so a zero-area result is never
// mistaken for a real rect). *out may alias a or b.
static inline uint32_t sys_dsp_clip(rect_t *out, const rect_t *a, const rect_t *b)
{
    uint16_t x0 = a->x > b->x ? a->x : b->x;
    uint16_t y0 = a->y > b->y ? a->y : b->y;
    uint16_t ax1 = a->x + a->w, bx1 = b->x + b->w;
    uint16_t ay1 = a->y + a->h, by1 = b->y + b->h;
    uint16_t x1 = ax1 < bx1 ? ax1 : bx1;
    uint16_t y1 = ay1 < by1 ? ay1 : by1;

    if (x1 <= x0 || y1 <= y0) {
        *out = (rect_t){ 0, 0, 0, 0 };
        return 0;
    }

    out->x = x0;
    out->y = y0;
    out->w = x1 - x0;
    out->h = y1 - y0;
    return (uint32_t)out->w * out->h;
}

// True if a and b overlap or share an edge (adjacent dirty rects get
// coalesced into one SPI transfer instead of two).
static inline bool sys_dsp_rect_touches(const rect_t *a, const rect_t *b)
{
    return a->x <= b->x + b->w && b->x <= a->x + a->w &&
           a->y <= b->y + b->h && b->y <= a->y + a->h;
}

static inline void sys_dsp_rect_union(rect_t *out, const rect_t *a, const rect_t *b)
{
    uint16_t x0 = a->x < b->x ? a->x : b->x;
    uint16_t y0 = a->y < b->y ? a->y : b->y;
    uint16_t x1 = (a->x + a->w) > (b->x + b->w) ? (a->x + a->w) : (b->x + b->w);
    uint16_t y1 = (a->y + a->h) > (b->y + b->h) ? (a->y + a->h) : (b->y + b->h);
    out->x = x0;
    out->y = y0;
    out->w = x1 - x0;
    out->h = y1 - y0;
}


// --- object tree (sys_dsp_obj.c) ---------------------------------------

// The root currently selected by sys_dsp_root_switch() -- the renderer only
// ever paints this one tree. NULL means nothing is selected (nothing to
// paint).
extern ui_obj_t *s_active_root;
extern SemaphoreHandle_t s_tree_lock;

// Idempotent: safe to call again after the first successful call.
esp_err_t sys_dsp_obj_init(void);


// --- dirty region list (sys_dsp_dirty.c) -------------------------------

// Idempotent: safe to call again after the first successful call.
esp_err_t sys_dsp_dirty_init(void);

// Copies up to SYS_DSP_MAX_DIRTY_RECTS pending rects into `out`, clears the
// pending list, and returns how many were copied. Safe to call from any task.
size_t sys_dsp_dirty_pop_pending(rect_t out[SYS_DSP_MAX_DIRTY_RECTS]);


// --- renderer (sys_dsp_render.c) ---------------------------------------

// Idempotent: safe to call again after the first successful call.
esp_err_t sys_dsp_render_start(void);
