# ArchMage Stage 1 Development Plan

**Plan version:** 1.0
**Date:** 2026-10-11
**Status:** Planning document awaiting user review. No implementation exists yet.
**Authoritative source:** `docs/ArchMage_Full_Development_Specification.md` (document version 1.0, 11 October 2026). "Spec §n" below refers to that file. Where this plan and the spec disagree, the spec wins and this plan must be corrected.
**Audience:** implementation sessions executing one sub-milestone at a time, and the project owner reviewing scope and decisions.

## 0. How to use this plan

- Read §1 (scope) and §2 (decisions) once. Decisions have IDs `D-nn`; cite them in code comments and ADRs instead of re-deriving them.
- Each implementation session executes exactly one sub-milestone from §4 (`S1-0` through `S1-9`), in order. Do not start the next until the current exit criteria pass.
- Before a session: read `docs/implementation-status.md` (created in S1-0), confirm the previous sub-milestone's exit criteria still pass, then begin.
- After a session: update `docs/implementation-status.md`, write the progress report (§8.2 template), commit.
- Tasks are `S1-n.m`. Every task has a definition of done (DoD). A task is done when its DoD is observed, not inferred.
- §6.3 lists open questions. Items marked blocking must be answered before the referenced task; others proceed under the stated default.

## 1. Stage 1 scope

### 1.1 What "Stage 1" means

The spec does not use the phrase "Stage 1". It defines milestones M0 through M9 (spec §28) and a "first development iteration" (spec §29.1) whose step 10 reads "Complete M0 evidence and M1 scope, then proceed through the remaining milestones." The first reviewable demonstration is defined in the same section.

**D-01: Stage 1 = M0 (Inception and feasibility) + M1 (Trustworthy raster foundation)**, executed as one program with the M0 risk experiments front-loaded and exiting at the M1 gate. Rationale: spec §29.1 bundles them; M0 deliverables (toolchain, architecture decisions, canvas prototype) are inputs to M1; M1's exit gate is the first gate with user-visible acceptance tests. See Q-01.

Stage 1 ends when the M1 exit gate passes (spec §28 M1): AT-001, AT-002, AT-013, AT-014 and the initial navigation/input checks pass, and "a user can perform useful simple edits without a crash or lost save." The result is an internal alpha, not feature-complete ArchMage.

### 1.2 M0 deliverables and experiments (spec §28 M0)

Deliverables:

| # | Deliverable | Where in this plan |
| --- | --- | --- |
| M0-D1 | Repository audit or clean project initialization | S1-0 (no repository exists; clean init) |
| M0-D2 | Toolchain and license manifest | S1-0 `docs/dependencies/manifest.yaml` (spec §5.3) |
| M0-D3 | Feature ledger | S1-0 infrastructure, S1-9 Stage 1 entries (spec §3.2); scope per D-30 |
| M0-D4 | Architecture decisions | S1-0 ADRs, updated at gate G1 (S1-1) and S1-6 |
| M0-D5 | Real canvas/input prototype | S1-1 |
| M0-D6 | Memory and format experiments | S1-2 (tile budget), S1-4 (container), S1-6 (probes) |
| M0-D7 | Test corpus plan | S1-0 `docs/testing/corpus-plan.md` (spec §25.3, §27.4) |

Required experiments (spec §28 M0):

| # | Experiment | Sub-milestone |
| --- | --- | --- |
| M0-E1 | Qt docking plus GPU canvas, menus, tool overlays, mixed DPI, tablet coordinates | S1-1 |
| M0-E2 | CPU/GPU tile compositing with alpha, one blur halo, one profile conversion, measurable brush latency | S1-2 (CPU), S1-3 (latency), S1-6 (GPU, blur, profile) |
| M0-E3 | Native snapshot save, reopen, kill-during-save recovery | S1-4, S1-5 |
| M0-E4 | Representative layered PSD decoding and rendering with explicit unsupported records | S1-6 (decode probe; flat raster layers rendered) |
| M0-E5 | A RAW file, a shaped Arabic/Indic text sample, and a basic macOS GPU path | S1-6 (RAW and text probes; macOS recorded unavailable per spec §29.2) |
| M0-E6 | Resource estimates on profiles A–D or a clear record of unavailable machines | S1-6 (A measured, C simulated, B and D recorded unavailable) |

M0 exit (spec §28): "The chosen architecture can execute these slices, limitations are recorded, and high-risk decisions have evidence. If a presentation or storage choice fails, change its adapter now."

### 1.3 M1 deliverables (spec §28 M1), decomposed

The spec line reads: "Build/packaging skeleton, usable shell, document tabs, tile engine, command registry, basic brush/eraser, layers, simple selections, source-over composition, common image I/O, native save/reopen, undo/redo, autosave recovery, headless validation."

| M1 item | Stage 1 interpretation | Spec basis | Sub-milestone |
| --- | --- | --- | --- |
| Build/packaging skeleton | CMake/Ninja/presets, test tiers, CLI target; `packaging/` holds a stub (installers are M9) | §5.2, §27.4 | S1-0 |
| Usable shell | Menu bar with the nine menus populated only with working actions, options row, tool strip, document area, right dock (color, layers), status bar, minimal home screen with recent and recovery lists, one dark theme with violet accent | §2.2, §2.3, §29.2 | S1-1, S1-7, S1-8 |
| Document tabs | Multiple open documents, tab switching, close with unsaved prompt; split and detach deferred | §2.2, NAV-08 subset | S1-7 |
| Tile engine | Sparse 128² tiles, lazy allocation, uniform-tile compaction, copy-on-write across revisions, dirty regions, versions, RAM budget with eviction after journaling, channel-generic planes | §7.1, §6.3, §30 retrofit row | S1-2, S1-5 |
| Command registry | Typed commands with stable IDs, parameter versions, targets, base revision, request IDs, preconditions, labels, undo records; serialized executor | §9.1, §9.3, App. A.2 | S1-2 |
| Basic brush/eraser | Round tip; size, hardness, spacing, opacity; pressure to size when a tablet is present; one stroke is one transaction; in-progress preview transaction; eraser as erase mode; clipped by selection | §16.1 PNT-01/02/03/05 subsets, §16.3, §6.2 | S1-3, S1-8 |
| Layers | Raster layers only: add, remove, rename, reorder, duplicate, visibility, opacity, full lock, integer offset placement; content identity separate from instance | §13.1 LAY-02/03 subsets, §6.1 | S1-2, S1-7 |
| Simple selections | Rectangular and elliptical marquee as fractional coverage fields; replace, add, subtract via modifiers; select all, deselect, invert; marching-ants overlay; undoable editing state | §12.1 SEL-01/SEL-06 subsets | S1-8 |
| Source-over composition | CPU reference compositor with the §7.4 premultiplied equations, straight-alpha storage, encoded blend policy | §7.4, §7.5 | S1-2 |
| Common image I/O | PNG and JPEG open (embedded ICC detected and recorded; untagged assumed sRGB with a notice) and export (full-quality render, sRGB tag, JPEG matte policy) | §22.1, §22.4, §7.3 | S1-8 |
| Native save/reopen | `.archmage` SQLite container with the §10.1 logical tables, §10.3 save transaction, Save versus Save As, unknown-payload preservation | §10.1–10.3, §10.5 | S1-4 |
| Undo/redo | Exact raster undo via retained tile versions; coalescing for opacity drags; clean-save-point tracking | §9.2 | S1-2, S1-7 |
| Autosave recovery | Recovery workspace, 2 s journal cadence, discovery by document/session identity, recovery list on home screen, never overwrite the original | §10.2, §10.4 | S1-5, S1-8 |
| Headless validation | `archmage-cli validate`, `inspect`, `render`, `recover`; core tests without a display | App. A.4, §1.2, §10.5 | S1-0, S1-4, S1-8 |

Also required by spec §29.1 steps 8 and 9: malformed-file tests (S1-5) and GPU evaluation of the same narrow slice compared with the CPU reference (S1-6).

### 1.4 Components in scope (spec §5.5)

Created in Stage 1: `apps/archmage/`, `apps/archmage-cli/`, `core/document/`, `core/commands/`, `core/tiles/`, `core/render/`, `core/color/` (skeleton), `core/brush/`, `io/native/`, `io/formats/`, `ui/`, `resources/`, `tests/`, `benchmarks/`, `fuzz/`, `docs/`, `packaging/` (stub), `cmake/`, `scripts/`.

Reserved, not populated with implementation: `core/vector/`, `core/text/`, `core/timeline/`, `io/psd/` (the S1-6 probe lives in `benchmarks/probes/`), `services/`, `sdk/`.

### 1.5 Architecture in scope

- Boundaries per spec §5.4: UI to validated commands to document revisions to processing graph to CPU evaluator (GPU experimental) to color-managed presentation; document to save and recovery. No panel mutates engine structures directly (spec §9.1).
- Document entities per spec §6.1, Stage 1 subset: Document, Layer (raster), Raster content, Resource, Revision. Reserved fields (serialized, validated, not consumed): boards/pages, timeline, masks, effects, transform beyond integer offset, capability flags (spec §6.4, §30 retrofit row).
- Invariants per spec §6.2 enforced in Stage 1: deterministic order; references resolve; commands validate locks and targets; panels observe immutable revisions; selection, active layer, and view state are distinct from the document; unknown schema content is preserved; document identity survives Save, Save As creates a new identity.
- Coordinates per spec §6.3: int64 tile addressing and extents, checked allocation arithmetic, double document geometry, half-open rects, documented pixel-center convention, one centralized view-to-document transform.
- Processing graph per spec §7.2, minimal: layer source nodes plus one source-over composite node. The node contract (bounds, required input region, halo, cache key) is real and is exercised by the S1-6 blur node so later filters need no redesign.
- Alpha and blending per spec §7.4. GPU contract per spec §7.5 on the experimental path only.
- Commands, history, and jobs per spec §9.
- Native format and recovery per spec §10.1–10.5 (migration framework present, no real migrations yet).
- Tool lifecycle per spec §11 (activate, configure, begin, preview, commit, cancel, suspend, resume, deactivate) for brush, eraser, marquee rect, marquee ellipse, hand, zoom, eyedropper.

### 1.6 Acceptance criteria for Stage 1

From spec §27.2:

