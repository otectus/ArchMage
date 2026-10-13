# ArchMage implementation status

Living record of Stage 1 progress (plan §0 and §3.3; spec §29.3). Every session reads this first and
updates it last. Plan: `docs/plans/Stage1_Development_Plan.md`. Decisions: `docs/decisions/`.

State vocabulary (spec §29.3), strongest first: **verified against benchmark**, **manually tested**,
**automatically tested**, **implemented**. A behavior is listed only in a state its evidence supports.

## Current position

- Stage 1 = M0 + M1 (ADR-0001), split into sub-milestones S1-0 to S1-9 (plan §3.2).
- Completed: S1-0 to S1-7 (see below). Two items are outstanding, and both wait on the machine, not on code:
  - S1-6 owes the profile A and C benchmark records (S1-6.8), which need an idle machine;
  - S1-7 owes the scripted native-Wayland run of the UI tests (G7), which needs a desktop nobody is using.
- Next: S1-8, shell: selections, eraser, image I/O, CLI render, recovery UI (gate G8).
- Before starting S1-8, re-run the G7 checks: `scripts/check.sh`, `scripts/check-ui.sh`,
  `scripts/check-sanitizers.sh`, the ThreadSanitizer run of the operation, image, document and resilience
  tiers, and the `ci` build.
- Pending record: `python3 -I scripts/bench_record.py --profile A --wait-idle 5`, then the same with
  `--profile C`, from a `dev-release` build of a clean tree; commit the results and fill in M0-E6 and the
  S1-6.8 row below.
- Pending run: each UI test program through `python3 -I scripts/experiments/wayland_run.py --focus --class
  '^<name>$' -- build/dev-debug/tests/<name>`, while nobody uses the desktop; record the results under S1-7's G7.
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
| I-09 | Raster canvas repaints are not paced to the display: 747 frames for 746 input events in 3 s on a 60 Hz output | S1-1 | Pace canvas repaints to the display. Not done in S1-3 or S1-7; S1-8's animated selection outline needs it, so it lands there |
| I-10 | Under XWayland the scale-1 tooltip probe was never shown (Qt reported it hidden), identically for all three surfaces; the scale-1.5 one was | S1-1 | Not investigated in S1-7, whose tooltips are tested offscreen only; check under XWayland in the S1-9 demonstration |
| I-11 | Hyprland 0.56 window-rule handles can only be disabled, not removed; the experiment driver's rules stay registered, disabled, until the next config reload | S1-1 | Nothing; they match only the `archmage-experiment` class |

## M0 deliverables and experiments (plan §1.2)

| Item | State | Evidence |
| --- | --- | --- |
| M0-D1 Clean project initialization | Done in S1-0 | Commit `0cdf8ce` on `main` |
| M0-D2 Toolchain and license manifest | Done in S1-0 | `docs/dependencies/manifest.yaml` |
| M0-D3 Feature ledger | Infrastructure done in S1-0; Stage 1 child entries due in S1-9 | `docs/parity/features.yaml`, `docs/parity/report.md` |
| M0-D4 Architecture decisions | Recorded in S1-0; D-11 settled at G1; evidence added in S1-6 (ADR-0009, ADR-0010, ADR-0012); implementation decisions ADR-0101 to ADR-0104 | `docs/decisions/` |
| M0-D5 Canvas and input prototype | Done in S1-1 | Three `CanvasSurface` adapters, `InputMapper`, `ViewTransform`; ADR-0011 |
| M0-D6 Memory and format experiments | Done: tile memory accounting and eviction ordering in S1-2; the `.archmage` container in S1-4; spooling in S1-5; PSD, RAW and text probes in S1-6 | `TilesTest`; `docs/architecture/native-format.md`, `NativeFormatTest`; `RecoverySpool` test; `docs/decisions/evidence/S1-6/` |
| M0-D7 Test corpus plan | Done in S1-0 | `docs/testing/corpus-plan.md` |
| M0-E1 Docking, GPU canvas, overlays, mixed DPI, tablet | Done in S1-1, tablet unavailable | ADR-0011 evidence matrix; `docs/decisions/evidence/S1-1/` |
| M0-E2 Tile compositing, blur halo, profile conversion, brush latency | Done: CPU compositing (S1-2), brush latency (S1-3, synthetic input), GPU compositing equal to the CPU, blur halo without seams, profile conversion (S1-6) | `EvaluatorTest`, `KernelsTest`, `GpuTest`, `BlurTest`, `ColorExperimentTest`; `benchmarks/results/2026-10-13-brush.json`; `docs/decisions/evidence/S1-6/color.md`; ADR-0012 |
| M0-E3 Native save, reopen, kill-during-save recovery | Done: save and reopen in S1-4; kill during save and journal, and recovery, in S1-5 | `At001Test`, `KillDuringSaveTest` (AT-013), `RecoveryTest` |
| M0-E4 Layered PSD decode probe | Done in S1-6: one GIMP-written fixture; 2 of 7 records rendered, 13 unsupported records listed | `docs/decisions/evidence/S1-6/psd.md` |
| M0-E5 RAW file, shaped Arabic and Indic text, macOS GPU path | RAW and text done in S1-6; macOS GPU path unavailable (no Apple machine), with reproduction instructions below | `docs/decisions/evidence/S1-6/raw.md`, `text.md`; "Unavailable platforms and devices" below |
| M0-E6 Resource estimates on profiles A to D | Not started (S1-6) | |

## Known gaps (spec §29.2, plan §5.5)

Not verified in Stage 1, each to be closed or carried forward with reproduction instructions:
Windows and macOS builds (ADR-0021); reference profiles B and D; real tablet input (no tablet on the
development machine); HDR; display color accuracy (sRGB display assumed, ADR-0010); compositors other
than Hyprland; accessibility beyond keyboard reachability; mixed DPI across physical monitors and
real-cursor alignment (S1-1 used headless outputs and synthetic events, ADR-0011); GPU device loss;
saving on vfat, exfat and network filesystems (the backup fallback is tested by overriding the detected
filesystem, not on such a mount); refusing a network filesystem for the recovery workspace (tested only
through the filesystem type table); power loss (interruptions are simulated with `_exit` at fault points).

## Unavailable platforms and devices (spec §29.2)

What could not be tested, with what would be run. Nothing here is claimed to work.

### macOS GPU path (M0-E5, plan S1-6.9)

Unavailable: no Apple machine exists here, and Stage 1 builds for Linux only (ADR-0021). The code has these
gaps that a Mac would hit first; all four need closing before the steps below can succeed:

1. `core/render/gpu/src/GpuContext.cpp` does not enable `VK_KHR_portability_enumeration` or set
   `VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR`. Current Vulkan loaders do not report portability
   drivers such as MoltenVK without both, so creation fails with `gpu.unavailable` and every GPU test skips.
   The device must also enable `VK_KHR_portability_subset` whenever the device lists it.
2. `io/native/src/Platform.cpp` has only stubs off Linux, so saving fails with `provider_unavailable`.
3. `core/base/src/Random.cpp` falls back to `std::random_device`; use `arc4random_buf` before identifiers are
   generated on macOS (D-16).
