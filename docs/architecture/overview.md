# ArchMage architecture overview

Status: S1-2. The headless core exists: vocabulary types, tiles, the document model, commands with an
executor and jobs, color profiles and the CPU evaluator, plus the S1-1 canvas experiment. Native files,
recovery, painting and the shell are planned design from `docs/plans/Stage1_Development_Plan.md` and its
ADRs in `docs/decisions/`. The "Contents" column lists what exists today.

## Boundaries (spec §5.4)

```mermaid
flowchart TD
    UI["Desktop interface"] --> CMD["Validated commands"]
    AUTO["Scripts and actions"] --> CMD
    CMD --> DOC["Document revisions"]
    DOC --> GRAPH["Processing graph"]
    GRAPH --> CPU["CPU evaluator"]
    GRAPH --> GPU["GPU evaluator"]
    CPU --> VIEW["Color-managed presentation"]
    GPU --> VIEW
    DOC --> STORE["Save and recovery"]
    WORK["Codec and AI workers"] --> CMD
```

Arrows show responsibility and data flow. Nothing mutates a document except a validated command
committed by the mutation executor (spec §9.1, ADR-0015). In Stage 1 the CPU evaluator is
authoritative and the GPU evaluator is an opt-in comparison (ADR-0012); there are no workers yet, and
the job model (S1-2.10) is the seam they will use (plan A-15).

## Module map

| Directory | Target | Namespace | Responsibility (spec §5.5) | Qt | Contents | Populated by |
| --- | --- | --- | --- | --- | --- | --- |
| `core/base/` | `archmage_base` | `archmage::base` | Vocabulary types (ADR-0102) | no | `Expected`, `Error` (ADR-0101), typed `Id`, `Geometry.h` (`IRect`, `DRect`), checked arithmetic, `secureRandomBytes`, `Sha256` | S1-2 |
| `core/document/` | `archmage_document` | `archmage::document` | Document graph, revisions, resource references, invariants | no | `Version.h`; `Document.h`: `DocumentState`, `Layer`, `RasterContent`, `Revision`, `validate()` | S1-2 |
| `core/commands/` | `archmage_commands` | `archmage::commands` | Operations, validation, transactions, undo descriptions | no | `CommandEnvelope`, `CommandDefinition`, `CommandRegistry`, `createDocument`, Stage 1 layer commands, `MutationExecutor`, `JobScheduler` | S1-2 |
| `core/tiles/` | `archmage_tiles` | `archmage::tiles` | Sparse tile storage, sharing, cache, storage-backed eviction | no | `SampleType`, `ChannelLayout`, `Tile`, `TileBuffer`, `TileMap` (chunked copy-on-write), `DirtyRegion`, `ResourceCoordinator` | S1-2, S1-5 |
| `core/render/` | `archmage_render` | `archmage::render` | Evaluation graph, scheduler, CPU kernels, GPU backend contracts | no | `ViewTransform`; `GraphNode`, `LayerSourceNode`, `CompositeNode`, `compileGraph`; source-over kernels; `CpuEvaluator` with `RenderRequest` | S1-1, S1-2, S1-6 |
| `core/color/` | `archmage_color` | `archmage::color` | Profiles, conversions, view transforms, proofing | no | `ColorProfile` (Little CMS, pinned sRGB), `ColorPolicy`, `ColorPipeline`, `iccTransform` | S1-2, S1-6 |
| `core/brush/` | `archmage_brush` | `archmage::brush` | Input processing, brush engines, stroke replay and deltas | no | none (INTERFACE) | S1-3 |
| `io/native/` | `archmage_native` | `archmage::native` | `.archmage` container, save transaction, recovery workspace | no | none (INTERFACE) | S1-4, S1-5 |
| `io/formats/` | `archmage_formats` | `archmage::formats` | PNG and JPEG codecs behind adapters | no | none (INTERFACE) | S1-8 |
| `ui/` | `archmage_ui` | `archmage::ui` | Tools, panels, canvas interaction, accessibility, themes | yes | empty `MainWindow`; `canvas/` (`CanvasSurface` with raster, QRhiWidget and QVulkanWindow adapters, ADR-0011), `input/` (`InputMapper`), `experiments/` (S1-1 harness) | S1-1, S1-3, S1-7, S1-8 |
| `apps/archmage/` | `archmage` | n/a | Desktop executable | yes | opens `MainWindow`; `--experiment=canvas:<kind>` runs the S1-1 harness | S1-7, S1-8 |
| `apps/archmage-cli/` | `archmage-cli` | n/a | Headless validate, inspect, render, recover | no | `--version` | S1-4, S1-5, S1-8 |

Canvas presentation (ADR-0011): every surface presents a CPU-rendered frame through `CanvasSurface`;
`CanvasSurface::defaultKind()` is raster unless `ARCHMAGE_CANVAS` selects `vulkan` or `rhi`. Shaders
compile at build time: SPIR-V headers through `cmake/ArchMageShaders.cmake`, qsb packages through
`qt_add_shaders`. Only `ui/` links Qt's private Gui module, for QRhi.

Aggregates: `archmage_core` links the seven core modules and `archmage_io` links the two io modules
(ADR-0006). A module without sources is an INTERFACE target; `archmage_add_module()` makes it a
static library once it lists sources. Only `ui/` and `apps/archmage/` may link Qt (ADR-0007).

Other directories: `tests/` (CTest tiers, ADR-0005), `benchmarks/` (empty `benchmarks` target until
S1-3.7 and S1-6.8), `fuzz/` (built only by the `fuzz` preset; harnesses in S1-5.8), `resources/`
(original assets only), `packaging/` (stub until M9), `cmake/` (build modules), `scripts/` (gates and
generators), `docs/` (plans, decisions, ledger, status).

## Build-time version stamp

`core/document/include/archmage/document/Version.h.in` is rendered by `cmake/GenerateVersion.cmake`
into `build/<preset>/core/document/generated/include/archmage/document/Version.h`, at configure time
and again before every build. The header carries the project version from `CMakeLists.txt` and the
abbreviated git commit (with `-dirty` when tracked files differ). `archmage-cli --version` prints
`ArchMage <version> (<revision>)`.

## Thread model (ADR-0024, ADR-0014)

| Thread | Owns | Notes |
| --- | --- | --- |
| GUI thread | Qt widgets, input mapping, presentation | Observes immutable revisions only |
| `MutationExecutor` thread (one per document) | Command validation and commits | Serializes every mutation; `requestId` idempotency; observers run here after each change |
| `CpuEvaluator` workers (`std::jthread`, `max(2, hardware_concurrency − 2)`) | Tile evaluation | `std::stop_token` cancellation per render; output tile cache under one mutex |
| `JobScheduler` workers | Long-running jobs | Read an immutable source revision; commit through `executeIf` or keep a candidate (spec §9.3) |
| Recovery journal (one per session, S1-5) | Writes committed revisions every 2 s while dirty | Never blocks the executor; tracks durability per revision |

Data flow: a command envelope reaches the executor, which applies its `CommandDefinition` to the current
immutable `DocumentState` and publishes a new `Revision`; undo moves the history cursor back to an earlier
revision. Renders compile the revision's state into a graph (`compileGraph`) and evaluate tiles on the
CPU evaluator, keyed by `TileMap::generation()` and node parameters.

## Where to look next

- Decisions: `docs/decisions/ADR-0001` to `ADR-0030`, one per plan decision D-01 to D-30.
- Dependencies: `docs/dependencies/manifest.yaml`.
- Current state and evidence: `docs/implementation-status.md`.
