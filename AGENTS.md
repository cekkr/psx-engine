# psx-engine — AI Agent Reference

This is the fast-access operational reference for `psx-engine`, an experimental
C99 runtime for games that execute on the original Sony PlayStation. The project
produces MIPS PS-X EXEs and BIN/CUE discs through PSn00bSDK. It is not a desktop
engine, emulator, proprietary Sony SDK replacement, or shader-based PS1 visual
filter.

## Read This First and Sources of Truth

Resolve conflicting claims in this order:

1. [`LICENSE`](LICENSE) governs repository-owned code. Upstream dependencies
   retain their own licenses.
2. Executable contracts in [`CMakeLists.txt`](CMakeLists.txt), public headers in
   [`src/include/psxe/`](src/include/psxe/), implementation, and future focused
   tests govern current behavior.
3. [`README.md`](README.md) governs user-facing setup, scope, and examples.
4. [`ROADMAP.md`](ROADMAP.md) governs planned direction only; it never proves a
   feature exists.
5. This file maps ownership and maintenance rules. Git history supplies context
   but does not override the checked-out revision.

When these sources disagree, inspect and validate the implementation. Fix the
mismatch in the same change or record it as a known gap; never silently promote
roadmap intent to shipped behavior.

## Collaboration and Maintenance Rules

- Read every applicable `AGENTS.md`, inspect `git status`, and preserve unrelated
  changes before editing.
- Put reusable target runtime code under `src/`, public API under
  `src/include/psxe/`, projects under `samples/`, future host asset tools under
  `tools/`, and dependency automation/generated installs under `thirds/`.
- Update this file when ownership, meaningful symbols, commands, interfaces,
  invariants, or status changes. Update README for user-visible changes and
  ROADMAP when future work is added or completed.
- Do not commit generated `build/` or generated `thirds/{build,cache,source,
  toolchain,psn00bsdk}` content. `thirds/versions.env` and setup scripts are the
  source of truth for those trees.
- Run the narrowest relevant compile first, then the full target build and disc
  packaging for cross-subsystem changes. Report emulator and hardware tests
  separately; a successful compiler does not prove console correctness.
- Keep new runtime APIs C99-compatible and freestanding-friendly. Do not add a
  host-only dependency to target code.
- Record measured optimization work with its scenario and hardware/emulator.
  Put unimplemented ideas in ROADMAP, not comments that resemble current APIs.
- Before a commit, review the full diff, run `git diff --check`, validate local
  Markdown links, and ensure status labels describe that exact commit.

## Essential Project Principles

### Original hardware is the semantic target

Use the GPU, Geometry Transformation Engine (GTE), Sound Processing Unit (SPU),
controller ports, memory cards, and CD-ROM as constrained hardware. A future
host previewer must consume target formats and emulate target behavior; it must
not become the source of truth.

### Capacity is explicit and failure is bounded

Per-frame work must not allocate from the heap. Ordering-table, GPU-packet,
entity, SPU, and save capacities are explicit. Exhaustion must return failure or
increment a visible drop counter; it must never write past an arena.

### Source assets do not belong in the runtime parser

The runtime may consume embedded or cooked, versioned target data. OBJ, glTF,
PNG, WAV, Blender, and editor formats belong in future host tools. Do not grow
runtime code around convenient desktop parsers.

### Current and planned behavior stay separate

Only compiled and proportionally tested behavior is present or experimental.
World streaming, skeletal animation, collision, fog, general asset cooking, asynchronous saves, and
platform bootstrap beyond macOS remain planned until their code and validation
land.

### Regional timing is observable

`psxe_engine_run` invokes one update per video frame. `ticks_per_second` is 60
for NTSC and 50 for PAL. Do not present frame increments as seconds or make PAL
silently simulate faster/slower without an explicit game policy.

## Critical Implementation Contracts

- **Packet arena:** `psxe_renderer_allocate` in
  [`src/render/packet.c`](src/render/packet.c) (declared for the renderer
  translation units in [`src/render/render_internal.h`](src/render/render_internal.h))
  is the only allocator for target GPU packets. It aligns to four bytes, checks
  `packet_end`, and increments `dropped_primitives` on failure. Every draw path
  must handle `NULL`; a path that needs a state packet plus a primitive must
  reserve both in one allocation. A failed compound draw leaves the packet
  pointer and ordering table intact and counts one drop.
- **Ordering-table lifetime:** draw functions add packets only to
  `renderer->ordering_table`/`ot_length`, which is the active buffer's table or,
  during a render-target pass, a table allocated from the active packet arena.
  Use `psxe_renderer_bucket`, never `buffers[...]` directly.
  `psxe_renderer_end_frame` closes an open pass, waits for GPU/vblank, submits
  the pass chain then that buffer, switches buffers, resets the packet pointer
  and destination table, and clears only the next table. Reusing packets before
  `DrawSync` corrupts the in-flight display list.
- **Depth direction:** bucket zero is frontmost text, bucket one is sprites and
  panels, and larger buckets render earlier/farther. Mesh depth derived from
  `gte_avsz3` must be rejected outside `[2, renderer->ot_length)` before
  indexing. Sprite `layer` values clamp to the last bucket.
- **Render-target chain:** [`src/render/target.c`](src/render/target.c) builds
  `enter DR_ENV → target OT → bucket 0 → leave DR_ENV` per pass; `leave` is
  inserted into bucket 0 immediately after `ClearOTagR` so later bucket-0
  packets execute before it. Passes link through `pass_head`/`pass_tail` and the
  last `leave` points at the main table's last bucket in `end_frame`. Begin saves
  camera/depth shift/light set and moves the GTE geometry offset to the target
  center; end restores them, so a pass may install its own lights. Targets are single-buffered VRAM, safe
  only because `end_frame` calls `DrawSync` before submitting the next list.
