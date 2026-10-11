# ArchMage implementation status

Living record of Stage 1 progress (plan §0 and §3.3; spec §29.3). Every session reads this first and
updates it last. Plan: `docs/plans/Stage1_Development_Plan.md`. Decisions: `docs/decisions/`.

State vocabulary (spec §29.3), strongest first: **verified against benchmark**, **manually tested**,
**automatically tested**, **implemented**. A behavior is listed only in a state its evidence supports.

## Current position

- Stage 1 = M0 + M1 (ADR-0001), split into sub-milestones S1-0 to S1-9 (plan §3.2).
- Completed: S1-0 (see below). Next: S1-1, presentation and input feasibility (gate G1).
- Before starting S1-1, re-run the G0 checks: `scripts/check.sh`, `scripts/check-ui.sh`,
  `scripts/check-sanitizers.sh`.

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
| I-01 | D-02 fixes C++20, but D-23 requires `std::expected`, a C++23 feature that neither GCC 16.2.1 nor Clang 22.1.8 provides under `-std=c++20` (ADR-0002) | S1-0 | User decision before S1-2: move to C++23 (superseding ADR) or keep C++20 with an equivalent `Expected` type |
| I-02 | The native Wayland platform plugin ships in `qt6-base` 6.11.2; `qt6-wayland` is not needed for the client path (ADR-0021) | S1-0 | Nothing; consider dropping `qt6-wayland` from the prerequisites |
| I-03 | `noto-fonts` already provides Noto Sans Devanagari; plan Q-04's font installation is unnecessary | S1-0 | Nothing |
| I-04 | The developer session exports `QT_QPA_PLATFORMTHEME=qt6ct`; on the offscreen platform it aborts Qt tests through GTK. Qt tests now unset it (`tests/CMakeLists.txt`) | S1-0 | Nothing |

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

## S1-0 Bootstrap and scaffolding

Pending final verification.

## S1-1 to S1-9

Not started.
