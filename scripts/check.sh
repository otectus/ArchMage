#!/usr/bin/env bash
# Main gate (plan D-22, S1-0.7): configure and build dev-debug, run every test tier except
# ui (scripts/check-ui.sh) and perf (records, not a gate), then the repository consistency
# checks. Prints each command before running it.
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
run ctest --preset dev-debug --label-exclude '^(ui|perf)$'
run python3 -I tests/docs/manifest_test.py
run python3 -I scripts/parity_report.py --check