- **GPU draw-mode bits:** `DR_TPAGE` packets must pass `renderer->dither` as the
  dither bit (bit 9), or dithering turns off for the rest of the list. Blend
  mode (ABR) comes from a textured primitive's own `tpage`, but untextured
  semi-transparent primitives and `SPRT` use the current draw mode, so they need
  a preceding `DR_TPAGE` (`psxe_tpage_with_blend` rewrites ABR bits).
- **GTE global state:** camera projection, matrices, light matrix, back color,
  and color matrix are global coprocessor state. `psxe_draw_mesh_lights` (all
  mesh draws route through it) must run light selection and the SDK matrix
  helpers *before* installing the mesh's rotation/translation/light/color/back
  registers, because those helpers may use GTE registers. Meshes that cannot
  light (`PSXE_EFFECT_UNLIT` or no normals) skip selection and leave the light
  registers stale, which is safe only because no face of theirs is lit.
- **Timing diagnostics:** `psxe_render_config.profile_timing` gives the engine
  exclusive use of Timer 2. `psxe_engine_run` times input, update, and render.
  `psxe_renderer_end_frame` times `DrawSync` before `VSync`. Each interval
  restarts the clock at CPU clock / 8 with timer IRQs disabled. At
  `UINT16_MAX`, the interval reached the timer limit. Register access lives in
  [`src/platform/profile_internal.h`](src/platform/profile_internal.h).
- **Light selection:** [`src/render/light.c`](src/render/light.c)
  `psxe_light_select` evaluates the renderer set plus borrowed extra lights at
  the mesh origin each draw; it never touches GTE state. Directions point from
  surfaces toward the light (unit = `ONE`), stored world-space and multiplied by
  the object rotation (not scale). Up to three directional/point lights fill
  GTE rows; ambient lights and the set ambient add to back color; the rest add a
  quarter of their color to back color and count as `lights_folded`. Colors map
  `255 * ONE / 255 = 4096` (1.0); back color clamps to 255 before
  `gte_SetBackColor`. Directional `direction` set by field assignment is not
  renormalized.
- **Scene light slots:** `psxe_entity.light_slot` and
  `psxe_scene_light_slot.owner` must point at each other; `psxe_scene_destroy`
  clears the slot before bumping generation, and gather re-checks both links.
- **SIO0 ownership:** [`src/platform/input.c`](src/platform/input.c) polls
  each controller in a bounded critical section. The BIOS card driver starts
  with BIOS pad polling disabled. Active-low pad bits are inverted once before
  deriving button edges. Motor negotiation preserves the selected pad mode.
- **CD ownership:** [`src/platform/cd.c`](src/platform/cd.c) owns the SDK CD
  callbacks. Never call a parallel SDK CD reader. A data read borrows DMA
  memory until its terminal result. Disarm the callback before recovering from
  cancellation or failure. Sector-transfer mode controls the DMA stride.
- **Asset ownership:** [`src/core/memory.c`](src/core/memory.c) owns mesh and
  heap lifetimes. [`src/render/vram.c`](src/render/vram.c) owns managed TIM
  pixels and palettes. Reserve fixed targets and legacy TIM regions before
  managed uploads. Detach borrowed views before release and wait for GPU work
  before recycling VRAM. Managed transfers happen between frame builds.
- **Animated poses:** [`src/core/animation.c`](src/core/animation.c) samples
  equally spaced baked vertex and optional normal frames into disjoint
  caller-owned buffers. The output view borrows mesh topology and texture.
  Keep clip data immutable, and advance in milliseconds using the video rate.
- **SPU DMA and memory:** uploads begin after `0x1010`, are rounded/aligned to 64
  bytes, and must remain below `0x80000`. `psxe_audio_upload_vag` must validate
  big-endian header lengths before DMA. Playback may steal the next round-robin
  voice; there is not yet completion or priority tracking.
- **Memory-card boundaries:** public payloads are limited by
  `PSXE_MAX_SAVE_PAYLOAD`. Files are one 8192-byte block, store engine metadata
  at byte 512, and require magic/header version/game format/size/CRC validation
  before copying into game memory. I/O is synchronous and must never occur every
  frame.
- **Entity handles:** `psxe_entity_id` is valid only when index, active flag, and
  generation all match. Destroy increments generation so stale handles cannot
  access a reused slot.
- **Generated dependency ownership:** never patch generated
  `thirds/psn00bsdk` as an engine fix. Pin or patch upstream inputs through
  `thirds/versions.env` and `thirds/setup_macos.sh`, with licensing and upgrade
  rationale recorded.
- **Disc names:** ISO entries follow PS1/ISO9660 8.3 limits. `system.cnf` boot
  paths and `iso.xml` executable names must change together.

## Architecture and Data/Control Flow

Build flow:

`thirds/setup_macos.sh` → local MIPS GCC + PSn00bSDK + `mkpsxiso` → `build.sh` →
CMake cross-compile → PS-X EXE → sample ISO XML → BIN/CUE

Runtime frame flow:

`main` → `psxe_engine_init` → game `init` → `psxe_input_poll` → game `update` →
`psxe_renderer_begin_frame` → game `render`/`psxe_scene_render` → GTE/GPU packet
arena + ordering table → `psxe_renderer_end_frame` → GPU DMA/vblank

Persistence flow:

game payload → one-block BIOS save header + PSXE format header + CRC → `bu00:` or
`bu10:`; load validates all metadata before copying back to the caller.

