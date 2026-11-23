#ifndef PSXE_RENDER_H
#define PSXE_RENDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <psxgpu.h>
#include <psxgte.h>

#include "psxe/config.h"
#include "psxe/types.h"

typedef enum psxe_video_mode {
    PSXE_VIDEO_NTSC = 0,
    PSXE_VIDEO_PAL = 1
} psxe_video_mode;

/* Per-face flags stored in psxe_face.flags. */
typedef enum psxe_material_flags {
    PSXE_MATERIAL_TEXTURED = 1 << 0,
    PSXE_MATERIAL_LIT = 1 << 1,
    PSXE_MATERIAL_SEMITRANSPARENT = 1 << 2,
    PSXE_MATERIAL_DOUBLE_SIDED = 1 << 3
} psxe_material_flags;

/* GPU semi-transparency equations (B = framebuffer, F = primitive). */
typedef enum psxe_blend_mode {
    PSXE_BLEND_HALF = 0,     /* B/2 + F/2 */
    PSXE_BLEND_ADD = 1,      /* B + F */
    PSXE_BLEND_SUBTRACT = 2, /* B - F */
    PSXE_BLEND_QUARTER = 3   /* B + F/4 */
} psxe_blend_mode;

/* How psxe_material.color combines with per-face colors. */
typedef enum psxe_color_mode {
    PSXE_COLOR_FACE = 0,    /* keep each face's own color */
    PSXE_COLOR_REPLACE = 1, /* use material.color for every face */
    PSXE_COLOR_TINT = 2     /* face * material.color / 128 (128 = unchanged) */
} psxe_color_mode;

/* Object/sprite-wide effects. All are applied by the CPU while packets are
 * built or by GPU state bits; none adds GTE work. */
typedef enum psxe_effect_flags {
    PSXE_EFFECT_UNLIT = 1 << 0,           /* skip GTE lighting for LIT faces */
    PSXE_EFFECT_UNTEXTURED = 1 << 1,      /* draw flat color instead of texels */
    PSXE_EFFECT_SEMITRANSPARENT = 1 << 2, /* blend everything with material.blend */
    PSXE_EFFECT_DOUBLE_SIDED = 1 << 3,    /* disable back-face rejection */
    PSXE_EFFECT_FLIP_X = 1 << 4,          /* sprites only: mirror horizontally */
    PSXE_EFFECT_FLIP_Y = 1 << 5,          /* sprites only: mirror vertically */
    PSXE_EFFECT_RAW_TEXTURE = 1 << 6      /* textured draws use baked texel colors */
} psxe_effect_flags;

/* Light kinds. Only DIRECTIONAL and POINT occupy one of the three GTE light
 * rows; AMBIENT adds to the GTE back color. */
typedef enum psxe_light_type {
    PSXE_LIGHT_DIRECTIONAL = 0,
    PSXE_LIGHT_POINT = 1,
    PSXE_LIGHT_AMBIENT = 2
} psxe_light_type;

/* Point-light attenuation, evaluated on the CPU once per mesh at its origin.
 * t = (radius - distance) / radius, clamped to 0..1. */
typedef enum psxe_light_falloff {
    PSXE_FALLOFF_LINEAR = 0, /* t */
    PSXE_FALLOFF_SMOOTH = 1, /* t * t: softer edge, faster drop near the light */
    PSXE_FALLOFF_NONE = 2    /* constant; radius ignored */
} psxe_light_falloff;

#define PSXE_LIGHT_LAYER_DEFAULT 0x01u
#define PSXE_LIGHT_LAYER_ALL 0xffu

/* A light description. Plain data: helpers never touch GTE state. */
typedef struct psxe_light {
    VECTOR position;   /* POINT: world position (scene lights: offset from entity) */
    SVECTOR direction; /* DIRECTIONAL: unit vector (ONE) from surfaces toward the light */
    psxe_color color;
    uint8_t type;      /* psxe_light_type */
    uint8_t falloff;   /* psxe_light_falloff */
    uint8_t layers;    /* bit set matched against psxe_material.light_exclude */
    bool enabled;
    int32_t intensity; /* 20.12 multiplier of color; ONE = color at full strength */
    int32_t radius;    /* POINT: world units where the contribution reaches zero */
} psxe_light;

/* Fixed-capacity light set plus ambient color. */
typedef struct psxe_light_set {
    psxe_light lights[PSXE_MAX_LIGHTS];
    uint16_t count;
    psxe_color ambient;
} psxe_light_set;

typedef struct psxe_texture {
    uint16_t tpage;
    uint16_t clut;
    uint16_t width;
    uint16_t height;
    uint8_t u;
    uint8_t v;
    bool valid;
} psxe_texture;

/* Object/sprite-level appearance override. Borrowed texture pointer. */
typedef struct psxe_material {
    const psxe_texture *texture; /* NULL: mesh texture (objects) / solid fill (sprites) */
    psxe_color color;            /* see color_mode; sprites: tint or fill color */
    uint8_t color_mode;          /* psxe_color_mode */
    uint8_t effects;             /* psxe_effect_flags */
    uint8_t blend;               /* psxe_blend_mode */
    uint8_t light_exclude;       /* lights whose layers intersect this are ignored */
} psxe_material;

