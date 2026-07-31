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
Streaming, animation, collision, fog, asset cooking, asynchronous saves, and
platform bootstrap beyond macOS remain planned until their code and validation
land.

### Regional timing is observable

`psxe_engine_run` invokes one update per video frame. `ticks_per_second` is 60
for NTSC and 50 for PAL. Do not present frame increments as seconds or make PAL
silently simulate faster/slower without an explicit game policy.

## Critical Implementation Contracts

- **Packet arena:** `psxe_renderer_allocate` in
  [`src/render/render.c`](src/render/render.c) is the only allocator for target
  GPU packets. It aligns to four bytes, checks `packet_end`, and increments
  `dropped_primitives` on failure. Every draw path must handle `NULL`.
- **Ordering-table lifetime:** draw functions add packets only to the active
  buffer. `psxe_renderer_end_frame` waits for GPU/vblank, submits that buffer,
  switches buffers, resets the packet pointer, and clears only the next table.
  Reusing packets before `DrawSync` corrupts the in-flight display list.
- **Depth direction:** bucket zero is frontmost text, bucket one is sprites and
  panels, and larger buckets render earlier/farther. Mesh depth derived from
  `gte_avsz3` must be rejected outside `[2, PSXE_OT_LENGTH)` before indexing.
- **GTE global state:** camera projection, matrices, light matrix, back color,
  and color matrix are global coprocessor state. `psxe_draw_mesh` must set the
  object/view/light state before transforming each mesh; callers must not expect
  private concurrent renderer state.
- **Active-low pads:** BIOS pad bits are zero when pressed.
  [`src/platform/input.c`](src/platform/input.c) inverts them once, then derives
  `pressed` and `released`. Game code must use `psxe_pad_state`, not reinterpret
  the raw buffer.
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
`bu01:`; load validates all metadata before copying back to the caller.

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
`PSXE_MAX_ENTITIES`, audio voice count, card block size, and maximum payload.
Override project-wide before headers instantiate context layouts; mismatched
translation-unit overrides create ABI corruption.

### [`src/include/psxe/types.h`](src/include/psxe/types.h)

Owns shared color/transform types, PSX fixed-point constants, array-count helper,
and `psxe_transform_identity`. `VECTOR` positions are integer world units;
rotation is a 4096-unit full turn and scale is 20.12 fixed point (`ONE`).

### [`src/include/psxe/input.h`](src/include/psxe/input.h)

Declares `psxe_input`, normalized `psxe_pad_state`, polling, port lookup, and
held/pressed/released queries. Raw 34-byte BIOS buffers remain an implementation
boundary even though they are stored in the context.

### [`src/platform/input.c`](src/platform/input.c)

Initializes BIOS pad polling and translates two ports each frame.

- **Key functions:** `psxe_input_init` owns `InitPAD`/`StartPAD` ordering;
  `psxe_input_poll` normalizes active-low bits, type support, axes, and edges;
  query helpers reject null/invalid ports.
- **Tests:** target compile and cube sample input; emulator/hardware reconnect
  tests are a known gap.
- **Common mistake:** computing edges from raw active-low bits reverses presses.

### [`src/include/psxe/audio.h`](src/include/psxe/audio.h)

Declares the linear SPU allocator, validated resident sample descriptor,
round-robin play, and stop API. It does not promise streaming, priorities,
automatic loop conversion, or voice completion callbacks.

### [`src/platform/audio.c`](src/platform/audio.c)

Owns SPU initialization, VAG header parsing, aligned DMA, panning, voice register
setup, and key on/off.

- **Key functions:** `psxe_audio_upload_vag` validates `VAGp`, big-endian size
  and sample rate, and SPU capacity; `psxe_audio_play` clamps volume/pan and may
  steal the next voice.
- **Tests:** target compile only; audible emulator/hardware tests remain open.
- **Common mistake:** VAG fields are big-endian and the first 4 KiB of SPU RAM
  is reserved. Do not upload the 48-byte VAG header as sample data.

### [`src/include/psxe/memory_card.h`](src/include/psxe/memory_card.h)

Declares game identity/title/version configuration, result codes, and synchronous
save/load calls. Slot zero means physical port/card slot 1 (`bu00:`).

### [`src/platform/memory_card.c`](src/platform/memory_card.c)

Owns BIOS card initialization, filenames, BIOS-visible header/icon, PSXE header,
CRC32, and exact-block file I/O.

- **Key subparts:** `psxe_make_filename` enforces the 20-character card filename
  payload; `psxe_write_card_visual_header` creates one static icon;
  `psxe_memory_card_load` validates before copy.
