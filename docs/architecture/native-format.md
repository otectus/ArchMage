# Native document format, schema 0.1

Status: S1-4. Describes what `io/native` writes and reads today: the `.archmage` container (spec §10.1,
ADR-0013), the save transaction (spec §10.3, ADR-0103), compatibility rules (spec §10.5) and the
`archmage-cli validate` and `inspect` commands (App. A.4). Schema major 0 is pre-release: it may change at
any minor until the major-1 freeze at the M2 exit, and every minor keeps a migration step and a fixture.
The recovery workspace (D-14) arrives in S1-5 and is not described here.

Code: `io/native/schema/0001.sql` (the DDL, embedded at build time), `io/native/src/NativeWriter.cpp`,
`NativeReader.cpp`, `Payloads.cpp`, `SaveTransaction.cpp`, `Migration.cpp`, `Platform.cpp`.

## Identity

| Property | Value |
| --- | --- |
| Extension | `.archmage` |
| MIME type | `application/x-archmage` (provisional, spec §10.1) |
| Container | SQLite 3 database, rollback-journal mode, no WAL |
| `PRAGMA application_id` | `1095582023` (`0x414D4147`, "AMAG") |
| Manifest `app_id` | `archmage` |
| Schema version | manifest `schema_major` = `0`, `schema_minor` = `1` |

A reader first checks `application_id`, then the manifest. A file that is not an SQLite database is
damaged input; an SQLite database with another `application_id` is unsupported input
(`file.not_archmage`).

## Tables

| Table | Key | Columns | Holds |
| --- | --- | --- | --- |
| `manifest` | `key` | `key`, `value` (text) | One row per manifest key, below |
| `objects` | `id` | `id`, `type`, `version`, `payload` (JSON text) | Typed objects with stable IDs |
| `relations` | `parent`, `kind`, `ordinal` | `parent`, `kind`, `ordinal`, `child`; index on `child` | Ordered typed references |
| `resources` | `hash` (unique) | `hash`, `media_type`, `codec`, `codec_version`, `raw_length`, `data` | Content-addressed blobs |
| `tile_maps` | `raster_id`, `version`, `tx`, `ty`, `plane` | the key plus `uniform` or `resource_hash` (exactly one is set); index on `resource_hash` | One row per tile plane |
| `revisions` | `id` | `id`, `sequence`, `parent`, `label`, `transaction_id`, `root`, `provenance` | The saved revision |
| `assets` | `id` | `id`, `kind`, `payload` | Embedded and linked sources (M2); empty in Stage 1 |
| `previews` | `kind`, `revision` | `width`, `height`, `media_type`, `resource_hash`, `profile`, `quality` | Composite preview |

`objects`, `relations`, `manifest`, `tile_maps`, `revisions`, `assets` and `previews` are `WITHOUT ROWID`
tables; `resources` is a rowid table so that large blobs do not inflate a clustered index.

### Manifest keys

| Key | Value |
| --- | --- |
| `app_id` | `archmage` |
| `schema_major`, `schema_minor` | Decimal integers |
| `required_capabilities` | JSON array of capability strings; Stage 1 writes `["raster.layers.v1"]` |
| `tile_size` | `128` (D-08); another size is unsupported input |
| `document_id` | The file's document identity, e.g. `doc-64WQ8CNNYMQHATH747F06GG0E1` (D-16) |
| `derived_from` | Present after Save As: the identity the copy came from |
| `current_revision` | ID of the row in `revisions` this file holds |
| `color_policy` | JSON `{profile, profileDescription, blendPolicy}`, a copy of the document object's policy |
| `writer` | `ArchMage <version> (<revision>)` of the build that saved |
| `saved_at` | UTC time of the save, `YYYY-MM-DDTHH:MM:SSZ` |

Keys this version does not know are preserved on save (below).

### Objects

| Type | Version | Payload fields |
| --- | --- | --- |
| `document` | 1 | `title`, `width`, `height` (1 to 300,000), `resolution` (pixels per inch), `unit` (`px`, `in`, `mm`, `cm`, `pt`, `pc`), `sampleType` (`uint8`, `uint16`, `float16`, `float32`), `colorPolicy {profile, profileDescription, blendPolicy}`, `capabilities`, `extensions` |
| `layer.raster` | 1 | `name`, `visible`, `locked`, `opacity` (0 to 1), `blendMode` (`normal`), `offset [x, y]`, `extensions` |
| `raster` | 1 | `sampleType`, `planes` (channel names bottom to top, e.g. `["red","green","blue","alpha"]`), `tileSize` (128), `defaultSamples` (one lowercase-hex sample per plane: what absent tiles read as) |

The document object's ID is the manifest's `document_id`. `colorPolicy.profile` is the SHA-256 of the
ICC profile, stored as a resource of media type `application/vnd.iccprofile`. `extensions` objects carry
reserved fields unchanged (spec §6.2).

### Relations

| Parent | Kind | Ordinal | Child |
| --- | --- | --- | --- |
| the document | `layer` | stack position, 0 = bottom | a `layer.raster` |
| a `layer.raster` | `content` | 0 | a `raster` |

