# psx-engine API Reference

This file lists every public function, type, and macro in
[`src/include/psxe/`](src/include/psxe/), with short snippets and longer
examples that combine several subsystems. It describes the checked-out
revision only. Everything here is **experimental/scaffold** (see
[`AGENTS.md`](AGENTS.md) and [`ROADMAP.md`](ROADMAP.md)): the API is
pre-release, may change, and has not been validated on original hardware.

Include everything with the umbrella header:

```c
#include <psxe/psxe.h>
```

or include the narrower headers (`psxe/render.h`, `psxe/scene.h`, ...) to reduce
coupling.

## Contents

- [Conventions](#conventions)
- [Configuration macros — `config.h`](#configuration-macros--configh)
- [Shared types — `types.h`](#shared-types--typesh)
- [Engine lifecycle — `engine.h`](#engine-lifecycle--engineh)
- [Renderer — `render.h`](#renderer--renderh)
- [Materials and effects](#materials-and-effects)
- [Objects — `psxe_object`](#objects--psxe_object)
- [Sprites — `psxe_sprite`](#sprites--psxe_sprite)
- [Render to texture — `psxe_render_target`](#render-to-texture--psxe_render_target)
- [Scene — `scene.h`](#scene--sceneh)
- [Input — `input.h`](#input--inputh)
- [Audio — `audio.h`](#audio--audioh)
- [Memory card — `memory_card.h`](#memory-card--memory_cardh)
- [Combining APIs: advanced recipes](#combining-apis-advanced-recipes)
- [Quick index](#quick-index)

## Conventions

- **Units.** `VECTOR`/`SVECTOR` positions are integer world units. Rotation uses
  4096 units per full turn (`PSXE_ANGLE_FULL_TURN`). Scale and unit normals use
  20.12 fixed point where `ONE` (4096) is 1.0.
- **Axes.** The GTE projects with Y pointing down the screen and +Z going away
  from the camera; objects in front of a default camera have positive `vz`.
- **Time.** One `update` happens per video frame. `engine->ticks_per_second` is
  60 for NTSC and 50 for PAL. Convert seconds to frames with it; never assume 60.
- **Failure is bounded.** No call allocates from the heap. When a fixed arena is
  full, the call returns failure (`false`, `-1`, an invalid handle, an error
  code) or increments `renderer.dropped_primitives`.
- **Ownership.** Meshes, vertices, faces, textures, and samples are borrowed by
  pointer. They must outlive every frame that draws them; `static const` data is
  the normal choice.
- **NULL.** Most functions ignore `NULL` arguments (noted per function). The
  `*_init` functions and `psxe_renderer_set_directional_light` do not check.

## Configuration macros — `config.h`

| Macro | Default | Meaning |
| --- | --- | --- |
| `PSXE_OT_LENGTH` | `1024` | Ordering-table buckets per buffer. Bucket 0 = text, 1 = sprites/panels, 2+ = meshes (larger = farther). |
| `PSXE_PACKET_BUFFER_SIZE` | `48 * 1024` | GPU packet bytes per buffer (two buffers exist). |
| `PSXE_MAX_ENTITIES` | `128` | Slots in a `psxe_scene`. |
| `PSXE_MEMORY_CARD_BLOCK_SIZE` | `8192` | Bytes per save file (one card block). |
| `PSXE_MAX_AUDIO_VOICES` | `24` | Hardware SPU voices (fixed, not overridable). |
| `PSXE_MAX_SAVE_PAYLOAD` | `8192 - 544` = 7648 | Largest payload accepted by `psxe_memory_card_save`. |

The overridable macros change the layout of public structs. Define them for
**every** translation unit (engine and game) at once, for example in your game's
`CMakeLists.txt`:

```cmake
target_compile_definitions(psxe PUBLIC PSXE_MAX_ENTITIES=256 PSXE_PACKET_BUFFER_SIZE=65536)
```

`PUBLIC` propagates the values to every target linking `psxe`. Clean-rebuild
afterwards. Defining a different value in only one `.c` file corrupts memory.

## Shared types — `types.h`

```c
typedef struct psxe_color { uint8_t r, g, b; } psxe_color;

typedef struct psxe_transform {
    VECTOR  position;   /* world units */
    SVECTOR rotation;   /* 4096 = 360 degrees, vx/vy/vz = pitch/yaw/roll */
    VECTOR  scale;      /* ONE = 1.0 */
} psxe_transform;
```

| Symbol | Description |
| --- | --- |
| `psxe_transform psxe_transform_identity(void)` | Zero position/rotation, scale `{ONE, ONE, ONE}`. |
| `PSXE_ARRAY_COUNT(array)` | Element count of a true array (not a pointer). |
| `PSXE_ANGLE_FULL_TURN` | `4096`. |
| `PSXE_FIXED_ONE` | Alias of the SDK's `ONE` (4096). |

```c
psxe_transform t = psxe_transform_identity();
t.position.vz = 900;                          /* 900 units in front of camera */
t.rotation.vy = PSXE_ANGLE_FULL_TURN / 4;     /* 90 degree yaw */
t.scale.vx = t.scale.vy = t.scale.vz = ONE * 2; /* 2x size */
```

## Engine lifecycle — `engine.h`

```c
typedef struct psxe_game {
    void (*init)(psxe_engine *engine, void *user_data);
    void (*update)(psxe_engine *engine, void *user_data);
    void (*render)(psxe_engine *engine, void *user_data);
    void (*shutdown)(psxe_engine *engine, void *user_data);
    void *user_data;
} psxe_game;

typedef struct psxe_engine_config {
    psxe_render_config render;
    psxe_memory_card_config memory_card;
} psxe_engine_config;

struct psxe_engine {
    psxe_renderer renderer;
    psxe_input input;
    psxe_audio audio;
    psxe_memory_card_config memory_card;
    uint32_t frame;              /* incremented after every rendered frame */
    uint16_t ticks_per_second;   /* 60 NTSC, 50 PAL */
    bool running;
};
```

`psxe_engine` contains both render buffers (about 2 × (OT + packet arena)
bytes). Declare it `static`, never on the stack.

### `void psxe_engine_init(psxe_engine *engine, const psxe_engine_config *config)`

Zeroes `engine`, copies the memory-card identity, picks 50/60 ticks from
`config->render.video_mode`, sets `running = true`, then initializes renderer →
input → audio → memory card. Call exactly once. Neither pointer may be `NULL`.

### `void psxe_engine_run(psxe_engine *engine, const psxe_game *game)`

Calls `init` once, then loops while `engine->running`:

1. `psxe_input_poll` → `update`
2. `psxe_renderer_begin_frame` → `render` → `psxe_renderer_end_frame`
3. `++engine->frame`

After the loop it calls `psxe_audio_stop_all` and then `shutdown`. Every
callback may be `NULL`. Returns immediately if either argument is `NULL`.

### `void psxe_engine_stop(psxe_engine *engine)`

Sets `running = false`. The current frame finishes rendering, then the loop
exits. Console games normally never call it; it is useful for tests and demo
attract loops.

```c
int main(void) {
    static psxe_engine engine;
    static my_game state;
    const psxe_engine_config config = {
        .render = { .width = 320, .height = 240, .video_mode = PSXE_VIDEO_NTSC,
                    .clear_color = { 0, 0, 0 }, .dither = true },
        .memory_card = { .product_code = "BAPXE-00001", .identifier = "MYGAME",
                         .title = "MY GAME", .format_version = 1 }
    };
    const psxe_game game = { game_init, game_update, game_render, NULL, &state };

    psxe_engine_init(&engine, &config);
    psxe_engine_run(&engine, &game);
    return 0;
}
```

## Renderer — `render.h`

### Types

| Type | Purpose |
| --- | --- |
| `psxe_video_mode` | `PSXE_VIDEO_NTSC`, `PSXE_VIDEO_PAL`. |
| `psxe_material_flags` | Per-face bits: `PSXE_MATERIAL_TEXTURED`, `PSXE_MATERIAL_LIT`, `PSXE_MATERIAL_SEMITRANSPARENT`, `PSXE_MATERIAL_DOUBLE_SIDED`. |
| `psxe_material` | Per-object/per-sprite override: `texture`, `color`, `color_mode`, `effects`, `blend`. See [Materials and effects](#materials-and-effects). |
| `psxe_blend_mode`, `psxe_color_mode`, `psxe_effect_flags` | Enums used by `psxe_material`. |
| `psxe_object` | `mesh` + `transform` + `material` + `visible`. See [Objects](#objects--psxe_object). |
| `psxe_sprite` | Screen rectangle with texel region, size, `layer`, `material`, `visible`. See [Sprites](#sprites--psxe_sprite). |
| `psxe_render_target`, `psxe_render_target_config` | Offscreen VRAM rectangle usable as a texture. See [Render to texture](#render-to-texture--psxe_render_target). |
| `psxe_render_config` | `width`, `height`, `video_mode`, `clear_color`, `dither`. |
| `psxe_camera` | `position` (world), `rotation` (Euler), `projection` (GTE screen distance; larger = narrower field of view, default 160). |
| `psxe_texture` | VRAM location filled by `psxe_texture_load_tim`: `tpage`, `clut`, `width`, `height`, base `u`/`v`, `valid`. |
| `psxe_face` | One triangle: `vertex[3]` indices, `normal` index, `uv[3][2]` offsets from the texture's base `u`/`v`, flat `color`, material `flags`. |
| `psxe_mesh` | Borrowed `vertices`, `normals`, `faces` arrays with their counts, plus an optional `texture`. |
| `psxe_renderer` | Double buffers and frame state. Fields worth reading: `submitted_primitives`, `dropped_primitives`, `target_passes`, `next_packet`/`packet_end`, `width`, `height`, `camera`. `ordering_table`/`ot_length` point at the current destination (main buffer, or a render target during a pass); the other fields are internal. |

### `void psxe_renderer_init(psxe_renderer *renderer, const psxe_render_config *config)`

Resets the GPU, sets the video mode, creates two stacked draw/display buffers
(`0,0` and `0,height` in VRAM), initializes the GTE with the screen center as
geometry offset, loads the debug font at VRAM `(960, 0)`, installs a default
light (direction `{-2048,-2048,-2048}`, color `{220,220,220}`, ambient
`{48,48,48}`) and a default camera at the origin with projection 160, and
enables display. `psxe_engine_init` calls it for you.

### `void psxe_renderer_begin_frame(psxe_renderer *renderer)`

Resets `submitted_primitives`, `dropped_primitives`, and `target_passes` to
zero. Called by the engine before `render`.

### `void psxe_renderer_end_frame(psxe_renderer *renderer)`

Closes a render-target pass left open, waits for the GPU (`DrawSync`) and
vblank (`VSync`), shows the previous buffer, submits this frame's offscreen
passes (in the order they began) followed by the active ordering table, swaps
buffers, resets the packet pointer, and clears the next ordering table. Called
by the engine after `render`. Never reuse or modify a packet after this call.

### `void psxe_renderer_set_camera(psxe_renderer *renderer, const psxe_camera *camera)`

Copies the camera and sets the GTE projection distance. Ignores `NULL`. It takes
effect for every later `psxe_draw_mesh`, even in the same frame. The view is
built by negating the camera's Euler angles; this is an exact inverse for
single-axis rotation (yaw-only or pitch-only) but only an approximation when
several angles are combined.

```c
psxe_camera cam = { .position = { 0, -200, -600 }, .rotation = { 0, 0, 0, 0 }, .projection = 200 };
psxe_renderer_set_camera(&engine->renderer, &cam);
```

### `void psxe_renderer_set_directional_light(psxe_renderer *renderer, const SVECTOR *direction, psxe_color color, psxe_color ambient)`

Sets one directional light (world space, components in `ONE` units) plus the
ambient color. Color and ambient are written to global GTE registers
immediately. Lighting is evaluated when `psxe_draw_mesh` runs, so changing the
light between two draw calls lights those meshes differently in the same frame.
Arguments are not `NULL`-checked.

```c
SVECTOR sun = { 0, ONE, ONE / 2, 0 };
psxe_renderer_set_directional_light(&engine->renderer, &sun,
    (psxe_color){ 255, 230, 200 }, (psxe_color){ 40, 40, 60 });
```

### `bool psxe_texture_load_tim(psxe_texture *texture, const uint32_t *tim_data)`

Parses an in-memory TIM, uploads pixels (and its CLUT, if present) to the VRAM
position stored in the TIM, waits for the transfer, and fills `texture`. `u` is
the texel offset inside the 64-halfword-aligned texture page: `(x & 63) * 4`
for 4-bit, `(x & 63) * 2` for 8-bit, `x & 63` for 16-bit TIMs.
Returns `false` for `NULL` or an unparsable TIM. It does not check the TIM's
VRAM rectangle against the framebuffers or the font at `(960, 0)`: place
textures in free VRAM when you author the TIM. Call it during `init` or a
loading screen, never every frame.

```c
extern const uint32_t crate_tim[];   /* embedded with psn00bsdk_target_incbin */
static psxe_texture crate;

if (!psxe_texture_load_tim(&crate, crate_tim)) {
    /* fall back to untextured faces */
}
```

### `void psxe_draw_mesh(psxe_renderer *renderer, const psxe_mesh *mesh, const psxe_transform *transform)`

Same as `psxe_draw_mesh_material(renderer, mesh, transform, NULL)`. Transforms
the mesh by `transform` and the current camera, then for each face:

- drops it (and counts it in `dropped_primitives`) if a vertex index is out of
  range;
- skips back faces unless `PSXE_MATERIAL_DOUBLE_SIDED` is set (match the winding
  of `cube_faces` in [`samples/cube/main.c`](samples/cube/main.c));
- skips faces whose ordering-table index falls outside `[2, ot_length)`
  (`PSXE_OT_LENGTH` for the main pass, the target's `ot_length` in a
  render-target pass).
  There is **no near-plane clipping**: faces too close to or behind the camera
  disappear rather than being cut;
- emits `POLY_FT3` when `TEXTURED` is set and `mesh->texture` is valid,
  otherwise `POLY_F3`;
- with `LIT` and a valid `normal` index, multiplies the face `color` by the
  light. For textured faces, color `{128,128,128}` means unmodified texels;
- untextured `SEMITRANSPARENT` faces get a small state packet so they blend
  with the requested mode (half for `psxe_draw_mesh`). If that packet does not
  fit, the face is dropped rather than drawn with the wrong blend.

Ignores `NULL` renderer, mesh, transform, vertices, or faces. Faces are flat
shaded (no Gouraud).

```c
static const SVECTOR tri_v[] = { { -50, 50, 0, 0 }, { 50, 50, 0, 0 }, { 0, -50, 0, 0 } };
static const psxe_face tri_f[] = {
    { { 0, 2, 1 }, 0, { { 0, 0 }, { 0, 0 }, { 0, 0 } }, { 255, 128, 0 }, PSXE_MATERIAL_DOUBLE_SIDED }
};
static const psxe_mesh tri = { tri_v, NULL, tri_f, 3, 0, 1, NULL };

psxe_transform t = psxe_transform_identity();
t.position.vz = 400;
psxe_draw_mesh(&engine->renderer, &tri, &t);
```

### `void psxe_draw_sprite(psxe_renderer *renderer, const psxe_texture *texture, int16_t x, int16_t y, int16_t width, int16_t height, psxe_color tint, bool semitransparent)`

Immediate-mode shortcut for an unscaled `psxe_sprite`: draws `width` x `height`
texels of `texture` from its base `u`/`v` at pixel `(x, y)` in bucket 1 (above
meshes, below text), half-blended when `semitransparent`. `tint`
`{128,128,128}` is neutral. Ignores `NULL`, invalid textures, and non-positive
sizes. Use a [`psxe_sprite`](#sprites--psxe_sprite) for atlas regions,
scaling, flipping, other blend modes, solid fills, or other layers.

```c
psxe_draw_sprite(&engine->renderer, &icon, 280, 8, 32, 32, (psxe_color){ 128, 128, 128 }, false);
```

### `void psxe_ui_panel(psxe_renderer *renderer, int16_t x, int16_t y, int16_t width, int16_t height, psxe_color color, bool semitransparent)`

Draws a solid rectangle in bucket 1. With `semitransparent`, it is blended 50/50
with what is behind it.

### `void psxe_ui_text(psxe_renderer *renderer, int16_t x, int16_t y, const char *text)`

Draws text with the SDK debug font in bucket 0 (always on top). Packet cost is
one small sprite per character plus one state packet. The samples use
uppercase ASCII.

```c
psxe_ui_panel(&engine->renderer, 4, 4, 312, 20, (psxe_color){ 0, 0, 40 }, true);
psxe_ui_text(&engine->renderer, 10, 10, "PRESS START");
```

### Layering rules

- Text (bucket 0) > sprites and panels (bucket 1) > meshes (bucket 2+, nearer
  over farther). A `psxe_sprite` with a larger `layer` sorts among meshes, and
  `layer = PSXE_OT_LENGTH - 1` is a background behind all of them.
- Inside one bucket, the ordering table is last-in-first-drawn. Among panels and
  sprites, **the one you submit first ends up on top**. Submit a background
  panel *after* the sprites that sit on it.
- Meshes are sorted per triangle by average depth (painter's algorithm, no
  Z-buffer). Large or intersecting triangles can sort wrongly; split them.

## Materials and effects

A `psxe_material` changes how a mesh or sprite looks **without touching the
shared, immutable mesh data**. Every option is applied while packets are built
or through GPU state bits: none adds GTE work, and `PSXE_EFFECT_UNLIT` and
`PSXE_EFFECT_UNTEXTURED` remove work.

```c
typedef struct psxe_material {
    const psxe_texture *texture; /* NULL: mesh->texture (objects) / solid fill (sprites) */
    psxe_color color;            /* meaning set by color_mode; sprites: tint or fill */
    uint8_t color_mode;          /* psxe_color_mode */
    uint8_t effects;             /* psxe_effect_flags, OR-ed */
    uint8_t blend;               /* psxe_blend_mode */
} psxe_material;
```

| `psxe_color_mode` | Face color used |
| --- | --- |
| `PSXE_COLOR_FACE` (default) | Each face's own `color`. |
| `PSXE_COLOR_REPLACE` | `material.color` for every face (flat recolor, hit flash, team color). |
| `PSXE_COLOR_TINT` | `face * material.color / 128` per channel, clamped. `{128,128,128}` = unchanged. |

The resolved color is what lighting multiplies and what modulates texels
(128 = unchanged texels, 255 ≈ 2x brighter).

| `psxe_effect_flags` | Effect | Cost |
| --- | --- | --- |
| `PSXE_EFFECT_UNLIT` | Ignore `PSXE_MATERIAL_LIT`; use the resolved color directly. | Saves the GTE lighting step per face. |
| `PSXE_EFFECT_UNTEXTURED` | Draw flat color even for textured faces/sprites (silhouettes, distant LOD). | Smaller packets, no texel fetch. |
| `PSXE_EFFECT_SEMITRANSPARENT` | Blend every primitive with `blend`. | GPU read-modify-write; untextured faces and solid sprites add one state packet each. |
| `PSXE_EFFECT_DOUBLE_SIDED` | Skip back-face rejection for all faces. | More faces submitted. |
| `PSXE_EFFECT_FLIP_X`, `PSXE_EFFECT_FLIP_Y` | Sprites only: mirror the texel region. | Uses a quad instead of `SPRT`. |

| `psxe_blend_mode` | Result (B = framebuffer, F = primitive) |
| --- | --- |
| `PSXE_BLEND_HALF` (default) | B/2 + F/2 |
| `PSXE_BLEND_ADD` | B + F (glow, fire, lights) |
| `PSXE_BLEND_SUBTRACT` | B − F (shadows, darkening) |
| `PSXE_BLEND_QUARTER` | B + F/4 (faint additive) |

Faces flagged `PSXE_MATERIAL_SEMITRANSPARENT` in the mesh also use the
material's `blend`. Textured pixels blend only where the texel's STP bit
(bit 15) is set, which is how the GPU works; untextured primitives always
blend.

| Function | Behavior |
| --- | --- |
| `psxe_material psxe_material_default(void)` | No texture override, color `{128,128,128}`, `PSXE_COLOR_FACE`, no effects, `PSXE_BLEND_HALF`. Drawing with it looks the same as drawing with no material. |
| `void psxe_material_set_texture(psxe_material *m, const psxe_texture *t)` | Sets the texture override; `NULL` returns to the mesh's own texture. |
| `void psxe_material_set_color(psxe_material *m, psxe_color c, psxe_color_mode mode)` | Sets color and color mode. |
| `void psxe_material_set_effects(psxe_material *m, uint8_t effects, psxe_blend_mode blend)` | Replaces the effect bits and blend mode. |

All ignore a `NULL` material.

### `void psxe_draw_mesh_material(psxe_renderer *renderer, const psxe_mesh *mesh, const psxe_transform *transform, const psxe_material *material)`

`psxe_draw_mesh` with an optional override (`NULL` = none). A material texture
replaces `mesh->texture` **for faces that carry `PSXE_MATERIAL_TEXTURED`**,
reusing their UVs as offsets from the new texture's base `u`/`v`. Faces
without that flag stay untextured, and UVs beyond a smaller replacement
texture wrap inside its 256x256 texture page. Material effects are OR-ed with
the per-face flags.

```c
psxe_material ghost = psxe_material_default();
psxe_material_set_color(&ghost, (psxe_color){ 96, 160, 255 }, PSXE_COLOR_TINT);
psxe_material_set_effects(&ghost, PSXE_EFFECT_UNLIT | PSXE_EFFECT_SEMITRANSPARENT, PSXE_BLEND_ADD);
psxe_draw_mesh_material(&engine->renderer, &player_mesh, &ghost_transform, &ghost);
```

## Objects — `psxe_object`

```c
typedef struct psxe_object {
    const psxe_mesh *mesh;     /* borrowed, shared between objects */
    psxe_transform transform;
    psxe_material material;
    bool visible;
} psxe_object;
```

An object is one drawable instance of a mesh. Many objects can share one
`static const psxe_mesh` and still each have their own position, texture,
color, and effects. Objects do not use the scene pool; store them wherever
your game state lives. Scene entities carry the same `material` field (see
[Scene](#scene--sceneh)).

| Function | Behavior |
| --- | --- |
| `void psxe_object_init(psxe_object *o, const psxe_mesh *mesh)` | Identity transform, default material, `visible = true`. |
| `void psxe_object_set_mesh(psxe_object *o, const psxe_mesh *mesh)` | Swaps the mesh (for example a low-detail version). |
| `void psxe_object_set_texture(psxe_object *o, const psxe_texture *t)` | Changes the texture of an existing object; `NULL` returns to `mesh->texture`. |
| `void psxe_object_set_color(psxe_object *o, psxe_color c, psxe_color_mode mode)` | See color modes above. |
| `void psxe_object_set_effects(psxe_object *o, uint8_t effects, psxe_blend_mode blend)` | Replaces effect bits and blend mode. |
| `void psxe_object_set_position(psxe_object *o, int32_t x, int32_t y, int32_t z)` | World units. |
| `void psxe_object_translate(psxe_object *o, int32_t dx, int32_t dy, int32_t dz)` | Adds to the position. |
| `void psxe_object_set_rotation(psxe_object *o, int16_t x, int16_t y, int16_t z)` | 4096 = full turn. |
| `void psxe_object_rotate(psxe_object *o, int16_t dx, int16_t dy, int16_t dz)` | Adds and wraps to `0..4095`. |
| `void psxe_object_set_scale(psxe_object *o, int32_t scale)` | Uniform scale, `ONE` = 1.0. Set `transform.scale` directly for non-uniform scale. |
| `void psxe_object_draw(psxe_renderer *r, const psxe_object *o)` | Draws if `visible` (via `psxe_draw_mesh_material`). |

All ignore `NULL` objects.

```c
static psxe_object crates[8];
unsigned int i;

for (i = 0; i < 8; ++i) {
    psxe_object_init(&crates[i], &crate_mesh);
    psxe_object_set_position(&crates[i], (int32_t)i * 220 - 770, 100, 1200);
}
psxe_object_set_texture(&crates[3], &crate_damaged_tex);           /* swap texture */
psxe_object_set_color(&crates[5], (psxe_color){ 255, 80, 80 }, PSXE_COLOR_TINT);

/* render: */
for (i = 0; i < 8; ++i) {
    psxe_object_draw(&engine->renderer, &crates[i]);
}
```

## Sprites — `psxe_sprite`

```c
typedef struct psxe_sprite {
    int16_t x, y;                 /* screen top-left */
    int16_t width, height;        /* on-screen size */
    uint8_t u, v;                 /* region offset from the texture base */
    uint16_t region_width;        /* region size in texels (0 = width) */
    uint16_t region_height;       /* (0 = height) */
    uint16_t layer;               /* ordering-table bucket, default 1 */
    psxe_material material;       /* texture, tint/fill color, effects, blend */
    bool visible;
} psxe_sprite;
```

How a sprite is drawn:

| Case | GPU packets |
| --- | --- |
| No valid texture, or `PSXE_EFFECT_UNTEXTURED` | `TILE` filled with `material.color` (+1 state packet if semi-transparent). |
| Region size equals on-screen size, no flip | `SPRT` + texture-page state packet (cheapest textured path). |
| Region size differs (scaling) or flipped | One textured quad (`POLY_FT4`). |

`material.color` tints textured sprites (`{128,128,128}` = unchanged) and is
the fill color of solid ones; `color_mode` and `UNLIT` do not apply to
sprites. Region texel coordinates are clamped to the 0..255 range of the
texture page. A flipped region drops its last texel column/row, because the
GPU never draws a polygon's right and bottom edge. `layer` values beyond the
current ordering table clamp to its last bucket.

| Function | Behavior |
| --- | --- |
| `void psxe_sprite_init(psxe_sprite *s, const psxe_texture *t)` | Position 0,0; region = whole texture (clamped to 256); size = region size (16x16 if `t` is `NULL`/invalid); `layer = 1`; default material; visible. |
| `void psxe_sprite_set_texture(psxe_sprite *s, const psxe_texture *t)` | Changes the texture of an existing sprite. Resets the region to the whole new texture; keeps position, on-screen size (unless it was 0), color, effects, and layer. `NULL` makes it a solid rectangle. |
| `void psxe_sprite_set_region(psxe_sprite *s, uint8_t u, uint8_t v, uint16_t w, uint16_t h)` | Selects an atlas cell or animation frame. Does not change the on-screen size. |
| `void psxe_sprite_set_position(psxe_sprite *s, int16_t x, int16_t y)` / `psxe_sprite_move(s, dx, dy)` | Screen pixels. |
| `void psxe_sprite_set_size(psxe_sprite *s, int16_t w, int16_t h)` | On-screen size; differs from the region → scaled. |
| `void psxe_sprite_set_color(psxe_sprite *s, psxe_color c)` | Tint or fill color. |
| `void psxe_sprite_set_effects(psxe_sprite *s, uint8_t effects, psxe_blend_mode blend)` | For example `PSXE_EFFECT_FLIP_X \| PSXE_EFFECT_SEMITRANSPARENT`. |
| `void psxe_sprite_set_layer(psxe_sprite *s, uint16_t layer)` | 0 = with text, 1 = UI (default), larger = among/behind meshes. |
| `void psxe_sprite_draw(psxe_renderer *r, const psxe_sprite *s)` | Draws if visible and the size is positive. |

All ignore `NULL` sprites.

```c
/* 4-frame walk cycle in a 64x16 atlas, drawn 2x size, facing left. */
psxe_sprite hero;
psxe_sprite_init(&hero, &hero_atlas);
psxe_sprite_set_region(&hero, 0, 0, 16, 16);
psxe_sprite_set_size(&hero, 32, 32);
psxe_sprite_set_effects(&hero, PSXE_EFFECT_FLIP_X, PSXE_BLEND_HALF);

/* update: */
psxe_sprite_set_region(&hero, (uint8_t)(((engine->frame / 8u) & 3u) * 16u), 0, 16, 16);
psxe_sprite_move(&hero, -1, 0);

/* render: */
psxe_sprite_draw(&engine->renderer, &hero);
```

## Render to texture — `psxe_render_target`

A render target is a free VRAM rectangle that you draw a 3D (or 2D) pass into
during `render`, then sample in the same frame as an ordinary 16-bit
`psxe_texture` on objects, entities, or sprites. Typical uses: security
monitors, mirrors, picture-in-picture, item previews, portals, and
precomposed HUD widgets.

```c
typedef struct psxe_render_target_config {
    int16_t x, y;              /* VRAM position; x multiple of 16 */
    int16_t width, height;     /* width 16..256 (multiple of 16), height 1..256 */
    psxe_color clear_color;
    bool clear;                /* fill before each pass */
    uint16_t ot_length;        /* buckets per pass, 3..PSXE_OT_LENGTH */
    uint8_t depth_shift;       /* average Z >> depth_shift = bucket */
} psxe_render_target_config;
```

| Function | Behavior |
| --- | --- |
| `bool psxe_render_target_init(psxe_render_target *t, const psxe_renderer *r, const psxe_render_target_config *c)` | Validates and fills `t`. Returns `false` (and leaves `t` invalid) for `NULL` arguments, bad sizes/alignment, a rectangle that leaves VRAM (1024x512), crosses a 256x256 texture-page boundary (`(x & 63) + width > 256` or `(y & 255) + height > 256`), overlaps the two framebuffers or the debug font at `(960, 0)`–`(991, 32)`, `ot_length` outside 3..`PSXE_OT_LENGTH`, or `depth_shift > 15`. It does not check overlap with your TIMs or other targets. |
| `bool psxe_render_target_begin(psxe_renderer *r, const psxe_render_target *t)` | Starts a pass. From now until `end`, every `psxe_draw_*`, `psxe_object_draw`, `psxe_sprite_draw`, `psxe_scene_render`, and `psxe_ui_*` call goes into the target, with the GTE screen center at the target's center. Returns `false` if a pass is already open, the target is invalid, or the packet arena cannot hold the pass's ordering table and two state packets (counted in `dropped_primitives`). Increments `target_passes`. |
| `void psxe_render_target_end(psxe_renderer *r)` | Returns drawing to the main ordering table and restores the camera, projection, depth shift, and screen center that were active at `begin`. No-op without an open pass. `psxe_renderer_end_frame` calls it if you forget. |
| `const psxe_texture *psxe_render_target_texture(const psxe_render_target *t)` | The 16-bit texture view (`clut = 0`, size = target size), or `NULL` if invalid. Valid for the target's whole lifetime; it shows whatever the last executed pass drew. |

Rules and costs:

- **Order.** Passes execute before the main pass, in the order they began, so
  the main pass and any later pass can sample an earlier target. Never sample a
  target inside its own pass.
- **Budget.** A pass takes `ot_length * 4` bytes plus 72 bytes from the current
  frame's packet arena, plus the packets of everything drawn into it. It also
  costs GPU fill time for the clear and for every pixel drawn. Skip the pass on
  some frames (for example every other frame) to halve the cost: VRAM keeps the
  last image.
- **Camera.** Call `psxe_renderer_set_camera` after `begin` for the pass's own
  view; `end` restores the main camera. Lights are **not** saved or restored.
- **Depth.** Mesh faces need `depth = avgZ >> depth_shift` in `[2, ot_length)`;
  a short table with a small shift rejects distant faces.
- **Transparency.** Texel value `0x0000` (pure black after 5-bit conversion) is
  transparent when sampled, so use a non-black `clear_color` and avoid
  near-black colors in the pass if holes are unwanted. Rendered pixels have the
  STP bit clear, so a semi-transparent primitive using the target texture does
  not blend.
- **VRAM.** One image; the target is not double buffered. The previous frame's
  display list always finishes before the next frame's begins, so this is safe.

```c
static psxe_render_target monitor;
static psxe_object tv_screen;    /* quad mesh with TEXTURED faces, UVs 0..127 */

/* init: */
const psxe_render_target_config cfg = {
    .x = 320, .y = 0, .width = 128, .height = 128,
    .clear_color = { 8, 24, 16 }, .clear = true,
    .ot_length = 256, .depth_shift = 2
};
if (psxe_render_target_init(&monitor, &engine->renderer, &cfg)) {
    psxe_object_init(&tv_screen, &quad_mesh);
    psxe_object_set_texture(&tv_screen, psxe_render_target_texture(&monitor));
}

/* render: */
if (psxe_render_target_begin(&engine->renderer, &monitor)) {
    psxe_renderer_set_camera(&engine->renderer, &security_camera);
    psxe_scene_render(&g->scene, &engine->renderer);
    psxe_ui_text(&engine->renderer, 4, 4, "CAM 2");
    psxe_render_target_end(&engine->renderer);
}
psxe_scene_render(&g->scene, &engine->renderer);         /* main view */
psxe_object_draw(&engine->renderer, &tv_screen);         /* shows CAM 2 */
```

[`samples/cube/main.c`](samples/cube/main.c) renders two 128x128 targets each
frame and shows one on a spinning quad and a scaled sprite; Circle swaps
which one, which is the same call you would use to change any object's or
sprite's texture.

## Scene — `scene.h`

```c
typedef struct psxe_entity_id { uint16_t index; uint16_t generation; } psxe_entity_id;

typedef struct psxe_entity {
    psxe_transform transform;
    const psxe_mesh *mesh;     /* NULL = not drawn */
    psxe_material material;    /* texture/color/effect override, default on create */
    uint16_t generation;
    bool active;
    bool visible;
} psxe_entity;

typedef struct psxe_scene {
    psxe_entity entities[PSXE_MAX_ENTITIES];
    uint16_t active_count;
} psxe_scene;
```

A handle is valid only while its slot is active and the generations match.
Destroying an entity bumps its generation, so old handles stop working even if
the slot is reused.

| Function | Behavior |
| --- | --- |
| `void psxe_scene_init(psxe_scene *scene)` | Clears all slots. Not `NULL`-checked. |
| `psxe_entity_id psxe_scene_create(psxe_scene *scene)` | Takes the first free slot, sets identity transform, `psxe_material_default()`, `visible = true`, `mesh = NULL`. When full (or `scene` is `NULL`) returns a handle with `index == UINT16_MAX`; check with `psxe_scene_get`. |
| `bool psxe_scene_destroy(psxe_scene *scene, psxe_entity_id id)` | Frees the slot and invalidates the handle. `false` for stale/invalid handles. |
| `psxe_entity *psxe_scene_get(psxe_scene *scene, psxe_entity_id id)` | Mutable entity, or `NULL` for a stale/invalid handle. |
| `const psxe_entity *psxe_scene_get_const(const psxe_scene *scene, psxe_entity_id id)` | Read-only variant. |
| `void psxe_scene_render(const psxe_scene *scene, psxe_renderer *renderer)` | Calls `psxe_draw_mesh_material` with the entity's `material` for each active, visible entity with a mesh, in slot order. Inside a render-target pass it draws into the target. |

```c
psxe_entity_id id = psxe_scene_create(&scene);
psxe_entity *e = psxe_scene_get(&scene, id);
if (e != NULL) {
    e->mesh = &cube_mesh;
    e->transform.position.vz = 900;
    psxe_material_set_texture(&e->material, &crate_tex);   /* per-entity texture */
}
/* later */
psxe_scene_destroy(&scene, id);
/* psxe_scene_get(&scene, id) now returns NULL */
```

## Input — `input.h`

```c
typedef struct psxe_pad_state {
    uint16_t held, pressed, released;   /* PadButton bit masks, 1 = active */
    uint8_t left_x, left_y, right_x, right_y;  /* 0..255, 128 = center */
    uint8_t type;                       /* PAD_ID_* from psxpad.h */
    bool connected;
} psxe_pad_state;
```

Buttons (`PadButton` from the SDK's `psxpad.h`): `PAD_SELECT`, `PAD_L3`,
`PAD_R3`, `PAD_START`, `PAD_UP`, `PAD_RIGHT`, `PAD_DOWN`, `PAD_LEFT`, `PAD_L2`,
`PAD_R2`, `PAD_L1`, `PAD_R1`, `PAD_TRIANGLE`, `PAD_CIRCLE`, `PAD_CROSS`,
`PAD_SQUARE`. Masks can be OR-ed and tested directly on `held`/`pressed`.

`connected` is true only for `PAD_ID_DIGITAL`, `PAD_ID_ANALOG_STICK`, and
`PAD_ID_ANALOG`. Other devices (mouse, guns, multitap) read as disconnected.
Sticks report 128 unless the pad is in an analog mode. There is no dead zone.

| Function | Behavior |
| --- | --- |
| `void psxe_input_init(psxe_input *input)` | Starts BIOS polling of both ports. Called by `psxe_engine_init`. |
| `void psxe_input_poll(psxe_input *input)` | Converts the raw active-low data to `held` and computes `pressed`/`released` against the previous poll. Called by the engine before every `update`. |
| `const psxe_pad_state *psxe_input_pad(const psxe_input *input, unsigned int port)` | Port 0 or 1, otherwise `NULL`. |
| `bool psxe_button_held(const psxe_pad_state *pad, PadButton button)` | Button down this frame. |
| `bool psxe_button_pressed(const psxe_pad_state *pad, PadButton button)` | Went down this frame. |
| `bool psxe_button_released(const psxe_pad_state *pad, PadButton button)` | Went up this frame. |

The button helpers return `false` for a `NULL` pad, so a missing port can be
passed straight through.

```c
const psxe_pad_state *pad = psxe_input_pad(&engine->input, 0);
if (psxe_button_pressed(pad, PAD_CROSS)) { jump(); }
if (psxe_button_held(pad, PAD_SQUARE)) { charge++; }
if (psxe_button_released(pad, PAD_SQUARE)) { fire(charge); charge = 0; }
if ((pad->held & (PAD_L1 | PAD_R1)) == (PAD_L1 | PAD_R1)) { reset_camera(); }
```

## Audio — `audio.h`

```c
typedef struct psxe_audio_sample {
    uint32_t spu_address;   /* where it lives in SPU RAM */
    uint32_t byte_size;     /* ADPCM payload bytes */
    uint32_t sample_rate;   /* Hz, from the VAG header */
    bool valid;
} psxe_audio_sample;
```

| Function | Behavior |
| --- | --- |
| `void psxe_audio_init(psxe_audio *audio)` | Initializes the SPU, sets master volume, resets the SPU RAM allocator to `0x1010`. Called by `psxe_engine_init`. |
| `bool psxe_audio_upload_vag(psxe_audio *audio, psxe_audio_sample *sample, const void *vag_data, size_t vag_size)` | Validates a mono VAG (`VAGp` magic, big-endian size), DMA-uploads the payload without the 48-byte header to the next 64-byte-aligned SPU address, and fills `sample`. `false` on bad data or when SPU RAM (512 KiB) is full. Space is never reclaimed. |
| `int psxe_audio_play(psxe_audio *audio, const psxe_audio_sample *sample, uint16_t volume, int16_t pan)` | Plays on the next voice (round robin over 24) and returns it, or `-1`. `volume` 0..`0x3fff`; `pan` `-0x1000` (left) .. `0` (center) .. `0x1000` (right); both are clamped. A busy voice is cut off. |
| `void psxe_audio_stop(int voice)` | Keys off one voice; ignores out-of-range values (including `-1`). |
| `void psxe_audio_stop_all(void)` | Keys off all 24 voices. Called by the engine after the loop exits. |

Uploads block on DMA; do them in `init` or during loading. Looping depends only
on the loop flags encoded in the VAG blocks. There is no "is playing" query.
No VAG sample is bundled yet, so audio is compile-tested only.

```c
extern const uint8_t jump_vag[];
extern const uint32_t jump_vag_size;
static psxe_audio_sample jump_sfx;

psxe_audio_upload_vag(&engine->audio, &jump_sfx, jump_vag, jump_vag_size);
int voice = psxe_audio_play(&engine->audio, &jump_sfx, 0x3000, 0);
```

## Memory card — `memory_card.h`

```c
typedef struct psxe_memory_card_config {
    const char *product_code;  /* e.g. "BAPXE-00001" */
    const char *identifier;    /* e.g. "CUBE"; product_code + identifier <= 20 chars */
    const char *title;         /* shown by the BIOS manager, truncated to 63 chars */
    uint16_t format_version;   /* bump whenever the payload layout changes */
} psxe_memory_card_config;
```

Results: `PSXE_MEMORY_CARD_OK`, `PSXE_MEMORY_CARD_INVALID_ARGUMENT`,
`PSXE_MEMORY_CARD_NOT_FOUND`, `PSXE_MEMORY_CARD_IO_ERROR`,
`PSXE_MEMORY_CARD_CORRUPT`, `PSXE_MEMORY_CARD_VERSION_MISMATCH`.

The file name is `bu00:` (slot 0 = physical slot 1) or `bu01:` (slot 1)
followed by `product_code` and `identifier`. Each identity is one 8192-byte
file with a BIOS header, a static icon, and an engine header (magic, version,
size, CRC32) at byte 512.

| Function | Behavior |
| --- | --- |
| `void psxe_memory_card_init(void)` | Starts BIOS card services. Called by `psxe_engine_init`. |
| `psxe_memory_card_result psxe_memory_card_save(const psxe_memory_card_config *config, unsigned int slot, const void *payload, size_t payload_size)` | Writes the whole block, replacing an existing save. `payload_size` must be 1..`PSXE_MAX_SAVE_PAYLOAD`. |
| `psxe_memory_card_result psxe_memory_card_load(const psxe_memory_card_config *config, unsigned int slot, void *payload, size_t payload_capacity, size_t *payload_size)` | Reads and validates magic, header version, size, `format_version`, and CRC **before** copying into `payload`. `INVALID_ARGUMENT` if the stored payload is larger than `payload_capacity`. `payload_size` may be `NULL`. |
| `const char *psxe_memory_card_result_string(psxe_memory_card_result result)` | Uppercase message suitable for `psxe_ui_text`. |

Card calls block for a noticeable time. Call them only on a user action or a
checkpoint, never every frame. The payload is copied as raw bytes: use
fixed-width fields and bump `format_version` whenever its layout changes.

```c
typedef struct save_v1 { int32_t level; int32_t score; } save_v1;

save_v1 s = { 3, 12000 };
psxe_memory_card_result r = psxe_memory_card_save(&engine->memory_card, 0, &s, sizeof(s));
psxe_ui_text(&engine->renderer, 10, 220, psxe_memory_card_result_string(r));
```

## Combining APIs: advanced recipes

These recipes only use the functions above. They show patterns, not shipped
engine features. Each one keeps to the engine rules: fixed capacity, no heap,
bounded failure.

### 1. Region-correct timers

`update` runs at 50 or 60 Hz. Derive durations from `ticks_per_second` so a PAL
build lasts as long in real time as an NTSC one.

```c
static uint16_t seconds_to_frames(const psxe_engine *engine, uint16_t tenths) {
    return (uint16_t)((engine->ticks_per_second * tenths) / 10u);
}

/* in init:   game->spawn_timer = seconds_to_frames(engine, 15);  -> 1.5 s */
/* in update: if (game->spawn_timer > 0 && --game->spawn_timer == 0) { spawn(); } */

/* Per-second speeds: move 300 units/s regardless of region. */
static int32_t per_frame(const psxe_engine *engine, int32_t units_per_second) {
    return units_per_second / (int32_t)engine->ticks_per_second;
}
```

Integer division loses precision: 300/60 = 5 and 300/50 = 6 are exact, but
100/60 truncates 1.67 to 1. For slow speeds, keep positions in a larger fixed-point unit and
shift down before writing `transform.position`.

### 2. Projectile pool: scene handles + input + audio

Generation-checked handles stop a dead bullet's stale handle from moving a new
entity that reused its slot.

```c
#define MAX_SHOTS 16

typedef struct shot { psxe_entity_id id; int16_t life; bool used; } shot;

typedef struct shooter_game {
    psxe_scene scene;
    psxe_entity_id ship;
    shot shots[MAX_SHOTS];
    psxe_audio_sample fire_sfx;
} shooter_game;

/* World X of roughly -160..160 -> SPU pan -0x1000..0x1000. Clamp in 32 bits
 * first: casting a large value to int16_t would wrap before the engine clamps. */
static int16_t pan_from_x(int32_t x) {
    int32_t pan = x * 25;
    if (pan < -0x1000) { pan = -0x1000; }
    if (pan > 0x1000)  { pan = 0x1000; }
    return (int16_t)pan;
}

static void fire(psxe_engine *engine, shooter_game *g) {
    const psxe_entity *ship = psxe_scene_get_const(&g->scene, g->ship);
    unsigned int i;

    for (i = 0; i < MAX_SHOTS; ++i) {
        if (!g->shots[i].used) {
            psxe_entity_id id = psxe_scene_create(&g->scene);
            psxe_entity *e = psxe_scene_get(&g->scene, id);
            if (e == NULL || ship == NULL) {
                return;                       /* scene full: drop the shot */
            }
            e->mesh = &shot_mesh;
            e->transform.position = ship->transform.position;
            g->shots[i] = (shot){ id, (int16_t)engine->ticks_per_second, true }; /* 1 s */
            psxe_audio_play(&engine->audio, &g->fire_sfx, 0x2800,
                            pan_from_x(ship->transform.position.vx));
            return;
        }
    }
}

static void update_shots(shooter_game *g) {
    unsigned int i;
    for (i = 0; i < MAX_SHOTS; ++i) {
        psxe_entity *e;
        if (!g->shots[i].used) {
            continue;
        }
        e = psxe_scene_get(&g->scene, g->shots[i].id);
        if (e == NULL || --g->shots[i].life <= 0) {
            psxe_scene_destroy(&g->scene, g->shots[i].id);   /* safe on stale ids */
            g->shots[i].used = false;
            continue;
        }
        e->transform.position.vz += 20;
    }
}

static void shooter_update(psxe_engine *engine, void *user_data) {
    shooter_game *g = user_data;
    const psxe_pad_state *pad = psxe_input_pad(&engine->input, 0);
    if (psxe_button_pressed(pad, PAD_CROSS)) {
        fire(engine, g);
    }
    update_shots(g);
}
```

`psxe_audio_play` also clamps `pan`, but only after the value has become an
`int16_t`, so clamp wide values yourself as `pan_from_x` does.

### 3. Analog orbit camera with digital fallback

Combines `psxe_pad_state` axes, the pad `type`, and `psxe_renderer_set_camera`
every frame. It uses yaw only, which is the case where the camera inverse is
exact.

```c
#define DEAD_ZONE 24

static int16_t axis(uint8_t raw) {
    int16_t v = (int16_t)raw - 128;
    return (v > -DEAD_ZONE && v < DEAD_ZONE) ? 0 : v;
}

static void orbit_update(psxe_engine *engine, my_game *g) {
    const psxe_pad_state *pad = psxe_input_pad(&engine->input, 0);
    psxe_camera cam;
    int16_t turn = 0;

    if (pad != NULL && pad->connected) {
        if (pad->type == PAD_ID_ANALOG || pad->type == PAD_ID_ANALOG_STICK) {
            turn = (int16_t)(axis(pad->right_x) / 4);
        }
        if (pad->held & PAD_L1) { turn = -24; }
        if (pad->held & PAD_R1) { turn = 24; }
    }
    g->yaw = (int16_t)((g->yaw + turn) & (PSXE_ANGLE_FULL_TURN - 1));

    /* Place the camera on a circle around the target and face it. */
    cam.position.vx = g->target.vx - ((isin(g->yaw) * g->distance) >> 12);
    cam.position.vy = g->target.vy - 150;
    cam.position.vz = g->target.vz - ((icos(g->yaw) * g->distance) >> 12);
    cam.rotation = (SVECTOR){ 0, g->yaw, 0, 0 };
    cam.projection = 200;
    psxe_renderer_set_camera(&engine->renderer, &cam);
}
```

`isin`/`icos` come from the SDK's `psxgte.h` (already included by
`psxe/render.h`) and return `ONE`-scaled values. Check the sign of the yaw
against your scene: flip `g->yaw` or the `isin` term if the camera orbits away
from the target.

### 4. Render budget HUD and adaptive detail

`begin_frame` clears the counters before `render`, and `update` runs before
`begin_frame`. So during `update`, `submitted_primitives` and
`dropped_primitives` still describe the **previous** frame. Use that to lower
detail before the arena overflows, and measure packet bytes at the end of
`render`.

```c
static void budget_update(psxe_engine *engine, my_game *g) {
    if (engine->renderer.dropped_primitives > 0) {
        g->use_low_detail = true;                 /* last frame overflowed */
    } else if (g->peak_packet_bytes < (PSXE_PACKET_BUFFER_SIZE / 2)) {
        g->use_low_detail = false;                /* comfortable headroom */
    }
}

static void budget_render(psxe_engine *engine, my_game *g) {
    psxe_renderer *r = &engine->renderer;
    const psxe_mesh *tree = g->use_low_detail ? &tree_low : &tree_high;
    char line[48];
    unsigned int i;
    size_t used;

    for (i = 0; i < g->tree_count; ++i) {
        psxe_draw_mesh(r, tree, &g->trees[i]);     /* one mesh, many transforms */
    }

    /* Bytes used so far = arena size minus what is left. */
    used = PSXE_PACKET_BUFFER_SIZE - (size_t)(r->packet_end - r->next_packet);
    g->peak_packet_bytes = used;

    snprintf(line, sizeof(line), "PKT %lu/%u DROP %lu",
             (unsigned long)used, (unsigned)PSXE_PACKET_BUFFER_SIZE,
             (unsigned long)r->dropped_primitives);
    psxe_ui_text(r, 8, 224, line);                 /* text itself costs packets */
}
```

Draw the HUD last if you want the byte count to include the 3D scene only, or
measure again after the text if you want the full total. This recipe also shows
instancing: one immutable `psxe_mesh` drawn with many transforms, with no scene
entity needed.

### 5. Per-object lighting in one frame

Lighting is computed during `psxe_draw_mesh`, so changing the light between
draws gives each object its own light. This works for a dark interior next to a
lit exterior, a damage flash, or a highlighted selection.

```c
static void lit_render(psxe_engine *engine, my_game *g) {
    psxe_renderer *r = &engine->renderer;
    static const SVECTOR sun = { -ONE / 2, ONE, ONE / 2, 0 };

    /* World: warm sun. */
    psxe_renderer_set_directional_light(r, &sun, (psxe_color){ 255, 220, 180 }, (psxe_color){ 40, 40, 50 });
    psxe_scene_render(&g->scene, r);

    /* Selected object: flash white every 8 frames. */
    if ((engine->frame & 8u) != 0u) {
        psxe_renderer_set_directional_light(r, &sun, (psxe_color){ 255, 255, 255 }, (psxe_color){ 160, 160, 160 });
    }
    psxe_draw_mesh(r, &item_mesh, &g->selected_transform);

    /* Restore before the next frame's world pass. */
    psxe_renderer_set_directional_light(r, &sun, (psxe_color){ 255, 220, 180 }, (psxe_color){ 40, 40, 50 });
}
```

A day/night cycle is the same idea spread over time: interpolate `color` and
`ambient` from `engine->frame` once per frame before drawing.

### 6. Pause menu: input edges + semitransparent UI + audio

```c
typedef enum { MODE_PLAY, MODE_PAUSE } game_mode;

static void pause_update(psxe_engine *engine, my_game *g) {
    const psxe_pad_state *pad = psxe_input_pad(&engine->input, 0);

    if (psxe_button_pressed(pad, PAD_START)) {
        g->mode = (g->mode == MODE_PLAY) ? MODE_PAUSE : MODE_PLAY;
        if (g->mode == MODE_PAUSE) {
            psxe_audio_stop_all();             /* silence looping effects */
        }
        psxe_audio_play(&engine->audio, &g->menu_sfx, 0x2000, 0);
        return;
    }
    if (g->mode == MODE_PAUSE) {
        if (psxe_button_pressed(pad, PAD_UP))   { g->cursor = 0; }
        if (psxe_button_pressed(pad, PAD_DOWN)) { g->cursor = 1; }
        return;                                /* world is frozen */
    }
    simulate_world(engine, g);
}

static void pause_render(psxe_engine *engine, my_game *g) {
    psxe_renderer *r = &engine->renderer;
    psxe_scene_render(&g->scene, r);           /* frozen world still visible */

    if (g->mode == MODE_PAUSE) {
        /* First-submitted is on top inside bucket 1: cursor before backdrop. */
        psxe_ui_panel(r, 100, (int16_t)(100 + g->cursor * 12), 120, 10, (psxe_color){ 200, 160, 40 }, false);
        psxe_ui_panel(r, 0, 0, r->width, r->height, (psxe_color){ 0, 0, 0 }, true);
        psxe_ui_text(r, 112, 80, "PAUSED");
        psxe_ui_text(r, 112, 102, "RESUME");
        psxe_ui_text(r, 112, 114, "SAVE GAME");
    }
}
```

### 7. Versioned saves with migration

The load call validates `format_version` against the config you pass in. Because
the config is a plain struct, you can retry with an older version to migrate old
saves without engine support.

```c
typedef struct save_v1 { int32_t level; int32_t score; } save_v1;
typedef struct save_v2 { int32_t level; int32_t score; int32_t lives; uint8_t flags[16]; } save_v2;

static psxe_memory_card_result load_any(const psxe_engine *engine, save_v2 *out) {
    size_t size = 0;
    psxe_memory_card_result r = psxe_memory_card_load(&engine->memory_card, 0, out, sizeof(*out), &size);

    if (r == PSXE_MEMORY_CARD_OK && size != sizeof(*out)) {
        return PSXE_MEMORY_CARD_CORRUPT;        /* right version, wrong size */
    }
    if (r == PSXE_MEMORY_CARD_VERSION_MISMATCH) {
        psxe_memory_card_config old = engine->memory_card;
        save_v1 v1;
        old.format_version = 1;
        r = psxe_memory_card_load(&old, 0, &v1, sizeof(v1), &size);
        if (r == PSXE_MEMORY_CARD_OK && size == sizeof(v1)) {
            memset(out, 0, sizeof(*out));
            out->level = v1.level;
            out->score = v1.score;
            out->lives = 3;                     /* default for the new field */
        }
    }
    return r;
}

/* main(): .memory_card.format_version = 2 */
```

Write the migrated data back with `psxe_memory_card_save` on the next explicit
save, not automatically every frame. Use slot 1 (`bu01:`) the same way for a
second card, for example "copy save to other card":

```c
save_v2 s;
if (load_any(engine, &s) == PSXE_MEMORY_CARD_OK) {
    psxe_memory_card_result r = psxe_memory_card_save(&engine->memory_card, 1, &s, sizeof(s));
    snprintf(g->status, sizeof(g->status), "COPY: %s", psxe_memory_card_result_string(r));
}
```

### 8. Two-player split controls

```c
static void versus_update(psxe_engine *engine, versus_game *g) {
    unsigned int port;
    for (port = 0; port < 2; ++port) {
        const psxe_pad_state *pad = psxe_input_pad(&engine->input, port);
        psxe_entity *player = psxe_scene_get(&g->scene, g->players[port]);

        if (player == NULL) {
            continue;
        }
        player->visible = (pad != NULL) && pad->connected;   /* hide absent players */
        if (psxe_button_held(pad, PAD_LEFT))  { player->transform.position.vx -= 4; }
        if (psxe_button_held(pad, PAD_RIGHT)) { player->transform.position.vx += 4; }
        if (psxe_button_pressed(pad, PAD_CROSS)) {
            psxe_audio_play(&engine->audio, &g->hit_sfx, 0x3000, port == 0 ? -0x0c00 : 0x0c00);
        }
    }
}
```

### 9. Textured mesh and sprite from one embedded TIM

Embed binary assets with the SDK's CMake helper in your sample's
`CMakeLists.txt`. It defines `symbol` and `symbol_size`:

```cmake
psn00bsdk_target_incbin(my_game PRIVATE atlas_tim assets/atlas.tim)
psn00bsdk_target_incbin(my_game PRIVATE jump_vag  assets/jump.vag)
```

```c
extern const uint32_t atlas_tim[];
static psxe_texture atlas;

/* UVs are offsets from the texture's base u/v (0..width-1, 0..height-1). */
static const psxe_face quad_faces[] = {
    { { 0, 1, 2 }, 0, { { 0, 0 }, { 63, 0 }, { 0, 63 } },  { 128, 128, 128 },
      PSXE_MATERIAL_TEXTURED | PSXE_MATERIAL_LIT },
    { { 1, 3, 2 }, 0, { { 63, 0 }, { 63, 63 }, { 0, 63 } }, { 128, 128, 128 },
      PSXE_MATERIAL_TEXTURED | PSXE_MATERIAL_LIT }
};
static psxe_mesh quad;   /* not const: texture pointer is set at runtime */

static void tex_init(psxe_engine *engine, void *user_data) {
    if (psxe_texture_load_tim(&atlas, atlas_tim)) {
        quad.texture = &atlas;   /* invalid/NULL texture -> faces fall back to flat color */
    }
}

static void tex_render(psxe_engine *engine, void *user_data) {
    psxe_draw_mesh(&engine->renderer, &quad, &quad_transform);
    /* Same VRAM image reused as a HUD icon (top-left 32x32 texels). */
    psxe_draw_sprite(&engine->renderer, &atlas, 8, 8, 32, 32, (psxe_color){ 128, 128, 128 }, false);
}
```

Fill `quad`'s vertices, normals, and counts like `cube_mesh` in the sample.
Author the TIM at a VRAM position that does not overlap the two framebuffers
(`0,0` to `width,2*height`), the debug font at `(960, 0)`, or any render
target. To show a different atlas cell, use a `psxe_sprite` with
`psxe_sprite_set_region`; `psxe_draw_sprite` always starts at the base
`u`/`v`.

### 10. Driving the subsystems without `psxe_engine_run`

For tools, tests, or a custom loop (for example several update ticks per
rendered frame), call the same functions the engine uses, in the same order.

```c
int main(void) {
    static psxe_engine engine;
    static psxe_scene scene;
    const psxe_engine_config config = { /* ... as above ... */ };
    unsigned int step;

    psxe_engine_init(&engine, &config);        /* still initializes every subsystem */
    psxe_scene_init(&scene);

    for (;;) {
        psxe_input_poll(&engine.input);
        for (step = 0; step < 2; ++step) {      /* two simulation steps per frame */
            simulate(&scene, &engine.input);
        }
        psxe_renderer_begin_frame(&engine.renderer);
        psxe_scene_render(&scene, &engine.renderer);
        psxe_renderer_end_frame(&engine.renderer);
        ++engine.frame;
    }
}
```

Keep `begin_frame`/`end_frame` paired once per video frame and never draw
outside them. Input edges (`pressed`/`released`) are computed per poll, so poll
once per frame, not once per simulation step.

### 11. Damage flash and death fade with materials only

Effects that need no extra assets and no GTE work: replace the color for a
flash, then switch to additive blending and fade the tint toward black, which
adds nothing to the framebuffer.

```c
static void enemy_update(psxe_engine *engine, enemy *e) {
    psxe_object *o = &e->object;

    if (e->hit_timer > 0) {
        --e->hit_timer;
        psxe_object_set_color(o, (psxe_color){ 255, 255, 255 }, PSXE_COLOR_REPLACE);
        psxe_object_set_effects(o, PSXE_EFFECT_UNLIT, PSXE_BLEND_HALF);   /* cheaper, too */
    } else if (e->dying > 0) {
        uint8_t level = (uint8_t)(e->dying * 4);   /* dying counts 63 -> 0: 252 -> 0 */
        --e->dying;
        psxe_object_set_color(o, (psxe_color){ level, level, level }, PSXE_COLOR_TINT);
        psxe_object_set_effects(o, PSXE_EFFECT_SEMITRANSPARENT | PSXE_EFFECT_UNLIT, PSXE_BLEND_ADD);
        o->visible = (e->dying > 0);
    } else {
        o->material = psxe_material_default();
    }
}
```

Use `PSXE_EFFECT_UNTEXTURED | PSXE_EFFECT_UNLIT` on far-away objects as a cheap
level of detail: same mesh, smaller packets, no lighting.

### 12. Minimap from a top-down render target

Combines a render target, a camera override, entity materials, and a sprite.
The pass is rendered every 4th frame; VRAM keeps the last image in between.

```c
static void minimap_render(psxe_engine *engine, my_game *g) {
    psxe_renderer *r = &engine->renderer;
    static const psxe_camera top = {
        .position = { 0, -3000, 0 }, .rotation = { 1024, 0, 0, 0 }, .projection = 200
    };

    if (((engine->frame & 3u) == 0u) && psxe_render_target_begin(r, &g->minimap)) {
        psxe_renderer_set_camera(r, &top);   /* pitch-only: exact camera inverse */
        psxe_scene_render(&g->scene, r);
        psxe_render_target_end(r);
    }
    psxe_scene_render(&g->scene, r);
    psxe_sprite_draw(r, &g->minimap_sprite); /* init'ed with psxe_render_target_texture() */
}
```

Make the minimap sprite smaller than the target to scale it down
(`psxe_sprite_set_size`), or add `PSXE_EFFECT_SEMITRANSPARENT` with
`PSXE_BLEND_HALF` for an overlay look (only untextured parts blend; see the
STP note above).

## Quick index

| Header | Functions |
| --- | --- |
| `types.h` | `psxe_transform_identity` |
| `engine.h` | `psxe_engine_init`, `psxe_engine_run`, `psxe_engine_stop` |
| `render.h` (core) | `psxe_renderer_init`, `psxe_renderer_begin_frame`, `psxe_renderer_end_frame`, `psxe_renderer_set_camera`, `psxe_renderer_set_directional_light`, `psxe_texture_load_tim`, `psxe_draw_mesh`, `psxe_draw_mesh_material`, `psxe_draw_sprite`, `psxe_ui_panel`, `psxe_ui_text` |
| `render.h` (materials) | `psxe_material_default`, `psxe_material_set_texture`, `psxe_material_set_color`, `psxe_material_set_effects` |
| `render.h` (objects) | `psxe_object_init`, `psxe_object_set_mesh`, `psxe_object_set_texture`, `psxe_object_set_color`, `psxe_object_set_effects`, `psxe_object_set_position`, `psxe_object_translate`, `psxe_object_set_rotation`, `psxe_object_rotate`, `psxe_object_set_scale`, `psxe_object_draw` |
| `render.h` (sprites) | `psxe_sprite_init`, `psxe_sprite_set_texture`, `psxe_sprite_set_region`, `psxe_sprite_set_position`, `psxe_sprite_move`, `psxe_sprite_set_size`, `psxe_sprite_set_color`, `psxe_sprite_set_effects`, `psxe_sprite_set_layer`, `psxe_sprite_draw` |
| `render.h` (render to texture) | `psxe_render_target_init`, `psxe_render_target_begin`, `psxe_render_target_end`, `psxe_render_target_texture` |
| `scene.h` | `psxe_scene_init`, `psxe_scene_create`, `psxe_scene_destroy`, `psxe_scene_get`, `psxe_scene_get_const`, `psxe_scene_render` |
| `input.h` | `psxe_input_init`, `psxe_input_poll`, `psxe_input_pad`, `psxe_button_held`, `psxe_button_pressed`, `psxe_button_released` |
| `audio.h` | `psxe_audio_init`, `psxe_audio_upload_vag`, `psxe_audio_play`, `psxe_audio_stop`, `psxe_audio_stop_all` |
| `memory_card.h` | `psxe_memory_card_init`, `psxe_memory_card_save`, `psxe_memory_card_load`, `psxe_memory_card_result_string` |

Not available yet (see [`ROADMAP.md`](ROADMAP.md)): near-plane clipping,
Gouraud shading, fog, sprite rotation, double-buffered or paletted render
targets, a VRAM allocator, animation, collision, VRAM/SPU allocators with reclaim,
voice status, streaming audio, asynchronous or listable card operations,
rumble, and multitap.
