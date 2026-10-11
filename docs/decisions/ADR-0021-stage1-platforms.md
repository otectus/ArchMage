---
id: ADR-0021
decision: D-21
title: Linux only in Stage 1, native Wayland and X11
status: accepted
date: 2026-10-11
spec: [§5.1, §26.4, §29.2]
---

# ADR-0021: Linux only in Stage 1, native Wayland and X11

## Context

Spec §5.1 makes Linux, including Arch/CachyOS and Wayland/Hyprland, the initial target, and asks to
keep Windows and macOS buildable early. No Windows or macOS machine is available (plan A-06). Spec
§29.2 forbids claiming an untested platform.

## Decision

- Stage 1 builds and tests on Linux only: native Wayland (Hyprland) and X11 through XWayland.
- Core and io use `std::filesystem` plus a small platform layer for `fsync`, directory sync, atomic
  rename and `statfs`; Windows and macOS implementations are compile-time stubs.
- No Windows or macOS build is attempted; the gap is recorded with reproduction instructions.

## Alternatives

- Cross-platform builds now: no machines to run them (plan Q-09; the default applies).

## Consequences

- Portability is maintained by discipline (imported targets, no POSIX calls outside the platform
  layer) until a Windows or macOS build is attempted.

## Evidence

- On 2026-10-11 the native Wayland platform plugin (`libqwayland.so`) and the Qt6WaylandClient CMake
  package are shipped by `qt6-base` (6.11.2-3.1, and 6.12.0-2.1 after that day's system upgrade;
  `pacman -Qo`). The plan expected them from `qt6-wayland`, which installs the Qt6WaylandCompositor
  libraries (`pacman -Ql qt6-wayland`, 6.12.0-1.1) that ArchMage does not use. It was installed as
  planned but is not a build or runtime requirement.
- S1-0 window checks under `QT_QPA_PLATFORM=wayland` and `xcb` are recorded in
  `docs/implementation-status.md`.
