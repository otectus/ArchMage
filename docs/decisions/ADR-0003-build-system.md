---
id: ADR-0003
decision: D-03
title: CMake and Ninja driven by CMakePresets.json
status: accepted
date: 2026-10-11
spec: [§5.2, §27.4]
---

# ADR-0003: CMake and Ninja driven by CMakePresets.json

## Context

Spec §5.2 names CMake and Ninja and requires one reproducible entry point per test tier. Neither tool
was installed when Stage 1 was planned.

## Decision

- CMake 3.28 or newer with the Ninja generator. `CMakePresets.json` is the single entry point and
  defines configure, build and test presets with matching names:

  | Preset | Compiler | Build type | Extra |
  | --- | --- | --- | --- |
  | `dev-debug` | GCC | Debug | |
  | `dev-release` | GCC | RelWithDebInfo | |
  | `asan-ubsan` | Clang | Debug | `ARCHMAGE_SANITIZERS=address;undefined` |
  | `tsan` | Clang | Debug | `ARCHMAGE_SANITIZERS=thread` |
  | `fuzz` | Clang | Debug | `ARCHMAGE_ENABLE_FUZZ=ON` plus address and undefined sanitizers |
  | `ci` | GCC | RelWithDebInfo | `ARCHMAGE_WARNINGS_AS_ERRORS=ON` |

- Build trees live in `build/<preset>/`. Test presets fail when a filter selects no tests.

## Alternatives

None evaluated: the specification fixes CMake and Ninja. The plan left the `tsan` and `fuzz` build
types open; S1-0 chose Debug for both so assertions stay active, which S1-5 may revisit for fuzzing
throughput.

## Consequences

- Scripts and documentation refer to presets, never to hand-written `cmake` flag lists.
- The sanitizer presets require Clang's runtime libraries (package `compiler-rt`).
- C++20 module dependency scanning is off (`CMAKE_CXX_SCAN_FOR_MODULES OFF`): ArchMage uses no
  modules, and the scan put GCC-only flags into `compile_commands.json`, which clang-tidy rejects.

## Evidence

- Recorded in `docs/implementation-status.md` under S1-0: configure, build and test runs per preset.
