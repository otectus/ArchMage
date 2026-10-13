---
id: ADR-0105
decision: D-28 (extends); App. B; plan S1-7
title: Stage 1 shell conventions for keys, interruptions, documents and recovery
status: accepted
date: 2026-10-13
spec: [§2.2, §2.3, §9.2, §10.3, §11, §16.3, §26.3, App. B]
---

# ADR-0105: Stage 1 shell conventions for keys, interruptions, documents and recovery

## Context

S1-7 builds the shell around the S1-3 slice. Spec §11 asks for defined behavior on focus loss, Escape,
Enter, device removal and document switching. App. B assigns Tab to "hide/show panels", while plan S1-7.9 asks
for "tab order through panels". App. B has no zoom-level shortcuts. The plan does not say what a window does
with the recovery journal of a document it closes, or what Duplicate Layer means for raster layers whose
painting gives them new content.

## Decision

Keys:
- **Tab** toggles the panels only while the canvas has keyboard focus. Everywhere else it moves focus, so
  keyboard users can reach every control (spec §26.3). **F6 / Shift+F6** move between the regions: options
  row, canvas, Layers list. Shift+Tab leaves the canvas as usual.
- **Space** held over the focused canvas is a temporary hand and is ignored mid-stroke. In the Layers list,
  Space shows or hides the current layer. **F2** renames the active layer from anywhere in the window.
- **Zoom:** Ctrl+0 fit, Ctrl+1 actual pixels (the plan's additions), Ctrl+= or Ctrl++ zoom in, Ctrl+- zoom
  out. **[ and ]** step the brush size by about 15 %, at least 1 px.
- **Single letters** (B, H, Z, I, X, D, [, ]) are window shortcuts. Text fields take precedence, so typing a
  layer name never switches tools (S1-7.6).
- **Mnemonics:** menus have Alt mnemonics; controls outside menus have none, so Alt+letter always reaches the
  menu bar.
- **The wheel** over the canvas always navigates. Over a spin box it changes the value only when that spin box
  has keyboard focus (spec §11).

Interruptions (Tool.h):
- Escape cancels the interaction in progress; Enter commits it.
- These commit what was drawn so far: focus loss, switching documents or tools, holding Space, Save, Close,
  Undo and Redo. Nothing in progress is discarded silently (spec §16.3).
- Before Close, Save and the modified check, the shell drains the executor (`MutationExecutor::drain`). A
  stroke submitted a moment earlier therefore counts.

Documents:
- The window starts empty, offering New and Open. Files named on the command line open at start.
- New Document defaults to 1920 × 1080, white, 8-bit; transparent and 32-bit float are options.
- Open and Save run on a worker pool, with the job in the status bar. Save chosen in the close prompt runs
  before the document closes. A document cannot be closed while its save runs.
- A document opened read-only because of a newer minor (ADR-0103) saves only as a copy with a derived
  identity. Opening a file that is already open brings its tab forward.
- Duplicate Layer is `archmage.layer.duplicateInstance`. In Stage 1 painting gives a layer new content, so a
  duplicate shares pixels only until either layer is edited, as users expect of Duplicate Layer.
- The menu bar always has the nine spec menus. A menu without working commands holds one disabled entry that
  says so, rather than dead items (spec §29.2).

Recovery:
- The window journals every open document (D-14).
- A deliberate close, whether after Save or Discard, discards its journal.
- A window destroyed without its close event keeps the journals for recovery.
- The status bar shows Saved, Journaled or In memory for the current revision.

## Alternatives

- Tab as a window-wide shortcut, as App. B lists it: rejected, because it would break keyboard traversal
  everywhere.
- Asynchronous Save from the close prompt: rejected for Stage 1. Closing would need a state machine over
  pending saves; a short synchronous save is the simpler correct choice.
- A new independent-duplicate command: not needed while edits already give the duplicate its own content.

## Consequences

- From the canvas, keyboard users move forward with F6, not Tab. The View menu shows Tab beside Show Panels.
- Closing a large modified document with Save blocks the window for the duration of the save.

## Evidence

`tests/ui/ShellTest.cpp`, `NavigationTest.cpp`, `LayersPanelTest.cpp`, `ColorPanelTest.cpp` (S1-7).
