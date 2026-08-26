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
// mistaken for a real rect). *out may alias a or b. x/y are signed (an
// object's absolute position can be negative, e.g. partly off the top/left
// edge of the panel); math is done in int32_t so a/b's x/y+w/h never
// overflows an int16_t before the min/max comparisons run.
static inline uint32_t sys_dsp_clip(rect_t *out, const rect_t *a, const rect_t *b)
{
    int32_t x0 = a->x > b->x ? a->x : b->x;
    int32_t y0 = a->y > b->y ? a->y : b->y;
    int32_t ax1 = (int32_t)a->x + a->w, bx1 = (int32_t)b->x + b->w;
    int32_t ay1 = (int32_t)a->y + a->h, by1 = (int32_t)b->y + b->h;
    int32_t x1 = ax1 < bx1 ? ax1 : bx1;
    int32_t y1 = ay1 < by1 ? ay1 : by1;

    if (x1 <= x0 || y1 <= y0) {
        *out = (rect_t){ 0, 0, 0, 0 };
        return 0;
    }

    out->x = (int16_t)x0;
    out->y = (int16_t)y0;
    out->w = (uint16_t)(x1 - x0);
    out->h = (uint16_t)(y1 - y0);
    return (uint32_t)out->w * out->h;
}

// True if a and b overlap or share an edge (adjacent dirty rects get
// coalesced into one SPI transfer instead of two).
static inline bool sys_dsp_rect_touches(const rect_t *a, const rect_t *b)
{
    return (int32_t)a->x <= (int32_t)b->x + b->w && (int32_t)b->x <= (int32_t)a->x + a->w &&
           (int32_t)a->y <= (int32_t)b->y + b->h && (int32_t)b->y <= (int32_t)a->y + a->h;
}

static inline void sys_dsp_rect_union(rect_t *out, const rect_t *a, const rect_t *b)
{
    int32_t x0 = a->x < b->x ? a->x : b->x;
    int32_t y0 = a->y < b->y ? a->y : b->y;
    int32_t ax1 = (int32_t)a->x + a->w, bx1 = (int32_t)b->x + b->w;
    int32_t ay1 = (int32_t)a->y + a->h, by1 = (int32_t)b->y + b->h;
    int32_t x1 = ax1 > bx1 ? ax1 : bx1;
    int32_t y1 = ay1 > by1 ? ay1 : by1;
    out->x = (int16_t)x0;
    out->y = (int16_t)y0;
    out->w = (uint16_t)(x1 - x0);
    out->h = (uint16_t)(y1 - y0);
}


// --- object tree (sys_dsp_obj.c) ---------------------------------------

// The root currently selected by sys_dsp_root_switch() -- the renderer only
// ever paints this one tree. NULL means nothing is selected (nothing to
// paint).
extern ui_obj_t *s_active_root;
extern SemaphoreHandle_t s_tree_lock;

// Resolves obj's absolute (screen) rect: obj->rect translated by the
// position of every ancestor, since obj->rect is relative to obj->parent
// (w/h are absolute magnitudes, untouched by nesting). Caller must hold
// s_tree_lock -- an ancestor's rect can otherwise change mid-walk.
static inline rect_t sys_dsp_obj_abs_rect(const ui_obj_t *obj)
{
    rect_t abs_rect = obj->rect;
    for (const ui_obj_t *p = obj->parent; p != NULL; p = p->parent) {
        abs_rect.x = (int16_t)(abs_rect.x + p->rect.x);
        abs_rect.y = (int16_t)(abs_rect.y + p->rect.y);
    }
    return abs_rect;
}

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