Runtime is a single process on one MIPS CPU. The GTE and GPU execute submitted
work asynchronously, SPU voices play independently after key-on, and memory-card
BIOS calls block. No engine subsystem currently owns threads or heap lifetime.

## Linked Source Tree and File Reference

### [`.gitignore`](.gitignore)

Excludes compiler products, disc images, editor noise, and generated dependency
trees. Do not ignore tracked dependency pins or source assets merely because a
generated extension is similar.

### [`CMakeLists.txt`](CMakeLists.txt)

Defines the `psxe` static library, its exact source list, public include root,
C99 requirement, warnings, and sample toggle.

- **Key subparts:** `PSXE_SOURCES` is the authoritative engine translation-unit
  registry; `PSN00BSDK_TARGET_TYPE=EXECUTABLE_GPREL` propagates correct target
  flags to the static library; explicit link flags select the R3000A soft-float
  `libgcc`; the public SDK archive list repeats target libraries after `psxe` so
  static references resolve in linker scan order.
- **Depends on:** PSn00bSDK's CMake toolchain and helper functions.
- **Common mistake:** adding a `.c` file without registering it here leaves it
  uncompiled; setting ordinary host CMake produces the wrong architecture.

### [`CMakePresets.json`](CMakePresets.json)

Defines `debug` and `release` target configure/build presets. Both consume
`PSN00BSDK_LIBS` and `PSXE_TOOLCHAIN` generated in `thirds/env.sh`.

### [`build.sh`](build.sh)

Sources the generated dependency environment, configures the selected preset,
builds it, and reports the sample CUE path. It changes `build/` only.

### [`run.sh`](run.sh)

Builds a missing sample and launches PCSX-Redux from `PCSX_REDUX`, `PATH`, or
the standard macOS application path. It does not download an emulator or BIOS.

### [`src/include/psxe/config.h`](src/include/psxe/config.h)

Owns compile-time capacities: `PSXE_OT_LENGTH`, `PSXE_PACKET_BUFFER_SIZE`,
`PSXE_MAX_ENTITIES`, `PSXE_MAX_LIGHTS`, `PSXE_MAX_SCENE_LIGHTS` (range-checked
with `#error`), audio voice count, card block size, and maximum payload.
Override project-wide before headers instantiate context layouts; mismatched
translation-unit overrides create ABI corruption.

### [`src/include/psxe/types.h`](src/include/psxe/types.h)

Owns shared color/transform types, PSX fixed-point constants, array-count helper,
and `psxe_transform_identity`. `VECTOR` positions are integer world units;
rotation is a 4096-unit full turn and scale is 20.12 fixed point (`ONE`).

### [`src/include/psxe/input.h`](src/include/psxe/input.h)

Declares `psxe_input`, normalized `psxe_pad_state`, polling, port lookup, and
held/pressed/released queries and finite-duration rumble. The raw SIO0 reply
is private to the driver.

### [`src/platform/input.c`](src/platform/input.c)

Polls both SIO0 ports each frame and negotiates DualShock motor mapping.

- **Key functions:** `psxe_input_poll` owns bounded serial exchange, active-low
  normalization, reconnect/idle handling, and edges. `psxe_input_rumble` queues
  motor levels for a finite number of polls.
- **Tests:** DuckStation digital pad input and DualShock motor mapping/output.
  Physical controllers, prolonged disconnects, and PAL timing remain open.
- **Common mistake:** computing edges from raw active-low bits reverses presses.

### [`src/include/psxe/audio.h`](src/include/psxe/audio.h)

Declares the linear SPU allocator, validated resident sample descriptor,
round-robin play, stop, and VAG loading from an ISO file with caller-owned
staging RAM. The CD module owns XA and CD-DA playback.

### [`src/platform/audio.c`](src/platform/audio.c)

Owns SPU initialization, VAG header parsing, aligned DMA, panning, voice register
setup, and key on/off.

- **Key functions:** `psxe_audio_upload_vag` validates `VAGp`, big-endian size
  and sample rate, and SPU capacity; `psxe_audio_play` clamps volume/pan and may
  steal the next voice. `psxe_audio_get_stats` reports the padded resident sample
  allocation beginning at the first aligned SPU address, `0x1040`.
- **Tests:** the cube chime loads and starts a voice in DuckStation. Audible
  capture and hardware tests remain open.
- **Common mistake:** VAG fields are big-endian and the first 4 KiB of SPU RAM
  is reserved. Do not upload the 48-byte VAG header as sample data.

### [`src/include/psxe/memory_card.h`](src/include/psxe/memory_card.h)

Declares game identity/title/version configuration, result codes, and synchronous
save/load, status, directory/free-block, and delete calls. Slots zero and one
mean physical port/card slots 1 and 2 (`bu00:` and `bu10:`).

### [`src/platform/memory_card.c`](src/platform/memory_card.c)

Owns BIOS card initialization, filenames, BIOS-visible header/icon, and
exact-block file I/O.

- **Key subparts:** `psxe_make_filename` enforces the 20-character card filename
  payload; `psxe_write_card_visual_header` creates one static icon;
  `psxe_memory_card_load` delegates to the save format decoder after an exact-block read.
- **Tests:** DuckStation save, overwrite, load, listing, and deletion in both
  slots. Card-full, removal, unformatted, and hardware cases remain open.
- **Common mistake:** changing payload layout without incrementing
  `format_version` makes old data appear valid. Do not return a partially read
  payload.

### [`src/core/save_format.c`](src/core/save_format.c)

`psxe_save_encode` and `psxe_save_decode` own the little-endian version 1 engine
header at byte 512 and its payload CRC32. Decode validates before modifying
caller output. Host tests lock down the serialized bytes and corruption paths.

### [`src/core/asset_formats.c`](src/core/asset_formats.c)