typedef struct psxe_face {
    uint16_t vertex[3];
    uint16_t normal;
    uint8_t uv[3][2];
    psxe_color color;
    uint8_t flags;
} psxe_face;

typedef struct psxe_mesh {
    const SVECTOR *vertices;
    const SVECTOR *normals;
    const psxe_face *faces;
    uint16_t vertex_count;
    uint16_t normal_count;
    uint16_t face_count;
    const psxe_texture *texture;
} psxe_mesh;

/* A drawable mesh instance: shared immutable mesh + own transform/material. */
typedef struct psxe_object {
    const psxe_mesh *mesh;
    psxe_transform transform;
    psxe_material material;
    bool visible;
} psxe_object;

/* A screen-space textured or solid rectangle. */
typedef struct psxe_sprite {
    int16_t x;
    int16_t y;
    int16_t width;          /* on-screen size in pixels */
    int16_t height;
    uint8_t u;              /* texel region offset from the texture base */
    uint8_t v;
    uint16_t region_width;  /* texel region size; differs from width -> scaled */
    uint16_t region_height;
    uint16_t layer;         /* ordering-table bucket, default 1 */
    psxe_material material;
    bool visible;
} psxe_sprite;

typedef struct psxe_camera {
    VECTOR position;
    SVECTOR rotation;
    uint16_t projection;
} psxe_camera;

typedef struct psxe_render_config {
    int16_t width;
    int16_t height;
    psxe_video_mode video_mode;
    psxe_color clear_color;
    bool dither;
} psxe_render_config;

typedef struct psxe_render_target_config {
    int16_t x;           /* VRAM position; x and width must be multiples of 16 */
    int16_t y;
    int16_t width;       /* 16..256 */
    int16_t height;      /* 1..256 */
    psxe_color clear_color;
    bool clear;          /* fill the target before drawing each pass */
    uint16_t ot_length;  /* buckets per pass, 3..PSXE_OT_LENGTH, from the packet arena */
    uint8_t depth_shift; /* average Z >> depth_shift = bucket */
} psxe_render_target_config;

/* An offscreen VRAM rectangle that 3D/2D draws can render into and that can
 * then be sampled as a 16-bit texture in the same frame. */
typedef struct psxe_render_target {
    psxe_texture texture;
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
    psxe_color clear_color;
    bool clear;
    uint16_t ot_length;
    uint8_t depth_shift;
    bool valid;
} psxe_render_target;

typedef struct psxe_render_buffer {
    DISPENV display;
    DRAWENV draw;
    uint32_t ordering_table[PSXE_OT_LENGTH];
    uint8_t packets[PSXE_PACKET_BUFFER_SIZE];
} psxe_render_buffer;

typedef struct psxe_renderer {
    psxe_render_buffer buffers[2];
    uint8_t *next_packet;
    uint8_t *packet_end;
    uint32_t submitted_primitives;
    uint32_t dropped_primitives;
    int active_buffer;
    int depth_shift;
    int16_t width;
    int16_t height;
    psxe_camera camera;
    psxe_light_set lights;
    /* Per-frame light diagnostics, summed over every lit mesh draw. */
    uint32_t lights_considered; /* enabled, unmasked lights evaluated */
    uint32_t lights_applied;    /* placed in a GTE light row or ambient */
    uint32_t lights_folded;     /* beyond three rows, approximated in back color */
    uint32_t lights_dropped;    /* rejected because a light set was full */
    bool dither;
    bool frame_open; /* Managed VRAM changes are allowed between frames. */
    /* Current destination ordering table (main buffer or a render target). */
    uint32_t *ordering_table;
    uint16_t ot_length;
    /* Render-to-texture pass state. */
    const psxe_render_target *active_target;
    psxe_camera saved_camera;
    psxe_light_set saved_lights;
    int saved_depth_shift;
    DR_ENV *pass_head;
    DR_ENV *pass_tail;
    uint16_t target_passes;
} psxe_renderer;

void psxe_renderer_init(psxe_renderer *renderer, const psxe_render_config *config);
void psxe_renderer_begin_frame(psxe_renderer *renderer);
void psxe_renderer_end_frame(psxe_renderer *renderer);
void psxe_renderer_set_camera(psxe_renderer *renderer, const psxe_camera *camera);

/* Lights */
void psxe_light_init(psxe_light *light, psxe_light_type type);
void psxe_light_set_color(psxe_light *light, psxe_color color, int32_t intensity);
void psxe_light_set_direction(psxe_light *light, int32_t x, int32_t y, int32_t z);
void psxe_light_set_position(psxe_light *light, int32_t x, int32_t y, int32_t z);
void psxe_light_set_range(psxe_light *light, int32_t radius, psxe_light_falloff falloff);
void psxe_light_set_layers(psxe_light *light, uint8_t layers);
void psxe_light_set_enabled(psxe_light *light, bool enabled);

