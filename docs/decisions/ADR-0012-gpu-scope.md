---
id: ADR-0012
decision: D-12
title: CPU evaluator is authoritative; Vulkan compute compositor is an opt-in comparison
status: accepted
date: 2026-10-11
spec: [§7.5, §29.1 step 9, §28 M0]
---

# ADR-0012: CPU evaluator is authoritative; Vulkan compute compositor is an opt-in comparison

## Context

Spec §7.5 makes CPU reference implementations the definition of correctness, requires explicit
tolerances for float comparisons and exact results for integer operations, and forbids shader
compilation on interaction-critical paths. Spec §29.1 step 9 asks for GPU evaluation of the same
narrow slice, compared with the CPU reference.

## Decision

- The CPU evaluator is authoritative throughout Stage 1.
- S1-6 implements a Vulkan compute-shader source-over compositor for the narrow slice as an opt-in
  and compares it with the CPU reference (uint8 exact, float32 within 1e-6 absolute).
- Shaders compile to SPIR-V at build time with `glslangValidator`; no runtime shader compiler in
  the interaction path.

## Alternatives

- GPU-first rendering: rejected by §7.5; the CPU reference must exist and define results.

## Consequences

- `Vulkan` and its `glslangValidator` component are required at configure time from S1-0, before
  any GPU code exists, so a missing toolchain is caught early.

## Evidence

None yet. S1-6.1 and S1-6.2 produce the comparison test and record GPU viability here.