Several layers may name the same raster: instances share content (spec §6.1). The raster is stored once.

### Tiles

Each stored tile of a raster has one `tile_maps` row per plane, with `version` 0. Tiles that read as the
raster's default are absent. A plane is stored in one of two ways:

- `uniform`: the plane's single sample, `bytesPerSample` bytes. Uniform tiles always use this, and so does
  a dense plane whose samples happen to be equal.
- `resource_hash`: a resource of media type `application/vnd.archmage.tile-plane` holding 128 × 128
  samples in row-major order, `bytesPerSample` bytes each, little-endian, exactly as `Tile::plane()`
  holds them. Samples are straight (unassociated) values (D-09); float planes keep negative, above-one and
  non-finite values bit for bit.

Identical planes share one resource. A reader checks that every tile has all its planes in order, that
`uniform` samples have the sample size, that tile coordinates lie within ±2³² tiles, and that each plane
resource decodes to exactly 128 × 128 samples.

### Resources

A resource is identified by the SHA-256 of its raw bytes (lowercase hex, D-19). `codec` is `zstd`
(Zstandard level 3, `codec_version` = `ZSTD_versionNumber()` of the writer) when compression saves space,
otherwise `raw` (`codec_version` 0). `raw_length` is the decoded size, at most 256 MiB. Decoding checks the
frame's declared size, the decoded length and the hash; any mismatch is damaged input (`blob.damaged`),
never a wrong tile. An unknown codec is unsupported input.

### Revisions and previews

One `revisions` row: the revision ID, its sequence number in the saving session, the parent revision,
the history label, the transaction ID, `root` (the document ID) and `provenance` (the JSON array of
command envelopes that produced the revision, spec §9.2). History is not stored in Stage 1.

One `previews` row of kind `composite` and quality `nearest`: the composite of the saved revision, no more
than 512 pixels on its longest edge, as straight RGBA8 rows (`application/vnd.archmage.rgba8`) encoded in
the color space of the ICC resource named by `profile`. Each preview pixel is the composite at the document
pixel under its center, evaluated through the document's render graph one row at a time, so the cost
follows the preview height times the painted width rather than the document area.

## Compatibility (spec §10.5)

| File | Opens as |
| --- | --- |
| Same major, same minor | Editable |
| Same major, older minor | Editable after migrating an in-memory copy; the file itself is not changed, and the next save writes the current minor |
| Same major, newer minor | Read-only (`schema.newer_minor`), content loaded if it parses, so that saving cannot discard newer content |
| Newer major | Read-only preview (`schema.newer_major`); only `application_id`, the manifest and the preview are read |
| Unknown required capability | Read-only preview (`capability.unknown`), with the capability names in the explanation |
| Older major | Unsupported (`schema.too_old`); major 0 is the first |

Every future major must keep the cross-version contract: `application_id`, `manifest(key, value)` with
`app_id`, `schema_major`, `schema_minor` and `document_id`, and the `previews` and `resources` columns used
to show a preview. That is what lets an older version identify a newer file and show its composite.

### Unknown content survives open then save

A reader keeps what it does not understand in `DocumentState::preserved`, and the writer puts it back:

- objects of unknown types, with their payload text byte for byte, and known-type objects reachable only
  through unknown ones;
- relations other than the layer stack and layer contents;
- tile planes of rasters that are not layer contents, and tile planes of any version other than 0;
- resources that no understood content uses, plus every resource a preserved tile plane names;
- asset rows and unknown manifest keys.

A preserved relation whose endpoint no longer exists at save time (for example, an unknown mask on a layer
that was deleted) is dropped and counted in `WriteReport::droppedOpaqueRelations`. References hidden inside
opaque payloads cannot be checked; spec §10.5 does not promise more than byte preservation for them.

### Migrations and fixtures (plan S1-4.7)

`migrationSteps()` (`io/native/include/archmage/native/Migration.h`) lists one step per older minor,
from `(major, minor)` to `minor + 1`; it is empty while 0.1 is the only version. `tests/io/native/fixtures/`
holds one file per minor ever written, `v0.1.archmage`, with a `.expected.json` recording its IDs, layers
and composite samples. `NativeMigrationTest` fails when a minor lacks a fixture or a step, and opens every
fixture. Adding minor 0.2 therefore means: bump `kSchemaMinor`, add the DDL change, add the 0.1 to 0.2
step, and run `archmage-native-fixture write tests/io/native/fixtures/v0.2.archmage` once.

## Reading untrusted files

Files are hostile input until checked (spec §26.1). Every connection runs with SQLite's defensive
settings: `SQLITE_DBCONFIG_DEFENSIVE`, no trusted schema, no double-quoted string literals, and a 300 MiB
value limit. The reader refuses a file whose eight tables are not real tables (a view of the same name
could run SQL on read), parses every payload as untrusted JSON with type and range checks, rejects
duplicate stack entries, missing contents and dangling references, bounds tile coordinates, checks the RAM
budget (D-26) before loading a raster's dense tiles, verifies every blob it decodes, and finally runs
`document::validate`. Stage 1 loads every tile on open; lazy loading belongs with the recovery workspace
and spooling (S1-5).