void psxe_renderer_clear_lights(psxe_renderer *renderer);
bool psxe_renderer_add_light(psxe_renderer *renderer, const psxe_light *light);
uint16_t psxe_renderer_set_lights(psxe_renderer *renderer, const psxe_light *lights, uint16_t count);
psxe_light *psxe_renderer_light(psxe_renderer *renderer, uint16_t index);
void psxe_renderer_set_ambient(psxe_renderer *renderer, psxe_color ambient);
/* One-light wrapper: clears the set, adds one directional light, sets ambient. */
void psxe_renderer_set_directional_light(
    psxe_renderer *renderer,
    const SVECTOR *direction,
    psxe_color color,
    psxe_color ambient
);
/* Debug: a size x size solid square at a POINT light's projected position. */
void psxe_draw_light_marker(psxe_renderer *renderer, const psxe_light *light, int16_t size);
bool psxe_texture_load_tim(psxe_texture *texture, const uint32_t *tim_data);

/* Materials */
psxe_material psxe_material_default(void);
void psxe_material_set_texture(psxe_material *material, const psxe_texture *texture);
void psxe_material_set_color(psxe_material *material, psxe_color color, psxe_color_mode mode);
void psxe_material_set_effects(psxe_material *material, uint8_t effects, psxe_blend_mode blend);
void psxe_material_set_light_exclude(psxe_material *material, uint8_t layers);

/* Meshes */
void psxe_draw_mesh(
    psxe_renderer *renderer,
    const psxe_mesh *mesh,
    const psxe_transform *transform
);
void psxe_draw_mesh_material(
    psxe_renderer *renderer,
    const psxe_mesh *mesh,
    const psxe_transform *transform,
    const psxe_material *material
);
/* As psxe_draw_mesh_material, also considering extra world-space lights
 * (borrowed for this call only) alongside the renderer's light set. */
void psxe_draw_mesh_lights(
    psxe_renderer *renderer,
    const psxe_mesh *mesh,
    const psxe_transform *transform,
    const psxe_material *material,
    const psxe_light *extra_lights,
    uint16_t extra_count
);

/* Objects */
void psxe_object_init(psxe_object *object, const psxe_mesh *mesh);
void psxe_object_set_mesh(psxe_object *object, const psxe_mesh *mesh);
void psxe_object_set_texture(psxe_object *object, const psxe_texture *texture);
void psxe_object_set_color(psxe_object *object, psxe_color color, psxe_color_mode mode);
void psxe_object_set_effects(psxe_object *object, uint8_t effects, psxe_blend_mode blend);
void psxe_object_set_position(psxe_object *object, int32_t x, int32_t y, int32_t z);
void psxe_object_translate(psxe_object *object, int32_t dx, int32_t dy, int32_t dz);
void psxe_object_set_rotation(psxe_object *object, int16_t x, int16_t y, int16_t z);
void psxe_object_rotate(psxe_object *object, int16_t dx, int16_t dy, int16_t dz);
void psxe_object_set_scale(psxe_object *object, int32_t scale);
void psxe_object_draw(psxe_renderer *renderer, const psxe_object *object);

/* Sprites */
void psxe_sprite_init(psxe_sprite *sprite, const psxe_texture *texture);
void psxe_sprite_set_texture(psxe_sprite *sprite, const psxe_texture *texture);
void psxe_sprite_set_region(psxe_sprite *sprite, uint8_t u, uint8_t v, uint16_t width, uint16_t height);
void psxe_sprite_set_position(psxe_sprite *sprite, int16_t x, int16_t y);
void psxe_sprite_move(psxe_sprite *sprite, int16_t dx, int16_t dy);
void psxe_sprite_set_size(psxe_sprite *sprite, int16_t width, int16_t height);
void psxe_sprite_set_color(psxe_sprite *sprite, psxe_color color);
void psxe_sprite_set_effects(psxe_sprite *sprite, uint8_t effects, psxe_blend_mode blend);
void psxe_sprite_set_layer(psxe_sprite *sprite, uint16_t layer);
void psxe_sprite_draw(psxe_renderer *renderer, const psxe_sprite *sprite);
void psxe_draw_sprite(
    psxe_renderer *renderer,
    const psxe_texture *texture,
    int16_t x,
    int16_t y,
    int16_t width,
    int16_t height,
    psxe_color tint,
    bool semitransparent
);

/* Render-to-texture */
bool psxe_render_target_init(
    psxe_render_target *target,
    const psxe_renderer *renderer,
    const psxe_render_target_config *config
);
bool psxe_render_target_begin(psxe_renderer *renderer, const psxe_render_target *target);
void psxe_render_target_end(psxe_renderer *renderer);
const psxe_texture *psxe_render_target_texture(const psxe_render_target *target);

/* UI */
void psxe_ui_panel(
    psxe_renderer *renderer,
    int16_t x,
    int16_t y,
    int16_t width,
    int16_t height,
    psxe_color color,
    bool semitransparent
);
void psxe_ui_text(psxe_renderer *renderer, int16_t x, int16_t y, const char *text);

#endif
