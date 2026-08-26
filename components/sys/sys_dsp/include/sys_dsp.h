#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "drv_st7789.h"


typedef struct rect
{
    int16_t  x; // signed: relative coordinates (see ui_obj_t.rect) can be negative
    int16_t  y;
    uint16_t w; // magnitudes -- never negative, unaffected by nesting
    uint16_t h;
} rect_t;


/**
 * @brief Concrete type of a ui_obj_t, set once at registration. Lets
 *        type-specific accessors (sys_dsp_rect_set_color, ...) reject a
 *        mismatched object -- e.g. handing a text object's ui_obj_t* to
 *        sys_dsp_rect_set_color -- instead of reinterpreting ctx as the
 *        wrong struct and corrupting memory.
 */
typedef enum
{
    UI_OBJ_CLASS_ROOT,
    UI_OBJ_CLASS_RECT,
    UI_OBJ_CLASS_PIC,
    UI_OBJ_CLASS_TEXT,
} ui_obj_class_t;

/**
 * @brief Base UI object. Concrete instance types (icon, image, text, shape,
 *        ...) each provide their own draw() and are created through a
 *        per-type registration interface.
 *
 *        rect is this object's position/size RELATIVE TO ITS PARENT (w/h are
 *        absolute magnitudes, unaffected by nesting -- only x/y are
 *        relative, and may be negative, e.g. to place an object partly or
 *        fully above/left of its parent's own origin). For a root, rect is
 *        meaningless (always zero -- roots have no parent frame to be
 *        relative to). Moving an object
 *        (sys_dsp_obj_move) therefore moves its whole subtree along with
 *        it, since every descendant's x/y is an offset within it, not a
 *        screen coordinate. parent/child/next encode the stacking tree:
 *        child is the object's first child, next is the next sibling under
 *        the same parent -- objects are painted parent-before-children, and
 *        siblings paint in next-list order (a later sibling stacks on top of
 *        an earlier one).
 */
typedef struct ui_obj
{
    rect_t rect;
    ui_obj_class_t kind;

    struct ui_obj *parent;
    struct ui_obj *child;
    struct ui_obj *next;

    /**
     * @brief Render this object. `self` is the object being drawn;
     *        `abs_rect` is self's absolute (screen) position/size --
     *        self->rect translated by the position of every ancestor, since
     *        self->rect is relative to self->parent. `band` is the region
     *        currently being rendered (also absolute); implementations must
     *        clip against it themselves (see sys_dsp_clip).
     */
    void (*draw)(struct ui_obj *self, rect_t *abs_rect, rect_t *band);

    void (*free_ctx)(struct ui_obj *self);

    /**
     * @brief Per-type private data (text string+color, image buffer, ...).
     *        May or may not be owned by sys_dsp depending on the type's
     *        register function (e.g. sys_dsp_pic_register does NOT take
     *        ownership of the pixel buffer). free_ctx, if set, is what
     *        actually releases it on sys_dsp_obj_unregister() -- never touch
     *        ctx outside the type's own draw()/free_ctx() implementation.
     */
    void *ctx;
} ui_obj_t;


esp_err_t sys_dsp_init(void);

/**
 * @brief Create a new, independent root: an empty container with no parent
 *        of its own, meant to anchor one whole screen's object tree. Pass it
 *        as `parent` to the top-level rect/pic/text instances of that
 *        screen, then sys_dsp_root_switch() it in to make it the one the
 *        renderer paints. You can create as many roots as you like (e.g.
 *        one per app screen/page) and switch between them; only the active
 *        one is ever drawn.
 * @return New root, or NULL on allocation failure.
 */
ui_obj_t *sys_dsp_root_create(void);

/**
 * @brief Make `root` the one the renderer paints, replacing whatever was
 *        active before. Invalidates the whole panel so the next render pass
 *        fully repaints it from `root`'s tree -- the previous root's
 *        content is not automatically cleared otherwise, since the two
 *        trees don't know what pixels either one occupied.
 * @return ESP_OK, or ESP_ERR_INVALID_ARG if root is NULL or not a root
 *         (i.e. not returned by sys_dsp_root_create()).
 */
esp_err_t sys_dsp_root_switch(ui_obj_t *root);

/**
 * @brief Immediately draw a single line of text straight to the panel,
 *        starting at (x, y) in screen (absolute) coordinates, clipped to
 *        w/h. Bypasses the object tree and dirty-region scheduler entirely
 *        -- intended for one-off/debug output, not for content that lives in
 *        the UI tree. Characters outside the built-in font's range are
 *        rendered as blanks. The area behind each glyph is cleared to black.
 */
esp_err_t sys_dsp_draw_text(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char *text, uint16_t color);

