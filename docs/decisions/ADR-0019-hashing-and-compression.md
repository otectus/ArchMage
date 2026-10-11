---
id: ADR-0019
decision: D-19
title: SHA-256 content identity, XXH3 cache keys, Zstandard blobs
status: accepted
date: 2026-10-11
spec: [§5.2, §7.2, §10.1]
---

# ADR-0019: SHA-256 content identity, XXH3 cache keys, Zstandard blobs

## Context

Spec §5.2 asks for Zstandard and a stable content hash such as SHA-256, with codec and version stored
and lengths and checksums verified. Spec §7.2 needs cheap cache keys over all rendering inputs.

## Decision

- SHA-256 through OpenSSL libcrypto EVP for content addressing.
- XXH3 for in-memory cache keys only, never persisted as identity.
- Zstandard level 3 by default for tile and resource blobs, with codec id and version recorded per
  blob.

## Alternatives

- A fast non-cryptographic hash for persisted identity: rejected; collisions there would corrupt
  content addressing.

## Consequences

- OpenSSL, xxHash and Zstandard are configure-time requirements from S1-0.

## Evidence

None yet. Blob codec and hashing in S1-4.2; cache keys in S1-2.7.
