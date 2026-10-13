---
id: ADR-0009
decision: D-09
title: CPU reference source-over in float32 premultiplied, straight storage
status: accepted
date: 2026-10-11
spec: [§7.4, §7.5]
---

# ADR-0009: CPU reference source-over in float32 premultiplied, straight storage

## Context

Spec §7.4 defines premultiplied source-over (`Aout = As + Ad × (1 − As)`, `Cout = Cs + Cd × (1 − As)`),
requires straight stored samples so hidden color under zero alpha survives, and asks for defined
behavior at zero alpha, negatives, infinities and NaNs. It also requires an explicit document blend
policy. Spec §7.5 makes CPU reference implementations the definition of correctness.

## Decision

- The CPU reference compositor works in `float32` premultiplied form using exactly the §7.4
  equations. Stored tiles keep straight samples.
- Straight-to-premultiplied conversion happens at evaluation: alpha 0 keeps the stored color and
  contributes nothing; NaN and infinity are sanitized to 0 with a diagnostic counter; negative float
  values are kept and clamped only when encoding to `uint8`.
- The document blend policy has values `encoded` and `linear`. Stage 1 implements `encoded` and
  rejects `linear` with a clear error until M2.

## Alternatives

- Premultiplied storage: rejected; it loses color under zero alpha, which §7.4 requires to keep.
- Linear-light blending now: deferred to M2 with the other blend modes.

## Consequences

- Layer opacity is applied as a coverage scale during evaluation, never baked into stored samples.

## Evidence

- S1-2: `KernelsTest` and `EvaluatorTest` (a 3-layer document equals a hand-computed reference; NaN and
  infinity sanitized and counted; color under zero alpha preserved; negative and above-one float samples kept).
- S1-6.2: an independent implementation (the Vulkan compute shader, `core/render/gpu/shaders/source_over.comp`)
  written to the same equations reproduces the CPU reference bit for bit on uint8 and float32 stacks
  (`GpuTest`).