`psxe_tim_parse` and `psxe_vag_parse` validate the same sized inputs consumed by
the managed VRAM and SPU upload paths. Their views borrow the input buffer.
They execute on the host without DMA or hardware register access.

### [`src/include/psxe/animation.h`](src/include/psxe/animation.h) and [`src/core/animation.c`](src/core/animation.c)

`psxe_anim_clip` borrows a mesh and frame-major vertex and optional normal
streams. `psxe_anim_play` validates frame sizes and starts playback.
`psxe_anim_seek` and `psxe_anim_advance` wrap loops and clamp one-shots.
`psxe_anim_finished` reports the last pose for one-shots.
`psxe_anim_sample` uses fixed-point interpolation and writes a drawable mesh
view with independent pose buffers. Host tests exercise wrap and clamp,
stream and buffer capacities, and signed interpolation.

### [`src/include/psxe/cd.h`](src/include/psxe/cd.h) and [`src/platform/cd.c`](src/platform/cd.c)

Owns ISO9660 file lookup, byte ranges, sector DMA, LBA seeks, spindle and
transport control, XA filtering, and CD-DA. The drive serves one operation
at a time. The byte-range reader uses a shared 2048-byte scratch sector for
partial or unaligned reads. Audio and data share the drive.
If SDK initialization returns with an outstanding command, `psxe_cd_init`
retries `CdlInit` with a frame-bounded wait before enabling data callbacks.
`psxe_cd_sectors_read` samples the IRQ-maintained count of completed data and
raw sectors. It includes filesystem reads and successful partial transfers.

### [`src/include/psxe/memory.h`](src/include/psxe/memory.h) and [`src/core/memory.c`](src/core/memory.c)

Owns the caller-backed aligned heap, adjacent-block coalescing and statistics,
and the single-allocation `psxe_mesh_storage` view and writable arrays.

### [`src/include/psxe/vram.h`](src/include/psxe/vram.h) and [`src/render/vram.c`](src/render/vram.c)

Owns reservations, page-aware placement, sized TIM/CLUT validation, palette
views, and generation-checked release. Tiles are 16 VRAM halfwords by 8 rows.
Legacy fixed uploads and render targets require explicit reservations.
`psxe_vram_get_stats` counts tiles in use, including reservations.

### [`src/include/psxe/render.h`](src/include/psxe/render.h)

Defines renderer/texture/mesh/camera structures, per-face flags, the
`psxe_material` override (texture, color mode, effects, blend), `psxe_object`,
`psxe_sprite`, `psxe_render_target`, `psxe_light`/`psxe_light_set`, and the 3D,
sprite, render-to-texture, light, and UI API. Context arrays make memory cost explicit. `psxe_face` is triangular and
uses indices into immutable vertex/normal arrays; materials let instances differ
without mutating shared mesh data.

### [`src/render/render.c`](src/render/render.c)

Owns GPU/GTE setup, double-buffered ordering tables, camera
composition, per-mesh GTE light installation, TIM upload, mesh submission,
panels, and font sorting.

- **Key functions:** `psxe_make_object_matrix` combines inverse camera and object transform;
  `psxe_draw_mesh_lights` validates indices, resolves material texture/color/
  effects, selects lights, installs GTE state, transforms three vertices,
  rejects backfaces/depth, optionally lights, then emits `POLY_F3`/`POLY_FT3`
  (+`DR_TPAGE` for untextured blended faces); `psxe_draw_mesh_material` and
  `psxe_draw_mesh` are the no-extra-lights/`NULL`-material wrappers;
  `psxe_renderer_end_frame` owns synchronization, pass-chain submission, and
  buffer flip. It records packet bytes and the high-water mark before switching
  arenas. Face rejection and actual font packet counts feed frame diagnostics.
- **Tests:** target compilation and cube rendering. Near-plane clipping,
  off-screen clipping, fog, Gouraud shading, target packet stress,
  PAL, and hardware captures are gaps.
- **Common mistakes:** PS1 texture pages use word-based TIM widths; 16-bit page
  `u` is `x & 63`; state packets for sprites must execute before sprite packets
  despite LIFO OT insertion; GTE global state must be re-established per mesh.

### [`src/render/packet.c`](src/render/packet.c)

Owns `psxe_renderer_allocate` and `psxe_renderer_bucket`. Capacity checks use
the remaining byte count before computing the result pointer. Host tests cover
alignment, exact fills, overflow, and layer clamping.

### [`src/render/render_internal.h`](src/render/render_internal.h)

Private renderer declarations for the packet allocator, destination-bucket
lookup, ABR rewrite helper, `psxe_light_state`, and `psxe_light_select`.

### [`src/core/fixed.c`](src/core/fixed.c)

Owns the CPU-only `psxe_vector_normalize` and `psxe_matrix_apply` used by the
scene and lighting code. Normalize rescales int32 components before squaring.
Host tests cover zero, signed, and extreme inputs and 4.12 matrix application.

### [`src/render/light.c`](src/render/light.c)

Owns `psxe_light_*` value helpers, the renderer light set
(`psxe_renderer_clear/add/set_lights`, `psxe_renderer_light`,
`psxe_renderer_set_ambient`, the `psxe_renderer_set_directional_light`
wrapper), per-mesh selection, and
`psxe_draw_light_marker`.

- **Key subparts:** `psxe_consider_light` applies masks,
  attenuation (`LINEAR` t, `SMOOTH` t², `NONE`), and top-three ranking by
  scaled `r+g+b`; the marker projects on the CPU into bucket 1.
- **Tests:** target compile and cube sample (sun + orbiting lamp + ambient
  presets + per-pass light set); emulator captures and CPU-tick cost are gaps.
