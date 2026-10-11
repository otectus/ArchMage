---
id: ADR-0029
decision: D-29
title: Pixel units in the Stage 1 interface
status: accepted
date: 2026-10-11
spec: [§2.3, §11 NAV-03]
---

# ADR-0029: Pixel units in the Stage 1 interface

## Context

Spec §2.3 asks numeric inputs to support units, and NAV-03 covers rulers and unit switching. Stage 1
has no rulers.

## Decision

- The Stage 1 interface uses pixels only.
- The document stores its resolution (default 72 DPI) and a unit preference, so unit switching
  (NAV-03, M2) needs no format change.

## Alternatives

- Unit switching now: deferred to M2 with rulers and guides.

## Consequences

- Printed size is not shown in Stage 1.

## Evidence

None yet. Document fields in S1-2.4; persistence in S1-4.
