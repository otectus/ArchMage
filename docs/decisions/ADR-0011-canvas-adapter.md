---
id: ADR-0011
decision: D-11
title: Canvas presentation adapter
status: accepted (gate G1, 2026-10-13)
date: 2026-10-13
spec: [§5.2, §7.5, §8.3, §11, §25.2, §26.2, §28 M0, §30]
---

# ADR-0011: Canvas presentation adapter

## Context

Spec §5.2 asks for a renderer-owned Qt window or surface, Vulkan first, and notes Qt's documented
limits on embedding Vulkan windows in widget interfaces [S20]. Popup stacking, docks, focus, overlays,
mixed DPI and tablet input are M0 experiments (§28 M0, §30 first row). S1-1 built three surfaces behind
`ui/canvas/CanvasSurface`, each presenting the same CPU-rendered frame:

- **Vulkan**: `QVulkanWindow` in `QWidget::createWindowContainer`, uploading the frame through staging
  buffers and drawing it texel for texel.
- **RHI**: `QRhiWidget` with the Vulkan backend. QRhi's headers ship only with Qt's private Gui module,
  under a limited compatibility guarantee.
- **Raster**: a `QWidget` painting the frame into the window backing store.

## Decision

1. The canvas always goes through the `CanvasSurface` adapter. `CanvasSurface::defaultKind()` picks the
   adapter: **raster by default in Stage 1**, `ARCHMAGE_CANVAS=vulkan` or `=rhi` to opt in.
2. **Raster is the default and the permanent safe mode** (spec §26.2, §7.5). The Stage 1 frame is
   CPU-composited anyway, raster passed every check on both platforms and both scales, and it had the
   lowest event-to-submission time with no GPU or validation issues.
3. **The GPU adapter is a Vulkan surface on a renderer-owned QWindow embedded with
   createWindowContainer.** It passed every integration check (stacking, docks, resize storm,
   1 ↔ 1.5 scale moves, focus, teardown). It stays opt-in until both conditions hold:
   - ArchMage owns the swapchain instead of QVulkanWindow, fixing the per-frame
     VUID-vkQueueSubmit-pSignalSemaphores-00067 error in QVulkanWindow's frame sync and adding explicit
     swapchain color space selection (spec §8.3; QVulkanWindow offers none).
   - GPU evaluation (S1-6) produces canvas content on the GPU, so the GPU path does real work.

   Revisit at the M1 exit (S1-9), and before M4 color and HDR presentation at the latest.
4. **QRhiWidget is rejected as the primary adapter** and kept only as an experimental option:
   - Its render step blocks the GUI thread for a full vsync interval (render p50 16.5 ms), so the GUI
     thread handled 169 of about 750 synthetic input events in 3 s.
   - It requires Qt's private Gui module.
   - Qt's QRhi Vulkan backend raises VUID-VkImageCreateInfo-initialLayout-12478 with validation layers
     1.4.363.
5. Canvas repaints should be paced to the display (QWindow::requestUpdate or frame callbacks): the raster
   path repainted once per input event, 747 frames in 3 s on a 60 Hz output. Applied when the real canvas
   lands (S1-3, S1-7).

## Alternatives

- Vulkan by default now: it adds a frame upload, carries a Qt synchronization error on every frame and
  gains nothing until GPU evaluation exists.
- QRhiWidget as the GPU path: rejected for GUI-thread blocking and the private-module dependency (item 4).

## Consequences

- The surface kind is a runtime switch, so the GPU adapter can become the default without touching tools
  or the document model (spec §5.2 "replace the presentation adapter while keeping the core").