- **Common mistake:** writing an unnormalized `direction` field, or expecting
  per-vertex point lighting: attenuation is per mesh origin, so large meshes
  near a point light light uniformly.

### [`src/render/object.c`](src/render/object.c)

Owns `psxe_material_*` defaults/setters and `psxe_object_*` helpers. It never
allocates packets; `psxe_object_draw` forwards to `psxe_draw_mesh_material`.
Rotation helpers wrap to 0..4095.

### [`src/render/sprite.c`](src/render/sprite.c)

Owns `psxe_sprite_*` and the `psxe_draw_sprite` shortcut. Draw selects `TILE`
(solid/untextured), `SPRT`+`DR_TPAGE` (unscaled), or `POLY_FT4`
(scaled/flipped). Texel coordinates clamp to 0..255.

- **Tests:** cube sample scaled/flipped render-target preview; atlas TIM, solid
  sprite, blend-mode, and layer captures are gaps.

### [`src/render/target.c`](src/render/target.c)

Owns render-target validation, pass begin/end, `DR_ENV` construction from
public SDK macros, and the 16-bit texture view.

- **Key subparts:** `psxe_render_target_init` rejects misaligned, oversized,
  page-crossing, framebuffer- or font-overlapping rectangles; begin allocates
  the pass table and both environment packets all-or-nothing from the packet
  arena.
- **Tests:** cube sample draws two 128x128 targets per frame; emulator VRAM
  capture, multiple-pass ordering, STP/black-texel behavior, and hardware runs
  are gaps.
- **Common mistake:** sampling a target in its own pass, black (`0x0000`) texels
  becoming transparent, or failing to reserve its rectangle in a managed pool.

### [`src/include/psxe/scene.h`](src/include/psxe/scene.h)

Declares a fixed entity pool with generational handles, transforms, optional
mesh, per-entity `psxe_material`, visibility, and a fixed
`PSXE_MAX_SCENE_LIGHTS` pool of entity-owned lights. It is a minimal render scene, not a general component or
serialization system.

### [`src/core/scene.c`](src/core/scene.c)

Owns pool initialization, first-free creation, generation-checked lookup,
destruction, entity light slots/world transforms/gathering, and visible mesh
iteration.

- **Key functions:** `psxe_scene_id_valid` centralizes handle validity;
  `psxe_scene_destroy` invalidates stale handles before reuse;
  `psxe_scene_set_light`/`psxe_scene_clear_light` own slot links;
  `psxe_scene_gather_lights` emits world-space lights of active entities whose
  light is enabled (mesh visibility does not matter);
  `psxe_scene_render` gathers once, then submits active visible meshes with
  their material and those lights.
- **Tests:** cube integration and host tests for handle generations, scene
  capacity, light-slot exhaustion, and release on entity destruction.

### [`src/include/psxe/engine.h`](src/include/psxe/engine.h)

Declares callback lifecycle, engine configuration, and the top-level aggregate
of renderer, input, audio, save identity, frame counter, regional tick rate, and
running flag.

### [`src/core/engine.c`](src/core/engine.c)

Owns subsystem initialization order and the perpetual update/render loop.

- **Key functions:** `psxe_engine_init` selects 50/60 ticks and initializes
  renderer → input → audio → card; `psxe_engine_run` calls game lifecycle hooks;
  `psxe_engine_stop` requests exit. The loop records `cpu_ticks` when timing is
  enabled.
- **Common mistake:** shutdown is only reached after `running` becomes false;
  ordinary console games normally keep running forever.

### [`src/include/psxe/psxe.h`](src/include/psxe/psxe.h)

Convenience umbrella header for game code. Subsystems may include narrower
headers to reduce coupling; do not define behavior here.

### [`samples/cube/CMakeLists.txt`](samples/cube/CMakeLists.txt)

Builds the `psxe_cube` PS-X executable, links `psxe`, stages the ISO inputs with
a fixed UTC timestamp, and registers the ISO image target. Add sample-specific
embedded assets with PSn00bSDK helpers here.
Debug configurations enable `PSXE_CUBE_DIAGNOSTICS` for timer sampling and BIOS
TTY output.

### [`samples/cube/main.c`](samples/cube/main.c)

Integration sample defining a lit 12-triangle cube, a scene entity, an orbiting
lamp entity owning a point light, a static directional light with switchable
ambient presets, a per-pass light set in target B, light counters, controller
movement, material effect cycling, two render targets shown on a textured quad
object and a scaled/flippable sprite, UI diagnostics, and save/load callbacks.

- **Key subparts:** `cube_faces` demonstrates winding/normals/materials;
  `game_update` demonstrates edge/held input and synchronous card operations;
  `game_render` demonstrates scene then UI submission; `main` owns region and
  memory-card identity. The debug HUD and `update_diagnostics` sample complete
  frames and resource occupancy once per video second.
- **Common mistake:** source-defined geometry is only a bootstrap example; new
  content pipelines belong in host cooking tools, not giant production C arrays.

### [`samples/cube/system.cnf`](samples/cube/system.cnf)

PS1 BIOS boot configuration. `BOOT` must exactly match the executable entry in
`iso.xml`; stack placement is target configuration, not host syntax.

### [`samples/cube/iso.xml`](samples/cube/iso.xml)

Defines volume identifiers, fixed timestamps, boot files, documentation, and
padding for `mkpsxiso`. File paths refer to `disc/` in the sample build directory.
Keep disc entry names uppercase 8.3.

### [`samples/carrot/`](samples/carrot/)

