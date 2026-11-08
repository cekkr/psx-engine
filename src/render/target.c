#include "psxe/render.h"

#include <stdint.h>
#include <string.h>

#include <inline_c.h>

#include "render_internal.h"

/* Debug font loaded by psxe_renderer_init: 32x32 halfword image at (960, 0)
 * plus its 16-entry CLUT on the next row. */
#define PSXE_FONT_X 960
#define PSXE_FONT_Y 0
#define PSXE_FONT_W 32
#define PSXE_FONT_H 33

static bool psxe_rect_overlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
    return (ax < (bx + bw)) && (bx < (ax + aw)) && (ay < (by + bh)) && (by < (ay + ah));
}

/* Builds a drawing-environment packet with public SDK macros only. */
static void psxe_build_env(
    DR_ENV *packet,
    int x,
    int y,
    int width,
    int height,
    bool dither,
    bool clear,
    psxe_color clear_color
) {
    setlen(packet, clear ? 8 : 5);
    setDrawTPage_T(&packet->tpage, 0, dither ? 1 : 0, 0x0a);
    packet->twin.code[0] = 0xe2000000u; /* no texture window */
    setDrawAreaXY_T(&packet->area, x, y, x + width - 1, y + height - 1);
    setDrawOffset_T(&packet->offset, x, y);
    if (clear) {
        setFill_T(&packet->fill);
        setRGB0(&packet->fill, clear_color.r, clear_color.g, clear_color.b);
        packet->fill.x0 = (uint16_t)x;
        packet->fill.y0 = (uint16_t)y;
        packet->fill.w = (uint16_t)width;
        packet->fill.h = (uint16_t)height;
    }
    termPrim(packet);
}

bool psxe_render_target_init(
    psxe_render_target *target,
    const psxe_renderer *renderer,
    const psxe_render_target_config *config
) {
    int x;
    int y;
    int w;
    int h;

    if (target == NULL) {
        return false;
    }
    memset(target, 0, sizeof(*target));
    if ((renderer == NULL) || (config == NULL)) {
        return false;
    }

    x = config->x;
    y = config->y;
    w = config->width;
    h = config->height;

    /* FILL needs 16-pixel horizontal alignment; u/v are 8-bit offsets inside
     * one 256x256 texel page, which a 16-bit page starts every 64 columns. */
    if ((x < 0) || (y < 0) || ((x & 15) != 0) || ((w & 15) != 0) ||
        (w < 16) || (w > 256) || (h < 1) || (h > 256) ||
        ((x + w) > 1024) || ((y + h) > 512) ||
        (((x & 63) + w) > 256) || (((y & 255) + h) > 256)) {
        return false;
    }
    if ((config->ot_length < 3u) || (config->ot_length > PSXE_OT_LENGTH) ||
        (config->depth_shift > 15u)) {
        return false;
    }
    if (psxe_rect_overlap(x, y, w, h, 0, 0, renderer->width, renderer->height * 2) ||
        psxe_rect_overlap(x, y, w, h, PSXE_FONT_X, PSXE_FONT_Y, PSXE_FONT_W, PSXE_FONT_H)) {
        return false;
    }

    target->x = (int16_t)x;
    target->y = (int16_t)y;
    target->width = (int16_t)w;
    target->height = (int16_t)h;
    target->clear_color = config->clear_color;
    target->clear = config->clear;
    target->ot_length = config->ot_length;
    target->depth_shift = config->depth_shift;

    target->texture.tpage = (uint16_t)getTPage(2, 0, x, y);
    target->texture.clut = 0;
    target->texture.u = (uint8_t)(x & 63);
    target->texture.v = (uint8_t)(y & 255);
    target->texture.width = (uint16_t)w;
    target->texture.height = (uint16_t)h;
    target->texture.valid = true;
    target->valid = true;
    return true;
}

bool psxe_render_target_begin(psxe_renderer *renderer, const psxe_render_target *target) {
    uint32_t *ordering_table;
    DR_ENV *enter;
    DR_ENV *leave;
    const DRAWENV *main_env;

    if ((renderer == NULL) || (target == NULL) || !target->valid ||
        (renderer->active_target != NULL)) {
        return false;
    }

    /* All pass storage comes from this frame's packet arena, so it shares the
     * double-buffered lifetime of every other packet. All or nothing. */
    ordering_table = psxe_renderer_allocate(renderer, (size_t)target->ot_length * sizeof(uint32_t));
    if (ordering_table == NULL) {
        return false;
    }
    enter = psxe_renderer_allocate(renderer, sizeof(*enter));
    if (enter == NULL) {
        return false;
    }
    leave = psxe_renderer_allocate(renderer, sizeof(*leave));
    if (leave == NULL) {
        return false;
    }

    ClearOTagR(ordering_table, target->ot_length);
    psxe_build_env(
        enter, target->x, target->y, target->width, target->height,
        renderer->dither, target->clear, target->clear_color
    );
    main_env = &renderer->buffers[renderer->active_buffer].draw;
    psxe_build_env(
        leave, main_env->clip.x, main_env->clip.y, main_env->clip.w, main_env->clip.h,
        renderer->dither, false, target->clear_color
    );

    /* enter -> target OT (far to near) -> bucket 0 ... -> leave -> next pass
     * or the main OT. leave goes into bucket 0 first so every later bucket-0
     * insertion executes before it. */
    setaddr(enter, &ordering_table[target->ot_length - 1u]);
    addPrim(&ordering_table[0], leave);
    if (renderer->pass_tail != NULL) {
        setaddr(renderer->pass_tail, enter);
    } else {
        renderer->pass_head = enter;
    }
    renderer->pass_tail = leave;

    renderer->active_target = target;
    renderer->saved_camera = renderer->camera;
    renderer->saved_depth_shift = renderer->depth_shift;
    renderer->ordering_table = ordering_table;
    renderer->ot_length = target->ot_length;
    renderer->depth_shift = target->depth_shift;
    gte_SetGeomOffset(target->width / 2, target->height / 2);
    ++renderer->target_passes;
    return true;
}

void psxe_render_target_end(psxe_renderer *renderer) {
    if ((renderer == NULL) || (renderer->active_target == NULL)) {
        return;
    }
    renderer->ordering_table = renderer->buffers[renderer->active_buffer].ordering_table;
    renderer->ot_length = PSXE_OT_LENGTH;
    renderer->depth_shift = renderer->saved_depth_shift;
    renderer->active_target = NULL;
    gte_SetGeomOffset(renderer->width / 2, renderer->height / 2);
    psxe_renderer_set_camera(renderer, &renderer->saved_camera);
}

const psxe_texture *psxe_render_target_texture(const psxe_render_target *target) {
    if ((target == NULL) || !target->valid) {
        return NULL;
    }
    return &target->texture;
}
