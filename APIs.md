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
- [Lights — `psxe_light`](#lights--psxe_light)
- [Objects — `psxe_object`](#objects--psxe_object)
- [Sprites — `psxe_sprite`](#sprites--psxe_sprite)
- [Render to texture — `psxe_render_target`](#render-to-texture--psxe_render_target)
- [Scene — `scene.h`](#scene--sceneh)
- [Input — `input.h`](#input--inputh)
- [CD-ROM — `cd.h`](#cd-rom--cdh)
- [Audio — `audio.h`](#audio--audioh)
- [Asset memory — `memory.h` and `vram.h`](#asset-memory--memoryh-and-vramh)
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
- **Failure is bounded.** The frame renderer and scene use fixed-capacity storage.
  When a fixed arena is full, the call returns failure (`false`, `-1`, an invalid handle, an error
  code) or increments `renderer.dropped_primitives`.
- **Ownership.** Meshes, vertices, faces, textures, and samples are borrowed by
  pointer. They must outlive every frame that draws them; `static const` data is
  the normal choice.
- **NULL.** Most functions ignore `NULL` arguments (noted per function). The
  `*_init` functions (except `psxe_light_init`) do not check.

## Configuration macros — `config.h`

| Macro | Default | Meaning |
| --- | --- | --- |
| `PSXE_OT_LENGTH` | `1024` | Ordering-table buckets per buffer. Bucket 0 = text, 1 = sprites/panels, 2+ = meshes (larger = farther). |
| `PSXE_PACKET_BUFFER_SIZE` | `48 * 1024` | GPU packet bytes per buffer (two buffers exist). |
| `PSXE_MAX_ENTITIES` | `128` | Slots in a `psxe_scene`. |
| `PSXE_MAX_LIGHTS` | `8` | Lights in the renderer's light set (1..255). |
| `PSXE_MAX_SCENE_LIGHTS` | `8` | Entity-owned lights in a `psxe_scene` (1..254). |
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
    uint16_t cpu_ticks;          /* last frame's input, update and render interval */
    bool running;
};
```

`psxe_engine` contains both render buffers (about 2 × (OT + packet arena)
bytes). Declare it `static`, never on the stack.

### `void psxe_engine_init(psxe_engine *engine, const psxe_engine_config *config)`

Zeroes `engine`, copies the memory-card identity, picks 50/60 ticks from
`config->render.video_mode`, sets `running = true`, then initializes renderer →
input → audio → memory card. Initialize the optional CD service after this call. Neither pointer may be `NULL`.

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
| `psxe_render_config` | `width`, `height`, `video_mode`, `clear_color`, `dither`, `profile_timing`. |
| `psxe_camera` | `position` (world), `rotation` (Euler), `projection` (GTE screen distance; larger = narrower field of view, default 160). |
| `psxe_texture` | VRAM location filled by `psxe_texture_load_tim`: `tpage`, `clut`, `width`, `height`, base `u`/`v`, `valid`. |
| `psxe_face` | One triangle: `vertex[3]` indices, `normal` index, `uv[3][2]` offsets from the texture's base `u`/`v`, flat `color`, material `flags`. |
| `psxe_mesh` | Borrowed `vertices`, `normals`, `faces` arrays with their counts, plus an optional `texture`. |
| `psxe_light`, `psxe_light_set`, `psxe_light_type`, `psxe_light_falloff` | Light values and the renderer's fixed set. See [Lights](#lights--psxe_light). |
| `psxe_renderer` | Double buffers, frame state, and the diagnostics described below. `ordering_table`/`ot_length` address the active pass. `next_packet`/`packet_end` bound the active packet arena. |

### `void psxe_renderer_init(psxe_renderer *renderer, const psxe_render_config *config)`

Resets the GPU, sets the video mode, creates two stacked draw/display buffers
(`0,0` and `0,height` in VRAM), initializes the GTE with the screen center as
geometry offset, loads the debug font at VRAM `(960, 0)`, installs a default
light set through `psxe_renderer_set_directional_light` (direction
`{-2048,-2048,-2048}` normalized to `ONE`, color `{220,220,220}`, ambient
`{48,48,48}`) and a default camera at the origin with projection 160, and
enables display. `psxe_engine_init` calls it for you.

### `void psxe_renderer_begin_frame(psxe_renderer *renderer)`

Resets draw, rejection, target-pass, and light counters to zero. Called by
the engine before `render`.

### `void psxe_renderer_end_frame(psxe_renderer *renderer)`

Closes a render-target pass left open, waits for the GPU (`DrawSync`) and
vblank (`VSync`), shows the previous buffer, submits this frame's offscreen
passes (in the order they began) followed by the active ordering table, swaps
buffers, resets the packet pointer, and clears the next ordering table. Called
by the engine after `render`. Never reuse or modify a packet after this call.

Read the following diagnostics during `update` to inspect the previous
completed frame.

| Field | Meaning |
| --- | --- |
| `submitted_primitives` | Draw packets linked by the draw helpers, including font glyphs and draw-mode packets. Render-target environment packets are tracked through `target_passes`. |
| `dropped_primitives` | Failed packet reservations and faces with invalid vertex indices. A failed compound reservation counts once. |
| `rejected_faces` | Faces skipped for invalid vertex indices, back-face rejection, or depth outside the active ordering table. |
| `target_passes` | Successfully opened render-target passes. |
| `packet_bytes`, `packet_high_water` | Arena bytes used by the completed frame and the peak since renderer initialization. Includes offscreen ordering tables and draw environment packets. |
| `lights_considered`, `lights_applied`, `lights_folded`, `lights_dropped` | Per-frame light diagnostics. |
| `gpu_wait_ticks` | Time spent waiting on `DrawSync` for the previous GPU submission. The subsequent VSync wait is excluded. |

Set `.profile_timing = true` in `psxe_render_config` to collect
`engine->cpu_ticks` and `renderer->gpu_wait_ticks`. The game grants the engine
exclusive access to hardware Timer 2. CPU timing covers input polling,
`update`, and `render` before `psxe_renderer_end_frame`. It measures elapsed
time, including blocking calls made by the game. Both intervals use
`PSXE_PROFILE_TICKS_PER_SECOND` (4,233,600 ticks per second). A reading of
`UINT16_MAX` means the interval reached the timer limit, about 15.48 ms.
Timing fields start at zero and stay zero when timing is disabled.

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

One-light wrapper over the [light set](#lights--psxe_light): clears the
renderer's lights, adds one directional light (direction normalized to `ONE`,
pointing from surfaces toward the light, intensity `ONE`), and sets the ambient
color. Nothing is written to the GTE here; lighting is installed per mesh when
it is drawn, so changing lights between two draw calls lights those meshes
differently in the same frame. Ignores `NULL` arguments.

```c
SVECTOR sun = { 0, -ONE, ONE / 2, 0 };   /* light above (Y down) and behind */
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
    uint8_t light_exclude;       /* lights whose layers intersect are ignored */
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
| `PSXE_EFFECT_RAW_TEXTURE` | Use the sampled color directly for textured draws, including the selected CLUT. Untextured faces keep their existing color and lighting. | Skips textured-face color modulation and GTE lighting. Combine with `PSXE_EFFECT_UNLIT` on a baked mesh to skip per-mesh light selection too. |

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
| `psxe_material psxe_material_default(void)` | No texture override, color `{128,128,128}`, `PSXE_COLOR_FACE`, no effects, `PSXE_BLEND_HALF`, `light_exclude = 0`. Drawing with it looks the same as drawing with no material. |
| `void psxe_material_set_texture(psxe_material *m, const psxe_texture *t)` | Sets the texture override; `NULL` returns to the mesh's own texture. |
| `void psxe_material_set_color(psxe_material *m, psxe_color c, psxe_color_mode mode)` | Sets color and color mode. |
| `void psxe_material_set_effects(psxe_material *m, uint8_t effects, psxe_blend_mode blend)` | Replaces the effect bits and blend mode. |
| `void psxe_material_set_light_exclude(psxe_material *m, uint8_t layers)` | Lights whose `layers` share a bit with this mask are skipped for the mesh. Default `0` (all lights apply). |

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

## Lights — `psxe_light`

Lighting stays flat per face (one `NCCS` per lit face), but any number of
lights can now affect a scene. The GTE has three light rows, so **for every
mesh draw** the renderer ranks the candidate lights by their contribution at
the mesh's origin and keeps the best three. Everything else becomes ambient.
This runs on the CPU, before the mesh's GTE state is installed, and only for
meshes that can light (`PSXE_EFFECT_UNLIT` or a mesh without normals skips it).

```c
typedef struct psxe_light {
    VECTOR position;   /* POINT: world position (scene lights: entity-local offset) */
    SVECTOR direction; /* DIRECTIONAL: unit vector (ONE) from surfaces toward the light */
    psxe_color color;
    uint8_t type;      /* psxe_light_type */
    uint8_t falloff;   /* psxe_light_falloff */
    uint8_t layers;    /* matched against psxe_material.light_exclude */
    bool enabled;
    int32_t intensity; /* 20.12; ONE = color at full strength, > ONE overbright */
    int32_t radius;    /* POINT: world units where the contribution reaches 0 */
} psxe_light;

typedef struct psxe_light_set {
    psxe_light lights[PSXE_MAX_LIGHTS];
    uint16_t count;
    psxe_color ambient;
} psxe_light_set;
```

| `psxe_light_type` | Behavior |
| --- | --- |
| `PSXE_LIGHT_DIRECTIONAL` | Constant direction. Candidate for a GTE row. |
| `PSXE_LIGHT_POINT` | Direction from the mesh origin to `position`, attenuated by distance. Candidate for a GTE row. |
| `PSXE_LIGHT_AMBIENT` | `color * intensity` added to the back color; no direction. |

| `psxe_light_falloff` (t = (radius − d) / radius, clamped 0..1) | Point-light multiplier |
| --- | --- |
| `PSXE_FALLOFF_LINEAR` (default) | t |
| `PSXE_FALLOFF_SMOOTH` | t², a softer edge |
| `PSXE_FALLOFF_NONE` | 1 (radius ignored) |

**Per-mesh selection rules.**

1. A light is a candidate when `enabled` and `(layers & material.light_exclude) == 0`
   (counted in `lights_considered`).
2. Its effective color is `color * intensity / 255` in 4.12 (255 at `ONE` = 1.0).
   For point lights, intensity is multiplied by the falloff first. A point
   light at distance ≥ `radius` (non-`NONE` falloff) contributes nothing.
3. Ambient lights, the set's `ambient`, and a point light exactly at the origin
   go into the back color.
4. The three directional/point candidates with the largest `r+g+b` fill the
   GTE light rows and color-matrix columns (`lights_applied`). Each one left
   over adds a quarter of its color to the back color, which is the average of
   `max(0, cos)` over all normal directions (`lights_folded`).
5. The back color clamps at 255 per channel.

Attenuation is evaluated once at the mesh origin, not per vertex or face, so a
big mesh next to a point light is lit evenly. Split large level geometry into
several meshes if you need local pools of light.

| Function | Behavior |
| --- | --- |
| `void psxe_light_init(psxe_light *l, psxe_light_type type)` | White, intensity `ONE`, enabled, `layers = PSXE_LIGHT_LAYER_DEFAULT` (1), direction `{0,-ONE,0}` (from above), position 0, radius 1024, `PSXE_FALLOFF_LINEAR`. Ignores `NULL`. |
| `void psxe_light_set_color(psxe_light *l, psxe_color c, int32_t intensity)` | Sets color and intensity (negative = 0). |
| `void psxe_light_set_direction(psxe_light *l, int32_t x, int32_t y, int32_t z)` | Normalizes any non-zero vector to length `ONE`; zero is ignored. Assigning `direction` directly skips normalization, so keep it unit length. |
| `void psxe_light_set_position(psxe_light *l, int32_t x, int32_t y, int32_t z)` | Sets the point-light position. |
| `void psxe_light_set_range(psxe_light *l, int32_t radius, psxe_light_falloff f)` | Sets radius (negative = 0) and falloff curve. |
| `void psxe_light_set_layers(psxe_light *l, uint8_t layers)` | Layer bits for material masks (`PSXE_LIGHT_LAYER_ALL` = `0xff`). |
| `void psxe_light_set_enabled(psxe_light *l, bool enabled)` | Toggles the light without removing it. |
| `void psxe_renderer_clear_lights(psxe_renderer *r)` | Empties the renderer's set and keeps the ambient color. |
| `bool psxe_renderer_add_light(psxe_renderer *r, const psxe_light *l)` | Copies one light into the set. `false` for `NULL`, or when full (also increments `lights_dropped`). |
| `uint16_t psxe_renderer_set_lights(psxe_renderer *r, const psxe_light *lights, uint16_t count)` | Replaces the set with a copy of `lights`. Returns how many were stored; any extra count as dropped. |
| `psxe_light *psxe_renderer_light(psxe_renderer *r, uint16_t index)` | Mutable access to stored light `index` (for animation), or `NULL`. |
| `void psxe_renderer_set_ambient(psxe_renderer *r, psxe_color ambient)` | Sets the set-wide ambient color. |
| `void psxe_draw_mesh_lights(psxe_renderer *r, const psxe_mesh *m, const psxe_transform *t, const psxe_material *mat, const psxe_light *extra, uint16_t extra_count)` | `psxe_draw_mesh_material` that also ranks `extra` world-space lights next to the renderer's set. `extra` is borrowed for this call only. `psxe_scene_render` uses it for entity lights. |
| `void psxe_draw_light_marker(psxe_renderer *r, const psxe_light *l, int16_t size)` | Debug: a solid `size`×`size` square in the light's color at an enabled point light's projected position, in bucket 1 (above meshes). Projected on the CPU with the current camera; skipped behind the camera. Costs one `TILE`. |

The light set is copied by value. Render-target passes save it at
`psxe_render_target_begin` and restore it at `end`, so a pass can install its
own lights. The light counters on `psxe_renderer` are summed over every lit
mesh draw in the frame, so the same light counts once for each mesh it reaches.

```c
psxe_light sun, torch;

psxe_light_init(&sun, PSXE_LIGHT_DIRECTIONAL);
psxe_light_set_direction(&sun, -ONE, -ONE, -ONE / 2);
psxe_light_set_color(&sun, (psxe_color){ 110, 130, 190 }, ONE);

psxe_light_init(&torch, PSXE_LIGHT_POINT);
psxe_light_set_position(&torch, 0, -150, 900);
psxe_light_set_color(&torch, (psxe_color){ 255, 180, 80 }, ONE + ONE / 2);
psxe_light_set_range(&torch, 700, PSXE_FALLOFF_SMOOTH);
psxe_light_set_layers(&torch, 0x02);           /* "world only" layer */

psxe_renderer_clear_lights(&engine->renderer);
psxe_renderer_add_light(&engine->renderer, &sun);
psxe_renderer_add_light(&engine->renderer, &torch);
psxe_renderer_set_ambient(&engine->renderer, (psxe_color){ 40, 40, 52 });

/* The held item ignores the torch but still gets the sun. */
psxe_material_set_light_exclude(&held_item.material, 0x02);
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
  view; `end` restores the main camera. The light set is saved at `begin` and
  restored at `end`, so lights you set inside the pass apply to it only.
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
    psxe_material material;    /* texture/color/effect/light-mask override */
    uint16_t generation;
    uint8_t light_slot;        /* internal; use psxe_scene_light */
    bool active;
    bool visible;
} psxe_entity;

typedef struct psxe_scene_light_slot {
    psxe_light light;          /* position/direction are entity-local */
    uint16_t owner;
    bool used;
} psxe_scene_light_slot;

typedef struct psxe_scene {
    psxe_entity entities[PSXE_MAX_ENTITIES];
    psxe_scene_light_slot lights[PSXE_MAX_SCENE_LIGHTS];
    uint16_t active_count;
    uint16_t light_count;
    uint16_t lights_dropped;   /* set_light calls rejected because the pool was full */
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
| `void psxe_scene_render(const psxe_scene *scene, psxe_renderer *renderer)` | Gathers the scene's entity lights once (`psxe_scene_gather_lights`), then calls `psxe_draw_mesh_lights` with the entity's `material` and those lights for each active, visible entity with a mesh, in slot order. Inside a render-target pass it draws into the target. |
| `bool psxe_scene_set_light(psxe_scene *scene, psxe_entity_id id, const psxe_light *light)` | Attaches a copy of `light` to the entity, or replaces its existing one. `position` is an offset and `direction` a local direction: both follow the entity's position and rotation (scale is ignored). `false` for stale handles, `NULL`, or a full pool (increments `scene->lights_dropped`). |
| `bool psxe_scene_clear_light(psxe_scene *scene, psxe_entity_id id)` | Detaches and frees the entity's light. `false` if it had none. `psxe_scene_destroy` does this for you, so stale handles never leave a light behind. |
| `psxe_light *psxe_scene_light(psxe_scene *scene, psxe_entity_id id)` | Mutable entity-local light (toggle `enabled`, change color), or `NULL`. |
| `bool psxe_scene_light_world(const psxe_scene *scene, psxe_entity_id id, psxe_light *out)` | Writes the light transformed to world space (for `psxe_draw_light_marker` or gameplay). |
| `uint16_t psxe_scene_gather_lights(const psxe_scene *scene, psxe_light *out, uint16_t capacity)` | Writes the world-space lights of every active entity whose light is enabled, whether or not its mesh is visible, and returns the count. Pass the result to `psxe_draw_mesh_lights` so objects outside the scene get the same lights. |

```c
psxe_entity_id id = psxe_scene_create(&scene);
psxe_entity *e = psxe_scene_get(&scene, id);
if (e != NULL) {
    e->mesh = &cube_mesh;
    e->transform.position.vz = 900;
    psxe_material_set_texture(&e->material, &crate_tex);   /* per-entity texture */
}
/* a lamp that carries its own point light */
psxe_light bulb;
psxe_light_init(&bulb, PSXE_LIGHT_POINT);
psxe_light_set_range(&bulb, 600, PSXE_FALLOFF_LINEAR);
psxe_scene_set_light(&scene, id, &bulb);   /* follows e->transform */
/* later */
psxe_scene_destroy(&scene, id);            /* also frees the light */
/* psxe_scene_get(&scene, id) now returns NULL */
```

## Input — `input.h`

```c
typedef struct psxe_pad_state {
    uint16_t held, pressed, released;   /* PadButton bit masks, 1 = active */
    uint8_t left_x, left_y, right_x, right_y;  /* 0..255, 128 = center */
    uint8_t type;                       /* PAD_ID_* from psxpad.h */
    bool connected;
    bool rumble_supported;             /* motor mapping succeeded */
} psxe_pad_state;
```

Buttons (`PadButton` from the SDK's `psxpad.h`): `PAD_SELECT`, `PAD_L3`,
`PAD_R3`, `PAD_START`, `PAD_UP`, `PAD_RIGHT`, `PAD_DOWN`, `PAD_LEFT`, `PAD_L2`,
`PAD_R2`, `PAD_L1`, `PAD_R1`, `PAD_TRIANGLE`, `PAD_CIRCLE`, `PAD_CROSS`,
`PAD_SQUARE`. Masks can be OR-ed and tested directly on `held`/`pressed`.

`connected` is true only for `PAD_ID_DIGITAL`, `PAD_ID_ANALOG_STICK`, and
`PAD_ID_ANALOG`. Other devices (mouse, guns, multitap) read as disconnected.
Sticks report 128 unless the pad is in an analog mode. Games can apply a dead
zone when consuming the axes. The controller keeps its selected analog or digital
mode during motor negotiation. A lost connection emits release edges and clears
the motor request. The engine owns SIO0 polling alongside synchronous BIOS card
operations. Standalone callers should leave BIOS pad polling disabled.
[Controller protocol](https://psx-spx.consoledev.net/controllersandmemorycards/)

| Function | Behavior |
| --- | --- |
| `void psxe_input_init(psxe_input *input)` | Initializes snapshots and motor state. Called by `psxe_engine_init`. |
| `void psxe_input_poll(psxe_input *input)` | Polls both SIO0 ports, converts active-low data to `held`, and computes `pressed`/`released` against the previous poll. Called by the engine before every `update`. |
| `const psxe_pad_state *psxe_input_pad(const psxe_input *input, unsigned int port)` | Port 0 or 1, otherwise `NULL`. |
| `bool psxe_button_held(const psxe_pad_state *pad, PadButton button)` | Button down this frame. |
| `bool psxe_button_pressed(const psxe_pad_state *pad, PadButton button)` | Went down this frame. |
| `bool psxe_button_released(const psxe_pad_state *pad, PadButton button)` | Went up this frame. |
| `bool psxe_input_rumble(psxe_input *input, unsigned int port, bool small, uint8_t large, uint8_t frames)` | Requests small on/off and large intensity 0–255 for the next `frames` polls. Pass zero frames to stop on the next poll. Returns false when the port has no negotiated rumble. |
| `void psxe_input_stop_rumble(psxe_input *input)` | Clears both requests and immediately sends zero motor levels to configured pads. Called by the engine on exit. |

The button helpers return `false` for a `NULL` pad, so a missing port can be
passed straight through.

```c
const psxe_pad_state *pad = psxe_input_pad(&engine->input, 0);
if (psxe_button_pressed(pad, PAD_CROSS)) { jump(); }
if (psxe_button_held(pad, PAD_SQUARE)) { charge++; }
if (psxe_button_released(pad, PAD_SQUARE)) { fire(charge); charge = 0; }
if ((pad->held & (PAD_L1 | PAD_R1)) == (PAD_L1 | PAD_R1)) { reset_camera(); }
```

## CD-ROM — `cd.h`

Call from the main thread with interrupts enabled. The CD service owns the
single drive and its PSn00bSDK callbacks. Audio playback and sector reads
share the transport, so pause or stop audio before loading more data. LBAs
start at program-area MSF 00:02:00 and have 75 sectors per second.

| Function | Behavior |
| --- | --- |
| `psxe_cd_init()` | Initializes the drive after renderer/IRQ setup. Retries a pending SDK initialization with the engine's frame-based command timeout. Returns `PSXE_CD_NOT_READY` if initialization fails. Call before other CD operations. |
| `psxe_cd_status(&status)` | Reports lid, spindle, seek, data/XA read, and CD-DA play state. May be queried during audio playback. |
| `psxe_cd_seek(lba, audio_track)` | Waits for a logical data-header seek or a physical subchannel seek when `audio_track` is true. |
| `psxe_cd_position(&lba)` | Reports the current physical head position from subchannel Q. Data reads use the exact requested LBA. |
| `psxe_cd_motor_on()`, `psxe_cd_pause()`, `psxe_cd_stop()` | Spin up, pause with the spindle running, or stop transport and spindle. Pending sector reads must be cancelled or completed first. |
| `psxe_cd_read_start(lba, sectors, mode, destination, capacity)` | Starts an IRQ-driven read into word-aligned RAM. `PSXE_CD_DATA` transfers `PSXE_CD_SECTOR_SIZE` (2048) bytes per sector. `PSXE_CD_RAW` transfers `PSXE_CD_RAW_SECTOR_SIZE` (2340) bytes per sector, including the sector header and excluding sync bytes. |
| `psxe_cd_read_poll()`, `psxe_cd_read_wait()` | Check progress once or wait until complete. Success and errors finalize the transport before returning. The final poll may wait for the drive to pause. |
| `psxe_cd_read_cancel()` | Disarms DMA to the caller's buffer and stops reading. May stop the spindle if pause is rejected during a seek. |
| `uint32_t psxe_cd_sectors_read(void)` | Returns the running count of successful data and raw DMA sector transfers since boot, including filesystem reads and sectors transferred before cancellation. The unsigned counter wraps after `UINT32_MAX`. |
| `psxe_cd_find(path, &file)` | Finds a contiguous file on a primary ISO9660 volume. Use slash or backslash, case-insensitive path components, and an optional `;1` suffix. Returns the starting LBA and byte length in `psxe_cd_file`. |
| `psxe_cd_read_file(&file, offset, destination, size)` | Blocking exact byte-range read into caller-owned RAM. Partial sectors and unaligned destinations use a shared sector buffer. Re-resolve file extents after a disc change. |
| `psxe_cd_play_xa(lba, file, channel, double_speed)` | Starts decoding the selected XA file/channel. The caller controls the playback range and calls pause/stop. |
| `psxe_cd_xa_filter(file, channel)` | Changes the selected XA file/channel during playback. Channel range is 0–31. |
| `psxe_cd_play_cdda(lba)` | Starts CD-DA on an audio track and enables track-end auto-pause. Query status or pause/stop before the next transfer. |
| `psxe_cd_set_audio_volume(left, right)`, `psxe_cd_set_muted(muted)` | Set the SPU CD input levels (0–`0x7fff`) or mute/demute drive audio. |

Sector reads own access to their destination until poll, wait, or cancel
returns a terminal result. The start call returns `PSXE_CD_PENDING` when the
transfer begins. Treat the buffer as incomplete on error or cancellation.
After a transport failure, `psxe_cd_stop` can recover the drive before reuse.
The laser follows the drive's built-in seek and transport commands.
[CD-ROM command reference](https://github.com/psx-spx/psx-spx.github.io/blob/master/docs/cdromdrive.md)

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
| `psxe_cd_result psxe_audio_load_vag(psxe_audio *audio, psxe_audio_sample *sample, const psxe_cd_file *file, void *scratch, size_t capacity)` | Reads the VAG into caller-owned scratch of at least `file->size` bytes, then validates it and uploads to SPU RAM. The scratch can be released after the call. |
| `int psxe_audio_play(psxe_audio *audio, const psxe_audio_sample *sample, uint16_t volume, int16_t pan)` | Plays on the next voice (round robin over 24) and returns it, or `-1`. `volume` 0..`0x3fff`; `pan` `-0x1000` (left) .. `0` (center) .. `0x1000` (right); both are clamped. A busy voice is cut off. |
| `void psxe_audio_stop(int voice)` | Keys off one voice; ignores out-of-range values (including `-1`). |
| `void psxe_audio_stop_all(void)` | Keys off all 24 voices. Called by the engine after the loop exits. |
| `psxe_audio_stats psxe_audio_get_stats(const psxe_audio *audio)` | Returns `used_bytes` and `free_bytes` in the aligned sample region `0x1040..0x7ffff`. Includes DMA padding. Returns zeros for a null or uninitialized context. |

Uploads block on DMA; do them in `init` or during loading. Looping depends only
on the loop flags encoded in the VAG blocks. There is no "is playing" query.
The cube disc includes `/ASSETS/CHIME.VAG` as a resident audio example.

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
`PSXE_MEMORY_CARD_CORRUPT`, `PSXE_MEMORY_CARD_VERSION_MISMATCH`,
`PSXE_MEMORY_CARD_NO_CARD`, `PSXE_MEMORY_CARD_UNFORMATTED`,
and `PSXE_MEMORY_CARD_FULL`.

The file name is `bu00:` (slot 0 = physical slot 1) or `bu10:` (slot 1)
followed by `product_code` and `identifier`. Each identity is one 8192-byte
file with a BIOS header, a static icon, and a 16-byte little-endian engine
header at byte 512. Its fields are the four magic bytes `PXSE`, the 16-bit
header version (`1`), the 16-bit game `format_version`, the 32-bit payload
length, and the 32-bit CRC32 of the payload. Payload bytes start at byte 528.

| Function | Behavior |
| --- | --- |
| `void psxe_memory_card_init(void)` | Starts BIOS card services. Called by `psxe_engine_init`. |
| `psxe_memory_card_result psxe_memory_card_save(const psxe_memory_card_config *config, unsigned int slot, const void *payload, size_t payload_size)` | Writes the whole block, replacing an existing save. `payload_size` must be 1..`PSXE_MAX_SAVE_PAYLOAD`. |
| `psxe_memory_card_result psxe_memory_card_load(const psxe_memory_card_config *config, unsigned int slot, void *payload, size_t payload_capacity, size_t *payload_size)` | Reads and validates magic, header version, size, `format_version`, and CRC **before** copying into `payload`. `INVALID_ARGUMENT` if the stored payload is larger than `payload_capacity`. `payload_size` may be `NULL`. |
| `psxe_memory_card_result psxe_memory_card_status(unsigned int slot)` | Reads the card header to report presence and format. |
| `psxe_memory_card_result psxe_memory_card_list(unsigned int slot, psxe_memory_card_file *files, size_t capacity, size_t *count, unsigned int *free_blocks)` | Copies up to `capacity` entries with a zero-terminated name and size in bytes. Optional outputs receive the total entries and free 8192-byte blocks. Pass `NULL, 0` to query without copying. A card has at most `PSXE_MEMORY_CARD_MAX_FILES` (15) entries. |
| `psxe_memory_card_result psxe_memory_card_delete(const psxe_memory_card_config *config, unsigned int slot)` | Deletes the save with the given identity. |
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

## Asset memory — `memory.h` and `vram.h`

Use one caller-owned heap per asset lifetime and one managed VRAM pool per
renderer. These operations are intended for initialization and level changes.

| Function | Behavior |
| --- | --- |
| `psxe_heap_init(&heap, memory, size)` | Initializes heap metadata inside caller-owned RAM. Reinitializing the heap invalidates its allocations. |
| `psxe_heap_alloc(&heap, size)` | Returns an 8-byte-aligned block or `NULL`. Uses first-fit allocation. |
| `psxe_heap_free(&heap, memory)` | Releases a live allocation from this heap and coalesces adjacent free blocks. Returns false for an invalid allocation. |
| `psxe_heap_get_stats(&heap)` | Returns used, free, and largest contiguous free payload bytes. Header overhead is excluded. |
| `psxe_mesh_alloc(&heap, vertices, normals, faces)` | Allocates a `psxe_mesh_storage` with writable arrays and the read-only mesh view used by scene entities or objects. Fill all referenced vertices, faces, and normals before drawing. A mesh can have zero normals. |
| `psxe_mesh_free(&heap, storage)` | Releases the mesh and its arrays together after all instances detach it. |
| `psxe_vram_init(&vram, &renderer)` | Reserves both framebuffers and the debug font. |
| `psxe_vram_reserve(&vram, x, y, width, height)` | Reserves a fixed rectangle, rounded out to 16-halfword × 8-row tiles. Call before loading assets for fixed targets or manually placed textures. Coordinates use VRAM halfwords. |
| `psxe_vram_load_tim(&vram, bytes, size, &id)` | Validates a sized, word-aligned 4/8/16-bit TIM, relocates it inside one texture page and uploads its pixels and optional CLUT. The TIM's source coordinates are ignored. A CLUT block may contain multiple rows of 16 or 256 colors. |
| `psxe_vram_get(&vram, id)` | Returns a borrowed texture view or `NULL` for a stale ID. |
| `psxe_vram_palette(&vram, id, row, &view)` | Copies a texture view using a zero-based CLUT row. Keeps the same uploaded texels. |
| `psxe_vram_free(&vram, id)` | Synchronizes with submitted GPU work, invalidates the ID and recycles the texture and CLUT tiles. |
| `psxe_vram_stats psxe_vram_get_stats(const psxe_vram *vram)` | Counts `used_tiles` and `free_tiles` in the 64 × 64 pool. Used tiles include buffers, the font, reservations, pixels, and CLUTs. Free tiles may be fragmented or cross texture-page boundaries. Returns zeros for a null or uninitialized pool. |

Perform managed TIM uploads and frees outside frame builds, after detaching
every mesh, material, and sprite that borrows the texture or a palette view.
Source TIM bytes can be released after upload. A pool holds up to
`PSXE_VRAM_MAX_TEXTURES` textures, subject to available page-compatible space.
IDs belong to the lifetime of their pool. Discard them when reinitializing it.
Set the raw-texture and unlit flags on a mesh whose texture contains baked
shadows and highlights.

```c
psxe_texture evening;
if (psxe_vram_palette(&vram, wall_texture, 1, &evening)) {
    /* Keep evening alive for as long as the material borrows it. */
    psxe_material_set_texture(&wall_material, &evening);
    psxe_material_set_effects(&wall_material,
        PSXE_EFFECT_RAW_TEXTURE | PSXE_EFFECT_UNLIT, PSXE_BLEND_HALF);
}
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

During `update`, `packet_bytes` and `dropped_primitives` describe the previous
completed frame. Use them to lower detail when the arena runs short.

```c
static void budget_update(psxe_engine *engine, my_game *g) {
    if (engine->renderer.dropped_primitives > 0) {
        g->use_low_detail = true;
    } else if (engine->renderer.packet_bytes < (PSXE_PACKET_BUFFER_SIZE / 2)) {
        g->use_low_detail = false;
    }
}

static void budget_render(psxe_engine *engine, my_game *g) {
    psxe_renderer *r = &engine->renderer;
    const psxe_mesh *tree = g->use_low_detail ? &tree_low : &tree_high;
    char line[48];
    unsigned int i;

    for (i = 0; i < g->tree_count; ++i) {
        psxe_draw_mesh(r, tree, &g->trees[i]);     /* one mesh, many transforms */
    }

    snprintf(line, sizeof(line), "LAST PKT %lu PEAK %lu",
             (unsigned long)r->packet_bytes, (unsigned long)r->packet_high_water);
    psxe_ui_text(r, 8, 212, line);
}
```

The completed-frame total includes the HUD and offscreen passes. The tree
instances share one immutable mesh and each supply their own transform.

### 5. Per-object lighting in one frame

Lighting is selected and installed during each mesh draw, so changing the
light set between draws gives each object its own lights. This works for a dark interior next to a
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
`ambient` from `engine->frame` once per frame before drawing. For lights that
belong to only some geometry, prefer `psxe_light.layers` with
`psxe_material.light_exclude` over swapping light sets mid-frame.

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
save, not automatically every frame. Use slot 1 (`bu10:`) the same way for a
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
| `render.h` (core) | `psxe_renderer_init`, `psxe_renderer_begin_frame`, `psxe_renderer_end_frame`, `psxe_renderer_set_camera`, `psxe_renderer_set_directional_light`, `psxe_texture_load_tim`, `psxe_draw_mesh`, `psxe_draw_mesh_material`, `psxe_draw_mesh_lights`, `psxe_draw_sprite`, `psxe_ui_panel`, `psxe_ui_text` |
| `render.h` (materials) | `psxe_material_default`, `psxe_material_set_texture`, `psxe_material_set_color`, `psxe_material_set_effects`, `psxe_material_set_light_exclude` |
| `render.h` (lights) | `psxe_light_init`, `psxe_light_set_color`, `psxe_light_set_direction`, `psxe_light_set_position`, `psxe_light_set_range`, `psxe_light_set_layers`, `psxe_light_set_enabled`, `psxe_renderer_clear_lights`, `psxe_renderer_add_light`, `psxe_renderer_set_lights`, `psxe_renderer_light`, `psxe_renderer_set_ambient`, `psxe_draw_light_marker` |
| `render.h` (objects) | `psxe_object_init`, `psxe_object_set_mesh`, `psxe_object_set_texture`, `psxe_object_set_color`, `psxe_object_set_effects`, `psxe_object_set_position`, `psxe_object_translate`, `psxe_object_set_rotation`, `psxe_object_rotate`, `psxe_object_set_scale`, `psxe_object_draw` |
| `render.h` (sprites) | `psxe_sprite_init`, `psxe_sprite_set_texture`, `psxe_sprite_set_region`, `psxe_sprite_set_position`, `psxe_sprite_move`, `psxe_sprite_set_size`, `psxe_sprite_set_color`, `psxe_sprite_set_effects`, `psxe_sprite_set_layer`, `psxe_sprite_draw` |
| `render.h` (render to texture) | `psxe_render_target_init`, `psxe_render_target_begin`, `psxe_render_target_end`, `psxe_render_target_texture` |
| `scene.h` | `psxe_scene_init`, `psxe_scene_create`, `psxe_scene_destroy`, `psxe_scene_get`, `psxe_scene_get_const`, `psxe_scene_render`, `psxe_scene_set_light`, `psxe_scene_clear_light`, `psxe_scene_light`, `psxe_scene_light_world`, `psxe_scene_gather_lights` |
| `input.h` | `psxe_input_init`, `psxe_input_poll`, `psxe_input_rumble`, `psxe_input_stop_rumble`, `psxe_input_pad`, `psxe_button_held`, `psxe_button_pressed`, `psxe_button_released` |
| `cd.h` | `psxe_cd_init`, `psxe_cd_status`, `psxe_cd_seek`, `psxe_cd_position`, `psxe_cd_motor_on`, `psxe_cd_pause`, `psxe_cd_stop`, `psxe_cd_read_start`, `psxe_cd_read_poll`, `psxe_cd_read_wait`, `psxe_cd_read_cancel`, `psxe_cd_sectors_read`, `psxe_cd_find`, `psxe_cd_read_file`, `psxe_cd_play_xa`, `psxe_cd_xa_filter`, `psxe_cd_play_cdda`, `psxe_cd_set_audio_volume`, `psxe_cd_set_muted` |
| `memory.h` | `psxe_heap_init`, `psxe_heap_alloc`, `psxe_heap_free`, `psxe_heap_get_stats`, `psxe_mesh_alloc`, `psxe_mesh_free` |
| `vram.h` | `psxe_vram_init`, `psxe_vram_reserve`, `psxe_vram_load_tim`, `psxe_vram_get`, `psxe_vram_palette`, `psxe_vram_free`, `psxe_vram_get_stats` |
| `audio.h` | `psxe_audio_init`, `psxe_audio_upload_vag`, `psxe_audio_load_vag`, `psxe_audio_play`, `psxe_audio_stop`, `psxe_audio_stop_all`, `psxe_audio_get_stats` |
| `memory_card.h` | `psxe_memory_card_init`, `psxe_memory_card_save`, `psxe_memory_card_load`, `psxe_memory_card_status`, `psxe_memory_card_list`, `psxe_memory_card_delete`, `psxe_memory_card_result_string` |

Not available yet (see [`ROADMAP.md`](ROADMAP.md)): near-plane clipping,
Gouraud shading and per-vertex lighting, light-radius debug draw, fog, sprite rotation, double-buffered or paletted render
targets, animation, collision, SPU reclamation and voice completion tracking,
asynchronous card operations, and multitap.