Builds `psxe_carrot` and packages it with the model credits into a BIN/CUE.
The port 1 D-pad or left stick moves and turns Carrot Man on a bounded floor.
`main.c` uses the animation player and a managed TIM; `bake.py` authors
idle/walk poses and calls the host cooker. The checked-in asset fixtures are
the baked topology, positions, and indexed TIM. Keep their attribution when
redistributing the demo. [Sample workflow](samples/carrot/README.md)

### [`tools/cook_glb.py`](tools/cook_glb.py)

Cooks one static textured GLB primitive into PSXE face initializers,
little-endian `SVECTOR` positions, and an 8/16-bpp TIM. It runs on the host
and calls FFmpeg to decode the image. Rotation, quantization, and baked pose
generation happen offline, and the executable embeds the cooked vectors and
texels with the SDK's `incbin` helper.

### [`tests/`](tests/)

`host/run.sh` uses a native C compiler with the pinned SDK headers to test CPU
modules. `reproducible_disc.sh` compares clean target builds from different
directories and time zones. `check_markdown_links.py` checks repository-local
inline links and heading anchors. [GitHub Actions](.github/workflows/ci.yml)
runs the host and target checks.

### [`thirds/versions.env`](thirds/versions.env)

Pins GNU binutils/GCC versions and archive SHA-256 values plus the exact
PSn00bSDK commit. Dependency upgrades begin here and require a clean bootstrap,
engine build, image build, and upstream license/API review.

### [`thirds/setup_macos.sh`](thirds/setup_macos.sh)

Bootstraps missing host prerequisites without upgrading already installed
formulae, verifies downloads, builds target tools locally, checks out PSn00bSDK
recursively, installs it, and generates ignored `env.sh`.

- **Key functions:** `download_and_verify` rejects tampered archives; required
  source markers make interrupted extraction resume safely; GCC is constrained
  to little-endian MIPS-I soft-float and the SDK to GNU C17 for GCC 15+ source
  compatibility; `check_installation` validates exact target tools;
  `write_environment` emits build paths.
- **Mutation/network:** normal mode contacts Homebrew, GNU mirrors, and GitHub
  and writes generated `thirds/` trees. `--check` is read-only.
- **Common mistake:** do not substitute an unpinned `master` checkout or skip
  checksums for convenience.

### [`thirds/README.md`](thirds/README.md)

Documents generated dependency ownership and the macOS entry points. Keep it in
sync with setup output paths and platform scripts.

### [`README.md`](README.md)

User-facing mission, current feature boundary, macOS workflow, programming
model, sample controls, influences, and safety/status notes. It must not claim a
ROADMAP feature is implemented.

### [`APIs.md`](APIs.md)

Game-developer reference for every public header symbol, with per-function
snippets and multi-subsystem recipes. Update it in the same change as any public
header or observable behavior change; it must describe the checked-out code, not
roadmap intent. Its recipes are compile-checked only, not runtime-tested.

### [`ROADMAP.md`](ROADMAP.md)

Authoritative phased backlog and architecture direction. Check off work only
after implementation and proportional validation; move status in README and
this file in the same change.

### [`LICENSE`](LICENSE)

CC0 dedication for repository-owned work. It does not relicense PSn00bSDK, GNU
tools, sample inputs from elsewhere, or future third-party assets.

## Features and Recurring Development Pitfalls

### Engine lifecycle and regional loop — Experimental/scaffold

- **Behavior:** callback-based one-update/one-render frame loop, 60 Hz NTSC or
  50 Hz PAL metadata.
- **Flow:** sample `main` → `psxe_engine_init` → `psxe_engine_run`.
- **Gap:** no delta/fixed-step accumulator, pause service, timer API, or PAL
  sample validation.

### Lit 3D, sprites, and UI — Experimental/scaffold

- **Behavior:** colored/textured flat triangles, optional per-face lighting,
  ordering-table depth, affine GPU texturing, dithering, sprites, panels, text.
- **Flow:** game/scene → `psxe_draw_mesh`/2D calls → packet arena → OT → GPU.
- **Gap:** no near clipping, subdivision, Gouraud, depth cue/fog, or production
  frustum/mesh culling.

### Baked mesh animation — Experimental/scaffold

- **Behavior:** loop or one-shot playback of evenly spaced poses with
  optional animated normals and fixed-point interpolation.
- **Flow:** cooked positions → animation player → caller-owned pose buffer
  → mesh view → ordinary object or scene drawing.
- **Gap:** rigid-part tracks, skeletal skinning, and character animation
  profiling on physical hardware.

### Materials, objects, and sprite structs — Experimental/scaffold

- **Behavior:** `psxe_material` overrides texture, color (face/replace/tint),
  effects (unlit, untextured, semi-transparent, double-sided, sprite flips), and
  the four GPU blend modes per object, entity, or sprite without GTE cost.
  `psxe_object`/`psxe_sprite` setters change texture/region/size/position.
- **Gap:** no sprite rotation, sprite sheets/animation helpers, per-face material
  ranges, or emulator captures of every blend mode.

### Lights — Experimental/scaffold

- **Behavior:** directional, point, and ambient `psxe_light` values in a
  `PSXE_MAX_LIGHTS` renderer set and `PSXE_MAX_SCENE_LIGHTS` entity slots;
  per-mesh top-three selection into GTE rows, remainder folded to back color,
  layer masks via `psxe_material.light_exclude`, render-target save/restore,
  point markers, per-frame counters.
- **Flow:** game/scene → `psxe_draw_mesh_lights` → `psxe_light_select` (CPU)
  → GTE light/color/back registers → `NCCS` per lit face.
- **Gap:** flat per-face only (no Gouraud/per-vertex), origin-only ranking (no
  mesh bounds), no radius debug draw, no measured CPU cost, no emulator
  captures.

### Render to texture — Experimental/scaffold