- **Tests:** cube sample save/load path; card-full, removal, unformatted, real
  hardware, and BIOS-manager appearance remain gaps.
- **Common mistake:** changing payload layout without incrementing
  `format_version` makes old data appear valid. Do not return a partially read
  payload.

### [`src/include/psxe/render.h`](src/include/psxe/render.h)

Defines renderer/material/texture/mesh/camera structures and the 3D, sprite, and
UI API. Context arrays make memory cost explicit. `psxe_face` is triangular and
uses indices into immutable vertex/normal arrays.

### [`src/render/render.c`](src/render/render.c)

Owns GPU/GTE setup, double-buffered ordering tables, packet allocation, camera
composition, lights, TIM upload, mesh submission, sprites, panels, and font
sorting.

- **Key functions:** `psxe_renderer_allocate` enforces packet bounds;
  `psxe_make_object_matrix` combines inverse camera and object transform;
  `psxe_draw_mesh` validates indices, transforms three vertices, rejects
  backfaces/depth, optionally lights, then emits `POLY_F3`/`POLY_FT3`;
  `psxe_renderer_end_frame` owns synchronization and buffer flip.
- **Tests:** target compilation and cube rendering. Near-plane clipping,
  off-screen clipping, textured sample assets, fog, Gouraud shading, packet
  stress, PAL, and hardware captures are gaps.
- **Common mistakes:** PS1 texture pages use word-based TIM widths; state packets
  for sprites must execute before sprite packets despite LIFO OT insertion; GTE
  global state must be re-established per mesh.

### [`src/include/psxe/scene.h`](src/include/psxe/scene.h)

Declares a fixed entity pool with generational handles, transforms, optional
mesh, and visibility. It is a minimal render scene, not a general component or
serialization system.

### [`src/core/scene.c`](src/core/scene.c)

Owns pool initialization, first-free creation, generation-checked lookup,
destruction, and visible mesh iteration.

- **Key functions:** `psxe_scene_id_valid` centralizes handle validity;
  `psxe_scene_destroy` invalidates stale handles before reuse;
  `psxe_scene_render` submits active visible meshes only.
- **Tests:** cube integration; focused generation/capacity unit tests are absent.

### [`src/include/psxe/engine.h`](src/include/psxe/engine.h)

Declares callback lifecycle, engine configuration, and the top-level aggregate
of renderer, input, audio, save identity, frame counter, regional tick rate, and
running flag.

### [`src/core/engine.c`](src/core/engine.c)

Owns subsystem initialization order and the perpetual update/render loop.

- **Key functions:** `psxe_engine_init` selects 50/60 ticks and initializes
  renderer → input → audio → card; `psxe_engine_run` calls game lifecycle hooks;
  `psxe_engine_stop` requests exit.
- **Common mistake:** shutdown is only reached after `running` becomes false;
  ordinary console games normally keep running forever.

### [`src/include/psxe/psxe.h`](src/include/psxe/psxe.h)

Convenience umbrella header for game code. Subsystems may include narrower
headers to reduce coupling; do not define behavior here.

### [`samples/cube/CMakeLists.txt`](samples/cube/CMakeLists.txt)

Builds the `psxe_cube` PS-X executable, links `psxe`, and registers the ISO image
target. Add sample-specific embedded assets with PSn00bSDK helpers here.

### [`samples/cube/main.c`](samples/cube/main.c)

Integration sample defining a lit 12-triangle cube, one scene entity, controller
movement, UI diagnostics, and save/load callbacks.

- **Key subparts:** `cube_faces` demonstrates winding/normals/materials;
  `game_update` demonstrates edge/held input and synchronous card operations;
  `game_render` demonstrates scene then UI submission; `main` owns region and
  memory-card identity.
- **Common mistake:** source-defined geometry is only a bootstrap example; new
  content pipelines belong in host cooking tools, not giant production C arrays.

### [`samples/cube/system.cnf`](samples/cube/system.cnf)

PS1 BIOS boot configuration. `BOOT` must exactly match the executable entry in
`iso.xml`; stack placement is target configuration, not host syntax.

### [`samples/cube/iso.xml`](samples/cube/iso.xml)

Defines volume identifiers, boot files, documentation, and padding for
`mkpsxiso`. Paths are configured relative to the sample build directory unless
made source-absolute. Keep disc entry names uppercase 8.3.

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

- **Behavior:** colored/textured flat triangles, optional directional lighting,
  ordering-table depth, affine GPU texturing, dithering, sprites, panels, text.
