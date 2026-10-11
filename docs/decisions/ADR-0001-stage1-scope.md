---
id: ADR-0001
decision: D-01
title: Stage 1 is milestones M0 and M1
status: accepted
date: 2026-10-11
spec: [§28 M0, §28 M1, §29.1]
---

# ADR-0001: Stage 1 is milestones M0 and M1

## Context

The specification defines milestones M0 to M9 (§28) and a first development iteration (§29.1) whose
last step reads "Complete M0 evidence and M1 scope, then proceed through the remaining milestones". It
never uses the term "Stage 1". The plan needed a fixed boundary for the first stage of work.

## Decision

Stage 1 is M0 (inception and feasibility) plus M1 (trustworthy raster foundation), run as one program
with the M0 risk experiments front-loaded. It ends when the M1 exit gate passes: AT-001, AT-002,
AT-013, AT-014 and the initial navigation and input checks (plan §1.6), with a user able to make useful
simple edits without a crash or lost save. The work is split into sub-milestones S1-0 to S1-9 (plan §3.2).

## Alternatives

- M0 alone: leaves no user-visible acceptance gate, because M1 has the first named acceptance tests.
- M0 to M2: the first gate would be too far away to steer by; M2 depends on M1's engine contracts.

## Consequences

- Everything outside M0 and M1 is deferred by the table in plan §1.7, including later behaviors that
  the M1 subsets touch (groups, blend modes, history branching).
- The result of Stage 1 is an internal alpha, not a feature-complete application (§28 M1 exit).

## Evidence

- The user confirmed this scope on 2026-10-11 (plan Q-01, answered before S1-0 began).
