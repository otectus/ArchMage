---
id: ADR-0022
decision: D-22
title: Git with one branch per sub-milestone and a green main
status: accepted
date: 2026-10-11
spec: [§29.1 step 1, §29.2]
---

# ADR-0022: Git with one branch per sub-milestone and a green main

## Context

Spec §29.1 starts by recording the starting commit or snapshot. No repository existed: the project
directory held only `docs/`.

## Decision

- Git repository initialized in S1-0 on branch `main`.
- `main` always configures, builds and passes the `math`, `operation` and `document` tiers.
- One branch per sub-milestone, `s1-<n>-<slug>`, merged when its exit criteria pass.
- Commit subjects are imperative; bodies cite task IDs.
- Until hosting and CI are decided (plan Q-05), `scripts/check.sh` is the gate and
  `.github/workflows/linux.yml` is written but inert.

## Alternatives

- Trunk-only commits: lose the sub-milestone boundary that the session protocol (plan §3.3) uses.

## Consequences

- No remote exists; nothing is pushed until Q-05 is answered.

## Evidence

- Initial commit `0cdf8ce` on `main` (specification, plan, editor configuration); S1-0 work on
  branch `s1-0-bootstrap`.