- **Flow:** game/scene → `psxe_draw_mesh`/2D calls → packet arena → OT → GPU.
- **Gap:** no near clipping, subdivision, Gouraud, depth cue/fog, animation,
  frustum/mesh culling, VRAM allocator, or cooked-asset sample.

### Controllers — Experimental/scaffold

- **Behavior:** two ports, common digital/analog IDs, axes, held/edge masks.
- **Gap:** no mode negotiation, rumble, multitap, dead zones, reconnect policy,
  or hardware matrix.

### Resident VAG effects — Experimental/scaffold

- **Behavior:** validated mono VAG payloads upload linearly and play with
  volume/pan on round-robin hardware voices.
- **Gap:** no cooker, reclaim, priorities, status, buses, spatialization,
  streaming, XA, CD-DA, or loop policy beyond encoded VAG block flags.

### Memory-card saves — Experimental/scaffold

- **Behavior:** one synchronous, one-block file per identity with format version
  and CRC; sample saves cube rotation.
- **Gap:** no asynchronous state machine, card status/free space/list/delete,
  localization/icon asset, journaling, migration callbacks, or physical-card
  validation.

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
- 3D/2D public drawing and textures: `psxe_renderer_*`, `psxe_draw_*`, `psxe_ui_*`
  → [`src/render/render.c`](src/render/render.c).
- Scene handles/render iteration: `psxe_scene_*` →
  [`src/core/scene.c`](src/core/scene.c).
- Controller snapshots: `psxe_input_*`, `psxe_button_*` →
  [`src/platform/input.c`](src/platform/input.c).
- Resident sound: `psxe_audio_*` → [`src/platform/audio.c`](src/platform/audio.c).
- Save persistence: `psxe_memory_card_*` →
  [`src/platform/memory_card.c`](src/platform/memory_card.c).
- Build/package commands: presets → [`CMakePresets.json`](CMakePresets.json),
  wrappers → [`build.sh`](build.sh), [`run.sh`](run.sh).
- Sample disc registry: [`samples/cube/CMakeLists.txt`](samples/cube/CMakeLists.txt)
  and [`samples/cube/iso.xml`](samples/cube/iso.xml).

## Build, Run, Test, Debug, and Release Checklist

Supported and implemented host bootstrap is macOS with Apple Command Line Tools
and Homebrew. Commands are repository-root relative:

```sh
./thirds/setup_macos.sh             # networked, long-running, mutates thirds/ and Homebrew host packages
./thirds/setup_macos.sh --check     # audits installed target dependencies
bash -n build.sh run.sh thirds/setup_macos.sh
./build.sh debug                    # configures, compiles, and creates sample BIN/CUE
./build.sh release
PCSX_REDUX=/path/to/PCSX-Redux ./run.sh debug
git diff --check
```

Direct CMake is valid only after sourcing generated paths:

```sh
source thirds/env.sh
cmake --preset debug
cmake --build --preset debug
```

There is no automated unit, lint, format, serial smoke, emulator screenshot, or
hardware suite yet. The cube sample is the current integration build. A release
claim additionally requires boot/input/audio/save/render checks in accurate
emulators and on original hardware; burning documentation and regional license
data handling are planned, not shipped.

## Test Ownership Map

- Engine/source ABI and all module link dependencies → full `./build.sh debug`.
- Disc manifest/boot-name consistency → `psxe_cube_iso` output from the same
  command, then emulator boot (manual).
- 3D/light/scene/UI and input/save integration →
  [`samples/cube/main.c`](samples/cube/main.c) (manual runtime).
- Script syntax → `bash -n build.sh run.sh thirds/setup_macos.sh`.
- Dependency installation → `./thirds/setup_macos.sh --check`.
- Audio playback → no bundled VAG sample; compile only, explicit known test gap.
- Memory-card corruption/capacity/removal → no focused tests; explicit known
  gap and near-term roadmap item.

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
- Engine static library, callback loop, scene pool, 3D/2D renderer, controller
  snapshots, resident VAG/SPU path, one-block saves, cube EXE and BIN/CUE.

### Known Gaps

- No automated tests, CI, emulator smoke harness, profiling, or original-hardware
  validation.
- Renderer lacks clipping, subdivision, production culling, animation, fog,
  Gouraud shading, asset cooking, and resource allocators.
- Audio and save APIs cover minimal synchronous resident cases only.
- Linux/Windows bootstraps and safe physical-media workflow are not implemented.

### Near-Term Priorities

1. Complete Milestone 0 runtime validation and Milestone 1 diagnostics/tests in
   [`ROADMAP.md`](ROADMAP.md).
2. Build the versioned asset cooker and memory allocators before expanding game
   content APIs.
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