| Test | Pass condition (spec) | Stage 1 automation |
| --- | --- | --- |
| AT-001 | Create, paint, save, close, reopen: pixels, layers, color policy, precision, names, and relevant editability survive | Headless core/io test plus an offscreen Qt UI test driving the real menu actions |
| AT-002 | Stroke undo/redo at tile boundaries: exact raster restoration, no seams, no random reseeding | Headless scripted stroke crossing at least four tile corners; byte-exact compare across undo/redo cycles; seed field asserted stable |
| AT-013 | Interrupted saves: last completed save opens; latest acknowledged durable recovery opens independently | Subprocess kill harness at every §10.4 fault point |
| AT-014 | Disk exhaustion mid-operation: no damaged original, no fake success, useful retry or save-elsewhere route | RLIMIT_FSIZE subprocess plus injected ENOSPC; UI test asserts the error state and the Save As route |

"Initial navigation/input checks" (spec §28 M1) are defined here as: NAV-01 zoom (wheel, fit, 100%, zoom tool), NAV-02 pan (hand tool, Space), NAV-04 transparency grid, wheel-versus-numeric separation (spec §11), tablet pressure path alignment at scale 1 when a tablet exists (AT-017 precursor), Escape/Enter commit-cancel lifecycle, single-letter shortcuts suppressed inside text inputs (App. B).

First reviewable demonstration (spec §29.1): launch, open/create, paint on a second layer, change opacity, undo/redo, save, close, reopen, export; the project file remains editable. S1-8 automates it offscreen and S1-9 runs it manually.

M0 evidence per §1.2, recorded in `docs/decisions/` and `docs/implementation-status.md`. Progress report per spec §29.3 with at least one sample `.archmage` document.

### 1.7 Explicitly deferred

| Deferred item | Spec | Target |
| --- | --- | --- |
| Groups, masks/channels, clipping, blend modes beyond Normal, adjustments, embedded/linked objects, styles, layer comps, transform/crop, editable text/shapes, linked resources | §12.2, §13, §14.1, §15.1, §17 basics | M2 |
| History branching, named snapshots, persisted history, history search | §9.2 | M2 (see Q-07) |
| Linear-light blend policy consumer; canvas size and crop | §7.4, GEO-03 | M2 |
| Full selection suite (lasso, wand, refine), retouch, filters, channel calculations, contextual action bar | SEL-02 to SEL-10, §14.2, §15.2 | M3 |
| ICC assign/convert commands, display-profile monitoring, per-monitor presentation, OCIO/HDR, proofing and print, CMYK/Lab/indexed/grayscale modes, RAW development, PSD import/export, TIFF/EXR/WebP and other formats, isolated decoder workers | §8, §18, §22 | M4 (M0 probes only in Stage 1) |
| Brush editor, dynamics beyond pressure to size, stabilization, symmetry, natural media, vector inking, typography, artboards, comics | §16, §17 | M5 |
| Procedural workspace, all AI capabilities | §19, §21 | M6 |
| Animation, pixel art and indexed color, 3D, scientific | §20, §21.2–21.3 | M7 |
| Actions, batch CLI, scripting SDK, plugins, collaboration, portable packaging | §23, §24, App. A.4 `batch` | M8 |
| Saved workspaces, light and high-contrast themes, density presets, command palette, shortcut editor, full accessibility pass, localization, installers, Windows/macOS qualification | §2.3, §2.4, NAV-07, §26.3, §26.4, §31 | M2 to M9 (see Q-10) |

Stage 1 does include: tooltips, accessible names, and shortcuts on every shipped tool (spec §2.3); keyboard access to menus and the layers list (spec §26.3 baseline); no animated navigation, so reduced motion is trivially satisfied.

## 2. Upfront decisions

Each decision becomes an ADR in `docs/decisions/` during S1-0 (one file per decision, `ADR-00nn-<slug>.md` numbered to match `D-nn`; sections: context, decision, alternatives, consequences, evidence). Decisions marked **(gate)** are provisional until the named experiment reports.

### 2.1 Toolchain and build

- **D-02 Language and compilers.** C++20 (spec §5.2), no compiler extensions. GCC 16.2 is the primary compiler on the development machine; Clang 22.1 is required for the sanitizer, fuzzing, and clang-tidy presets. Minimum supported: GCC 13, Clang 17 (recorded in the manifest, not tested in Stage 1).
- **D-03 Build system.** CMake 3.28 or newer with Ninja; `CMakePresets.json` is the single entry point. Configure presets: `dev-debug` (GCC, Debug), `dev-release` (GCC, RelWithDebInfo), `asan-ubsan` (Clang, Debug, address and undefined sanitizers), `tsan` (Clang), `fuzz` (Clang, libFuzzer, `ARCHMAGE_ENABLE_FUZZ=ON`), `ci` (GCC, RelWithDebInfo, warnings as errors). Build trees live under `build/<preset>/`. cmake and ninja are not installed on the development machine; S1-0 installs them.
- **D-04 Dependency management.** Stage 1 uses system packages from pacman, found via CMake `find_package` and pkg-config imported targets for libraries without CMake configs (lcms2, libjpeg, libraw). Pinned versions, licenses, redistribution notes, maintenance status, and affected features are recorded in `docs/dependencies/manifest.yaml` (spec §5.3). A vcpkg manifest is deferred until the first Windows or macOS build is attempted; CMake code must use imported targets only and never assume pacman paths. See Q-06.
- **D-05 Test framework.** GoogleTest and GoogleMock 1.18 (installed) through CTest. Qt Test for UI interaction tests under `QT_QPA_PLATFORM=offscreen`. CTest labels per spec §27.1 tier: `math`, `operation`, `document`, `image`, `ui`, `interchange`, `resilience`, `perf`. One reproducible entry point per tier: `ctest --preset <preset> -L <label>` (spec §5.2). libFuzzer targets exist only under the `fuzz` preset.
- **D-06 Repository layout.** Spec §5.5 verbatim (§1.4). Each module directory has `include/archmage/<module>/` for public headers and `src/` for implementation. Module targets are `archmage_<module>` (for example `archmage_tiles`), aggregated into the interface target `archmage_core`. Namespace `archmage::<module>`.
- **D-07 Core is Qt-free.** No Qt in `core/`, `io/`, or `apps/archmage-cli/` (spec §5.2 "No GUI objects in document or processing APIs"; spec §1.2 headless testability). Supporting libraries: `nlohmann-json` (command envelope JSON and CLI `--json`), `spdlog`/`fmt` (logging), `cli11` (CLI parsing). nlohmann-json and cli11 are not installed; S1-0 installs them.
- **D-22 Version control.** `git init` in S1-0. `main` always configures, builds, and passes the `math`, `operation`, and `document` tiers. One branch per sub-milestone named `s1-<n>-<slug>`, merged when its exit criteria pass. Commit subjects are imperative; bodies cite task IDs. Hosting and CI: see Q-05; until decided, `scripts/check.sh` is the gate and a GitHub Actions workflow file is written but may be inert.
- **D-23 Code quality.** `.clang-format` (LLVM base, 4-space indent, 110 columns), `.clang-tidy` (bugprone, performance, modernize subsets), `-Wall -Wextra -Wpedantic -Wshadow -Wconversion` on all presets, `-Werror` only in `ci`. Engine code reports failures through `std::expected<T, Error>` with structured domain errors (spec §26.2), not exceptions.
- **D-24 Threading and cancellation.** `std::jthread` pool sized `max(2, hardware_concurrency - 2)`; `std::stop_token` is the cancellation token; one mutation executor thread serializes commits (spec §9.3). Jobs carry identity, source revision fingerprint, and target IDs from the first implementation.

### 2.2 Engine

- **D-08 Tile configuration.** 128 × 128 tiles (spec §7.1), tile size recorded in the document manifest. Channel-planar layout behind the tile interface (simplest for masks, N-channel data, and GPU upload; interleaved can be added behind the same interface later per spec §7.1). Sample types in the type system: `uint8`, `uint16`, `float16`, `float32`; Stage 1 exercises `uint8` and `float32` end to end and compiles the others. Uniform tiles, including fully transparent ones, are a single sample value, not an allocation.
- **D-09 Compositing.** CPU reference in `float32` premultiplied working representation using exactly the spec §7.4 source-over equations. Stored tiles keep straight (unassociated) samples (spec §7.4). Straight-to-premultiplied conversion happens at evaluation with a documented policy: alpha 0 preserves stored color and contributes nothing; NaN and Inf are sanitized to 0 with a diagnostic counter; negative float values are preserved and clamped only at `uint8` encoding. The document blend policy field has values `encoded` and `linear`; Stage 1 implements `encoded` and rejects `linear` with a clear error until M2.
- **D-10 Color.** `core/color/` defines `ColorProfileRef`, `ColorPolicy`, and a `ColorPipeline` with named stages (decode, working, operations, view, display; spec §8.3). Stage 1 implementation: working space is the document profile (default sRGB generated by lcms2 `cmsCreate_sRGBProfile`, hash pinned in a test), view is identity, display is assumed sRGB. Little CMS is linked for embedded-profile parsing on import, sRGB embedding on export, and the S1-6 conversion experiment. No assign/convert commands in Stage 1 (M4).
- **D-11 Canvas presentation (gate G1, decided in S1-1).** Primary candidate: `QVulkanWindow` inside `QWidget::createWindowContainer` within the dock layout (spec §5.2 "renderer-owned Qt window/surface; Vulkan first"). Fallback candidate: `QRhiWidget` (public API since Qt 6.7 with a limited compatibility guarantee; choosing it records a deviation from "avoid a private Qt rendering dependency"). Required regardless: a plain `QWidget` raster canvas painting CPU-composited tiles, used for safe mode and device-loss fallback (spec §26.2, §7.5). S1-1 measures both candidates against the M0-E1 list and records the decision in `ADR-0011`.
- **D-12 GPU scope.** The CPU evaluator is authoritative in Stage 1. A Vulkan compute-shader source-over compositor for the narrow slice is implemented in S1-6 as an opt-in and compared with the CPU reference per spec §7.5 tolerances. Shaders compile at build time with `glslangValidator` to SPIR-V; no runtime shader compiler in the interaction path.
- **D-15 Command model.** In-process typed commands: `CommandId` string `archmage.<area>.<verb>`, parameter struct with `parameterVersion`, `targetIds`, `baseRevision`, `requestId` (idempotency enforced by the executor over a bounded retained-result window), `transaction {id, label}`, preconditions (lock, target existence, mode compatibility), resource estimate, undo record. Results: committed revision, validation error, or conflict (spec App. A.2). Interactive previews run inside an in-progress transaction invisible to panels until commit (spec §6.2, §9.1). History is linear in Stage 1 with a clean-save-point marker (spec §9.2); branching is M2.
- **D-16 Identifiers.** 128-bit random IDs from a CSPRNG, serialized as `<type>-<26-char Crockford base32>` with `type` in {`doc`, `layer`, `raster`, `res`, `rev`, `tx`, `req`, `sess`, `job`}. Names are separate fields and never used as references.
- **D-17 Coordinates.** Exactly spec §6.3: `int64_t` tile and extent coordinates, checked multiplication for allocation sizes, `double` document geometry, half-open `IRect` and `DRect`, pixel centers at +0.5. One `ViewTransform` type owns zoom, pan, and DPI mapping; tools receive document coordinates only.
- **D-19 Hashing and compression.** SHA-256 via OpenSSL libcrypto EVP for content addressing (spec §5.2); XXH3 for in-memory cache keys only, never persisted as identity. Zstandard level 3 default for tile and resource blobs with codec id and version recorded per blob.
- **D-20 Selection.** Single-channel `float32` sparse coverage field over document space (spec §12.1) with antialiased ellipse rasterization. Combination: add is max, subtract is `a × (1 − b)`, plus replace and invert. Session editing state, undoable through a selection command kind recorded in history without bumping the document revision (spec §6.2). Not persisted in Stage 1 (named selections are SEL-07, M3).
- **D-26 Resource coordinator.** `core/tiles/ResourceCoordinator` holds four budgets (RAM, VRAM, scratch, history; spec §7.1). Stage 1 enforces RAM (default half of physical memory, spec §25.4) by evicting disposable caches first (mips, thumbnails), then spooling authoritative tiles that are already journaled (D-14), never dirty unjournaled tiles. VRAM and scratch are accounted; only the experimental GPU path's VRAM is bounded. A reserved 64 MiB pool stays available for error, save, and recovery paths (spec §7.1).

