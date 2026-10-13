# ArchMage Stage 2 Development Plan: M2, Layered editing and nondestructive structure

**Plan version:** 1.0
**Date:** 2026-10-13
**Status:** Planning document awaiting user review. Stage 2 has not started and cannot start yet (see §1.6).
**Authoritative source:** `docs/ArchMage_Full_Development_Specification.md` (document version 1.0, 11 October 2026). "Spec §n" below refers to that file. Where this plan and the spec disagree, the spec wins and this plan must be corrected.
**Predecessor:** `docs/plans/Stage1_Development_Plan.md` (plan version 1.0). Its decisions D-01 to D-30 and ADR-0001 to ADR-0030 remain binding; this plan adds D-31 to D-44 and never re-decides a Stage 1 decision silently.
**Naming:** the specification does not use the phrase "Stage 2". It defines milestones M0 to M9 (spec §28). Stage 1 was defined as M0 plus M1 (ADR-0001). The next stage the specification defines is **M2, "Layered editing and nondestructive structure"** (spec §28 M2). Inside this document "Stage 2" and "M2" mean the same thing; the file keeps the name `Stage2_Development_Plan.md` for continuity with the Stage 1 plan and the project instructions.
**Audience:** implementation sessions executing one sub-milestone at a time, and the project owner reviewing scope and decisions.

## 0. How to use this plan

- Read §1 once to understand what actually exists, then §2 (scope) and §3 (decisions). Decisions have IDs `D-nn`; cite them in code comments and ADRs instead of re-deriving them.
- Each implementation session executes exactly one sub-milestone from §5 (`S2-0` through `S2-12`), in order. Do not start the next until the current exit criteria pass.
- Before a session: read `docs/implementation-status.md`, confirm the previous sub-milestone's exit criteria still pass, then begin.
- After a session: update `docs/implementation-status.md`, write the progress report (§9.2 template), commit, merge.
- Tasks are `S2-n.m`. Every task has a definition of done (DoD). A task is done when its DoD is observed, not inferred.
- §7.3 lists open questions. Items marked blocking must be answered before the referenced task; others proceed under the stated default.
- **Entry condition:** no Stage 2 task may begin before the Stage 1 M1 gate is recorded as met (§1.6). A session that opens this plan while that is false executes the next Stage 1 sub-milestone instead.

## 1. Current state assessment

### 1.1 What was inspected (2026-10-13)

| Source | Observation |
| --- | --- |
| `git log` | 8 commits, all authored 2026-10-11 between 07:52 and 08:48 local time: `0cdf8ce` (initialize), `ff71b69`, `d463906`, `ed1132b`, `e4c1432`, `b447526` (S1-0 work), `b35b538` (merge), `5d32326` (S1-0 evidence). `main` and `s1-0-bootstrap` both point at `5d32326`. No tags. Working tree clean. Nothing has been committed since. |
| `git ls-files` | 86 tracked files. C++ totals 190 lines across 8 files: `core/document/.../Version.h.in` (27), `core/document/src/Version.cpp` (9), `ui/include/archmage/ui/MainWindow.h` (19), `ui/src/MainWindow.cpp` (10), `apps/archmage/main.cpp` (21), `apps/archmage-cli/main.cpp` (43), `tests/core/VersionTest.cpp` (28), `tests/ui/SmokeTest.cpp` (33). Build glue (CMake, scripts, workflow) totals 1,031 lines; documentation (Markdown and YAML, excluding the spec and plan) 6,819 lines. |
| Module targets | `core/{commands,tiles,render,color,brush}`, `io/{native,formats}` are source-less INTERFACE targets (`archmage_add_module()` with no `SOURCES`). Only `core/document` (version banner) and `ui` (empty `QMainWindow`) contain code. |
| Reserved directories | Absent: `core/vector`, `core/text`, `core/timeline`, `io/psd`, `services`, `sdk`, `docs/samples`, `tests/fixtures`, `tests/resilience`, `benchmarks/probes`, `resources/icons`. Consistent with ADR-0006 ("created when used"). |
| `docs/implementation-status.md` | "Completed: S1-0. Next: S1-1." S1-1 to S1-9 "Not started". Every M0 experiment (E1 to E6) and M0-D5/D6 "Not started". |
| `docs/decisions/` | ADR-0001 to ADR-0030 exist. ADR-0011 (canvas adapter) is `provisional (gate G1)` with every cell of its evidence matrix reading "not run". 17 ADRs carry "Evidence: None yet". |
| `docs/parity/` | Ledger valid: 121 parent entries, 0 child behaviors, baseline `unfrozen`. 21 parents are assigned to M2 (NAV 2, MSK 7, LAY 4, GEO 4, TXT 2, VEC 2); 25 of the 121 parent milestones are marked `inferred:`. |
| `docs/dependencies/manifest.yaml` | 33 entries covering 21 CMake lookups; `--check-installed` passes today. HarfBuzz, FreeType and LibRaw are found but not linked. fontconfig and ICU are installed on the machine but are not manifest entries. |
| `docs/testing/corpus-plan.md` | Register of corpus files: "none yet". |
| Check scripts, re-run on `5d32326` on 2026-10-13 | `scripts/check.sh`: 4 of 4 tests pass (`Version.*` ×3, `cli.version`), manifest test "33 entries cover 21 CMake lookups", ledger "121 entries; report up to date". `scripts/check-ui.sh`: `SmokeTest` 1 of 1, exit 0. `scripts/check-sanitizers.sh`: 5 of 5 under Clang ASan+UBSan. Logs were not kept. |
| Environment | Kernel `7.2.9-2-cachyos` (the reboot that status issue I-06 asked for has happened). NVIDIA kernel module `615.78.08` matches `nvidia-utils 615.78.08-2`. `vulkaninfo`: RTX 4050 Laptop (driver 615.78.8.0, API 1.4.351) and Intel UHD (Mesa 26.2.4). Hyprland 0.56.2. 24 hardware threads, 31 GiB RAM, 321 GiB free on the project filesystem. No pen tablet in `/dev/input/by-id`. |

### 1.2 Stage 1 intent versus delivery

| Sub-milestone (Stage 1 plan §3.2) | Intended output | Observed state | Evidence |
| --- | --- | --- | --- |
| S1-0 Bootstrap and scaffolding | Build, test tiers, docs scaffolds, git | **Complete and re-verified.** Gate G0 passes today. | §1.1 rows "Check scripts" and "git log"; status document S1-0 section |
| S1-1 Presentation and input feasibility (M0-E1, gate G1) | Canvas adapter decision, input pipeline | **Not started.** No `ui/canvas/`, no `ui/input/`, no `ViewTransform`; ADR-0011 matrix empty. | `find` output; ADR-0011 |
| S1-2 Core foundation | IDs, revisions, tiles, compositor, commands, history | **Not started.** Blocked on status issue I-01 (C++20 versus `std::expected`), which still has no user decision. | status "Issues raised" I-01; INTERFACE targets |
| S1-3 Brush stroke vertical slice (AT-002) | One real stroke to canvas, history, tiles | **Not started.** | no `core/brush` sources |
| S1-4 Native format save and reopen | `.archmage` writer/reader, save transaction, CLI validate/inspect | **Not started.** CLI has `--version` only. | `apps/archmage-cli/main.cpp` |
| S1-5 Recovery, durability, malformed input (AT-013, AT-014) | Journal, recovery, fault injection, fuzz | **Not started.** `fuzz/` has an empty aggregate target. | `fuzz/CMakeLists.txt` |
| S1-6 GPU slice and M0 probes | Vulkan comparison, blur, color experiment, PSD/RAW/text probes, benchmarks | **Not started.** `benchmarks/` has an empty aggregate target. | `benchmarks/CMakeLists.txt` |
| S1-7 Shell: documents, layers, navigation | Main window regions, tabs, layers panel, tools, shortcuts, theme | **Not started.** `MainWindow` sets a title and nothing else. | `ui/src/MainWindow.cpp` |
| S1-8 Shell: selections, eraser, image I/O, CLI render, recovery UI | Marquee tools, PNG/JPEG, export, home screen, first demonstration | **Not started.** | no `io/formats` sources |
| S1-9 Stage exit (M1 gate) | Acceptance automation, ledger children, sample document, tag `v0.1.0-alpha.1` | **Not started.** 0 ledger children, no `docs/samples/`, no tags. | ledger report; `git tag` |

Summary: one of ten Stage 1 sub-milestones is done. The M0 risk experiments have not run and M1 has no implementation. The Stage 1 plan's working assumption of roughly one session per sub-milestone leaves nine sessions of Stage 1 work before Stage 2 can begin. This plan is therefore written for a codebase that does not exist yet, against the contracts the Stage 1 plan and its ADRs define; §1.8 lists those contracts so S2-0 can revalidate them against whatever Stage 1 actually produced.

### 1.3 M0 deliverables and experiments (spec §28 M0)

| Item | State | Evidence or gap |
| --- | --- | --- |
| M0-D1 Clean project initialization | Done | commit `0cdf8ce`; ADR-0006 |
| M0-D2 Toolchain and license manifest | Done, pins current | `manifest_test.py --check-installed` passes 2026-10-13 |
| M0-D3 Feature ledger | Infrastructure only | 121 parents, 0 behaviors; benchmark audit (plan Q-08) has no owner |
| M0-D4 Architecture decisions | Recorded, mostly unevidenced | 30 ADRs; D-11 provisional; 17 ADRs "Evidence: None yet" |
| M0-D5 Real canvas/input prototype | Not started | S1-1 |
| M0-D6 Memory and format experiments | Not started | S1-2, S1-4, S1-6 |
| M0-D7 Test corpus plan | Done, register empty | `docs/testing/corpus-plan.md` |
| M0-E1 Docking, GPU canvas, overlays, mixed DPI, tablet | Not run | ADR-0011 matrix |
| M0-E2 Tile compositing, blur halo, profile conversion, brush latency | Not run | S1-2, S1-3, S1-6 |
| M0-E3 Native save, reopen, kill-during-save | Not run | S1-4, S1-5 |
| M0-E4 Layered PSD decode probe | Not run | S1-6.5 |
| M0-E5 RAW, shaped Arabic/Indic text, macOS GPU path | Not run | S1-6.6, S1-6.7, S1-6.9 |
| M0-E6 Resource estimates on profiles A to D | Not run | S1-6.8 |

The M0 exit clause (spec §28 M0: "the chosen architecture can execute these slices, limitations are recorded, and high-risk decisions have evidence") is not met. Spec §30 places the canvas-embedding and storage-choice risks at "latest sensible resolution: M0"; they are unresolved.

### 1.4 M1 items (spec §28 M1, Stage 1 plan §1.3)

None of the thirteen M1 items (build/packaging skeleton beyond the build itself, usable shell, document tabs, tile engine, command registry, basic brush/eraser, layers, simple selections, source-over composition, common image I/O, native save/reopen, undo/redo, autosave recovery, headless validation) has an implementation. The build skeleton exists; `archmage-cli` offers no `validate`, `inspect`, `render` or `recover`. None of AT-001, AT-002, AT-013, AT-014 has a test. The first reviewable demonstration (spec §29.1) cannot run.

### 1.5 Stage 1 decisions and issues that shape Stage 2

| ID | State | Effect on Stage 2 |
| --- | --- | --- |
| I-01 (ADR-0002, ADR-0023) | Open: D-23 requires `std::expected`, a C++23 feature, while D-02 fixes C++20 | Blocks S1-2 and therefore everything after it. Whatever Stage 1 chooses (C++23, or a project `Expected<T, Error>` type) is inherited by every Stage 2 API; this plan uses the name `Expected` generically. |
| Q-02 (ADR-0025) | Open: distribution and license model | Still open at the Stage 2 ledger and manifest refresh (S2-12). Stage 2 adds two dependencies (fontconfig, ICU; D-39) under the same interim policy: permissive or LGPL, dynamically linked. |
| Q-05 (ADR-0022) | Open: hosting and CI | `scripts/check*.sh` remain the gate; the workflow stays inert. |
| Q-07 (ADR-0013) | Open, default applied: schema major 0 through M2, freeze at the M2 exit | S2-12 performs the freeze. Needs an explicit answer before S2-12 (Q2-07). |
| Q-08 (ADR-0030) | Open: who performs the benchmark audit | Stage 2 ledger entries keep `public_documentation_only`; no parity claim is possible. |
| Q-03 | Default: no tablet | Irrelevant to M2 exit criteria; affects mask painting evaluation only. |
| I-06 | Closed by the reboot (kernel 7.2.9, driver match confirmed 2026-10-13) | S1-1 can proceed. |
| ADR-0009 | "rejects `linear` with a clear error until M2" | Binding: S2-1 implements the `linear` blend policy. |
| ADR-0013 | "Schema major 1 is frozen at the M2 exit" | Binding: S2-12. |
| ADR-0015 | "History is linear in Stage 1 ... branching is M2" | Binding: S2-11 implements branching. |
| ADR-0028 | "Light and high-contrast themes come in M2 or later" | Not required by spec §28 M2; deferred to M3 or later (§2.6). |
| ADR-0029 | "unit switching (NAV-03, M2) needs no format change" | Binding: S2-4 implements unit switching with rulers. |
| Stage 1 plan A-13 | "History branching and snapshots have no milestone; plan assumption M2" | Adopted as D-42 with an open question (Q2-06). |

### 1.6 Entry condition: Stage 1 work that must close before Stage 2

Stage 2 begins only when all of the following are recorded in `docs/implementation-status.md` with commands and output paths:

1. S1-1 to S1-9 complete, each gate (G1 to G8) met, merged to `main`.
2. The M1 gate: AT-001, AT-002, AT-013, AT-014 and the navigation and input checks pass under `dev-debug` and `asan-ubsan`; the first reviewable demonstration runs offscreen and manually.
3. `docs/testing/acceptance.md` exists (S1-9.2); ledger child entries for shipped Stage 1 behaviors exist (S1-9.3); `docs/samples/stage1-demo.archmage` validates (S1-9.4); tag `v0.1.0-alpha.1` exists (S1-9.8).
4. I-01 resolved by a superseding ADR or an `Expected` type (whichever Stage 1 chose), and ADR-0011 resolved with its evidence matrix filled.
5. M0 evidence complete or each gap explicitly recorded as unavailable with reproduction instructions (S1-6.10).

Nothing in this plan re-plans S1-1 to S1-9: the Stage 1 plan remains the instruction for that work. If a Stage 1 session deviates from a Stage 1 decision, the Stage 1 protocol (superseding ADR plus a pointer in the Stage 1 plan §2) applies, and S2-0 picks up the change through the checklist in §1.8.

### 1.7 Stage 1 deferrals that land inside Stage 2

From the Stage 1 plan §1.7 and the ADRs, these items were explicitly deferred to M2 and are therefore in scope here: groups, masks and channels, clipping, blend modes beyond Normal, adjustments, embedded and linked objects, styles, layer compositions, transform and crop fundamentals, basic editable text and shapes, linked resources (all spec §28 M2); the `linear` blend policy consumer (ADR-0009); canvas size and crop (GEO-03); history branching and named snapshots (ADR-0015, plan A-13); unit switching with rulers (ADR-0029); the schema major 1 freeze (ADR-0013); the 100 MP composite corpus fixture (`docs/testing/corpus-plan.md`, "M2 onward"). Items the Stage 1 plan listed as "M2 to M9" without a binding decision (saved workspaces, light and high-contrast themes, density presets, command palette, shortcut editor, full accessibility pass, localization) are deferred again in §2.6, with one exception noted there.

### 1.8 Stage 1 artifacts this plan depends on

S2-0.1 revalidates every row against the real tree. Where a name or shape differs, the session records the mapping in `docs/implementation-status.md` and adjusts task wording; it does not change a decision.

