---
id: ADR-0102
decision: D-06 (amends)
title: A core/base module for vocabulary types
status: accepted
date: 2026-10-13
spec: [§5.5, §6.3, §26.2]
---

# ADR-0102: A core/base module for vocabulary types

## Context

Plan tasks S1-2.1 and S1-2.11 put identifiers and errors in `core/document`, and S1-1 placed the geometry
header in `core/tiles`. Every core module needs these types, but `core/document` depends on `core/tiles`
(raster content is a tile map), so neither module can host types the other needs.

## Decision

- New module `core/base` (target `archmage_base`, namespace `archmage::base`) holds `Expected` and
  `Error` (ADR-0101), typed identifiers (D-16), geometry (moved from `core/tiles`), checked integer
  arithmetic, secure random bytes and SHA-256 content hashing (D-19).
- Every other core module links `archmage_base`; `archmage_base` links only the standard library and
  OpenSSL's libcrypto. It stays Qt-free (ADR-0007).
- `archmage_core` aggregates seven modules: base plus the six of ADR-0006.

## Alternatives

- Keep the plan's placement: forces `core/tiles` to define its own error and identifier types.

## Consequences

- `archmage/tiles/Geometry.h` becomes `archmage/base/Geometry.h`; S1-1 code that used `tiles::DPoint`
  now uses `base::DPoint`.
