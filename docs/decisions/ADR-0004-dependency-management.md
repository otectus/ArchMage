---
id: ADR-0004
decision: D-04
title: System packages now, manifest-pinned, imported targets only
status: accepted
date: 2026-10-11
spec: [§5.2, §5.3]
---

# ADR-0004: System packages now, manifest-pinned, imported targets only

## Context

Spec §5.2 asks for a pinned dependency manifest; §5.3 lists the fields each entry records. Stage 1
builds only on Linux (ADR-0021), where every library is available from pacman.

## Decision

- Stage 1 uses system packages, located through CMake package configs or find modules, with
  pkg-config imported targets for libraries that ship no usable CMake config (Little CMS through
  `cmake/FindLCMS2.cmake`; xxHash, HarfBuzz, FreeType and LibRaw through `pkg_check_modules`).
- CMake code links imported targets only and never assumes pacman paths.
- `docs/dependencies/manifest.yaml` records every dependency: source, pinned version, checksum,
  build options, license, redistribution obligations, maintenance status and affected features.
  `tests/docs/manifest_test.py` fails when a CMake lookup has no entry.
- A vcpkg manifest is deferred until the first Windows or macOS build is attempted.

## Alternatives

- vcpkg from day one (plan Q-06). Not chosen: no non-Linux build exists to benefit, and it would
  duplicate packages that pacman already provides. Q-06 remains open; this ADR is the stated default.
- Vendoring sources: rejected; vendoring is excluded for S1-0 by the plan's handoff brief.

## Consequences

- A rolling-distribution upgrade can change a dependency under the build; the manifest pins the
  exact package versions and `manifest_test.py --check-installed` reports drift.
- The checksum of a system package is the SHA-256 of its package archive as listed in the pacman
  sync database and confirmed against the cached archive.

## Evidence

- `docs/dependencies/manifest.yaml` (S1-0.8) and the manifest test results in
  `docs/implementation-status.md`.