- Follow-up: an ArchMage-owned Vulkan swapchain on a QWindow before the Vulkan adapter becomes the default.
- XWayland with `xwayland.force_zero_scaling = true` (the user's Hyprland setting): X11 clients see device
  pixels at ratio 1 on a scale-1.5 output. The canvas stays pixel-exact but the UI is not scaled. The
  native Wayland path is the target.

## Evidence

Method: `python3 -I scripts/experiments/canvas_matrix.py` (2026-10-13, `dev-release` build) runs
`archmage --experiment=canvas:<kind>` under Qt's `wayland` and `xcb` platforms on Hyprland 0.56.2.
- Two temporary headless outputs (1920 × 1080 at 60 Hz, scale 1 and 1.5), so no physical monitor was
  rescaled. Window rules and hyprctl place the windows; the user's focus and cursor are restored afterwards.
- `ARCHMAGE_VK_VALIDATION=1` enables VK_LAYER_KHRONOS_validation 1.4.363.
- GPU: NVIDIA RTX 4050 Laptop, driver 615.78.08.
- Frame checks compare grim captures with the frame each app reported presenting. "Exact" means every
  pixel within 2 levels, at offset 0; at fractional scale ±1 px is allowed on a half-pixel origin.
- Probe figures are the share of the probe's expected area showing its color (popup magenta, tooltip
  cyan) above the canvas.
- Evidence: `docs/decisions/evidence/S1-1/` (per run: `log.txt`, `result.json`, frames, window-only crops).

| Check | Vulkan Wayland | Vulkan X11 | RHI Wayland | RHI X11 | Raster Wayland | Raster X11 |
| --- | --- | --- | --- | --- | --- | --- |
| Popup menu over the canvas, scale 1 / 1.5 | 0.93 / 0.94 | 0.92 / 0.90 | 0.93 / 0.94 | 0.92 / 0.90 | 0.93 / 0.94 | 0.92 / 0.90 |
| Tooltip over the canvas, scale 1 / 1.5 | 0.76 / 0.78 | not shown¹ / 0.73 | 0.76 / 0.78 | not shown¹ / 0.73 | 0.76 / 0.78 | not shown¹ / 0.73 |
| Canvas after dock float, re-dock, resize storm | exact ×3 | exact ×3 | exact ×3 | exact ×3 | exact ×3 | exact ×3 |
| Frames presented during the 60-step storm | 120 | 106 | 64 | 66 | 60 | 60 |
| Move to the scale-1.5 output and back | ratio 1→1.5→1, exact | ratio stays 1², exact | ratio 1→1.5→1, exact | ratio stays 1², exact | ratio 1→1.5→1, exact | ratio stays 1², exact |
| Fractional-scale canvas alignment | exact³ | n/a² | exact³ | n/a² | exact³ | n/a² |
| Focus returns after a modal dialog | 3/3 | 3/3 | 3/3 | 3/3 | 3/3 | 3/3 |
| Validation messages while running | 10 shown (VUID-00067, every frame) | same | 10 shown (VUID-12478) | same | n/a | n/a |
| Validation messages at teardown | 0 | 0 | 0 | 0 | n/a | n/a |
| Input events handled in 3 s (250 Hz synthetic drag) | 748 | 372 | 169 | 170 | 746 | 747 |
| Event to submission p50 / p95 (ms) | 16.05 / 17.54 | 16.65 / 17.41 | 16.64 / 16.97 | 16.64 / 17.35 | 0.92 / 1.47 | 0.97 / 1.45 |
| Render start to submission p50 (ms) | 1.01 | 1.02 | 16.49 | 16.52 | 0.90 | 0.88 |

Notes:

- ¹ On X11 the scale-1 tooltip was never shown: Qt reported it hidden, identically for all three
  surfaces, so it does not depend on the adapter. Recorded for S1-7.
- ² `xwayland.force_zero_scaling`: X11 clients render 1:1 in device pixels.
- ³ Hyprland rounds a half-pixel surface origin up. Content is texel-exact with no resampling.
- The validation layer stops reporting a message ID after 10 repeats. The two GPU surfaces report only
  these Qt-internal IDs: the VK_EXT_swapchain_colorspace and API-version messages S1-1 found in ArchMage's
  own instance setup were fixed before this run.
- Submission is the hand-off point each surface can observe; photon latency was not measured. Budget
  for comparison: brush preview p95 ≤ 25 ms (spec §25.2).

Not run, with reasons:

- Physical monitors at mixed scale: headless outputs were used instead, to leave the user's displays alone.
- Real-cursor alignment: needs pointer injection that moves the user's cursor.
- Tablet input: no tablet attached (plan Q-03), so no pressure was logged; the mapper is unit-tested
  with synthetic tablet events (`InputMapperTest`).
- Device loss: no injection mechanism yet (S1-6).
- Compositors other than Hyprland; accessibility of the embedded native window (AT-025, later
  milestones).
