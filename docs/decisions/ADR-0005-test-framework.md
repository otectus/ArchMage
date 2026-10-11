---
id: ADR-0005
decision: D-05
title: GoogleTest through CTest, Qt Test offscreen, labeled tiers
status: accepted
date: 2026-10-11
spec: [§5.2, §27.1]
---

# ADR-0005: GoogleTest through CTest, Qt Test offscreen, labeled tiers

## Context

Spec §5.2 asks for CTest plus a selected C++ test framework, Qt interaction tests and fuzzing, with
one reproducible entry point per test tier. Spec §27.1 defines nine test layers. GoogleTest 1.18 is
installed.

## Decision

- GoogleTest and GoogleMock through CTest; each `TEST()` is discovered as its own CTest test.
- Qt Test for UI interaction tests, always on the `offscreen` platform so no display is needed.
- CTest labels name the automated tiers: `math`, `operation`, `document`, `image`, `ui`,
  `interchange`, `resilience`, `perf`. The tier entry point is `ctest --preset <preset> -L <label>`.
  `archmage_add_test()` in `tests/CMakeLists.txt` rejects any other label.
- libFuzzer targets exist only under the `fuzz` preset.

## Alternatives

- Catch2: listed by the plan as not needed in Stage 1; GoogleTest was already installed and offers
  GoogleMock for the command and job seams.

## Consequences

- Tier 9 of spec §27.1 (human validation) has no label; it is recorded manually.
- `perf` tests are records, not a gate, and `scripts/check.sh` excludes them along with `ui`, which
  `scripts/check-ui.sh` runs.

## Evidence

- S1-0 registers `Version.*` (document), `SmokeTest` (ui) and `cli.version` (interchange); results
  are in `docs/implementation-status.md`.
