#include "psxe/render.h"

#include <stdint.h>
#include <string.h>

#include <inline_c.h>

#include "render_internal.h"

void *psxe_renderer_allocate(psxe_renderer *renderer, size_t size) {
    uintptr_t current = (uintptr_t)renderer->next_packet;
    uintptr_t aligned = (current + 3u) & ~(uintptr_t)3u;
    uint8_t *result = (uint8_t *)aligned;

    if ((result + size) > renderer->packet_end) {
        ++renderer->dropped_primitives;
        return NULL;
    }

    renderer->next_packet = result + size;
    return result;
}

uint32_t *psxe_renderer_bucket(psxe_renderer *renderer, unsigned int layer) {
    if (layer >= renderer->ot_length) {
        layer = (unsigned int)renderer->ot_length - 1u;
    }
    return &renderer->ordering_table[layer];
}

static psxe_color psxe_resolve_color(psxe_color face, const psxe_material *material) {
    psxe_color result = face;
    unsigned int r;
    unsigned int g;
    unsigned int b;

    if (material == NULL) {
        return result;
    }
    if (material->color_mode == PSXE_COLOR_REPLACE) {
        return material->color;
    }
    if (material->color_mode == PSXE_COLOR_TINT) {
        r = ((unsigned int)face.r * material->color.r) >> 7;
        g = ((unsigned int)face.g * material->color.g) >> 7;
        b = ((unsigned int)face.b * material->color.b) >> 7;
        result.r = (uint8_t)((r > 255u) ? 255u : r);
        result.g = (uint8_t)((g > 255u) ? 255u : g);
        result.b = (uint8_t)((b > 255u) ? 255u : b);
    }
    return result;
}

static void psxe_make_object_matrix(
    const psxe_renderer *renderer,
    const psxe_transform *transform,
    MATRIX *object_rotation,
    MATRIX *view_object
) {
    SVECTOR inverse_camera_rotation = {
        (int16_t)-renderer->camera.rotation.vx,
        (int16_t)-renderer->camera.rotation.vy,
        (int16_t)-renderer->camera.rotation.vz,
        0
    };
    MATRIX view;
    VECTOR relative;
    VECTOR view_position;

    RotMatrix((SVECTOR *)&transform->rotation, object_rotation);
    RotMatrix(&inverse_camera_rotation, &view);
    MulMatrix0(&view, object_rotation, view_object);
    ScaleMatrix(view_object, (VECTOR *)&transform->scale);

    relative.vx = transform->position.vx - renderer->camera.position.vx;
    relative.vy = transform->position.vy - renderer->camera.position.vy;
    relative.vz = transform->position.vz - renderer->camera.position.vz;
    ApplyMatrixLV(&view, &relative, &view_position);
    TransMatrix(view_object, &view_position);
}

void psxe_renderer_init(psxe_renderer *renderer, const psxe_render_config *config) {
    psxe_render_buffer *first;
    psxe_render_buffer *second;
    psxe_camera default_camera = {
        .position = { 0, 0, 0 },
        .rotation = { 0, 0, 0, 0 },
        .projection = 160
    };
    SVECTOR default_light = { -2048, -2048, -2048, 0 };

    memset(renderer, 0, sizeof(*renderer));
    renderer->width = config->width;
    renderer->height = config->height;
    renderer->depth_shift = 2;
    renderer->camera = default_camera;
    renderer->dither = config->dither;

    ResetGraph(0);
    SetVideoMode((config->video_mode == PSXE_VIDEO_PAL) ? MODE_PAL : MODE_NTSC);

    first = &renderer->buffers[0];
    second = &renderer->buffers[1];
    SetDefDrawEnv(&first->draw, 0, 0, config->width, config->height);
    SetDefDispEnv(&first->display, 0, 0, config->width, config->height);
    SetDefDrawEnv(&second->draw, 0, config->height, config->width, config->height);
    SetDefDispEnv(&second->display, 0, config->height, config->width, config->height);

    setRGB0(&first->draw, config->clear_color.r, config->clear_color.g, config->clear_color.b);
    setRGB0(&second->draw, config->clear_color.r, config->clear_color.g, config->clear_color.b);
    first->draw.isbg = 1;
    second->draw.isbg = 1;
    first->draw.dtd = config->dither ? 1 : 0;
    second->draw.dtd = config->dither ? 1 : 0;

    renderer->active_buffer = 0;
    renderer->next_packet = first->packets;
    renderer->packet_end = first->packets + sizeof(first->packets);
    renderer->ordering_table = first->ordering_table;
    renderer->ot_length = PSXE_OT_LENGTH;
    ClearOTagR(first->ordering_table, PSXE_OT_LENGTH);

    InitGeom();
    gte_SetGeomOffset(config->width / 2, config->height / 2);
    gte_SetGeomScreen(default_camera.projection);
    FntLoad(960, 0);
    psxe_renderer_set_directional_light(
        renderer,
        &default_light,
        (psxe_color){ 220, 220, 220 },
        (psxe_color){ 48, 48, 48 }
    );
    SetDispMask(1);
}

