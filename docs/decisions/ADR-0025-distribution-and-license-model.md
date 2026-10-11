---
id: ADR-0025
decision: D-25
title: Distribution and license model left to the user; no path foreclosed
status: accepted (interim policy; the model itself is open, plan Q-02)
date: 2026-10-11
spec: [§5.3, §30]
---

# ADR-0025: Distribution and license model left to the user; no path foreclosed

## Context

Spec §5.3 asks to decide the application's distribution and license model before adopting
dependencies that would constrain it, while allowing routine development with verified compatible
components. Spec §30 places the decision in M0, before the dependency lock. The decision belongs to
the user.

## Decision

Interim policy until the user decides (plan Q-02):

- Stage 1 dependencies are permissive or LGPL.
- Qt is linked dynamically, so open-source, proprietary and dual-license paths all stay open.
- No bundled fonts, brushes or color libraries with unknown rights.

## Alternatives

None taken: choosing a license model is outside an implementation session's authority.

## Consequences

- `docs/dependencies/manifest.yaml` records each dependency's license and redistribution
  obligations, so the eventual decision can be checked against them.
- The manifest and ledger freeze at S1-9 needs Q-02 answered.

## Evidence

- License fields in the manifest come from the installed packages (`pacman -Qi`).
