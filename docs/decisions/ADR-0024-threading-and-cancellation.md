---
id: ADR-0024
decision: D-24
title: std::jthread pool, stop tokens and one mutation executor
status: accepted
date: 2026-10-11
spec: [§9.3, §1.2]
---

# ADR-0024: std::jthread pool, stop tokens and one mutation executor

## Context

Spec §9.3 asks for a document mutation executor with serialized commits plus worker pools, jobs that
evaluate an immutable source revision, and cancellation tokens, job identity, source fingerprints and
target identities on long-running work. Spec §1.2 requires responsive editing during background work.

## Decision

- A `std::jthread` pool sized `max(2, hardware_concurrency − 2)`.
- `std::stop_token` is the cancellation token.
- One mutation executor thread serializes commits.
- Jobs carry identity, source revision fingerprint and target IDs from the first implementation.

## Alternatives

- Qt's thread pool: unavailable to the Qt-free core (ADR-0007).

## Consequences

- The recovery journal (ADR-0014) runs on its own thread and only reads committed revisions.

## Evidence

None yet. Job model in S1-2.10; ThreadSanitizer runs from S1-5.
