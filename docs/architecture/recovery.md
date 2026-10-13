# Recovery, durability and fault injection

Status: S1-5. Describes the recovery workspace (D-14, ADR-0014, ADR-0104), tile spooling (D-26,
ADR-0026) and fault injection (D-27, ADR-0027) as implemented in `io/native` and `core/tiles`, and the
guarantee they give (spec §10.4). The shell shows none of this yet: the status bar's durability indicator
is S1-7 work and the recovery dialog S1-8 work; `archmage-cli recover` is the interface today.

Code: `io/native/include/archmage/native/Recovery.h`, `io/native/src/Recovery.cpp`,
`core/tiles/include/archmage/tiles/TileSpool.h`, `Tile.h` (pins), `io/native/include/archmage/native/FaultPoints.h`.

## Durability states and the guarantee

| State | Meaning |
| --- | --- |
| `in_memory` | The revision exists only in this process |
| `journaled` | A recovery journal transaction holding the revision has committed |
| `saved` | A save of the revision completed (spec §10.3 steps 1 to 6) |

`DocumentJournal::durability(revision)` and `currentDurability()` report these per revision.

**Guarantee.** After a crash, a kill or a power loss, the journal of a document holds its last journaled
revision or a later one. A journal transaction runs in SQLite WAL mode with `synchronous = FULL`, so the
write-ahead log is synchronized before the commit returns; only then does the revision become
`journaled`. Edits committed after the last journal write are not covered: at most one cadence (two
seconds) plus the duration of one journal write. Nothing is claimed for storage that acknowledges `fsync`
without making data durable.

## Workspace

```
<root>/sessions/<session-id>/session.lock
<root>/sessions/<session-id>/<document-id>.recovery     (+ -wal, -shm while open)
```

`<root>` is the first of these on a local filesystem, created with mode 0700:

1. `$XDG_STATE_HOME/archmage` (when `XDG_STATE_HOME` is an absolute path);
2. `~/.local/state/archmage`;
3. `/var/tmp/archmage-<uid>` (ADR-0104).

Network and FUSE filesystems (`statfs`: nfs, smb, cifs, smb2, fuse, 9p) are refused because SQLite's
write-ahead log needs shared memory that they do not provide reliably (spec §10.2, D-14). If no candidate is
local, `locateRecoveryRoot()` fails with `recovery.no_location` and the reasons.

A session (`RecoverySession::start`) creates its directory and holds an exclusive `flock(2)` on
`session.lock` for its lifetime. The lock is how discovery tells a running session from an abandoned one; the
kernel releases it when the process dies, however it dies. A session that ends with no journal left removes
its directory.

## Journals

`RecoverySession::track(executor, sourcePath)` creates one journal per document and subscribes to the
executor; untrack with `discard = true` deletes it (after a save or a deliberate close), untrack without
discard keeps it for recovery. A document opened twice gets `<document-id>-2.recovery`; names never
matter to discovery.

A journal is an `.archmage` database of schema 0.1 (`docs/architecture/native-format.md`) with
`auto_vacuum = INCREMENTAL`, `journal_mode = WAL` and `synchronous = FULL`. It holds one revision: the latest
journaled one, with its objects, relations, tile planes, resources, provenance, unknown content carried over
from the opened file (`DocumentState::preserved`) and a thumbnail. Manifest keys added to the native ones:

| Key | Value |
| --- | --- |
| `journal.session` | The session's ID (identifies the file as a journal) |
| `journal.written_at` | UTC time of the last journal write |
| `journal.source_path` | The file the document was opened from or last saved to |
| `journal.saved_revision`, `journal.saved_path` | The last completed save, recorded by `markSaved` |

**Cadence.** The session's thread wakes every two seconds (`Options::cadence`) and journals each document
whose current revision differs from its journaled one. One write is one transaction: the small tables are
rewritten, and only tiles that changed since the journaled state are encoded. Tiles are compared by identity
(`TileMap::changedTiles`); when a layer's raster ID changes, as every brush stroke does, its rows move to the
new ID and only the stroke's tiles are written (a short 10 px stroke on a 1024² document wrote at most four
tiles in `WritesOnlyTheTilesAnEditChanged`). Thumbnails (at most 256 px) are refreshed at most every 30 s,
because rendering one reads back tiles that may have been spooled.
A failed write is kept as `lastError()` and retried on the next tick.

**Compaction.** Every eighth write (`Options::compactEvery`), the journal deletes blobs that nothing needs:
not those of its tile rows, previews, the color profile or preserved resources, and not those that live
spooled tiles may read back (`TileSpool::liveHashes`). It then runs `PRAGMA incremental_vacuum` and
`PRAGMA wal_checkpoint(TRUNCATE)`.

## Spooling (D-26)

