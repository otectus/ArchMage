#!/usr/bin/env bash
# Sanitizer gate (plan D-03, S1-0.7): Clang AddressSanitizer and UndefinedBehaviorSanitizer
# build plus the full test suite, UI tier included (offscreen, no display server). The
# asan-ubsan test preset supplies ASAN_OPTIONS and UBSAN_OPTIONS. Prints each command first.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

run() {
    printf '+'
    printf ' %q' "$@"
    printf '\n'
    "$@"
}

run cmake --preset asan-ubsan
run cmake --build --preset asan-ubsan
run env -u DISPLAY -u WAYLAND_DISPLAY ctest --preset asan-ubsan
