# ArchMage implementation status

Living record of Stage 1 progress (plan §0 and §3.3; spec §29.3). Every session reads this first and
updates it last. Plan: `docs/plans/Stage1_Development_Plan.md`. Decisions: `docs/decisions/`.

State vocabulary (spec §29.3), strongest first: **verified against benchmark**, **manually tested**,
**automatically tested**, **implemented**. A behavior is listed only in a state its evidence supports.

## Current position

- Stage 1 = M0 + M1 (ADR-0001), split into sub-milestones S1-0 to S1-9 (plan §3.2).
- Completed: S1-0 (see below). Next: S1-1, presentation and input feasibility (gate G1).
- Before starting S1-1, reboot (I-06), then re-run the G0 checks: `scripts/check.sh`,
  `scripts/check-ui.sh`, `scripts/check-sanitizers.sh`.

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
| I-01 | D-02 fixes C++20, but D-23 requires `std::expected`, a C++23 feature that neither GCC 16.2.1 nor Clang (22.1.8 or 23.1.1) provides under `-std=c++20` (ADR-0002) | S1-0 | User decision before S1-2: move to C++23 (superseding ADR) or keep C++20 with an equivalent `Expected` type |
| I-02 | The native Wayland platform plugin ships in `qt6-base` (6.11.2 and 6.12.0); `qt6-wayland` only adds the Qt6WaylandCompositor libraries and is not needed (ADR-0021) | S1-0 | Nothing; consider dropping `qt6-wayland` from the prerequisites |
| I-03 | `noto-fonts` already provides Noto Sans Devanagari; plan Q-04's font installation is unnecessary | S1-0 | Nothing |
| I-04 | The developer session exports `QT_QPA_PLATFORMTHEME=qt6ct`; on the offscreen platform it aborts Qt tests through GTK. Qt tests now unset it (`tests/CMakeLists.txt`) | S1-0 | Nothing |
| I-05 | The pacman sync database dated 2026-09-25 pointed at package versions the mirrors had dropped (404 on at.cachyos.org, cachyos.doridian.net, us-mnz.soulharsh007.dev); `mirror.bali0531.hu` redirected to `cdn.cachyos.org`, which answered with an HTML "Welcome to our server" page that pacman cached in place of `cmake`, `qt6-wayland` and `vulkan-validation-layers` and their signatures, so every retry failed. Resolved by the user: stub files removed, then `pacman -Syu --needed <packages>` | S1-0 | Nothing; if pacman reports signature errors again, look for HTML files in `/var/cache/pacman/pkg` |
| I-06 | That `-Syu` upgraded 352 packages: Clang 22.1.8 to 23.1.1, Qt 6.11.2 to 6.12.0, kernel 7.2.7 to 7.2.9 (still running 7.2.7). The loaded NVIDIA kernel module is 615.71.09 while `nvidia-utils` is 615.78.08 | S1-0 | Reboot before S1-1, whose Vulkan experiments need a matching NVIDIA driver; manifest pins already reflect the upgrade |

## M0 deliverables and experiments (plan §1.2)

| Item | State | Evidence |
| --- | --- | --- |
| M0-D1 Clean project initialization | Done in S1-0 | Commit `0cdf8ce` on `main` |
| M0-D2 Toolchain and license manifest | Done in S1-0 | `docs/dependencies/manifest.yaml` |
| M0-D3 Feature ledger | Infrastructure done in S1-0; Stage 1 child entries due in S1-9 | `docs/parity/features.yaml`, `docs/parity/report.md` |
| M0-D4 Architecture decisions | Recorded in S1-0; D-11 provisional until G1 | `docs/decisions/ADR-0001` to `ADR-0030` |
| M0-D5 Canvas and input prototype | Not started (S1-1) | |
| M0-D6 Memory and format experiments | Not started (S1-2, S1-4, S1-6) | |
| M0-D7 Test corpus plan | Done in S1-0 | `docs/testing/corpus-plan.md` |
| M0-E1 Docking, GPU canvas, overlays, mixed DPI, tablet | Not started (S1-1) | |
| M0-E2 Tile compositing, blur halo, profile conversion, brush latency | Not started (S1-2, S1-3, S1-6) | |
| M0-E3 Native save, reopen, kill-during-save recovery | Not started (S1-4, S1-5) | |
| M0-E4 Layered PSD decode probe | Not started (S1-6) | |
| M0-E5 RAW file, shaped Arabic and Indic text, macOS GPU path | Not started (S1-6) | |
| M0-E6 Resource estimates on profiles A to D | Not started (S1-6) | |

## Known gaps (spec §29.2, plan §5.5)

Not verified in Stage 1, each to be closed or carried forward with reproduction instructions:
Windows and macOS builds (ADR-0021); reference profiles B and D; real tablet input (no tablet on the
development machine); HDR; display color accuracy (sRGB display assumed, ADR-0010); compositors other
than Hyprland; accessibility beyond keyboard reachability.

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

## S1-1 to S1-9

Not started.
