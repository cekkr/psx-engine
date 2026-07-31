# psx-engine

`psx-engine` is a compact C99 game-engine foundation for the original Sony
PlayStation. It builds real MIPS executables and BIN/CUE disc images through
[PSn00bSDK](https://github.com/Lameguy64/PSn00bSDK); it is not a modern renderer
that merely imitates the PS1 look.

The current milestone supplies the reusable frame loop and the low-level pieces
needed to grow complete games without hiding the console's constraints:

- fixed-capacity scene entities and transforms;
- GTE-accelerated triangle projection, back-face rejection, directional
  lighting, affine textured or colored materials, ordering-table depth sorting,
  double buffering, and GPU dithering;
- 2D sprites, translucent panels, and debug-font UI in the same display list;
- edge-triggered digital and analog-controller state for two ports;
- VAG upload to SPU RAM and round-robin playback across 24 hardware voices;
- one-block, CRC-protected, versioned memory-card saves;
- a self-contained cube sample packaged as an emulator- and CD-ready BIN/CUE.

The APIs and save format are pre-release. Streaming, asset cooking, animation,
collision, XA/CD-DA music, richer UI, and hardware validation are intentionally
tracked as future work in [`ROADMAP.md`](ROADMAP.md).

## Quick start on macOS

Prerequisites are Apple Command Line Tools and
[Homebrew](https://brew.sh). The bootstrap builds target tools locally in
`thirds/`; it does not install a target compiler or SDK system-wide.

```sh
./thirds/setup_macos.sh
./build.sh
```

The first command downloads pinned GNU sources, builds a
`mipsel-none-elf` GCC toolchain, builds PSn00bSDK and `mkpsxiso`, and generates
`thirds/env.sh`. The first run is intentionally substantial. Later runs reuse
the local cache and build outputs.

Build configurations are selected by the first argument:

```sh
./build.sh debug
./build.sh release
```

The sample disc is written to
`build/<preset>/samples/cube/psxe_cube.cue` plus its matching `.bin` data track.
To launch it with [PCSX-Redux](https://github.com/grumpycoders/pcsx-redux):

```sh
./run.sh debug
```

`run.sh` finds `pcsx-redux` on `PATH` or in `/Applications`, or accepts an
explicit executable through `PCSX_REDUX`.

## Cube sample

[`samples/cube/main.c`](samples/cube/main.c) is both a starting project and an
integration example.

- D-pad left/right moves the cube laterally.
- D-pad up/down changes its depth.
- Start writes its rotation to memory-card slot 1.
- Select restores the saved rotation.

The sample deliberately uses source-defined geometry so the first build has no
asset-conversion prerequisite. Add asset cooking before treating that approach
as a content workflow.

## Programming model

A game supplies `init`, `update`, `render`, and optional `shutdown` callbacks to
`psxe_engine_run`. Update cadence is one callback per video frame: 60 Hz for
NTSC and 50 Hz for PAL. Rendering is submitted between the engine's frame-list
begin/end calls.

```c
#include <psxe/psxe.h>

static void update(psxe_engine *engine, void *data) {
    const psxe_pad_state *pad = psxe_input_pad(&engine->input, 0);
    if (psxe_button_pressed(pad, PAD_CROSS)) {
        /* React once to the press. */
    }
}
```

All hot-path storage is statically sized. Override `PSXE_OT_LENGTH`,
`PSXE_PACKET_BUFFER_SIZE`, or `PSXE_MAX_ENTITIES` at compile time when a game
has measured a different requirement. When the primitive arena fills, drawing
is dropped and counted in `psxe_renderer.dropped_primitives`; memory is never
overwritten.

## Layout

- [`src/`](src/) contains the engine public headers and implementation.
- [`samples/`](samples/) contains independently packaged sample games.
- [`thirds/`](thirds/) contains the reproducible dependency bootstrap and owns
  all generated target libraries, SDK tools, and compiler files.
- [`ROADMAP.md`](ROADMAP.md) separates current scaffolding from the work needed
  for production-sized games.
- [`AGENTS.md`](AGENTS.md) is the operational and architecture reference for
  contributors and coding agents.

## Design influences

The renderer embraces the traits explained in David Colson's
[PS1-style renderer article](https://www.david-colson.com/2021/11/30/ps1-style-renderer.html):
integer vertex projection, pixel-grid motion, affine texture mapping, per-vertex
or per-face lighting, and depth ordered primitives. On real PS1 hardware those
are native GPU/GTE behaviors rather than post-processing effects.

Repository organization and longer-term direction also draw from:

- [PSoXide](https://github.com/EBonura/PSoXide) for an end-to-end author/cook/build/test pipeline;
- [OmegaTech](https://github.com/paganswrath/OmegaTech) for a small approachable game-facing API;
- [3dcam-headers](https://github.com/ABelliqueux/3dcam-headers) for native 3D, camera, sound, and Blender-oriented workflows;
- [Tundra](https://github.com/maltebp/tundra) for fixed-point transforms, components, asset compilation, sprites, input, sound, and tests.

No source was copied from those projects. PSn00bSDK remains a third-party
dependency with its own license and provenance; review its documentation before
distributing SDK-derived binaries or code.

## Status and safety notes

This is an experimental foundation, not a production-ready complete engine.
The target build and disc-image workflow is automated, but original-hardware
testing is still required. In particular, memory-card operations are synchronous
and must be tested against physical cards before a game release. Keep saved data
small, never remove a card during I/O, and always surface the returned status to
the player.

The repository is CC0. Third-party code downloaded under `thirds/` is governed
by its respective upstream license.