### 2.3 Persistence

- **D-13 Native container.** `.archmage` is a SQLite database implementing the spec §10.1 logical tables (`manifest`, `objects`, `relations`, `resources`, `tile_maps`, `revisions`, `assets`, `previews`) as a first physical schema. Stage 1 writes `schema_major = 0` (pre-release) with a `schema_minor` that increments on any change; the migration framework exists with an ordered step registry and a test proving every Stage 1 minor version still opens. Schema major 1 is frozen at the M2 exit after indices, blob limits, and compaction are benchmarked (spec §10.1). Files with a newer required major open read-only with the stored composite preview (spec §10.5). See Q-07.
- **D-14 Recovery workspace.** `${XDG_STATE_HOME:-~/.local/state}/archmage/sessions/<sess-id>/<doc-id>.recovery`, a SQLite WAL database (spec §10.2), journaled every 2 s when dirty (spec §10.4) by a dedicated thread that writes committed revisions only. Durability status (`in_memory`, `journaled`, `saved`) is tracked per revision, exposed internally and in the status bar. Discovery scans the sessions directory and reads identity from each manifest, never from filenames. Recovery files survive until a replacement save or explicit discard. Network filesystems are detected via `statfs` and refused for the workspace with a documented fallback to the local state directory.
- **D-18 Image codecs.** libpng and libjpeg-turbo directly, behind `io/formats/ImageCodec` adapters with a capability record per format (open, export, alpha, profile, precision; spec §22.1). Decoding is in-process in Stage 1 with hard limits (300,000 px per axis, decoded bytes checked against the RAM budget, chunk sanity) and fuzz coverage; isolated decoder workers arrive with the broad format set in M4 (spec §26.1). OpenImageIO is not used in Stage 1.
- **D-27 Fault injection.** A compile-time option `ARCHMAGE_FAULT_INJECTION` (on in Debug and test presets, off in `dev-release`) enables `ARCHMAGE_FAULT=<point>[:<n>]` to abort (`_exit(137)`) or return a synthetic error at named points in the save, journal, checkpoint, rename, and compaction paths. Disk exhaustion is produced with `RLIMIT_FSIZE` on a subprocess plus an `enospc@<point>` synthetic error. Fault points are enumerated in `io/native/FaultPoints.h`; a test asserts every enumerated point is exercised.

### 2.4 Application

- **D-21 Platforms in Stage 1.** Linux only: Wayland (Hyprland native path through `qt6-wayland`, which is not installed and is required) and X11/XWayland. Core and io use `std::filesystem` plus a small `platform/` layer for `fsync`, directory sync, atomic rename, and `statfs`; Windows and macOS implementations are compile-time stubs. No Windows or macOS builds are attempted; the gap is recorded (spec §29.2). See Q-09.
- **D-25 Distribution and license model.** Undecided and owned by the user (spec §5.3, §30 "M0 before dependency lock"). Stage 1 dependencies are permissive or LGPL; Qt is linked dynamically so no license path is foreclosed. No bundled fonts, brushes, or color libraries with unknown rights. See Q-02.
- **D-28 Visual system in Stage 1.** One dark theme with the violet accent (spec §2.3) via `QPalette` plus a small stylesheet; light and high-contrast themes are M2 or later. Original SVG icons only for shipped tools and panel actions (brush, eraser, marquee rect, marquee ellipse, hand, zoom, eyedropper, add/remove/duplicate layer, visibility, lock) with consistent stroke weight in `resources/icons/`. Every icon-only control has tooltip, accessible name, and shortcut text (spec §2.3).
- **D-29 Units.** Pixels only in the Stage 1 UI; the document stores resolution (default 72 DPI) and a unit preference; unit switching is NAV-03 (M2).
- **D-30 Feature ledger scope.** Stage 1 delivers `docs/parity/features.yaml` with the App. A.1 schema, a generator `scripts/parity_report.py` producing `docs/parity/report.md`, parent entries for every spec-tabled ID (NAV, SEL, MSK, LAY, GEO, RET, FIL, PNT, TXT, VEC, RAW, AI, ANM, PIX) in state `unstarted`, and child entries with real state for every behavior Stage 1 ships. The option-level benchmark audit against live Photoshop, Krita, and the other products (spec §3.1–3.2 "frozen inventory") is a research workstream a coding session cannot complete honestly; it is tracked as Q-08 and does not block Stage 1 code.

## 3. Sequencing and management approach

### 3.1 Principles (spec §28 M0 exit, §29.2, §30)

1. Risky adapters first: the presentation and input experiment (S1-1) runs before engine work beyond scaffolding because its outcome can replace the canvas adapter (spec §30 row 1, "latest sensible resolution: M0").
2. Vertical slices before scaffolding breadth (spec §29.2): the brush stroke reaches canvas, history, and tiles (S1-3) before the full layers panel exists.
3. Save and recovery before the tool palette grows (spec §29.1 step 8).
4. CPU correctness before GPU (spec §7.5); GPU is a comparison, not the default, in Stage 1.
5. A behavior counts only when interaction, engine behavior, undo, persistence, error handling, test, and status entry exist (spec §1.2 last bullet, §31).
6. Unfinished controls are hidden or labeled "(development)" (spec §29.2).

### 3.2 Sub-milestone graph

| ID | Name | Depends on | Produces | Gate |
| --- | --- | --- | --- | --- |
| S1-0 | Bootstrap and scaffolding | none | build, test tiers, docs scaffolds, git | G0: check scripts green on a clean clone |
| S1-1 | Presentation and input feasibility (M0-E1) | S1-0 | canvas adapter decision, input pipeline skeleton | G1: D-11 resolved with evidence |
| S1-2 | Core foundation (headless) | S1-0 | IDs, revisions, tiles, compositor, commands, history | G2: math, operation, document, image tiers green |
| S1-3 | Brush stroke vertical slice | S1-1, S1-2 | one real stroke to canvas, history, tiles; AT-002 | G3: AT-002 automated |
| S1-4 | Native format save and reopen | S1-2 | `.archmage` writer and reader, save transaction, CLI validate and inspect | G4: AT-001 engine half automated |
| S1-5 | Recovery, durability, malformed input | S1-4 | journal, recovery, fault injection, fuzz corpus; AT-013 and AT-014 headless | G5: AT-013 and AT-014 automated |
| S1-6 | GPU slice and M0 probes | S1-2, S1-3, S1-5 | Vulkan compositor comparison, blur halo, profile conversion, PSD/RAW/text probes, benchmark harness, profile records | G6: M0 evidence complete |
| S1-7 | Shell: documents, layers, navigation | S1-3, S1-4 | main window regions, tabs, layers panel, color, navigation tools, shortcuts | G7: navigation and input checks pass offscreen |
| S1-8 | Shell: selections, eraser, image I/O, CLI render, recovery UI | S1-5, S1-7 | marquee tools, eraser, PNG/JPEG open and export, home screen, recovery list, `render` and `recover` CLI | G8: first reviewable demonstration runs |
| S1-9 | Stage exit | S1-6, S1-8 | acceptance automation end to end, sanitizer runs, ledger entries, sample document, progress report | M1 gate |

Order for single-session execution: S1-0, S1-1, S1-2, S1-3, S1-4, S1-5, S1-6, S1-7, S1-8, S1-9. S1-6 may be swapped after S1-8 if an earlier demo is preferred; the M0 evidence must exist before S1-9.

### 3.3 Session protocol

1. Open this plan at §4 for the target sub-milestone and `docs/implementation-status.md`.
2. Re-run the previous gate's checks (`scripts/check.sh` plus the gate's named commands). If anything fails, fix it first and record the fix.
3. Create branch `s1-<n>-<slug>`.
4. Execute tasks in order; commit per task or coherent pair. Do not skip a DoD.
5. Run the sub-milestone exit criteria. Record commands, outputs, and artifact paths in `docs/implementation-status.md` under the sub-milestone heading.
6. Write the progress report section (§8.2). Merge to `main`.
7. If a §2 decision proves wrong, write a new ADR superseding it and add a one-line pointer in this plan's §2; never diverge silently (spec §29.2, §30).

### 3.4 Definition of done, per task

Code compiles under `dev-debug` and `asan-ubsan`; the named tests exist, carry their label, and pass; new public types have a header comment stating ownership and thread safety; `docs/implementation-status.md` reflects the task; nothing outside the task's stated files changed without a note.

## 4. Task breakdown

Conventions: paths are relative to the repository root; "test" means a GoogleTest case unless stated; `[label]` is the CTest label; size is a guide (S under an hour of focused work, M a few hours, L most of a session).

### S1-0 Bootstrap and scaffolding

Goal: a reproducible C++20/Qt/CMake build with a headless test executable and the documentation scaffolds (spec §29.1 steps 1–4). Spec: §5.2, §5.3, §5.5, §27.4, §29.1.

