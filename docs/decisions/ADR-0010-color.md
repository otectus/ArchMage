---
id: ADR-0010
decision: D-10
title: Named color pipeline with Little CMS, sRGB working space in Stage 1
status: accepted
date: 2026-10-11
spec: [§8.2, §8.3, §5.2]
---

# ADR-0010: Named color pipeline with Little CMS, sRGB working space in Stage 1

## Context

Spec §8.3 asks for a named pipeline (decode, working representation, operations, view, display) and
§8.2 for ICC workflows with assign and convert kept distinct. Spec §5.2 names Little CMS for ICC and
OpenColorIO for scene and view transforms. Full color work is milestone M4.

## Decision

- `core/color/` defines `ColorProfileRef`, `ColorPolicy` and a `ColorPipeline` with the named stages.
- Stage 1: the working space is the document profile (default sRGB generated with Little CMS
  `cmsCreate_sRGBProfile`, its hash pinned by a test); the view stage is identity; the display is
  assumed to be sRGB.
- Little CMS is linked for embedded-profile parsing on import, sRGB embedding on export, and the S1-6
  conversion experiment. No assign or convert commands in Stage 1.

## Alternatives

- OpenColorIO now: deferred to M4; Stage 1 has no scene-referred workflow.
- Display-profile monitoring now: deferred to M4 (plan A-05 records the sRGB display assumption).

## Consequences

- Displays that are not sRGB show inaccurate color in Stage 1; this is a documented limitation, not a
  claim of color accuracy.

## Evidence

None yet. Skeleton and pinned sRGB hash in S1-2.9; conversion experiment in S1-6.4.
