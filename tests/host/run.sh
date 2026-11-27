#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
sdk_include="${PSXE_SDK_INCLUDE:-${root}/thirds/psn00bsdk/include/libpsn00b}"
if [[ ! -f "${sdk_include}/psxgte.h" || ! -f "${sdk_include}/psxgpu.h" ]]; then
    echo "Set PSXE_SDK_INCLUDE to the pinned PSn00bSDK public header directory." >&2
    exit 1
fi

build_dir="$(mktemp -d "${TMPDIR:-/tmp}/psxe-host.XXXXXXXX")"
trap 'rm -rf "${build_dir}"' EXIT

# Scene and object code also define drawing functions. Discard those sections
# so the host binary only links CPU code exercised by these tests.
case "$(uname -s)" in
    Darwin) strip_unused=-Wl,-dead_strip ;;
    *) strip_unused=-Wl,--gc-sections ;;
esac
sanitizers=()
if [[ "${PSXE_SANITIZE:-0}" == 1 ]]; then
    sanitizers=(-fsanitize=address,undefined -fno-sanitize-recover=all)
fi

cd "${root}"
"${CC:-cc}" -std=c99 -Wall -Wextra -Wshadow -Wconversion -Werror \
    -g -O1 -ffunction-sections -fdata-sections "${strip_unused}" \
    ${sanitizers[@]+"${sanitizers[@]}"} \
    -I src/include -I src -idirafter "${sdk_include}" \
    tests/host/runtime.c src/core/{animation,asset_formats,fixed,memory,save_format,scene}.c \
    src/render/{object,packet}.c -o "${build_dir}/runtime"
"${build_dir}/runtime"
