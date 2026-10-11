---
id: ADR-0011
decision: D-11
title: Canvas presentation adapter
status: provisional (gate G1)
date: 2026-10-11
spec: [§5.2, §7.5, §26.2, §28 M0, §30]
---

# ADR-0011: Canvas presentation adapter

## Context

Spec §5.2 asks for a renderer-owned Qt window or surface, Vulkan first, and notes Qt's documented
limitations for embedding Vulkan windows in widget interfaces [S20]. Popup stacking, docks, focus,
overlays, mixed DPI and tablet input are explicit M0 experiments (§28 M0, §30 first row).

## Decision (provisional until S1-1 reports)

- Primary candidate: `QVulkanWindow` inside `QWidget::createWindowContainer` in the dock layout.
- Fallback candidate: `QRhiWidget` (public since Qt 6.7 with a limited compatibility guarantee;
  choosing it records a deviation from "avoid a private Qt rendering dependency").
- Required either way: a `QWidget` raster canvas painting CPU-composited tiles, used for safe mode
  and device-loss fallback (§26.2, §7.5).

## Alternatives

The two GPU candidates above; S1-1 builds all three surfaces behind `ui/canvas/CanvasSurface`.

## Consequences

- The non-chosen GPU surface stays behind an option marked experimental.

## Evidence (S1-1.3 fills this matrix; screenshots in `docs/decisions/evidence/S1-1/`)

| Check | Vulkan Wayland | Vulkan X11 | RHI Wayland | RHI X11 | Raster Wayland | Raster X11 |
| --- | --- | --- | --- | --- | --- | --- |
| Popup, menu and tooltip stacking over the canvas | not run | not run | not run | not run | not run | not run |
| Dock float, re-dock and resize without corruption | not run | not run | not run | not run | not run | not run |
| Move across monitors, one at scale 1.5 | not run | not run | not run | not run | not run | not run |
| Fractional-scale cursor and canvas alignment | not run | not run | not run | not run | not run | not run |
| Focus returns after a dialog | not run | not run | not run | not run | not run | not run |
| Resize storm stability | not run | not run | not run | not run | not run | not run |
| Teardown clean under `VK_LAYER_KHRONOS_validation` | not run | not run | not run | not run | n/a | n/a |
| Latency p50/p95, synthetic drag (S1-1.6) | not run | not run | not run | not run | not run | not run |

Tablet pressure: no tablet detected on 2026-10-11 (plan §7.2); S1-1.4 records the result.
