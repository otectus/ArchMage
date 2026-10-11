---
id: ADR-0026
decision: D-26
title: One resource coordinator with RAM, VRAM, scratch and history budgets
status: accepted
date: 2026-10-11
spec: [§7.1, §25.4, §30]
---

# ADR-0026: One resource coordinator with RAM, VRAM, scratch and history budgets

## Context

Spec §7.1 requires separate RAM, VRAM, scratch-space and history budgets under one resource
coordinator, evicting disposable caches before dirty data, spooling authoritative dirty tiles before
reclaiming them, and a reserved memory pool. Spec §25.4 starts the editing RAM budget near half of
physical memory.

## Decision

- `core/tiles/ResourceCoordinator` holds the four budgets.
- Stage 1 enforces RAM (default half of physical memory) by evicting disposable caches first (mips,
  thumbnails), then spooling authoritative tiles that are already journaled (ADR-0014); never dirty
  tiles that are not yet journaled.
- VRAM and scratch are accounted; only the experimental GPU path's VRAM is bounded.
- A reserved 64 MiB pool stays available for error, save and recovery paths.

## Alternatives

- No eviction in Stage 1: rejected; bounded caches are a §1.2 non-negotiable and §30 places the
  coordinator in the M1 foundation.

## Consequences

- Residency pins per transaction and job prevent evicting tiles in use (plan §6.1 risk table).

## Evidence

None yet. Coordinator in S1-2.3; spool-to-journal eviction in S1-5.4.