void psxe_renderer_begin_frame(psxe_renderer *renderer) {
    renderer->submitted_primitives = 0;
    renderer->dropped_primitives = 0;
    renderer->target_passes = 0;
}

void psxe_renderer_end_frame(psxe_renderer *renderer) {
    psxe_render_buffer *draw_buffer;
    psxe_render_buffer *display_buffer;

    /* A render-target pass left open is closed so its chain is terminated. */
    psxe_render_target_end(renderer);

    DrawSync(0);
    VSync(0);

    draw_buffer = &renderer->buffers[renderer->active_buffer];
    display_buffer = &renderer->buffers[renderer->active_buffer ^ 1];
    PutDispEnv(&display_buffer->display);
    if (renderer->pass_head != NULL) {
        /* Offscreen passes execute first, then the main ordering table. */
        setaddr(renderer->pass_tail, &draw_buffer->ordering_table[PSXE_OT_LENGTH - 1]);
        DrawOTagEnv((const uint32_t *)renderer->pass_head, &draw_buffer->draw);
    } else {
        DrawOTagEnv(&draw_buffer->ordering_table[PSXE_OT_LENGTH - 1], &draw_buffer->draw);
    }
    renderer->pass_head = NULL;
    renderer->pass_tail = NULL;

    renderer->active_buffer ^= 1;
    renderer->next_packet = display_buffer->packets;
    renderer->packet_end = display_buffer->packets + sizeof(display_buffer->packets);
    renderer->ordering_table = display_buffer->ordering_table;
    renderer->ot_length = PSXE_OT_LENGTH;
    ClearOTagR(display_buffer->ordering_table, PSXE_OT_LENGTH);
}

void psxe_renderer_set_camera(psxe_renderer *renderer, const psxe_camera *camera) {
    if ((renderer == NULL) || (camera == NULL)) {
        return;
    }
    renderer->camera = *camera;
    gte_SetGeomScreen(camera->projection);
}

void psxe_renderer_set_directional_light(
    psxe_renderer *renderer,
    const SVECTOR *direction,
    psxe_color color,
    psxe_color ambient
) {
    memset(&renderer->light_matrix, 0, sizeof(renderer->light_matrix));
    memset(&renderer->color_matrix, 0, sizeof(renderer->color_matrix));

    renderer->light_matrix.m[0][0] = direction->vx;
    renderer->light_matrix.m[0][1] = direction->vy;
    renderer->light_matrix.m[0][2] = direction->vz;
    renderer->color_matrix.m[0][0] = (int16_t)(((int32_t)color.r * ONE) / 255);
    renderer->color_matrix.m[1][0] = (int16_t)(((int32_t)color.g * ONE) / 255);
    renderer->color_matrix.m[2][0] = (int16_t)(((int32_t)color.b * ONE) / 255);
    gte_SetBackColor(ambient.r, ambient.g, ambient.b);
    gte_SetColorMatrix(&renderer->color_matrix);
}

bool psxe_texture_load_tim(psxe_texture *texture, const uint32_t *tim_data) {
    TIM_IMAGE image;
    int mode;

    if ((texture == NULL) || (tim_data == NULL)) {
        return false;
    }

    memset(texture, 0, sizeof(*texture));
    if (GetTimInfo(tim_data, &image) != 0) {
        return false;
    }
    mode = image.mode & 3;
    if (image.mode & 8) {
        LoadImage(image.crect, image.caddr);
        texture->clut = getClut(image.crect->x, image.crect->y);
    }
    LoadImage(image.prect, image.paddr);
    DrawSync(0);

    texture->tpage = getTPage(mode, 0, image.prect->x, image.prect->y);
    texture->v = (uint8_t)(image.prect->y & 255);
    if (mode == 0) {
        texture->u = (uint8_t)((image.prect->x & 63) * 4);
        texture->width = (uint16_t)(image.prect->w * 4);
    } else if (mode == 1) {
        texture->u = (uint8_t)((image.prect->x & 63) * 2);
        texture->width = (uint16_t)(image.prect->w * 2);
    } else {
        /* 16-bit pages start every 64 halfwords; u is the offset inside it. */
        texture->u = (uint8_t)(image.prect->x & 63);
        texture->width = (uint16_t)image.prect->w;
    }
    texture->height = (uint16_t)image.prect->h;
    texture->valid = true;
    return true;
}

