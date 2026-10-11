---
id: ADR-0016
decision: D-16
title: Random 128-bit typed identifiers
status: accepted
date: 2026-10-11
spec: [§6.1]
---

# ADR-0016: Random 128-bit typed identifiers

## Context

Spec §6.1 requires stable identifiers for documents, objects, layers, masks, effects, resources,
frames, commands and revisions, and keeps names separate from references.

## Decision

- 128-bit random IDs from a CSPRNG, serialized as `<type>-<26-character Crockford base32>`.
- `type` is one of `doc`, `layer`, `raster`, `res`, `rev`, `tx`, `req`, `sess`, `job`.
- Names are separate fields and never used as references.

## Alternatives

- Sequential integers: collide across documents and merges, which cross-document paste and
  recovery need to avoid.
- Names as references: rejected by §6.1.

## Consequences

- IDs are opaque; ordering comes from explicit order fields, not from ID values.

## Evidence

None yet. Implemented in S1-2.1 with a 1e6-draw collision test.