| Task | Description | DoD | Size |
| --- | --- | --- | --- |
| S1-0.1 | Install prerequisites after Q-11: `cmake ninja qt6-wayland vulkan-headers vulkan-validation-layers nlohmann-json cli11`; confirm `openssl` present | `cmake --version` is 3.28 or newer, `ninja --version` works, `pacman -Q` succeeds for each package; versions recorded in the manifest (S1-0.8) | S |
| S1-0.2 | `git init`; `.gitignore` (build/, .cache/, *.user, recovery dirs), `.editorconfig`, `.clang-format`, `.clang-tidy`; initial commit containing `docs/` and these files | `git log` shows the commit; `git status` clean | S |
| S1-0.3 | Top-level `CMakeLists.txt` (project, options, C++20) with `cmake/CompilerWarnings.cmake`, `cmake/Sanitizers.cmake`, `cmake/Dependencies.cmake`, `cmake/FindLCMS2.cmake`; `CMakePresets.json` with the six D-03 presets | `cmake --preset dev-debug` and `cmake --preset asan-ubsan` configure without missing-package warnings | M |
| S1-0.4 | Module skeleton per D-06: `core/{document,commands,tiles,render,color,brush}`, `io/{native,formats}`, `ui/`, `apps/{archmage,archmage-cli}`, `tests/`, `benchmarks/`, `fuzz/`, `resources/`, `packaging/README.md`, `scripts/`; each module has a CMake target and one real header (`Version.h` generated in core/document) | `cmake --build --preset dev-debug` builds all targets; `archmage-cli --version` prints `ArchMage 0.1.0 (<git sha>)` | M |
| S1-0.5 | Test infrastructure: `tests/CMakeLists.txt` with an `archmage_add_test(name LABELS ...)` helper; `tests/core/VersionTest.cpp`; `tests/ui/SmokeTest.cpp` (Qt Test, constructs and destroys a `QApplication` offscreen); D-05 label set registered | `ctest --preset dev-debug` runs at least two tests, all pass; `ctest -L ui` runs without a display | M |
| S1-0.6 | Desktop app skeleton: `apps/archmage/main.cpp` creating `QApplication` and an empty `QMainWindow` titled "ArchMage"; exits cleanly on close | Under `QT_QPA_PLATFORM=wayland`, `hyprctl clients -j` lists a client with class `archmage`; under `xcb` it also opens; exit code 0 on close | S |
| S1-0.7 | `scripts/check.sh` (configure `dev-debug`, build, ctest for the non-UI tiers), `scripts/check-ui.sh` (offscreen UI tier), `scripts/check-sanitizers.sh` (`asan-ubsan` build plus full ctest); each prints the commands it runs | All three exit 0 from a clean `build/` | S |
| S1-0.8 | `docs/dependencies/manifest.yaml`: one entry per dependency with source, pinned version, license, redistribution note, maintenance status, affected features (spec §5.3); includes compilers, Qt modules, fonts and fixtures (none yet) | Every `find_package` in `Dependencies.cmake` has an entry; `tests/docs/manifest_test.py` (run with `python3 -I`) asserts that | M |
| S1-0.9 | Docs scaffolds (spec §29.1 step 2): `docs/implementation-status.md` (per-sub-milestone sections; state vocabulary implemented / automatically tested / manually tested / verified against benchmark per §29.3), `docs/architecture/overview.md` (§5.4 diagram, module map, thread model from D-24), `docs/decisions/ADR-0001` through `ADR-0030` (one per §2 decision; provisional ones flagged), `docs/parity/features.yaml` (App. A.1 schema plus D-30 parent entries), `docs/parity/README.md`, `scripts/parity_report.py` producing `docs/parity/report.md`, `docs/testing/corpus-plan.md` (fixtures to create or obtain per §25.3 and §27.4 with a licensing column) | Files exist with real content; `python3 -I scripts/parity_report.py --check` validates the YAML and regenerates the report with zero diff | L |
| S1-0.10 | Project `CLAUDE.md`: build and test commands, plan path, status path, session protocol pointer (§3.3), no sub-agents, one sub-milestone per session | File exists, under 80 lines | S |
| S1-0.11 | CI stand-in: `.github/workflows/linux.yml` running `scripts/check.sh` and `scripts/check-ui.sh` in an Arch container (inert until Q-05 is answered) | Workflow YAML parses; `actionlint` passes if available | S |

Exit (G0): the three check scripts are green from a fresh clone; the empty window opens under Wayland and X11; docs scaffolds and manifest exist; commits on `main`.

### S1-1 Presentation and input feasibility (M0-E1, gate G1)

Goal: decide the canvas adapter (D-11) with evidence and leave behind the input pipeline the brush slice needs. Spec: §5.2 (Qt embedding limitations [S20]), §28 M0-E1, §2.2, §6.3, §11, §16.1 tablet samples, §26.4 Linux/Wayland, §30 row 1.

| Task | Description | DoD | Size |
| --- | --- | --- | --- |
| S1-1.1 | `ui/canvas/CanvasSurface` interface (present a tile set at a `ViewTransform`; overlay callback; DPI-change signal; device-loss signal) with three implementations: `VulkanWindowSurface` (`QVulkanWindow` in a window container), `RhiWidgetSurface` (`QRhiWidget`), `RasterWidgetSurface` (`QWidget::paintEvent`) | All three compile and display a synthetic checkerboard plus a colored tile grid | L |
| S1-1.2 | Experiment harness `archmage --experiment=canvas:<impl>`: main window with menu bar, one floating and one docked `QDockWidget`, a popup menu and tooltip over the canvas, an options toolbar, a status bar | Harness runs for each implementation under Wayland and XWayland | M |
| S1-1.3 | Evidence matrix in `docs/decisions/ADR-0011-canvas-adapter.md`: popup/menu/tooltip stacking over the canvas; dock float, re-dock, resize without canvas corruption; window move across the three attached monitors with one temporarily set to Hyprland scale 1.5 (restore afterward); fractional-scale cursor and canvas alignment; focus return after a dialog; resize storm stability; teardown with `VK_LAYER_KHRONOS_validation` clean | Matrix filled per implementation and backend (Wayland, X11) with screenshots under `docs/decisions/evidence/S1-1/` | L |
| S1-1.4 | Input pipeline skeleton: `ui/input/PointerSample {device, timestamp_ns, pos_device, pos_doc, pressure, tilt, buttons, modifiers}`; `QTabletEvent`, `QMouseEvent`, and wheel/pinch mapped to samples; wheel over the canvas routes to navigation, never to numeric controls (spec §11) | Unit test on the mapper with synthetic events; tablet pressure logged if a tablet exists, else "no tablet available" recorded in the ADR | M |
| S1-1.5 | `core/render/ViewTransform` (D-17): device, logical, view, and document mappings; zoom about a point; pan; DPI; round-trip property test | `[math]` tests pass: exact at integer zoom, within 1e-9 otherwise | M |
| S1-1.6 | Latency instrumentation: timestamps at input receipt, sample enqueue, render submit, present callback; `ARCHMAGE_TRACE=latency` prints p50/p95 per second | Synthetic drag reports numbers for each implementation; recorded in the ADR | S |
| S1-1.7 | Decision: complete `ADR-0011`, pick the primary adapter, confirm the raster safe-mode path; keep the non-chosen GPU implementation behind an option marked experimental | ADR committed; `docs/implementation-status.md` records G1 with evidence paths | S |

Exit (G1): ADR-0011 names the adapter with its evidence matrix; `RasterWidgetSurface` works as safe mode; input pipeline tests pass; D-11 in this plan gains a pointer to the ADR.

### S1-2 Core foundation (headless)

Goal: document IDs and revisions, typed raster tiles, a CPU source-over compositor, and basic command transactions (spec §29.1 step 5). Spec: §6.1–6.4, §7.1, §7.2, §7.4, §9.1–9.3, §25.4, App. A.2, A.3.

| Task | Description | DoD | Size |
| --- | --- | --- | --- |
| S1-2.1 | `core/document/Id.h` (D-16): typed ID, generation, parse and format, hashing | `[math]` tests: round trip; no collision over 1e6 draws | S |
| S1-2.2 | `core/tiles/`: `TileCoord` (int64), half-open `IRect` with checked arithmetic, `SampleType`, `ChannelLayout` (planes with semantic tags), `Tile` (immutable after seal; uniform-value representation), `TileMap` (sparse, copy-on-write via shared tiles plus version), dirty-region tracking | `[math]` tests: a 300,000² empty map allocates zero tiles; uniform compaction; COW (modifying a duplicate leaves the source unchanged); overflow rejected on `width × height × bytes` | L |
| S1-2.3 | `core/tiles/ResourceCoordinator` (D-26): budgets, accounting hooks, eviction callback registration, reserved pool | Tests: exceeding the budget evicts disposable entries first; dirty authoritative tiles are never evicted without a spool target | M |
| S1-2.4 | `core/document/`: `Document`, `Layer` (raster, fields per §1.5), `RasterContent`, `Revision` (parent, root state, label, durability), immutable `DocumentState` snapshot; reserved fields for boards, timeline, masks, effects, capability flags (spec §30 retrofit row) | `[document]` tests: construct, snapshot, structural sharing between revisions, invariant checks (unique IDs, resolvable references, deterministic order) | L |
| S1-2.5 | `core/commands/`: `Command` base with the App. A.2 envelope fields, `CommandRegistry`, `MutationExecutor` (single thread, serialized commits, requestId idempotency with a retained-result window), `History` (linear undo/redo, clean save point, coalescing key), `Transaction` (grouped commands, in-progress preview state) | `[operation]` tests: duplicate requestId applies once; undo restores the exact prior state; clean marker behaves across undo past the save point (spec §9.2) | L |
| S1-2.6 | Layer commands `archmage.layer.add`, `remove`, `rename`, `reorder`, `duplicateInstance`, `setVisible`, `setOpacity`, `setLocked`, `setOffset`; `archmage.document.create` | `[operation]` tests per command: validation (locked layer rejects edits; unknown target errors), result, undo/redo, JSON parameter round trip | M |
| S1-2.7 | `core/render/`: `GraphNode` contract (type and version, inputs/outputs, bounds, required input region per output tile, halo, cache key from parameter hash plus source revision plus time plus quality), `GraphCompiler` (layer list to source nodes plus composite node), `CpuEvaluator` (per-tile pull evaluation on the pool with cancellation), `RenderRequest` per App. A.3 (region, rational time, quality, destination channel model, sample type, alpha association) | `[image]` tests: a 3-layer document equals a hand-computed reference; cancellation stops within one tile; cache hits on an unchanged revision | L |
| S1-2.8 | `core/render/kernels/SourceOver`: spec §7.4 equations in float32 premultiplied; straight/premultiplied conversion; alpha-zero, NaN, negative policy (D-09); layer opacity as coverage scale | `[math]` fixtures: opaque over opaque, 50 % over transparent, zero-alpha hidden color preserved in storage, NaN sanitized, uint8 round trip exact for opaque pixels | M |
| S1-2.9 | `core/color/` skeleton (D-10): `ColorProfileRef`, `ColorPolicy {profile, blendPolicy}`, `ColorPipeline` stage interfaces; lcms2 wrapper to load a profile and expose description and hash; generated sRGB with its hash pinned | `[math]` tests: profile parse, hash stability, identity pipeline is a no-op | M |
| S1-2.10 | Job model (D-24): `JobScheduler` with `std::stop_token`, job identity, source revision fingerprint, target IDs, completion reconciliation per spec §9.3 steps 1–4 | `[operation]` test: a job completing after its target layer was deleted becomes a candidate, not a commit | M |
| S1-2.11 | Structured errors `core/document/Error.h`: categories per spec §26.2 (unsupported_input, damaged_input, cancelled, resource_exhausted, provider_unavailable, permission_denied, internal), code, user message, diagnostic | Used by all S1-2 APIs; every category formats in a test | S |