4. `core/tiles/src/ResourceCoordinator.cpp` assumes 16 GiB of RAM off Linux; use `sysctl hw.memsize`.

What would be run on an Apple Silicon Mac. These steps are untested, including the Homebrew package names and
the compiler choice (Apple Clang has not been tried):

```
xcode-select --install
brew install cmake ninja llvm qt sqlite zstd openssl@3 little-cms2 libpng jpeg-turbo harfbuzz freetype \
    libraw xxhash nlohmann-json cli11 fmt spdlog googletest python \
    vulkan-headers vulkan-loader molten-vk glslang vulkan-validationlayers
cmake -S . -B build/macos -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_COMPILER="$(brew --prefix llvm)/bin/clang++" -DCMAKE_PREFIX_PATH="$(brew --prefix qt);$(brew --prefix)"
cmake --build build/macos --target GpuTest bench_main
export VK_ICD_FILENAMES="$(brew --prefix molten-vk)/etc/vulkan/icd.d/MoltenVK_icd.json"
ctest --test-dir build/macos -R Gpu --output-on-failure
QT_QPA_PLATFORM=offscreen build/macos/benchmarks/bench_main --profile D
```

Evidence to collect:
- `GpuTest` passing: composites equal to the CPU's, and no validation errors;
- the `composite_gpu` entry of the `bench_main` output;
- macOS and Xcode versions, the MoltenVK version, and the GPU core count and unified memory size (spec §25.1).

### Reference profiles B and D (M0-E6, plan S1-6.8)

Unavailable: there is no 16 GiB machine with only integrated graphics (B) and no Apple Silicon machine (D).
`benchmarks/results/2026-10-13-bench-B.json` and `-D.json` record this, with the commands to run on such
machines: `cmake --preset dev-release && cmake --build --preset dev-release`, then
`python3 -I scripts/bench_record.py --profile B` (or `D`, after the macOS gaps above are closed).

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

## S1-3 Brush stroke vertical slice: complete, 2026-10-13

Work on branch `s1-3-brush-slice` (`d835689`, `230f465`, `963cc5e`, `b175b31`), merged to `main` in
`54c971f`. The G2 checks passed on `337cb92` before work began.

### Tasks and definitions of done

| Task | State | Evidence |
| --- | --- | --- |
| S1-3.1 Stroke input and resampling | Automatically tested | `core/brush/Stroke.h` `StrokeResampler`; `BrushTest`: dab positions identical for one-by-one, uneven and whole-stroke batching; spacing follows the diameter; zero pressure still advances |
| S1-3.2 Round dab | Automatically tested | `dabCoverage`, `dabBounds`; `BrushTest`: bounds, four-way symmetry, hardness extremes, coverage monotonic in diameter, sub-pixel dabs scaled by area |
| S1-3.3 paintStroke command | Automatically tested | `archmage.raster.paintStroke`; `PaintStrokeTest`: paint, undo and redo byte-exact; one history item per stroke; locked layer rejected; hidden layer allowed with a `layer.hidden` warning; flat opacity where dabs overlap; erase keeps the stored color |
| S1-3.4 Preview transaction | Automatically tested | `StrokeSession`; `PaintStrokeTest`: the preview equals the committed stroke tile for tile; cancelling leaves the revision unchanged and allocates no committed tile |
| S1-3.5 Canvas wiring | Automatically tested (offscreen and native Wayland) | `DocumentSession`, `DocumentFrameSource`, `Tool`, `BrushTool`, `DocumentView`, interim `MainWindow`; `BrushSliceTest`: a drag through the real canvas widget shows a preview, commits one "Brush Stroke" history item, puts black pixels in the presented frame and undoes; Escape cancels; a locked layer reports an error; focus loss commits. Passes offscreen (`scripts/check-ui.sh`) and on native Wayland (`python3 -I scripts/experiments/wayland_run.py --class '^BrushSliceTest$' -- ./build/dev-debug/tests/BrushSliceTest`: 6 passed). A display-cache eviction test covers shared content keys and eviction within one frame |
| S1-3.6 AT-002 | Automatically tested | `PaintStrokeTest` `AT002.StrokeUndoRedoAtTileBoundaries`: a stroke across four tile corners on a layer at (−37, 59), ten undo/redo cycles byte-exact against the pre- and post-stroke composites, bit-for-bit replay from the recorded envelope (no seed), and every pixel within two pixels of a tile border equal to an untiled rasterization (more than 10,000 pixels checked); passes under `dev-debug` and `asan-ubsan` |
| S1-3.7 Latency measurement | Recorded | `benchmarks/results/2026-10-13-brush.json` (built at `963cc5e`): p50 2.49 ms, p95 3.19 ms, max 12.78 ms over 2,480 frames of synthetic tablet input on a 4096² RGBA8 canvas, offscreen; pass against p95 ≤ 25 ms |

### Exit gate G3

Met, with one substitution: AT-002 passes under `dev-debug` and `asan-ubsan`, and the latency record is
committed. The plan's manual stroke with undo and redo on the real canvas was not done by a person;
`BrushSliceTest` drives the same canvas widget with synthetic events on native Wayland instead. A hand-drawn
stroke is part of the S1-9 manual demonstration.

### Commands and results (on `54c971f`, 2026-10-13)

| Command | Result |
| --- | --- |
| `scripts/check.sh` | exit 0; 113/113 non-UI tests; manifest and ledger checks pass |
| `scripts/check-ui.sh` | exit 0; 4/4 |
| `scripts/check-sanitizers.sh` | exit 0; 117/117 including AT-002; 0 compiler warnings |
| `cmake --build --preset tsan && ctest --preset tsan -L "operation\|image\|document"` | 51/51, no ThreadSanitizer reports |
| `python3 -I scripts/experiments/wayland_run.py --class '^BrushSliceTest$' -- ./build/dev-debug/tests/BrushSliceTest` | 6 passed on native Wayland; workspace and cursor restored |
| `python3 -I scripts/benchmarks/brush_latency.py --seconds 10` (three runs before the record, one for it) | p95 3.07 to 3.21 ms; no failure |

### Deviations from the plan

1. Render cache keys became per region (`GraphNode::cacheKey(region)`, built from source tile versions)
   so a stroke re-renders only the tiles it touches; the canvas caches 8-bit display tiles by content key.
2. Stroke previews use `StrokeSession` outside the executor rather than an executor transaction: the mask
   must accumulate over the whole stroke for flat opacity, so the command recomputes the stroke from its
   samples, which also makes replay exact.
3. The interim `MainWindow` (one 2048² document, the brush, Edit > Undo and Redo) stands until S1-7.
4. The synthetic input rate is 248 Hz (millisecond timer resolution), not 240 Hz.
5. The latency benchmark runs on the offscreen platform, which measures the spec's CPU event-to-submission
   boundary; photon latency on a compositor is not measured.
6. Focus loss commits an interrupted stroke and Escape cancels it (the rule spec §16.3 asks to document).
7. New `scripts/experiments/wayland_run.py` runs any Qt program's windows on a temporary headless output.

### Progress report (plan §8.2)

#### S1-3 Brush stroke vertical slice, 2026-10-13

