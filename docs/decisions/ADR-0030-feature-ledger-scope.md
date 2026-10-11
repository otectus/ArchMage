---
id: ADR-0030
decision: D-30
title: Feature ledger infrastructure plus spec-derived parent entries
status: accepted
date: 2026-10-11
spec: [§3.1, §3.2, §3.3, App. A.1, §28 M0]
---

# ADR-0030: Feature ledger infrastructure plus spec-derived parent entries

## Context

Spec §3.2 requires `docs/parity/features.yaml` with one testable behavior per entry and a generated
report; §3.1 and §28 M0 ask for a frozen, option-level inventory of the benchmark products. That audit
is research against live products, which a coding session cannot complete honestly (plan A-08).

## Decision

- `docs/parity/features.yaml` uses the App. A.1 schema (field reference in `docs/parity/README.md`).
- `scripts/parity_report.py` validates it and generates `docs/parity/report.md`; `--check` fails
  on a validation error or a stale report.
- Parent entries exist for every requirement row in the spec tables of §11 to §20, state `unstarted`,
  with the row text verbatim, a milestone from §28 and `benchmark.evidence: public_documentation_only`.
- Child entries with real state are added for every behavior Stage 1 ships (S1-9.3).
- The option-level benchmark audit is tracked as plan Q-08 and does not block Stage 1 code.

## Alternatives

- Completing the audit before writing code: blocks all engineering on research with no owner yet.

## Consequences

- The ledger baseline is `unfrozen` until the audit; completion counts are not parity claims.
- Parent milestones marked `inferred:` in `milestone_basis` are assignments §28 does not state
  explicitly; the audit may move them.

## Evidence

- S1-0: 121 parent entries; `python3 -I scripts/parity_report.py --check` passes.