Exit (G2): `ctest -L "math|operation|document|image"` green under `dev-debug` and `asan-ubsan`; `docs/architecture/overview.md` names the real types; a 10,000-layer snapshot test runs in under 1 s as a sanity check.

### S1-3 Brush stroke vertical slice (gate G3)

Goal: one genuine brush stroke connected to the canvas, history, and tile store (spec §29.1 step 6). Spec: §16.1 PNT-01/02/03/05 subsets, §16.3, §9.1, §6.2, §7.3, §25.2 brush budget, AT-002.

| Task | Description | DoD | Size |
| --- | --- | --- | --- |
| S1-3.1 | `core/brush/StrokeInput`: ordered `PointerSample` list with timestamps; spatial resampling by spacing (fraction of diameter) with a documented policy; no sample discarded in a way that changes the final line (spec §16.1) | `[math]` tests: identical dab positions regardless of arrival batching | M |
| S1-3.2 | `core/brush/RoundDab`: antialiased round dab with hardness falloff rasterized into tile-aligned coverage; pressure to diameter with a linear default curve; mouse uses a fixed diameter | `[math]` tests: bounds, symmetry, hardness extremes, coverage monotonic in diameter | M |
| S1-3.3 | `archmage.raster.paintStroke` command: parameters (layer, color, size, hardness, spacing, opacity, mode normal/erase, samples); dabs accumulate on a scratch map then composite onto the layer with flat stroke opacity; one history item per stroke; undo retains prior tile versions | `[operation]` tests: paint, undo, redo byte-exact; locked layer rejected; hidden layer allowed with a warning | L |
| S1-3.4 | In-progress transaction for preview (spec §6.2): dabs accumulate in a `StrokeTransaction` observed only by the canvas; Escape cancels and restores the pre-stroke revision; release commits one history item | `[operation]` test: cancel leaves the revision unchanged and no committed tile allocated | M |
| S1-3.5 | Canvas wiring: `ui/tools/BrushTool` implementing the spec §11 lifecycle; dirty-region re-composite of affected tiles only; presented via the G1 adapter | Manual: the stroke appears while dragging; after release it is in history | M |
| S1-3.6 | AT-002 automation: scripted stroke crossing at least four tile corners on a 1,000 × 1,000 layer at offset (−37, 59); ten undo/redo cycles; byte-exact compare with pre- and post-stroke snapshots; seam check along tile borders in the composite | `[operation]` and `[image]` tests pass | M |
| S1-3.7 | Latency measurement: event-to-submission p95 on a 4096² RGBA8 canvas with synthetic 240 Hz input; results in `benchmarks/results/<date>-brush.json` with every spec §25.1 record field (OS, compositor, driver, compiler, power mode, thermal conditions, storage, monitor scale, exact fixture hash) | Numbers recorded; p95 ≤ 25 ms noted as pass or fail, no tuning yet | M |

Exit (G3): AT-002 passes under `dev-debug` and `asan-ubsan`; a manual stroke with undo/redo on the real canvas; latency record committed.

### S1-4 Native format save and reopen (gate G4)

Goal: save and reopen the layered document through the native format (spec §29.1 step 7). Spec: §10.1, §10.2, §10.3, §10.5, §5.2 storage row, App. A.4 `validate` and `inspect`.

| Task | Description | DoD | Size |
| --- | --- | --- | --- |
| S1-4.1 | Physical schema v0.1 in `io/native/schema/0001.sql`: the eight logical tables; indices on `(raster_id, version, tx, ty, plane)` and `(resource_hash)`; `manifest` holds app id, schema major/minor, required capabilities, tile size, color policy, document identity, current revision | Schema applies to an empty database; `PRAGMA integrity_check` returns ok | M |
| S1-4.2 | Blob codec: zstd-compressed tile planes and resources with header {codec id, codec version, raw length, sha256}; verification on read | `[document]` tests: round trip; a tampered byte yields `damaged_input`, never a wrong tile | M |
| S1-4.3 | `io/native/NativeWriter`: serialize a `DocumentState` (versioned JSON object payloads, relations, tile maps, resources, one revision row, a composite preview at 512 px or smaller) | Written file reopens and compares equal structurally and byte-exact for tiles | L |
| S1-4.4 | `io/native/NativeReader`: load manifest; validate schema compatibility (newer major opens read-only with preview and explanation); resolve references; preserve unknown object payloads as opaque records with their relations (spec §10.5) | `[document]` tests: an unknown object type survives open then save; a newer major opens read-only | L |
| S1-4.5 | Save transaction per spec §10.3 steps 1–7 in `io/native/SaveTransaction`: capture revision; write `<name>.archmage.tmp-<rand>` beside the target via the SQLite backup API; finalize and close; validate; `fsync` file, atomic `rename`, `fsync` directory; mark the revision saved only after success; keep the previous file until replacement completes; detect non-atomic filesystems and use the documented fallback | `[document]` tests on `/tmp` and on the home filesystem: no `-wal` or `-shm` sidecars; edits during save leave the document modified; previous file intact when any step fails (hooks exercised fully in S1-5) | L |
| S1-4.6 | Save versus Save As identity (spec §6.2): Save keeps the `doc` id; Save As assigns a new id and records `derived_from` | `[document]` tests | S |
| S1-4.7 | Migration framework: ordered step registry keyed by (major, minor); `tests/io/native/fixtures/` holds one file per written minor version; a test opens every fixture | Passes with the v0.1 fixture; a new minor without a fixture fails the test | M |
| S1-4.8 | `archmage-cli validate <file> [--deep]` (structure, references, hashes with `--deep`) and `inspect <file> --json` (manifest, objects, revision, resource summary); exit codes 0/1/2 (ok, invalid, error); JSON error records (spec §23.3, App. A.4) | `[interchange]` subprocess tests on the fixture and a corrupted copy | M |
| S1-4.9 | AT-001 engine half: create, paint (S1-3 command), save, reopen, compare pixels, layer names/order/opacity/visibility, color policy, sample type | `[document]` test passes | S |

Exit (G4): AT-001 engine half green; `validate` and `inspect` work on a saved sample; `docs/architecture/native-format.md` describes schema v0.1 and the save transaction with its fsync and rename sequence.

### S1-5 Recovery, durability, fault injection, malformed input (gate G5)

Goal: forced-termination recovery and malformed-file tests before the tool palette grows (spec §29.1 step 8). Spec: §10.2, §10.4, §26.1, §26.2, §7.1 spooling, AT-013, AT-014, §27.1 tier 7.

| Task | Description | DoD | Size |
| --- | --- | --- | --- |
| S1-5.1 | Fault injection (D-27): `FaultPoints.h` enumerating `tile_write`, `tx_commit`, `backup`, `checkpoint`, `rename`, `compaction`, `journal_write`, `dir_sync`; `ARCHMAGE_FAULT` parser; abort and synthetic-error modes | Test asserts every enumerated point is reachable by at least one test | M |
| S1-5.2 | Recovery workspace (D-14): session directory, per-document WAL database with the same schema, journal thread writing committed revisions on a 2 s cadence when dirty, durability status per revision, network-filesystem refusal | `[document]` tests: a commit appears in the journal within 2.5 s; status moves in_memory to journaled | L |
| S1-5.3 | Recovery discovery and load: enumerate sessions, read manifest identity, produce `RecoveryCandidate {doc id, session id, title, timestamp, path, revision, thumbnail}`; open as a new unsaved document (never overwrite the original); explicit discard | `[document]` tests: candidate found by identity after the file is renamed; original untouched | M |
| S1-5.4 | Spool-to-journal eviction (D-26): journaled tiles may leave RAM and reload from the journal on demand; a 20,000² sparse document under a 512 MiB budget never exceeds budget plus reserved pool | `[document]` test with budget assertions and reload correctness | L |
| S1-5.5 | AT-013 harness `tests/resilience/KillDuringSave`: child process (`archmage-cli --selftest-save <fault>`) killed at each fault point; parent asserts the previous save opens and the latest acknowledged journaled revision recovers; three repetitions per point | `[resilience]` tests pass for all points | L |
| S1-5.6 | AT-014 harness: `RLIMIT_FSIZE`-constrained child saving a document larger than the limit, plus `enospc@tile_write`; asserts original intact, `resource_exhausted` with the affected path, document still modified, journaling continues, Save As elsewhere succeeds | `[resilience]` tests pass | M |
| S1-5.7 | Malformed native corpus: truncated file, wrong magic, schema major 99, missing resource, hash mismatch, 300,001 px dimension, cyclic relation, 1 GiB claimed blob length; reader fails within budget with the right error category and no allocation beyond declared limits | `[resilience]` tests; corpus generator script checked in | M |
| S1-5.8 | libFuzzer harnesses `fuzz/native_reader_fuzz.cpp` and `fuzz/png_fuzz.cpp` (PNG target activates in S1-8); ten-minute run per target under the `fuzz` preset seeded from the corpus | No crashes or leaks; command and result recorded in `docs/testing/fuzz.md` | M |
| S1-5.9 | `archmage-cli recover --session <id> --output <file>` (App. A.4): lists candidates without arguments; never overwrites an existing output | `[interchange]` subprocess test | S |

