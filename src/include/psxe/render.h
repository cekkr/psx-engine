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

typedef enum psxe_material_flags {
    PSXE_MATERIAL_TEXTURED = 1 << 0,
    PSXE_MATERIAL_LIT = 1 << 1,
    PSXE_MATERIAL_SEMITRANSPARENT = 1 << 2,
    PSXE_MATERIAL_DOUBLE_SIDED = 1 << 3
} psxe_material_flags;

typedef struct psxe_texture {
    uint16_t tpage;
    uint16_t clut;
    uint16_t width;
    uint16_t height;
    uint8_t u;
    uint8_t v;
    bool valid;
} psxe_texture;

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
    MATRIX light_matrix;
    MATRIX color_matrix;
} psxe_renderer;

void psxe_renderer_init(psxe_renderer *renderer, const psxe_render_config *config);
void psxe_renderer_begin_frame(psxe_renderer *renderer);
void psxe_renderer_end_frame(psxe_renderer *renderer);
void psxe_renderer_set_camera(psxe_renderer *renderer, const psxe_camera *camera);
void psxe_renderer_set_directional_light(
    psxe_renderer *renderer,
    const SVECTOR *direction,
    psxe_color color,
    psxe_color ambient
);
bool psxe_texture_load_tim(psxe_texture *texture, const uint32_t *tim_data);
void psxe_draw_mesh(
    psxe_renderer *renderer,
    const psxe_mesh *mesh,
    const psxe_transform *transform
);
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
