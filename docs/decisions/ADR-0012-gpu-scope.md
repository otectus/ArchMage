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

S1-6 (2026-10-13), `tests/core/render/GpuTest.cpp`, `core/render/gpu/`:

- **Correctness.** On the RTX 4050 Laptop GPU (NVIDIA 615.78.08, Vulkan 1.4), with the Khronos validation
  layer on and no validation errors, the compute shader's composite equals the CPU reference bit for bit.
  This holds on the AT-002 fixture and on random 20-layer uint8 and float32 stacks (30 tiles, 491,520 tile pixels each, with
  offsets, opacities 0 and 1, hidden layers, uniform tiles, out-of-range and NaN samples). The plan's bound of
  1e-6 for float32 is met with 0. `precise` and the CPU's operation order make the arithmetic identical.
- **Fallback.** After a device loss, or for graphs outside the slice, the same calls return the CPU's
  results.
- **Viability.** The compositing arithmetic is cheap on the GPU. The slice as built is not fast, and the
  cost is data movement. A full 24 MP, 20-layer composite (1,504 output tiles from 5,955 layer tiles) broke
  down as:
  - gathering samples on one CPU thread: 1.72 s;
  - transfer plus shader: 0.23 s;
  - readback: 0.25 s (2.69 s before it used cached host memory);
  - the CPU evaluator on 22 threads: 84 to 105 ms.

  These timings were taken on 2026-10-13 while a game was running on the same machine, so they are
  indicative; the idle profile A record (`benchmarks/results/`, S1-6.8) supersedes them.
- **Consequence for M2+.** GPU evaluation pays off only with device-resident tiles (uploaded once and kept),
  unpacking on the GPU, and asynchronous readback. That is the design for M4; the CPU stays authoritative.

Graph contract (blur halo, S1-6.3, `tests/core/render/BlurTest.cpp`): `GaussianBlurNode` declares its halo
and needs its output region grown by the radius. Evaluated tile by tile through `CpuEvaluator`, it equals
the whole-image result exactly (0 LSB; the plan allowed 1) at sigma 0.6, 2 and 7.5 on features placed across
tile borders. The node contract needs no change for neighborhood operations.
