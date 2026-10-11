---
id: ADR-0014
decision: D-14
title: Per-session recovery workspace journaled every two seconds
status: accepted
date: 2026-10-11
spec: [§10.2, §10.4]
---

# ADR-0014: Per-session recovery workspace journaled every two seconds

## Context

Spec §10.2 separates the working recovery workspace (which may use SQLite WAL) from the saved file,
which must be self-contained, and warns against active WAL databases on network filesystems. Spec
§10.4 targets a two-second journal cadence, guarantees recovery of the last acknowledged durable
revision, and requires discovery by document and session identity.

## Decision

- Location: `${XDG_STATE_HOME:-~/.local/state}/archmage/sessions/<sess-id>/<doc-id>.recovery`, a
  SQLite WAL database with the native schema.
- A dedicated thread journals committed revisions only, every 2 s while the document is dirty.
- Each revision carries a durability status (`in_memory`, `journaled`, `saved`), exposed internally
  and in the status bar.
- Discovery scans the sessions directory and reads identity from each manifest, never filenames.
- Recovery files survive until a replacement save or an explicit discard.
- A network filesystem (detected with `statfs`) is refused for the workspace, with a documented
  fallback to the local state directory.

## Alternatives

- Journaling into the user's file: rejected by §10.2.

## Consequences

- The journal thread competes with painting for I/O; S1-6.8 measures latency with journaling on.

## Evidence

None yet. Implemented in S1-5.2 and S1-5.3; AT-013 automated in S1-5.5.
