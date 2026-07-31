# psx-engine roadmap

This roadmap is the authority for planned work. A checkbox does not imply that
an API is stable, optimized, or hardware-proven. Current behavior is documented
in [`README.md`](README.md) and enforced by the checked-in source.

## Direction and constraints

The engine targets original PlayStation hardware first. A host previewer may be
added, but it must consume the same cooked assets and must not redefine target
behavior.

- Keep frame-time work deterministic and allocation-free by default. Use named,
  measurable arenas for scene, packet, streaming, VRAM, and SPU allocations.
- Prefer data-oriented C99 and explicit ownership over a deep object hierarchy.
- Let games tune capacity at compile time; report exhaustion without corrupting
  adjacent memory.
- Treat 2 MiB main RAM, 1 MiB VRAM, 512 KiB SPU RAM, CD seek latency, and GPU
  ordering-table behavior as design inputs, not late optimization problems.
- Put reusable runtime code in `src/`, runnable projects in `samples/`, host
  conversion tools in `tools/`, and downloaded/installed dependencies in
  `thirds/`.
- Cook common source formats into small target-native formats. The runtime must
  not parse OBJ, glTF, PNG, WAV, or editor metadata.
- Preserve regional video timing. Simulation APIs must state whether a quantity
  is per-tick, fixed-point per-second, or wall-clock derived.
- Keep disc builds deterministic: pin tools, record asset order, expose logical
  block addresses (LBAs), and make streaming layout inspectable.
- Validate risky behavior in at least PCSX-Redux and DuckStation, then on real
  NTSC and PAL hardware before calling it shipped.

## Milestone 0 — foundation (present, experimental)

- [x] CMake/PSn00bSDK static engine library with code under `src/`.
- [x] macOS bootstrap for pinned MIPS GCC, PSn00bSDK, and `mkpsxiso` under
  `thirds/`.
- [x] BIN/CUE packaging and a runnable sample under `samples/`.
- [x] Fixed-capacity engine loop, scene entities, GTE triangle renderer,
  ordering table, colored/textured material path, directional lighting,
  dithering, sprites, panels, and debug text.
- [x] Two-port pad snapshots with held/pressed/released states and analog axes.
- [x] In-memory VAG upload and 24-channel SPU playback.
- [x] Versioned, CRC-protected, one-block memory-card save/load API.
- [ ] Run emulator regression captures and original-hardware tests for every
  shipped-looking feature above; until then this milestone remains experimental.

## Milestone 1 — correctness, diagnostics, and portability

- [ ] Add host-unit-testable modules for handle generations, CRC/save headers,
  fixed-point utilities, asset parsers, and capacity failure paths.
- [ ] Add a PCSX-Redux smoke harness that boots the sample, waits a bounded
  number of frames, captures serial output/framebuffer, and fails on engine
  assertions or packet overflow.
- [ ] Add on-screen and serial instrumentation for CPU ticks, GPU sync time,
  primitive counts, rejected faces, arena high-water marks, CD reads, VRAM, and
  SPU allocation.
- [ ] Define debug assertions, panic screen, symbol-map workflow, and release
  logging policy.
- [ ] Add CI for target compilation, BIN/CUE creation, Markdown links, script
  syntax, and deterministic rebuild checks.
- [ ] Implement `thirds/setup_linux.sh` for Debian/Ubuntu first, then package
  mappings for Fedora and Arch.
- [ ] Implement Windows bootstrap through native MSYS2 or a documented WSL2
  path; add PowerShell wrappers equivalent to `build.sh` and `run.sh`.
- [ ] Test Apple Silicon and Intel macOS, then record minimum supported host
  versions rather than inferring them.

Exit gate: one command produces the same sample disc on every supported host,
automated emulator smoke tests pass, and capacity failures are observable.

## Milestone 2 — asset cooking and memory layout

- [ ] Add a manifest-driven `tools/psxe-cook` host executable with dependency
  tracking and stable content hashes.
- [ ] Convert triangulated OBJ and glTF meshes into a versioned PSXE mesh format
  containing quantized vertices, normals, UVs, material ranges, bounds, and
  optional subdivision hints.
- [ ] Add a Blender exporter for named nodes, collision meshes, sockets, vertex
  colors, rigid skin assignments, animation clips, cameras, and scene metadata.
- [ ] Convert PNG/TGA images to 4/8/16-bpp TIM with palette quantization,
  transparency rules, dithering preview, texture-page placement, and atlas
  diagnostics.
- [ ] Convert WAV/AIFF sound effects to validated mono VAG, including loop flags;
  prepare XA-ADPCM and CD-DA tracks with explicit channel/rate constraints.
- [ ] Generate C/assembly `incbin` bindings for resident assets and ISO XML/LBA
  manifests for streamed assets.
- [ ] Implement bounded main-RAM arenas plus explicit VRAM and SPU-RAM allocators
  with alignment, lifetimes, fragmentation diagnostics, and reset points.
- [ ] Document binary formats and add version checks, size limits, corrupted
  input tests, and migration policy before formats are declared stable.

Exit gate: a Blender-authored textured scene plus audio cooks reproducibly and
loads without runtime parsing of source formats.

## Milestone 3 — production 3D and 2D rendering

