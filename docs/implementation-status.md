# ArchMage implementation status

Living record of Stage 1 progress (plan §0 and §3.3; spec §29.3). Every session reads this first and
updates it last. Plan: `docs/plans/Stage1_Development_Plan.md`. Decisions: `docs/decisions/`.

State vocabulary (spec §29.3), strongest first: **verified against benchmark**, **manually tested**,
**automatically tested**, **implemented**. A behavior is listed only in a state its evidence supports.

## Current position

- Stage 1 = M0 + M1 (ADR-0001), split into sub-milestones S1-0 to S1-9 (plan §3.2).
- Completed: S1-0, S1-1 and S1-2 (see below). Next: S1-3, brush stroke vertical slice (gate G3).
- Before starting S1-3, re-run the G2 checks: `scripts/check.sh`, `scripts/check-ui.sh`,
  `scripts/check-sanitizers.sh`, and the ThreadSanitizer run of the operation, image and document tiers.
- Stage 2 (M2) is planned in `docs/plans/Stage2_Development_Plan.md`; it starts only after the M1 gate.

## Open questions (plan §6.3)

| ID | State on 2026-10-11 | Effect |
| --- | --- | --- |
| Q-01 | Answered by the user: Stage 1 is M0 + M1 | ADR-0001 accepted |
| Q-02 | Open | Interim policy of ADR-0025 applies; needed before the S1-9 manifest and ledger freeze |
| Q-03 | Open | Synthetic input only; no tablet detected |
| Q-04 | Open | Defaults apply (GIMP or Krita PSD, CC0 RAW sample); the Devanagari font is already installed |
| Q-05 | Open | Inert `.github/workflows/linux.yml`; no remote; `scripts/check.sh` is the gate |
| Q-06 | Open | D-04 default: system packages, vcpkg deferred |
| Q-07 | Open | D-13 default: schema major 0 until the M2 exit |
| Q-08 | Open | Ledger has spec-derived parent entries only (ADR-0030) |
| Q-09 | Open | D-21 default: Linux only |
| Q-10 | Open | Plan §1.3 interpretations apply |
| Q-11 | Answered by the user: install authorized | Packages installed in S1-0.1 (see below) |

## Issues raised during implementation

| ID | Issue | Raised | Needs |
| --- | --- | --- | --- |
| I-01 | D-02 fixes C++20, but D-23 requires `std::expected`, a C++23 feature that neither GCC 16.2.1 nor Clang (22.1.8 or 23.1.1) provides under `-std=c++20` (ADR-0002) | S1-0 | Closed 2026-10-13 by ADR-0101 under the user's "reasonable assumptions" direction: C++20 stays, engine code uses `archmage::base::Expected` |
| I-02 | The native Wayland platform plugin ships in `qt6-base` (6.11.2 and 6.12.0); `qt6-wayland` only adds the Qt6WaylandCompositor libraries and is not needed (ADR-0021) | S1-0 | Nothing; consider dropping `qt6-wayland` from the prerequisites |
| I-03 | `noto-fonts` already provides Noto Sans Devanagari; plan Q-04's font installation is unnecessary | S1-0 | Nothing |
| I-04 | The developer session exports `QT_QPA_PLATFORMTHEME=qt6ct`; on the offscreen platform it aborts Qt tests through GTK. Qt tests now unset it (`tests/CMakeLists.txt`) | S1-0 | Nothing |
| I-05 | The pacman sync database dated 2026-09-25 pointed at package versions the mirrors had dropped (404 on at.cachyos.org, cachyos.doridian.net, us-mnz.soulharsh007.dev); `mirror.bali0531.hu` redirected to `cdn.cachyos.org`, which answered with an HTML "Welcome to our server" page that pacman cached in place of `cmake`, `qt6-wayland` and `vulkan-validation-layers` and their signatures, so every retry failed. Resolved by the user: stub files removed, then `pacman -Syu --needed <packages>` | S1-0 | Nothing; if pacman reports signature errors again, look for HTML files in `/var/cache/pacman/pkg` |
| I-06 | That `-Syu` upgraded 352 packages: Clang 22.1.8 to 23.1.1, Qt 6.11.2 to 6.12.0, kernel 7.2.7 to 7.2.9 (still running 7.2.7). The loaded NVIDIA kernel module is 615.71.09 while `nvidia-utils` is 615.78.08 | S1-0 | Closed 2026-10-13: running kernel 7.2.9, NVIDIA module 615.78.08 matches `nvidia-utils` |
| I-07 | QVulkanWindow's frame synchronization raises VUID-vkQueueSubmit-pSignalSemaphores-00067 on every frame with validation layers 1.4.363, and QVulkanWindow offers no swapchain color space selection (the NVIDIA Wayland driver lists `PASS_THROUGH` first) | S1-1 | An ArchMage-owned Vulkan swapchain before the Vulkan adapter becomes the default (ADR-0011); no later than M4 |
| I-08 | Qt's QRhi Vulkan backend raises VUID-VkImageCreateInfo-initialLayout-12478 | S1-1 | Nothing while QRhiWidget stays experimental (ADR-0011) |
| I-09 | Raster canvas repaints are not paced to the display: 747 frames for 746 input events in 3 s on a 60 Hz output | S1-1 | Pace canvas repaints with `QWindow::requestUpdate` when the real canvas lands (S1-3, S1-7) |
| I-10 | Under XWayland the scale-1 tooltip probe was never shown (Qt reported it hidden), identically for all three surfaces; the scale-1.5 one was | S1-1 | Investigate with the real tooltips in S1-7 |
| I-11 | Hyprland 0.56 window-rule handles can only be disabled, not removed; the experiment driver's rules stay registered, disabled, until the next config reload | S1-1 | Nothing; they match only the `archmage-experiment` class |