- **Implemented outcomes:** a round brush and eraser with pressure-controlled size, flat stroke opacity,
  deterministic resampling, live preview and one history item per stroke; a document canvas that
  re-renders only changed tiles; undo and redo in the interim window; a latency benchmark.
- **Changed modules:** `core/brush` (new code), `core/commands` (`paintStroke`, completion callbacks),
  `core/render` (per-region keys, `renderTiles`), `ui` (document session, frame source, tools, document
  view, main window, benchmark), `apps/archmage` (`--benchmark`), `scripts`.
- **Passing evidence (commands and result paths):** the table above.
- **Manual validation (what, how, platform):** none by a person; scripted canvas runs offscreen and on
  native Wayland (Hyprland headless output).
- **Performance changes (benchmark result file):** `benchmarks/results/2026-10-13-brush.json`: brush
  preview p95 3.19 ms against the 25 ms budget.
- **Migrations:** none.
- **Dependency changes (manifest diff):** none.
- **Open risks:** zoomed-out views sample full-resolution tiles (no mip levels yet), so the first frame of
  a large document at fit zoom evaluates every tile; no tablet to verify real pressure input (Q-03).
- **State by behavior:** brush, eraser, preview, undo and redo automatically tested; nothing manually
  tested by a person or verified against a benchmark product.
- **Sample document:** none; samples start in S1-4.

### Notes for the next session

- Qt defines `slots`, `signals` and `emit` as macros in Qt code; do not use them as identifiers.
- When a scripted replacement edits code, assert that the anchor text exists: clang-format rewraps
  lines, and a silent miss cost two debugging rounds in S1-2 and S1-3.

## S1-4 Native format save and reopen: complete, 2026-10-13

Work on branch `s1-4-native-format` (`4d9e418`, `edc1851`, `f090c58`, `f473f53`), merged to `main` in
`37b094e`. The G3 checks were not re-run on `da02d43` before work began: that commit changed only this
document after the same session's G3 run on `54c971f`, so the run would have repeated an unchanged check.
The full G3 set passes again on the S1-4 tree below.

### Tasks and definitions of done

| Task | State | Evidence |
| --- | --- | --- |
| S1-4.1 Schema v0.1 | Automatically tested | `io/native/schema/0001.sql`, embedded at build time; `NativeSchema.AppliesToAnEmptyDatabaseAndPassesTheIntegrityCheck`: applies to an empty database, `integrity_check` returns ok, `application_id` set, eight tables |
| S1-4.2 Blob codec | Automatically tested | `Blob.h`; `NativeBlob.*`: zstd and raw round trips; a flipped byte at three offsets, a wrong hash, a wrong or oversized length and a truncated raw blob are `damaged_input`, an unknown codec `unsupported_input`; `NativeFile.ATamperedTileBlobFailsToOpenAsDamagedInput` flips one bit of a stored tile plane |
| S1-4.3 Writer | Automatically tested | `writeDocument`, `renderPreview`; `NativeFile.RoundTripIsStructurallyEqualAndByteExactForTiles`: every document and layer field equal, tiles byte-exact, the instance still shares its raster, identical composites; `Float32SamplesSurviveBitForBit` (−0.25, 7.5, NaN, a denormal); `StoresAPreviewOfTheCompositeAtMost512Pixels` (512 × 205 for 1500 × 600, pixels equal to the composite at the sampled positions) |
| S1-4.4 Reader | Automatically tested | `readDocument`; `UnknownContentSurvivesOpenThenSave`: an unknown object's payload text byte for byte, unknown relations, tile planes, a blob, an asset and a manifest key survive open then save, and deleting the layer an unknown relation hangs from drops and counts exactly that relation; `NewerOrUnknownFilesOpenReadOnlyWithTheirPreview`: a newer major (with a changed `objects` table) opens as a read-only preview, a newer minor read-only with content, an unknown capability as a read-only preview; `RejectsFilesThatAreNotArchMageDocuments`: a text file, another SQLite database, a missing file and a view standing in for a table |
| S1-4.5 Save transaction | Automatically tested on tmpfs (`/tmp`) and btrfs (home) | `saveDocument`; `NativeSaveOnFilesystem.LeavesOneSelfContainedFile`: during the write only the temporary file exists, afterwards only the target, journal mode `delete`; `AFailedStepLeavesThePreviousFileIntact`: a failure before each of five steps leaves the previous file byte-identical and no temporary file, a failure after the rename is reported with the new file in place; `NativeSave.EditsDuringASaveLeaveTheDocumentModified`; `NonAtomicFilesystemsKeepThePreviousFileAsBackup`; `SavesThroughASymbolicLinkAndKeepsTheLink`; `ReportsUnwritableLocationsAsPermissionDenied` |
| S1-4.6 Save versus Save As | Automatically tested | `identityForSave`, `identityForSaveAs`; `NativeSave.SaveKeepsIdentityAndSaveAsDerivesANewOne`: Save keeps `document_id`, Save As writes a new one with `derived_from`, the original file is untouched |
| S1-4.7 Migrations | Automatically tested | `Migration.h`; `tests/io/native/fixtures/v0.1.archmage` and its `.expected.json`; `NativeMigration.*`: one step per older minor, a gap is an error, every written minor has a fixture that opens with its recorded IDs, layers and composite samples and passes deep validation |
| S1-4.8 CLI validate and inspect | Automatically tested | `apps/archmage-cli/main.cpp`; `cli.native` (interchange): the fixture is valid at both levels, a copy with one damaged tile blob exits 1 with a `damaged_input` record, a text file exits 1, a missing file exits 2 with a `permission_denied` / `file.not_found` record, `inspect --json` fields, a usage error exits 2 |
| S1-4.9 AT-001 engine half | Automatically tested | `At001Test` (`document`, `image`): uint8 and float32 documents created, painted, reordered, renamed, faded and hidden through commands, saved on btrfs and reopened; layer names, order, opacity, visibility, tiles, color policy and sample type equal; composites identical as uint8 and float32 |

### Exit gate G4

Met: the AT-001 engine half passes for uint8 and float32 documents; `archmage-cli validate` (standard and
deep) and `inspect --json` work on `docs/samples/s1-4-layered.archmage`; `docs/architecture/native-format.md`
describes schema 0.1 and the save transaction with its fsync and rename sequence.

### Commands and results (2026-10-13)

The gate scripts ran on the working tree that was then committed as `f473f53` (code identical; the commits
added documentation and removed a trailing blank line). ThreadSanitizer ran two edits earlier, before the
unknown-filesystem default and a cast in `NativeMigrationTest`; neither touches threads.

