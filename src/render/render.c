#include "psxe/render.h"

#include <stdint.h>
#include <string.h>

#include <inline_c.h>

static void *psxe_renderer_allocate(psxe_renderer *renderer, size_t size) {
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

static uint32_t *psxe_renderer_ui_bucket(psxe_renderer *renderer, unsigned int layer) {
    if (layer >= PSXE_OT_LENGTH) {
        layer = 0;
    }
    return &renderer->buffers[renderer->active_buffer].ordering_table[layer];
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
}

void psxe_renderer_end_frame(psxe_renderer *renderer) {
    psxe_render_buffer *draw_buffer;
    psxe_render_buffer *display_buffer;

    DrawSync(0);
    VSync(0);

    draw_buffer = &renderer->buffers[renderer->active_buffer];
    display_buffer = &renderer->buffers[renderer->active_buffer ^ 1];
    PutDispEnv(&display_buffer->display);
    DrawOTagEnv(&draw_buffer->ordering_table[PSXE_OT_LENGTH - 1], &draw_buffer->draw);

    renderer->active_buffer ^= 1;
    renderer->next_packet = display_buffer->packets;
    renderer->packet_end = display_buffer->packets + sizeof(display_buffer->packets);
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
        texture->u = (uint8_t)((image.prect->x & 127) * 2);
        texture->width = (uint16_t)(image.prect->w * 2);
    } else {
        texture->u = (uint8_t)(image.prect->x & 255);
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
    MATRIX object_rotation;
    MATRIX view_object;
    MATRIX object_light;
    uint16_t face_index;

    if ((renderer == NULL) || (mesh == NULL) || (transform == NULL) ||
        (mesh->vertices == NULL) || (mesh->faces == NULL)) {
        return;
    }

    psxe_make_object_matrix(renderer, transform, &object_rotation, &view_object);
    MulMatrix0(&renderer->light_matrix, &object_rotation, &object_light);
    gte_SetRotMatrix(&view_object);
    gte_SetTransMatrix(&view_object);
    gte_SetLightMatrix(&object_light);

    for (face_index = 0; face_index < mesh->face_count; ++face_index) {
        const psxe_face *face = &mesh->faces[face_index];
        int32_t winding;
        int32_t average_z;
        int depth;
        bool textured;
        bool lit;

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
        if (!(face->flags & PSXE_MATERIAL_DOUBLE_SIDED) && (winding <= 0)) {
            continue;
        }

        gte_avsz3();
        gte_stotz(&average_z);
        depth = average_z >> renderer->depth_shift;
        /* Buckets 0 and 1 are reserved for text and 2D primitives. */
        if ((depth <= 1) || (depth >= PSXE_OT_LENGTH)) {
            continue;
        }

        textured = (face->flags & PSXE_MATERIAL_TEXTURED) &&
                   (mesh->texture != NULL) && mesh->texture->valid;
        lit = (face->flags & PSXE_MATERIAL_LIT) &&
              (mesh->normals != NULL) && (face->normal < mesh->normal_count);

        if (textured) {
            POLY_FT3 *primitive = psxe_renderer_allocate(renderer, sizeof(*primitive));
            if (primitive == NULL) {
                continue;
            }
            setPolyFT3(primitive);
            setRGB0(primitive, face->color.r, face->color.g, face->color.b);
            gte_stsxy0(&primitive->x0);
            gte_stsxy1(&primitive->x1);
            gte_stsxy2(&primitive->x2);
            primitive->u0 = (uint8_t)(mesh->texture->u + face->uv[0][0]);
            primitive->v0 = (uint8_t)(mesh->texture->v + face->uv[0][1]);
            primitive->u1 = (uint8_t)(mesh->texture->u + face->uv[1][0]);
            primitive->v1 = (uint8_t)(mesh->texture->v + face->uv[1][1]);
            primitive->u2 = (uint8_t)(mesh->texture->u + face->uv[2][0]);
            primitive->v2 = (uint8_t)(mesh->texture->v + face->uv[2][1]);
            primitive->tpage = mesh->texture->tpage;
            primitive->clut = mesh->texture->clut;
            if (lit) {
                gte_ldrgb(&primitive->r0);
                gte_ldv0(&mesh->normals[face->normal]);
                gte_nccs();
                gte_strgb(&primitive->r0);
            }
            if (face->flags & PSXE_MATERIAL_SEMITRANSPARENT) {
                setSemiTrans(primitive, 1);
            }
            addPrim(&renderer->buffers[renderer->active_buffer].ordering_table[depth], primitive);
        } else {
            POLY_F3 *primitive = psxe_renderer_allocate(renderer, sizeof(*primitive));
            if (primitive == NULL) {
                continue;
            }
            setPolyF3(primitive);
            setRGB0(primitive, face->color.r, face->color.g, face->color.b);
            gte_stsxy0(&primitive->x0);
            gte_stsxy1(&primitive->x1);
            gte_stsxy2(&primitive->x2);
            if (lit) {
                gte_ldrgb(&primitive->r0);
                gte_ldv0(&mesh->normals[face->normal]);
                gte_nccs();
                gte_strgb(&primitive->r0);
            }
            if (face->flags & PSXE_MATERIAL_SEMITRANSPARENT) {
                setSemiTrans(primitive, 1);
            }
            addPrim(&renderer->buffers[renderer->active_buffer].ordering_table[depth], primitive);
        }
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
    SPRT *sprite;
    DR_TPAGE *page;
    uint32_t *bucket;

    if ((renderer == NULL) || (texture == NULL) || !texture->valid || (width <= 0) || (height <= 0)) {
        return;
    }
    sprite = psxe_renderer_allocate(renderer, sizeof(*sprite));
    page = psxe_renderer_allocate(renderer, sizeof(*page));
    if ((sprite == NULL) || (page == NULL)) {
        return;
    }

    bucket = psxe_renderer_ui_bucket(renderer, 1);
    setSprt(sprite);
    setXY0(sprite, x, y);
    setWH(sprite, width, height);
    setUV0(sprite, texture->u, texture->v);
    setRGB0(sprite, tint.r, tint.g, tint.b);
    sprite->clut = texture->clut;
    setSemiTrans(sprite, semitransparent ? 1 : 0);
    addPrim(bucket, sprite);
    setDrawTPage(page, 0, semitransparent ? 1 : 0, texture->tpage);
    addPrim(bucket, page);
    renderer->submitted_primitives += 2;
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
    addPrim(psxe_renderer_ui_bucket(renderer, 1), tile);
    ++renderer->submitted_primitives;
    if (page != NULL) {
        setDrawTPage(page, 0, 1, getTPage(0, 0, 0, 0));
        addPrim(psxe_renderer_ui_bucket(renderer, 1), page);
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
    renderer->next_packet = (uint8_t *)FntSort(psxe_renderer_ui_bucket(renderer, 0), start, x, y, text);
    renderer->submitted_primitives += (uint32_t)length + 1u;
}
