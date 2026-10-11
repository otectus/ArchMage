---
id: ADR-0028
decision: D-28
title: One dark theme with a violet accent and original icons for shipped tools
status: accepted
date: 2026-10-11
spec: [§2.3]
---

# ADR-0028: One dark theme with a violet accent and original icons for shipped tools

## Context

Spec §2.3 asks for neutral dark and light themes with a high-contrast option, a restrained violet
accent, original icons with a coherent stroke weight, and a tooltip, accessible label and shortcut
for every icon-only command.

## Decision

- Stage 1 ships one dark theme with the violet accent through `QPalette` plus a small stylesheet.
  Light and high-contrast themes come in M2 or later.
- Original SVG icons only for shipped tools and panel actions (brush, eraser, marquee rectangle,
  marquee ellipse, hand, zoom, eyedropper; add, remove and duplicate layer; visibility; lock), with
  consistent stroke weight, in `resources/icons/`.
- Every icon-only control has a tooltip, accessible name and shortcut text.

## Alternatives

- The full theme set now: deferred (plan §1.7, Q-10).

## Consequences

- No icon artwork is created before S1-7.

## Evidence

None yet. Theme and icons in S1-7.8.