## M0 deliverables and experiments (plan §1.2)

| Item | State | Evidence |
| --- | --- | --- |
| M0-D1 Clean project initialization | Done in S1-0 | Commit `0cdf8ce` on `main` |
| M0-D2 Toolchain and license manifest | Done in S1-0 | `docs/dependencies/manifest.yaml` |
| M0-D3 Feature ledger | Infrastructure done in S1-0; Stage 1 child entries due in S1-9 | `docs/parity/features.yaml`, `docs/parity/report.md` |
| M0-D4 Architecture decisions | Recorded in S1-0; D-11 provisional until G1 | `docs/decisions/ADR-0001` to `ADR-0030` |
| M0-D5 Canvas and input prototype | Done in S1-1 | Three `CanvasSurface` adapters, `InputMapper`, `ViewTransform`; ADR-0011 |
| M0-D6 Memory and format experiments | Partly done: tile memory accounting and eviction ordering in S1-2; container and probes pending (S1-4, S1-6) | `TilesTest` (uniform tiles allocate nothing, dense tiles charge RAM, coordinator eviction order) |
| M0-D7 Test corpus plan | Done in S1-0 | `docs/testing/corpus-plan.md` |
| M0-E1 Docking, GPU canvas, overlays, mixed DPI, tablet | Done in S1-1, tablet unavailable | ADR-0011 evidence matrix; `docs/decisions/evidence/S1-1/` |
| M0-E2 Tile compositing, blur halo, profile conversion, brush latency | CPU tile compositing done in S1-2; blur halo, profile conversion experiment and brush latency pending (S1-3, S1-6) | `EvaluatorTest`, `KernelsTest` |
| M0-E3 Native save, reopen, kill-during-save recovery | Not started (S1-4, S1-5) | |
| M0-E4 Layered PSD decode probe | Not started (S1-6) | |
| M0-E5 RAW file, shaped Arabic and Indic text, macOS GPU path | Not started (S1-6) | |
| M0-E6 Resource estimates on profiles A to D | Not started (S1-6) | |

## Known gaps (spec §29.2, plan §5.5)

Not verified in Stage 1, each to be closed or carried forward with reproduction instructions:
Windows and macOS builds (ADR-0021); reference profiles B and D; real tablet input (no tablet on the
development machine); HDR; display color accuracy (sRGB display assumed, ADR-0010); compositors other
than Hyprland; accessibility beyond keyboard reachability; mixed DPI across physical monitors and
real-cursor alignment (S1-1 used headless outputs and synthetic events, ADR-0011); GPU device loss.