- **Behavior:** bounded offscreen passes into validated 16-bit VRAM rectangles,
  executed before the main pass in begin order and sampled as `psxe_texture`.
- **Gap:** single-buffered only, no paletted/8-bit or automatically allocated targets,
  no emulator/hardware validation.

### Controllers — Experimental/scaffold

- **Behavior:** two ports, common digital/analog IDs, axes, held/edge masks,
  motor mapping, finite rumble, and reconnect handling.
- **Gap:** no forced analog-mode switching, multitap, dead zones, or hardware matrix.

### Resident VAG effects — Experimental/scaffold

- **Behavior:** validated mono VAG payloads from RAM or disc upload linearly
  and play with volume/pan on round-robin hardware voices.
- **Gap:** no cooker, reclaim, priorities, status, buses, or spatialization.
  XA and CD-DA use the separate CD transport.

### Memory-card saves — Experimental/scaffold

- **Behavior:** synchronous one-block files per identity with version and CRC,
  plus directory, block count, status, and delete. The sample saves cube rotation.
- **Gap:** no asynchronous state machine, localization/icon assets, journaling,
  migration callbacks, or physical-card validation.

### Packet allocation overflow

- **Symptom / wrong assumption:** geometry or UI disappears; increasing entity
  capacity alone does not help.
- **Cause and invariant:** packet bytes, not entity count, exhausted the active
  frame arena. `psxe_renderer_allocate` drops safely and increments a counter.
- **Safe pattern / check:** inspect `dropped_primitives`, measure high-water use,
  reduce submissions or deliberately raise `PSXE_PACKET_BUFFER_SIZE` for all
  translation units. Never bypass the allocator.
- **Status:** deliberate bounded limitation.

### Ordering-table state reverses insertion order

- **Symptom / wrong assumption:** a sprite samples the wrong texture page or UI
  layering appears reversed.
- **Cause and invariant:** `addPrim` prepends; the last inserted packet in a
  bucket executes first. State packets therefore get inserted after primitives
  they must precede at execution.
- **Safe pattern / check:** follow `psxe_draw_sprite` and `FntSort`, validate in
  an emulator VRAM/frame capture.
- **Status:** hardware behavior and regression risk.

### Render-target texels are transparent or missing

- **Symptom / wrong assumption:** holes in a render-target texture, or the
  target shows stale/garbage pixels.
- **Cause and invariant:** sampled 16-bit texel `0x0000` is transparent; the pass
  silently drops when the packet arena cannot hold `ot_length * 4 + 72` bytes;
  an unreserved rectangle can be overwritten by a TIM.
- **Safe pattern / check:** non-black clear color, check `begin`'s return and
  `target_passes`, author TIM positions away from targets, inspect VRAM in
  PCSX-Redux.
- **Status:** hardware behavior plus deliberate bounded limitation.

### Capacity macros can break ABI

- **Symptom / wrong assumption:** context fields corrupt despite individually
  compiling files.
- **Cause and invariant:** public structs contain arrays sized by macros. Defining
  different values for engine and game translation units changes layout.
- **Safe pattern / check:** set definitions with target-wide compile definitions,
  clean-rebuild every object, and consider a future generated config header.
- **Status:** active design limitation.

## Interface Ownership Map

- Game lifecycle: `psxe_engine_init/run/stop` →
  [`src/core/engine.c`](src/core/engine.c).
- 3D/2D public drawing and textures: `psxe_renderer_*`, `psxe_draw_mesh*`,
  `psxe_texture_*`, `psxe_ui_*` → [`src/render/render.c`](src/render/render.c).
- Materials and objects: `psxe_material_*`, `psxe_object_*` →
  [`src/render/object.c`](src/render/object.c).
- Sprites: `psxe_sprite_*`, `psxe_draw_sprite` →
  [`src/render/sprite.c`](src/render/sprite.c).
- Lights: `psxe_light_*`, `psxe_renderer_*light*`, `psxe_renderer_set_ambient`,
  `psxe_draw_light_marker` → [`src/render/light.c`](src/render/light.c).
- Render to texture: `psxe_render_target_*` →
  [`src/render/target.c`](src/render/target.c).
- Scene handles/render iteration: `psxe_scene_*` →
  [`src/core/scene.c`](src/core/scene.c).
- Baked mesh playback and sampling: `psxe_anim_*` →
  [`src/core/animation.c`](src/core/animation.c).
- Controller snapshots: `psxe_input_*`, `psxe_button_*` →
  [`src/platform/input.c`](src/platform/input.c).
- CD data, drive, and audio transport: `psxe_cd_*` →
  [`src/platform/cd.c`](src/platform/cd.c).
- Asset RAM and mesh storage: `psxe_heap_*`, `psxe_mesh_alloc/free` →
  [`src/core/memory.c`](src/core/memory.c).
- Managed texture storage: `psxe_vram_*` →
  [`src/render/vram.c`](src/render/vram.c).
- Resident sound: `psxe_audio_*` → [`src/platform/audio.c`](src/platform/audio.c).
- Save persistence: `psxe_memory_card_*` →
  [`src/platform/memory_card.c`](src/platform/memory_card.c).
- Build/package commands: presets → [`CMakePresets.json`](CMakePresets.json),
  wrappers → [`build.sh`](build.sh), [`run.sh`](run.sh).
- Sample disc registry: [`samples/cube/CMakeLists.txt`](samples/cube/CMakeLists.txt)
  and [`samples/carrot/CMakeLists.txt`](samples/carrot/CMakeLists.txt), each
  with its own ISO manifest.

## Build, Run, Test, Debug, and Release Checklist

Supported and implemented host bootstrap is macOS with Apple Command Line Tools
and Homebrew. Commands are repository-root relative:

