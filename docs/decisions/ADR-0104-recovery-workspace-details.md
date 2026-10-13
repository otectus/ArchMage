---
id: ADR-0104
decision: D-14 (amends), D-26, D-27
title: Recovery workspace details settled in S1-5
status: accepted
date: 2026-10-13
spec: [§10.2, §10.4, §7.1, §26.1]
---

# ADR-0104: Recovery workspace details settled in S1-5

## Context

D-14 fixes the recovery workspace's place, format and cadence, D-26 the spooling order and D-27 the fault
injection mechanism. Implementing them (plan S1-5.1 to S1-5.9) needed decisions the plan left open: how a
running session is told apart from an abandoned one, where the workspace goes when the state directory is on
a network filesystem and so is the home directory, how much a journal write rewrites, and when spooled tiles
may be evicted.

## Decision

- **Liveness by lock.** A session holds an exclusive `flock(2)` on `sessions/<id>/session.lock`; discovery,
  recovery and discard skip or refuse locked sessions. The kernel drops the lock when the process dies.
- **Location.** Candidates in order: `$XDG_STATE_HOME/archmage`, `~/.local/state/archmage`,
  `/var/tmp/archmage-<uid>`; the first on a local filesystem wins. `/var/tmp` extends D-14's "fallback to the
  local state directory" for systems whose home directory is itself a network mount.
- **Incremental journal writes.** A write rewrites the small tables and only the tiles that changed since the
  journaled state, following a layer to its new raster ID after a stroke. Thumbnails are refreshed at most
  every 30 s. Every eighth write compacts and checkpoints.
- **Spooling.** Tiles become evictable when the journal transaction holding them commits. The evictor frees
  the least recently pinned tiles, at least 16 MiB per call. Reading a dense tile requires a pin; a tile that
  cannot be read back is an error, not blank pixels.
- **Fault grammar.** D-27's abort and synthetic-error modes are `<point>[:<n>]`, `error@<point>[:<n>]` and
  `enospc@<point>[:<n>]`. `backup` is the save fallback's move to `<name>.bak` and database copies (a save
  never uses the SQLite backup API, ADR-0103).
- **Self-test children.** AT-013 and AT-014 run hidden `archmage-cli selftest-save` and
  `selftest-disk-full` subcommands rather than a `--selftest-save` option.

## Alternatives

- PID files for liveness: rejected; PIDs are reused and a stale file outlives a crash.
- Re-encoding every tile on every journal write: rejected; a 20,000² document would take seconds per write.
- Evicting tiles before they are journaled: rejected by D-26.

## Consequences

- The UI must untrack a document with discard after a save or a deliberate close, or its journal stays
  for recovery (S1-7, S1-8).
- A session's journals are offered for recovery only after the session has ended.

## Evidence

`docs/architecture/recovery.md`; `RecoveryTest`, `FaultPointsTest`, `KillDuringSaveTest`, `DiskFullTest`,
`NativeCorpusTest`, `cli.recover`.