## S1-0 Bootstrap and scaffolding: complete, 2026-10-11

Work on branch `s1-0-bootstrap` (`ff71b69` to `b447526`), merged to `main` in `b35b538`. Every check
below ran on `b35b538b418c` from a clean `build/` unless stated otherwise. This record was committed
afterwards and changes no code.

### Tasks and definitions of done

| Task | State | Evidence |
| --- | --- | --- |
| S1-0.1 Prerequisites | Done | `pacman -Q`: cmake 4.4.4-1.1, ninja 1.13.2-3.1, qt6-wayland 6.12.0-1.1, vulkan-headers 1:1.4.363.0-1, vulkan-validation-layers 1.4.363.0-1.1, nlohmann-json 3.12.0-2, cli11 2.7.2-1 (exit 0). `cmake --version` 4.4.4; `ninja --version` 1.13.2. `pacman -Qkk` on the ten new packages: 0 altered files. Versions pinned in the manifest. Installation needed a full upgrade (I-05, I-06) |
| S1-0.2 Repository | Done | `git log` shows `0cdf8ce Initialize ArchMage repository with specification and plan`; `git status --porcelain` is empty |
| S1-0.3 CMake and presets | Automatically tested | `cmake --preset dev-debug` and `cmake --preset asan-ubsan`: no warning, error or missing-package lines; all six presets configure (matrix below) |
| S1-0.4 Module skeleton | Automatically tested | `cmake --build --preset dev-debug` builds every target with 0 compiler warnings; `archmage-cli --version` prints `ArchMage 0.1.0 (b35b538b418c)` and exits 0, which the `cli.version` test also checks |
| S1-0.5 Test infrastructure | Automatically tested | `ctest --preset dev-debug --output-on-failure`: 5/5 pass (labels: document 3, interchange 1, ui 1); `scripts/check-ui.sh` runs the ui tier with `DISPLAY` and `WAYLAND_DISPLAY` unset: 1/1 pass |
| S1-0.6 Desktop window | Manually tested (scripted, Hyprland 0.56.2) | `QT_QPA_PLATFORM=wayland`: `hyprctl clients -j` lists class `archmage`, title `ArchMage`, `xwayland: false`. `QT_QPA_PLATFORM=xcb`: class `ArchMage`, `xwayland: true`. Each closed with `hyprctl dispatch 'hl.dsp.window.close({ window = "pid:<pid>" })'` and exited with code 0 |
| S1-0.7 Check scripts | Automatically tested | `scripts/check.sh`, `scripts/check-ui.sh` and `scripts/check-sanitizers.sh` exit 0 from a clean `build/` and from a fresh clone of `b35b538` (86 tracked files); each prints the commands it runs |
| S1-0.8 Dependency manifest | Automatically tested | `python3 -I tests/docs/manifest_test.py`: "33 entries cover 21 CMake lookups"; `--check-installed`: pinned versions match the installed packages. A copy with one entry removed and a bogus lookup added fails with both errors (exit 1) |
| S1-0.9 Documentation scaffolds | Automatically tested | `python3 -I scripts/parity_report.py --check`: "ledger valid (121 entries); report up to date". Ledgers with altered row text, a missing row, invalid child fields or an unbacked `verified` state each fail (exit 1). ADR-0001 to ADR-0030 (each under 60 lines), overview, corpus plan, ledger README and this file exist |
| S1-0.10 Project CLAUDE.md | Done | 48 lines |
| S1-0.11 CI stand-in | Done, inert | `.github/workflows/linux.yml` parses (PyYAML, 5 steps); `actionlint` is not installed, so it was not run |

### Exit gate G0

Met: the three check scripts pass from a fresh clone; the empty window opens under Wayland and X11;
the documentation scaffolds and the manifest exist; the work is merged on `main`.

### Presets on `b35b538`

