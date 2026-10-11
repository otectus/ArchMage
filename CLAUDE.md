# ArchMage

Professional image editor for Linux first, in C++20, Qt 6 Widgets, CMake and Ninja. This is not a
Minecraft mod: the Gradle, MODMAP and `.mcmod-tools` guidance in the global instructions does not
apply here.

## Operative documents

- Specification (authoritative): `docs/ArchMage_Full_Development_Specification.md`.
- Stage 1 plan: `docs/plans/Stage1_Development_Plan.md`. Decisions D-01 to D-30 are binding; each has
  an ADR in `docs/decisions/ADR-00nn-*.md`. To change one, write a superseding ADR and add a one-line
  pointer in plan §2; never diverge silently.
- Current state and evidence: `docs/implementation-status.md`. Read it before starting work.

## Session protocol (plan §3.3)

- One sub-milestone (S1-0 to S1-9) per session, in plan order. Do not start the next one.
- Re-run the previous gate's checks first; fix any failure before new work and record the fix.
- Branch `s1-<n>-<slug>`; commit per task or coherent pair; merge to `main` when the exit criteria
  pass. Commit subjects are imperative; bodies cite task IDs.
- A task is done when its definition of done is observed, not inferred. Record commands, outputs
  and artifact paths in `docs/implementation-status.md`, then write the progress report (plan §8.2).
- No sub-agents: the main session does all reconnaissance, implementation and verification.
- Escalate only genuine blockers: plan/spec contradictions, unanticipated environment problems, or
  decisions the plan reserves for the user (plan §6.3).

## Build and test

```
cmake --preset dev-debug && cmake --build --preset dev-debug
ctest --preset dev-debug --output-on-failure      # all tiers
ctest --preset dev-debug -L math                  # one tier: math operation document image ui interchange resilience perf
scripts/check.sh                                  # gate: non-UI tiers, manifest test, ledger check
scripts/check-ui.sh                               # offscreen UI tier, no display needed
scripts/check-sanitizers.sh                       # Clang ASan+UBSan build, full suite
python3 -I scripts/parity_report.py               # regenerate docs/parity/report.md after editing the ledger
```

Presets (`CMakePresets.json`): `dev-debug`, `dev-release`, `asan-ubsan`, `tsan`, `fuzz`, `ci`. Build
trees are `build/<preset>/`. Run Python helpers with `python3 -I`.

## Rules that bite

- `core/`, `io/` and `apps/archmage-cli/` are Qt-free (D-07); only `ui/` and `apps/archmage/` link Qt.
- New modules use `archmage_add_module()`; new tests use `archmage_add_test()` with a tier label.
- Every new CMake dependency lookup needs an entry in `docs/dependencies/manifest.yaml`.
- Ledger edits go in `docs/parity/features.yaml`; regenerate the report in the same commit.
- Hide or label unfinished controls; never claim an untested platform or device (spec §29.2).
