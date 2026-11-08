#include "psxe/render.h"

#include <stdint.h>

#include "render_internal.h"

static uint8_t psxe_clamp_texel(unsigned int value) {
    return (uint8_t)((value > 255u) ? 255u : value);
}

void psxe_sprite_init(psxe_sprite *sprite, const psxe_texture *texture) {
    if (sprite == NULL) {
        return;
    }
    sprite->x = 0;
    sprite->y = 0;
    sprite->width = 0;
    sprite->height = 0;
    sprite->u = 0;
    sprite->v = 0;
    sprite->region_width = 0;
    sprite->region_height = 0;
    sprite->layer = 1;
    sprite->material = psxe_material_default();
    sprite->visible = true;
    psxe_sprite_set_texture(sprite, texture);
    if (texture == NULL) {
        sprite->width = 16;
        sprite->height = 16;
    }
}

void psxe_sprite_set_texture(psxe_sprite *sprite, const psxe_texture *texture) {
    if (sprite == NULL) {
        return;
    }
    sprite->material.texture = texture;
    sprite->u = 0;
    sprite->v = 0;
    if ((texture == NULL) || !texture->valid) {
        sprite->region_width = 0;
        sprite->region_height = 0;
        return;
    }
    sprite->region_width = (texture->width > 256u) ? 256u : texture->width;
    sprite->region_height = (texture->height > 256u) ? 256u : texture->height;
    if ((sprite->width <= 0) || (sprite->height <= 0)) {
        sprite->width = (int16_t)sprite->region_width;
        sprite->height = (int16_t)sprite->region_height;
    }
}

void psxe_sprite_set_region(psxe_sprite *sprite, uint8_t u, uint8_t v, uint16_t width, uint16_t height) {
    if (sprite == NULL) {
        return;
    }
    sprite->u = u;
    sprite->v = v;
    sprite->region_width = width;
    sprite->region_height = height;
}

void psxe_sprite_set_position(psxe_sprite *sprite, int16_t x, int16_t y) {
    if (sprite != NULL) {
        sprite->x = x;
        sprite->y = y;
    }
}

void psxe_sprite_move(psxe_sprite *sprite, int16_t dx, int16_t dy) {
    if (sprite != NULL) {
        sprite->x = (int16_t)(sprite->x + dx);
        sprite->y = (int16_t)(sprite->y + dy);
    }
}

void psxe_sprite_set_size(psxe_sprite *sprite, int16_t width, int16_t height) {
    if (sprite != NULL) {
        sprite->width = width;
        sprite->height = height;
    }
}

void psxe_sprite_set_color(psxe_sprite *sprite, psxe_color color) {
    if (sprite != NULL) {
        sprite->material.color = color;
    }
}

void psxe_sprite_set_effects(psxe_sprite *sprite, uint8_t effects, psxe_blend_mode blend) {
    if (sprite != NULL) {
        psxe_material_set_effects(&sprite->material, effects, blend);
    }
}

void psxe_sprite_set_layer(psxe_sprite *sprite, uint16_t layer) {
    if (sprite != NULL) {
        sprite->layer = layer;
    }
}

static void psxe_sprite_draw_solid(
    psxe_renderer *renderer,
    const psxe_sprite *sprite,
    uint32_t *bucket,
    bool semitransparent,
    uint8_t blend
) {
    TILE *tile;
    DR_TPAGE *page = NULL;

    if (semitransparent) {
        page = psxe_renderer_allocate(renderer, sizeof(*page));
        if (page == NULL) {
            return;
        }
    }
    tile = psxe_renderer_allocate(renderer, sizeof(*tile));
    if (tile == NULL) {
        return;
    }
    setTile(tile);
    setXY0(tile, sprite->x, sprite->y);
    setWH(tile, sprite->width, sprite->height);
    setRGB0(tile, sprite->material.color.r, sprite->material.color.g, sprite->material.color.b);
    setSemiTrans(tile, semitransparent ? 1 : 0);
    addPrim(bucket, tile);
    ++renderer->submitted_primitives;
    if (page != NULL) {
        setDrawTPage(page, 0, renderer->dither ? 1 : 0, getTPage(0, blend, 0, 0));
        addPrim(bucket, page);
        ++renderer->submitted_primitives;
    }
}

