#!/usr/bin/env bash
# Builds and runs the host unit tests (ASan + UBSan). No hardware needed.
#   tests/host/run.sh [build dir]   (default ~/.cache/ds5dongle/host-tests)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD="${1:-${XDG_CACHE_HOME:-$HOME/.cache}/ds5dongle/host-tests}"
GEN=()
command -v ninja >/dev/null && GEN=(-G Ninja)
cmake -S "$HERE" -B "$BUILD" "${GEN[@]}" -DCMAKE_BUILD_TYPE=Debug >/dev/null
cmake --build "$BUILD"
"$BUILD/host_tests"
