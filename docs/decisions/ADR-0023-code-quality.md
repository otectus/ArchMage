---
id: ADR-0023
decision: D-23
title: Formatting, static analysis, warnings and structured errors
status: accepted
date: 2026-10-11
spec: [§26.2]
---

# ADR-0023: Formatting, static analysis, warnings and structured errors

## Context

Spec §26.2 requires structured domain errors with user-facing explanations and diagnostic codes, and
forbids catching every exception and continuing silently.

## Decision

- `.clang-format`: LLVM base, 4-space indent, 110 columns.
- `.clang-tidy`: bugprone, performance and modernize subsets; advisory in Stage 1.
- `-Wall -Wextra -Wpedantic -Wshadow -Wconversion` on every project target in every preset; `-Werror`
  only in `ci` (`ARCHMAGE_WARNINGS_AS_ERRORS`).
- Engine code reports failures through `std::expected<T, Error>` with structured domain errors, not
  exceptions.

## Alternatives

- Exceptions in engine code: rejected; failures are values that commands and jobs must handle.

## Consequences

- Open issue, needs a user decision before S1-2: `std::expected` requires C++23, while ADR-0002 fixes
  C++20. See ADR-0002 for the options.
- Third-party code (CLI11, Qt) may use exceptions internally; they are contained at the call site.
- clang-tidy reads a Clang preset's compile database (for example `build/asan-ubsan`): the GCC
  databases carry `-mno-direct-extern-access`, a GCC-only flag that Qt's CMake package adds.

## Evidence

- S1-0: `clang-format --dump-config` and `clang-tidy --dump-config` parse the configuration; every
  C++ source passes `clang-format --dry-run --Werror`; clang-tidy over all sources with the
  `asan-ubsan` database reports no findings in project code; the `ci` preset builds with `-Werror`.
  Commands and output: `docs/implementation-status.md`.
