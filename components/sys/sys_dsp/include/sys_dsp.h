#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "drv_st7789.h"


typedef struct rect
{
    int16_t  x; // signed: relative coordinates (see ui_obj_t.rect) can be negative
    int16_t  y;
    uint16_t w; // magnitudes -- never negative, unaffected by nesting
    uint16_t h;
} rect_t;


typedef enum
{
    UI_OBJ_CLASS_ROOT,
    UI_OBJ_CLASS_RECT,
    UI_OBJ_CLASS_PIC,
    UI_OBJ_CLASS_TEXT,
} ui_obj_class_t;

typedef struct ui_obj
{
    rect_t rect;
    ui_obj_class_t kind;
    uint8_t invalid;

    struct ui_obj *parent;
    struct ui_obj *child;
    struct ui_obj *next;

    void (*draw)(struct ui_obj *self, rect_t *abs_rect, rect_t *band);

    void (*free_ctx)(struct ui_obj *self);

    void *ctx;
} ui_obj_t;


esp_err_t sys_dsp_init(void);

ui_obj_t *sys_dsp_root_create(void);

esp_err_t sys_dsp_root_switch(ui_obj_t *root);

esp_err_t sys_dsp_draw_text(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char *text, uint16_t color);

ui_obj_t *sys_dsp_text_register(ui_obj_t *parent, int16_t x, int16_t y, uint16_t w, uint16_t h,
                                 const char *text, uint16_t color);

esp_err_t sys_dsp_text_set_text(ui_obj_t *obj, const char *text);

ui_obj_t *sys_dsp_rect_register(ui_obj_t *parent, int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t color);

ui_obj_t *sys_dsp_pic_register(ui_obj_t *parent, int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *pic);

void sys_dsp_obj_unregister(ui_obj_t *obj);

esp_err_t sys_dsp_obj_move(ui_obj_t *obj, int16_t x, int16_t y);

// Hides (invalid = true) or shows (invalid = false) obj and its whole
// subtree: while invalid, neither obj nor any descendant is drawn or
// contributes to dirty-region bookkeeping (sys_dsp_obj_bbox, used by
// unregister/move). Invalidates the affected screen area so the change
// actually repaints.
esp_err_t sys_dsp_obj_set_invalid(ui_obj_t *obj, bool invalid);

esp_err_t sys_dsp_rect_set_color(ui_obj_t *obj, uint16_t color);

esp_err_t sys_dsp_text_set_color(ui_obj_t *obj, uint16_t color);

void sys_dsp_invalidate(rect_t rect);

esp_err_t sys_dsp_render(ui_obj_t *obj);


#ifdef __cplusplus
}
#endif