```sh
./thirds/setup_macos.sh             # networked, long-running, mutates thirds/ and Homebrew host packages
./thirds/setup_macos.sh --check     # audits installed target dependencies
PSXE_SANITIZE=1 tests/host/run.sh     # native CPU tests, with address and undefined behavior sanitizers
python3 tests/check_markdown_links.py
./build.sh debug                    # configures, compiles, and creates sample BIN/CUE
./build.sh release
tests/reproducible_disc.sh release  # compares two fresh PS-X EXE and BIN/CUE builds
PCSX_REDUX=/path/to/PCSX-Redux ./run.sh debug
git diff --check
```

Direct CMake is valid only after sourcing generated paths:

```sh
source thirds/env.sh
cmake --preset debug
cmake --build --preset debug
```

The cube and Carrot Man samples are target integration builds. A release claim additionally
requires boot/input/audio/save/render checks in accurate emulators and on
original hardware. Serial smoke, emulator screenshots, and physical-media
validation remain open.

## Test Ownership Map

- Engine/source ABI and all module link dependencies → full `./build.sh debug`.
- Disc manifest/boot-name consistency → `psxe_cube_iso` and `psxe_carrot_iso` outputs from the same
  command, then emulator boot (manual).
- 3D/light objects/scene lights/UI, materials/effects, objects, sprites, render targets, and
  input/save integration → [`samples/cube/main.c`](samples/cube/main.c)
  (manual runtime and DuckStation smoke checks).
- Script syntax → run `bash -n` on each script, as in the GitHub Actions host job.
- Animation interpolation and clip bounds → host tests. Cooked mesh, TIM,
  and movement integration → Carrot Man disc in DuckStation.
- Dependency installation → `./thirds/setup_macos.sh --check`.
- Disc data/audio, mesh and texture lifetime → DuckStation checks with the
  cube disc plus temporary smoke executables. A separate mixed-mode disc
  checks XA filter/mute/pause and CD-DA track-end auto-pause. Original hardware
  tests remain open.
- CD startup recovery → the DuckStation NTSC-U baseline timed out during
  SDK initialization and skipped the disc assets. A debug smoke build with
  the retry loaded the TIM and VAG, started an SPU voice, round-tripped saves
  in both slots, and reported two render-target passes with zero dropped
  primitives at frame 120.
- Frame diagnostics → the debug cube's HUD and BIOS TTY in DuckStation.
  A temporary target test covered compound packet exhaustion, exact font
  packet counts, and timing saturation after two VBlanks inside `update`.
- Memory-card corruption and version/size checks → host save-format tests.
  Card-full, removal, and physical I/O remain emulator/hardware cases.

## Data, Security, Privacy, and Compatibility Boundaries

- Repository source and dependency pins are canonical. `build/`, disc images,
  installed SDK/toolchain, caches, generated maps, and `thirds/env.sh` are
  derived and must be regenerated, not hand-edited or committed.
- Dependency bootstrap downloads only HTTPS-pinned archives/commits. GNU archives
  require stored SHA-256 matches. Review upstream license/provenance on upgrades.
- The engine holds no credentials, network data, telemetry, or personal data.
  Do not add BIOS images, Sony SDK files, regional disc-license data, signing
  keys, or proprietary game assets to the repository.
- Card filename identity and `format_version` are persistent compatibility
  contracts. Changing schema requires a new version and, once releases exist, a
  bounded migration path. CRC is corruption detection, not authentication or
  encryption.
- Public APIs and binary/save formats are pre-release and may change. Document
  breaking changes; do not claim compatibility before a release policy exists.
- All external data sizes and indices must be validated before DMA, array access,
  memory copy, or disc/card read. Target memory has no process isolation.

## Current Status and Known Gaps

### Shipped

- No stable release or production/hardware-qualified feature set exists yet.

### Experimental / Scaffold

- macOS-local reproducible target toolchain/SDK bootstrap.
- Engine static library, callback loop, scene pool, 3D/2D renderer, light
  objects and scene lights, materials with baked textures, render-to-texture,
  controller input and rumble, ISO/sector/drive access, CD and resident VAG
  audio, one-block saves, bounded RAM/VRAM allocation, baked mesh animation,
  and cube and Carrot Man EXEs and BIN/CUEs.

### Known Gaps

- Hosted CI verification, an emulator smoke harness, profiling, and
  original-hardware validation are open.
- Renderer lacks clipping, subdivision, production culling, skeletal animation,
  fog, and Gouraud shading. A general-purpose asset cooker is still planned.
- SPU reclamation, mixed audio/data scheduling, and asynchronous card I/O are open.
- Linux/Windows bootstraps and safe physical-media workflow are not implemented.

### Near-Term Priorities

1. Complete Milestone 0 runtime validation and Milestone 1 diagnostics/tests in
   [`ROADMAP.md`](ROADMAP.md).
2. Build the versioned asset cooker and expand resource lifetime diagnostics.
3. Add production clipping/culling/render budgets and streaming world services.

## Task Start and Handoff Checklist

1. Read this file, check `git status`, and identify public header, implementation,
   build registry, sample, docs, and roadmap owners for the change.
2. Re-check capacity, ordering-table/GTE global state, DMA/alignment, persistence,
   and generated-data boundaries before editing.
3. Add or update focused validation; never use a successful host syntax check as
   proof of a target runtime behavior.
4. Synchronize public headers, source registration, README, ROADMAP status, and
   the relevant file/feature/pitfall sections here.
5. Run script syntax, target build/package, `git diff --check`, local-link checks,
   and any relevant emulator/hardware tests. Report unrun checks and remaining
   gaps exactly.
