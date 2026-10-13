---
id: ADR-0101
decision: D-23 (amends); resolves the open issue of ADR-0002
title: Engine results use archmage::base::Expected under C++20
status: accepted
date: 2026-10-13
spec: [§5.2, §26.2]
supersedes: "the std::expected clause of ADR-0023"
---

# ADR-0101: Engine results use archmage::base::Expected under C++20

Numbering: ADR-0001 to ADR-0099 match plan decisions D-01 to D-99. ADRs from ADR-0101 onward record
decisions made during implementation that amend or supersede one of them.

## Context

ADR-0023 (D-23) requires engine failures to be reported through `std::expected<T, Error>`, but ADR-0002
(D-02) and spec §5.2 fix the language at C++20, and `std::expected` is a C++23 library feature. GCC 16.2.1
and Clang 22.1.8 and 23.1.1 reject it under `-std=c++20` (status issue I-01). The user asked for
implementation to proceed on reasonable assumptions (2026-10-13), so the issue is settled here.

## Decision

- The language stays C++20 (spec §5.2, ADR-0002 unchanged).
- `core/base/include/archmage/base/Expected.h` provides `archmage::base::Expected<T, E>`, `Unexpected<E>`
  and `unexpected(e)` with the subset of the `std::expected` interface the engine uses: `has_value`,
  `operator bool`, `value`, `error`, `operator*`, `operator->`, `value_or`, `and_then`, `transform`,
  `or_else`, `transform_error`, and the `Expected<void, E>` specialization.
- `Result<T>` aliases `Expected<T, Error>` and `Status` aliases `Expected<void, Error>`.
- `value()` on an error, or `error()` on a value, is a programming error and terminates with a message;
  engine code never throws for expected failures (ADR-0023).

## Alternatives

- Move to C++23: possible with the installed compilers, but it changes the spec's stated baseline and
  raises the minimum Clang to 19 (earlier Clang cannot use libstdc++'s `<expected>`). Rejected for now.
- `tl::expected` or another library: an extra dependency for about 200 lines of code.

## Consequences

- When the project moves to C++23, `Expected` can become an alias of `std::expected`; the subset matches
  the standard names and semantics, except that `value()` terminates instead of throwing.

## Evidence

- `ExpectedTest` (S1-2) covers construction, access, the monadic operations, move-only values and the
  void specialization under `dev-debug` and `asan-ubsan`.