| Command | Result |
| --- | --- |
| `scripts/check.sh` | exit 0; 140/140 non-UI tests (113 before S1-4); manifest test: 38 entries cover 21 lookups; ledger valid, report current |
| `scripts/check-ui.sh` | exit 0; 4/4 |
| `scripts/check-sanitizers.sh` | exit 0; 144/144; 0 compiler warnings |
| `cmake --preset ci && cmake --build --preset ci && ctest --preset ci --label-exclude '^(perf)$'` | 144/144; 0 warnings under `-Werror` (GCC) |
| `cmake --preset tsan && cmake --build --preset tsan && ctest --preset tsan -L "operation\|image\|document"` | 77/77, no ThreadSanitizer reports; Clang flagged one `-Wsign-conversion` in `NativeMigrationTest.cpp`, fixed and covered by the sanitizer build above |
| `build/dev-debug/apps/archmage-cli/archmage-cli validate docs/samples/s1-4-layered.archmage` | exit 0: format 0.1, 8 objects, 60 tile planes, 41 resources |
| `build/dev-debug/apps/archmage-cli/archmage-cli validate --deep docs/samples/s1-4-layered.archmage` | exit 0: 41 resources verified |
| `build/dev-debug/apps/archmage-cli/archmage-cli inspect --json docs/samples/s1-4-layered.archmage` | exit 0: layers Background, Ink, Ink instance, Glaze; 3 rasters; 52 dense and 8 uniform tile planes; a 320 × 200 preview |
| `build/dev-debug/tests/archmage-native-fixture write tests/io/native/fixtures/v0.1.archmage` | 90,112 bytes, with `v0.1.archmage.expected.json` |

### Deviations from the plan

1. ADR-0103 amends D-13: a save writes the captured revision straight into the temporary file rather than
   through the backup API (no source database exists before S1-5), with the rollback journal in memory
   because SQLite's defensive mode forbids `journal_mode = OFF`. Newer minors open read-only; older minors
   migrate in an in-memory copy.
2. The writer and reader are functions behind one header, `NativeFile.h`, in `NativeWriter.cpp` and
   `NativeReader.cpp`; `Payloads.cpp` holds the object encoders and the untrusted-input parsers they share.
3. Validation has three levels: `Quick` for the save path, `Standard` for `validate` (a full load, which
   already verifies every tile blob it decodes) and `Deep` for `validate --deep`.
4. `SaveRequest::hook` is the seam for fault injection, which S1-5 drives from `ARCHMAGE_FAULT` (D-27);
   `SaveRequest::filesystem` overrides detection, which is how the backup fallback is tested.
5. Unrecognized filesystem types take the backup fallback, the conservative reading of spec §10.3.
6. Four commits instead of one per task: the reader, writer, save transaction and their tests depend on
   each other, so S1-4.1 to S1-4.7 and S1-4.9 share `edc1851`.
7. The G3 checks were not re-run separately before work began (see above).
8. The interim main window does not save or open files; that is shell work (S1-7, S1-8).

### Progress report (plan §8.2)

#### S1-4 Native format save and reopen, 2026-10-13

- **Implemented outcomes:** documents save to `.archmage` files through a validated temporary file, fsync
  and atomic rename, and reopen with identical structure and pixels; damaged files are reported, never
  misread; files from newer versions open read-only with their preview; content this version does not
  understand survives open and save; `archmage-cli validate` and `inspect`.
- **Changed modules:** `io/native` (new code), `core/tiles`, `core/color` and `core/document` (name
  parsers, preserved content), `apps/archmage-cli`, `tests/io/native`, `tests/cli`.
- **Passing evidence (commands and result paths):** the table above.
- **Manual validation (what, how, platform):** none by a person; the CLI commands above were run by hand
  on the sample.
- **Performance changes (benchmark result file):** none recorded; large-document save and open timings are
  an open item before the major-1 freeze.
- **Migrations:** schema 0.1 introduced, with fixture `tests/io/native/fixtures/v0.1.archmage`; no
  migration steps yet.
- **Dependency changes (manifest diff):** SQLite and Zstandard move from `found_not_linked` to `linked`; no
  new dependencies.
- **Open risks:** opening loads every tile (memory grows with the document until spooling in S1-5);
  migrations copy whole files into memory; page size and indices are not benchmarked; interrupted saves are
  exercised with injected errors, not process termination (S1-5).
- **State by behavior:** native save, open, validate and inspect automatically tested; nothing manually
  tested by a person or verified against a benchmark product.
- **Sample document:** `docs/samples/s1-4-layered.archmage`.

### Notes for the next session

- SQLite's defensive mode, on for every connection, silently ignores `PRAGMA journal_mode = OFF`; use
  `MEMORY` for files that are discarded on failure.
- With nlohmann/json 3.12, `EXPECT_EQ(std::string, json)` does not compile; compare with `.get<T>()`.
- Do not name an `INSTANTIATE_TEST_SUITE_P` name-generator parameter `info`: `-Wshadow` flags it.

## S1-5 Recovery, durability, fault injection, malformed input: complete, 2026-10-13

Work on branch `s1-5-recovery` (`a7e5a52`, `9a50996`, `da49be0`, `5a173da`, `abd1c0c`), merged to `main`
in `579f04e`. The G4 checks were not re-run on `08ff479` before work began: that commit changed only this
document after the same session's G4 run, so the run would have repeated an unchanged check. The full G4 set
passes again on the S1-5 tree below.

### Tasks and definitions of done