| Artifact (Stage 1 task) | Assumed shape | Used by |
| --- | --- | --- |
| `core/document`: `Document`, `Layer`, `RasterContent`, `Revision`, `DocumentState`, reserved fields for masks, effects, transform, boards, timeline, capability flags (S1-2.4) | Immutable snapshots with structural sharing; reserved fields serialized and validated but unconsumed | S2-0 onward: the reserved mask, effect and transform fields become real |
| `core/document/Id.h` (S1-2.1) | Typed 128-bit IDs; type set `doc, layer, raster, res, rev, tx, req, sess, job` | S2-0 extends the type set (D-32) |
| `core/document/Error.h` (S1-2.11) and the I-01 `Expected` type | Structured categories per spec §26.2 | every Stage 2 API |
| `core/commands`: `Command`, `CommandRegistry`, `MutationExecutor`, `History`, `Transaction` (S1-2.5, S1-2.6) | Envelope per spec App. A.2; linear history with clean save point; in-progress preview transactions | every Stage 2 command; S2-11 turns `History` into a tree |
| `core/tiles`: `TileMap`, `Tile`, `IRect`, `ChannelLayout`, `ResourceCoordinator` (S1-2.2, S1-2.3, S1-5.4) | Channel-planar 128² tiles, COW, uniform tiles, budgets | masks and channels (S2-2) add single-plane layouts; S2-11 uses the history budget |
| `core/render`: `GraphNode`, `GraphCompiler`, `CpuEvaluator`, `RenderRequest`, `SourceOver` kernel, cache keys (S1-2.7, S1-2.8), Gaussian blur node with halo (S1-6.3), `ViewTransform` (S1-1.5) | Pull evaluation per tile; required input region; cache key from parameter hash, source revision, time, quality | S2-0 compiles trees; S2-1 adds blend kernels; S2-2 feather reuses blur; S2-3 adds the transform node; S2-10 reuses blur for shadows and glows |
| `core/color`: `ColorPipeline`, `ColorPolicy {profile, blendPolicy}`, lcms2 wrapper, pinned sRGB (S1-2.9, S1-6.4) | Named stages; `blendPolicy` field with `encoded` implemented | S2-1 implements `linear` |
| `core/brush`: `StrokeInput`, `RoundDab`, `archmage.raster.paintStroke`, `StrokeTransaction` (S1-3) | Dabs onto a scratch map, flat stroke opacity, one history item per stroke | S2-2 adds a mask target and the `behind` mode |
| `core/document/Selection` and `archmage.selection.*` (S1-8.1, S1-8.2) | Sparse float32 coverage field; combine ops | S2-2 mask and channel conversion |
| `core/commands/JobScheduler` (S1-2.10) | Jobs with identity, source revision fingerprint, target IDs; stale-result reconciliation | S2-9 linked-source updates and AT-009 |
| `io/native`: schema 0.1, blob codec, `NativeWriter`, `NativeReader`, `SaveTransaction`, migrations with per-minor fixtures, unknown-payload preservation (S1-4) | Eight logical tables; `objects` and `relations` typed | every Stage 2 object type; S2-12 freezes 1.0 |
| `io/native/FaultPoints.h`, `ARCHMAGE_FAULT` (S1-5.1) | Named abort and synthetic-error points | S2-9 adds `export_between_tiles` for AT-009 |
| Recovery workspace and journal (S1-5.2, S1-5.3) | Committed revisions journaled every 2 s | all new object types must journal and recover |
| `io/formats`: `PngCodec`, `JpegCodec`, export at final quality (S1-8.6, S1-8.7) | Capability records; embedded ICC recorded; untagged assumed sRGB | S2-9 Place, linked sources; S2-11 composition export |
| `archmage-cli validate`, `inspect`, `render`, `recover` (S1-4.8, S1-5.9, S1-8.8) | Exit codes 0/1/2; JSON output | S2-12 extends `validate --deep` and `inspect` to new objects |
| `ui`: `MainWindow` regions, document tabs, `LayersModel` (`QAbstractListModel`) and `LayersPanel`, color panel, tool lifecycle framework, options row, shortcut profile, theme and icons (S1-7) | Virtualized list over the snapshot | S2-0 replaces the list model with a tree model |
| `ui/canvas/CanvasSurface` with the G1 adapter and raster fallback (S1-1) | Presents tiles at a `ViewTransform`; overlay callback | overlays for transform handles, guides, rulers, mask visualization |
| `ui/tools/BrushTool`, `MarqueeRectTool`, `MarqueeEllipseTool`, `ZoomTool`, `HandTool`, `EyedropperTool` (S1-3.5, S1-7.5, S1-8.3) | Spec §11 lifecycle | new tools follow the same framework |
| GPU bootstrap and compute source-over with the CPU comparison test (S1-6.1, S1-6.2) | Opt-in; uint8 exact, float32 within 1e-6 | S2-1 extends the shader to blend modes (D-43) |
| `benchmarks/bench_main`, `scripts/bench_record.py`, `benchmarks/results/` (S1-6.8, S1-9.6) | JSON records with the spec §25.1 fields | S2-12 benchmark refresh |
| Fuzz harnesses for the native reader and PNG (S1-5.8) | libFuzzer under the `fuzz` preset; corpus seeds | S2-12 extends the native-reader seeds; S2-7 and S2-8 add payload harnesses |
| `docs/testing/acceptance.md`, ledger children, `docs/samples/stage1-demo.archmage`, tag `v0.1.0-alpha.1` (S1-9) | Entry condition | S2-0 entry check |

## 2. Stage 2 scope

### 2.1 What "Stage 2" means

**D-31: Stage 2 = M2 (Layered editing and nondestructive structure) alone**, entered only after the M1 gate (§1.6) and exited at the M2 gate (spec §28 M2). Rationale: M2 is the largest single milestone in spec §28 by requirement rows that touch the document model (21 ledger parents plus carry-forwards); bundling M3 would push the first acceptance gate out by another ten or more sessions. M2's exit tests (AT-003, AT-008, AT-009) are the first that exercise hierarchy, nondestructive transforms and linked sources, which M3 to M9 all build on. See Q2-01.

Spec §28 M2 reads: "**Deliver:** Groups, masks/channels, clipping, blend modes, adjustments, embedded/linked objects, styles, layer compositions, transform/crop fundamentals, basic editable text/shapes, linked-resource handling. **Exit:** AT-003, AT-008, AT-009, and applicable native round trips pass. Document hierarchy and render graph have real consumers. Every visible operation has functioning undo and persistence."

### 2.2 M2 deliverables, decomposed