Exit (G5): AT-013 and AT-014 pass headless under `dev-debug` and `asan-ubsan`; fuzz run recorded; `docs/architecture/recovery.md` documents durability states and the "last acknowledged durable revision" guarantee (spec §10.4).

### S1-6 GPU slice and M0 probes (gate G6)

Goal: GPU evaluation of the narrow slice compared with the CPU reference (spec §29.1 step 9) plus the remaining M0 experiments and resource records. Spec: §7.5, §7.2 halo, §8.2 and §8.5 (one conversion), §22.2 PSD probe, §18.1 RAW probe, §17.1 shaping, §25.1–25.3, §28 M0-E2/E4/E5/E6.

| Task | Description | DoD | Size |
| --- | --- | --- | --- |
| S1-6.1 | Vulkan device, queue, and memory bootstrap in `core/render/gpu/` (headless), validation layers in Debug, VRAM budget registered with the coordinator, device-loss detection with CPU fallback | `[image]` test skips cleanly without a Vulkan device; otherwise creates and destroys a device with no validation errors | M |
| S1-6.2 | SPIR-V compute shader for source-over over a tile batch, compiled at build time; `GpuEvaluator` for the composite node only; readback used in tests only (spec §7.5) | `[image]` test: GPU versus CPU on the AT-002 fixture and a random 20-layer fixture; uint8 exact, float32 within 1e-6 absolute | L |
| S1-6.3 | Gaussian blur `GraphNode` with required input region equal to output plus radius, CPU only; seam test with high-contrast features across tile borders (spec §15.3) | `[image]` test: tiled result equals the whole-image reference within 1 LSB for uint8 | M |
| S1-6.4 | Profile conversion experiment: lcms2 sRGB to an Adobe RGB (1998)-equivalent profile built from primaries, perceptual and relative colorimetric with black-point compensation, on a neutral ramp and saturated patches; assign versus convert demonstrated (assign leaves samples unchanged; convert changes them within tolerance) | `[math]` tests with numeric expectations recorded in `docs/decisions/evidence/S1-6/color.md` | M |
| S1-6.5 | PSD decode probe `benchmarks/probes/psd_probe.cpp`: bounded parse of header, color mode data, image resources, layer and mask info for a fixture exported from GIMP 3.2 or Krita 6.0 (both installed; Q-04); list layers, blend keys, and every unrecognized tagged block as explicit unsupported records; render flat raster layers through the CPU compositor to PNG | Probe output and PNG committed under evidence with limitations listed | L |
| S1-6.6 | RAW probe `raw_probe.cpp`: LibRaw opens a DNG or RAW fixture (Q-04), reports camera, orientation, black/white levels, demosaics to a float32 tile map, exports PNG | Evidence committed; an unsupported-file diagnostic is demonstrated with a non-RAW input | M |
| S1-6.7 | Text shaping probe `text_probe.cpp`: HarfBuzz shapes an Arabic and a Devanagari sample with Noto Sans (Devanagari font to be installed, Q-04); FreeType rasterizes into a tile; logs glyph count and cluster map | Evidence PNG and log committed | M |
| S1-6.8 | Benchmark harness `benchmarks/bench_main.cpp`: brush preview, warm pan/zoom on a synthetic 24 MP 20-layer composite, pixel update after stroke, small undo, launch time (scripted); JSON output with the spec §25.1 record fields; `scripts/bench_record.py` appends to `benchmarks/results/` | Profile A results committed; profile C simulated with `ARCHMAGE_FORCE_CPU=1`; B and D recorded unavailable | L |
| S1-6.9 | macOS GPU path recorded as unavailable with the exact reproduction instructions that would be run (spec §29.2) | Entry in `docs/implementation-status.md` | S |
| S1-6.10 | Update ADRs with evidence (GPU viability, blur contract, color pipeline) and the M0 evidence table in `docs/implementation-status.md` | Every M0-E row has an evidence path or an explicit unavailable record | S |

Exit (G6): every M0 experiment has evidence or an explicit unavailable record; the CPU/GPU comparison test exists and passes or skips with a reason; benchmark results committed.

### S1-7 Shell: documents, layers, navigation (gate G7)

Goal: a usable shell around the slice. Spec: §2.2, §2.3, §11 NAV-01/02/04/05 subsets, §13.1 LAY-02/03 subsets, §9.2 coalescing, App. B, §26.3 baseline.

| Task | Description | DoD | Size |
| --- | --- | --- | --- |
| S1-7.1 | `ui/MainWindow` with the spec §2.2 regions: menu bar (nine menus, only working actions visible), options row (about 36 px), left tool strip (about 44 px, one column), document area (`QTabWidget`), right dock (Color above Layers), status bar (zoom, document coordinates, durability status, job indicator) | Offscreen UI test asserts the widgets exist and the initial geometry at 1440 × 900 is within 10 % of the targets | L |
| S1-7.2 | Document tabs: open several documents, switch, close with Save/Discard/Cancel; title modified marker driven by the clean save point | UI test: modify, undo back to saved, marker clears (spec §9.2) | M |
| S1-7.3 | `ui/panels/LayersModel` (`QAbstractListModel` over the current snapshot, thumbnails from a bounded cache) and `LayersPanel` (`QListView`, inline rename, drag reorder, visibility and lock toggles, add/remove/duplicate, opacity slider with coalesced history) | UI test: a 2,000-layer document shows the panel without 2,000 widgets; an opacity drag yields one history item | L |
| S1-7.4 | Color panel: foreground/background swatches, swap (X), reset (D), `QColorDialog`; `EyedropperTool` sampling the composite (NAV-05 subset) | UI tests for swap and reset; eyedropper checked manually | M |
| S1-7.5 | Navigation: `ZoomTool` (click, Alt-click, drag), `HandTool`, Space temporary hand, wheel zoom about the cursor, fit and 100 % actions bound to Ctrl+0 and Ctrl+1 (conventional additions; App. B lists no zoom-level shortcuts), transparency grid; no animated navigation | UI tests with synthetic wheel and mouse events assert `ViewTransform` changes; wheel over a spin box does not zoom (spec §11) | M |
| S1-7.6 | Shortcut profile from App. B for shipped actions (New, Open, Save, Save As, Undo, Redo, Select All, Deselect, Invert, New layer, Duplicate, B, E, M, H, Z, I, X, D, [, ], Tab, Enter, Escape); single-letter shortcuts suppressed while a text input has focus | UI test: pressing B inside the rename editor types "b" and does not switch tools | M |
| S1-7.7 | Tool lifecycle framework `ui/tools/Tool` per spec §11 (focus loss, Escape, Enter, document switch defined); options row rebuilt per active tool | UI test: switching documents mid-stroke follows the documented cancel rule | M |
| S1-7.8 | Theme (D-28): dark palette, violet accent, icons for shipped tools; tooltips with shortcut text; accessible names on all icon-only controls | UI test: every `QAction` with an icon has a tooltip and accessible name | M |
| S1-7.9 | Keyboard access: menus via Alt, tab order through panels, layers list operable with arrows, Space, F2 | UI test drives layer selection and rename by keyboard only (AT-025 precursor) | S |

Exit (G7): offscreen UI tier green; manual Wayland run covering create, paint, opacity, undo/redo, reorder, zoom, pan; navigation and input checks listed with results in `docs/implementation-status.md`.

### S1-8 Shell: selections, eraser, image I/O, CLI render, recovery UI (gate G8)

Goal: finish the M1 feature list and the first reviewable demonstration. Spec: §12.1 SEL-01/06 subsets, §16.1 PNT-05, §22.1, §22.4, §7.3, §8.2 embedded profiles, §10.4 recovery UI, App. A.4 `render`.

| Task | Description | DoD | Size |
| --- | --- | --- | --- |
| S1-8.1 | `core/document/Selection` coverage field (D-20): rect and antialiased ellipse rasterization, combine ops, bounds, emptiness; repeated add/subtract must not harden edges (spec §12.1) | `[math]` tests pass | M |
| S1-8.2 | Selection commands `archmage.selection.replace`, `add`, `subtract`, `invert`, `all`, `none` as undoable context commands that leave the revision unchanged | `[operation]` tests | M |
| S1-8.3 | `MarqueeRectTool` and `MarqueeEllipseTool` with Shift add, Alt subtract, Shift-drag constraint; marching-ants overlay rendered from the coverage field (visualization only) | UI tests with synthetic drags assert coverage bounds; overlay checked manually | M |
| S1-8.4 | Brush and eraser clipped by selection (PNT-05): dab coverage multiplied by selection coverage; `EraserTool` is the brush with erase mode | `[operation]` tests: painting outside the selection changes nothing; erasing inside reduces alpha | M |
| S1-8.5 | `archmage.raster.fillSelection` with the foreground color (SEL-10 subset) and the Edit menu action | `[operation]` test | S |
| S1-8.6 | `io/formats/PngCodec` and `JpegCodec` (D-18) with capability records; open yields a single-layer document with the embedded ICC recorded as the document profile, untagged input assumed sRGB with a compatibility notice; dimension and allocation limits; fuzz seeds | `[interchange]` tests: round trips; 16-bit PNG mapping decision recorded; CMYK JPEG rejected as `unsupported_input` | L |
| S1-8.7 | Export: `RenderRequest` at final quality, never the viewport (spec §7.3), to PNG (straight alpha, sRGB tag) or JPEG (matte color, default white; quality); File menu Export dialog with format, quality, matte; Save As stays separate (spec §22.4) | `[interchange]` test compares export to the CPU reference render; UI test for the dialog | M |
| S1-8.8 | `archmage-cli render <file> --output <png> [--region ...] [--quality final]` (App. A.4) | `[interchange]` subprocess test equals the in-app export byte for byte | S |
| S1-8.9 | Home screen: New, Open, Recent (bounded list), Recoveries (S1-5.3 candidates with thumbnail, timestamp, path, revision; Open recovery and Discard); no promotional content (spec §2.3) | UI test: a planted recovery candidate appears and opens as unsaved | M |
| S1-8.10 | Error presentation (spec §26.2): non-modal bar for background failures, modal only for direct user actions; save failure offers Retry and Save As | UI test driving `enospc@rename` asserts the bar and the Save As route | M |
| S1-8.11 | First reviewable demonstration as `tests/ui/FirstDemoTest`: launch, New, paint on a second layer, opacity, undo/redo, Save, close, reopen, export; the saved file validates and remains editable (a layer can be added after reopen) | Test passes; manual run recorded with a capture under evidence | M |

Exit (G8): the demonstration passes offscreen and manually under Wayland; every M1 item in §1.3 has an implementation and a test; `docs/implementation-status.md` lists each with its state vocabulary.

