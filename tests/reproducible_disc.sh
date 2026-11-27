#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
preset="${1:-release}"
if [[ ! -f "${root}/thirds/env.sh" ]]; then
    echo "Missing target toolchain environment. Run ./thirds/setup_macos.sh first." >&2
    exit 1
fi
# shellcheck source=/dev/null
source "${root}/thirds/env.sh"

build_dir="$(mktemp -d "${TMPDIR:-/tmp}/psxe-disc.XXXXXXXX")"
trap 'rm -rf "${build_dir}"' EXIT

build_disc() {
    local build_path="$1"
    cmake --preset "${preset}" -S "${root}" -B "${build_path}"
    cmake --build "${build_path}" --target psxe_cube_iso psxe_carrot_iso
}

TZ=UTC build_disc "${build_dir}/first"
TZ=JST-9 build_disc "${build_dir}/second"
for sample in cube carrot; do
    for extension in exe bin cue; do
        disc_file="samples/${sample}/psxe_${sample}.${extension}"
        cmp "${build_dir}/first/${disc_file}" "${build_dir}/second/${disc_file}"
    done
done
echo "Independent ${preset} builds produced identical cube and carrot PS-X EXE, BIN and CUE files."
