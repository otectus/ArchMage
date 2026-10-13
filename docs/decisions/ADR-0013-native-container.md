---
id: ADR-0013
decision: D-13
title: SQLite native container with schema major 0 until the M2 exit
status: accepted
date: 2026-10-11
spec: [§10.1, §10.3, §10.5]
---

# ADR-0013: SQLite native container with schema major 0 until the M2 exit

## Context

Spec §10.1 proposes `.archmage` as a versioned SQLite container with eight logical tables, and says
to benchmark indices, migration steps, blob limits and compaction before freezing schema version 1.
Spec §10.5 requires explicit migration steps with fixtures from every released schema and read-only
opening of files that need newer capabilities.

## Decision

- `.archmage` is a SQLite database implementing the logical tables `manifest`, `objects`,
  `relations`, `resources`, `tile_maps`, `revisions`, `assets`, `previews` as a first physical schema.
- Stage 1 writes `schema_major = 0` with a `schema_minor` that increments on every change. A migration
  framework with an ordered step registry exists, and a test proves every Stage 1 minor still opens.
- Schema major 1 is frozen at the M2 exit, after the benchmarks.
- Files whose required major is newer open read-only with the stored composite preview.

## Alternatives

- Freezing major 1 in Stage 1: rejected; §10.1 requires the benchmarks first.
- A custom binary container: rejected by §5.2 and §10.1.

## Consequences

- Early `.archmage` files are migration-supported across Stage 1 minors only (plan Q-07, default).

## Evidence

Schema 0.1 in `io/native/schema/0001.sql`, described in `docs/architecture/native-format.md` (S1-4).
`NativeSchema` and `NativeFile` tests in `tests/io/native/NativeFormatTest.cpp`; the 0.1 fixture
`tests/io/native/fixtures/v0.1.archmage` opens in `NativeMigrationTest`. Amended by ADR-0103.