### S1-9 Stage exit (M1 gate)

Goal: evidence, reports, and the M1 exit gate. Spec: §28 M1 exit, §29.3, §3.2, §27.4, §31.

| Task | Description | DoD | Size |
| --- | --- | --- | --- |
| S1-9.1 | Full matrix: `scripts/check.sh`, `check-ui.sh`, `check-sanitizers.sh`, `tsan` on the `operation` and `document` tiers, ten-minute fuzz per target; fix or document every failure | All green, or each failure has an entry in `docs/implementation-status.md` with its owning milestone | L |
| S1-9.2 | `docs/testing/acceptance.md`: AT-001, AT-002, AT-013, AT-014 and the navigation and input checks with exact test names, commands, and last-run output paths | Every test name resolves in `ctest -N` | S |
| S1-9.3 | Feature ledger child entries (D-30) with real state and fidelity for every shipped behavior; regenerate the report | `scripts/parity_report.py --check` passes; counts match the status document | M |
| S1-9.4 | Sample documents: `docs/samples/stage1-demo.archmage` (two layers, strokes, 60 % opacity) plus its exported PNG and the `validate --deep` output | Files committed; validate exit code 0 | S |
| S1-9.5 | Manifest and ADR refresh: versions re-verified, dependencies added during Stage 1 recorded, provisional ADRs resolved or carried to M2 with a note | Manifest test passes; no ADR left provisional without a carry-forward note | S |
| S1-9.6 | Benchmark refresh on the final build (S1-6.8) compared against the spec §25.2 budgets; record only | Results JSON committed; pass/fail table in the progress report | M |
| S1-9.7 | Progress report per spec §29.3 (§8.2 template) in `docs/implementation-status.md`, separating implemented, automatically tested, manually tested, and verified against benchmark; open risks and M2 entry conditions | Report committed; user review requested | M |
| S1-9.8 | Tag `v0.1.0-alpha.1` on `main` | Tag exists; `archmage-cli --version` shows it | S |

M1 gate: AT-001, AT-002, AT-013, AT-014 and the navigation and input checks pass; the demonstration runs; no known data-loss defect, false save success, or fake control exists; M0 evidence is complete.

## 5. Testing and verification strategy

### 5.1 Tiers (spec §27.1 mapped to Stage 1)

| Tier | Label | Stage 1 content | Entry point |
| --- | --- | --- | --- |
| 1 Mathematical | `math` | IDs, rects and overflow, tile addressing, source-over equations, straight/premultiplied, dab geometry, view transform, selection combine, profile hash, blur reference | `ctest --preset dev-debug -L math` |
| 2 Operation | `operation` | every command: validation, result, cancellation, undo/redo, dirty bounds, JSON round trip; idempotency; job reconciliation | `-L operation` |
| 3 Document | `document` | snapshots, structural sharing, invariants, save and reopen, Save As identity, unknown payloads, migrations, journaling, recovery discovery, budgets and eviction | `-L document` |
| 4 Image | `image` | CPU reference renders, CPU versus GPU, seams across tile borders, export equals render | `-L image` |
| 5 UI | `ui` | offscreen Qt Test workflows: regions, tabs, layers panel, shortcuts, tool lifecycle, first demonstration, error routes | `scripts/check-ui.sh` |
| 6 Interchange | `interchange` | native fixtures, PNG and JPEG round trips, CLI subprocess tests | `-L interchange` |
| 7 Resilience | `resilience` | kill-during-save matrix, disk exhaustion, malformed corpus, fuzz seeds | `-L resilience`; fuzz under the `fuzz` preset |
| 8 Performance | `perf` | benchmark harness; records only, not a gate in Stage 1 | `benchmarks/bench_main` |
| 9 Human | none | the user runs the demonstration under Wayland and X11 after S1-8 and S1-9; a tablet stroke if hardware exists | manual, recorded |

Prefer one acceptance-style workflow test over many trivial unit tests (spec §27.1).

### 5.2 Comparison policy (spec §27.3)

Exact byte comparison for `uint8` tiles, stored blobs, undo/redo, and integer-document export. `float32`: absolute 1e-6 for compositing; per-operation tolerances declared in each color test and recorded in evidence. Pixel comparisons always carry structural assertions (layer count, names, sample type, profile hash, dimensions). Goldens live in `tests/fixtures/golden/` with a README stating how each was produced; updating one requires a commit message explaining why.

### 5.3 Sanitizers, fuzzing, static checks

`asan-ubsan` runs the full non-UI suite at every sub-milestone exit. `tsan` runs the `operation` and `document` tiers from S1-5 onward (journal thread plus executor). libFuzzer targets for the native reader and PNG adapter run ten minutes each at S1-5 and S1-9 with the malformed corpus as seeds. `clang-tidy` runs on changed files via `scripts/tidy.sh` (advisory in Stage 1).

### 5.4 Fixtures and corpus (spec §25.3, §27.4)

Only small fixtures live in the repository: synthetic PNG and JPEG under 1 MiB, native fixtures per schema minor, the AT-002 stroke fixture, the malformed corpus. Larger corpora (24 MP, 100 MP, 16k, PSD, RAW) are generated by `scripts/make_fixtures.py` where possible or obtained with a recorded license (CC0 RAW samples; PSD exported from the installed GIMP or Krita) and stored under `~/.local/share/archmage-dev/corpus/` with a manifest in `docs/testing/corpus-plan.md`.

### 5.5 Not verified in Stage 1

Windows and macOS builds; profiles B and D; real tablet behavior unless hardware appears; HDR; display color accuracy; compositors beyond Hyprland; accessibility beyond keyboard reachability. Each is listed in `docs/implementation-status.md` as a gap with reproduction instructions (spec §29.2).

## 6. Risks, ambiguities, and open questions

### 6.1 Risks and mitigations

| Risk | Impact | Mitigation | Where |
| --- | --- | --- | --- |
| `QVulkanWindow` embedding fails popup, dock, or DPI checks (spec §5.2 [S20]) | Canvas adapter rework | S1-1 runs the full matrix before engine work; `QRhiWidget` and raster fallbacks already built; decision at G1 | S1-1 |
| Atomic rename or fsync assumptions wrong on the user's filesystem | Data loss | S1-4 records the `statfs` type and tests on `/tmp` and the home filesystem; fallback path documented | S1-4, S1-5 |
| 2 s journaling competes with painting | Latency regression | Journal thread writes committed revisions only, batched; measured in S1-6.8 with journaling on | S1-5, S1-6 |
| Eviction drops a tile still referenced by an in-progress stroke or job | Corruption | Residency pins per transaction and job; only journaled unpinned tiles evict; tested in S1-5.4 | S1-5 |
| GPU/CPU tolerance hides real bugs | Wrong renders later | Exact for uint8; float tolerance declared per test; seams tested separately | S1-6 |
| No pen tablet on the machine | Brush latency and pressure path unmeasured (spec §30 brush row) | Synthetic measurement in S1-3.7; gap recorded; Q-03 | S1-3 |
| Only Hyprland available; mixed DPI needs a temporary config change | Incomplete M0-E1 evidence | Set one monitor to scale 1.5 with `hyprctl keyword monitor`, restore afterward, record | S1-1 |
| Feature ledger audit is research, not code | M0 ledger deliverable partially met | D-30 scope; Q-08 | S1-0, S1-9 |
| Schema v0 files diverge before the v1 freeze | Early documents lost | Migration framework and per-minor fixtures from S1-4.7; v1 freeze at M2 exit | S1-4 |
| Rolling-distribution Qt or library upgrades mid-stage | Build breaks | Manifest pins exact versions; upgrades recorded as manifest changes with a rebuild check | continuous |
| In-process PNG/JPEG decoding of untrusted files | Crash or compromise | Hard limits, fuzzing, structured errors; isolation workers in M4 | S1-8 |
| Scope creep into M2 (groups, blend modes) during shell work | Stage never exits | §1.7 deferral table; hidden unimplemented menus; tasks outside §4 rejected | continuous |

### 6.2 Spec ambiguities and gaps found while planning

| ID | Issue | Spec | Plan assumption | User input |
| --- | --- | --- | --- | --- |
| A-01 | "Stage 1" is not a spec term | §28, §29.1 | D-01: M0 plus M1 | Q-01 |
| A-02 | M1 "simple selections", "common image I/O", "usable shell" are not enumerated | §28 M1 | §1.3 interpretations | Q-10 |
| A-03 | Stage 1 precision and modes unspecified; §8.1 requires many eventually | §8.1 | RGBA uint8 and float32 end to end; others compile only | Q-10 |
| A-04 | Schema v1 freeze timing versus "fixtures from every released schema" | §10.1, §10.5 | D-13: major 0 through M2 | Q-07 |
| A-05 | Display profile handling before M4 | §8.2 | Assume an sRGB display; record | none |
| A-06 | "Maintain Windows and macOS buildability early" with no such machines | §5.1 | D-21: portable code, no builds, gap recorded | Q-09 |
| A-07 | Profiles B and D unavailable | §25.1 | Recorded unavailable; C simulated | none |
| A-08 | M0 "feature ledger" implies the full benchmark freeze | §3.1–3.2, §28 M0 | D-30 | Q-08 |
| A-09 | PSD and RAW fixtures must be legally obtained | §22.2, §28 M0-E4/E5 | GIMP or Krita-exported PSD; CC0 RAW sample | Q-04 |
| A-10 | License model must precede dependency lock | §5.3, §30 | Dynamic Qt, permissive deps, decision deferred to the user | Q-02 |
| A-11 | Whether selection changes are history items | §6.2, §9.2 | D-20: undoable context commands, not revisions | none |
| A-12 | 2 s journal cadence versus painting throughput | §10.4 | Committed revisions only; measured | none |
| A-13 | History branching and snapshots have no milestone | §9.2 | M2 | Q-07 |
| A-14 | Tile size arithmetic (128² × 4 channels × 4 bytes = 256 KiB) | §7.1 | Confirmed, no contradiction | none |
| A-15 | §5.4 diagram shows workers feeding commands; Stage 1 has no workers | §5.4 | Job model (S1-2.10) is the seam; workers from M4 | none |
| A-16 | Minimum Qt, CMake, and compiler versions not stated | §5.2 | D-02 and D-03 minimums; Qt 6.7 or newer for the `QRhiWidget` fallback, 6.11 tested | none |

### 6.3 Open questions