| Preset | Configure | Build | Tests |
| --- | --- | --- | --- |
| `dev-debug` | 0 warnings | 0 compiler warnings | 5/5 pass |
| `dev-release` | 0 warnings | 0 compiler warnings | 5/5 pass |
| `asan-ubsan` | 0 warnings | 0 compiler warnings | 5/5 pass; every translation unit carries `-fsanitize=address,undefined`, every binary links the ASan and UBSan runtimes |
| `tsan` | 0 warnings | 0 compiler warnings | 4/4 non-UI tests pass (the plan runs tsan on non-UI tiers) |
| `fuzz` | 0 warnings | 0 compiler warnings | Not run: no fuzz targets until S1-5.8 |
| `ci` | 0 warnings | 0 compiler warnings with `-Werror` | 5/5 pass |

Static checks: every C++ source passes `clang-format --dry-run --Werror`; `clang-tidy -p build/asan-ubsan`
over the six sources processes all of them with no findings in project code.

### Deviations from the plan

1. `archmage_io` is an INTERFACE aggregate, not an empty static library, and source-less module targets
   are INTERFACE libraries until they get code (ADR-0006).
2. `cmake/Dependencies.cmake` also finds xxHash (needed by D-19) and uses LibRaw's thread-safe
   `libraw_r` variant.
3. `Version.h` is regenerated before every build and the revision gets a `-dirty` suffix when tracked
   files differ from the commit.
