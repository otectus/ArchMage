---
id: ADR-0008
decision: D-08
title: 128 × 128 channel-planar tiles with compact uniform tiles
status: accepted
date: 2026-10-11
spec: [§6.4, §7.1]
---

# ADR-0008: 128 × 128 channel-planar tiles with compact uniform tiles

## Context

Spec §7.1 asks for a benchmarkable 128 × 128 starting tile size recorded in the format, lazy
allocation, compact empty and uniform tiles, and planar or interleaved layouts behind a stable tile
interface. Spec §6.4 warns against hardwiring content to one RGBA bitmap.

## Decision

- 128 × 128 tiles; the tile size is recorded in the document manifest.
- Channel-planar storage behind the tile interface; an interleaved layout can be added behind the
  same interface later.
- Sample types `uint8`, `uint16`, `float16`, `float32` exist in the type system; Stage 1 exercises
  `uint8` and `float32` end to end and only compiles the others.
- A uniform tile, including a fully transparent one, is stored as a single sample value.

## Alternatives

- Interleaved RGBA: simpler for 8-bit RGBA painting, but awkward for masks, N-channel data and GPU
  upload, which the later domains need.
- 64 or 256 pixel tiles: possible after benchmarking; the size is a recorded parameter.

## Consequences

- Kernels address planes, not packed pixels; conversion to interleaved form happens at export and
  presentation boundaries.

## Evidence

None yet. Implemented and tested in S1-2 (S1-2.2); memory behavior measured in S1-5.4 and S1-6.8.
