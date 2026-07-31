#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
preset="${1:-debug}"
image="${repo_root}/build/${preset}/samples/cube/psxe_cube.cue"

if [[ ! -f "${image}" ]]; then
    "${repo_root}/build.sh" "${preset}"
fi

if [[ -n "${PCSX_REDUX:-}" ]]; then
    emulator="${PCSX_REDUX}"
elif command -v pcsx-redux >/dev/null 2>&1; then
    emulator="$(command -v pcsx-redux)"
elif [[ -x "/Applications/PCSX-Redux.app/Contents/MacOS/PCSX-Redux" ]]; then
    emulator="/Applications/PCSX-Redux.app/Contents/MacOS/PCSX-Redux"
else
    echo "PCSX-Redux was not found. Set PCSX_REDUX to its executable path." >&2
    exit 1
fi

exec "${emulator}" -run -iso "${image}"
