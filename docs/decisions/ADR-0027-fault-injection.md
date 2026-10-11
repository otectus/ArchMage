---
id: ADR-0027
decision: D-27
title: Compile-time fault injection at named persistence points
status: accepted
date: 2026-10-11
spec: [§10.3, §10.4, §26.2]
---

# ADR-0027: Compile-time fault injection at named persistence points

## Context

Spec §10.4 requires tests that terminate the process during tile writes, transaction commit, backup,
checkpoint, rename and compaction. AT-013 and AT-014 need interrupted saves and disk exhaustion to be
reproducible.

## Decision

- A compile-time option `ARCHMAGE_FAULT_INJECTION` (on in Debug and test presets, off in
  `dev-release`) enables `ARCHMAGE_FAULT=<point>[:<n>]`, which aborts with `_exit(137)` or returns a
  synthetic error at named points in the save, journal, checkpoint, rename and compaction paths.
- Disk exhaustion uses `RLIMIT_FSIZE` on a subprocess plus an `enospc@<point>` synthetic error.
- Fault points are enumerated in `io/native/FaultPoints.h`; a test asserts every point is exercised.

## Alternatives

- Killing processes with external signals at random times: not deterministic enough for a matrix.

## Consequences

- The option and its presets are added in S1-5, when the first fault point exists; S1-0 does not
  declare an unused option.

## Evidence

None yet. Implemented in S1-5.1; AT-013 and AT-014 harnesses in S1-5.5 and S1-5.6.
