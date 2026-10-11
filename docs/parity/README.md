# Feature ledger

`features.yaml` is the traceable feature ledger required by spec §3.2, using the entry shape of
spec App. A.1. `report.md` is generated from it; never edit the report by hand.

```
python3 -I scripts/parity_report.py           # validate the ledger and rewrite report.md
python3 -I scripts/parity_report.py --check   # validate and fail if report.md is stale (run by scripts/check.sh)
```

## Entries

- **Parent entries** (`parent: null`) mirror one requirement row of the specification tables
  (§11 to §20: NAV, SEL, MSK, LAY, GEO, RET, FIL, PNT, TXT, VEC, RAW, AI, ANM, PIX). Their `outcome`
  repeats the row text verbatim; the checker fails if a row is missing or its text differs.
- **Child entries** are the testable behaviors (spec §3.2: "one entry represents a testable behavior").
  Their id is the parent id plus a slug, for example `PNT-03-pressure-size`.
- A new top-level requirement found during the benchmark audit carries `discovered_on: <date>` and is
  listed separately in the report (spec §3.3).

## Fields

Required on every entry (App. A.1):

| Field | Values |
| --- | --- |
| `id`, `parent` | Stable ID; parent ID or `null` |
| `title`, `outcome` | Short name; user outcome (verbatim spec row for parents) |
| `scope` | `current_baseline`, `extended_capability`, `legacy_compatibility`, `external_service`, or `null` until classified; must be set once work starts |
| `milestone` | `M0` to `M9` |
| `prerequisites` | List of prerequisite IDs or capability names |
| `implementation_state` | `unstarted`, `researching`, `partial`, `implemented`, `verified`, `blocked` |
| `fidelity_target`, `fidelity_verified` | `equivalent`, `approximate`, `raster_fallback`, `preserved_uneditable`, `unsupported`, or `null` |
| `benchmark` | `product`, `edition`, `observed_version`, `source_url`, `observed_date`, `evidence` (`public_documentation_only`, or `hands_on`, which needs `observed_version`) |
| `planned_capabilities`, `platform_results` | Mappings |
| `command_id` | `archmage.<area>.<verb>` or `null` |
| `acceptance`, `test_scenarios`, `limitations` | Lists; scenarios are `AT-nnn` IDs |

Optional (spec §3.2 items beyond the App. A.1 example): `spec_section`, `milestone_basis`,
`discovered_on`, `entry_points`, `options`, `document_modes`, `precision`, `alpha`, `layer_types`,
`animation`, `persistence_version`, `test_ids`, `demonstration_fixture`, `requirements`.

Rules the checker enforces: implementation state and fidelity stay separate fields; a `verified`
entry needs `fidelity_verified` and at least one test ID or scenario; `hands_on` evidence needs the
version actually observed (App. A.1: a source URL alone proves no version was tested).

## Milestones on parent entries

A parent's `milestone` is the earliest spec §28 milestone that delivers part of the row, and
`milestone_basis` quotes the §28 wording or plan section it rests on. Bases starting with
`inferred:` mark rows §28 does not name explicitly; the benchmark audit may move them.

## Baseline status

The baseline is `unfrozen`. The option-level audit of the benchmark products (spec §3.1–3.2, §28 M0)
is research that has not happened (plan Q-08, ADR-0030). Every parent entry therefore records
`benchmark.evidence: public_documentation_only` and `scope: null`, and the report's counts are
progress tracking, not parity claims (spec §1.1 item 7).