/**
 * @brief Register a text instance under `parent`, which must not be NULL --
 *        either a root from sys_dsp_root_create() or another object in that
 *        root's tree. (x, y) are relative to `parent`'s own position, not
 *        the screen -- see the note on ui_obj_t.rect. `text` is copied into
 *        a buffer sized to fit exactly what w can show -- excess characters
 *        are silently truncated, same as they'd be silently clipped if they
 *        were drawn. Glyphs are 6x8; everything behind/around them within
 *        w/h is transparent -- whatever was painted below (background,
 *        image, ...) shows through unchanged.
 * @return New object, or NULL on allocation failure (including parent ==
 *         NULL).
 */
ui_obj_t *sys_dsp_text_register(ui_obj_t *parent, int16_t x, int16_t y, uint16_t w, uint16_t h,
                                 const char *text, uint16_t color);

/**
 * @brief Replace a text object's content (copied and truncated the same way
 *        as sys_dsp_text_register) and invalidate its area. Safe to call
 *        from any task -- synchronized against the renderer so it never
 *        reads a partially-written string.
 * @return ESP_OK, or ESP_ERR_INVALID_ARG if obj is NULL or not a
 *         UI_OBJ_KIND_TEXT object.
 */
esp_err_t sys_dsp_text_set_text(ui_obj_t *obj, const char *text);

/**
 * @brief Register a solid-color filled-rectangle "shape" instance under
 *        `parent`, which must not be NULL -- either a root from
 *        sys_dsp_root_create() or another object in that root's tree.
 *        (x, y) are relative to `parent`'s own position, not the screen --
 *        see the note on ui_obj_t.rect.
 * @return New object, or NULL on allocation failure (including parent ==
 *         NULL).
 */
ui_obj_t *sys_dsp_rect_register(ui_obj_t *parent, int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t color);

/**
 * @brief Register an image instance under `parent`, which must not be NULL
 *        -- either a root from sys_dsp_root_create() or another object in
 *        that root's tree. (x, y) are relative to `parent`'s own position,
 *        not the screen -- see the note on ui_obj_t.rect. `pic` is a
 *        `w * h` RGB565 buffer stored by reference, not copied or freed by
 *        sys_dsp -- the caller owns it and must keep it valid for the
 *        object's lifetime.
 * @return New object, or NULL on allocation failure (including parent ==
 *         NULL).
 */
ui_obj_t *sys_dsp_pic_register(ui_obj_t *parent, int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *pic);

/**
 * @brief Unregister `obj`: unlinks it from its parent, invalidates the
 *        screen area it and its whole subtree used to occupy so whatever's
 *        underneath gets repainted, then frees it, its subtree, and each
 *        freed node's ctx (via free_ctx, when set). If `obj` is a root, it
 *        isn't linked under anything, so this just invalidates the whole
 *        panel if it was the active root (clearing sys_dsp_root_switch()'s
 *        target) before freeing.
 */
void sys_dsp_obj_unregister(ui_obj_t *obj);

/**
 * @brief Move `obj` to (x, y), relative to its parent (may be negative --
 *        see the note on ui_obj_t.rect), without changing its size. Since
 *        every descendant's position is itself relative to `obj`, this
 *        moves obj's whole subtree along with it. Invalidates both the
 *        vacated screen area and the new one (the union of obj and all its
 *        descendants, before and after) so whatever's underneath gets
 *        repainted. Works on any object kind, including a root (moving one
 *        shifts everything under it).
 * @return ESP_OK, or ESP_ERR_INVALID_ARG if obj is NULL.
 */
esp_err_t sys_dsp_obj_move(ui_obj_t *obj, int16_t x, int16_t y);

/**
 * @brief Change a rect object's fill color and invalidate its area.
 * @return ESP_OK, or ESP_ERR_INVALID_ARG if obj is NULL or not a
 *         UI_OBJ_KIND_RECT object.
 */
esp_err_t sys_dsp_rect_set_color(ui_obj_t *obj, uint16_t color);

/**
 * @brief Change a text object's color and invalidate its area.
 * @return ESP_OK, or ESP_ERR_INVALID_ARG if obj is NULL or not a
 *         UI_OBJ_KIND_TEXT object.
 */
esp_err_t sys_dsp_text_set_color(ui_obj_t *obj, uint16_t color);

/**
 * @brief Mark `rect` dirty so the background renderer repaints it (clipped
 *        to the panel) on its next pass. Safe to call from any task.
 */
void sys_dsp_invalidate(rect_t rect);

/**
 * @brief Schedule a repaint of `obj`'s own screen area (its rect resolved to
 *        absolute coordinates -- NOT including any descendants that extend
 *        past it; invalidate those separately if needed). Asynchronous:
 *        the actual paint (obj, then its children, then its next sibling,
 *        matching the order documented on ui_obj_t) happens on the next
 *        background renderer pass, which always walks the whole active root
 *        so anything underneath is correctly recomposited.
 */
esp_err_t sys_dsp_render(ui_obj_t *obj);


#ifdef __cplusplus
}
#endif
