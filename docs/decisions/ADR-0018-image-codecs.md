---
id: ADR-0018
decision: D-18
title: libpng and libjpeg-turbo behind ImageCodec adapters, in-process with hard limits
status: accepted
date: 2026-10-11
spec: [§22.1, §22.4, §26.1]
---

# ADR-0018: libpng and libjpeg-turbo behind ImageCodec adapters, in-process with hard limits

## Context

Spec §22.1 requires per-format capability records rather than extension lists. Spec §26.1 treats
image files as untrusted and asks for isolated decoders for complex or risky formats. Stage 1 needs
PNG and JPEG only.

## Decision

- libpng and libjpeg-turbo used directly behind `io/formats/ImageCodec` adapters, each with a
  capability record (open, export, alpha, profile, precision).
- Decoding is in-process in Stage 1 with hard limits: 300,000 pixels per axis, decoded bytes checked
  against the RAM budget, chunk sanity checks, and fuzz coverage.
- Isolated decoder workers arrive with the broad format set in M4.

## Alternatives

- OpenImageIO (named as an option in §5.2): not used in Stage 1; two direct libraries are smaller to
  audit and fuzz.

## Consequences

- PNG and JPEG run in the editor process until M4; the limits and fuzzing are the mitigation.

## Evidence

None yet. Codecs in S1-8.6; PNG fuzz target in S1-5.8.