void psxe_sprite_draw(psxe_renderer *renderer, const psxe_sprite *sprite) {
    const psxe_material *material;
    const psxe_texture *texture;
    uint32_t *bucket;
    bool semitransparent;
    uint8_t blend;
    uint16_t tpage;
    unsigned int region_w;
    unsigned int region_h;
    unsigned int u;
    unsigned int v;
    psxe_color tint;

    if ((renderer == NULL) || (sprite == NULL) || !sprite->visible ||
        (sprite->width <= 0) || (sprite->height <= 0)) {
        return;
    }

    material = &sprite->material;
    texture = material->texture;
    semitransparent = (material->effects & PSXE_EFFECT_SEMITRANSPARENT) != 0;
    blend = (uint8_t)(material->blend & 3u);
    bucket = psxe_renderer_bucket(renderer, sprite->layer);

    if ((texture == NULL) || !texture->valid || (material->effects & PSXE_EFFECT_UNTEXTURED)) {
        psxe_sprite_draw_solid(renderer, sprite, bucket, semitransparent, blend);
        return;
    }

    region_w = (sprite->region_width != 0u) ? sprite->region_width : (unsigned int)sprite->width;
    region_h = (sprite->region_height != 0u) ? sprite->region_height : (unsigned int)sprite->height;
    u = (unsigned int)texture->u + sprite->u;
    v = (unsigned int)texture->v + sprite->v;
    tpage = psxe_tpage_with_blend(texture->tpage, blend);
    tint = material->color;

    if ((region_w == (unsigned int)sprite->width) && (region_h == (unsigned int)sprite->height) &&
        !(material->effects & (PSXE_EFFECT_FLIP_X | PSXE_EFFECT_FLIP_Y))) {
        /* Unscaled: SPRT is the cheapest textured rectangle, but it samples
         * the GPU's current page, so a DR_TPAGE must execute before it. */
        SPRT *rect = psxe_renderer_allocate(renderer, sizeof(*rect));
        DR_TPAGE *page = psxe_renderer_allocate(renderer, sizeof(*page));
        if ((rect == NULL) || (page == NULL)) {
            return;
        }
        setSprt(rect);
        setXY0(rect, sprite->x, sprite->y);
        setWH(rect, sprite->width, sprite->height);
        setUV0(rect, (uint8_t)u, (uint8_t)v);
        setRGB0(rect, tint.r, tint.g, tint.b);
        rect->clut = texture->clut;
        setSemiTrans(rect, semitransparent ? 1 : 0);
        addPrim(bucket, rect);
        setDrawTPage(page, 0, renderer->dither ? 1 : 0, tpage);
        addPrim(bucket, page);
        renderer->submitted_primitives += 2;
    } else {
        /* Scaled or flipped: a textured quad carries its own page. */
        POLY_FT4 *quad = psxe_renderer_allocate(renderer, sizeof(*quad));
        uint8_t left = psxe_clamp_texel(u);
        uint8_t right = psxe_clamp_texel(u + region_w);
        uint8_t top = psxe_clamp_texel(v);
        uint8_t bottom = psxe_clamp_texel(v + region_h);
        int16_t x1 = (int16_t)(sprite->x + sprite->width);
        int16_t y1 = (int16_t)(sprite->y + sprite->height);

        if (quad == NULL) {
            return;
        }
        if (material->effects & PSXE_EFFECT_FLIP_X) {
            /* The GPU never draws the right edge, so mirror inclusively. */
            left = psxe_clamp_texel(u + region_w - 1u);
            right = (uint8_t)((u > 0u) ? psxe_clamp_texel(u - 1u) : 0u);
        }
        if (material->effects & PSXE_EFFECT_FLIP_Y) {
            top = psxe_clamp_texel(v + region_h - 1u);
            bottom = (uint8_t)((v > 0u) ? psxe_clamp_texel(v - 1u) : 0u);
        }
        setPolyFT4(quad);
        setXY4(quad, sprite->x, sprite->y, x1, sprite->y, sprite->x, y1, x1, y1);
        setUV4(quad, left, top, right, top, left, bottom, right, bottom);
        setRGB0(quad, tint.r, tint.g, tint.b);
        quad->tpage = tpage;
        quad->clut = texture->clut;
        setSemiTrans(quad, semitransparent ? 1 : 0);
        addPrim(bucket, quad);
        ++renderer->submitted_primitives;
    }
}

void psxe_draw_sprite(
    psxe_renderer *renderer,
    const psxe_texture *texture,
    int16_t x,
    int16_t y,
    int16_t width,
    int16_t height,
    psxe_color tint,
    bool semitransparent
) {
    psxe_sprite sprite;

    if ((texture == NULL) || !texture->valid || (width <= 0) || (height <= 0)) {
        return;
    }
    psxe_sprite_init(&sprite, texture);
    sprite.x = x;
    sprite.y = y;
    sprite.width = width;
    sprite.height = height;
    sprite.region_width = (uint16_t)width;
    sprite.region_height = (uint16_t)height;
    sprite.material.color = tint;
    if (semitransparent) {
        sprite.material.effects = PSXE_EFFECT_SEMITRANSPARENT;
    }
    psxe_sprite_draw(renderer, &sprite);
}
