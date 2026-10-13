---
id: ADR-0103
decision: D-13 (amends)
title: Saves write the revision directly into the temporary file; newer minors open read-only
status: accepted
date: 2026-10-13
spec: [§10.2, §10.3, §10.5]
---

# ADR-0103: Saves write the revision directly into the temporary file; newer minors open read-only

## Context

Plan task S1-4.5 says the save writes `<name>.archmage.tmp-<rand>` "via the SQLite backup API". The
backup API copies one database into another, but in S1-4 the source of a save is an immutable in-memory
`Revision`, not a database: the recovery workspace (D-14) that could serve as the source arrives in S1-5.
Building an in-memory database only to copy it would hold the whole encoded document twice. Spec §10.3
step 2 allows "SQLite's supported snapshot/backup mechanisms or a validated equivalent".

SQLite's defensive mode, which every ArchMage connection enables because files are untrusted input
(spec §26.1), forbids `PRAGMA journal_mode = OFF`.

D-13 says what happens to files with a newer major and requires migrations for older minors, but not what
a reader does with a newer minor of its own major.

## Decision

- A save serializes the captured revision straight into the exclusively created temporary file, in one
  SQLite transaction with `journal_mode = MEMORY` and `synchronous = OFF`, then closes it, checks that no
  `-journal`, `-wal` or `-shm` file exists, and validates it (`ValidationLevel::Quick` plus identity) before
  the `fsync`, rename and directory `fsync`. Step 4's validation is what makes the direct write a
  "validated equivalent". When a recovery database exists (S1-5), a save from it may use the backup API or
  `VACUUM INTO`; that choice is revisited then.
- A file of the same major and a newer minor opens read-only, with its content loaded when it parses, so
  that saving cannot silently drop content added by the newer minor.
- A file of an older minor is migrated in an in-memory copy; opening never writes to the user's file.
- An unrecognized filesystem gets the backup fallback, like vfat and network filesystems.

## Alternatives

- Backup API from an in-memory database: rejected for S1-4; doubles peak memory and gains nothing while the
  revision is the source.
- `journal_mode = OFF` without defensive mode on the writing connection: rejected; one connection policy
  for every file is simpler to audit, and a memory journal costs little for a fresh file.
- Newer minors editable: rejected; Stage 1 minors may add content an older build cannot preserve.

## Consequences

- A crash during the write leaves a damaged temporary file beside the target; it is never renamed into
  place, and `strayTemporaries()` finds it for cleanup (S1-5).
- Users of a build older than a file's minor can view but not save over it; Save As is a later shell
  decision (S1-7).

## Evidence

`tests/io/native/NativeFormatTest.cpp`: `LeavesOneSelfContainedFile` (only the temporary file exists
during the write; one file afterwards, on tmpfs and btrfs), `AFailedStepLeavesThePreviousFileIntact`,
`NewerOrUnknownFilesOpenReadOnlyWithTheirPreview`, `NonAtomicFilesystemsKeepThePreviousFileAsBackup`.