## Save transaction (spec §10.3)

`saveDocument(SaveRequest)` in `io/native/include/archmage/native/SaveTransaction.h`:

1. **Capture.** The caller passes an immutable committed `Revision` (a `shared_ptr<const ...>`); edits
   made afterwards cannot reach the save.
2. **Materialize.** Create `<name>.tmp-<12 hex digits>` beside the target with `O_CREAT | O_EXCL`, open it
   with SQLite, and write the revision in one transaction with the rollback journal in memory and
   `synchronous = OFF` (ADR-0103). Defensive mode forbids `journal_mode = OFF`; nothing is written beside
   the temporary file.
3. **Finalize.** Close the database and check that no `-journal`, `-wal` or `-shm` file exists.
4. **Validate.** `validateFile(temporary, ValidationLevel::Quick)`: identity, manifest, tables, SQLite
   `quick_check`, and the reference checks of `archmage-cli validate`; then the file's `document_id` and
   `current_revision` must be the ones requested. The previous file's permission bits are copied.
5. **Make durable and replace.** `fsync` the temporary file, `rename(2)` it over the target, `fsync` the
   directory.
6. **Mark saved.** The caller passes `SaveOutcome::sequence` to `MutationExecutor::markSaved`; a revision
   committed during the save keeps the document modified.
7. **Keep the previous file.** Until the rename, the previous file is untouched. On a filesystem without a
   trusted atomic rename, the previous file is first renamed to `<name>.bak` and kept (the fallback,
   reported as `SaveOutcome::atomicReplace = false` and `backup`).

Failure handling: any failure before the rename removes the temporary file and leaves the target as it
was; the error's detail starts with the step name (`write_temporary`, `close_temporary`, `validate`,
`sync_temporary`, `backup_previous`, `replace`, `sync_directory`). A failure of the final directory `fsync`
is reported although the new file is already in place. In the fallback, a failed rename leaves the target
missing and the error names the backup. Saving through a symbolic link writes the file it names and keeps
the link. `SaveRequest::hook` runs before each step; tests use it to fail each step, and fault injection
(D-27) will drive it in S1-5. `strayTemporaries(target)` finds temporary files left by interrupted saves.

Filesystems are recognized with `statfs(2)`:

| Filesystems | Rename treated as | Note |
| --- | --- | --- |
| ext2/3/4, btrfs, xfs, f2fs, zfs, bcachefs, jfs, nilfs2, tmpfs, ramfs, overlayfs | Atomic | Direct replacement |
| vfat, exfat | Not trusted | No journal: backup fallback |
| nfs, smb, cifs, smb2, fuse, 9p | Not trusted | Network or user-space: backup fallback; also not local (D-14) |
| anything else | Not trusted | Backup fallback until verified |

`SaveRequest::filesystem` overrides the detection, for example to force the fallback.

### File identity (spec §6.2)

| Action | `document_id` | `derived_from` |
| --- | --- | --- |
| First save of a new document (Save or Save As) | the document's own ID | absent |
| Save of a saved document | unchanged | unchanged |
| Save As of a saved document | a new ID | the previous `document_id` |

`identityForSave` and `identityForSaveAs` compute this; the session keeps the file identity, and a reopened
document's `DocumentState::id` is its file's `document_id`.

## Command line (App. A.4, plan S1-4.8)

| Command | Checks | Exit |
| --- | --- | --- |
| `archmage-cli validate <file>` | `ValidationLevel::Standard`: identity, manifest, tables, `quick_check`, references (relation endpoints, rasters of tile rows, resources of tiles and previews, revision roots), then a full load as Open does, which verifies every tile blob it decodes | 0 valid, 1 invalid, 2 error |
| `archmage-cli validate <file> --deep` | `ValidationLevel::Deep`: as above with `integrity_check` instead of `quick_check`, and every stored resource decoded and hash-checked, including preserved ones | same |
| `archmage-cli inspect <file> [--json]` | Manifest, object list and counts by type, layer stack, revisions, resource totals by media type and codec, tile plane counts, previews; no pixels are decoded | 0, 1 when not a document or damaged, 2 error |

With `--json`, output is one JSON document on stdout. Problems and errors are records
`{category, code, message, detail}`, with `category` from spec §26.2 (`damaged_input`,
`unsupported_input`, `permission_denied`, ...). A file that cannot be examined at all (missing, unreadable)
is an error (exit 2); a file examined and found wrong is invalid (exit 1).

## Open items before the major-1 freeze (spec §10.1, M2 exit)

- Benchmark page size, indices and blob sizes on large documents; Stage 1 uses SQLite's default 4 KiB pages.
- Compaction and history storage (`revisions` holds one row; previous revisions are not kept).
- Lazy tile loading and spooling instead of loading every tile on open (S1-5).
- Migrations copy the whole file into memory; large files need a temporary-file copy.
- Big-endian hosts would need byte swapping of tile planes; only little-endian Linux is built (D-21).
- Previews are nearest-sampled; a filtered thumbnail needs mip levels (S1-6 or later).
