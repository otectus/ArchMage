---
id: ADR-0017
decision: D-17
title: int64 tile coordinates, double geometry, one view transform
status: accepted
date: 2026-10-11
spec: [§6.3]
---

# ADR-0017: int64 tile coordinates, double geometry, one view transform

## Context

Spec §6.3 requires signed 64-bit integers for tile addressing and extents, checked allocation
arithmetic, double precision for document geometry, half-open rectangles, a documented pixel-center
convention, and one central set of coordinate mappings that tools do not reimplement.

## Decision

- `int64_t` tile and extent coordinates; checked multiplication for allocation sizes.
- `double` document geometry; half-open `IRect` and `DRect`; pixel centers at +0.5.
- One `ViewTransform` type owns zoom, pan and DPI mapping; tools receive document coordinates only.

## Alternatives

- 32-bit coordinates: rejected by §6.3 and the 300,000-pixel-per-axis design target.

## Consequences

- Negative layer positions are valid everywhere; tests cover offsets such as (−37, 59).

## Evidence

None yet. `ViewTransform` in S1-1.5; rectangles and tile addressing in S1-2.2.
