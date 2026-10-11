---
id: ADR-0015
decision: D-15
title: Typed in-process commands with an idempotent serialized executor
status: accepted
date: 2026-10-11
spec: [§6.2, §9.1, §9.2, App. A.2]
---

# ADR-0015: Typed in-process commands with an idempotent serialized executor

## Context

Spec §9.1 requires stable command IDs, schema-versioned parameters, explicit targets, preconditions,
resource estimates, transaction policy, labels and undo representation for every mutation. App. A.2
gives the envelope and requires that a repeated `requestId` never applies twice.

## Decision

- Commands are typed in-process objects. `CommandId` strings are `archmage.<area>.<verb>`.
- Each command carries a parameter struct with `parameterVersion`, `targetIds`, `baseRevision`,
  `requestId`, `transaction {id, label}`, preconditions (lock, target existence, mode
  compatibility), a resource estimate and an undo record.
- Results: committed revision, validation error or conflict. The executor enforces `requestId`
  idempotency over a bounded window of retained results.
- Interactive previews run inside an in-progress transaction that panels cannot observe until
  commit.
- History is linear in Stage 1 with a clean-save-point marker; branching is M2.

## Alternatives

- Branching history now: deferred to M2 (plan Q-07, A-13).

## Consequences

- Menus, shortcuts and the CLI all dispatch these commands; no panel mutates engine state directly.

## Evidence

None yet. Implemented in S1-2.5 and S1-2.6; inventory in plan §8.1.