| M2 item | Stage 2 interpretation | Spec basis | Sub-milestone |
| --- | --- | --- | --- |
| Groups | Group objects with nested children, deterministic order, containment-cycle rejection; isolated and pass-through compositing; group opacity, visibility, locks, mask, transform; create, ungroup, move into and out of, collapse; virtualized tree panel that stays within the §25.2 large-tree budget | §6.1 parent/order, §6.2 no cycles, §13.1 LAY-02 "group, ungroup, collapse", §13.2 "Pass Through separately from isolated-group Normal", §25.2 | S2-0, S2-1 |
| Masks and channels | Raster layer masks and group masks (S2-2), vector masks (S2-7), per-effect masks (S2-10); MSK-02 enable, invert, density, feather, visualization, transform linkage; MSK-04 parametric luminosity masks and live combinations of raster, vector and parametric masks; MSK-05 paint, paste, fill and transform a mask with the core tools (filter a mask is M3); MSK-06 channel inspection, isolate and composite display, named alpha channels, duplicate (split/merge and spot channels are M4 with document modes); MSK-08 mask to selection and selection to mask without profile conversion. "Reusable named masks" (MSK-01) are named alpha channels loadable into any mask (D-34) | §12.2 | S2-2, S2-7, S2-10 |
| Clipping | MSK-03 clipping chains with an explicit base object and predictable ordering through groups; clipped adjustments; "blend clipped layers as group" as a base-object option | §12.2 MSK-03, §13.2 "clipping effects" | S2-2 |
| Blend modes | All 27 §13.2 modes plus Pass Through; paint mode `behind` alongside the Stage 1 `erase`; advanced blending: channel enablement, luminance and channel blend ranges with split handles, knockout (none, shallow, deep), opacity versus fill opacity; document blend policy `linear` implemented next to `encoded` with an import policy and compatibility notice; mode and precision restrictions inventoried (RGB uint8 and float32 in Stage 2; CMYK, Lab, grayscale and indexed are M4) | §13.2, §7.4, §16.1 "behind/erase" | S2-1 (knockout and fill opacity get their style consumers in S2-10) |
| Adjustments | The §6.1 Adjustment entity; adjustment layers (apply to the composite below them within their group or clip base) and per-object adjustment nodes (apply to one object's content before blending); the M2 catalog (D-37) with the §15.1 curve requirements (histogram overlay, channel selection, eyedroppers, point and numeric editing, interpolation choice, presets, predictable clipping); live preview, reset, bypass, presets, mask, blend and opacity; the histogram as a global-analysis node; a generated mode and precision capability table | §15.1, §15.3, §7.2, §8.1 | S2-5, S2-6 |
| Embedded and linked objects | Every §13.3 bullet except packaging: embed raster (PNG, JPEG), vector (an ArchMage shape or text subtree) and nested ArchMage content (RAW is M4); independent versus shared instances as distinct commands; open nested content in a tab and update dependents as one transaction; reversible transforms, adjustments and masks on the instance; replace content; convert linked to embedded and back; detect changed, moved, missing, inaccessible and cyclic sources; last-known-good render; relink one, relink folder, exact-match lookup; explicit update policy; export pins source fingerprints (AT-009). Place Embedded and Place Linked commands; cross-document layer copy with the §6.2 placement rules. Packaging dependencies for transfer is spec §28 M4 "preflight and packaging" | §13.3, §6.1 Embedded and Linked rows, §6.2, §9.3 | S2-9 |
| Styles | An effect stack per object with a canonical render order; the M2 effect subset (D-41): stroke, drop shadow, inner shadow, outer glow, inner glow, color overlay, gradient overlay, with contour on shadow and glow falloff; global lighting; per-effect blend mode, opacity, spread, choke, distance, size, angle, noise, scale; copy and paste, clear, presets with identifiers and versions, effect scaling, convert to editable layers, per-effect masks; render bounds include effects. Bevel/emboss, satin, pattern overlay, texture and multiple fills/strokes are proposed for M3 (Q2-04) | §13.4, §6.1 "Adjustment/effect" | S2-10 |
| Layer compositions | LAY-06: a composition stores per-object visibility, position (transform) and appearance (opacity, blend, fill, styles enabled, masks enabled); create, update, apply, delete, rename; compare by switching; batch export of selected compositions through the Stage 1 export path | §13.1 LAY-06, §22.4 "compositions" | S2-11 |
| Transform and crop fundamentals | GEO-01 move, scale, rotate, skew, distort, perspective, flip, reference point, numeric transform, transform again and duplicate; GEO-02 free transform on raster, vector, text, masks and groups with unified handles; GEO-03 nondestructive crop, straighten, canvas resize, trim, reveal all, crop presets (perspective crop is M3 with GEO-07); GEO-04 nearest, bilinear, bicubic family, Lanczos (edge and detail-preserving options are M3 and M6); resolution change distinct from resampling | §14.1, §6.2 off-canvas content survives, §6.3 | S2-3, S2-4 |
| Basic editable text and shapes | TXT-01 point text and paragraph frames (linked frames, text on paths, text in shapes and warp are M5); TXT-02 subset: font family and style, size, color, tracking, leading, baseline shift, alignment; kerning from the font through HarfBuzz; VEC-02 live rectangles, rounded rectangles, ellipses, polygons, stars, lines with arrowheads, parameter editing (custom shapes are M5); VEC-04 subset: solid and gradient fills, one stroke with width, alignment, caps, joins and dashes (pattern fills, multiple strokes, variable widths and brush strokes are M5) | §17.1, §17.2 | S2-7, S2-8 |
| Linked-resource handling | Fingerprints, update policies, relinking and last-known-good copies for linked sources; font references as pinned resources with a substitution report; missing-resource placeholders with stored fallback and diagnostic | §13.3, §6.1 Resource, §6.2, §17.1 | S2-8, S2-9 |
| Carry-forwards (§1.7) | `linear` blend policy (S2-1); LAY-04 align and distribute, LAY-05 merge, stamp, flatten, rasterize, trim, LAY-08 background layer, isolation and content replacement (frames and placeholders are M5), NAV-03 rulers, unit switching, guides, grid and snapping subset (S2-4); history branching and named snapshots (S2-11); schema major 1 freeze and the 100 MP corpus fixture (S2-12) | ADR-0009, ADR-0013, ADR-0015, ADR-0029, Stage 1 plan A-13, ledger `inferred:` rows | as listed |

### 2.3 Components in scope (spec §5.5)

Created in Stage 2: `core/vector/` (S2-7) and `core/text/` (S2-8), each with `archmage_add_module()` targets, `include/archmage/<module>/` headers and `src/`.

Extended: `core/document` (tree, masks, channels, transforms, adjustments, sources, effects, compositions, snapshots), `core/commands` (tree history, new commands), `core/tiles` (single-plane layouts for masks and channels, history budget consumers), `core/render` (blend kernels, transform, mask, clip, adjustment, style, vector and text nodes, histogram reduction, cache keys over mask versions and source revisions), `core/color` (linear blend policy transforms), `core/brush` (mask target, `behind` mode), `io/native` (schema 0.2 to 0.n, then 1.0), `io/formats` (Place paths only), `ui/` (tree panel, properties, adjustments, channels, history, compositions, transform, crop, shape and text tools, rulers, guides, mask mode), `apps/archmage-cli` (`inspect`, `validate --deep` and `render --composition` over the new objects), `tests/`, `benchmarks/`, `fuzz/`.

Still reserved, not populated: `core/timeline/`, `io/psd/`, `services/`, `sdk/`.

### 2.4 Architecture in scope

- Boundaries unchanged from Stage 1 (spec §5.4): UI and CLI dispatch validated commands; the executor commits revisions; the graph compiler consumes revisions; the CPU evaluator is authoritative (ADR-0012); nothing mutates engine structures directly (spec §9.1).
- Document entities added from spec §6.1: Group (a Layer/object with children), Mask, Embedded object, Linked object, Adjustment/effect, plus Composition and Snapshot as document-level records. Boards/pages, animation tracks and timeline fields stay reserved (M5, M7). Content identity remains separate from placement: an instance references a source; duplicating an instance and duplicating a source are different commands (spec §6.1).
- Invariants enforced in Stage 2 (spec §6.2): no containment cycles, no effect dependency cycles, no recursive linked-document loops; every reference resolves or is an explicit missing-resource placeholder with a stored fallback and diagnostic; deterministic order and rendering order through groups; commands validate locks, targets and mode compatibility (RGB only until M4); panels observe immutable revisions; the active editing target (content or mask) is view state, not document state; off-canvas content survives crop and canvas changes; cross-document paste defines profile handling (notice, no conversion until M4), scale, resolution, resource copying and naming; unknown payloads survive open and save.
- Coordinates (spec §6.3, ADR-0017): instance transforms are 3 × 3 `double` matrices in document space with an affine fast path; transforms use local origins so precision is kept far from zero; half-open rects and the +0.5 pixel-center convention govern rasterization, hit testing and snapping.
- Processing graph (spec §7.2): every new node declares type and version, inputs, bounds, required input region, halo, cache key inputs and classification (local, bounded-neighborhood, global reduction). Cache keys gain mask versions, embedded-source revisions, blend policy and style parameters. Visibility toggles and linked-source updates invalidate only descendants (tested).
- Per-object render pipeline, canonical order (D-32): content → per-object adjustments → transform → vector mask → raster mask → effects derived from the resulting coverage → fill opacity on content → style composite → blend ranges and channel enablement → blend mode with opacity against the backdrop → knockout.
- Alpha and blending (spec §7.4): every blend mode has its own color and alpha equation and reference fixture (D-33). The `linear` policy converts the working representation to linear light before compositing and back after; it is a document setting, never applied implicitly.
- Commands, history and jobs (spec §9): history becomes a revision tree with retained branches and named snapshots (D-42); linked-source updates and nested-document commits are jobs reconciled per spec §9.3.
- Native format (spec §10): every new entity is an `objects` row with typed `relations` (child, mask, effect, adjustment, source); schema minors 0.2 to 0.n during Stage 2, major 1 frozen at S2-12 (D-44).
- Tool lifecycle (spec §11) for every new tool: transform, crop, shape family, text, guide drag.

### 2.5 Acceptance criteria for Stage 2

From spec §27.2 and §28 M2:

| Test | Pass condition (spec) | Stage 2 automation |
| --- | --- | --- |
| AT-003 | Nested group/clipping/style composite: fixture result matches its specified blend semantics | Headless `[image]` test over a fixture document (two nested groups, one pass-through and one isolated, a clipped adjustment, a clipped raster, a group mask, a drop shadow and a stroke, three blend modes) compared with a reference computed by the independent scalar reference implementation of D-33 (checked in under `tests/reference/`); uint8 exact under the declared rounding rule, float32 within 1e-6 |
| AT-008 | Repeated object transforms: final render samples original content; cancel restores exact prior state | `[operation]` and `[image]` tests: ten successive transforms of one raster instance render identically to a single composed transform; Escape during the eleventh leaves the revision byte-identical; an identity transform renders byte-exact to the source |
| AT-009 | Linked source changes during export: export uses one pinned source snapshot, no mixed-revision tiles | `[resilience]` test: `archmage-cli render` paused at the `export_between_tiles` fault point while the linked PNG is rewritten; the output equals the pre-change render exactly; the document then reports the link as changed |
| Native round trips | "applicable native round trips pass" | For every object type introduced (group, raster mask, vector mask, channel, transform, adjustment layer, per-object adjustment, shape, text, embedded source, linked source, style, composition, snapshot): create, save, reopen, compare structure and render byte-exact; plus AT-001 re-run on the Stage 2 sample documents |
| Stage 1 regression | AT-001, AT-002, AT-013, AT-014 and the navigation and input checks keep passing | Re-run at every sub-milestone exit |

"Document hierarchy and render graph have real consumers" is defined as: the tree panel, the compiler, the exporter, the CLI `render` and `inspect` commands, the compositions panel and the embedded-object editor all operate on the same tree and graph types, with no parallel flattened representation. "Every visible operation has functioning undo and persistence" is the per-task DoD rule in §4.4: a menu item, panel control or tool that exists in a build must undo, redo, journal, save, reopen and validate, or be hidden.

Navigation and input checks extended in Stage 2: rulers show document units at every zoom (spec §11 "preview zoom never changes their meaning"); guides and grid snapping with visible feedback; transform handles align with the cursor at fractional scale; Escape and Enter follow the §11 lifecycle for every new tool; single-letter shortcuts stay suppressed while the text tool edits (spec §17.1).

Evidence per spec §29.3: `docs/implementation-status.md` records commands, outputs and artifact paths; the progress report separates implemented, automatically tested, manually tested and verified against benchmark; at least two sample documents (`docs/samples/stage2-composite.archmage` for AT-003, `docs/samples/stage2-linked.archmage` with its linked asset for AT-009) are committed.

### 2.6 Explicitly deferred

| Deferred item | Spec | Target | Note |
| --- | --- | --- | --- |
| Lasso, wand, brush and range selections, selection refinement, named selections, selection transform, stroke selection | SEL-02 to SEL-05, SEL-07 to SEL-09, rest of SEL-10 | M3 | spec §28 M3 |
| Channel calculations and apply image; channel split and merge; spot channels | MSK-07; MSK-06 remainder | M3; M4 | split and merge need grayscale documents (M4) |
| Filter a mask (MSK-05 "filter"), retouch tools, filter families, contextual action bar | §14.2, §15.2 | M3 | |
| Warp, puppet, liquify, perspective planes and perspective crop, content-aware scaling | GEO-05 to GEO-08, GEO-03 "perspective crop" | M3 | |
| Edge and detail-preserving resampling | GEO-04 remainder | M3 (conventional), M6 (model-assisted) | |
| Adjustments outside the D-37 catalog: shadows/highlights, tonal range compression, texture, clarity, local contrast, dehaze, grain, vignette, split toning and color wheels, lens corrections, color matching, palette remapping, Lab operations | §15.1 | M3 (multiscale and local-contrast), M4 (Lab, lens, matching, palette) | Q2-03 |
| Style effects: bevel/emboss, contour outside shadows and glows, satin, pattern overlay, texture, multiple fills and strokes, "mask hides effects" option | §13.4 | M3 | Q2-04 |
| Auto-align and auto-blend | LAY-07 | M4 | spec §28 M4 |
| Frames and placeholders, image fitting | LAY-08 remainder, §17.3 | M5 | |
| Pen and node editing, Booleans, path effects, symbols, tracing, vector snapping, custom shapes, pattern fills, multiple strokes, variable widths, brush strokes | VEC-01, VEC-03, VEC-05 to VEC-08, VEC-02 and VEC-04 remainders | M5 | |
| Linked text frames, text on paths, text in shapes, warp; OpenType features, variable fonts, glyph browser; RTL and bidirectional layout, complex-script line breaking, vertical text, IME composition beyond commit strings; lists, hyphenation, spell checking; text effects and conversion beyond rasterize; color fonts; font identification | TXT-01 remainder, TXT-03 to TXT-08 | M5 | D-39 records the M2 limits |
| Packaging linked dependencies for transfer; preflight | §13.3 last bullets, §17.3, §24.1 | M4 | spec §28 M4 |
| ICC assign and convert, display-profile monitoring, OCIO, CMYK, Lab, grayscale, indexed, uint16 and float16 end to end, PSD, TIFF, EXR, isolated decoders | §8, §22 | M4 | |
| Persisted history, history search | §9.2 | M3 or later | D-42 |
| Command palette, shortcut editor, contextual help | NAV-07 | M3 | the ledger's `inferred:` M2 assignment moves to M3 in S2-0.9 (Q2-10) |
| Light and high-contrast themes, density presets, saved workspaces, localization, full accessibility pass, Windows and macOS builds, installers | §2.3, §2.4, §26.3, §26.4, §31 | M3 to M9 | ADR-0028 "M2 or later" is satisfied by "later" |
| GPU evaluation of mask, transform, adjustment, style, vector and text nodes | §7.5 | after M3 | D-43 limits Stage 2 GPU work to blend kernels |
| Brush dynamics, assistants, natural media, brush editor | §16 | M5 | |
| Everything in §19, §20, §21, §23, §24 | | M6 to M8 | |

## 3. Upfront decisions

Each decision becomes an ADR in `docs/decisions/` during S2-0 (`ADR-00nn-<slug>.md` numbered to match `D-nn`; sections: context, decision, alternatives, consequences, evidence). Decisions marked **(gate)** are provisional until the named condition is met. Stage 1 decisions D-01 to D-30 stay in force; where a Stage 2 decision consumes one, it says so.

### 3.1 Scope, process and versions

- **D-31 Stage 2 scope and entry.** Stage 2 is M2 (§2.1). Entry condition §1.6. Branches are `s2-<n>-<slug>`; the project version becomes `0.2.0` in S2-0 and `v0.2.0-alpha.1` is tagged at the M2 gate (S2-12). `CLAUDE.md` is updated in S2-0 to point at this plan and the `S2-0` to `S2-12` session protocol. The per-task DoD (§4.4) adds persistence, undo and ledger requirements to the Stage 1 DoD.
- **D-44 Schema major 1 freeze, conditional (gate: Q2-07).** Every sub-milestone that changes persisted structure bumps `schema_minor` (0.2 to 0.n) with a migration step and a per-minor fixture through the S1-4.7 framework. S2-12 freezes major 1 only after: indices benchmarked on the 2,000-object and 100 MP fixtures (open, save, per-object lookup); a blob size limit chosen from measurements (default 16 MiB, larger resources chunked); compaction measured (`VACUUM INTO` for saved files, incremental vacuum for the recovery workspace); migration 0.n to 1.0 with fixtures; `validate --deep` covering every relation type. ADR-0044 records the frozen tables and the compatibility rule: 1.x minors stay readable by older 1.x readers through unknown-payload preservation; a newer major opens read-only (ADR-0013). If Q2-07 defers the freeze, S2-12 performs the benchmarks and records the results without changing the major.

### 3.2 Hierarchy, compositing and masks

- **D-32 Hierarchy and group compositing.** Identifier types gain `group`, `mask`, `chan`, `adj`, `fx`, `shape`, `text`, `src`, `comp`, `snap` (extends ADR-0016). Typed relations in the existing `relations` table: `child` (ordered), `mask` (role `raster` or `vector`), `effect` (ordered), `adjust` (ordered per-object adjustments), `source` (instance to source). Rendering order is depth-first, bottom to top, by explicit order index. An **isolated** group composites its children onto a transparent canvas and blends the result with its own mode, opacity and mask onto the backdrop; a **pass-through** group composites children directly onto the backdrop, and group opacity `o` and mask `m` apply as `result = backdrop + o·m·(withChildren − backdrop)` in premultiplied form, so a pass-through group with opacity or a mask is not forced isolated. Any blend mode other than Pass Through isolates the group. **Clipping:** an object with `clipped = true` takes as base the nearest unclipped object below it in the same parent; clipped objects composite onto the base's content with their own mode and opacity, confined to the base's coverage; the base's option `blendClippedAsGroup` (default true) applies the base's mode and opacity to the combined result, otherwise the base blends first and each clipped object blends individually against the backdrop within the base's coverage. Clipped adjustment layers affect only the base chain. Group transforms compose with children at render time (never baked); group masks live in group-local space. The canonical per-object pipeline is the one in §2.4. The tree panel is a `QAbstractItemModel` over the snapshot with lazy thumbnails and must meet the spec §25.2 large-tree budget (p95 ≤ 100 ms at 2,000 objects). Alternatives rejected: forcing isolation whenever a group has opacity (changes documented product behavior for pass-through groups); flattening groups at commit time (destroys editability).
- **D-33 Blend modes and blend policy.** Separable modes use the general premultiplied formula `co = cs·(1 − αb) + cb·(1 − αs) + αs·αb·B(Cb, Cs)`, `αo = αs + αb·(1 − αs)`, with `B` on unpremultiplied colors; non-separable Hue, Saturation, Color and Luminosity use the SetLum, ClipColor and SetSat procedures of the W3C Compositing and Blending Level 1 recommendation. Modes absent from that recommendation use these functions, recorded in ADR-0033: Linear Burn `Cb + Cs − 1`; Linear Dodge (Add) `Cb + Cs`; Vivid Light `Cs ≤ 0.5 ? ColorBurn(Cb, 2Cs) : ColorDodge(Cb, 2Cs − 1)`; Linear Light `Cb + 2Cs − 1`; Pin Light `Cs < 0.5 ? min(Cb, 2Cs) : max(Cb, 2Cs − 1)`; Hard Mix `VividLight < 0.5 ? 0 : 1`; Darker Color and Lighter Color pick the whole color with lower or higher luma `0.3R + 0.59G + 0.11B`; Subtract `Cb − Cs`; Divide `Cs == 0 ? 1 : Cb / Cs`; Dissolve selects the source per pixel with probability `αs · opacity` using a hash of (object seed, document x, y), so the pattern is identical across zoom, export and reopen (spec §9.2, §15.3). Results clamp to [0, 1] for uint8 encoding only; float32 keeps out-of-range values. Fidelity: no licensed reference application is available, so every mode's ledger fidelity is `approximate` and "verified against benchmark" stays false (spec §27.3); the equations and fixtures are the contract. **Blend policy:** `encoded` composites stored encodings; `linear` converts the working RGB to linear light through the document profile's tone curve (an lcms2 transform built once per profile; the exact piecewise sRGB function for the pinned sRGB profile), composites, and converts back for storage and export. The policy is a manifest field, changed only by `archmage.document.setBlendPolicy`, which shows a notice that appearance will change; imports default to `encoded`. **Advanced blending:** fill opacity scales the content before styles, opacity scales the styled result; blend ranges (this layer and underlying, gray plus R, G, B, with split handles giving linear ramps between the split points) multiply coverage per pixel; channel enablement makes a disabled channel take the backdrop value; knockout `none`, `shallow` (to the enclosing group's backdrop) or `deep` (to the document background) punches through by `1 − fillOpacity`. Paint mode `behind` (destination-over) joins `normal` and `erase` in `archmage.raster.paintStroke`. Fixtures come from an independent scalar reference implementation, `tests/reference/blend_reference.py` (pure Python, run with `python3 -I`), generating goldens over a 17 × 17 grid of source and backdrop values for alpha pairs {0, 0.25, 0.5, 1}; goldens are checked in with the generator and their hash.
- **D-34 Masks and channels.** A mask is an object with single-plane `coverage` raster content in the document's sample type (uint8 masks for uint8 documents, float32 for float32), properties `enabled`, `inverted`, `density` in [0, 1], `feather` (document pixels; rendered with the S1-6.3 Gaussian node, halo 3σ), `linkedToTransform`, coordinate space object-local. Processing order per mask: invert, then `coverage' = 1 − density·(1 − coverage)`, then feather. An object may hold one raster mask, one vector mask (S2-7) and any number of parametric masks (S2-2: luminosity range with feather, evaluated on the node's input: the composite below for adjustment layers, the object's own content otherwise); each mask carries a combination operation `multiply` (default), `add` or `subtract` applied in order. New masks default to "reveal all" (a single uniform tile); alternatives are "hide all" and "from selection". **Active target** (`content`, `mask`, `vectorMask`) is view state; paint, fill, paste and transform commands take an explicit `target` parameter; the panel shows both thumbnails with the active one outlined and the status bar names the target (spec §12.2). **Channels:** R, G, B and A are computed views; named alpha channels are document-level single-plane rasters (`chan` objects) persisted and listed in a Channels panel with isolate (grayscale) and overlay (tinted) display as view state; commands load a channel as a selection, save a selection as a channel, duplicate, delete, rename, and load a channel into a mask (this is how MSK-01 "reusable named masks" is met). Mask and selection conversions copy coverage with sample-type scaling only; no profile is involved (MSK-08). Mask painting uses the Stage 1 stroke with the foreground color's luma as coverage.

### 3.3 Geometry

- **D-35 Nondestructive transforms and resampling.** Each instance stores a 3 × 3 `double` matrix `T` in document space (the reserved transform field of S1-2.4 becomes real); effective transform = parent transforms composed with `T`; a linked mask shares it, an unlinked mask has its own. The transform node maps each output tile through `T⁻¹`; required input region = inverse-mapped output rect dilated by the kernel radius (nearest 0.5, bilinear 1, bicubic 2, Lanczos 3) plus one pixel; affine fast path, general homography path with local origins (spec §6.3). Identity and integer translations copy tiles byte-exact and never resample. Minification samples a prefiltered mip level chosen from the local Jacobian so downscaling does not alias. Interpolation runs in premultiplied float32 (spec §7.4). Kernels: nearest, bilinear, bicubic as the Mitchell–Netravali family with named presets Catmull-Rom (B 0, C 0.5), Mitchell (1/3, 1/3) and B-spline (1, 0), and Lanczos3; "sharper" variants that need sharpening passes are M3. During a drag the preview uses bilinear at the view's mip level, labelled as a preview (spec §7.3), and converges to the chosen kernel on release. `archmage.object.rasterizeTransform` bakes `T` destructively and undoably. The transform tool follows spec §11: handles for scale, rotate, skew, distort and perspective modes (mode buttons in the options row plus documented modifiers), reference point, numeric fields (x, y, width %, height %, angle, horizontal and vertical skew), Shift constrains, Alt scales about the reference point, Enter commits one history item, Escape restores the pre-interaction revision exactly, Transform Again and Transform Again Duplicate replay the last matrix. Validation rejects singular matrices and scales below 1e-6; negative scales (flips) are valid. Vector and text instances keep `T` as geometry, so repeated transforms never degrade them.
- **D-36 Crop, canvas, rulers, guides, snapping, align, merge.** The document stores `canvasRect` (int64 half-open rect in document space). Crop and Canvas Size are commands changing `canvasRect` (with an anchor for Canvas Size); content outside the canvas survives (spec §6.2); "Delete cropped pixels" is a separate explicit option that rasterizes. Reveal All sets `canvasRect` to the union of content bounds; Trim sets it to the bounds of the non-transparent composite or of one object; crop presets are ratio and size lists. Straighten is one transaction: rotate every top-level object about the canvas center by the measured angle (nondestructive, through `T`) and set `canvasRect` to the largest axis-aligned rectangle inside the rotated canvas, or to its full bounds by option. Image Size with resampling off changes only the stored resolution (ADR-0029); with resampling on it scales every top-level object and `canvasRect` as one transaction, with an option to rasterize. Rulers show document units (px, in, mm, cm, pt, pc) from the stored resolution with a draggable origin; the unit preference is the document field ADR-0029 reserved. Guides (`{axis, position, locked}`) and the grid (`{spacing, subdivisions, unit, visible}`) are document fields changed by commands. Snapping targets are guides, grid, canvas edges and center, and transformed object bounds, each toggleable and with a global View toggle; tolerance is 8 logical pixels; a snap indicator is drawn on the canvas overlay. Align and Distribute (LAY-04) emit transform commands in one transaction against selection bounds, the canvas or a key object. Merge Down, Merge Visible, Stamp Visible and Flatten render through the authoritative graph at final quality (never the viewport, spec §7.3) into a raster object and remove the merged sources in one history item; Rasterize converts text, shapes, embedded instances or transforms to raster at the document sample type; Trim Transparent crops to content. A background layer is a bottom raster with `background = true`: opaque at render, position-locked, no mask or blend mode, with Layer From Background and Background From Layer commands. Isolation (show only selected objects) is view state.

### 3.4 Nondestructive operations

- **D-37 Adjustments.** An adjustment is an object with `{typeId, implementationVersion, parameters (versioned payload), bypassed, masks, blendMode, opacity, clipped}`. Two placements: an **adjustment layer** is a child of a group whose input is the composite of its siblings below (or its clip base); a **per-object adjustment** is attached through the `adjust` relation and takes that object's content as input. Every adjustment follows the spec §15.3 contract: live preview in an in-progress transaction, reset, bypass, presets as resources with id and version, mask, blend and opacity, deterministic borders (point operations have no halo). Each registry entry declares the space it works in (`encoded` or `linear`, so Exposure works in linear light while Levels and Curves work on the document encoding), supported modes (RGB only in Stage 2) and sample types (uint8, float32). `archmage-cli capabilities --markdown` generates `docs/architecture/capabilities.md` from the registry (spec §8.1) and a test fails when the committed file is stale. **M2 catalog** (Q2-03): S2-5 delivers Invert, Threshold, Posterize, Brightness/Contrast (documented linear-offset and contrast-about-mid-gray model, fidelity `approximate`), Exposure (stops, offset, gamma in linear light), Gamma, Levels (input black, white and gamma, output black and white, per channel and composite), Curves (per channel and composite, point and freehand editing, monotone cubic or linear interpolation, numeric points, histogram overlay, black, gray and white eyedroppers, presets, predictable clipping) and Hue/Saturation (master plus six ranges with falloff, colorize); S2-6 delivers Color Balance (shadows, midtones, highlights, preserve luminosity), Vibrance and Saturation, Black & White (six weights plus tint), Channel Mixer (3 × 3 plus constants, monochrome), Gradient Map (gradient model from D-38, dither), Color Lookup (`.cube` 3D LUTs through a bounded parser with a fuzz target), White Balance (temperature and tint as a documented linear-light multiplier model, `approximate`) and Selective Color (`approximate`). The histogram is a global-reduction node computed at the view mip level during drags (labelled) and at full resolution as a job. Everything else in spec §15.1 is deferred per §2.6.
- **D-38 Vector geometry and rasterizer.** `core/vector/` holds `Path` (subpaths of cubic segments, closed flag, fill rule nonzero or even-odd), `Shape` (live parameters: rectangle with four corner radii, ellipse, polygon with star inner ratio, line with optional arrowheads) that generates a `Path`, `Paint` (solid color; linear or radial gradient with color and opacity stops, midpoints, interpolation space `encoded` or `linear`, repeat `none`, `repeat` or `reflect`, dither flag, per spec §15.1) and `Stroke` (width, alignment center, inside or outside, caps butt, round or square, joins miter, round or bevel with miter limit, dash array and offset). The rasterizer is project code: adaptive flattening to 0.1 px at the render resolution, then a sparse scanline rasterizer with 16 sub-scanlines and exact horizontal span coverage for both fill rules, at the D-17 pixel-center convention. Stroking flattens, offsets with joins and caps, applies dashes by arc length and fills nonzero; inside and outside alignment clip against the fill path. Acceptance: integer-aligned rectangles rasterize to exact 0 and 1; half-pixel offsets give exact 0.5 edges; a circle's coverage sum is within 0.1 % of its area; nonzero and even-odd differ only where specified on a self-intersecting star; tiny segments, reversed winding, near-coincident edges and transformed compound paths neither crash nor produce empty output (spec §17.2). The vector node is a local operation whose bounds are the path bounds plus stroke extent; it rasterizes in document space after the instance transform, so vector content is resolution independent. Vector masks reuse the rasterizer. No third-party 2D rasterizer enters `core/` (Qt is excluded by ADR-0007; Skia, Blend2D and Cairo would add large manifest entries for a small M2 scope).
- **D-39 Text stack.** `core/text/` holds `TextContent` (paragraphs of style runs over UTF-8 text; run style: font family and style name, size in points mapped through the document resolution, color, tracking, leading auto or value, baseline shift; paragraph style: alignment left, center, right or justify; frame: point or box with width, height and an overflow flag), `FontCatalog` over fontconfig (family and style enumeration, file path and index), pinned font references `{family, style, PostScript name, file SHA-256, version string}` stored as resources, a HarfBuzz shaper per run with script itemization, and a layout that breaks lines at Unicode whitespace. Glyphs are loaded as outlines through FreeType without hinting and rasterized by the D-38 rasterizer after the instance transform, so text transforms stay exact. Caret movement and selection use ICU grapheme-cluster iteration. **M2 limits, recorded in the ledger:** no bidirectional reordering (a paragraph containing strong right-to-left characters shows a "bidirectional layout is not supported in this version" notice and stays editable; TXT-04, M5), no language-aware line breaking or hyphenation (TXT-05), no OpenType feature controls or variable axes (TXT-03), no color or bitmap glyph formats (unsupported glyphs render nothing and are reported; TXT-07), Qt input-method commit strings are accepted and pre-edit text is shown read-only (composition rendering is M5). Missing fonts produce a substitution report (object, requested font, substitute) and the text object keeps a bounded cached appearance resource rendered at the last save, shown until the text is edited (spec §17.1); fonts are never embedded. The text tool follows text-editing conventions before canvas conventions (spec §17.1): Enter inserts a newline, Escape and Ctrl+Enter commit, single-letter shortcuts are suppressed, typing is coalesced into history entries per burst (spec §9.2). Dependencies added to the manifest and `Dependencies.cmake`: fontconfig and ICU (both installed; licenses recorded under the ADR-0025 interim policy); HarfBuzz and FreeType move from found to linked.
- **D-40 Embedded and linked sources.** A `Source` object (`src`) has `kind` (`image` or `document`), `content` (the resource hash of the original bytes: PNG or JPEG, or a nested `.archmage` container), and `sourceRevision`; decoded rasters and nested composites are disposable caches. Instances reference sources through the `source` relation and carry their own transform, adjustments, masks and effects. `archmage.source.duplicateInstance` shares the source; `archmage.source.duplicateSource` copies it (spec §6.1). A **linked** source adds `link {uri (stored absolute and relative to the document), policy manual, promptOnOpen or autoOnOpen (default promptOnOpen), fingerprint {sha256, size, mtime}, expectedVersion, lastKnownGood resource}`; the last-known-good render is always kept, bounded by a size cap. Status `ok`, `changed`, `moved`, `missing`, `inaccessible` or `cyclic` is computed on open and on demand and shown in the panel; a nested document records its identity chain, and a link whose chain contains the current document is `cyclic` and renders its last-known-good with a badge. Place Embedded and Place Linked (menu and drag-and-drop) create a source and an instance centered on the canvas at native size, or scaled to fit when larger. Edit Contents opens a nested document in a tab; Save there commits a new source revision in the parent as one transaction through the job model (if the source was deleted meanwhile the result is a candidate, spec §9.3). Replace Contents keeps placement and effects; Convert to Linked writes the bytes to a chosen path; Convert to Embedded copies them in. Relink One uses a file dialog; Relink Folder searches one chosen folder, non-recursively, by exact filename and then by fingerprint among same-size files; Exact Match looks up by fingerprint only (spec §13.3). Updates happen only through `archmage.source.update` (manual, or on open under the policy); export never triggers an update, and render requests reference the revision's resources, so an update during export cannot mix tiles (AT-009). Cross-document copy and paste copies resources by hash, keeps names with a " copy" suffix on collision, places objects at the same document coordinates (clamped into the canvas when fully outside), ignores resolution differences (one pixel is one pixel), and on a profile mismatch keeps samples unchanged and shows a compatibility notice until M4 conversion exists.
- **D-41 Layer styles.** An effect stack per object (including groups, shapes and text) with the fixed canonical order, top to bottom: Stroke, Inner Shadow, Inner Glow, Color Overlay, Gradient Overlay, Outer Glow, Drop Shadow (Satin, Pattern Overlay, Texture, Bevel/Emboss and duplicate effects such as multiple strokes are M3, Q2-04). Reordering is not permitted because the appearance model fixes the order (spec §13.4 "where the appearance model permits it"). Each effect has `enabled`, `blendMode`, `opacity`, color or gradient, `useGlobalLight`, `angle`, `distance`, `spread` or `choke`, `size`, `noise` (seeded per-document-pixel hash), `contour` (a 1-D curve on the falloff for shadows and glows, with linear, gaussian and ring presets), `antialiased`, and for Stroke `position` (outside, inside, center) and fill type (solid or gradient); the document stores global light angle and altitude. Effects derive from the object's coverage after transform and masks (§2.4 pipeline); the "mask hides effects" option is M3. Rendering: drop shadow blurs the coverage offset along the angle by `distance`, spread adjusts the ramp, `size` is the blur radius; glows are unoffset; inner variants use the inverted coverage clipped to the object; Stroke is exact from the path for vector and text objects and uses a distance field from coverage for raster objects, both with the spec §7.2 halo. Render bounds expand by the largest `distance + size`. Knockout and fill opacity (D-33) are consumed here. Copy Style, Paste Style, Clear Style, Scale Effects (percentage), presets as resources `{id, version, effects}` (no bundled presets), per-effect masks (MSK-01 "multiple effect masks"), and Create Layers (each effect to its own raster object in a group, destructive and undoable).
- **D-42 Layer compositions, history branching and snapshots.** A composition (`comp`) stores `name`, `comment`, which of visibility, position and appearance it captures, and a map from object id to `{visible, transform, appearance {opacity, fillOpacity, blendMode, stylesEnabled, maskEnabled}}`. Apply is one command producing one revision; Update recaptures; "Last Document State" is the current revision; objects created after capture are unaffected and flagged. Export Compositions runs the Stage 1 export per selected composition into a folder with the naming template `{document}-{composition}` as one job with per-item results (spec §22.4); `archmage-cli render --composition <name>` renders one. **History** becomes a revision tree: undo moves the cursor to the parent; a new command after undo creates a sibling branch; abandoned branches remain reachable within the D-26 history budget and are pruned oldest-first (in-memory pruning needs no prompt; journaled revisions are reported before pruning, spec §9.2); the History panel lists the current path and branch points; `archmage.history.switchBranch` moves the cursor. Named snapshots bookmark a revision; Restore Snapshot creates a new revision equal to the snapshot's state (undoable); snapshots pin their revision's resources. Persisted history and history search are not in Stage 2 (Q2-06). The clean save point rule is unchanged.

### 3.5 GPU

- **D-43 GPU scope in Stage 2 (gate: S1-6 outcome).** The CPU evaluator remains authoritative (ADR-0012). The opt-in Vulkan compute compositor from S1-6.2 gains the blend-mode kernels and blend-policy conversions in S2-1 so the CPU/GPU comparison covers every mode: uint8 exact under the shared rounding rule, float32 within 1e-6 for separable modes and 1e-5 for non-separable modes (the tolerances are declared in the test). No other Stage 2 node gets a GPU path; mask, transform, adjustment, style, vector and text nodes are CPU only. Rationale: blend math is the one place where shader and C++ can diverge silently, and keeping the comparison harness alive there costs one bounded task; node breadth for a GPU milestone is unknown until M3 ends. If S1-6 recorded that the GPU path is not viable, S2-1.10 is skipped with a record in the status document and ADR-0043.

## 4. Sequencing and management approach

### 4.1 Principles (spec §28 M2 exit, §29.2, §30)

1. Hierarchy before everything: groups and the tree compiler (S2-0) come first because masks, clipping, styles, compositions and embedded objects all attach to the tree.
2. Fixtures before kernels: the independent scalar reference (D-33) and the AT-003 fixture definition exist before the blend kernels are written, so the kernels are implemented against goldens rather than goldens being generated from the kernels (spec §27.3 "goldens are reviewed assets").
3. One nondestructive mechanism at a time, each with its persistence and undo in the same sub-milestone: transforms (S2-3), adjustments (S2-5), vectors (S2-7), text (S2-8), sources (S2-9), styles (S2-10). A mechanism without a native round trip is not done.
4. CPU correctness first; the GPU path only follows the blend kernels (D-43).
5. Vertical slices over catalog breadth: the adjustment and style mechanisms ship with a subset of their catalogs (D-37, D-41) and the ledger records the rest as `unstarted` with a milestone; the catalogs are never silently narrowed (spec §29.2).
6. A behavior counts only when interaction, engine behavior, undo, persistence, error handling, test and ledger entry exist (spec §1.2, §31).
7. Unfinished controls are hidden or labelled "(development)" (spec §29.2); menus list only working actions.
8. Stage 1 acceptance stays green at every exit; a regression blocks the merge.

Alternative sequencing considered: an "AT-003 first" vertical slice (groups, one blend mode, one clip, one style in a single session, then breadth). Rejected because the style render needs blur bounds, fill opacity and knockout semantics that are only coherent once the blend layer (S2-1) and masks (S2-2) exist; building them twice costs more than it saves.

### 4.2 Sub-milestone graph

| ID | Name | Depends on | Produces | Gate |
| --- | --- | --- | --- | --- |
| S2-0 | Stage entry, hierarchy, Stage 2 scaffolding | M1 gate (§1.6) | groups, tree compiler, tree panel, ADR-0031 to 0044, ledger children, schema 0.2 | G2-0: M1 gate re-verified; nested groups render, persist and undo |
| S2-1 | Blend modes, blend policy, advanced blending, GPU parity | S2-0 | 27 modes plus Pass Through, `linear` policy, blend ranges, channel enablement, knockout, `behind`, reference goldens | G2-1: every mode matches the reference; CPU/GPU comparison green or recorded |
| S2-2 | Masks, channels, clipping | S2-0, S2-1 | raster and group masks, parametric masks, active target, Channels panel, clipping chains, mask and selection conversion | G2-2: mask and clip round trips; brush paints a mask |
| S2-3 | Nondestructive transforms | S2-2 | transform node and kernels, transform tool, AT-008 | G2-3: AT-008 automated |
| S2-4 | Crop, canvas, rulers, guides, snapping, align, merge, background | S2-3 | `canvasRect` commands, rulers and units, guides and grid, snapping, align and distribute, merge, stamp, flatten, rasterize, trim, background layer | G2-4: navigation checks extended; merge equals render |
| S2-5 | Adjustment layers and per-object adjustments, core catalog | S2-2 | adjustment entity, node, panel, first catalog half, histogram | G2-5: each adjustment matches its scalar reference; round trips |
| S2-6 | Adjustment catalog completion, presets, capability table | S2-5, S2-7 (gradient model) | second catalog half, `.cube` parser and fuzz target, presets, `capabilities.md` | G2-6: catalog complete per D-37; capability table generated |
| S2-7 | Vector shapes and vector masks | S2-3, S2-2 | `core/vector/`, rasterizer, shape tools, fills and strokes, vector masks | G2-7: rasterizer acceptance; shape round trips |
| S2-8 | Basic editable text | S2-7 | `core/text/`, font catalog, shaping, layout, text tool, substitution report | G2-8: text round trips with pinned fonts; limits recorded |
| S2-9 | Embedded and linked objects, Place, linked resources | S2-3, S2-4 | sources, instances, Place, nested editing, link status and relink, update policy, AT-009 | G2-9: AT-009 automated |
| S2-10 | Layer styles | S2-1, S2-2, S2-7 | effect stack, M2 effects, global light, presets, Create Layers, AT-003 | G2-10: AT-003 automated |
| S2-11 | Layer compositions, history branching, snapshots | S2-10 | compositions and export, history tree, History panel, snapshots | G2-11: composition export equals per-state render; branch recovery tested |
| S2-12 | Stage exit | all | schema 1 (or benchmarks only), `validate --deep`, benchmarks, acceptance document, ledger, samples, report, tag | M2 gate |

Order for single-session execution: S2-0, S2-1, S2-2, S2-3, S2-4, S2-5, S2-7, S2-6, S2-8, S2-9, S2-10, S2-11, S2-12. S2-6 follows S2-7 because Gradient Map needs the gradient model; S2-8 and S2-9 may swap if an earlier AT-009 is wanted. Thirteen sessions; with the nine remaining Stage 1 sessions, the M2 gate sits roughly twenty-two sessions from today.

### 4.3 Session protocol

1. Open this plan at §5 for the target sub-milestone and `docs/implementation-status.md`.
2. Re-run the previous gate's checks: `scripts/check.sh`, `scripts/check-ui.sh`, `scripts/check-sanitizers.sh`, plus the gate's named commands. If anything fails, fix it first and record the fix.
3. Create branch `s2-<n>-<slug>` from `main`.
4. Execute tasks in order; commit per task or coherent pair with imperative subjects and task IDs in the body. Do not skip a DoD.
5. Run the sub-milestone exit criteria. Record commands, outputs and artifact paths in `docs/implementation-status.md` under the sub-milestone heading, including the state vocabulary per behavior.
6. Write the progress report section (§9.2). Merge to `main`.
7. If a §3 decision proves wrong, write a superseding ADR and add a one-line pointer in this plan's §3; never diverge silently (spec §29.2, §30). The same applies to a Stage 1 decision: supersede it in the Stage 1 plan's §2.
8. Escalate only genuine blockers (plan or spec contradictions, environment problems, decisions in §7.3 marked blocking).

### 4.4 Definition of done, per task

The Stage 1 DoD (plan §3.4: compiles under `dev-debug` and `asan-ubsan`; named tests exist with their label and pass; new public types state ownership and thread safety; the status document reflects the task; nothing outside the stated files changed without a note) plus, for any task that adds or changes a user-visible operation or a persisted structure:

- **Persistence:** the structure saves, reopens byte-exact for rasters and structurally equal otherwise, journals to the recovery workspace, survives `validate --deep`, and has a schema-minor fixture when the schema changed.
- **Undo:** the operation is one history item (or a documented coalescing rule), undo restores the exact prior revision, redo re-applies it, and the clean save point behaves.
- **Interaction:** the menu item, panel control or tool exists only if it works; it has a tooltip, accessible name and shortcut text where icon-only (spec §2.3).
- **Ledger:** the child entry's `implementation_state`, `fidelity_target`, `command_id`, `acceptance` and `test_scenarios` are updated and the report regenerated in the same commit.
- **Errors:** validation failures are structured errors with user messages (spec §26.2), never silent no-ops.

## 5. Task breakdown

Conventions: paths are relative to the repository root; "test" means a GoogleTest case unless stated; `[label]` is the CTest label; size is a guide (S under an hour of focused work, M a few hours, L most of a session). Command IDs follow ADR-0015 (`archmage.<area>.<verb>`); Stage 1 command IDs are never renamed (spec §9.1 stable IDs), so the Stage 1 `archmage.layer.*` commands keep their IDs and learn to accept group targets. Every task is subject to the §4.4 DoD in addition to its own.

### S2-0 Stage entry, hierarchy, Stage 2 scaffolding (gate G2-0)

Goal: verify the entry condition, make the document a tree, and put the Stage 2 decisions, ledger entries and status sections in place. Spec: §28 M1 exit, §6.1, §6.2, §13.1 LAY-02, §13.2 isolation, §25.2 large-tree budget, §29.2.

| Task | Description | DoD | Size |
| --- | --- | --- | --- |
| S2-0.1 | Entry check: run `scripts/check.sh`, `scripts/check-ui.sh`, `scripts/check-sanitizers.sh` and every command in `docs/testing/acceptance.md`; confirm each §1.6 item; walk the §1.8 checklist against the real tree and record the actual name and path of every artifact | `docs/implementation-status.md` gains a "Stage 2 entry" section listing each §1.6 item with evidence and each §1.8 row with its real path; any mismatch is recorded with the task wording it changes. If any §1.6 item is unmet, stop and execute the Stage 1 plan instead | M |
| S2-0.2 | Branch `s2-0-hierarchy`; `project(ArchMage VERSION 0.2.0)`; `docs/decisions/ADR-0031` to `ADR-0044` (one per D-31 to D-44, under 60 lines, D-43 and D-44 marked provisional with their gates); `CLAUDE.md` updated to this plan, the `S2-0` to `S2-12` protocol and the schema-minor rule | ADR files exist; `CLAUDE.md` under 80 lines; `archmage-cli --version` prints `ArchMage 0.2.0 (<sha>)`; `cli.version` passes | S |
| S2-0.3 | Document tree (D-32): `Group` object type; `child` relation with an explicit order index; `DocumentState` traversal (depth-first order, parent lookup, ancestor path, subtree bounds); invariants: no containment cycle, single membership, deterministic order; identifier types extended | `[document]` tests: a cycle is rejected with a `validation` error; a 10,000-object tree with 1,000 groups snapshots in under 1 s; reparenting shares structure with the prior revision (tile maps untouched) | L |
| S2-0.4 | Commands: `archmage.group.create` (from the selected objects or empty, at the position of the topmost selected), `archmage.group.ungroup`, `archmage.object.move` (new parent and index in one command); Stage 1 `archmage.layer.remove`, `.rename`, `.setVisible`, `.setOpacity`, `.setLocked`, `.duplicateInstance` accept group targets (duplicate copies the subtree, sharing sources) | `[operation]` tests per command: moving a group into its own descendant is rejected; a locked parent rejects child edits; undo and redo restore the exact tree; JSON parameter round trip; duplicate `requestId` applies once | M |
| S2-0.5 | Graph compiler for trees: the composite node takes a child list; isolated groups composite to a transparent canvas and then blend Normal with group opacity (pass-through and other modes come in S2-1); cache keys include the child-set version; invalidation propagates to ancestors only | `[image]` tests: a document with two nested groups equals a hand-computed reference; toggling a leaf's visibility invalidates its ancestors and nothing else (cache-hit counters); cancellation still stops within one tile | L |
| S2-0.6 | Native format schema 0.2: group objects, `child` relation; migration 0.1 → 0.2 (Stage 1 layers become children of an implicit root in order); per-minor fixture; `NativeReader` validates acyclicity; unknown object payloads inside a group survive open and save | `[document]` tests: round trip of a three-level tree; the 0.1 fixture opens and renders byte-identically to its Stage 1 golden; `validate --deep` reports a planted cycle as `damaged_input` | M |
| S2-0.7 | `ui/panels/LayersTreeModel` (`QAbstractItemModel` over the snapshot, lazy bounded thumbnails) replacing the Stage 1 list model; `LayersPanel` becomes a `QTreeView` with expand and collapse (view state), drag to reorder and reparent, multi-select, group and ungroup actions | UI tests: a 2,000-object tree with 200 groups shows without 2,000 widgets; a drag into a group emits exactly one `archmage.object.move`; keyboard-only reparent through the Layer menu; `[perf]` record of selection and scroll p95 against the spec §25.2 100 ms budget | L |
| S2-0.8 | Menu and shortcuts: Layer > New Group, Group Layers (Ctrl+G), Ungroup (Ctrl+Shift+G) as conventional additions (spec App. B lists no group shortcuts; record the addition as Stage 1 did for Ctrl+0 and Ctrl+1) | UI test: shortcuts dispatch the commands and are suppressed while a text input has focus | S |
| S2-0.9 | Ledger: child entries for every Stage 2 behavior named in §2.2 and §5 (`implementation_state: unstarted`, `milestone: M2`, `prerequisites`, `test_scenarios` with AT-003, AT-008 or AT-009 where applicable, `scope: null`); NAV-07 moves to M3 with a `milestone_basis` citing this plan (Q2-10); `milestone_basis` refined on MSK, LAY, GEO, TXT and VEC parents where §2.2 narrows M2; report regenerated | `python3 -I scripts/parity_report.py --check` passes; the report's M2 behavior count equals the number of child entries this plan names (recorded in the status document) | M |
| S2-0.10 | Status and corpus: Stage 2 section in `docs/implementation-status.md` with headings S2-0 to S2-12 ("Not started"), the §1.8 mapping table and the open-question table Q2-01 to Q2-11; `docs/testing/corpus-plan.md` gains the Stage 2 fixture rows (blend goldens, AT-003 fixture, AT-009 linked asset, `.cube` LUTs, system Noto fonts through fontconfig with no bundled fonts, the 100 MP composite) | Files updated; `scripts/check.sh` green | S |

Exit (G2-0): the M1 gate is re-verified and recorded; nested groups render, persist, undo and validate; the tree panel meets the large-tree budget; ADR-0031 to ADR-0044, ledger children and status sections are committed; merged to `main`.

### S2-1 Blend modes, blend policy, advanced blending, GPU parity (gate G2-1)

Goal: every spec §13.2 mode with its own equation and fixture, the `linear` policy, and the advanced blending controls. Spec: §13.2, §7.4, §7.5, §9.2 (seeds), §15.3 (determinism), §16.1 (`behind`), ADR-0009, ADR-0012.

| Task | Description | DoD | Size |
| --- | --- | --- | --- |
| S2-1.1 | `tests/reference/blend_reference.py` (pure Python, `python3 -I`): scalar implementation of every D-33 formula, the premultiplied general formula, the policy conversion (exact sRGB curve), fill opacity, knockout, blend ranges and channel enablement; `--generate` writes `tests/fixtures/golden/blend/*.json` over a 17 × 17 grid of source and backdrop values for alpha pairs {0, 0.25, 0.5, 1}; `--check` verifies the goldens' hashes; a README records provenance | Goldens committed; `--check` passes and is added to `scripts/check.sh`; changing one formula changes the hash (negative test) | M |
| S2-1.2 | `core/render/kernels/Blend`: the separable modes (Darken, Multiply, Color Burn, Linear Burn, Darker Color, Lighten, Screen, Color Dodge, Linear Dodge, Lighter Color, Overlay, Soft Light, Hard Light, Vivid Light, Linear Light, Pin Light, Hard Mix, Difference, Exclusion, Subtract, Divide) in float32 premultiplied with the D-09 alpha, NaN and negative policies | `[math]` tests against the goldens: float32 within 1e-6; uint8 exact under the documented round-half-even rule | L |
| S2-1.3 | Non-separable Hue, Saturation, Color, Luminosity; Dissolve with the seeded per-document-pixel hash (seed stored on the object, generated on creation); Normal and Pass Through plumbing | `[math]` goldens pass (1e-5 for the non-separable modes); Dissolve renders identically at 25 % and 400 % zoom, after export and after reopen | M |
| S2-1.4 | Pass-through groups per D-32 (children onto the backdrop; group opacity and mask as a lerp); isolated groups with any mode; `blendMode` field and `archmage.object.setBlendMode`; schema 0.3 (blend fields) with fixture | `[image]` fixture: pass-through and isolated groups with a Multiply child differ exactly as specified; a pass-through group at 50 % opacity equals the lerp formula; undo; round trip | M |
| S2-1.5 | Blend policy `linear` (D-33): `ColorPipeline` stages working → linear → working through lcms2 transforms, with the exact sRGB function used when the profile is the pinned sRGB; `archmage.document.setBlendPolicy` with a notice; manifest field; imports default to `encoded` | `[math]` tests: 50 % gray over white differs between policies by the predicted amount; lcms2 path equals the exact function within 1e-6; `linear` works on a uint8 document (float32 working representation); policy round trips | M |
| S2-1.6 | Advanced blending: `fillOpacity` (`archmage.object.setFillOpacity`; until S2-10 it scales content exactly like opacity, and the test says so), blend ranges with split handles (`setBlendRanges`), channel enablement (`setChannelMask`), knockout (`setKnockout`, rendering to the group or document backdrop by `1 − fillOpacity`); Blending Options dialog with live preview in an in-progress transaction, Reset, OK and Cancel | `[math]` and `[image]` tests against the reference; UI test: Cancel leaves the revision unchanged, OK yields one history item | L |
| S2-1.7 | Paint mode `behind` in `archmage.raster.paintStroke` and the brush options row | `[operation]` test: painting behind opaque pixels leaves them unchanged and fills transparent ones | S |
| S2-1.8 | Layers panel: blend mode combo (27 modes; Pass Through only for groups) and fill opacity slider with coalesced history | UI tests: changing the mode yields one history item; the combo for a raster lacks Pass Through | S |
| S2-1.9 | `docs/architecture/blending.md`: equations, policy semantics, advanced blending semantics, precision and mode restrictions; registry entries for every mode (RGB × uint8 and float32) | Document committed; a test asserts every registered mode has a golden | S |
| S2-1.10 | GPU parity (D-43, gate): the compute shader gains every mode, the policy conversion, fill opacity and knockout; comparison on the AT-002 fixture, the random 20-layer fixture and a per-mode fixture | `[image]` CPU versus GPU within the D-43 tolerances; or the task is skipped with a record in the status document and ADR-0043 when S1-6 found the GPU path non-viable | L |

Exit (G2-1): every mode matches its golden under `dev-debug` and `asan-ubsan`; the `linear` policy round-trips; the GPU comparison is green or recorded; `docs/architecture/blending.md` exists.

### S2-2 Masks, channels, clipping (gate G2-2)

Goal: raster and group masks, parametric masks, the active editing target, named channels and clipping chains, all rendering, persisting and undoing. Spec: §12.2 MSK-01 to MSK-06 and MSK-08, §12.1 (coverage), §6.2, §7.2 (halo), ADR-0020.

| Task | Description | DoD | Size |
| --- | --- | --- | --- |
| S2-2.1 | Mask entity and relation (D-34): single-plane raster content, properties and defaults; `archmage.mask.add` (`revealAll`, `hideAll`, `fromSelection`; target object or group), `.remove`, `.setEnabled`, `.setInverted`, `.setDensity`, `.setFeather`, `.setLinked`; schema 0.4 with fixture | `[document]` and `[operation]` tests: round trip; undo; a mask on a locked object is rejected; `revealAll` allocates one uniform tile | L |
| S2-2.2 | Mask render: mask node (invert, density, feather through the Gaussian node with a 3σ halo, combination) feeding object coverage in the §2.4 pipeline; group masks; cache keys include mask versions | `[image]` tests: feathered-mask seam test across tile borders (spec §15.3); density 0.5 matches the formula; a masked layer inside a masked group equals a hand-computed reference; toggling a mask invalidates only that subtree | L |
| S2-2.3 | Parametric luminosity mask `{lowIn, lowOut, highIn, highOut, feather}` evaluated on the node input (D-34); `archmage.mask.addParametric`, `.setParameters`; combination operations multiply, add and subtract in order | `[math]` tests against a scalar reference; `[image]` live combination of a raster mask and a parametric mask | M |
| S2-2.4 | Active target (view state `content`, `mask`) and the `target` parameter on `archmage.raster.paintStroke` and `archmage.raster.fillSelection`; brush writes the foreground luma as coverage; mask thumbnail click and the Layer > Mask menu switch targets; status bar names the target | `[operation]` tests: a stroke targeting the mask changes only the mask raster; UI test: clicking the mask thumbnail switches the target, outlines it and updates the status bar | M |
| S2-2.5 | Mask and selection conversion: `archmage.selection.fromMask`, `archmage.selection.fromChannel`, `archmage.mask.fromSelection` (used by `add`); sample-type scaling only (MSK-08) | `[math]` tests: uint8 ↔ float32 scaling exact at 0 and full coverage and symmetric; no profile objects touched (assert on the color pipeline's call counter) | S |
| S2-2.6 | Named channels (`chan`): document-space sparse single-plane rasters; `archmage.channel.add` (from selection or empty), `.remove`, `.duplicate`, `.rename`, `.loadIntoMask`; schema 0.4 includes them | `[document]` round trip; `[operation]` undo; a channel survives a later canvas change (S2-4 re-tests) | M |
| S2-2.7 | Channels panel: composite rows R, G, B, A as views, named channels; isolate (grayscale) and overlay (tinted) display as view state; load as selection, save selection as channel | UI tests: isolate display renders the channel as grayscale (offscreen pixel probe); load-as-selection sets the expected coverage bounds | M |
| S2-2.8 | Clipping chains (D-32): `clipped` flag, `archmage.object.setClipped`, base resolution, `blendClippedAsGroup` on the base; compiler emits clip composites; panel shows the clip indent; Alt-click between rows toggles | `[image]` fixture: a clipped raster over a 50 %-alpha base, a clipped group, and both `blendClippedAsGroup` values match the reference; ordering through a group; undo; round trip | L |
| S2-2.9 | Mask visualization: overlay and isolate views, Shift-click disables, Alt-click isolates, keyboard equivalents | UI test: toggles change view state only (revision unchanged) | S |
| S2-2.10 | `validate --deep` and `inspect` cover masks, channels and clipping; native fuzz seeds regenerated with mask objects; ten-minute fuzz run | `[interchange]` tests; run recorded in `docs/testing/fuzz.md` | S |

Exit (G2-2): mask, channel and clip round trips pass; the brush paints a mask; the feather seam test is green; the status document records each behavior's state.

### S2-3 Nondestructive transforms (gate G2-3)

Goal: instance transforms that sample the original content, with the transform tool and AT-008. Spec: §14.1 GEO-01, GEO-02, GEO-04, §6.3, §7.2, §7.3, §7.4, AT-008, ADR-0017.

| Task | Description | DoD | Size |
| --- | --- | --- | --- |
| S2-3.1 | Transform field made real (D-35): `T` on instances, parent composition, mask linkage; `archmage.object.setTransform` (absolute) and `archmage.object.transform` (compose); validation (singular, scale below 1e-6); schema 0.5 with a migration turning the Stage 1 integer offset into a translation and a fixture | `[document]` and `[operation]` tests: round trip preserves doubles (1e-12 differences survive); undo; the offset migration is exact | M |
| S2-3.2 | Resampling kernels in `core/render/kernels/Resample`: nearest, bilinear, Mitchell–Netravali (B, C) with the three presets, Lanczos3; premultiplied | `[math]` tests: constant input gives constant output exactly; bilinear preserves a linear ramp exactly at half-pixel positions; symmetry; kernel weights sum to 1 within 1e-6; Lanczos ringing on a step is bounded by the documented value | M |
| S2-3.3 | Transform node: inverse mapping per tile, required input region per kernel radius, affine fast path, homography path with local origins, byte-exact identity and integer translation, mip selection from the Jacobian for minification; cache key includes `T` and the kernel | `[image]` tests: identity byte-exact; a 90° rotation of a tile-aligned uint8 image under nearest is byte-exact; seam test on a rotation spanning tile edges; 4 × minification equals the mip reference within 1 LSB; out-of-bounds regions are transparent; a far-from-origin (10⁸ px) translation keeps sub-pixel precision | L |
| S2-3.4 | AT-008 automation: ten successive transforms (rotate, scale, skew, perspective) of a 1,000 × 1,000 raster at offset (−37, 59) render identically to the composed single transform; Escape during the eleventh restores the revision byte-exact; no intermediate rasterization occurs (tile allocation counter) | `[operation]` and `[image]` tests pass; entries added to `docs/testing/acceptance.md` | M |
| S2-3.5 | `TransformTool` (Ctrl+T, spec §11 lifecycle): handles, scale, rotate, skew, distort and perspective modes, reference point, Shift and Alt modifiers, numeric options row, Enter and Escape, labelled bilinear preview inside the in-progress transaction, convergence on release; multi-selection in one transaction; mask target | UI tests with synthetic drags: Shift keeps aspect; Escape leaves the revision unchanged; numeric entry equals the dragged matrix within 1e-9; a mask target transforms the mask only | L |
| S2-3.6 | Flip Horizontal and Vertical, Rotate 90° CW and CCW, 180° under Layer > Transform; Transform Again (Ctrl+Shift+T) and Transform Again Duplicate (Ctrl+Shift+Alt+T) replay the last matrix | `[operation]` tests; UI shortcut tests | S |
| S2-3.7 | `archmage.object.rasterizeTransform` and Layer > Rasterize > Transform, using the final-quality kernel | `[image]` test: the baked raster equals the nondestructive render byte-exact | S |
| S2-3.8 | Performance record: warm pan and zoom on the 24 MP, 20-layer fixture with five transformed layers; transform drag preview latency | `benchmarks/results/<date>-transform.json` with every spec §25.1 field | S |

Exit (G2-3): AT-008 passes under `dev-debug` and `asan-ubsan`; a manual transform with cancel and commit on the real canvas under Wayland is recorded; benchmark record committed.

### S2-4 Crop, canvas, rulers, guides, snapping, align, merge, background (gate G2-4)

Goal: the document-level geometry commands, measurement and snapping, and the destructive consolidation commands rendered through the authoritative graph. Spec: GEO-03, NAV-03, LAY-04, LAY-05, LAY-08, §7.3, §6.2, §2.3 numeric inputs, ADR-0029.

| Task | Description | DoD | Size |
| --- | --- | --- | --- |
| S2-4.1 | `canvasRect` (D-36) and `archmage.document.setCanvasRect` with labelled variants (crop, canvas size with anchor, reveal all, trim) and the `deleteCroppedPixels` option as a rasterize step in the same transaction; renderer, export and CLI `render` respect it; schema 0.6 with a migration from Stage 1 dimensions and a fixture | `[document]` and `[image]` tests: content outside the canvas survives a crop and returns on reveal all; export dimensions equal `canvasRect`; a named channel survives the change | M |
| S2-4.2 | `CropTool` (C): rectangle handles, ratio and size presets, straighten line, Enter and Escape; Image > Canvas Size dialog with anchor and units; Image > Trim; Image > Reveal All | UI tests: a crop drag emits one command; straightening a line drawn at 10° yields a −10° rotation within 1e-6 and a canvas inside the rotated bounds | L |
| S2-4.3 | Image Size: resolution-only change versus resample (D-36); `archmage.document.setResolution`; dialog with units and the resample toggle | `[operation]` tests: resolution-only leaves pixels byte-exact; resample scales top-level transforms and `canvasRect` in one transaction | M |
| S2-4.4 | Rulers and units: ruler widgets around the canvas, origin drag, tick adaptation, unit preference command (ADR-0029 field); `UnitValueEdit` shared by every options row and dialog, accepting units and arithmetic (spec §2.3) | UI tests: at 50 % and 300 % zoom a 100 px span reads 100 px and its inch equivalent at the document resolution; `UnitValueEdit` parses `2in+3mm` and `50%` where a percentage is valid | M |
| S2-4.5 | Guides and grid: document fields; `archmage.guide.add`, `.move`, `.remove`, `.clear`, `.setLocked`; `archmage.document.setGrid`; drag from rulers; View menu toggles; persistence | `[operation]` undo tests; `[document]` round trip; UI test: dragging from the ruler creates a guide at the expected document coordinate | M |
| S2-4.6 | Snapping service (`ui/snapping/`): guide, grid, canvas and object-bounds targets, 8 logical px tolerance, indicator overlay, per-target and global toggles; consumed by the Move tool (if S1-7 shipped none, add `MoveTool` (V) here using `archmage.object.transform` translations), the transform and crop tools and guide drags | UI tests: a move within 8 px of a guide lands exactly on it; a disabled target does not snap; the indicator appears | M |
| S2-4.7 | Align and Distribute (LAY-04): Layer menu and Move-tool options; alignment to selection, canvas or a Shift-clicked key object; distribution by edges and centers; transformed bounds | `[operation]` tests on rotated objects; one transaction per action | M |
| S2-4.8 | Merge Down, Merge Visible, Stamp Visible (Ctrl+Shift+Alt+E, conventional), Flatten, Rasterize (object, mask, transform), Trim Transparent: each renders a final-quality `RenderRequest` on the authoritative graph into a raster object and removes the sources in one history item | `[image]` tests: a merge equals the pre-merge composite byte-exact for uint8; a stamp with a clipped layer and a masked group equals its render; undo restores every removed object and mask | L |
| S2-4.9 | Background layer (LAY-08): flag, opaque render rule, position lock, no mask or mode, Layer From Background and Background From Layer; New Document option | `[operation]` and `[image]` tests; UI: lock icons and disabled controls on a background | S |
| S2-4.10 | Isolation view (show only selected) as view state; the extended navigation and input checks (§2.5) recorded with results | UI test for isolation; the status document lists each check with its result | S |

Exit (G2-4): merge equals render; crop, canvas, guide and grid round trips pass; rulers, guides and snapping checks are recorded; AT-001 and AT-002 remain green.

### S2-5 Adjustment layers and per-object adjustments, core catalog (gate G2-5)

Goal: the adjustment mechanism with both placements, the histogram, and the first half of the D-37 catalog, each matched against a scalar reference. Spec: §15.1, §15.3, §6.1 "Adjustment/effect", §7.2, §8.1, §10.5.

| Task | Description | DoD | Size |
| --- | --- | --- | --- |
| S2-5.1 | Adjustment entity (D-37): `adj` object with a versioned typed parameter payload, `bypassed`, masks, blend and opacity, `clipped`; `AdjustmentRegistry` declaring space, modes, sample types and implementation version per type; `archmage.adjustment.add` (target: group position for a layer, or an object for a per-object node), `.remove`, `.setParameters`, `.setBypassed`; schema 0.7 with fixture | `[document]` and `[operation]` tests: round trip; undo; an unknown adjustment type opens as an opaque bypassed node with a diagnostic and survives save (spec §10.5) | L |
| S2-5.2 | Adjustment node: an adjustment layer's input is the composite of its siblings below within the group (or its clip base); a per-object node's input is the object's content; output = `lerp(input, adjusted(input), opacity · mask)`; point-operation classification; cache keys over parameter hash and implementation version | `[image]` tests: an adjustment layer inside a group affects only that group's composite; a clipped one affects only its base chain; a bypassed node equals absence byte-exact; invalidation limited to dependents | L |
| S2-5.3 | `tests/reference/adjust_reference.py`: scalar reference for every M2 adjustment in its declared space with goldens over value grids and the float32 and uint8 encodings | Goldens committed with hashes; `--check` added to `scripts/check.sh` | M |
| S2-5.4 | Point adjustments: Invert, Threshold, Posterize, Brightness/Contrast (documented model, `approximate`), Exposure (linear light), Gamma | `[math]` tests against goldens: float32 within 1e-6, uint8 exact; registry entries declare their spaces | M |
| S2-5.5 | Levels: per-channel and composite input black, white and gamma, output black and white; black, gray and white eyedroppers set parameters from a sampled pixel | `[math]` goldens; identity parameters are byte-exact; clipping behavior documented in the test names | M |
| S2-5.6 | Curves: per-channel and composite point lists with monotone cubic (Fritsch–Carlson) or linear interpolation, freehand curves stored as a table, numeric point editing, presets | `[math]` tests: identity curve byte-exact; interpolated curves match goldens; freehand table round trips | M |
| S2-5.7 | Hue/Saturation: master and six ranges with overlapping falloff in a documented HSL model, lightness, colorize | Goldens; a continuity test shows no discontinuity at range boundaries | M |
| S2-5.8 | Histogram: global-reduction node (256 bins per channel plus luma) over an adjustment's input, computed at the view mip level during drags (labelled) and at full resolution as a job | `[image]` test: histogram of a known gradient; cancellation; the mip-level result is labelled | M |
| S2-5.9 | Adjustments and Properties panels: Layer > New Adjustment Layer submenu and a per-object "Add Adjustment" action, parameter editors (`UnitValueEdit` sliders, curve editor with histogram overlay, channel selector, eyedroppers, point editing by mouse and keyboard), Reset, Bypass, presets, live preview in an in-progress transaction with coalesced commit on release | UI tests: a slider drag yields one history item; Reset restores defaults; the curve editor moves a point by keyboard; every control has an accessible name | L |
| S2-5.10 | `validate --deep` and `inspect` cover adjustments; `archmage-cli capabilities --markdown` generates `docs/architecture/capabilities.md` from the registry; a test fails when the committed file is stale | `[interchange]` tests; file committed; stale-file test exists | S |

Exit (G2-5): every S2-5 adjustment matches its golden; both placements round-trip and undo; the histogram works; the capability table is generated and current.

### S2-6 Adjustment catalog completion, presets, LUTs (gate G2-6)

Goal: the second half of the D-37 catalog, presets, and the LUT parser. Runs after S2-7 because Gradient Map uses the gradient model. Spec: §15.1, §15.3, §26.1 (bounded parsing), §23.1 (resources).

| Task | Description | DoD | Size |
| --- | --- | --- | --- |
| S2-6.1 | Color Balance: shadows, midtones and highlights with a documented tonal weighting; preserve luminosity | Goldens | M |
| S2-6.2 | Vibrance and Saturation with a documented model (`approximate`) | Goldens | S |
| S2-6.3 | Black & White: six weights plus tint | Goldens | S |
| S2-6.4 | Channel Mixer: 3 × 3 plus constants, monochrome option | Goldens | S |
| S2-6.5 | Gradient Map using the D-38 gradient model; seeded dither | Goldens; dithering identical at two zoom levels and after export | M |
| S2-6.6 | Color Lookup: bounded `.cube` parser (1D and 3D, size limits, title and domain), trilinear interpolation, LUT stored as a resource with provenance; `fuzz/cube_fuzz.cpp` and a malformed corpus | An identity LUT is byte-exact; malformed fixtures fail with `damaged_input` within budget; ten-minute fuzz run recorded | M |
| S2-6.7 | White Balance (temperature and tint as linear-light multipliers, `approximate`) and Selective Color (`approximate`) | Goldens | M |
| S2-6.8 | Presets for every adjustment as resources `{typeId, version, parameters}`; parameters are stored inline on the adjustment, the preset id is provenance only | `[document]` test: deleting a preset resource leaves the render unchanged | S |
| S2-6.9 | Ledger states and the capability table refreshed; `docs/architecture/adjustments.md` (catalog, spaces, fidelity, deferred items) | Checks green; document committed | S |

Exit (G2-6): the catalog is complete per D-37; `capabilities.md` regenerated; fuzz recorded.

### S2-7 Vector shapes and vector masks (gate G2-7)

Goal: `core/vector/`, the rasterizer, live shapes with fills and strokes, and vector masks. Spec: §17.2 VEC-02 and VEC-04, §15.1 gradient requirements, §12.2 MSK-01, §6.3, §26.1.

| Task | Description | DoD | Size |
| --- | --- | --- | --- |
| S2-7.1 | `core/vector/`: `Path` (cubic segments, closed flag, fill rule), flattening, bounds, transform; `Shape` generators (rectangle with four radii, ellipse, polygon and star, line with arrowheads); `Paint` and `Stroke` types; versioned JSON payload | `[math]` tests: generators produce the expected node counts; path transforms are exact for affine inputs; payload round trip | M |
| S2-7.2 | Rasterizer (D-38): sparse scanline, 16 sub-scanlines, exact horizontal coverage, nonzero and even-odd, writing coverage tiles; benchmark on a 10,000-node path | `[math]` acceptance tests from D-38; `[perf]` record | L |
| S2-7.3 | Stroker: offsetting with caps and joins, miter limit, dashes by arc length, inside and outside alignment by clipping | `[math]` tests: a stroked unit square at width 2 has the expected area within 0.5 %; dash counts; round joins are continuous | M |
| S2-7.4 | Gradient evaluation: linear and radial, stops with midpoints and opacity, repeat modes, interpolation space, seeded dither (consumed later by Gradient Map and Gradient Overlay) | `[math]` tests against a scalar reference; dither deterministic across zoom | M |
| S2-7.5 | Shape object and vector node: rasterize fill then stroke in document space after the instance transform; local operation with bounds from path and stroke extent; `archmage.shape.create`, `.setParameters`, `.setFill`, `.setStroke`; schema 0.8 with fixture | `[image]` tests: a 100 × 100 rectangle at integer coordinates renders exact 0 and 1; a 45° rotated shape renders without resampling (it is the rotated path); round trip; undo | L |
| S2-7.6 | Shape tools (U family: Rectangle, Rounded Rectangle, Ellipse, Polygon, Star, Line): drag with Shift and Alt, snapping, options row with fill, stroke, radii, sides and arrowheads, live parameter editing of the selected shape; spec §11 lifecycle | UI tests: one drag creates one shape with one history item; editing the radius afterwards changes the live shape | L |
| S2-7.7 | Vector masks (MSK-01): `archmage.mask.addVector` (from the selected shape or empty), parameter edits through the shape commands, rasterized by the D-38 rasterizer, linked to the transform | `[image]` test: the vector mask equals the rasterized shape coverage; round trip | M |
| S2-7.8 | Layer > Rasterize > Shape; the AT-008 vector case (ten transforms of a shape render identically to one) | `[image]` tests | S |
| S2-7.9 | `fuzz/vector_payload_fuzz.cpp` and a malformed corpus (NaN coordinates, a claimed 10⁷ nodes, degenerate segments); `validate --deep` covers shapes | Ten-minute fuzz recorded; `[resilience]` corpus tests | M |

Exit (G2-7): rasterizer acceptance green; shapes and vector masks round-trip; tools checked manually under Wayland.

### S2-8 Basic editable text (gate G2-8)

Goal: editable text objects with real shaping, pinned fonts and the substitution report, within the D-39 limits. Spec: §17.1 TXT-01 and TXT-02 subsets, §6.1 "Text content", §9.2 coalescing, App. B (T), §26.3, §5.3.

| Task | Description | DoD | Size |
| --- | --- | --- | --- |
| S2-8.1 | Dependencies: fontconfig and ICU lookups in `cmake/Dependencies.cmake`; manifest entries with license and redistribution notes; `archmage_text` links HarfBuzz, FreeType, fontconfig and ICU | Configure is clean; `manifest_test.py` and `--check-installed` pass | S |
| S2-8.2 | `core/text/FontCatalog` over fontconfig: families and styles, resolution of `{family, style}` to file and index, lazy file SHA-256, version string from the font's name table; `FontRef` resource | `[document]` tests: "Noto Sans" Regular resolves; a bogus family yields `unsupported_input`; the hash is stable across runs | M |
| S2-8.3 | `TextContent` model and payload; style runs; paragraph styles; frames; ICU grapheme iteration; pure edit operations (insert, delete range, style a range, split paragraph) | `[math]` tests: run splitting and merging invariants; grapheme boundaries for a combining sequence and an emoji ZWJ sequence; payload round trip | M |
| S2-8.4 | Shaping and layout: HarfBuzz per run with script itemization and no fallback fonts (missing glyphs render `.notdef` and are reported); whitespace line breaking in boxes; point text; leading, tracking, baseline shift, alignment; right-to-left detection raises the D-39 notice | `[math]` tests: a known Latin string in Noto Sans at 12 pt gives the expected advance sum within 1e-3 px; a box wraps at the expected word; an Arabic string shapes (glyph count above zero) and sets the notice | L |
| S2-8.5 | Text object and node: FreeType outlines (unhinted) to `Path` to the rasterizer after the instance transform; bounds from layout; cache keys include font hashes; `archmage.text.create`, `.edit` (range replacement with a coalescing key), `.setStyle`, `.setParagraphStyle`, `.setFrame`; schema 0.9 with fixture | `[image]` tests: a reviewed golden of one glyph at 100 px; round trip including font references; typing "abc" in one burst is one history item | L |
| S2-8.6 | `TextTool` (T): click for point text, drag for a box; in-canvas editing with grapheme caret, selection, clipboard, Enter newline, Escape and Ctrl+Enter commit, single-letter shortcuts suppressed, input-method commit strings; options row (family from the catalog, style, size with units, color, alignment); Properties panel for tracking, leading and baseline shift | UI tests: typing while editing never switches tools; Escape commits; the font list is populated offscreen | L |
| S2-8.7 | Missing fonts: a fixture referencing a nonexistent font opens with a substitution report (shared `CompatibilityReport` type, reused by S2-9), shows the cached appearance resource until the text is edited, then re-shapes with the substitute and records it; the original reference survives save | `[document]` tests: report content; cached appearance byte-exact until edit; original font reference preserved | M |
| S2-8.8 | Layer > Rasterize > Text; the AT-008 text case; `validate --deep` covers text; `fuzz/text_payload_fuzz.cpp` | `[image]` and `[resilience]` tests; fuzz recorded | M |
| S2-8.9 | Ledger: TXT-01 and TXT-02 children with real states; the D-39 limits recorded as `limitations` and as M5 children | `--check` green | S |

Exit (G2-8): text round-trips with pinned fonts; the substitution report works; limits are recorded; a manual run (type Latin text, transform, save, reopen) is recorded.

### S2-9 Embedded and linked objects, Place, linked resources (gate G2-9)

Goal: sources and instances, placement, nested editing, link status and relinking, and AT-009. Spec: §13.3, §6.1 Embedded and Linked rows, §6.2, §9.3, §7.3, §10.5, §26.1, AT-009.

| Task | Description | DoD | Size |
| --- | --- | --- | --- |
| S2-9.1 | `Source` entity (D-40): kinds `image` and `document`, resource-backed content, `sourceRevision`; `source` relation; `archmage.source.placeEmbedded` (path or bytes), `.duplicateInstance` (shares), `.duplicateSource` (copies), `.replaceContent`; decoded rasters registered with the coordinator as disposable; schema 0.10 with fixture | `[document]` and `[operation]` tests: round trip; two instances share one resource (file size); `duplicateSource` adds one; undo | L |
| S2-9.2 | Rendering: an image instance decodes through the Stage 1 codecs with their limits and feeds the §2.4 pipeline; a document source compiles a nested graph from the nested revision (cycle-checked, depth limit 8); cache keys include `sourceRevision` | `[image]` tests: a placed PNG at identity equals the decoded PNG byte-exact; a nested `.archmage` composite equals its own CLI render | L |
| S2-9.3 | Place Embedded and Place Linked (File menu, drag-and-drop): PNG, JPEG and `.archmage`; centered at native size or scaled to fit; new instance above the active object; one history item | UI tests: dropping a PNG emits one `placeEmbedded`; a 10,000 px image is scaled to fit | M |
| S2-9.4 | Linked sources: `link` record with absolute and relative paths, fingerprint, status, last-known-good capture; `archmage.source.placeLinked`, `.convertToLinked`, `.convertToEmbedded`, `.relink`, `.relinkFolder`, `.update`, `.setUpdatePolicy`; open-time check under `promptOnOpen` as a non-modal bar (spec §26.2); missing links render the last-known-good with a badge | `[document]` tests: a changed file gives `changed`; a renamed file gives `missing` and Relink Folder finds it by fingerprint; an A-links-B-links-A fixture gives `cyclic` with bounded recursion; an undecodable file gives `inaccessible` with the last-known-good shown | L |
| S2-9.5 | Nested editing: Edit Contents opens the nested document in a child tab; Save there runs `archmage.source.commitNested` in the parent through the job model with spec §9.3 reconciliation | `[operation]` test: a commit after the source was deleted becomes a candidate; UI test: edit, save, parent re-renders | L |
| S2-9.6 | AT-009: fault point `export_between_tiles` in the export path (ADR-0027 style); `[resilience]` harness pauses `archmage-cli render` there while rewriting the linked PNG; the output equals the pre-change render; `inspect` then reports `changed`; the in-app export path is covered by the same hook | Tests pass; `docs/testing/acceptance.md` updated | M |
| S2-9.7 | Cross-document copy, cut and paste of objects (`archmage.object.paste` with a serialized subtree plus referenced resources; Edit menu); the §6.2 placement rules and the profile-mismatch notice (D-40) | `[operation]` tests: resources deduplicated by hash; names suffixed on collision; the notice is emitted and samples are unchanged | M |
| S2-9.8 | Layers panel and Properties: source badges (embedded; linked ok, changed, missing, cyclic), link path, Update, Relink, Replace Contents, Edit Contents and Convert actions; a report dialog listing link issues through `CompatibilityReport` | UI tests: badge states for planted fixtures; actions dispatch the commands | M |
| S2-9.9 | `validate --deep` and `inspect` cover sources and links (local paths only; nothing remote is contacted, spec §10.5); fuzz seeds with nested documents; a nesting-depth fixture | `[interchange]` tests; `[resilience]`: a depth-100 fixture is rejected within budget | M |

Exit (G2-9): AT-009 passes under `dev-debug` and `asan-ubsan`; `docs/samples/stage2-linked.archmage` and its asset are committed; a manual Place and relink under Wayland is recorded.

### S2-10 Layer styles (gate G2-10)

Goal: the effect stack with the M2 effects, fill opacity and knockout consumers, and AT-003. Spec: §13.4, §6.1, §7.2, §13.2 (fill opacity, knockout), AT-003.

| Task | Description | DoD | Size |
| --- | --- | --- | --- |
| S2-10.1 | Effect entity (`fx`) and ordered `effect` relation with the canonical order enforced by type; D-41 parameters; document global light; `archmage.style.addEffect`, `.removeEffect`, `.setEffect`, `.setGlobalLight`, `.clear`; schema 0.11 with fixture | `[document]` and `[operation]` round trip; undo; a second Stroke is rejected with a clear error (M3 scope) | M |
| S2-10.2 | `tests/reference/style_reference.py`: scalar reference for shadow, glow, stroke and overlay math on small coverage fields with a reference Gaussian; goldens | Goldens committed; `--check` in `scripts/check.sh` | M |
| S2-10.3 | Style render nodes: coverage-derived effects, contour curves, spread and choke, seeded noise, per-effect blend, opacity and mask; render bounds expansion; cache keys | `[image]` tests against goldens within 1e-5: drop shadow offset, size and spread; inner shadow; both glows; color and gradient overlays; Stroke exact for a vector object and within 1 px of the path stroke for a raster object; an object at the canvas edge renders its 50 px shadow outside the content bounds; seam test across tiles | L |
| S2-10.4 | Fill opacity and knockout completed with styles present (D-33, D-41) | `[image]` tests against the reference | M |
| S2-10.5 | Layer Style dialog (Blending Options plus per-effect pages, live preview in an in-progress transaction, Reset, OK, Cancel, Use Global Light); Layers panel fx badge with expandable effect rows and eye toggles; Copy, Paste and Clear Style; Scale Effects | UI tests: Cancel restores the revision; OK is one history item; an eye toggle dispatches `setEffect` | L |
| S2-10.6 | Presets as resources (`archmage.style.applyPreset`, save preset); no bundled presets (ADR-0025) | `[document]` test: preset provenance stored, parameters inline | S |
| S2-10.7 | Create Layers (`archmage.style.toLayers`): each effect becomes a raster object in a group, rendered at final quality, destructive and undoable | `[image]` test: the group's composite equals the styled render byte-exact for uint8 | M |
| S2-10.8 | AT-003: `docs/samples/stage2-composite.archmage` (two nested groups, one pass-through and one isolated, a clipped adjustment, a clipped raster, a group mask, a drop shadow and a stroke, three blend modes including a non-separable one); `tests/reference/at003_reference.py` composes the scalar references; `[image]` comparison; `validate --deep` | Tests pass; the sample, its reference PNG and the generator are committed | L |
| S2-10.9 | Ledger children for styles (M2 subset implemented, M3 subset `unstarted` at M3); `docs/architecture/styles.md` | Checks green | S |

Exit (G2-10): AT-003 passes under `dev-debug` and `asan-ubsan`; a manual style edit under Wayland is recorded.

### S2-11 Layer compositions, history branching, snapshots (gate G2-11)

Goal: compositions with export, and the history tree with snapshots. Spec: LAY-06, §22.4, §9.2, §9.3, §10.4.

| Task | Description | DoD | Size |
| --- | --- | --- | --- |
| S2-11.1 | Composition entity (D-42); `archmage.comp.create`, `.update`, `.apply`, `.delete`, `.rename`, `.setCapture`; schema 0.12 with fixture | `[document]` and `[operation]` tests: Apply is one revision; objects created after capture are unaffected and flagged; undo | M |
| S2-11.2 | Compositions panel: list, apply, update, new, delete, capture toggles, Last Document State, stale flags | UI tests | M |
| S2-11.3 | Export Compositions: dialog (compositions, Stage 1 format options, naming template, folder); one job with per-item results where one failure does not abort the rest; `archmage-cli render --composition <name>` | `[interchange]` tests: each file equals the CLI render of the applied composition byte-exact; a planted write failure is reported while the others complete | M |
| S2-11.4 | History tree (D-42): revision DAG with retained branches and a cursor; undo and redo along the current path; `archmage.history.switchBranch`; budget pruning oldest-first with the spec §9.2 report rule; resource retention for reachable revisions and snapshots | `[operation]` tests: undo twice, new command, switch back, redo both; a tiny budget prunes the oldest revisions not reachable from a snapshot and never the saved revision; the clean save point is correct across branches | L |
| S2-11.5 | Named snapshots: `archmage.history.snapshot`, `.deleteSnapshot`, `.restoreSnapshot` (a new revision); snapshots persist in the native file as named states sharing resources, bounded (default 16) with a size warning | `[document]` tests: a snapshot survives save and reopen; restore is undoable; the file grows only by changed tiles | M |
| S2-11.6 | History panel: the current path with branch markers, snapshot list, click to jump, keyboard access | UI tests | M |
| S2-11.7 | Recovery with branches: the journal covers the current path only (ADR-0014 unchanged); branches are in-memory; `docs/architecture/history.md` | `[document]` test: recovery after a kill restores the current revision; the limitation is recorded | S |
| S2-11.8 | Ledger states and docs | Checks green | S |

Exit (G2-11): composition export equals per-state renders; branch recovery and snapshots are tested.

### S2-12 Stage exit (M2 gate)

Goal: evidence, the schema decision, benchmarks, the ledger, samples and the report. Spec: §28 M2 exit, §29.3, §3.3, §10.1, §25.2, §27.4, §31.

| Task | Description | DoD | Size |
| --- | --- | --- | --- |
| S2-12.1 | Full matrix: `scripts/check.sh`, `check-ui.sh`, `check-sanitizers.sh`, `tsan` on the `operation`, `document` and `resilience` tiers, ten-minute fuzz runs for the native, PNG, cube, vector and text targets; fix or document every failure | All green, or each failure has an entry in the status document with its owning milestone | L |
| S2-12.2 | Schema benchmarks and, per Q2-07, the major 1 freeze (D-44): open, save and lookup timings on the 2,000-object and 100 MP fixtures, blob size limit, compaction, migration 0.12 to 1.0 with fixtures, ADR-0044 finalized, `docs/architecture/native-format.md` updated | Numbers in `benchmarks/results/`; every fixture opens; or benchmarks only with the freeze deferred and recorded | L |
| S2-12.3 | `validate --deep` coverage: every object and relation type in the schema has a validation rule and a malformed fixture | `[resilience]` corpus extended; a test enumerates the types | M |
| S2-12.4 | Architecture documents refreshed (`capabilities.md`, `blending.md`, `adjustments.md`, `styles.md`, `history.md`, `overview.md` module map) and verified against source in a separate pass | Changed claims listed with their source locations | M |
| S2-12.5 | `docs/testing/acceptance.md`: AT-003, AT-008, AT-009, the native round-trip suite, the Stage 1 regressions and the extended navigation checks with exact test names and commands | Every name resolves in `ctest -N` | S |
| S2-12.6 | Ledger: every Stage 2 child with real state, fidelity and tests; report regenerated; counts match the status document | `--check` passes | M |
| S2-12.7 | Samples: `docs/samples/stage2-composite.archmage`, `stage2-linked.archmage` with its asset, `stage2-text-shapes.archmage`, each with an exported PNG and `validate --deep` output | Committed; exit code 0 | S |
| S2-12.8 | Benchmark refresh: large-tree p95; 24 MP, 20-layer composite with modes, masks and styles (warm pan and zoom); invalidation scope; adjustment drag latency; 100 MP open and save; compared with the spec §25.2 budgets, record only | Results JSON committed; pass-fail table in the report | M |
| S2-12.9 | Manifest and ADR refresh: fontconfig and ICU recorded; provisional ADRs resolved or carried to M3 with a note | Manifest tests pass; no provisional ADR without a carry-forward note | S |
| S2-12.10 | Manual validation under Wayland: the M2 demonstration (create, group, mask, clip, blend mode, adjustment, shape, text, place linked, style, composition, snapshot, save, reopen, export) with captures under `docs/decisions/evidence/S2-12/` | Recorded in the status document | M |
| S2-12.11 | Progress report per spec §29.3 (§9.2 template), M3 entry conditions, open risks; tag `v0.2.0-alpha.1` | Report committed; tag exists; `archmage-cli --version` shows it | S |

M2 gate: AT-003, AT-008, AT-009 and the native round-trip suite pass; Stage 1 acceptance still passes; the hierarchy and graph have the real consumers listed in §2.5; every visible operation has undo and persistence or is hidden; the demonstration runs; no known data-loss defect, false save success or fake control exists.

## 6. Testing and verification strategy

### 6.1 Tiers (spec §27.1 mapped to Stage 2, extending the Stage 1 plan §5.1)

| Tier | Label | Stage 2 content | Entry point |
| --- | --- | --- | --- |
| 1 Mathematical | `math` | blend equations and policy conversion, mask combination, resampling kernels, rasterizer coverage, stroker geometry, gradients, text shaping metrics, grapheme boundaries, adjustment transfer functions, style falloffs | `ctest --preset dev-debug -L math` |
| 2 Operation | `operation` | every new command: validation, result, undo and redo, coalescing, JSON round trip; tree invariants; history branching; job reconciliation for nested commits and link updates | `-L operation` |
| 3 Document | `document` | round trips per object type, schema minors and migrations, snapshots, link status, font references, resource sharing, recovery with branches | `-L document` |
| 4 Image | `image` | reference composites (AT-003), transform determinism (AT-008), invalidation scope, seam tests for feather, blur-based styles and transforms, merge equals render, CPU versus GPU for blend modes | `-L image` |
| 5 UI | `ui` | offscreen Qt Test workflows: tree panel, blend options, mask target, transform and crop tools, rulers, guides and snapping, adjustments and style dialogs, shape and text tools, source actions, compositions and history panels | `scripts/check-ui.sh` |
| 6 Interchange | `interchange` | native fixtures per minor, CLI `inspect`, `validate --deep`, `render --composition`, composition export, cross-document paste | `-L interchange` |
| 7 Resilience | `resilience` | AT-009 export pinning, malformed corpora for masks, shapes, text, LUTs, nested documents and cycles, fuzz seeds | `-L resilience`; fuzz under the `fuzz` preset |
| 8 Performance | `perf` | large-tree p95, warm pan and zoom with modes, masks and styles, invalidation scope, adjustment drag latency, 100 MP open and save; records only | `benchmarks/bench_main` |
| 9 Human | none | the user runs the M2 demonstration under Wayland after S2-12; broader artist validation is M3 onward | manual, recorded |

### 6.2 Reference implementations and goldens (spec §27.3)

Every kernel family with a closed-form definition gets an independent scalar reference under `tests/reference/` written in pure Python, run with `python3 -I`, deterministic, with no third-party packages: `blend_reference.py` (S2-1), `adjust_reference.py` (S2-5), `style_reference.py` (S2-10) and `at003_reference.py` (S2-10), plus the gradient and rasterizer checks embedded in their `[math]` tests. Goldens live in `tests/fixtures/golden/<family>/` with a README stating the generator, its arguments and the hash of each file; `--check` modes run inside `scripts/check.sh`. Goldens are reviewed assets: changing one requires a commit message explaining why, and a failing comparison is never fixed by regenerating.

### 6.3 Comparison policy

- Exact byte comparison for uint8 tiles, stored blobs, undo and redo, merges and flattens, baked transforms, identity transforms and integer-document exports. The rounding rule for uint8 encoding is round-half-even from float32, documented once in `core/render/kernels/Encode` and used by the references.
- float32: 1e-6 absolute for compositing and separable blend modes, 1e-5 for non-separable modes and for blur-based results (feather, shadows, glows); per-adjustment tolerances declared in each test; resampling kernels tested analytically (constants, ramps, symmetry, partition of unity) rather than against images where possible.
- Every pixel comparison carries structural assertions: object counts and types, relation counts, order, names, sample type, profile hash, font references, source fingerprints, canvas rectangle.
- Fidelity against benchmark products is recorded as `approximate` and "not verified" throughout Stage 2 because no licensed reference application is available (spec §27.3); the references above are the contract.

### 6.4 Sanitizers, fuzzing, static checks

`asan-ubsan` runs the full suite at every sub-milestone exit. `tsan` runs the `operation`, `document` and `resilience` tiers from S2-9 onward (nested commits, export pinning and histogram jobs add concurrency). libFuzzer targets: the native reader (seeds regenerated whenever a schema minor lands), PNG (unchanged), `cube_fuzz` (S2-6), `vector_payload_fuzz` (S2-7), `text_payload_fuzz` (S2-8); ten minutes each at S2-6, S2-7, S2-8 and S2-12, recorded in `docs/testing/fuzz.md`. clang-tidy stays advisory through `scripts/tidy.sh`.

### 6.5 Fixtures and corpus (spec §25.3, §27.4)

Small fixtures stay in the repository: goldens, the AT-003 sample, the AT-009 sample and its linked PNG, malformed corpora, `.cube` LUTs authored by the project (identity and a hue rotation), schema-minor fixtures. Fonts are used from the system through fontconfig and never committed (ADR-0025). The 2,000-object tree, the 24 MP 20-layer composite with modes, masks and styles, and the 100 MP composite are generated by `scripts/make_fixtures.py` with fixed seeds into `~/.local/share/archmage-dev/corpus/` and registered in `docs/testing/corpus-plan.md` with hashes.

### 6.6 Performance

Records only, not a gate (spec §25.2 budgets are targets): large-tree selection and scroll p95 ≤ 100 ms at 2,000 objects; warm pan and zoom p95 ≤ 16.7 ms on the cached 24 MP, 20-layer composite, qualified by the modes, masks and styles present; invalidation scope (tiles re-evaluated after one leaf change); adjustment slider latency p95 ≤ 50 ms to visible preview; 100 MP open and save times. Each record carries every spec §25.1 field.

### 6.7 Not verified in Stage 2

Benchmark fidelity of any blend mode, adjustment or style (no licensed reference); Windows and macOS; tablets; HDR; display color accuracy; compositors other than Hyprland; bidirectional and complex-script layout; non-RGB modes and uint16 or float16 end to end; GPU evaluation beyond blend modes; accessibility beyond keyboard reachability. Each is listed in `docs/implementation-status.md` as a gap with reproduction instructions (spec §29.2).

## 7. Risks, ambiguities, and open questions

### 7.1 Risks and mitigations

| Risk | Impact | Mitigation | Where |
| --- | --- | --- | --- |
| Stage 1 produces contracts that differ from §1.8 (names, shapes, a missing piece such as a Move tool or the blur node) | Stage 2 tasks mis-specified | S2-0.1 walks the checklist and records mappings; decisions stay, task wording changes | S2-0 |
| Stage 1 still has nine sub-milestones; schedule drift | Stage 2 starts late or never | The entry condition is explicit and date-free; the user decides Q2-02 | §1.6 |
| I-01 unresolved: the engine error type is unknown | API churn across every Stage 2 module | The plan names the type generically as `Expected`; S2-0 adopts Stage 1's resolution | S2-0 |
| No licensed reference application for blend modes, adjustments or styles | Parity cannot be claimed; user expectations | Fidelity `approximate`, scalar references are the contract, ledger says "not verified against benchmark" (spec §27.3) | S2-1, S2-5, S2-10 |
| M2 catalogs (adjustments, styles, text) expand during implementation | The stage never exits | D-37, D-39 and D-41 subsets with the remainder visible in the ledger; tasks outside §5 are rejected; Q2-03 to Q2-05 | continuous |
| Cache keys miss a new input (mask version, source revision, style parameter) or invalidate too much | Stale or slow renders | A cache-key test per node; invalidation-scope tests with hit counters; performance records | S2-0, S2-2, S2-9, S2-10 |
| Homography resampling loses precision far from the origin; minification aliases | Visible artifacts | Local origins (spec §6.3); analytic kernel tests; mip selection from the Jacobian; a 10⁸ px translation test | S2-3 |
| AT-009 depends on timing | Flaky test | The deterministic `export_between_tiles` fault point; immutable revision resources make the result independent of timing | S2-9 |
| Nested document cycles or deep nesting | Stack overflow or hang | Identity-chain cycle check; nesting limit 8; a depth-100 fixture | S2-9 |
| Untrusted inputs: `.cube` files, nested `.archmage`, vector and text payloads, font files through FreeType | Crash or compromise | Bounded parsers, fuzz targets and size limits; font parsing stays in-process with FreeType until the M4 isolated decoders, recorded as a gap | S2-6 to S2-9 |
| fontconfig and ICU dependencies and licenses | Manifest and legal exposure | Manifest entries under the ADR-0025 interim policy; dynamic linking | S2-8 |
| The project rasterizer is numerically wrong on degenerate geometry | Wrong coverage or empty output | D-38 acceptance tests and the spec §17.2 robustness fixtures | S2-7 |
| Text scope creep (bidi, IME, OpenType) | S2-8 overruns | D-39 limits with user-visible notices; M5 ledger entries | S2-8 |
| History branches retain tile versions | Memory growth | History-budget pruning oldest-first; snapshots pin; tests under a tiny budget | S2-11 |
| Schema major 1 frozen before enough is known | Premature compatibility promises | Benchmarks first; the freeze is conditional on Q2-07; migration fixtures | S2-12 |
| Rolling-release upgrades mid-stage (Qt, Clang, HarfBuzz) | Build breaks | `manifest_test.py --check-installed` in `scripts/check.sh`; pins updated as manifest changes with a rebuild check | continuous |
| Offscreen UI tests depend on fontconfig and input methods | Flaky UI tier | fontconfig is verified headless in S2-8.6; no input-method composition in tests | S2-8 |
| Adjustment layers over a 24 MP composite recompute everything per slider tick | Sluggish editing | Dirty-region evaluation, view-mip previews, coalesced commits; records in S2-12; GPU breadth after M3 | S2-5, S2-12 |

### 7.2 Spec ambiguities and gaps found while planning

| ID | Issue | Spec | Plan assumption | User input |
| --- | --- | --- | --- | --- |
| A2-01 | "Stage 2" is not a spec term | §28 | D-31: Stage 2 is M2 | Q2-01 |
| A2-02 | M2 "adjustments" is unqualified while §15.1 lists some thirty operations | §28 M2, §15.1 | D-37 subset; remainder at M3 and M4 | Q2-03 |
| A2-03 | M2 "styles" is unqualified while §13.4 lists a full catalog | §28 M2, §13.4 | D-41 subset; remainder at M3 | Q2-04 |
| A2-04 | "basic editable text/shapes" is undefined | §28 M2, §17 | D-38 and D-39 define it | Q2-05 |
| A2-05 | "linked-resource handling" overlaps M4 packaging and preflight and M8 portable projects | §13.3, §17.3, §24.1, §28 | packaging and preflight are M4 | none |
| A2-06 | Only source-over is given an equation; Photoshop-specific modes have no official public formulas | §7.4, §13.2 | D-33 formulas; fidelity `approximate` | none |
| A2-07 | Pass-through group with opacity or mask is unspecified | §13.2 | D-32 lerp model | none |
| A2-08 | Fill opacity, knockout and blend ranges relative to clipping and styles | §13.2 | §2.4 pipeline order, D-33, D-41 | none |
| A2-09 | Whether style effects derive from coverage before or after masks | §13.4, §12.2 | after masks; the option is M3 | none |
| A2-10 | Mask coordinate space under group transforms | §12.2 MSK-02 | D-34 and D-35 linkage model | none |
| A2-11 | "Reusable named masks" | §12.2 MSK-01 | named channels loadable into masks | none |
| A2-12 | History branching and snapshots have no milestone | §9.2 | D-42: M2 | Q2-06 |
| A2-13 | Schema major 1 "at the M2 exit" while the product is still an internal alpha | §10.1, ADR-0013 | conditional freeze | Q2-07 |
| A2-14 | Dissolve must be deterministic across zoom and reopen | §13.2, §15.3 | per-document-pixel hash with a stored seed | none |
| A2-15 | Cross-document paste before profile conversion exists | §6.2, §8.2 | keep samples, show a notice | Q2-11 |
| A2-16 | "Every visible operation has functioning undo and persistence" versus view-state operations (collapse, isolate, overlays) | §28 M2, §6.2 | view state is excluded and documented | none |
| A2-17 | Layer composition "batch export" versus the §22.4 export workspace | §13.1, §22.4 | minimal composition export through the Stage 1 export | none |
| A2-18 | Which fonts may be referenced; "cached appearance where allowed" | §17.1 | system fonts only; a cached raster resource; no embedding | none |
| A2-19 | GPU breadth per milestone is unstated | §7.5, §28 | D-43 | Q2-08 |
| A2-20 | NAV-03 and NAV-07 sit in M2 only by ledger inference | ledger `inferred:` | NAV-03 subset in; NAV-07 to M3 | Q2-10 |
| A2-21 | "bicubic families" and "validated edge/detail-preserving options" | GEO-04 | the Mitchell–Netravali family; other options M3 and M6 | none |
| A2-22 | M2 "linked" content includes RAW in §13.3, but RAW is M4 | §13.3, §28 M4 | RAW sources arrive with M4 | none |

### 7.3 Open questions

| ID | Question | Blocking | Default if unanswered |
| --- | --- | --- | --- |
| Q2-01 | Confirm Stage 2 = M2 alone (D-31). | Before S2-0 | M2 alone |
| Q2-02 | Stage 1 has nine sub-milestones left and nothing beyond bootstrap exists. Execute the Stage 1 plan as written first (recommended), or hold a Stage 1 re-planning session? | Before the next implementation session | Execute Stage 1 as written |
| Q2-03 | Adjustments: accept the D-37 M2 subset with the remainder at M3 and M4, or require the full §15.1 catalog in M2 (about two more sessions)? | Before S2-5 | D-37 subset |
| Q2-04 | Styles: accept the D-41 subset (seven effects plus contour) with bevel/emboss, satin, pattern overlay, texture and multiple fills or strokes at M3 (about one more session if kept in M2)? | Before S2-10 | D-41 subset |
| Q2-05 | Text: accept the D-39 limits (left-to-right layout with a notice, whitespace breaking, no OpenType controls, no color fonts) and the fontconfig and ICU dependencies? | Before S2-8 | As D-39 |
| Q2-06 | History branching and named snapshots in M2 (D-42) with persisted history and search deferred, or defer branching entirely to M3? | Before S2-11 | D-42 |
| Q2-07 | Freeze schema major 1 at the M2 exit (ADR-0013; Stage 1 Q-07 is still open), or benchmark only and freeze at the first public release? | Before S2-12 | Freeze at the M2 exit |
| Q2-08 | GPU: extend the compute compositor to blend modes (D-43), or keep Stage 2 CPU-only? | Before S2-1.10 | D-43 |
| Q2-09 | Inherited Stage 1 questions: I-01 (C++23 or an `Expected` type; blocks S1-2 and therefore all of Stage 2), Q-02 (license model), Q-05 (hosting and CI), Q-08 (ledger audit owner). | I-01 before S1-2; the others before S2-12 | Stage 1 defaults |
| Q2-10 | Move NAV-07 (command palette, shortcut editor) from the ledger's inferred M2 to M3? | Before S2-0.9 | Move to M3 |
| Q2-11 | Cross-document paste on a profile mismatch: keep samples with a notice (D-40) until M4, or block the paste? | Before S2-9.7 | Keep with a notice |

## 8. Handoff brief: S2-0 Stage entry, hierarchy, Stage 2 scaffolding

Self-contained instructions for the first Stage 2 implementation session. Read §3 once; do not re-derive the decisions. Do not read this brief as permission to skip Stage 1.

### 8.1 Goal

Verify that Stage 1 is complete, then turn the flat layer list into a document tree with groups that render, persist, undo and validate, and put the Stage 2 decisions, ledger entries and status sections in place. No blend modes, masks, transforms or other M2 mechanisms: S2-1 onward own them.

### 8.2 Preconditions and the stop rule

Before anything else, confirm every item of §1.6 from `docs/implementation-status.md`, `git tag` and the acceptance commands. If any item is unmet: do not start S2-0; execute the next Stage 1 sub-milestone under `docs/plans/Stage1_Development_Plan.md` instead, and report which §1.6 item was missing. As of 2026-10-13 all of them are unmet (only S1-0 exists), so the first session that opens this brief will almost certainly take that branch.

### 8.3 Environment facts (verified 2026-10-13; re-verify at session start)

- Machine: CachyOS (Arch), kernel 7.2.9-2-cachyos, Hyprland 0.56.2 on Wayland, 24 hardware threads, 31 GiB RAM, 321 GiB free on the project filesystem. NVIDIA RTX 4050 Laptop (kernel module 615.78.08 matching `nvidia-utils 615.78.08-2`; Vulkan 1.4.351) and Intel UHD (Mesa 26.2.4). No pen tablet.
- Toolchain and libraries as pinned in `docs/dependencies/manifest.yaml`; `python3 -I tests/docs/manifest_test.py --check-installed` passed on 2026-10-13. Present but not yet in the manifest: fontconfig, ICU 78.3 (needed from S2-8). `openimageio` is not installed and is not needed.
- Repository: `main` at `5d32326`, 86 tracked files, clean. Build trees exist under `build/` for all six presets. Stage 1 sessions will move all of this; trust the status document and the check scripts, not this list.

### 8.4 Steps

1. Entry check (S2-0.1): run `scripts/check.sh`, `scripts/check-ui.sh`, `scripts/check-sanitizers.sh` and every command listed in `docs/testing/acceptance.md`; confirm `git tag` shows `v0.1.0-alpha.1`; confirm `docs/samples/stage1-demo.archmage` validates. Apply the stop rule.
2. Walk §1.8: for every row, find the real header, type and command names; write the mapping table into a new "Stage 2 entry" section of `docs/implementation-status.md`, together with the Q2-01 to Q2-11 table and headings S2-0 to S2-12 marked "Not started".
3. Branch `s2-0-hierarchy`. Set `project(ArchMage VERSION 0.2.0)`. Write `docs/decisions/ADR-0031-stage2-scope.md` through `ADR-0044-schema-v1-freeze.md`, one per D-31 to D-44, under 60 lines each, `status: accepted` except `ADR-0043` (`provisional (gate: S1-6 GPU outcome)`) and `ADR-0044` (`provisional (gate: Q2-07)`). Update `CLAUDE.md`: plan path, status path, session protocol (§4.3), sub-milestones S2-0 to S2-12, the schema-minor rule, the DoD additions (§4.4). Keep it under 80 lines.
4. Tree (S2-0.3): add the `Group` object type, the ordered `child` relation, traversal helpers and the invariants in `core/document/`; extend the identifier type set per D-32; `[document]` tests including the cycle rejection and the 10,000-object timing.
5. Commands (S2-0.4): `archmage.group.create`, `archmage.group.ungroup`, `archmage.object.move`; make the Stage 1 `archmage.layer.*` commands accept group targets without renaming them; `[operation]` tests including undo, JSON round trip and `requestId` idempotency.
6. Compiler (S2-0.5): composite node over child lists; isolated groups with Normal and group opacity; cache keys over the child-set version; `[image]` tests with hit counters for invalidation scope.
7. Schema 0.2 (S2-0.6): group objects and `child` relations; migration 0.1 → 0.2; fixture in `tests/io/native/fixtures/`; acyclicity in `validate --deep`; unknown payloads inside groups preserved.
8. Panel (S2-0.7): `LayersTreeModel` and the `QTreeView`-based panel; UI tests and the `[perf]` record against the 100 ms budget.
9. Menu and shortcuts (S2-0.8); ledger children and the NAV-07 move (S2-0.9); status and corpus updates (S2-0.10).
10. Run the G2-0 checks (§8.5), record commands, outputs and artifact paths under the S2-0 heading, write the progress report (§9.2), commit per task, merge to `main`.

### 8.5 Verification commands (all must pass)

```
scripts/check.sh && scripts/check-ui.sh && scripts/check-sanitizers.sh
ctest --preset dev-debug -L "document|operation|image" --output-on-failure
ctest --preset asan-ubsan -L "document|operation|image" --output-on-failure
./build/dev-debug/apps/archmage-cli/archmage-cli --version          # ArchMage 0.2.0 (<sha>)
./build/dev-debug/apps/archmage-cli/archmage-cli validate --deep tests/io/native/fixtures/<0.2 fixture>.archmage
./build/dev-debug/apps/archmage-cli/archmage-cli validate --deep docs/samples/stage1-demo.archmage
python3 -I scripts/parity_report.py --check
python3 -I tests/docs/manifest_test.py --check-installed
QT_QPA_PLATFORM=wayland ./build/dev-debug/apps/archmage/archmage    # create, group two layers, ungroup, undo, save, reopen
git status --porcelain                                              # empty after the final commit
```

### 8.6 Do not

- Start S2-1 work: no blend modes, no pass-through groups, no masks, transforms, adjustments, shapes, text, sources, styles or compositions. Reserved fields stay reserved until their sub-milestone.
- Rename or renumber any Stage 1 command ID, ADR or decision; supersede through a new ADR if one must change.
- Vendor dependencies, add vcpkg, or add fontconfig and ICU yet (S2-8 adds them).
- Pull Qt into `core/`, `io/` or `archmage-cli`.
- Regenerate or hand-edit `docs/parity/report.md`; run the generator.
- Record anything in `docs/implementation-status.md` as more than "implemented" and "automatically tested" beyond what the tests and the manual Wayland run cover.
- Answer an open question on the user's behalf; apply the §7.3 default and say so.

### 8.7 Report back

The outcome of the stop rule; the §1.8 mapping table; the exact commands and outputs for §8.5; the commit list; the ADR list; any deviation from §3 with its reason; the state of Q2-01 to Q2-11 as understood by the session; the measured tree-panel p95.

## 9. Appendices

### 9.1 Stage 2 command inventory

Stage 1 commands (plan §8.1) remain with their IDs. Stage 2 adds:

- Hierarchy: `archmage.group.create`, `.ungroup`; `archmage.object.move`, `.setBlendMode`, `.setFillOpacity`, `.setBlendRanges`, `.setChannelMask`, `.setKnockout`, `.setClipped`, `.setTransform`, `.transform`, `.rasterizeTransform`, `.align`, `.distribute`, `.paste`; `archmage.document.setBlendPolicy`, `.setCanvasRect`, `.setResolution`, `.setGrid`, `.setUnits`.
- Masks and channels: `archmage.mask.add`, `.addParametric`, `.addVector`, `.remove`, `.setEnabled`, `.setInverted`, `.setDensity`, `.setFeather`, `.setLinked`, `.setParameters`, `.fromSelection`; `archmage.selection.fromMask`, `.fromChannel`; `archmage.channel.add`, `.remove`, `.duplicate`, `.rename`, `.loadIntoMask`.
- Painting: `archmage.raster.paintStroke` gains `target` and the `behind` mode; `archmage.raster.fillSelection` gains `target`.
- Guides: `archmage.guide.add`, `.move`, `.remove`, `.clear`, `.setLocked`.
- Consolidation: `archmage.layer.mergeDown`, `.mergeVisible`, `.stampVisible`, `.flatten`, `.rasterize`, `.trim`, `.convertBackground`, `.convertToBackground`.
- Adjustments: `archmage.adjustment.add`, `.remove`, `.setParameters`, `.setBypassed`, `.applyPreset`.
- Vectors and text: `archmage.shape.create`, `.setParameters`, `.setFill`, `.setStroke`; `archmage.text.create`, `.edit`, `.setStyle`, `.setParagraphStyle`, `.setFrame`.
- Sources: `archmage.source.placeEmbedded`, `.placeLinked`, `.duplicateInstance`, `.duplicateSource`, `.replaceContent`, `.convertToLinked`, `.convertToEmbedded`, `.relink`, `.relinkFolder`, `.update`, `.setUpdatePolicy`, `.commitNested`.
- Styles: `archmage.style.addEffect`, `.removeEffect`, `.setEffect`, `.setGlobalLight`, `.copy`, `.paste`, `.clear`, `.scale`, `.toLayers`, `.applyPreset`.
- Compositions and history: `archmage.comp.create`, `.update`, `.apply`, `.delete`, `.rename`, `.setCapture`; `archmage.history.snapshot`, `.deleteSnapshot`, `.restoreSnapshot`, `.switchBranch`.

Non-command services added: Place (file dialog and drop), Edit Contents (nested tab), Export Compositions, `archmage-cli capabilities`, `render --composition`.

### 9.2 Progress report template (spec §29.3)

```
## S2-n <name>, <date>
Implemented outcomes:
Changed modules:
Passing evidence (commands and result paths):
Manual validation (what, how, platform):
Performance changes (benchmark result file):
Migrations (schema minor, fixture path):
Dependency changes (manifest diff):
Open risks:
State by behavior: implemented | automatically tested | manually tested | verified against benchmark
Sample document: docs/samples/...
```

### 9.3 Schema minor plan

| Minor | Introduced by | Adds |
| --- | --- | --- |
| 0.2 | S2-0.6 | group objects, ordered `child` relation |
| 0.3 | S2-1.4 | blend mode, fill opacity, blend ranges, channel mask, knockout, document blend policy |
| 0.4 | S2-2.1, S2-2.6 | mask objects and `mask` relation, named channels, clipping flag |
| 0.5 | S2-3.1 | instance transforms (offset migration) |
| 0.6 | S2-4.1 | `canvasRect`, resolution and units, guides, grid, background flag |
| 0.7 | S2-5.1 | adjustment objects and `adjust` relation |
| 0.8 | S2-7.5 | shape objects, vector masks |
| 0.9 | S2-8.5 | text objects, font reference resources, cached appearance resources |
| 0.10 | S2-9.1 | source objects, `source` relation, link records |
| 0.11 | S2-10.1 | effect objects and `effect` relation, global light |
| 0.12 | S2-11.1, S2-11.5 | compositions, persisted snapshots |
| 1.0 | S2-12.2 | the freeze, if Q2-07 confirms it |

### 9.4 Spec sections used by this plan

§1.2 non-negotiables; §2.3 numeric inputs and controls; §3.2 and §3.3 ledger; §5.2 to §5.5 stack and layout; §6.1 to §6.4 document model, invariants and coordinates; §7.1 to §7.5 tiles, graph, interactive evaluation, alpha and GPU contract; §8.1 and §8.2 modes and ICC boundaries; §9.1 to §9.3 commands, history and jobs; §10.1 to §10.5 native format, saving, recovery and migration; §11 tool lifecycle and NAV-03; §12.1 coverage; §12.2 MSK; §13.1 to §13.4 layers, blends, embedded and linked objects, styles; §14.1 GEO; §15.1 and §15.3 adjustments and the filter contract; §16.1 `behind`; §17.1 and §17.2 text and vectors; §22.4 export; §23.1 resources; §25.1 to §25.4 budgets; §26.1 to §26.3 reliability and accessibility; §27.1 to §27.4 verification; §28 M2 (and M3 to M8 for deferrals); §29.2 and §29.3 working rules and reports; §30 risks; App. A.1 to A.4; App. B.
