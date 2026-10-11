---
id: ADR-0020
decision: D-20
title: Selections as sparse float32 coverage fields
status: accepted
date: 2026-10-11
spec: [§6.2, §12.1]
---

# ADR-0020: Selections as sparse float32 coverage fields

## Context

Spec §12.1 defines selections as coverage fields that preserve fractional coverage, with marching
ants as visualization only, and requires combination math that does not harden feathered edges.
Spec §6.2 keeps selection distinct from document state while undo restores the editing context.

## Decision

- A single-channel `float32` sparse coverage field over document space, with antialiased ellipse
  rasterization.
- Combination: add is `max(a, b)`, subtract is `a × (1 − b)`, plus replace and invert.
- Selection changes are session editing state, undoable through a selection command kind recorded
  in history without bumping the document revision.
- Not persisted in Stage 1; named selections are SEL-07 (M3).

## Alternatives

- Binary masks or polygon outlines: rejected by §12.1.

## Consequences

- Brush and fill clip by multiplying with the coverage value (PNT-05 subset in S1-8.4).

## Evidence

None yet. Implemented in S1-8.1 and S1-8.2.
