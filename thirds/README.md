# Third-party toolchain

This directory owns everything needed to compile a game and produce a burnable
PlayStation BIN/CUE image. Generated dependencies remain local and ignored:

- `toolchain/` — `mipsel-none-elf` binutils and GCC;
- `psn00bsdk/` — installed PSn00bSDK libraries, asset tools, `mkpsxiso`, and
  `dumpsxiso`;
- `source/`, `build/`, and `cache/` — reproducible inputs and working trees;
- `env.sh` — generated paths consumed by the root build script.

On macOS, run:

```sh
./thirds/setup_macos.sh
./build.sh
```

The bootstrap pins source versions and verifies GNU archive checksums. It uses
Homebrew only for host build dependencies; target binaries and the SDK install
under this directory. `./thirds/setup_macos.sh --check` performs a read-only
installation audit. Linux and Windows bootstrap scripts are planned in
[`ROADMAP.md`](../ROADMAP.md).
