---
id: ADR-0007
decision: D-07
title: Core, io and the CLI are Qt-free
status: accepted
date: 2026-10-11
spec: [§1.2, §5.2]
---

# ADR-0007: Core, io and the CLI are Qt-free

## Context

Spec §5.2 forbids GUI objects in document or processing APIs, and §1.2 requires core behavior to be
testable without launching the desktop interface.

## Decision

- No Qt headers or targets in `core/`, `io/` or `apps/archmage-cli/`. Only `ui/` and `apps/archmage/`
  link Qt.
- Supporting libraries for the Qt-free code: nlohmann-json (command envelope JSON and CLI `--json`
  output), spdlog with fmt (logging), CLI11 (command-line parsing).

## Alternatives

- Qt types (QString, QImage, QThreadPool) throughout: rejected by the spec's constraint.

## Consequences

- Strings in engine APIs are UTF-8 `std::string`/`std::string_view`; conversion to `QString` happens
  in `ui/`.
- Threading uses the standard library (ADR-0024), not Qt's thread pool.

## Evidence

- `archmage-cli` links only the core and io aggregates and CLI11; `ldd` on the built executable lists
  no Qt library (recorded in `docs/implementation-status.md`, S1-0).
