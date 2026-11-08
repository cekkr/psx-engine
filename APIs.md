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
| `psxe_render_config` | `width`, `height`, `video_mode`, `clear_color`, `dither`. |
| `psxe_camera` | `position` (world), `rotation` (Euler), `projection` (GTE screen distance; larger = narrower field of view, default 160). |
| `psxe_texture` | VRAM location filled by `psxe_texture_load_tim`: `tpage`, `clut`, `width`, `height`, base `u`/`v`, `valid`. |
| `psxe_face` | One triangle: `vertex[3]` indices, `normal` index, `uv[3][2]` offsets from the texture's base `u`/`v`, flat `color`, material `flags`. |
| `psxe_mesh` | Borrowed `vertices`, `normals`, `faces` arrays with their counts, plus an optional `texture`. |
| `psxe_renderer` | Double buffers and frame state. Fields worth reading: `submitted_primitives`, `dropped_primitives`, `next_packet`/`packet_end`, `width`, `height`, `camera`. |

### `void psxe_renderer_init(psxe_renderer *renderer, const psxe_render_config *config)`

Resets the GPU, sets the video mode, creates two stacked draw/display buffers
(`0,0` and `0,height` in VRAM), initializes the GTE with the screen center as
geometry offset, loads the debug font at VRAM `(960, 0)`, installs a default
light (direction `{-2048,-2048,-2048}`, color `{220,220,220}`, ambient
`{48,48,48}`) and a default camera at the origin with projection 160, and
enables display. `psxe_engine_init` calls it for you.

### `void psxe_renderer_begin_frame(psxe_renderer *renderer)`

Resets `submitted_primitives` and `dropped_primitives` to zero. Called by the
engine before `render`.

### `void psxe_renderer_end_frame(psxe_renderer *renderer)`

Waits for the GPU (`DrawSync`) and vblank (`VSync`), shows the previous buffer,
submits the active ordering table, swaps buffers, resets the packet pointer,
and clears the next ordering table. Called by the engine after `render`. Never
reuse or modify a packet after this call.

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
position stored in the TIM, waits for the transfer, and fills `texture`.
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

Transforms the mesh by `transform` and the current camera, then for each face:

- drops it (and counts it in `dropped_primitives`) if a vertex index is out of
  range;
- skips back faces unless `PSXE_MATERIAL_DOUBLE_SIDED` is set (match the winding
  of `cube_faces` in [`samples/cube/main.c`](samples/cube/main.c));
- skips faces whose ordering-table index falls outside `[2, PSXE_OT_LENGTH)`.
  There is **no near-plane clipping**: faces too close to or behind the camera
  disappear rather than being cut;
- emits `POLY_FT3` when `TEXTURED` is set and `mesh->texture` is valid,
  otherwise `POLY_F3`;
- with `LIT` and a valid `normal` index, multiplies the face `color` by the
  light. For textured faces, color `{128,128,128}` means unmodified texels.

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

Draws an unscaled screen-space rectangle of `texture` starting at its base
`u`/`v`, at pixel `(x, y)`, in bucket 1 (above meshes, below text). `width` and
`height` select how many texels to show; sprites cannot scale or rotate.
`tint` `{128,128,128}` is neutral. Ignores `NULL`, invalid textures, and
non-positive sizes.

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
  over farther).
- Inside one bucket, the ordering table is last-in-first-drawn. Among panels and
  sprites, **the one you submit first ends up on top**. Submit a background
  panel *after* the sprites that sit on it.
- Meshes are sorted per triangle by average depth (painter's algorithm, no
  Z-buffer). Large or intersecting triangles can sort wrongly; split them.

## Scene — `scene.h`

```c
typedef struct psxe_entity_id { uint16_t index; uint16_t generation; } psxe_entity_id;

typedef struct psxe_entity {
    psxe_transform transform;
    const psxe_mesh *mesh;     /* NULL = not drawn */
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
| `psxe_entity_id psxe_scene_create(psxe_scene *scene)` | Takes the first free slot, sets identity transform, `visible = true`, `mesh = NULL`. When full (or `scene` is `NULL`) returns a handle with `index == UINT16_MAX`; check with `psxe_scene_get`. |
| `bool psxe_scene_destroy(psxe_scene *scene, psxe_entity_id id)` | Frees the slot and invalidates the handle. `false` for stale/invalid handles. |
| `psxe_entity *psxe_scene_get(psxe_scene *scene, psxe_entity_id id)` | Mutable entity, or `NULL` for a stale/invalid handle. |
| `const psxe_entity *psxe_scene_get_const(const psxe_scene *scene, psxe_entity_id id)` | Read-only variant. |
| `void psxe_scene_render(const psxe_scene *scene, psxe_renderer *renderer)` | Calls `psxe_draw_mesh` for each active, visible entity with a mesh, in slot order. |

```c
psxe_entity_id id = psxe_scene_create(&scene);
psxe_entity *e = psxe_scene_get(&scene, id);
if (e != NULL) {
    e->mesh = &cube_mesh;
    e->transform.position.vz = 900;
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
(`0,0` to `width,2*height`) or the debug font at `(960, 0)`. A sprite always
starts at the texture's base `u`/`v`; to show a different atlas cell, load that
cell as its own TIM.

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

## Quick index

| Header | Functions |
| --- | --- |
| `types.h` | `psxe_transform_identity` |
| `engine.h` | `psxe_engine_init`, `psxe_engine_run`, `psxe_engine_stop` |
| `render.h` | `psxe_renderer_init`, `psxe_renderer_begin_frame`, `psxe_renderer_end_frame`, `psxe_renderer_set_camera`, `psxe_renderer_set_directional_light`, `psxe_texture_load_tim`, `psxe_draw_mesh`, `psxe_draw_sprite`, `psxe_ui_panel`, `psxe_ui_text` |
| `scene.h` | `psxe_scene_init`, `psxe_scene_create`, `psxe_scene_destroy`, `psxe_scene_get`, `psxe_scene_get_const`, `psxe_scene_render` |
| `input.h` | `psxe_input_init`, `psxe_input_poll`, `psxe_input_pad`, `psxe_button_held`, `psxe_button_pressed`, `psxe_button_released` |
| `audio.h` | `psxe_audio_init`, `psxe_audio_upload_vag`, `psxe_audio_play`, `psxe_audio_stop`, `psxe_audio_stop_all` |
| `memory_card.h` | `psxe_memory_card_init`, `psxe_memory_card_save`, `psxe_memory_card_load`, `psxe_memory_card_result_string` |

Not available yet (see [`ROADMAP.md`](ROADMAP.md)): near-plane clipping,
Gouraud shading, fog, animation, collision, VRAM/SPU allocators with reclaim,
voice status, streaming audio, asynchronous or listable card operations,
rumble, and multitap.