- [ ] Add camera frustum rejection, mesh bounds, material batching, configurable
  ordering-table ranges/layers, and packet-budget reservation.
- [ ] Clip triangles against the near plane and viewport; add optional adaptive
  subdivision for large affine-textured polygons to reduce warping and popping.
- [ ] Add flat/Gouraud vertex color and lighting, multiple directional lights,
  ambient light, GTE depth cueing/fog, emissive/unlit materials, and selectable
  semi-transparency modes.
- [ ] Add textured quads, billboards, nine-slice UI, tile maps, sprite sheets,
  sprite animation, particles, trails, decals, sky/background layers, and
  render-to-texture where budgets permit.
- [ ] Add rigid-part and keyframed vertex animation first; evaluate limited
  skinning only after profiling memory and GTE cost.
- [ ] Add LOD groups, portal/room visibility, occlusion hints, and offline
  ordering/subdivision tools for large environments.
- [ ] Establish representative render benchmarks and budgets for NTSC/PAL,
  textured/lit geometry, particles, and UI-heavy scenes.

Exit gate: indoor and outdoor reference scenes hold their declared frame budget
without packet overflow on original hardware.

## Milestone 4 — game runtime, worlds, and collision

- [ ] Replace the minimal render-only entity record with opt-in component pools,
  stable handles, tags/layers, lifecycle events, and deterministic iteration.
- [ ] Add scene/prefab serialization, additive rooms, transition state, persistent
  game state, and a loading-screen contract.
- [ ] Implement asynchronous CD reads, sector-aligned bundles, room/asset
  residency, prefetching, cancellation, seek scheduling, and LBA-aware packing.
- [ ] Add executable/data overlays for games that exceed resident RAM, with a
  link-map and ownership model that prevents dangling pointers across swaps.
- [ ] Add broad-phase spatial partitioning and simple production collision:
  raycasts, spheres, AABBs, capsules, triangle meshes, triggers, swept movement,
  and character grounding. Full rigid-body physics is not an initial goal.
- [ ] Add deterministic timers, coroutines/state machines, event queues, seeded
  random streams, and pause/time-scale policies.
- [ ] Evaluate a tiny scripting VM only after profiling; native C callbacks
  remain supported and scripting must have bounded memory/instruction budgets.

Exit gate: a multi-room playable vertical slice streams content, collides,
changes scenes, and survives repeated transitions within RAM budgets.

## Milestone 5 — audio, controllers, saves, and complete-game services

- [ ] Add priority-based voice allocation, completion tracking, buses, master and
  per-bus volume, pitch, pan, simple distance attenuation, and optional reverb.
- [ ] Stream long-form XA music/voices from CD and support CD-DA tracks without
  starving scene streaming; document layout tradeoffs.
- [ ] Add DualShock mode negotiation, analog dead zones, rumble, reconnect,
  remapping, multitap/four-player support, and controller-type capability APIs.
- [ ] Replace blocking save calls with an explicit state machine, progress UI,
  cancellation boundaries, card-change detection, free-space/list/delete APIs,
  backup/journal strategy, and robust full/no-card/unformatted responses.
- [ ] Support multiple save blocks, BIOS-valid localized titles/icons, animated
  icons, settings/profile slots, migration callbacks, and corruption recovery.
- [ ] Add UI focus/navigation, menus, modal dialogs, text layout, proportional
  fonts, glyph cooking, localization, safe-area handling, and accessibility
  options feasible on target hardware.
- [ ] Add standard services for boot flow, title screen, pause, options, loading,
  credits, attract mode, soft reset, and region/video selection.

Exit gate: the vertical slice supports a complete boot-to-credits loop, music
and effects, controller reconnect, and fault-tolerant saves on hardware.

## Milestone 6 — authoring, release, and physical media

- [ ] Add a project generator and sample templates for 2D, fixed-camera 3D,
  third-person 3D, and streamed-room games.
- [ ] Add live serial asset/executable upload, memory inspection, frame capture,
  symbolized crashes, and documented PCSX-Redux GDB workflows.
- [ ] Build an editor/preview path only on top of versioned cooked formats and
  runtime-compatible scene rules.
- [ ] Add ISO validation, root-entry/directory limits, LBA reports, regional
  license-data injection hooks (never distribute proprietary license data),
  XA/CD-DA track verification, and reproducible release manifests.
- [ ] Document safe CD-R authoring with maintained macOS/Linux/Windows tools,
  media/speed caveats, checksums, emulator validation, and original-hardware
  acceptance tests.
- [ ] Define API/asset-format compatibility, semantic releases, changelog,
  migration guides, downstream licensing notes, and a curated hardware test
  matrix.

Exit gate: a fresh machine can build, test, package, verify, and document a
release candidate ready for emulator distribution or lawful physical media.

## Deferred unless a game proves the need

- General rigid-body dynamics, arbitrary runtime mesh import, a heavyweight STL
  layer, a full visual scripting editor, and modern physically based rendering.
- A bundled emulator or proprietary BIOS/license material. Development tooling
  integrates with external emulators and user-supplied lawful hardware data.
- Network services. The original target has no standard network interface; any
  accessory-specific support belongs in an isolated optional module.