void psxe_draw_mesh(
    psxe_renderer *renderer,
    const psxe_mesh *mesh,
    const psxe_transform *transform
) {
    psxe_draw_mesh_material(renderer, mesh, transform, NULL);
}

void psxe_draw_mesh_material(
    psxe_renderer *renderer,
    const psxe_mesh *mesh,
    const psxe_transform *transform,
    const psxe_material *material
) {
    MATRIX object_rotation;
    MATRIX view_object;
    MATRIX object_light;
    const psxe_texture *texture;
    uint8_t effects = 0;
    uint8_t blend = PSXE_BLEND_HALF;
    uint16_t tpage = 0;
    bool can_texture;
    uint16_t face_index;

    if ((renderer == NULL) || (mesh == NULL) || (transform == NULL) ||
        (mesh->vertices == NULL) || (mesh->faces == NULL)) {
        return;
    }

    texture = mesh->texture;
    if (material != NULL) {
        effects = material->effects;
        blend = (uint8_t)(material->blend & 3u);
        if (material->texture != NULL) {
            texture = material->texture;
        }
    }
    can_texture = (texture != NULL) && texture->valid && !(effects & PSXE_EFFECT_UNTEXTURED);
    if (can_texture) {
        tpage = psxe_tpage_with_blend(texture->tpage, blend);
    }

    psxe_make_object_matrix(renderer, transform, &object_rotation, &view_object);
    MulMatrix0(&renderer->light_matrix, &object_rotation, &object_light);
    gte_SetRotMatrix(&view_object);
    gte_SetTransMatrix(&view_object);
    gte_SetLightMatrix(&object_light);

    for (face_index = 0; face_index < mesh->face_count; ++face_index) {
        const psxe_face *face = &mesh->faces[face_index];
        uint8_t flags = face->flags;
        psxe_color color;
        int32_t winding;
        int32_t average_z;
        int depth;
        bool textured;
        bool lit;
        bool semitransparent;
        uint32_t *bucket;

        if ((face->vertex[0] >= mesh->vertex_count) ||
            (face->vertex[1] >= mesh->vertex_count) ||
            (face->vertex[2] >= mesh->vertex_count)) {
            ++renderer->dropped_primitives;
            continue;
        }

        gte_ldv3(
            &mesh->vertices[face->vertex[0]],
            &mesh->vertices[face->vertex[1]],
            &mesh->vertices[face->vertex[2]]
        );
        gte_rtpt();
        gte_nclip();
        gte_stopz(&winding);
        if (!(flags & PSXE_MATERIAL_DOUBLE_SIDED) && !(effects & PSXE_EFFECT_DOUBLE_SIDED) &&
            (winding <= 0)) {
            continue;
        }

        gte_avsz3();
        gte_stotz(&average_z);
        depth = average_z >> renderer->depth_shift;
        /* Buckets 0 and 1 are reserved for text and 2D primitives. */
        if ((depth <= 1) || (depth >= (int)renderer->ot_length)) {
            continue;
        }
        bucket = &renderer->ordering_table[depth];

        textured = can_texture && (flags & PSXE_MATERIAL_TEXTURED);
        lit = (flags & PSXE_MATERIAL_LIT) && !(effects & PSXE_EFFECT_UNLIT) &&
              (mesh->normals != NULL) && (face->normal < mesh->normal_count);
        semitransparent = (flags & PSXE_MATERIAL_SEMITRANSPARENT) ||
                          (effects & PSXE_EFFECT_SEMITRANSPARENT);
        color = psxe_resolve_color(face->color, material);

        if (textured) {
            POLY_FT3 *primitive = psxe_renderer_allocate(renderer, sizeof(*primitive));
            if (primitive == NULL) {
                continue;
            }
            setPolyFT3(primitive);
            setRGB0(primitive, color.r, color.g, color.b);
            gte_stsxy0(&primitive->x0);
            gte_stsxy1(&primitive->x1);
            gte_stsxy2(&primitive->x2);
            primitive->u0 = (uint8_t)(texture->u + face->uv[0][0]);
            primitive->v0 = (uint8_t)(texture->v + face->uv[0][1]);
            primitive->u1 = (uint8_t)(texture->u + face->uv[1][0]);
            primitive->v1 = (uint8_t)(texture->v + face->uv[1][1]);
            primitive->u2 = (uint8_t)(texture->u + face->uv[2][0]);
            primitive->v2 = (uint8_t)(texture->v + face->uv[2][1]);
            /* The textured primitive carries its own page and blend mode. */
            primitive->tpage = tpage;
            primitive->clut = texture->clut;
            if (lit) {
                gte_ldrgb(&primitive->r0);
                gte_ldv0(&mesh->normals[face->normal]);
                gte_nccs();
                gte_strgb(&primitive->r0);
            }
            if (semitransparent) {
                setSemiTrans(primitive, 1);
            }
            addPrim(bucket, primitive);
            ++renderer->submitted_primitives;
        } else {
            POLY_F3 *primitive;
            DR_TPAGE *page = NULL;

            /* Untextured primitives blend with the GPU's current ABR bits, so
             * a state packet must execute first. Reserve it before the face so
             * an exhausted arena drops the whole face instead of mis-blending. */
            if (semitransparent) {
                page = psxe_renderer_allocate(renderer, sizeof(*page));
                if (page == NULL) {
                    continue;
                }
            }
            primitive = psxe_renderer_allocate(renderer, sizeof(*primitive));
            if (primitive == NULL) {
                continue;
            }
            setPolyF3(primitive);
            setRGB0(primitive, color.r, color.g, color.b);
            gte_stsxy0(&primitive->x0);
            gte_stsxy1(&primitive->x1);
            gte_stsxy2(&primitive->x2);
            if (lit) {
                gte_ldrgb(&primitive->r0);
                gte_ldv0(&mesh->normals[face->normal]);
                gte_nccs();
                gte_strgb(&primitive->r0);
            }
            if (semitransparent) {
                setSemiTrans(primitive, 1);
            }
            addPrim(bucket, primitive);
            ++renderer->submitted_primitives;
            if (page != NULL) {
                /* addPrim prepends: inserted after, executes before. */
                setDrawTPage(page, 0, renderer->dither ? 1 : 0, getTPage(0, blend, 0, 0));
                addPrim(bucket, page);
                ++renderer->submitted_primitives;
            }
        }
    }
}