4. The `tsan` and `fuzz` presets use the Debug build type, which the plan left open (ADR-0003).
5. C++20 module scanning is off (`CMAKE_CXX_SCAN_FOR_MODULES OFF`, ADR-0003).
6. The manifest records a checksum and build options for every entry (spec §5.3 requires them; the
   plan's S1-0.8 row did not list them), and `manifest_test.py` gained `--check-installed`.
7. `scripts/check.sh` also runs the manifest test and the ledger check.
8. A third test, `cli.version`, checks the CLI banner and exit code as a subprocess.
9. Qt tests unset `QT_QPA_PLATFORMTHEME` and `QT_STYLE_OVERRIDE` (I-04).
10. `ARCHMAGE_FAULT_INJECTION` (D-27) is left to S1-5, when the first fault point exists (ADR-0027).
11. Ledger entries add the optional `spec_section` and `milestone_basis` fields; parent entries keep
    `scope: null` until the benchmark audit; milestones that §28 does not state are marked `inferred:`.
12. `.clang-format` also sets `AccessModifierOffset: -4` to match the 4-space indent.
13. The toolchain moved with the system upgrade: Clang 23.1.1 instead of the planned 22.1, Qt 6.12.0
    instead of 6.11.2.

### Progress report (plan §8.2)

#### S1-0 Bootstrap and scaffolding, 2026-10-11

- **Implemented outcomes:** reproducible C++20, Qt 6, CMake and Ninja build with six presets; Qt-free core skeleton with a generated version header; headless `archmage-cli --version`; empty desktop window under Wayland and X11; labeled CTest tiers with an offscreen UI tier; three check scripts; dependency manifest with a coverage test; ADR-0001 to ADR-0030; feature ledger with validator and generated report; architecture overview; test corpus plan; project `CLAUDE.md`; inert CI workflow.
- **Changed modules:** new code in `core/document` (`Version`), `ui` (`MainWindow`), `apps/archmage` and `apps/archmage-cli`; source-less targets for `core/{commands,tiles,render,color,brush}` and `io/{native,formats}`.
- **Passing evidence (commands and result paths):** the plan §7.4 list above, run on `b35b538b418c`; full logs were written to `/tmp/archmage-scratch/s1-0-final/` and are not kept in the repository.
- **Manual validation (what, how, platform):** window open and close under native Wayland and XWayland on Hyprland 0.56.2, driven by `hyprctl`.
- **Performance changes (benchmark result file):** none; benchmarks start in S1-3.7.
- **Migrations:** none; the native format starts in S1-4.
- **Dependency changes (manifest diff):** first manifest, 33 entries; pins taken after the 2026-10-11 upgrade.
- **Open risks:** I-01 (C++20 versus `std::expected`) needs a decision before S1-2; I-06 reboot before S1-1; rolling-release drift, which `manifest_test.py --check-installed` detects.
- **State by behavior:** `archmage-cli --version` automatically tested; offscreen main window automatically tested; main window on Wayland and X11 manually tested; check scripts automatically tested; nothing is verified against a benchmark.
- **Sample document:** none; samples start in S1-4.

### Notes for the next session

- Reboot first (I-06); Vulkan on the NVIDIA GPU is unreliable until the kernel module matches the
  userspace driver.
- Hyprland 0.56 takes Lua dispatchers: close a test window with
  `hyprctl dispatch 'hl.dsp.window.close({ window = "pid:<pid>" })'`.
- Run clang-tidy against a Clang compile database (`-p build/asan-ubsan`); GCC databases carry Qt's
  GCC-only `-mno-direct-extern-access`.
- The agent's shell is zsh, where an unquoted `$VAR` does not split into words; put multi-step shell
  logic in bash scripts.
- The manifest was generated once from pacman metadata by a script that is not in the repository;
  edit entries by hand and confirm with `python3 -I tests/docs/manifest_test.py --check-installed`.

## S1-1 Presentation and input feasibility: complete, 2026-10-13

Work on branch `s1-1-canvas-feasibility` (`2658358`, `cc58562`, `9c05c1b`; `697291f` added the Stage 2
plan), merged to `main` in `629c913`. The G0 checks passed on `5d32326` before work began (same day,
after the I-06 reboot).

### Tasks and definitions of done

| Task | State | Evidence |
| --- | --- | --- |
| S1-1.1 Canvas surfaces | Automatically tested (raster), manually tested by script (all three) | `ui/canvas/`: `RasterCanvasSurface`, `RhiCanvasSurface`, `VulkanCanvasSurface` behind `CanvasSurface`; each displays the synthetic tile grid. `CanvasSurfaceTest` checks the raster frame equals the source rendering. The matrix found every adapter's presented frame pixel-exact on Wayland and XWayland (ADR-0011) |
| S1-1.2 Experiment harness | Manually tested by script | `archmage --experiment=canvas:<raster\|rhi\|vulkan>` with menus, docked and floating docks, tool bar, status bar, popup and tooltip probes; ran for each adapter under `wayland` and `xcb` (`docs/decisions/evidence/S1-1/<platform>-<kind>/log.txt`) |
| S1-1.3 Evidence matrix | Done | ADR-0011 matrix; crops, frames, `result.json` and `summary.json` under `docs/decisions/evidence/S1-1/` |
| S1-1.4 Input pipeline | Automatically tested | `ui/input/PointerSample.h`, `InputMapper`; `InputMapperTest` (10 cases: mouse, tablet pressure and tilt, eraser, wheel zoom, touchpad pan, Shift and Ctrl wheel, pinch). No tablet available (ADR-0011) |
| S1-1.5 ViewTransform | Automatically tested | `core/render/ViewTransform`; `ViewTransformTest` 11 `[math]` cases: exact round trips at integer zoom, within 1e-9 over 100,000 random views |
| S1-1.6 Latency instrumentation | Manually tested by script | `ARCHMAGE_TRACE=latency` prints `LATENCY` lines every second; synthetic 250 Hz drags measured for each adapter (ADR-0011 table) |
| S1-1.7 Decision | Done | ADR-0011 accepted; plan D-11 points to it; `CanvasSurface::defaultKind()` implements it |

### Exit gate G1

Met: ADR-0011 names the adapter with its evidence matrix, the raster surface works as safe mode (and is
the Stage 1 default), the input pipeline tests pass, and D-11 in the plan points to the ADR.

### Commands and results (on the merged tree, 2026-10-13)

| Command | Result |
| --- | --- |
| `scripts/check.sh` | exit 0; 15/15 non-UI tests; manifest "38 entries cover 21 CMake lookups"; ledger "121 entries; report up to date" |
| `scripts/check-ui.sh` | exit 0; 3/3 (`SmokeTest`, `InputMapperTest`, `CanvasSurfaceTest`) |
| `scripts/check-sanitizers.sh` | exit 0; 18/18 under Clang ASan and UBSan; 0 compiler warnings |
| `cmake --preset ci && cmake --build --preset ci && ctest --preset ci` | 0 warnings with `-Werror`; 18/18 |
| `python3 -I tests/docs/manifest_test.py --check-installed` | pinned versions match the installed packages |
| `clang-format --dry-run --Werror` on the 27 new or changed C++ files | clean |
| Clean clone of `2658358`: configure, build, `ctest --preset dev-debug` | 16/16 |
| `python3 -I scripts/experiments/canvas_matrix.py` (all six runs), then `--platforms xcb` after fixing the X11 coordinate mapping | every presented frame pixel-exact; results in `docs/decisions/evidence/S1-1/summary.json`; user focus, cursor and outputs restored after each run |

### Deviations from the plan

1. Mixed DPI used two temporary headless Hyprland outputs (scale 1 and 1.5) instead of rescaling a
   physical monitor, and windows moved between them rather than across the three physical monitors. This
   left the user's displays untouched; physical mixed-DPI remains a known gap.
2. Evidence screenshots are cropped to the experiment window; full captures (which show the user's bar
   and wallpaper) are not kept.
3. The synthetic drag runs at 250 Hz (a 4 ms timer), not 240 Hz.
4. QRhiWidget was measured with its Vulkan backend; `ARCHMAGE_RHI_API=opengl` exists but was not measured.
5. QRhi needs `Qt6::GuiPrivate`; `QT_NO_PRIVATE_MODULE_WARNING` silences Qt's configure reminder, and the
   RHI path's validation uses Qt's private `QVulkanDefaultInstance` flag.
6. `core/tiles/include/archmage/tiles/Geometry.h` arrived in S1-1 because `ViewTransform` needs it;
   S1-2.2 adds checked arithmetic.
7. `cmake/ArchMageShaders.cmake` compiles GLSL to SPIR-V headers at build time (S1-6 reuses it).
8. The surfaces are named `RasterCanvasSurface`, `RhiCanvasSurface` and `VulkanCanvasSurface` (the plan
   said `...WidgetSurface` and `VulkanWindowSurface`).
9. Raster, not a GPU adapter, is the Stage 1 default (ADR-0011).
10. Experiment windows use the application identity `archmage-experiment` so window rules can target them.

### Progress report (plan §8.2)

#### S1-1 Presentation and input feasibility, 2026-10-13

- **Implemented outcomes:** three canvas presentation adapters behind one interface, with an
  evidence-based default (raster); view transform with device, logical and document mappings; input
  mapping for mouse, tablet, wheel and pinch; latency tracing; a scripted canvas experiment and an
  automated Hyprland evidence driver.
- **Changed modules:** `core/tiles` (geometry header), `core/render` (`ViewTransform`), `ui` (`canvas/`,
  `input/`, `experiments/`, shaders), `apps/archmage` (`--experiment`), `cmake` (shader helper, Qt
  GuiPrivate and ShaderTools components), `scripts/experiments`.
- **Passing evidence (commands and result paths):** the table above; matrix results in
  `docs/decisions/evidence/S1-1/`.
- **Manual validation (what, how, platform):** the six adapter and platform combinations, scripted on
  Hyprland 0.56.2 with headless outputs and checked against grim captures; no interactive session with a
  person at the canvas yet.
- **Performance changes (benchmark result file):** none in `benchmarks/results/` (benchmarks start in
  S1-3.7). Event-to-submission latencies for each adapter are in ADR-0011.
- **Migrations:** none.
- **Dependency changes (manifest diff):** added `qt6-shadertools`, `grim`, `python-numpy`,
  `python-pillow`, `hyprland` (33 to 38 entries); `find_package(Qt6)` also requests `GuiPrivate` and
  `ShaderTools`.
- **Open risks:** I-01 must be settled before S1-2; I-07 (QVulkanWindow synchronization and color space)
  blocks making the Vulkan adapter the default; I-09 repaint pacing.
- **State by behavior:** view transform and input mapping automatically tested; raster canvas
  presentation automatically tested; all adapters manually tested by script on Wayland and XWayland;
  nothing verified against a benchmark.
- **Sample document:** none; samples start in S1-4.

### Notes for the next session

- Run experiments on the `dev-release` build: the Debug synthetic frame takes about 9 ms to render.
- In the agent's zsh, `$var:...` applies history modifiers (`$g:set_enabled` became garbage); write
  `${var}` before a colon.
- The matrix takes about 5 minutes and briefly focuses experiment windows on headless outputs; it restores
  the active workspace and cursor afterwards.

## S1-2 Core foundation (headless): complete, 2026-10-13

Work on branch `s1-2-core-foundation` (`d6346d3`, `c395249`, `65096ec`, `7c842ff`, `65b45d4`, `bec858d`,
`d76c380`), merged to `main` in `c25613e`. The G1 checks passed on `b34a634` before work began.

### Tasks and definitions of done

| Task | State | Evidence |
| --- | --- | --- |
| S1-2.1 Identifiers | Automatically tested | `core/base/Id.h`: 128-bit typed IDs from `getrandom`, `<prefix>-<26 Crockford>`; `IdTest`: round trip of 10,000 IDs, no collision over 1,000,000 draws, malformed text rejected |
| S1-2.2 Tiles and tile maps | Automatically tested | `core/tiles`: `Tile`, `TileBuffer`, `TileMap`, `DirtyRegion`; `TilesTest`: an empty 300,000² map allocates no tiles, uniform compaction, copy-on-write (a modified copy leaves the source unchanged), overflow rejected in `denseBytes`, every finite half-float round trips |
| S1-2.3 Resource coordinator | Automatically tested | `ResourceCoordinator`: disposable evicted before spoolable, memory without an evictor never reclaimed, reserved pool for recovery paths |
| S1-2.4 Document model | Automatically tested | `core/document/Document.h`; `DocumentTest`: invariants, instances sharing content, structural sharing between snapshots; the 10,000-layer snapshot and edit takes 0.03 s against the 1 s budget |
| S1-2.5 Commands, executor, history, transactions | Automatically tested | `CommandEnvelope`, `CommandRegistry`, `MutationExecutor`; `ExecutorTest`: a repeated `requestId` applies once, undo restores the identical snapshot, the clean marker follows undo past the save point, coalescing, provisional transactions with rebase, 100 concurrent submissions serialize |
| S1-2.6 Layer commands | Automatically tested | `archmage.document.create` and nine `archmage.layer.*` commands; `CommandsTest`: validation (locked layers, unknown targets, bad parameters), results, JSON round trip of every envelope |
| S1-2.7 Graph and CPU evaluator | Automatically tested | `GraphNode` contract, `compileGraph`, `CpuEvaluator`; `EvaluatorTest`: a three-layer document equals an independently coded reference byte for byte, cancellation stops within one tile per worker, an unchanged revision is served entirely from the cache |
| S1-2.8 Source-over kernel | Automatically tested | `KernelsTest`: opaque over opaque, 50 % over transparent, zero alpha contributes nothing, NaN sanitized and counted, negative color kept; `EvaluatorTest`: uint8 round trips exactly for opaque pixels |
| S1-2.9 Color skeleton | Automatically tested | `ColorProfile`, `ColorPolicy`, `ColorPipeline`; `ColorTest`: profile parse, the generated sRGB hash pinned (`de05479f…`, stable across processes), identity pipeline leaves samples untouched, linear blending rejected |
| S1-2.10 Jobs | Automatically tested | `JobScheduler`; `ExecutorTest`: a job finishing after its target layer was deleted or moved becomes a candidate and nothing is applied |
| S1-2.11 Structured errors | Automatically tested | `core/base/Error.h`; every category formats (`ExpectedTest`); used by every S1-2 API |

### Exit gate G2

Met: `ctest -L "math|operation|document|image"` is green under `dev-debug` and `asan-ubsan`, the
architecture overview names the real types, and the 10,000-layer snapshot runs in well under 1 s.

### Commands and results (on `c25613e`, 2026-10-13)

| Command | Result |
| --- | --- |
| `ctest --preset dev-debug -L "math\|operation\|document\|image"` | 96/96 |
| `ctest --preset asan-ubsan -L "math\|operation\|document\|image"` | 96/96 |
| `scripts/check.sh` | exit 0; 97/97 non-UI tests; manifest and ledger checks pass |
| `scripts/check-ui.sh` | exit 0; 3/3 |
| `scripts/check-sanitizers.sh` | exit 0; 100/100; 0 compiler warnings |
| `cmake --preset tsan && cmake --build --preset tsan && ctest --preset tsan -L "operation\|image\|document"` | 43/43, no ThreadSanitizer reports, 0 compiler warnings |
| `cmake --preset ci && cmake --build --preset ci && ctest --preset ci` | 0 warnings with `-Werror`; 100/100 |
| `clang-format --dry-run --Werror` on every new or changed C++ file | clean |

### Deviations from the plan

1. A new `core/base` module holds identifiers, errors, geometry, checked arithmetic, random bytes and
   SHA-256 (ADR-0102), instead of `core/document`; `Geometry.h` moved there from `core/tiles`.
2. `archmage::base::Expected` replaces `std::expected` under C++20 (ADR-0101, closes I-01).
3. Error categories add `validation` and `conflict` to the spec §26.2 list; commands need both (App. A.2).
4. SHA-256 arrived in S1-2 rather than S1-4.2: the pinned sRGB hash (S1-2.9) needs it.
5. `TileMap` copies on write per 16 × 16-tile chunk and carries a content generation number, which render
   cache keys use instead of addresses.
6. Coalescing uses the App. A.2 transaction field: commands sharing an implicit transaction ID merge into
   one history entry; explicitly begun transactions stay provisional and rebase by replay on commit.
7. `MutationExecutor::executeIf` checks a precondition atomically with the commit; jobs use it.
8. Document creation is `createDocument()` under the command ID `archmage.document.create`; it has no prior
   state, so it is not applied through the executor.
9. Locks: rename, visibility and reordering are allowed on a locked layer; opacity, offset and removal are
   rejected.
10. Alpha is clamped to [0, 1] when compositing (coverage); color samples keep out-of-range float values
    (D-09).
11. ThreadSanitizer already ran in S1-2 (the plan starts it in S1-5) because the executor, jobs and
    evaluator are threaded.

### Progress report (plan §8.2)

#### S1-2 Core foundation (headless), 2026-10-13

- **Implemented outcomes:** typed identifiers, structured errors and `Expected`; sparse copy-on-write
  tiles with memory accounting; the immutable document model with revisions; commands with an
  idempotent serialized executor, exact undo, a clean save point, coalescing and preview transactions;
  jobs with stale-result candidates; ICC profiles and the named color pipeline; the CPU evaluator with
  the spec §7.4 source-over kernel, cancellation and a tile cache.
- **Changed modules:** new `core/base`; `core/tiles`, `core/document`, `core/commands`, `core/render`,
  `core/color` gained their first real code; `core/brush` is still empty.
- **Passing evidence (commands and result paths):** the table above.
- **Manual validation (what, how, platform):** none; S1-2 is headless.
- **Performance changes (benchmark result file):** none recorded; the 10,000-layer snapshot sanity check
  took 0.03 s.
- **Migrations:** none.
- **Dependency changes (manifest diff):** none; OpenSSL, xxHash, Little CMS and nlohmann-json, already in
  the manifest, are now linked.
- **Open risks:** the evaluator's thread pool must not be re-entered from its own workers (documented);
  history keeps whole states, so memory grows with edits until S1-5's history budget enforcement.
- **State by behavior:** every S1-2 behavior is automatically tested; nothing is manually tested or
  verified against a benchmark.
- **Sample document:** none; samples start in S1-4.

### Notes for the next session

- `CpuEvaluator::render` must not be called from one of its own worker threads (it waits on a latch).
- Executor observers run on the executor thread; UI code must marshal to the GUI thread.

## S1-3 to S1-9

Not started.