| ID | Question | Blocking | Default if unanswered |
| --- | --- | --- | --- |
| Q-01 | Confirm Stage 1 = M0 plus M1 (D-01). | Before S1-1 | Proceed as M0 plus M1 |
| Q-02 | Distribution and license model (open-source license, proprietary, dual). Affects asset choices and static linking; does not block Stage 1 code. | Before the S1-9 manifest and ledger freeze | Dynamic Qt, permissive dependencies, no bundled third-party art |
| Q-03 | Is a pen tablet available for S1-3 and S1-6 measurements? Which model? | Before S1-3.7 | Synthetic input only; gap recorded |
| Q-04 | Fixture sourcing: export a layered PSD from the installed GIMP or Krita; download a CC0 RAW sample (raw.pixls.us); install a Devanagari Noto font package. | Before S1-6 | Yes to all three, recorded in the corpus plan |
| Q-05 | Repository hosting and CI (GitHub, local only, other)? | Before S1-0.11 | Inert GitHub Actions workflow; local scripts are the gate |
| Q-06 | Dependency strategy: system packages now and vcpkg at the first cross-platform build (D-04), or vcpkg from day one? | Before S1-0.3 | D-04 as written |
| Q-07 | Accept schema major 0 through M2 with the v1 freeze at the M2 exit (D-13)? Early `.archmage` files are migration-supported across Stage 1 minors only. | Before S1-4 | D-13 as written |
| Q-08 | Who performs the option-level benchmark audit for the feature ledger (spec §3.1–3.2), and when? It is research against live products, not a coding task. | Not blocking Stage 1 code | D-30: infrastructure plus spec-derived entries only |
| Q-09 | Accept Linux-only builds in Stage 1 with portability discipline (D-21)? | Before S1-0 | Yes |
| Q-10 | Confirm the M1 interpretations in §1.3 (marquee rect and ellipse only; PNG and JPEG only; RGBA uint8 and float32; one dark theme; no workspaces). | Before S1-7 | As written |
| Q-11 | Authorize installing the missing system packages (S1-0.1) at the start of the first implementation session. | Before S1-0.1 | The session asks before running `pacman -S` |

## 7. Handoff brief: S1-0 Bootstrap and scaffolding

Self-contained instructions for the first implementation session. Read §2 once; do not re-derive the decisions.

### 7.1 Goal

Produce a reproducible C++20, Qt 6, CMake, Ninja build with: a Qt-free core library skeleton, a headless CLI printing its version, an empty desktop window that opens natively under Wayland and X11, CTest with labeled tiers and an offscreen UI tier, sanitizer presets, check scripts, and the documentation scaffolds required by spec §29.1 step 2 and §5.3. No features. No engine code beyond `Version.h`.

### 7.2 Environment facts (verified 2026-10-11)

- Machine: CachyOS (Arch), kernel 7.2.7, Hyprland on Wayland, three monitors at scale 1 (1920 × 1200 at 165 Hz internal, 1920 × 1080 at 60 Hz, 1920 × 1080 at 240 Hz). CPU i7-13700HX (24 threads), 31 GiB RAM, NVMe with 329 GiB free, NVIDIA RTX 4050 Laptop 6 GiB (proprietary driver, Vulkan 1.4) plus Intel UHD 770 (Mesa). This matches spec profile A.
- Present: GCC 16.2.1, Clang 22.1.8, lld, gdb, clang-format, clang-tidy, pkgconf, Python 3.14 with PyYAML, git 2.55, Qt 6.11.2 (`qt6-base`, `qt6-svg`; CMake configs for Core, Gui, Widgets, Test, Svg; `QVulkanWindow` and `QRhiWidget` headers present), Vulkan ICD loader 1.4.357, vulkan-tools, shaderc, glslang, lcms2 2.19 (pkg-config only), sqlite 3.53.4, zstd 1.5.7 (CMake config present), openssl 3.6.4 (CMake config present), libpng 1.6.58, libjpeg-turbo 3.2, libtiff, libwebp, openexr, libraw 0.22, harfbuzz 14.5, freetype 2.14, icu 78, gtest 1.18, opencolorio 2.5, ffmpeg 9, xxhash, spdlog 1.17, fmt 12.2, yaml-cpp, mesa, GIMP 3.2.6, Krita 6.0.4, `noto-fonts` (Noto Sans Arabic present; Devanagari not detected), Noto Color Emoji.
- Missing, install in S1-0.1 after Q-11: `cmake`, `ninja`, `qt6-wayland` (without it Qt runs under XWayland only), `vulkan-headers`, `vulkan-validation-layers`, `nlohmann-json`, `cli11`. Optional: `ccache`, `mold`. Not needed in Stage 1: openimageio, catch2, benchmark, onnxruntime.
- No pen tablet detected (touchpad only).
- `/home/otectus/Projects/ArchMage` contains only `docs/` and is not a git repository. An empty, unrelated `.git` directory exists at `/home/otectus/Projects/.git`; ignore it and initialize inside `ArchMage`.

### 7.3 Steps

1. Ask for Q-11, then install: `sudo pacman -S --needed cmake ninja qt6-wayland vulkan-headers vulkan-validation-layers nlohmann-json cli11`. Record the versions.
2. `git init` in `/home/otectus/Projects/ArchMage`; add `.gitignore`, `.editorconfig`, `.clang-format`, `.clang-tidy`; commit `docs/` and these files as "Initialize ArchMage repository with specification and plan".
3. Create the D-06 layout, the top-level `CMakeLists.txt`, `cmake/*.cmake`, and `CMakePresets.json` with the D-03 presets. Targets: `archmage_document` (with `include/archmage/document/Version.h` generated from `Version.h.in` carrying the project version and git sha), `archmage_core` (INTERFACE aggregating module targets), `archmage_io` (real but empty static library), `archmage_ui` (static, links Qt Widgets), `archmage` (executable, empty window), `archmage-cli` (executable, cli11, prints version), tests, an empty `benchmarks` target, `fuzz` only under the `fuzz` preset.
4. `Dependencies.cmake`: `find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Widgets Test Svg)`, `SQLite3`, `ZLIB`, `PNG`, `JPEG`, `zstd` (target `zstd::libzstd_shared`), `OpenSSL` (Crypto), `GTest`, `Vulkan`, `nlohmann_json`, `CLI11`, `spdlog`, `fmt`, and `PkgConfig` for `lcms2`, `harfbuzz`, `freetype2`, `libraw` (found now, linked later). Every dependency gets a manifest entry even if not yet linked.
5. Tests per S1-0.5 using the `archmage_add_test` helper and D-05 labels; the UI smoke test sets `QT_QPA_PLATFORM=offscreen` through the CTest `ENVIRONMENT` property.
6. Scripts per S1-0.7; each prints the exact commands it runs.
7. Docs per S1-0.8 and S1-0.9. ADR files: one per §2 decision numbered to match (`ADR-0001-stage1-scope.md` for D-01 and so on), each under 60 lines, with `status: accepted` or `status: provisional (gate G1)` for D-11. Ledger parent entries: every ID in the spec tables of §11 through §20 (NAV-01 to 08, SEL-01 to 10, MSK-01 to 08, LAY-01 to 08, GEO-01 to 08, RET-01 to 08, FIL-01 to 10, PNT-01 to 10, TXT-01 to 08, VEC-01 to 08, RAW-01 to 10, AI-01 to 10, ANM-01 to 08, PIX-01 to 07) with `implementation_state: unstarted`, `milestone` from spec §28, and `benchmark.evidence: public_documentation_only`.
8. `CLAUDE.md` per S1-0.10 and the inert workflow per S1-0.11.
9. Run the G0 checks (§7.4), update `docs/implementation-status.md`, write the progress report, commit, merge to `main`.

### 7.4 Verification commands (all must pass)

```
cmake --preset dev-debug && cmake --build --preset dev-debug
ctest --preset dev-debug --output-on-failure
scripts/check.sh && scripts/check-ui.sh && scripts/check-sanitizers.sh
QT_QPA_PLATFORM=wayland ./build/dev-debug/apps/archmage/archmage &
hyprctl clients -j | grep -i archmage        # then close the window
QT_QPA_PLATFORM=xcb ./build/dev-debug/apps/archmage/archmage
./build/dev-debug/apps/archmage-cli/archmage-cli --version
python3 -I scripts/parity_report.py --check
python3 -I tests/docs/manifest_test.py
git status --porcelain                        # empty after the final commit
```

### 7.5 Do not

- Add any engine type beyond `Version.h` (S1-2 owns them), any menu with actions, any panel, or any icon artwork.
- Vendor dependencies or add vcpkg (Q-06).
- Pull Qt into `core/`, `io/`, or `archmage-cli`.
- Record anything in `implementation-status.md` as more than "implemented" and "automatically tested" for what the tests actually cover.

### 7.6 Report back

The exact commands and outputs for §7.4; installed package versions; the commit list; the ADR list; any deviation from §2 with its reason; and the state of Q-01 through Q-11 as understood by the session.

## 8. Appendices

### 8.1 Stage 1 command inventory

`archmage.document.create`; `archmage.layer.add`, `.remove`, `.rename`, `.reorder`, `.duplicateInstance`, `.setVisible`, `.setOpacity`, `.setLocked`, `.setOffset`; `archmage.raster.paintStroke` (modes normal and erase); `archmage.raster.fillSelection`; `archmage.selection.replace`, `.add`, `.subtract`, `.invert`, `.all`, `.none`.

Non-command services: open (native, PNG, JPEG), save, save as, export, recover, validate, inspect, render.

### 8.2 Progress report template (spec §29.3)

```
## S1-n <name>, <date>
Implemented outcomes:
Changed modules:
Passing evidence (commands and result paths):
Manual validation (what, how, platform):
Performance changes (benchmark result file):
Migrations:
Dependency changes (manifest diff):
Open risks:
State by behavior: implemented | automatically tested | manually tested | verified against benchmark
Sample document: docs/samples/... (from S1-4 onward)
```

### 8.3 Spec sections used by this plan

§1.2 non-negotiables; §2.2–2.3 layout and visual system; §3.2 ledger; §5.1–5.5 stack and layout; §6.1–6.4 document model; §7.1–7.5 tiles, graph, alpha, GPU; §8.2–8.3 color boundaries; §9.1–9.3 commands and jobs; §10.1–10.5 native format and recovery; §11 tool lifecycle and NAV; §12.1 SEL; §13.1 LAY; §16.1–16.3 brush; §22.1 and §22.4 formats and export; §25.1–25.4 budgets; §26.1–26.3 reliability and accessibility; §27.1–27.4 verification; §28 M0 and M1; §29.1–29.3 bootstrap protocol; §30 risks; App. A.1–A.4; App. B.
