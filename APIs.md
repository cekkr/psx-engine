# psx-engine API Reference

This reference covers the public C99 declarations, calling rules, and examples
for the checked-out revision. The engine targets the original PlayStation
through PSn00bSDK. The API and save format are experimental. [Public headers](src/include/psxe/) · [Implementation map](AGENTS.md#interface-ownership-map) · [Roadmap](ROADMAP.md)

Use the umbrella header or the individual header named in each section.

```c
#include <psxe/psxe.h>
```

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
  - [Drive and transport](#drive-and-transport)
  - [Sector reads and file lookup](#sector-reads-and-file-lookup)
  - [XA and CD-DA](#xa-and-cd-da)
- [Audio — `audio.h`](#audio--audioh)
- [Memory card — `memory_card.h`](#memory-card--memory_cardh)
- [Asset memory — `memory.h` and `vram.h`](#asset-memory--memoryh-and-vramh)
  - [Main RAM and mesh storage](#main-ram-and-mesh-storage)
  - [Managed texture storage](#managed-texture-storage)
- [Combining APIs: advanced recipes](#combining-apis-advanced-recipes)
- [Quick index](#quick-index)

## Conventions

- **Units.** Positions and mesh vertices use integer world units. Rotation uses
  4096 units per full turn (`PSXE_ANGLE_FULL_TURN`). Scale, normals, and light
  directions use 4096 as 1.0 (`PSXE_FIXED_ONE`).
- **Axes.** The GTE projects with Y pointing down the screen and +Z going away
  from the camera; objects in front of a default camera have positive `vz`.
- **Time.** One `update` runs per video frame. `engine->ticks_per_second` is
  60 for NTSC and 50 for PAL. Use it to convert durations to frame counts.
- **Failure is bounded.** The frame renderer and scene use fixed-capacity storage.
  When a fixed arena is full, the call returns failure (`false`, `-1`, an invalid handle, an error
  code) or increments `renderer.dropped_primitives`.
- **Ownership.** Objects, entities, and materials borrow their mesh or texture
  pointers. Detach those borrowers before releasing managed assets. GPU packets
  hold copied draw data and reference VRAM, so managed VRAM release waits for
  pending GPU work. The disc service borrows a sector buffer from read start
  through completion or cancellation.
- **Pointers.** Pass initialized contexts and valid output buffers unless the
  function documents an optional pointer. The renderer, engine, scene, input,
  and audio initializers require writable context storage.

## Configuration macros — `config.h`

| Macro | Default | Meaning |
| --- | --- | --- |
| `PSXE_OT_LENGTH` | `1024` | Ordering-table buckets per buffer. Bucket 0 = text, 1 = sprites/panels, 2+ = meshes (larger = farther). |
| `PSXE_PACKET_BUFFER_SIZE` | `48 * 1024` | GPU packet bytes per buffer (two buffers exist). |
| `PSXE_MAX_ENTITIES` | `128` | Slots in a `psxe_scene`. |
| `PSXE_MAX_LIGHTS` | `8` | Lights in the renderer's light set (1..255). |
| `PSXE_MAX_SCENE_LIGHTS` | `8` | Entity-owned lights in a `psxe_scene` (1..254). |
| `PSXE_MEMORY_CARD_BLOCK_SIZE` | `8192` | Bytes per save file. The header rejects any other value. |
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

Zeroes `engine`, copies the memory-card configuration, selects the regional tick
rate, and sets `running = true`. Initializes the renderer, input, audio, and
memory card in that order. Use static storage for the engine and keep the
memory-card strings alive for later save calls. Initialize the optional CD
service after this call. Both pointers are required.

### `void psxe_engine_run(psxe_engine *engine, const psxe_game *game)`

Calls `init` once, then loops while `engine->running`:

1. `psxe_input_poll` → `update`
2. `psxe_renderer_begin_frame` → `render` → `psxe_renderer_end_frame`
3. `++engine->frame`

On exit it stops the SPU voices and controller rumble before calling `shutdown`.
Each callback is optional. The function returns immediately if either argument
is `NULL`.

### `void psxe_engine_stop(psxe_engine *engine)`

Sets `running = false`. A call from `update` or `render` lets that frame finish
before shutdown. A call from `init` skips the frame loop. Ignores a `NULL` engine.

This complete program changes the background while Cross is held and exits the
engine loop when Start is pressed.

```c
#include <psxe/psxe.h>

typedef struct demo_state {
    bool cross_held;
} demo_state;

static void demo_update(psxe_engine *engine, void *user_data) {
    demo_state *state = user_data;
    const psxe_pad_state *pad = psxe_input_pad(&engine->input, 0);
    state->cross_held = psxe_button_held(pad, PAD_CROSS);
    if (psxe_button_pressed(pad, PAD_START)) {
        psxe_engine_stop(engine);
    }
}

static void demo_render(psxe_engine *engine, void *user_data) {
    const demo_state *state = user_data;
    psxe_color color = state->cross_held ? (psxe_color){ 28, 90, 56 }
                                        : (psxe_color){ 20, 28, 48 };
    psxe_ui_panel(&engine->renderer, 0, 0, 320, 240, color, false);
    psxe_ui_text(&engine->renderer, 24, 32, "HOLD CROSS   START TO EXIT");
}

int main(void) {
    static psxe_engine engine;
    static demo_state state;
    const psxe_engine_config config = {
        .render = { .width = 320, .height = 240, .video_mode = PSXE_VIDEO_NTSC,
                    .clear_color = { 0, 0, 0 }, .dither = true },
        .memory_card = { .product_code = "BAPXE-00001", .identifier = "DEMO",
                         .title = "PSXE DEMO", .format_version = 1 }
    };
    const psxe_game game = {
        .update = demo_update, .render = demo_render, .user_data = &state
    };

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
| `psxe_render_buffer` | One `DISPENV`, `DRAWENV`, ordering table, and GPU packet arena. The renderer owns and swaps its two buffers. |
| `psxe_renderer` | Double buffers, frame state, and the diagnostics described below. `ordering_table`/`ot_length` address the active pass. `next_packet`/`packet_end` bound the active packet arena. |

### `void psxe_renderer_init(psxe_renderer *renderer, const psxe_render_config *config)`

Resets the GPU, sets the video mode, creates two stacked draw/display buffers
(`0,0` and `0,height` in VRAM), initializes the GTE with the screen center as
geometry offset, loads the debug font at VRAM `(960, 0)`, installs a default
light set through `psxe_renderer_set_directional_light` (direction
`{-2048,-2048,-2048}` normalized to `ONE`, color `{220,220,220}`, ambient
`{48,48,48}`) and a default camera at the origin with projection 160, and
enables display. `psxe_engine_init` calls it for you.

The two framebuffers occupy `(0, 0)` through `(width - 1, 2 * height - 1)` in
VRAM. Choose dimensions that fit this layout and stay clear of the debug font.
The engine sample uses 320 × 240.

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

Uploads an authored, word-aligned TIM to the VRAM coordinates stored in that
file. It queues pixels and any CLUT, waits for GPU work, and fills `texture`. `u` is
the texel offset inside the 64-halfword-aligned texture page: `(x & 63) * 4`
for 4-bit, `(x & 63) * 2` for 8-bit, `x & 63` for 16-bit TIMs.
Returns `false` for `NULL` or when the SDK TIM parser rejects the header. Use
complete embedded TIM data and reserve its authored pixel and CLUT rectangles
before using a managed VRAM pool. For a sized buffer loaded from disc, use
[`psxe_vram_load_tim`](#asset-memory--memoryh-and-vramh), which checks block
sizes and finds free VRAM. Upload during initialization or between frame builds.

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
  Near-plane crossings can distort or disappear because the renderer submits
  projected triangles without clipping them against the near plane;
- emits `POLY_FT3` when `TEXTURED` is set and `mesh->texture` is valid,
  otherwise `POLY_F3`;
- with `LIT` and a valid `normal` index, multiplies the face `color` by the
  light. For textured faces, color `{128,128,128}` means unmodified texels;
- untextured `SEMITRANSPARENT` faces get a small state packet so they blend
  with the requested mode (half for `psxe_draw_mesh`). The face and state
  command share one reservation and are dropped together when it fails.

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
texels from the texture's base `u`/`v` at screen pixel `(x, y)` in bucket 1.
`semitransparent` selects half blending for texels with the STP bit set.
`tint` `{128,128,128}` is neutral. Ignores `NULL`, invalid textures, and
non-positive sizes. Use [`psxe_sprite`](#sprites--psxe_sprite) for atlas regions,
scaling, flipping, other blend modes, solid fills, and other layers.

```c
psxe_draw_sprite(&engine->renderer, &icon, 280, 8, 32, 32, (psxe_color){ 128, 128, 128 }, false);
```

### `void psxe_ui_panel(psxe_renderer *renderer, int16_t x, int16_t y, int16_t width, int16_t height, psxe_color color, bool semitransparent)`

Draws a solid rectangle in bucket 1. With `semitransparent`, it is blended 50/50
with what is behind it. Ignores `NULL` renderers and non-positive dimensions.

### `void psxe_ui_text(psxe_renderer *renderer, int16_t x, int16_t y, const char *text)`

Draws a null-terminated string with PSn00bSDK's debug font in bucket 0. ASCII
letters are shown in uppercase. Spaces advance without emitting a glyph, and
`\n` starts a new 8-pixel line. The reservation uses the full string length as
an upper bound. Actual usage is one sprite per glyph plus one state packet.

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

A `psxe_material` sets appearance per instance. Store it on an object, sprite,
or scene entity, or pass it directly to a mesh draw. `PSXE_EFFECT_UNLIT` skips
mesh lighting. `PSXE_EFFECT_UNTEXTURED` uses the smaller flat-color packets.

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
| `void psxe_material_set_texture(psxe_material *material, const psxe_texture *texture)` | Sets the texture override; `NULL` returns to the mesh's own texture. |
| `void psxe_material_set_color(psxe_material *material, psxe_color color, psxe_color_mode mode)` | Sets color and color mode. |
| `void psxe_material_set_effects(psxe_material *material, uint8_t effects, psxe_blend_mode blend)` | Replaces the effect bits and blend mode. |
| `void psxe_material_set_light_exclude(psxe_material *material, uint8_t layers)` | Lights whose `layers` share a bit with this mask are skipped for the mesh. Default `0` (all lights apply). |

The setters ignore a `NULL` material. `psxe_material_default` returns a value.

### `void psxe_draw_mesh_material(psxe_renderer *renderer, const psxe_mesh *mesh, const psxe_transform *transform, const psxe_material *material)`

Draws with an optional override. A non-`NULL` material texture replaces
`mesh->texture` for faces carrying `PSXE_MATERIAL_TEXTURED`. Their UVs stay
relative to the texture's base `u`/`v`. Use compatible UV ranges when swapping
textures because the renderer stores coordinates as 8-bit page offsets and
does not bound them to the new texture rectangle. A `NULL` material uses the
mesh defaults.

```c
psxe_material ghost = psxe_material_default();
psxe_material_set_color(&ghost, (psxe_color){ 96, 160, 255 }, PSXE_COLOR_TINT);
psxe_material_set_effects(&ghost, PSXE_EFFECT_UNLIT | PSXE_EFFECT_SEMITRANSPARENT, PSXE_BLEND_ADD);
psxe_draw_mesh_material(&engine->renderer, &player_mesh, &ghost_transform, &ghost);
```

## Lights — `psxe_light`

Lighting is flat per face. Before drawing a mesh with normals, the renderer
ranks lights by contribution at its origin and places the strongest three
directional or point lights in the GTE light rows. Other directional or point
lights contribute to ambient. A mesh with `PSXE_EFFECT_UNLIT` skips selection.

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

Point-light direction and attenuation are shared by the whole mesh. Face
normals still affect brightness. Split large level geometry into smaller meshes
when a single light needs to produce different falloff across a surface.

| Function | Behavior |
| --- | --- |
| `void psxe_light_init(psxe_light *light, psxe_light_type type)` | White, intensity `ONE`, enabled, `layers = PSXE_LIGHT_LAYER_DEFAULT` (1), direction `{0,-ONE,0}` (from above), position 0, radius 1024, `PSXE_FALLOFF_LINEAR`. Ignores `NULL`. |
| `void psxe_light_set_color(psxe_light *light, psxe_color color, int32_t intensity)` | Sets color and intensity (negative = 0). |
| `void psxe_light_set_direction(psxe_light *light, int32_t x, int32_t y, int32_t z)` | Normalizes any non-zero vector to length `ONE`; zero is ignored. Assigning `direction` directly skips normalization, so keep it unit length. |
| `void psxe_light_set_position(psxe_light *light, int32_t x, int32_t y, int32_t z)` | Sets the point-light position. |
| `void psxe_light_set_range(psxe_light *light, int32_t radius, psxe_light_falloff falloff)` | Sets radius and falloff. Negative radius becomes zero, and an invalid falloff value selects linear. |
| `void psxe_light_set_layers(psxe_light *light, uint8_t layers)` | Layer bits for material masks (`PSXE_LIGHT_LAYER_ALL` = `0xff`). |
| `void psxe_light_set_enabled(psxe_light *light, bool enabled)` | Toggles the light without removing it. |
| `void psxe_renderer_clear_lights(psxe_renderer *renderer)` | Empties the renderer's set and keeps the ambient color. |
| `bool psxe_renderer_add_light(psxe_renderer *renderer, const psxe_light *light)` | Copies one light into the set. `false` for `NULL`, or when full (also increments `lights_dropped`). |
| `uint16_t psxe_renderer_set_lights(psxe_renderer *renderer, const psxe_light *lights, uint16_t count)` | Replaces the stored lights and preserves ambient. Returns the stored count and counts entries beyond capacity as dropped. A `NULL` array clears the set. |
| `psxe_light *psxe_renderer_light(psxe_renderer *renderer, uint16_t index)` | Mutable access to stored light `index` (for animation), or `NULL`. |
| `void psxe_renderer_set_ambient(psxe_renderer *renderer, psxe_color ambient)` | Sets the set-wide ambient color. |
| `void psxe_draw_mesh_lights(psxe_renderer *renderer, const psxe_mesh *mesh, const psxe_transform *transform, const psxe_material *material, const psxe_light *extra_lights, uint16_t extra_count)` | `psxe_draw_mesh_material` that also ranks `extra` world-space lights next to the renderer's set. `extra` is borrowed for this call only. `psxe_scene_render` uses it for entity lights. |
| `void psxe_draw_light_marker(psxe_renderer *renderer, const psxe_light *light, int16_t size)` | Debug: a solid `size`×`size` square in the light's color at an enabled point light's projected position, in bucket 1 (above meshes). Projected on the CPU with the current camera; skipped behind the camera. Costs one `TILE`. |

The light set is copied by value. Render-target passes save it at
`psxe_render_target_begin` and restore it at `end`, so a pass can install its
own lights. `lights_considered`, `lights_applied`, and `lights_folded` sum over
mesh draws. `lights_dropped` tracks additions rejected by the renderer's set.
Reacquire borrowed pointers from `psxe_renderer_light` after replacing the set.

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
| `void psxe_object_init(psxe_object *object, const psxe_mesh *mesh)` | Identity transform, default material, `visible = true`. |
| `void psxe_object_set_mesh(psxe_object *object, const psxe_mesh *mesh)` | Sets the borrowed mesh and preserves transform and material. Pass `NULL` to detach it. |
| `void psxe_object_set_texture(psxe_object *object, const psxe_texture *texture)` | Changes the texture of an existing object; `NULL` returns to `mesh->texture`. |
| `void psxe_object_set_color(psxe_object *object, psxe_color color, psxe_color_mode mode)` | See color modes above. |
| `void psxe_object_set_effects(psxe_object *object, uint8_t effects, psxe_blend_mode blend)` | Replaces effect bits and blend mode. |
| `void psxe_object_set_position(psxe_object *object, int32_t x, int32_t y, int32_t z)` | World units. |
| `void psxe_object_translate(psxe_object *object, int32_t dx, int32_t dy, int32_t dz)` | Adds to the position. |
| `void psxe_object_set_rotation(psxe_object *object, int16_t x, int16_t y, int16_t z)` | Assigns the three rotation components. `4096` is one full turn. |
| `void psxe_object_rotate(psxe_object *object, int16_t dx, int16_t dy, int16_t dz)` | Adds and wraps to `0..4095`. |
| `void psxe_object_set_scale(psxe_object *object, int32_t scale)` | Uniform scale, `ONE` = 1.0. Set `transform.scale` directly for non-uniform scale. |
| `void psxe_object_draw(psxe_renderer *renderer, const psxe_object *object)` | Draws if `visible` (via `psxe_draw_mesh_material`). |

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

`material.color` tints textured sprites (`{128,128,128}` is neutral) and fills
solid sprites. Sprite drawing ignores `color_mode` and `PSXE_EFFECT_UNLIT`.
Keep atlas regions inside the source texture and its 256 × 256 texel page.
The unscaled `SPRT` path stores its base coordinates as 8-bit values. The quad
path clamps its UV endpoints and uses mirrored endpoints for flips. Layer
values beyond the active ordering table select its last bucket.

| Function | Behavior |
| --- | --- |
| `void psxe_sprite_init(psxe_sprite *sprite, const psxe_texture *texture)` | Creates a visible sprite at `(0, 0)` and bucket 1. A valid texture sets the size from its region, clamped to 256 per axis. `NULL` creates a 16 × 16 solid sprite. A non-`NULL`, invalid texture starts at zero size. |
| `void psxe_sprite_set_texture(psxe_sprite *sprite, const psxe_texture *texture)` | Resets the source region. A valid texture supplies the on-screen size if either dimension is non-positive. An absent or invalid texture selects the solid path using the existing size. |
| `void psxe_sprite_set_region(psxe_sprite *sprite, uint8_t u, uint8_t v, uint16_t width, uint16_t height)` | Selects an atlas cell or animation frame. Does not change the on-screen size. |
| `void psxe_sprite_set_position(psxe_sprite *sprite, int16_t x, int16_t y)` | Sets the top-left screen position in pixels. |
| `void psxe_sprite_move(psxe_sprite *sprite, int16_t dx, int16_t dy)` | Adds pixel offsets to the current position. |
| `void psxe_sprite_set_size(psxe_sprite *sprite, int16_t width, int16_t height)` | On-screen size; differs from the region → scaled. |
| `void psxe_sprite_set_color(psxe_sprite *sprite, psxe_color color)` | Tint or fill color. |
| `void psxe_sprite_set_effects(psxe_sprite *sprite, uint8_t effects, psxe_blend_mode blend)` | For example `PSXE_EFFECT_FLIP_X \| PSXE_EFFECT_SEMITRANSPARENT`. |
| `void psxe_sprite_set_layer(psxe_sprite *sprite, uint16_t layer)` | 0 = with text, 1 = UI (default), larger = among/behind meshes. |
| `void psxe_sprite_draw(psxe_renderer *renderer, const psxe_sprite *sprite)` | Draws if visible and the size is positive. |

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
| `bool psxe_render_target_init(psxe_render_target *target, const psxe_renderer *renderer, const psxe_render_target_config *config)` | Validates the configuration and fills `target` with a texture view. Returns `false` and invalidates the destination on a bad configuration. |
| `bool psxe_render_target_begin(psxe_renderer *renderer, const psxe_render_target *target)` | Starts a pass. From now until `end`, every `psxe_draw_*`, `psxe_object_draw`, `psxe_sprite_draw`, `psxe_scene_render`, and `psxe_ui_*` call goes into the target, with the GTE screen center at the target's center. Returns `false` if a pass is already open, the target is invalid, or the packet arena cannot hold the pass's ordering table and two state packets (counted in `dropped_primitives`). Increments `target_passes`. |
| `void psxe_render_target_end(psxe_renderer *renderer)` | Returns drawing to the main ordering table and restores the camera, projection, screen center, depth shift, and light set. A call without an open pass has no effect. `psxe_renderer_end_frame` also closes an open pass. |
| `const psxe_texture *psxe_render_target_texture(const psxe_render_target *target)` | Returns the 16-bit texture view with `clut = 0`, or `NULL` if invalid. The view borrows the initialized target and samples the last executed pass. |

The rectangle must fit in 1024 × 512 VRAM and inside one 256 × 256 texture
page. For a 16-bit target, `(x & 63) + width` and `(y & 255) + height` must
each be at most 256. X and width must be 16-pixel aligned. Width is `16..256`,
height is `1..256`, `ot_length` is `3..PSXE_OT_LENGTH`, and `depth_shift` is
`0..15`. Initialization checks overlap with the two framebuffers and debug
font. Account for other render targets and textures when planning reservations.

- **Order.** Passes execute before the main pass, in the order they began, so
  the main pass and any later pass can sample an earlier target. Never sample a
  target inside its own pass.
- **Budget.** A pass takes `ot_length * sizeof(uint32_t) + 2 * sizeof(DR_ENV)` bytes from the current
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
- **VRAM.** The target occupies one persistent VRAM rectangle. Reserve it
  with `psxe_vram_reserve` before managed uploads. The renderer waits for the
  previous display list before submitting the next one.

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

A handle resolves while its slot is active and the generations match.
Destroying an entity increments its 16-bit generation. Keep the handle between
frames and resolve it when needed. Discard handles when their entity dies.
`PSXE_SCENE_NO_LIGHT` is the internal `light_slot` sentinel, distinct from
`UINT16_MAX`, the invalid entity index. `psxe_scene_init` starts a new pool
lifetime. Discard any prior handles and borrowed pointers before reusing it.

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
| `uint16_t psxe_scene_gather_lights(const psxe_scene *scene, psxe_light *out, uint16_t capacity)` | Writes up to `capacity` world-space lights from active entities with enabled lights, regardless of mesh visibility. Returns the number written. Pass this array and count to `psxe_draw_mesh_lights` for objects outside the scene. |

```c
psxe_entity_id id = psxe_scene_create(&scene);
psxe_entity *e = psxe_scene_get(&scene, id);
if (e != NULL) {
    e->mesh = &cube_mesh;
    e->transform.position.vz = 900;
    psxe_material_set_texture(&e->material, &crate_tex);   /* per-entity texture */
    psxe_light bulb;
    psxe_light_init(&bulb, PSXE_LIGHT_POINT);
    psxe_light_set_position(&bulb, 0, -100, 0);
    psxe_light_set_range(&bulb, 600, PSXE_FALLOFF_LINEAR);
    if (!psxe_scene_set_light(&scene, id, &bulb)) {
        /* Leave the entity visible without its local light. */
    }
}

/* Later, during unload. This also frees the attached light. */
psxe_scene_destroy(&scene, id);
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

`psxe_input` owns the two snapshots and the per-port motor requests. Initialize
it once and treat snapshots returned by `psxe_input_pad` as read-only. The next
poll updates those snapshots in place.

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
| `const psxe_pad_state *psxe_input_pad(const psxe_input *input, unsigned int port)` | Returns the snapshot for port 0 or 1, including disconnected pads. Returns `NULL` for an invalid port or context. |
| `bool psxe_button_held(const psxe_pad_state *pad, PadButton button)` | Button down this frame. |
| `bool psxe_button_pressed(const psxe_pad_state *pad, PadButton button)` | Went down this frame. |
| `bool psxe_button_released(const psxe_pad_state *pad, PadButton button)` | Went up this frame. |
| `bool psxe_input_rumble(psxe_input *input, unsigned int port, bool small, uint8_t large, uint8_t frames)` | Requests small on/off and large intensity 0–255 for the next `frames` polls. Pass zero frames to stop on the next poll. Returns false when the port has no negotiated rumble. |
| `void psxe_input_stop_rumble(psxe_input *input)` | Clears both requests and immediately sends zero motor levels to configured pads. Called by the engine on exit. |

The button helpers return `false` for a `NULL` pad. They check for any matching
bit in the relevant mask. Use an explicit mask comparison for a chord that
requires all its buttons.

```c
const psxe_pad_state *pad = psxe_input_pad(&engine->input, 0);
if (psxe_button_pressed(pad, PAD_CROSS)) {
    jump();
    /* A 100 ms pulse at either regional frame rate. */
    psxe_input_rumble(&engine->input, 0, true, 80,
                      (uint8_t)(engine->ticks_per_second / 10u));
}
if (psxe_button_held(pad, PAD_SQUARE)) { charge++; }
if (psxe_button_released(pad, PAD_SQUARE)) { fire(charge); charge = 0; }
const uint16_t chord = PAD_L1 | PAD_R1;
if (pad != NULL && (pad->held & chord) == chord) { reset_camera(); }
```

## CD-ROM — `cd.h`

Call from the game loop or a loading callback with interrupts enabled. The CD
service owns the single drive and its PSn00bSDK callbacks. Audio playback and
data reads share the transport. Pause or stop audio before starting a new data
read. An LBA of zero is MSF `00:02:00`. Each second contains 75 sectors.

| Type or constant | Meaning |
| --- | --- |
| `psxe_cd_file` | `lba` is the starting sector of a contiguous file and `size` is its logical length in bytes. Resolve it again after a disc change. |
| `psxe_cd_drive_status` | `lid_open`, `motor_on`, `seeking`, `reading` for data or XA, and `playing` for CD-DA. |
| `psxe_cd_sector_mode` | `PSXE_CD_DATA` uses `PSXE_CD_SECTOR_SIZE` (2048 bytes). `PSXE_CD_RAW` uses `PSXE_CD_RAW_SECTOR_SIZE` (2340 bytes, without the 12-byte sync field). |

| `psxe_cd_result` | Meaning |
| --- | --- |
| `PSXE_CD_OK` | The call completed successfully. For playback start, the command was accepted and audio continues independently. |
| `PSXE_CD_PENDING` | A sector transfer owns the caller's buffer. Continue polling or cancel it. |
| `PSXE_CD_BUSY` | The drive is serving a transfer, playback, or recovery that conflicts with the requested operation. |
| `PSXE_CD_INVALID_ARGUMENT` | A pointer, range, mode, path, alignment, or buffer size failed validation. |
| `PSXE_CD_NOT_READY` | The drive service has yet to initialize successfully. |
| `PSXE_CD_NOT_FOUND` | File lookup found no matching entry. |
| `PSXE_CD_BAD_FILESYSTEM` | ISO9660 metadata or a requested file layout failed validation. |
| `PSXE_CD_IO_ERROR` | A CD command, transfer, or transport recovery failed or timed out. |

### Drive and transport

| Function | Behavior |
| --- | --- |
| `psxe_cd_result psxe_cd_init(void)` | Initializes the drive after renderer/IRQ setup and retries a pending SDK initialization with the engine's frame-based timeout. Subsequent calls return `PSXE_CD_OK` once initialized. |
| `psxe_cd_result psxe_cd_status(psxe_cd_drive_status *status)` | Writes the drive state. Can be queried during audio playback. Returns `PSXE_CD_BUSY` during a sector transfer. |
| `psxe_cd_result psxe_cd_seek(uint32_t lba, bool audio_track)` | Waits for a seek on an idle drive. `audio_track` chooses a physical subchannel seek. Use `false` for a data-header seek. |
| `psxe_cd_result psxe_cd_position(uint32_t *lba)` | Writes the current physical head position from subchannel Q. Returns `PSXE_CD_BUSY` during a sector transfer. |
| `psxe_cd_result psxe_cd_motor_on(void)` | Spins up an idle drive. Returns success immediately if its spindle is already running. |
| `psxe_cd_result psxe_cd_pause(void)` | Pauses the transport with the spindle running. Complete or cancel an active sector transfer first. |
| `psxe_cd_result psxe_cd_stop(void)` | Stops transport and spindle. Can recover a failed transport after any active transfer has been finalized. |

### Sector reads and file lookup

| Function | Behavior |
| --- | --- |
| `psxe_cd_result psxe_cd_read_start(uint32_t lba, uint32_t sectors, psxe_cd_sector_mode mode, void *destination, size_t capacity)` | Starts an IRQ-driven read into 4-byte-aligned RAM. Supply a nonzero sector count and capacity for the entire transfer at the selected stride. Returns `PSXE_CD_PENDING` once it begins. |
| `psxe_cd_result psxe_cd_read_poll(void)` | Checks progress once. A terminal poll may wait for the drive to pause or stop. Call from the game loop while the buffer is borrowed. |
| `psxe_cd_result psxe_cd_read_wait(void)` | Polls until the active read finishes. Keeps the caller blocked while the transfer progresses. |
| `psxe_cd_result psxe_cd_read_cancel(void)` | Disarms DMA and pauses the drive, or stops the spindle if it cannot pause. Returns `PSXE_CD_INVALID_ARGUMENT` when there is no active sector read. |
| `uint32_t psxe_cd_sectors_read(void)` | Returns the running count of successful data and raw DMA sector transfers since boot, including filesystem reads and sectors transferred before cancellation. The unsigned counter wraps after `UINT32_MAX`. |
| `psxe_cd_result psxe_cd_find(const char *path, psxe_cd_file *file)` | Resolves a contiguous file on the primary ISO9660 volume. Use slash or backslash, case-insensitive path components, and an optional `;1` suffix. Writes the output on success. |
| `psxe_cd_result psxe_cd_read_file(const psxe_cd_file *file, uint32_t offset, void *destination, size_t size)` | Reads an exact byte range into caller-owned RAM. Uses scratch storage for partial sectors or unaligned output. Returns an error if the range exceeds `file->size`. An I/O error can leave a partial result in `destination`. |

Keep the DMA buffer alive through a terminal poll, wait, or cancellation. Use
its contents only if poll or wait reported `PSXE_CD_OK`. After a transport
failure, `psxe_cd_stop` can recover the drive before reuse. For a full file,
prefer `psxe_cd_read_file` when its logical byte size matters.

This loading helper reads the first bytes of the bundled chime. Call it after
`psxe_engine_init` and pass a buffer of at least 48 bytes.

```c
#include <psxe/cd.h>

static psxe_cd_result read_chime_header(uint8_t header[48]) {
    psxe_cd_file file;
    psxe_cd_result result = psxe_cd_init();
    if (result == PSXE_CD_OK) {
        result = psxe_cd_find("/ASSETS/CHIME.VAG", &file);
    }
    if (result == PSXE_CD_OK) {
        result = psxe_cd_read_file(&file, 0, header, 48);
    }
    return result;
}
```

For a sector-aligned prefetch, keep the job in static game state. Call
`prefetch_begin` once, poll during `update`, and consume `words`
after success. Use an LBA from a resolved file extent or disc layout.

```c
#include <psxe/cd.h>

typedef struct sector_prefetch {
    uint32_t words[PSXE_CD_SECTOR_SIZE / sizeof(uint32_t)];
    psxe_cd_result result;
} sector_prefetch;

static psxe_cd_result prefetch_begin(sector_prefetch *job, uint32_t lba) {
    if (job->result == PSXE_CD_PENDING) {
        return PSXE_CD_BUSY;
    }
    job->result = psxe_cd_read_start(lba, 1, PSXE_CD_DATA, job->words, sizeof(job->words));
    return job->result;
}

static psxe_cd_result prefetch_poll(sector_prefetch *job) {
    if (job->result == PSXE_CD_PENDING) {
        job->result = psxe_cd_read_poll();
    }
    return job->result;
}

static psxe_cd_result prefetch_cancel(sector_prefetch *job) {
    if (job->result != PSXE_CD_PENDING) {
        return PSXE_CD_INVALID_ARGUMENT;
    }
    psxe_cd_result result = psxe_cd_read_cancel();
    job->result = PSXE_CD_INVALID_ARGUMENT;
    return result;
}
```

Initialize `result` to `PSXE_CD_INVALID_ARGUMENT` before the first request, as
`PSXE_CD_OK` is zero. A sector read can include padding beyond the logical end
of a file. Use `psxe_cd_file.size` when interpreting that sector.

### XA and CD-DA

| Function | Behavior |
| --- | --- |
| `psxe_cd_result psxe_cd_play_xa(uint32_t lba, uint8_t file, uint8_t channel, bool double_speed)` | Starts XA decoding on an idle drive with the given file and channel filter. Select the speed to match the authored stream. The caller manages playback boundaries. |
| `psxe_cd_result psxe_cd_xa_filter(uint8_t file, uint8_t channel)` | Changes the filter during XA playback. The accepted channel range is `0..31`. |
| `psxe_cd_result psxe_cd_play_cdda(uint32_t lba)` | Starts CD-DA at an audio track LBA and enables auto-pause at track end. Query drive status to observe completion before the next operation. |
| `psxe_cd_result psxe_cd_set_audio_volume(uint16_t left, uint16_t right)` | Sets SPU CD-input volume per channel in the range `0..0x7fff`. Values outside the range return `PSXE_CD_INVALID_ARGUMENT`. |
| `psxe_cd_result psxe_cd_set_muted(bool muted)` | Mutes or demutes drive audio without pausing the transport. Returns `PSXE_CD_BUSY` during a sector transfer. |

Set the volume, then start playback at the audio track's authored LBA.
Check the result before displaying a playing state. [CD-ROM command reference](https://github.com/psx-spx/psx-spx.github.io/blob/master/docs/cdromdrive.md)

```c
/* Call after psxe_cd_init and with the transport idle. */
static psxe_cd_result start_music(uint32_t audio_track_lba) {
    psxe_cd_result result = psxe_cd_set_audio_volume(0x5000, 0x5000);
    return result == PSXE_CD_OK ? psxe_cd_play_cdda(audio_track_lba) : result;
}
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

`psxe_audio` owns the SPU allocation cursor and next voice. Keep one initialized
context for the hardware. Discard old sample descriptors if you reinitialize
that context. Playback copies the descriptor's parameters to the voice
registers, and the source VAG buffer can be released once upload succeeds.

| Function | Behavior |
| --- | --- |
| `void psxe_audio_init(psxe_audio *audio)` | Initializes the SPU, sets master volume, resets the SPU RAM allocator to `0x1010`. Called by `psxe_engine_init`. |
| `bool psxe_audio_upload_vag(psxe_audio *audio, psxe_audio_sample *sample, const void *vag_data, size_t vag_size)` | Validates a mono VAG (`VAGp` magic, big-endian size), DMA-uploads the payload without the 48-byte header to the next 64-byte-aligned SPU address, and fills `sample`. `false` on bad data or when SPU RAM (512 KiB) is full. Space is never reclaimed. |
| `psxe_cd_result psxe_audio_load_vag(psxe_audio *audio, psxe_audio_sample *sample, const psxe_cd_file *file, void *scratch, size_t capacity)` | Reads the VAG into caller-owned scratch of at least `file->size` bytes, then validates it and uploads to SPU RAM. The scratch can be released after the call. |
| `int psxe_audio_play(psxe_audio *audio, const psxe_audio_sample *sample, uint16_t volume, int16_t pan)` | Plays on the next voice (round robin over 24) and returns it, or `-1`. `volume` 0..`0x3fff`; `pan` `-0x1000` (left) .. `0` (center) .. `0x1000` (right); both are clamped. A busy voice is cut off. |
| `void psxe_audio_stop(int voice)` | Keys off one voice; ignores out-of-range values (including `-1`). |
| `void psxe_audio_stop_all(void)` | Keys off all 24 voices. Called by the engine after the loop exits. |
| `psxe_audio_stats psxe_audio_get_stats(const psxe_audio *audio)` | Returns `used_bytes` and `free_bytes` in the aligned sample region `0x1040..0x7ffff`. Includes DMA padding. Returns zeros for a null or uninitialized context. |

Uploads block on DMA. Load during `init` or a loading transition. VAG loop
flags control looping. Playback rotates through all 24 hardware voices and
can replace an active one. Call `load_chime` during initialization and
`play_chime` on a button edge.

```c
#include <psxe/psxe.h>

static psxe_audio_sample chime;
static uint32_t chime_staging[512];

static psxe_cd_result load_chime(psxe_engine *engine) {
    psxe_cd_file file;
    psxe_cd_result result = psxe_cd_init();
    if (result == PSXE_CD_OK) {
        result = psxe_cd_find("/ASSETS/CHIME.VAG", &file);
    }
    if (result == PSXE_CD_OK) {
        result = psxe_audio_load_vag(&engine->audio, &chime, &file,
                                     chime_staging, sizeof(chime_staging));
    }
    return result;
}

static int play_chime(psxe_engine *engine) {
    return psxe_audio_play(&engine->audio, &chime, 0x2800, 0);
}
```

For an embedded VAG, `psn00bsdk_target_incbin` supplies the data and length as
separate symbols. The size symbol contains the byte count.

```c
extern const uint8_t jump_vag[];
extern const uint32_t jump_vag_size;

static bool upload_jump(psxe_audio *audio, psxe_audio_sample *sample) {
    return psxe_audio_upload_vag(audio, sample, jump_vag, jump_vag_size);
}
```

## Memory card — `memory_card.h`

```c
typedef struct psxe_memory_card_config {
    const char *product_code;  /* e.g. "BAPXE-00001" */
    const char *identifier;    /* e.g. "CUBE"; product_code + identifier <= 20 chars */
    const char *title;         /* shown by the BIOS manager, truncated to 63 chars */
    uint16_t format_version;   /* bump whenever the payload layout changes */
} psxe_memory_card_config;

typedef struct psxe_memory_card_file {
    char name[21];
    uint32_t size;
} psxe_memory_card_file;
```

Results: `PSXE_MEMORY_CARD_OK`, `PSXE_MEMORY_CARD_INVALID_ARGUMENT`,
`PSXE_MEMORY_CARD_NOT_FOUND`, `PSXE_MEMORY_CARD_IO_ERROR`,
`PSXE_MEMORY_CARD_CORRUPT`, `PSXE_MEMORY_CARD_VERSION_MISMATCH`,
`PSXE_MEMORY_CARD_NO_CARD`, `PSXE_MEMORY_CARD_UNFORMATTED`,
and `PSXE_MEMORY_CARD_FULL`.

The file name is `bu00:` (slot 0 = physical slot 1) or `bu10:` (slot 1)
followed by `product_code` and `identifier`. Both strings must be nonempty and
their combined length is limited to 20 characters. The characters `*`, `/`,
`?`, `:`, and `\` are rejected. Each identity is one 8192-byte
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

Card calls block. Perform them on a user action or at a checkpoint and keep
the result for rendering on subsequent frames. The engine stores your bytes
as supplied. Use an explicit payload layout and increment `format_version`
when it changes.

```c
#include <psxe/memory_card.h>

typedef struct checkpoint {
    uint32_t level;
    uint32_t score;
} checkpoint;

static psxe_memory_card_result save_checkpoint(const psxe_memory_card_config *config,
                                                const checkpoint *current) {
    return psxe_memory_card_save(config, 0, current, sizeof(*current));
}

static psxe_memory_card_result load_checkpoint(const psxe_memory_card_config *config,
                                                checkpoint *current) {
    checkpoint loaded;
    size_t length = 0;
    psxe_memory_card_result result = psxe_memory_card_load(config, 0, &loaded,
                                                           sizeof(loaded), &length);
    if (result != PSXE_MEMORY_CARD_OK) {
        return result;
    }
    if (length != sizeof(loaded)) {
        return PSXE_MEMORY_CARD_CORRUPT;
    }
    *current = loaded;
    return PSXE_MEMORY_CARD_OK;
}
```

Display `psxe_memory_card_result_string(result)` in your HUD. For a free-space
indicator, query directory totals with a zero-capacity list.

```c
unsigned int free_blocks = 0;
psxe_memory_card_result result = psxe_memory_card_list(0, NULL, 0, NULL, &free_blocks);
```

## Asset memory — `memory.h` and `vram.h`

Use a caller-owned heap for staging or storing level assets and one managed
VRAM pool per renderer. Initialize them before loading. Keep heap operations,
managed VRAM transfers, and resource teardown outside frame construction.

| Type or constant | Meaning |
| --- | --- |
| `psxe_heap`, `psxe_heap_block` | Heap handle and opaque block metadata stored within the supplied RAM. |
| `psxe_heap_stats` | `used_bytes`, `free_bytes`, and `largest_free` describe payload capacity, excluding headers. |
| `psxe_mesh_storage` | Owns one allocation with the `mesh` view and writable `vertices`, `normals`, and `faces`. The mesh texture pointer is borrowed independently. |
| `psxe_vram`, `psxe_vram_slot` | Renderer-bound pool and its internal allocations. The pool tracks occupied tiles and owns its slot views. |
| `psxe_vram_id` | Slot `index` plus allocation `generation`. IDs are scoped to the pool lifetime. |
| `psxe_vram_stats` | Counts `used_tiles` and `free_tiles` in units of 16 halfwords × 8 rows. |
| `PSXE_VRAM_MAX_TEXTURES` | 32 managed texture slots per pool. |

### Main RAM and mesh storage

| Function | Behavior |
| --- | --- |
| `bool psxe_heap_init(psxe_heap *heap, void *memory, size_t size)` | Initializes metadata inside the supplied byte span. Returns `false` if space is insufficient for aligned metadata and a minimal allocation. Reinitialization invalidates earlier allocations. |
| `void *psxe_heap_alloc(psxe_heap *heap, size_t size)` | Returns an 8-byte-aligned block using first fit. Returns `NULL` on zero size, invalid input, or exhaustion. |
| `bool psxe_heap_free(psxe_heap *heap, void *memory)` | Releases a live allocation from this heap and coalesces adjacent free blocks. Returns `false` for an invalid pointer or a double free. |
| `psxe_heap_stats psxe_heap_get_stats(const psxe_heap *heap)` | Reports used, free, and largest contiguous free payload bytes. Read `largest_free` when sizing a single allocation. |
| `psxe_mesh_storage *psxe_mesh_alloc(psxe_heap *heap, uint16_t vertex_count, uint16_t normal_count, uint16_t face_count)` | Allocates the mesh view and writable arrays together. Vertex and face counts must be positive. The normal count may be zero. Fill referenced entries before drawing. Returns `NULL` on invalid size or insufficient memory. |
| `bool psxe_mesh_free(psxe_heap *heap, psxe_mesh_storage *storage)` | Releases the mesh and its arrays as a unit after every instance detaches. Returns `false` when the allocation is invalid. |

Allocate from static game-owned storage between frames. This example creates
one unlit triangle, exposes it through an object, and detaches the object before
freeing the mesh. Call `build_triangle` with fresh state and call
`psxe_object_draw` during `render` while the mesh is live.

```c
#include <psxe/psxe.h>

static uint64_t triangle_arena[128];
static psxe_heap triangle_heap;
static psxe_mesh_storage *triangle_storage;
static psxe_object triangle_object;

static bool build_triangle(void) {
    if (!psxe_heap_init(&triangle_heap, triangle_arena, sizeof(triangle_arena))) {
        return false;
    }
    triangle_storage = psxe_mesh_alloc(&triangle_heap, 3, 0, 1);
    if (triangle_storage == NULL) {
        return false;
    }
    triangle_storage->vertices[0] = (SVECTOR){ -48, 48, 0, 0 };
    triangle_storage->vertices[1] = (SVECTOR){ 0, -48, 0, 0 };
    triangle_storage->vertices[2] = (SVECTOR){ 48, 48, 0, 0 };
    triangle_storage->faces[0] = (psxe_face){
        .vertex = { 0, 1, 2 }, .color = { 230, 120, 48 },
        .flags = PSXE_MATERIAL_DOUBLE_SIDED
    };
    psxe_object_init(&triangle_object, &triangle_storage->mesh);
    psxe_object_set_position(&triangle_object, 0, 0, 400);
    return true;
}

static bool release_triangle(void) {
    if (triangle_storage == NULL) {
        return true;
    }
    psxe_object_set_mesh(&triangle_object, NULL);
    if (!psxe_mesh_free(&triangle_heap, triangle_storage)) {
        return false;
    }
    triangle_storage = NULL;
    return true;
}
```

### Managed texture storage

| Function | Behavior |
| --- | --- |
| `void psxe_vram_init(psxe_vram *vram, const psxe_renderer *renderer)` | Clears the pool and reserves both framebuffers and the debug font. Pass an initialized renderer and initialize the pool before creating texture IDs. |
| `bool psxe_vram_reserve(psxe_vram *vram, int x, int y, int width, int height)` | Reserves a rectangle until pool reinitialization. X and width use VRAM halfwords. Y and height use rows. Rectangles round outward to tiles, and overlap with an occupied tile fails. |
| `bool psxe_vram_load_tim(psxe_vram *vram, const void *data, size_t size, psxe_vram_id *id)` | Validates a sized, word-aligned 4/8/16-bit TIM and writes an ID on success. Relocates pixels inside a texture page and allocates a CLUT as needed. Indexed CLUTs use rows of 16 or 256 colors. Source coordinates are ignored. |
| `const psxe_texture *psxe_vram_get(const psxe_vram *vram, psxe_vram_id id)` | Returns a borrowed texture view or `NULL` for a stale or invalid ID. |
| `bool psxe_vram_palette(const psxe_vram *vram, psxe_vram_id id, unsigned int palette, psxe_texture *view)` | Writes a texture view using a zero-based CLUT row and the same pixels. Returns `false` for invalid IDs or unavailable rows. |
| `bool psxe_vram_free(psxe_vram *vram, psxe_vram_id id)` | Waits for submitted GPU work, invalidates the ID, and recycles the tiles. Returns `false` during frame construction, for an invalid ID, or when GPU synchronization fails. |
| `psxe_vram_stats psxe_vram_get_stats(const psxe_vram *vram)` | Counts `used_tiles` and `free_tiles` in the 64 × 64 pool. Used tiles include buffers, the font, reservations, pixels, and CLUTs. Free tiles may be fragmented or cross texture-page boundaries. Returns zeros for a null or uninitialized pool. |

Reserve fixed render targets and manually placed textures before managed
uploads. This helper uses the heap for staging and releases the memory on
either outcome. Call it with an initialized CD service, heap, and VRAM pool
between frame builds. A successful ID remains valid after staging is released.

```c
#include <psxe/psxe.h>

static bool load_disc_tim(psxe_heap *heap, psxe_vram *vram, const char *path,
                          psxe_vram_id *id) {
    psxe_cd_file file;
    if (psxe_cd_find(path, &file) != PSXE_CD_OK) {
        return false;
    }
    void *staging = psxe_heap_alloc(heap, file.size);
    if (staging == NULL) {
        return false;
    }
    bool loaded = psxe_cd_read_file(&file, 0, staging, file.size) == PSXE_CD_OK &&
                  psxe_vram_load_tim(vram, staging, file.size, id);
    psxe_heap_free(heap, staging);
    return loaded;
}
```

Every borrowed texture pointer and copied palette view depends on the
allocation. Detach all borrowers before `psxe_vram_free`. If the free fails,
keep the ID for a later retry. Give palette views persistent game-owned
storage whenever a material retains a pointer to them.

```c
static psxe_texture evening;
if (psxe_vram_palette(&vram, wall_texture, 1, &evening)) {
    /* Keep evening alive for as long as the material borrows it. */
    psxe_material_set_texture(&wall_material, &evening);
    psxe_material_set_effects(&wall_material,
        PSXE_EFFECT_RAW_TEXTURE | PSXE_EFFECT_UNLIT, PSXE_BLEND_HALF);
}
```

## Combining APIs: advanced recipes

The recipes show how to compose the public APIs in game callbacks. Names such
as `my_game`, `item_mesh`, and `simulate_world` stand for game-owned types,
assets, and routines. Snippets labeled `init`, `update`, or `render` belong in
the corresponding callback.

### 1. Region-correct timers

`update` runs at 50 or 60 Hz. Derive durations from `ticks_per_second` so a PAL
build lasts as long in real time as an NTSC one.

```c
static uint32_t tenths_to_frames(const psxe_engine *engine, uint16_t tenths) {
    return ((uint32_t)engine->ticks_per_second * tenths) / 10u;
}

/* in init:   game->spawn_timer = tenths_to_frames(engine, 15);  -> 1.5 s */
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

/* World X of roughly -160..160 -> SPU pan -0x1000..0x1000. */
static int16_t pan_from_x(int32_t x) {
    if (x <= -164) { return -0x1000; }
    if (x >= 164)  { return 0x1000; }
    return (int16_t)(x * 25);
}

static void fire(psxe_engine *engine, shooter_game *g) {
    const psxe_entity *ship = psxe_scene_get_const(&g->scene, g->ship);
    if (ship == NULL) {
        return;
    }

    for (unsigned int i = 0; i < MAX_SHOTS; ++i) {
        if (!g->shots[i].used) {
            psxe_entity_id id = psxe_scene_create(&g->scene);
            psxe_entity *e = psxe_scene_get(&g->scene, id);
            if (e == NULL) {
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

static void update_shots(psxe_engine *engine, shooter_game *g) {
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
        e->transform.position.vz += 1200 / (int32_t)engine->ticks_per_second;
    }
}

static void shooter_update(psxe_engine *engine, void *user_data) {
    shooter_game *g = user_data;
    const psxe_pad_state *pad = psxe_input_pad(&engine->input, 0);
    update_shots(engine, g);
    if (psxe_button_pressed(pad, PAD_CROSS)) {
        fire(engine, g);
    }
}
```

Clamp world coordinates before multiplying and narrowing to `int16_t`.
`psxe_audio_play` receives the narrowed value. The shot moves 1200 units per
second and lives for one second at either regional frame rate.

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
    save_v2 candidate;
    psxe_memory_card_result r = psxe_memory_card_load(&engine->memory_card, 0,
                                                      &candidate, sizeof(candidate), &size);

    if (r == PSXE_MEMORY_CARD_OK) {
        if (size != sizeof(candidate)) {
            return PSXE_MEMORY_CARD_CORRUPT;
        }
        *out = candidate;
        return PSXE_MEMORY_CARD_OK;
    }
    if (r != PSXE_MEMORY_CARD_VERSION_MISMATCH) {
        return r;
    }

    psxe_memory_card_config old = engine->memory_card;
    save_v1 v1;
    old.format_version = 1;
    r = psxe_memory_card_load(&old, 0, &v1, sizeof(v1), &size);
    if (r != PSXE_MEMORY_CARD_OK) {
        return r;
    }
    if (size != sizeof(v1)) {
        return PSXE_MEMORY_CARD_CORRUPT;
    }
    *out = (save_v2){ .level = v1.level, .score = v1.score, .lives = 3 };
    return PSXE_MEMORY_CARD_OK;
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
static psxe_transform quad_transform;
static const SVECTOR quad_vertices[] = {
    { -64, -64, 0, 0 }, { 64, -64, 0, 0 },
    { -64,  64, 0, 0 }, { 64,  64, 0, 0 }
};

/* UVs are offsets from the texture's base u/v (0..width-1, 0..height-1). */
static const psxe_face quad_faces[] = {
    { { 0, 1, 2 }, 0, { { 0, 0 }, { 63, 0 }, { 0, 63 } },  { 128, 128, 128 },
      PSXE_MATERIAL_TEXTURED | PSXE_MATERIAL_DOUBLE_SIDED },
    { { 1, 3, 2 }, 0, { { 63, 0 }, { 63, 63 }, { 0, 63 } }, { 128, 128, 128 },
      PSXE_MATERIAL_TEXTURED | PSXE_MATERIAL_DOUBLE_SIDED }
};
static const psxe_mesh quad = {
    .vertices = quad_vertices, .faces = quad_faces,
    .vertex_count = PSXE_ARRAY_COUNT(quad_vertices),
    .face_count = PSXE_ARRAY_COUNT(quad_faces), .texture = &atlas
};

static void tex_init(psxe_engine *engine, void *user_data) {
    (void)engine;
    (void)user_data;
    quad_transform = psxe_transform_identity();
    quad_transform.position.vz = 600;
    psxe_texture_load_tim(&atlas, atlas_tim); /* an invalid texture gives flat color */
}

static void tex_render(psxe_engine *engine, void *user_data) {
    (void)user_data;
    psxe_draw_mesh(&engine->renderer, &quad, &quad_transform);
    /* Same VRAM image reused as a HUD icon (top-left 32x32 texels). */
    psxe_draw_sprite(&engine->renderer, &atlas, 8, 8, 32, 32, (psxe_color){ 128, 128, 128 }, false);
}
```

Author a TIM at least 64 × 64 texels in free VRAM, clear of the two
framebuffers, the font at `(960, 0)`, and render targets. The same texture
backs both the double-sided mesh and the HUD icon. Use
`psxe_sprite_set_region` to choose a different atlas cell.

### 10. Driving the subsystems without `psxe_engine_run`

For tools, tests, or a custom loop (for example several update ticks per
rendered frame), call the same functions the engine uses in the same order.
The two application helpers below populate the scene and step the simulation.
Consume input edges only on the first simulation step.

```c
extern void populate_scene(psxe_scene *scene);
extern void simulate(psxe_scene *scene, const psxe_input *input, bool use_button_edges);

int main(void) {
    static psxe_engine engine;
    static psxe_scene scene;
    const psxe_engine_config config = {
        .render = {
            .width = 320, .height = 240, .video_mode = PSXE_VIDEO_NTSC,
            .clear_color = { 20, 28, 48 }, .dither = true
        }
    };

    psxe_engine_init(&engine, &config);        /* still initializes every subsystem */
    psxe_scene_init(&scene);
    populate_scene(&scene);

    for (;;) {
        psxe_input_poll(&engine.input);
        for (unsigned int step = 0; step < 2; ++step) {
            simulate(&scene, &engine.input, step == 0);
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

Replace the face color for an opaque flash. For the death effect, use additive
blending and fade the tint to black. Both effects use unlit, untextured faces.
Store the ordinary material in `e->base_material` when initializing the enemy.

```c
static void enemy_update(enemy *e) {
    psxe_object *o = &e->object;

    if (e->hit_timer > 0) {
        --e->hit_timer;
        psxe_object_set_color(o, (psxe_color){ 255, 255, 255 }, PSXE_COLOR_REPLACE);
        psxe_object_set_effects(o, PSXE_EFFECT_UNLIT | PSXE_EFFECT_UNTEXTURED,
                                PSXE_BLEND_HALF);
    } else if (e->dying > 0) {
        uint8_t level = (uint8_t)(e->dying * 2);   /* start dying at 64 for a factor of 1.0 */
        --e->dying;
        psxe_object_set_color(o, (psxe_color){ level, level, level }, PSXE_COLOR_TINT);
        psxe_object_set_effects(o, PSXE_EFFECT_SEMITRANSPARENT | PSXE_EFFECT_UNLIT |
                                PSXE_EFFECT_UNTEXTURED, PSXE_BLEND_ADD);
        o->visible = (e->dying > 0);
    } else {
        o->material = e->base_material;
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

Use `psxe_sprite_set_size` to fit the map in a smaller HUD rectangle. A
render-target texture uses opaque drawn pixels and transparent zero texels.
For a blended surround, submit a semitransparent panel behind the sprite.

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
