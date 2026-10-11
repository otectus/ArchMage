---
id: ADR-0006
decision: D-06
title: Repository layout and module targets
status: accepted
date: 2026-10-11
spec: [§5.5]
---

# ADR-0006: Repository layout and module targets

## Context

Spec §5.5 proposes a repository layout and says to honor an existing repository's conventions. No
repository existed: the project directory held only `docs/`.

## Decision

- The §5.5 layout is used verbatim for the Stage 1 directories: `apps/archmage`, `apps/archmage-cli`,
  `core/{document,commands,tiles,render,color,brush}`, `io/{native,formats}`, `ui`, `resources`,
  `tests`, `benchmarks`, `fuzz`, `docs`, `packaging`, `cmake`, plus `scripts`. Reserved directories
  (`core/vector`, `core/text`, `core/timeline`, `io/psd`, `services`, `sdk`) are created when used.
- Each module directory has `include/archmage/<module>/` for public headers and `src/` for code,
  namespace `archmage::<module>`, and a target `archmage_<module>` made by `archmage_add_module()`
  (`cmake/ArchMageModules.cmake`). A module without sources is an INTERFACE target until its first
  source file lands.
- Aggregates: `archmage_core` (the six core modules) and `archmage_io` (`archmage_native`,
  `archmage_formats`).

## Alternatives

None: the layout is specified, and there were no existing conventions to preserve.

## Consequences

- The plan's handoff brief described `archmage_io` as an empty static library. CMake cannot build a
  static library without sources, so S1-0 made it an INTERFACE aggregate instead; the io modules
  become static libraries when S1-4 and S1-8 add code.

## Evidence

- `cmake --build --preset dev-debug` builds all targets (`docs/implementation-status.md`, S1-0).
