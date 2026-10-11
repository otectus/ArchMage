---
id: ADR-0002
decision: D-02
title: C++20 with GCC as the primary compiler and Clang for analysis builds
status: accepted
date: 2026-10-11
spec: [§5.2]
---

# ADR-0002: C++20 with GCC as the primary compiler and Clang for analysis builds

## Context

Spec §5.2 sets the core implementation language to C++20 with modern ownership. The development
machine has GCC 16.2.1 and, since the 2026-10-11 system upgrade, Clang 23.1.1 (22.1.8 when the plan
was written). Sanitizer, libFuzzer and clang-tidy support is strongest with Clang.

## Decision

- C++20 without compiler extensions (`CMAKE_CXX_STANDARD 20`, `CMAKE_CXX_EXTENSIONS OFF`).
- GCC 16.2 is the primary compiler (`dev-debug`, `dev-release`, `ci`); Clang (22.1 in the plan, 23.1
  installed) builds the `asan-ubsan`, `tsan` and `fuzz` presets and runs clang-tidy.
- Recorded minimums, not tested in Stage 1: GCC 13 and Clang 17.

## Alternatives

- C++23. Not chosen by the plan; see the open issue below.
- Clang as the primary compiler: no stated advantage over GCC for the main build; both are installed.

## Consequences

- Open issue, needs a user decision before S1-2: D-23 requires `std::expected<T, Error>` for engine
  errors, but `std::expected` is a C++23 library feature. Under `-std=c++20` both installed compilers
  reject it. Either the standard moves to C++23 (a superseding ADR; recheck the D-02 minimum compiler
  versions), or the engine keeps C++20 and uses an equivalent `Expected` type.
- Using C++20 features such as `std::jthread`, `std::stop_token` and `std::format` is fine with both
  compilers.

## Evidence

- `g++ --version`: 16.2.1; `clang++ --version`: 22.1.8 before and 23.1.1 after the 2026-10-11
  system upgrade.
- `std::expected<int,int>` fails to compile with `-std=c++20` on GCC 16.2.1 ("'expected' in namespace
  'std' does not name a template type") and on Clang 22.1.8 and 23.1.1 ("no template named
  'expected'"), and compiles with `-std=c++23` on all three (S1-0 session, 2026-10-11).
- All six presets build warning-free with these compilers (`docs/implementation-status.md`, S1-0).