void psxe_ui_panel(
    psxe_renderer *renderer,
    int16_t x,
    int16_t y,
    int16_t width,
    int16_t height,
    psxe_color color,
    bool semitransparent
) {
    TILE *tile;
    DR_TPAGE *page = NULL;
    if ((renderer == NULL) || (width <= 0) || (height <= 0)) {
        return;
    }
    tile = psxe_renderer_allocate(renderer, sizeof(*tile));
    if (tile == NULL) {
        return;
    }
    if (semitransparent) {
        page = psxe_renderer_allocate(renderer, sizeof(*page));
        if (page == NULL) {
            return;
        }
    }
    setTile(tile);
    setXY0(tile, x, y);
    setWH(tile, width, height);
    setRGB0(tile, color.r, color.g, color.b);
    setSemiTrans(tile, semitransparent ? 1 : 0);
    addPrim(psxe_renderer_bucket(renderer, 1), tile);
    ++renderer->submitted_primitives;
    if (page != NULL) {
        setDrawTPage(page, 0, renderer->dither ? 1 : 0, getTPage(0, PSXE_BLEND_HALF, 0, 0));
        addPrim(psxe_renderer_bucket(renderer, 1), page);
        ++renderer->submitted_primitives;
    }
}

void psxe_ui_text(psxe_renderer *renderer, int16_t x, int16_t y, const char *text) {
    size_t required;
    size_t length;
    uint8_t *start;

    if ((renderer == NULL) || (text == NULL)) {
        return;
    }
    length = strlen(text);
    required = (length * sizeof(SPRT_8)) + sizeof(DR_TPAGE);
    start = psxe_renderer_allocate(renderer, required);
    if (start == NULL) {
        return;
    }
    renderer->next_packet = (uint8_t *)FntSort(psxe_renderer_bucket(renderer, 0), start, x, y, text);
    renderer->submitted_primitives += (uint32_t)length + 1u;
}
