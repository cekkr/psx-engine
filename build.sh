#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
env_file="${repo_root}/thirds/env.sh"
preset="${1:-debug}"

if [[ ! -f "${env_file}" ]]; then
    echo "Missing ${env_file}. Run ./thirds/setup_macos.sh first." >&2
    exit 1
fi

# shellcheck source=/dev/null
source "${env_file}"
cmake --preset "${preset}" -S "${repo_root}"
cmake --build --preset "${preset}"

echo "Disc image: ${repo_root}/build/${preset}/samples/cube/psxe_cube.cue"
