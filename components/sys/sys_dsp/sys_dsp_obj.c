#include "sys_dsp.h"
#include "sys_dsp_priv.h"
#include "sys_dsp_font.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SYS_DSP_TEXT_BG_COLOR 0x0000

ui_obj_t *s_active_root;
SemaphoreHandle_t s_tree_lock;

esp_err_t sys_dsp_obj_init(void)
{
    if (s_tree_lock == NULL) {
        s_tree_lock = xSemaphoreCreateMutex();
        if (s_tree_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}


// --- roots ----------------------------------------------------------------

ui_obj_t *sys_dsp_root_create(void)
{
    ui_obj_t *root = calloc(1, sizeof(ui_obj_t));
    if (root == NULL) {
        return NULL;
    }
    root->kind = UI_OBJ_CLASS_ROOT;
    return root;
}

esp_err_t sys_dsp_root_switch(ui_obj_t *root)
{
    if (root == NULL || root->kind != UI_OBJ_CLASS_ROOT) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_tree_lock, portMAX_DELAY);
    s_active_root = root;
    xSemaphoreGive(s_tree_lock);

    static const rect_t panel = { 0, 0, SYS_DSP_PANEL_WIDTH, SYS_DSP_PANEL_HEIGHT };
    sys_dsp_invalidate(panel);
    return ESP_OK;
}


// --- object tree --------------------------------------------------------

static void sys_dsp_free_ctx_simple(ui_obj_t *self)
{
    free(self->ctx);
}

// parent must not be NULL -- only sys_dsp_root_create() may produce a
// parentless node. `rect` is relative to parent, per ui_obj_t.rect.
static ui_obj_t *sys_dsp_obj_alloc(ui_obj_t *parent, rect_t rect, ui_obj_class_t kind, void *ctx,
                                    void (*draw)(ui_obj_t *, rect_t *, rect_t *),
                                    void (*free_ctx)(ui_obj_t *))
{
    if (parent == NULL) {
        return NULL;
    }

    ui_obj_t *obj = calloc(1, sizeof(ui_obj_t));
    if (obj == NULL) {
        return NULL;
    }

    obj->rect = rect;
    obj->kind = kind;
    obj->ctx = ctx;
    obj->draw = draw;
    obj->free_ctx = free_ctx;
    obj->parent = parent;

    xSemaphoreTake(s_tree_lock, portMAX_DELAY);
    if (parent->child == NULL) {
        parent->child = obj;
    } else {
        ui_obj_t *last = parent->child;
        while (last->next != NULL) {
            last = last->next;
        }
        last->next = obj;
    }
    rect_t abs_rect = sys_dsp_obj_abs_rect(obj);
    xSemaphoreGive(s_tree_lock);

    sys_dsp_invalidate(abs_rect);

    return obj;
}

// Unions obj's absolute rect and its whole descendant subtree (children,
// grandchildren, ...) into *out -- NOT obj's siblings. Caller must hold
// s_tree_lock and pass *first = true on the outermost call.
static void sys_dsp_obj_bbox(ui_obj_t *obj, rect_t *out, bool *first)
{
    rect_t abs_rect = sys_dsp_obj_abs_rect(obj);

    if (*first) {
        *out = abs_rect;
        *first = false;
    } else {
        sys_dsp_rect_union(out, out, &abs_rect);
    }

    for (ui_obj_t *child = obj->child; child != NULL; child = child->next) {
        sys_dsp_obj_bbox(child, out, first);
    }
}

static void sys_dsp_obj_free_subtree(ui_obj_t *obj)
{
    if (obj == NULL) {
        return;
    }

    ui_obj_t *child = obj->child;
    while (child != NULL) {
        ui_obj_t *next_child = child->next;
        sys_dsp_obj_free_subtree(child);
        child = next_child;
    }

    if (obj->free_ctx != NULL) {
        obj->free_ctx(obj);
    }
    free(obj);
}

void sys_dsp_obj_unregister(ui_obj_t *obj)
{
    if (obj == NULL) {
        return;
    }

    bool was_active_root = false;
    rect_t bbox = { 0, 0, 0, 0 };

    xSemaphoreTake(s_tree_lock, portMAX_DELAY);
    if (obj->parent != NULL) {
        ui_obj_t **link = &obj->parent->child;
        while (*link != NULL && *link != obj) {
            link = &(*link)->next;
        }
        if (*link == obj) {
            *link = obj->next;
        }
        bool first = true;
        sys_dsp_obj_bbox(obj, &bbox, &first);
    } else if (s_active_root == obj) {
        // Roots aren't linked into any parent's child list -- the only
        // reference to one is s_active_root, if it's the selected one.
        s_active_root = NULL;
        was_active_root = true;
    }
    xSemaphoreGive(s_tree_lock);

    if (was_active_root) {
        static const rect_t panel = { 0, 0, SYS_DSP_PANEL_WIDTH, SYS_DSP_PANEL_HEIGHT };
        sys_dsp_invalidate(panel);
    } else {
        // Covers obj's whole subtree, not just its own rect -- a descendant
        // can extend past its parent, since children aren't clipped to it.
        sys_dsp_invalidate(bbox);
    }
    sys_dsp_obj_free_subtree(obj);
}

esp_err_t sys_dsp_obj_move(ui_obj_t *obj, int16_t x, int16_t y)
{
    if (obj == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    rect_t old_bbox = { 0, 0, 0, 0 };
    rect_t new_bbox = { 0, 0, 0, 0 };
    bool first;

    xSemaphoreTake(s_tree_lock, portMAX_DELAY);
    first = true;
    sys_dsp_obj_bbox(obj, &old_bbox, &first);

    obj->rect.x = x;
    obj->rect.y = y;

    first = true;
    sys_dsp_obj_bbox(obj, &new_bbox, &first);
    xSemaphoreGive(s_tree_lock);

    // obj's whole subtree moved along with it (descendants are relative to
    // obj), so both the vacated and the new bounding box need repainting.
    sys_dsp_invalidate(old_bbox);
    sys_dsp_invalidate(new_bbox);
    return ESP_OK;
}


// --- draw callbacks -------------------------------------------------------
//
// Each draw(self, abs_rect, band) call composites self's contribution into
// sys_dsp_send_buff, which represents *band* (not abs_rect): row stride is
// band->w, and the top-left of the buffer is (band->x, band->y). abs_rect is
// self->rect resolved to absolute (screen) coordinates -- self->rect itself
// is relative to self->parent, so it's not usable directly here. Colors are
// byte-swapped on the way in, matching drv_st7789_fill's convention that the
// wire format is big-endian RGB565 while callers/ctx pass/store it host
// (little-endian) order.

static inline uint16_t sys_dsp_swap16(uint16_t c)
{
    return (uint16_t)((c << 8) | (c >> 8));
}

static void sys_dsp_rect_draw(ui_obj_t *self, rect_t *abs_rect, rect_t *band)
{
    rect_t clip;
    if (!sys_dsp_clip(&clip, abs_rect, band)) {
        return;
    }

    uint16_t color = sys_dsp_swap16(*(uint16_t *)self->ctx);
    for (uint16_t row = 0; row < clip.h; row++) {
        uint16_t *dst = sys_dsp_send_buff + (size_t)(clip.y - band->y + row) * band->w + (clip.x - band->x);
        for (uint16_t col = 0; col < clip.w; col++) {
            dst[col] = color;
        }
    }
}

ui_obj_t *sys_dsp_rect_register(ui_obj_t *parent, int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    uint16_t *ctx = malloc(sizeof(uint16_t));
    if (ctx == NULL) {
        return NULL;
    }
    *ctx = color;

    rect_t rect = { .x = x, .y = y, .w = w, .h = h };
    ui_obj_t *obj = sys_dsp_obj_alloc(parent, rect, UI_OBJ_CLASS_RECT, ctx, sys_dsp_rect_draw, sys_dsp_free_ctx_simple);
    if (obj == NULL) {
        free(ctx);
    }
    return obj;
}

esp_err_t sys_dsp_rect_set_color(ui_obj_t *obj, uint16_t color)
{
    if (obj == NULL || obj->kind != UI_OBJ_CLASS_RECT) {
        return ESP_ERR_INVALID_ARG;
    }

    rect_t abs_rect;
    xSemaphoreTake(s_tree_lock, portMAX_DELAY);
    *(uint16_t *)obj->ctx = color;
    abs_rect = sys_dsp_obj_abs_rect(obj);
    xSemaphoreGive(s_tree_lock);

    sys_dsp_invalidate(abs_rect);
    return ESP_OK;
}

static void sys_dsp_pic_draw(ui_obj_t *self, rect_t *abs_rect, rect_t *band)
{
    rect_t clip;
    if (!sys_dsp_clip(&clip, abs_rect, band)) {
        return;
    }

    const uint16_t *pic = self->ctx;
    for (uint16_t row = 0; row < clip.h; row++) {
        const uint16_t *src = pic + (size_t)(clip.y - abs_rect->y + row) * abs_rect->w + (clip.x - abs_rect->x);
        uint16_t *dst = sys_dsp_send_buff + (size_t)(clip.y - band->y + row) * band->w + (clip.x - band->x);
        for (uint16_t col = 0; col < clip.w; col++) {
            dst[col] = sys_dsp_swap16(src[col]);
        }
    }
}

ui_obj_t *sys_dsp_pic_register(ui_obj_t *parent, int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *pic)
{
    rect_t rect = { .x = x, .y = y, .w = w, .h = h };
    return sys_dsp_obj_alloc(parent, rect, UI_OBJ_CLASS_PIC, pic, sys_dsp_pic_draw, NULL);
}

typedef struct {
    char *text;   // owned, NUL-terminated, `cap` bytes total
    size_t cap;
    uint16_t color;
} sys_dsp_text_ctx_t;

static void sys_dsp_text_free_ctx(ui_obj_t *self)
{
    sys_dsp_text_ctx_t *ctx = self->ctx;
    free(ctx->text);
    free(ctx);
}

// True if the self_col/self_row pixel (relative to self->rect's origin) is a
// "lit" pixel of `text`'s glyphs; false if it falls outside the string, the
// font's range, or the font's row height.
static bool sys_dsp_glyph_bit(const char *text, size_t text_len, uint16_t self_col, uint16_t self_row)
{
    if (self_row >= SYS_DSP_FONT_HEIGHT) {
        return false;
    }

    size_t char_idx = self_col / SYS_DSP_FONT_WIDTH;
    if (char_idx >= text_len) {
        return false;
    }

    char c = text[char_idx];
    if (c < SYS_DSP_FONT_FIRST_CHAR || c > SYS_DSP_FONT_LAST_CHAR) {
        return false;
    }

    uint16_t glyph_col = self_col % SYS_DSP_FONT_WIDTH;
    uint8_t col_bits = sys_dsp_font6x8[c - SYS_DSP_FONT_FIRST_CHAR][glyph_col];
    return (col_bits & (1u << self_row)) != 0;
}

static void sys_dsp_text_draw(ui_obj_t *self, rect_t *abs_rect, rect_t *band)
{
    rect_t clip;
    if (!sys_dsp_clip(&clip, abs_rect, band)) {
        return;
    }

    sys_dsp_text_ctx_t *ctx = self->ctx;
    uint16_t fg = sys_dsp_swap16(ctx->color);
    size_t text_len = strlen(ctx->text);

    for (uint16_t row = 0; row < clip.h; row++) {
        uint16_t self_row = clip.y - abs_rect->y + row;
        uint16_t *dst = sys_dsp_send_buff + (size_t)(clip.y - band->y + row) * band->w + (clip.x - band->x);
        for (uint16_t col = 0; col < clip.w; col++) {
            uint16_t self_col = clip.x - abs_rect->x + col;
            // Off-glyph pixels are left untouched -- whatever an
            // earlier-painted object put there (background, image, ...)
            // shows through instead of being overwritten.
            if (sys_dsp_glyph_bit(ctx->text, text_len, self_col, self_row)) {
                dst[col] = fg;
            }
        }
    }
}

ui_obj_t *sys_dsp_text_register(ui_obj_t *parent, int16_t x, int16_t y, uint16_t w, uint16_t h,
                                 const char *text, uint16_t color)
{
    sys_dsp_text_ctx_t *ctx = malloc(sizeof(sys_dsp_text_ctx_t));
    if (ctx == NULL) {
        return NULL;
    }

    // Cap fits the widest string w can ever show (ceil(w / glyph width)),
    // so anything longer is truncated the same as it'd otherwise be clipped.
    size_t cap = (size_t)(w + SYS_DSP_FONT_WIDTH - 1) / SYS_DSP_FONT_WIDTH + 1;
    char *buf = malloc(cap);
    if (buf == NULL) {
        free(ctx);
        return NULL;
    }
    snprintf(buf, cap, "%s", text != NULL ? text : "");

    ctx->text = buf;
    ctx->cap = cap;
    ctx->color = color;

    rect_t rect = { .x = x, .y = y, .w = w, .h = h };
    ui_obj_t *obj = sys_dsp_obj_alloc(parent, rect, UI_OBJ_CLASS_TEXT, ctx, sys_dsp_text_draw, sys_dsp_text_free_ctx);
    if (obj == NULL) {
        free(buf);
        free(ctx);
    }
    return obj;
}

esp_err_t sys_dsp_text_set_text(ui_obj_t *obj, const char *text)
{
    if (obj == NULL || obj->kind != UI_OBJ_CLASS_TEXT) {
        return ESP_ERR_INVALID_ARG;
    }

    sys_dsp_text_ctx_t *ctx = obj->ctx;
    rect_t abs_rect;
    xSemaphoreTake(s_tree_lock, portMAX_DELAY);
    snprintf(ctx->text, ctx->cap, "%s", text != NULL ? text : "");
    abs_rect = sys_dsp_obj_abs_rect(obj);
    xSemaphoreGive(s_tree_lock);

    sys_dsp_invalidate(abs_rect);
    return ESP_OK;
}

esp_err_t sys_dsp_text_set_color(ui_obj_t *obj, uint16_t color)
{
    if (obj == NULL || obj->kind != UI_OBJ_CLASS_TEXT) {
        return ESP_ERR_INVALID_ARG;
    }

    rect_t abs_rect;
    xSemaphoreTake(s_tree_lock, portMAX_DELAY);
    ((sys_dsp_text_ctx_t *)obj->ctx)->color = color;
    abs_rect = sys_dsp_obj_abs_rect(obj);
    xSemaphoreGive(s_tree_lock);

    sys_dsp_invalidate(abs_rect);
    return ESP_OK;
}

esp_err_t sys_dsp_draw_text(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char *text, uint16_t color)
{
    uint16_t height = SYS_DSP_FONT_HEIGHT < h ? SYS_DSP_FONT_HEIGHT : h;
    if (height == 0) {
        return ESP_OK;
    }

    uint16_t max_width = SYS_DSP_MAX_RENDER_PIXELS / height;
    uint16_t width = w < max_width ? w : max_width;
    if (width == 0) {
        return ESP_OK;
    }

    uint16_t fg = sys_dsp_swap16(color);
    uint16_t bg = sys_dsp_swap16(SYS_DSP_TEXT_BG_COLOR);
    size_t text_len = strlen(text);

    for (uint16_t row = 0; row < height; row++) {
        uint16_t *dst = sys_dsp_send_buff + (size_t)row * width;
        for (uint16_t col = 0; col < width; col++) {
            dst[col] = sys_dsp_glyph_bit(text, text_len, col, row) ? fg : bg;
        }
    }

    esp_err_t ret = drv_st7789_set_window(x, y, x + width - 1, y + height - 1);
    if (ret != ESP_OK) {
        return ret;
    }
    return drv_st7789_send_data((uint8_t *)sys_dsp_send_buff, (size_t)width * height * sizeof(uint16_t));
}
