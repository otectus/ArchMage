#!/usr/bin/env bash
# UI tier (plan D-05, S1-0.7): offscreen Qt tests. DISPLAY and WAYLAND_DISPLAY are removed,
# so a pass shows the tier needs no display server. Prints each command before running it.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

run() {
    printf '+'
    printf ' %q' "$@"
    printf '\n'
    "$@"
}

run cmake --preset dev-debug
run cmake --build --preset dev-debug
run env -u DISPLAY -u WAYLAND_DISPLAY QT_QPA_PLATFORM=offscreen \
    ctest --preset dev-debug --label-regex '^ui$'