After a journal transaction commits, every dense tile it wrote is registered with `TileSpool`, with the hash
or inline sample of each plane. Under RAM pressure, `ResourceCoordinator::makeRoom` calls the evictors in
order: disposable caches first, then `TileSpool`, which frees the samples of the least recently pinned
registered tiles that no pin holds, at least 16 MiB at a time. Tiles that are not journaled are never
evicted, so eviction cannot lose an edit.

Reading a dense tile's planes requires a `Tile::Pin`. Pinning an evicted tile asks the coordinator for room
(without holding the tile's lock), then reads the planes back through the journal's own read connection and
verifies each blob's hash. If the journal cannot be read, the pin fails: rendering, painting and saving
report the error instead of using wrong pixels. Before a journal is deleted, `TileSpool::detach` reads back
any of its tiles that are still alive.

Measured in `RecoverySpool.A20000PixelDocumentStaysWithinA512MiBBudget`: a 20,000² document filled with
12,246 dense tiles (765 MiB of samples) in 32 MiB commits, under a 512 MiB RAM budget, peaked at 512 MiB of
accounted RAM (budget 512 MiB plus a 64 MiB reserved pool) with 16,384 evictions, and every tile read back
intact (12,246 reloads).

## Discovery and recovery

`listRecoveryCandidates(root)` walks `<root>/sessions/*`, skips every session whose lock is held, and opens
every other file that is not a lock or a SQLite sidecar. A file is a candidate when its `application_id` and
manifest say ArchMage and it has `journal.session`; its identity, revision, title, source path, save state and
thumbnail come from the database, never from file names. A journal that cannot be read is listed with its
`problem`.

`recoverDocument(candidate)` opens the journal read-write (so SQLite can recover its write-ahead log),
loads it like any document and returns it as a new, unsaved document; `journal.*` keys are not carried into
it. Neither the journal nor the original file changes. `discardRecovery(candidate)` deletes the journal and,
when it was the last one, the session directory. Both refuse a session that is still running
(`recovery.session_live`).

`archmage-cli recover [--root <dir>] [--json]` lists candidates; `recover --session <id> [--document <id>]
--output <file>` writes one. The output name is claimed with `O_EXCL` first, so an existing file is never
replaced (`recover.output_exists`, exit 2).

## Fault injection (D-27)

Compiled when the CMake option `ARCHMAGE_FAULT_INJECTION` is on (every preset except `dev-release`). A fault
is armed with the environment variable `ARCHMAGE_FAULT` or `FaultInjector::arm`:

| Spec | Effect at the n-th hit after arming (default 1) |
| --- | --- |
| `<point>[:<n>]` | `_exit(137)`: no destructors, no atexit handlers, no buffered output |
| `error@<point>[:<n>]` | the operation fails with `internal` / `fault.injected` |
| `enospc@<point>[:<n>]` | the operation fails with `resource_exhausted` / `io.enospc` |

| Point | Where |
| --- | --- |
| `journal_write` | start of a journal write |
| `tile_write` | before each tile plane row, in saves and journal writes |
| `tx_commit` | before `COMMIT`, in saves and journal writes |
| `compaction` | start of a journal compaction |
| `checkpoint` | before the journal's WAL checkpoint |
| `backup` | before the save fallback moves the previous file to `<name>.bak`, and before a database copy |
| `rename` | before the saved temporary file replaces the target |
| `dir_sync` | after that rename, before the directory `fsync` |

Hits are counted per point; `FaultPointsTest` asserts that one scenario reaches every point and that an armed
fault fires at each.

## Evidence

| Test | Shows |
| --- | --- |
| `RecoveryJournal.ACommitIsJournaledWithinTwoAndAHalfSeconds` | a commit moves from `in_memory` to `journaled` in 1.9 s with the default cadence |
| `RecoveryDiscovery.FindsAnAbandonedJournalByIdentityAfterRenames` | discovery after renaming both the document and the journal; recovery equals the edited state; the original is byte-identical |
| `KillDuringSave` (AT-013) | a child killed at each of the 8 points (10 cases, `tile_write` and `tx_commit` in both a journal write and a save), 3 times each: the last completed save opens and validates; recovery yields the last acknowledged journaled revision or a later commit |
| `DiskFull` (AT-014) | under `RLIMIT_FSIZE` and with `enospc@tile_write`: `resource_exhausted` naming the file, the original byte-identical, the document still modified, journaling continues, Save As elsewhere succeeds |
| `NativeCorpus` | ten malformed files fail within 2 s with the right category and less than 64 MiB of extra resident memory |
| `cli.recover` | listing, writing a new file, refusing to overwrite |
| `docs/testing/fuzz.md` | the native reader fuzz run |

## Limits

- Kills are simulated with `_exit(137)` at named points and by killing processes; power loss is not tested.
- Network-filesystem refusal is tested only through the type table; no network mount was available.
- Journals hold full document content in the user's state directory (mode 0700), unencrypted.
- Opening a document still loads every tile; spooling applies to tiles once journaled.