| Task | State | Evidence |
| --- | --- | --- |
| S1-5.1 Fault injection | Automatically tested | `io/native/include/archmage/native/FaultPoints.h` (eight points, `ARCHMAGE_FAULT` grammar, hit counts), option `ARCHMAGE_FAULT_INJECTION` (off in `dev-release`); `FaultPointsTest`: grammar, one scenario reaches all eight points, an armed fault fires at each, `enospc@` gives `resource_exhausted` |
| S1-5.2 Recovery workspace | Automatically tested | `Recovery.h` (`RecoverySession`, `DocumentJournal`); `RecoveryJournal.ACommitIsJournaledWithinTwoAndAHalfSeconds` (in_memory to journaled after 1,929 ms on btrfs), `SavedRevisionsReportSavedAndLaterEditsDoNot`, `WritesOnlyTheTilesAnEditChanged`; `RecoveryRoot` and `RecoverySession` tests |
| S1-5.3 Discovery and load | Automatically tested | `listRecoveryCandidates`, `recoverDocument`, `discardRecovery`; `RecoveryDiscovery.FindsAnAbandonedJournalByIdentityAfterRenames` (document and journal both renamed; identity matched; recovered state equals the edited one; the original byte-identical; discard removes the session), `RunningSessionsAreNotOffered`, `ASavedDocumentIsFlaggedUpToDate` |
| S1-5.4 Spool-to-journal eviction | Automatically tested | `TileSpool`, `Tile::pin`; `RecoverySpool.A20000PixelDocumentStaysWithinA512MiBBudget`: 12,246 dense tiles (765 MiB) in a 20,000² document under a 512 MiB budget; accounted RAM peaked at 512 MiB (limit 512 + 64 MiB reserve), 16,384 evictions, every tile read back intact (12,246 reloads) |
| S1-5.5 AT-013 | Automatically tested (dev-debug, asan-ubsan, tsan) | `KillDuringSaveTest`: `archmage-cli selftest-save` killed with `_exit(137)` at each of the eight points in ten cases (journal and save phases), three repetitions each; the last completed save opens and validates (the new file only after `dir_sync`), and recovery yields the last acknowledged journaled revision or a later commit |
| S1-5.6 AT-014 | Automatically tested (dev-debug, asan-ubsan, tsan) | `DiskFullTest`, modes `rlimit` (`RLIMIT_FSIZE` = the original's size) and `enospc` (`enospc@tile_write`): `resource_exhausted` naming the file, original byte-identical, `modified true`, a journal write after the failure, Save As elsewhere succeeds with `derived_from`, no stray temporary file |
| S1-5.7 Malformed corpus | Automatically tested | `tests/resilience/corpus/make_native_corpus.py` (ten files from the v0.1 fixture); `NativeCorpusTest`: each fails within 2 s with the expected category and code (the newer major opens as a read-only preview), with less than 64 MiB of extra resident memory; the reader and validator now reject containment cycles |
| S1-5.8 Fuzzing | Recorded | `fuzz/native_reader_fuzz.cpp`, `fuzz/seed_native_reader.sh`; `docs/testing/fuzz.md`: 600 s, 78,079 executions, coverage 10,377 to 11,553 edges, no crash, leak or sanitizer report. The PNG harness comes with the PNG codec (S1-8) |
| S1-5.9 CLI recover | Automatically tested | `apps/archmage-cli/Recover.cpp`; `cli.recover` (interchange): empty list, a session killed while journaling is listed with its source path, recovery writes a valid file at the journaled revision, the original unchanged, an existing output refused (`recover.output_exists`, exit 2) and left as it was |

### Exit gate G5

Met: AT-013 and AT-014 pass headless under `dev-debug` and `asan-ubsan` (and under `tsan`); the fuzz run is
recorded in `docs/testing/fuzz.md`; `docs/architecture/recovery.md` documents the durability states and the
"last acknowledged durable revision" guarantee (spec §10.4).

### Commands and results (2026-10-13)

The gate scripts ran on the working tree committed as `da49be0` to `abd1c0c` (the later commits added the
fuzz harness, which the gates do not build, and documentation).

| Command | Result |
| --- | --- |
| `scripts/check.sh` | exit 0; 165/165 non-UI tests (140 before S1-5); manifest test: 38 entries cover 22 lookups; ledger valid, report current |
| `scripts/check-ui.sh` | exit 0; 4/4 |
| `scripts/check-sanitizers.sh` | exit 0; 169/169 including AT-013, AT-014 and the spool test; 0 compiler warnings |
| `cmake --preset tsan && cmake --build --preset tsan && ctest --preset tsan -L "operation\|image\|document\|resilience"` | 101/101, no ThreadSanitizer reports |
| `cmake --preset ci && cmake --build --preset ci && ctest --preset ci --label-exclude '^(perf)$'` | 169/169; 0 warnings under `-Werror` |
| `cmake --preset dev-release && cmake --build --preset dev-release && ctest --test-dir build/dev-release -L "resilience\|interchange"` | builds with fault injection off; 17/17: 12 fault-dependent tests skip, the rest pass (AT-014's `rlimit` mode included) |
| `build/fuzz/fuzz/native_reader_fuzz -max_total_time=600 ...` (full command in `docs/testing/fuzz.md`) | 78,079 executions, no findings |

### Deviations from the plan

1. ADR-0104 amends D-14 and settles details of D-26 and D-27: liveness by `flock` on `session.lock`,
   `/var/tmp/archmage-<uid>` as the last local fallback, incremental journal writes, tiles spoolable once
   their journal transaction commits, the `error@`/`enospc@` grammar, and `selftest-save` /
   `selftest-disk-full` subcommands instead of a `--selftest-save` option.
2. Spooling made tile residency explicit: reading a dense tile requires `Tile::pin()`, copies go through the
   fallible `TileBuffer::copyOf`, and `applyStrokeToTile` and `paintLayer` return errors; `StrokeSession`
   keeps the first failure. Dense tiles now ask the coordinator for room before charging it.
3. Fuzz seeds are generated by `fuzz/seed_native_reader.sh` rather than committed as copies; the corpus plan
   rows say so.
4. The ThreadSanitizer run also covers the resilience tier.
5. The G4 checks were not re-run separately before work began (see above).
6. The interim window does not use the recovery session yet; the status-bar durability indicator is S1-7
   and the recovery dialog S1-8.

### Progress report (plan §8.2)

#### S1-5 Recovery, durability, fault injection, malformed input, 2026-10-13

- **Implemented outcomes:** documents are journaled every two seconds to a crash-safe recovery database;
  after a crash, `archmage-cli recover` finds them by identity and writes them to a new file without
  touching the original; documents larger than the RAM budget stay within it by spooling journaled tiles;
  saves survive kills at every durability step and report disk exhaustion without damage; malformed files
  fail fast with the right error.
- **Changed modules:** `io/native` (recovery, fault points, writer and reader internals), `core/tiles`
  (pins, `TileSpool`), `core/brush`, `core/commands`, `core/render` (pinned reads), `ui/tools` (stroke
  failures), `apps/archmage-cli` (`recover`, self-tests), `tests/resilience`, `fuzz`.
- **Passing evidence (commands and result paths):** the table above; `docs/testing/fuzz.md`.
- **Manual validation (what, how, platform):** none by a person; `archmage-cli recover` and the self-test
  children were also run by hand on tmpfs (outputs consistent with the tests).
- **Performance changes (benchmark result file):** none recorded; the spool test's figures are in the
  table above. Journaling cost during painting is measured in S1-6.8.
- **Migrations:** none; journals use schema 0.1.
- **Dependency changes (manifest diff):** Python 3 is now a CMake lookup (`find_package:Python3`, for the
  corpus generator); no new dependencies.
- **Open risks:** power loss and real network mounts are untested; opening a document still loads every
  tile; the UI does not show durability yet.
- **State by behavior:** journaling, recovery, spooling, kill-during-save and disk exhaustion automatically
  tested; nothing manually tested by a person or verified against a benchmark product.
- **Sample document:** `docs/samples/s1-4-layered.archmage` (unchanged).

### Notes for the next session

- Reading a dense tile's planes requires a `Tile::Pin`; take one before `plane()` in any new reader.
- Fault-dependent tests skip in `dev-release` (`ARCHMAGE_FAULT_INJECTION` is off there).
- Each discovered GoogleTest case runs in its own process, so a test may change
  `ResourceCoordinator::global()` limits without affecting others.

## S1-6 GPU slice and M0 probes: complete except two benchmark records, 2026-10-13

Work on branch `s1-6-gpu-probes` (`4bab441`, `5d59852`, `134003a`, `3bac24b`, `6f6572c`, `11d6c6a`, `127fcaa`,
`b622ef9`, `d2c9812`, `10ebad3`, `5ef3cf3`, `10a89c5`, `a852462`), merged to `main` in `e644dc3`. The G5 checks
were not re-run on `9a9fdee` before work began: that commit changed only this document after the G5 run on
the S1-5 tree. The full set passes again on the S1-6 tree below.

### Tasks and definitions of done

| Task | State | Evidence |
| --- | --- | --- |
| S1-6.1 Vulkan bootstrap | Automatically tested | `core/render/gpu/` (`archmage_gpu`, outside `archmage_core`): `GpuContext` picks a Vulkan 1.1 device with a compute queue (discrete first), runs the Khronos validation layer in Debug and counts its messages, registers half of the largest device-local heap as the VRAM budget, remembers a lost device; `ARCHMAGE_FORCE_CPU=1` reports no device. `GpuTest`: a device is created and destroyed with validation on and no errors (NVIDIA GeForce RTX 4050 Laptop GPU, driver 615.78.08); with `ARCHMAGE_FORCE_CPU=1` the six device tests skip with the reason and the forced-CPU test passes |
| S1-6.2 GPU source-over | Automatically tested | `shaders/source_over.comp` (SPIR-V at build time), `GpuEvaluator`; `GpuTest`: equal to the CPU bit for bit (maximum difference 0; plan bound: uint8 exact, float32 1e-6) on the AT-002 fixture and on random 20-layer uint8 and float32 stacks; after device loss, and for graphs outside the slice, the CPU's results |
| S1-6.3 Blur with halo | Automatically tested | `GaussianBlurNode`; `BlurTest`: required region is the output grown by the radius; tiled evaluation equals the whole image exactly (0 LSB, float difference 0; plan bound 1 LSB) at sigma 0.6, 2 and 7.5 on features across tile borders |
| S1-6.4 Profile conversion | Automatically tested | `ColorProfile::fromPrimaries`; `ColorExperimentTest`; numbers in `docs/decisions/evidence/S1-6/color.md` (Little CMS within 2e-5 of an independent computation in linear light; assign versus convert) |
| S1-6.5 PSD probe | Recorded | `benchmarks/probes/psd_probe.cpp`; `docs/decisions/evidence/S1-6/psd.md` (GIMP 3.2.6 fixture; 2 of 7 records rendered; 13 unsupported records; outside the unsupported shapes all 116,718 pixels equal GIMP's composite) |
| S1-6.6 RAW probe | Recorded | `benchmarks/probes/raw_probe.cpp`; `docs/decisions/evidence/S1-6/raw.md` (CC0 Canon PowerShot A410 DNG; unsupported diagnostic for a non-RAW file) |
| S1-6.7 Text probe | Recorded | `benchmarks/probes/text_probe.cpp`; `docs/decisions/evidence/S1-6/text.md` (Arabic and Devanagari shaped with HarfBuzz and Noto Sans) |
| S1-6.8 Benchmark harness | Partly recorded | `benchmarks/bench_main.cpp`, `archmage --benchmark=launch`, `scripts/benchmarks/launch_timer.py`, `scripts/bench_record.py`. Profiles B and D recorded unavailable (`benchmarks/results/2026-10-13-bench-B.json`, `-D.json`). Profiles A and C **pending**: the record script refused to measure while a game was running (load average 10 to 12, GPU utilization 50 to 63 %) |
| S1-6.9 macOS GPU path | Recorded unavailable | "Unavailable platforms and devices" above: four code gaps and the untested commands that would be run |
| S1-6.10 ADR evidence | Done | ADR-0009, ADR-0010 and ADR-0012 cite the S1-6 evidence; the M0 table above is updated (M0-E6 waits for the A and C records) |

### Exit gate G6

Met except for S1-6.8's profile A and C records:
- every M0 experiment has evidence or an explicit unavailable record, except M0-E6 profiles A and C;
- the CPU/GPU comparison test exists, passes on the RTX 4050 and skips with a reason elsewhere;
- benchmark results: B and D are committed; A and C are not yet.

S1-6 was merged with the two records outstanding. They depend only on the machine being idle, not on code, and
they will be committed to `main` as a follow-up.

### Commands and results (2026-10-13)

| Command | Result |
| --- | --- |
| `scripts/check.sh` on the merge `e644dc3` | exit 0; 180/180 non-UI tests; manifest: 39 entries cover 22 lookups; ledger valid, report current |
| `scripts/check-ui.sh` (tree of `10a89c5`) | exit 0; 4/4 |
| `cmake --preset asan-ubsan && cmake --build --preset asan-ubsan && ctest --preset asan-ubsan` (the steps of `scripts/check-sanitizers.sh`, at low priority; tree of `10a89c5`) | 184/184; 0 compiler warnings |
| `cmake --build --preset tsan && ctest --preset tsan -L "operation\|image\|document\|resilience"` (tree of `d2c9812`) | 113/113, no ThreadSanitizer reports; the 7 GPU tests skip (the TSan runtime crashes on a driver thread) |
| `cmake --preset ci && cmake --build --preset ci && ctest --preset ci --label-exclude '^(perf)$'` (tree of `5ef3cf3`), then `GpuTest` rebuilt from `10a89c5` | 184/184 and 7/7; 0 warnings under `-Werror` |
| `build/dev-debug/tests/GpuTest`; `ARCHMAGE_FORCE_CPU=1 build/dev-debug/tests/GpuTest` | 7 passed; 1 passed and 6 skipped with the reason |
| `psd_probe`, `raw_probe`, `text_probe` rerun after the Clang fixes | outputs identical to the committed evidence |
| `python3 -I scripts/bench_record.py --profile A` | exit 3, refused: "load average 10.86 > 2; GPU utilization 63 % > 10 %" |

### Deviations from the plan

1. The GPU code is its own target, `archmage_gpu` in `core/render/gpu/`, outside `archmage_core`, so only code
   that opts into GPU evaluation links Vulkan.
2. The GPU slice gathers straight samples on the CPU into host-visible buffers and reads results back: it is a
   correctness slice. ADR-0012 records the cost: data movement, not arithmetic, dominates.
3. `GpuTest` runs without LeakSanitizer (the driver's unfreed pipeline allocations are unsymbolizable) and
   skips under ThreadSanitizer.
4. LibRaw's pkg-config link flag `-fopenmp` is dropped so that Clang presets link (`cmake/Dependencies.cmake`).
5. The PSD fixture comes from GIMP only (Krita was not used) and lives in the corpus directory, as the corpus
   plan says. The RAW fixture is a CC0 DNG; vendor RAW formats are untested.
6. `bench_main` measures the frame source's CPU time offscreen, without presentation. Launch is measured warm;
   a cold launch needs root to drop the page cache.
7. The G5 checks were not re-run separately before work began (see above).
8. S1-6 was merged before the profile A and C records exist (see G6).

### Progress report (plan §8.2)

#### S1-6 GPU slice and M0 probes, 2026-10-13

- **Implemented outcomes:**
  - an opt-in Vulkan compute compositor whose results equal the CPU reference bit for bit;
  - a blur node whose tiled output has no seams;
  - a validated profile conversion with assign and convert distinguished;
  - PSD, RAW and text-shaping probes with committed evidence;
  - a benchmark harness and a record script that measures only an idle machine.
- **Changed modules:**
  - `core/render`: the blur node and the new `gpu` module;
  - `core/color`: `fromPrimaries`;
  - `apps/archmage`: the launch benchmark;
  - `benchmarks`: `bench_main` and the probes;
  - `scripts`: `bench_record.py` and `launch_timer.py`;
  - `tests`: `GpuTest`, `BlurTest`, `ColorExperimentTest`;
  - `cmake`: the LibRaw link flags.
- **Passing evidence (commands and result paths):** the table above; `docs/decisions/evidence/S1-6/`.
- **Manual validation (what, how, platform):** none by a person. The probe images (`merged.png`,
  `archmage-flat.png`, `raw-probe-preview.png`, `text-probe.png`) were inspected on the development machine.
- **Performance changes (benchmark result file):** none committed yet for profiles A and C. Indicative GPU-slice
  timings, taken on a busy machine, are in ADR-0012.
- **Migrations:** none.
- **Dependency changes (manifest diff):**
  - now linked: the Vulkan loader and headers, nlohmann/json, libpng, LibRaw, HarfBuzz and FreeType;
  - new entry: `noto-fonts`, a test-time font;
  - new third-party fixture: the CC0 DNG in `fixtures`.
- **Open risks:**
  - the GPU slice's data movement;
  - the macOS GPU path needs Vulkan portability enumeration before it can work;
  - PSD coverage rests on one writer;
  - no idle-machine performance record yet.
- **State by behavior:**
  - GPU compositing, the blur and the color conversion are automatically tested;
  - the probes are recorded;
  - nothing is manually tested by a person or verified against a benchmark product.
- **Sample document:** `docs/samples/s1-4-layered.archmage` (unchanged).

### Notes for the next session

- Benchmarks need an idle machine. Run `scripts/bench_record.py --wait-idle 5` and never pass `--force` for a
  record that is meant to count. `bench_main` exits on unknown arguments: it once ran by accident during a game.
- Each commit changes the revision stamped into `Version.h`, so every build relinks. Before using build
  results as evidence for a commit, compare object timestamps with the sources.
- Third-party fixtures go in `docs/dependencies/manifest.yaml` (`fixtures`) and in the corpus register,
  together with their license evidence.

## S1-7 Shell: documents, layers, navigation: complete except the scripted Wayland run, 2026-10-13

Work on branch `s1-7-shell` (`4da3c00`, `bbbf861`, `0ab75c1`, `a31ef05`, `bf3103d`, `1d6371e`, `c35d8c9`,
`75fc318`), merged to `main` in `22d2fce`, whose tree equals the branch's last commit. The G6 checks were not
re-run on `f8a2fef` before work began: that commit changed only this document after the G6 run on `e644dc3`. The
full set passes on the S1-7 tree below.

### Tasks and definitions of done

| Task | State | Evidence |
| --- | --- | --- |
| S1-7.1 Main window | Automatically tested | `MainWindow` with the spec §2.2 regions: the nine menus, an options row, a one-column tool strip, tabbed documents behind an empty page that offers New and Open, Color above Layers in the right dock, and a status bar (job, document coordinates, zoom, durability). `ShellTest::regionsMatchTheTargetLayout` at 1440 × 900: options row height, tool strip width and dock width each within 10 % of 36, 44 and 300 px; Color above Layers. `menusOfferOnlyWorkingCommands`: Image, Select and Filter each hold one disabled entry saying there are no commands yet; document commands are disabled without a document |
| S1-7.2 Document tabs | Automatically tested | New (dialog), Open and Save on a two-thread pool, Save As, Close with Save, Discard or Cancel; the tab and window titles carry `*` while the revision differs from the clean save point. `ShellTest::modifiedMarkerFollowsTheSavePoint`: modify, undo back, the marker clears, also after a save and before it; `closingAModifiedDocumentAsks`, `openingAndSwitchingDocuments`. `ShellPromptsTest` drives the shipped Qt dialogs (New Document defaults and choices, the three close answers and Escape, the save path's extension) |
| S1-7.3 Layers panel | Automatically tested | `LayersModel` over the current revision (top layer first; thumbnails rendered on demand into a 256-entry cache) and `LayersPanel` (inline rename, drag reorder, visibility and lock, new, duplicate, delete, opacity slider). `LayersPanelTest`: 2,000 layers with fewer than 40 child widgets and thumbnails only for the rows drawn; a real slider drag is one "Layer Opacity" history item; renames, toggles and drops become commands |
| S1-7.4 Color panel and eyedropper | Automatically tested | `ColorState`; `ColorPanel` (the swatches open `QColorDialog`; swap and reset buttons); `EyedropperTool` samples the composite. `ColorPanelTest`: X and D by key and by button, the brush paints the foreground, the eyedropper picks a painted pixel and the opaque background and adds no history item. The plan's eyedropper check by a person is not done (deviation 2) |
| S1-7.5 Navigation | Automatically tested | `HandTool`; `ZoomTool` (click ×2, Alt-click ×0.5, a horizontal scrub doubles the scale per 120 logical pixels); Space as a temporary hand; wheel zoom about the cursor; Ctrl+0 fit, Ctrl+1 actual pixels, Ctrl+= and Ctrl+- zoom; the S1-3 transparency checkerboard. `NavigationTest` (7 tests): the document point under the cursor stays within 1e-6 px, hand and Space pans add no history item, a wheel over an unfocused spin box changes neither the view nor the value |
| S1-7.6 Shortcut profile | Automatically tested | App. B shortcuts of the shipped actions: New, Open, Save, Save As, Undo, Redo, New Layer, Duplicate, B, H, Z, I, X, D, [, ], Tab, Enter, Escape. `ShellTest::singleLetterShortcutsSwitchTools`; `textInputKeepsSingleLetters` (B, Z and I typed into the rename editor stay text and the tool does not change); `tabTogglesPanelsOnlyFromTheCanvas` |
| S1-7.7 Tool lifecycle | Automatically tested | `Tool` with activate, deactivate, suspend, resume, Escape and Enter (ADR-0105); the options row shows the active tool's page. `ShellTest::switchingDocumentsMidStrokeCommitsIt`: switching documents mid-stroke commits one "Brush Stroke" item and a late release does nothing; `NavigationTest::spaceIsATemporaryHand` |
| S1-7.8 Theme and accessible names | Automatically tested | `Theme`: Fusion with a dark palette, violet accent `#8B6FE8`, 13 original SVG icons. `ShellTest::iconControlsHaveTooltipsAndNames`: every action with an icon has a tooltip with its shortcut, every visible icon button has an accessible name, and no action's tooltip carries a mnemonic marker or a menu hint. The themed window was reviewed in an offscreen snapshot (`ARCHMAGE_SNAPSHOT_DIR`) |
| S1-7.9 Keyboard access | Automatically tested | Alt mnemonics on the menus; F6 and Shift+F6 between the options row, canvas and Layers list; the list with arrows, Space and F2. `ShellTest::layersCanBeDrivenByKeyboardAlone` selects, renames and hides a layer with the keyboard only; `altOpensTheMenus` |

### Exit gate G7

- Offscreen UI tier green: met (`scripts/check-ui.sh`, 9 of 9 programs).
- Navigation and input checks listed with results: met (table below).
- Manual Wayland run covering create, paint, opacity, undo and redo, reorder, zoom and pan: **not met**. No
  person ran it. Its scripted substitute (the six UI test programs on a headless Wayland output) is pending, because
  the windows must take keyboard focus and so must run while nobody uses the desktop. A silent run, which leaves
  the user's focus alone, cannot activate the windows: `NavigationTest` failed 7 of 7 at its fixture.

S1-7 was merged with the Wayland run outstanding, as S1-6 was with its benchmark records. The run depends on an
unused desktop, not on code. Its results will be added here from `main`.

### Navigation and input checks

Each check passes offscreen (`scripts/check-ui.sh`). On native Wayland, every check is pending the scripted run.

| Check | Test |
| --- | --- |
| Create a document from File > New | `ShellPromptsTest::newDocumentDialog`; the window's `newDocument` in every shell test |
| Paint with the foreground color | `ColorPanelTest::brushPaintsWithTheForeground`, `BrushSliceTest` |
| Layer opacity drag is one history item | `LayersPanelTest::opacityDragIsOneHistoryItem` |
| Undo and redo; modified marker | `ShellTest::modifiedMarkerFollowsTheSavePoint` |
| Reorder by drop | `LayersPanelTest::dragAndDropReorders` (the model's drop handler, not a platform drag) |
| Zoom: wheel, zoom tool, Ctrl+0, Ctrl+1, Ctrl+= | `NavigationTest::wheelZoomsAboutTheCursor`, `zoomToolClicksAndScrubs`, `fitAndActualPixelsShortcuts` |
| Pan: hand tool, Space | `NavigationTest::handDragPans`, `spaceIsATemporaryHand` |
| Wheel over a spin box | `NavigationTest::wheelOverASpinBoxDoesNotNavigate` |
| Tool letters, [ and ] | `ShellTest::singleLetterShortcutsSwitchTools` |
| Letters typed into a text field | `ShellTest::textInputKeepsSingleLetters` |
| Tab, F6, keyboard-only layers | `ShellTest::tabTogglesPanelsOnlyFromTheCanvas`, `layersCanBeDrivenByKeyboardAlone` |
| Alt opens the menus | `ShellTest::altOpensTheMenus` |
| Eyedropper; X and D | `ColorPanelTest::eyedropperSamplesTheComposite`, `swapAndResetByShortcutAndButton` |
| Document switch mid-stroke | `ShellTest::switchingDocumentsMidStrokeCommitsIt` |
| Pointer coordinates in the status bar | `NavigationTest::statusBarFollowsThePointer` |

### Commands and results (2026-10-13)

| Command | Result |
| --- | --- |
| `scripts/check.sh` (tree of `c35d8c9`) | exit 0; 181/181 non-UI tests; manifest: 39 entries cover 22 lookups; ledger valid (121 entries), report current |
| `scripts/check-ui.sh` (tree of `1d6371e`) | exit 0; 9/9 |
| `scripts/check-sanitizers.sh` at low priority (tree of `bf3103d`), then `ctest --preset asan-ubsan -R ShellPromptsTest` (tree of `1d6371e`) | 189/189 and 1/1; 0 compiler warnings |
| `cmake --preset tsan && cmake --build --preset tsan && ctest --preset tsan -L "operation\|image\|document\|resilience"` (tree of `bf3103d`) | 114/114, no ThreadSanitizer reports; the 7 GPU tests skip |
| `cmake --preset ci && cmake --build --preset ci && ctest --preset ci --label-exclude '^(perf)$'` (tree of `bf3103d`), then `ShellPromptsTest` built and run in that tree (tree of `1d6371e`) | 189/189 and 1/1; 0 warnings under `-Werror` |
| `python3 -I scripts/experiments/wayland_run.py --class '^NavigationTest$' -- ./build/dev-debug/tests/NavigationTest` (silent) | 2 passed, 7 failed at `ShellFixture::ready()`: a window on a silent workspace never becomes active. The user's focus stayed where it was |

### Deviations from the plan

1. G7's manual Wayland run was not done by a person. As in S1-3, the person's run belongs to the S1-9
   demonstration. Its scripted substitute is pending (see G7).
2. S1-7.4's eyedropper is tested automatically, offscreen, instead of by hand.
3. Reordering is tested through the model's drop handler. `QDrag::exec` runs its own event loop and needs a
   real pointer, so a platform drag on Wayland is left to the person's run.
4. Tab toggles the panels only from the canvas, and F6 moves between regions (ADR-0105); App. B lists Tab as a
   global shortcut.
5. The App. B shortcuts of commands that arrive in S1-8 (Select All, Deselect, Invert, E, M) are not bound yet.
6. Save chosen in the close prompt runs synchronously (ADR-0105).
7. I-09 (repaint pacing) and I-10 (the XWayland tooltip probe) were not addressed; their rows above say where they
   go next.

### Progress report (plan §8.2)

#### S1-7 Shell: documents, layers, navigation, 2026-10-13

- **Implemented outcomes:**
  - a themed main window with the spec's regions, tabbed documents, and New, Open, Save, Save As and Close with
    the clean save point;
  - a Layers panel that stays light at 2,000 layers, with an opacity slider whose drag is one history item;
  - foreground and background colors, an eyedropper, and hand, zoom and Space navigation;
  - App. B shortcuts with text fields taking precedence;
  - a defined tool lifecycle for interruptions (ADR-0105);
  - keyboard access to the menus, regions and layers;
  - journaling and the durability state for every open document.
- **Changed modules:**
  - `core/commands`: `MutationExecutor::drain`;
  - `ui`: `MainWindow`, `Theme`, `ColorState`, `OptionsRow`, `ShellPrompts`, `DocumentView`, `DocumentSession`,
    `tools/` (`Tool`, `NavigationTools`), `panels/`, and `CanvasSurface::viewChanged`;
  - `apps/archmage`: the theme, and files named on the command line;
  - `resources/icons`;
  - `tests/ui`: five new programs;
  - `scripts/experiments/wayland_run.py`: `--focus`.
- **Passing evidence (commands and result paths):** the table above.
- **Manual validation (what, how, platform):** none by a person. The themed window was reviewed in an offscreen
  snapshot. The scripted native-Wayland run is pending.
- **Performance changes (benchmark result file):** none. The 2,000-layer panel test bounds widgets and thumbnails,
  not time.
- **Migrations:** none.
- **Dependency changes (manifest diff):** `qt6-svg` changes from `found_not_linked` to `runtime`, because the SVG
  icon engine plugin loads the icons. No new lookups.
- **Open risks:**
  - nothing has run on a real compositor with focus yet, including popups (menus) on Wayland, which need an input
    serial;
  - closing a large document with Save blocks the window;
  - repaints are not paced to the display (I-09).
- **State by behavior:**
  - every S1-7 behavior listed above is automatically tested offscreen;
  - nothing is manually tested by a person or verified against a benchmark product.
- **Sample document:** `docs/samples/s1-4-layered.archmage` (unchanged).

### Notes for the next session

- The shell tests need an active window. On native Wayland, run them with `wayland_run.py --focus`, and only while
  nobody uses the desktop.
- `ShellPrompts` keeps dialogs out of the shell tests. New dialogs go behind it and get a `ShellPromptsTest` case
  that drives the real dialog.
- Close, Save and the modified check drain the executor first. New commands that should count before a save
  need nothing more.

## S1-8 to S1-9

Not started.
