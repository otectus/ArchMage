-- ArchMage native document, schema 0.1 (D-13, spec §10.1): the eight logical tables as a first physical
-- schema. Applied to an empty database; embedded into the binary at build time.
PRAGMA application_id = 1095582023; -- 0x414D4147, "AMAG"

-- Document identity, schema version, required capabilities, color policy: one row per key.
CREATE TABLE manifest (
    key TEXT PRIMARY KEY NOT NULL,
    value TEXT NOT NULL
) WITHOUT ROWID;

-- Typed objects with stable IDs and versioned JSON payloads (document, layer.raster, raster, ...).
CREATE TABLE objects (
    id TEXT PRIMARY KEY NOT NULL,
    type TEXT NOT NULL,
    version INTEGER NOT NULL,
    payload TEXT NOT NULL
) WITHOUT ROWID;

-- Ordered typed references: document -layer-> layer (bottom to top), layer -content-> raster.
CREATE TABLE relations (
    parent TEXT NOT NULL,
    kind TEXT NOT NULL,
    ordinal INTEGER NOT NULL,
    child TEXT NOT NULL,
    PRIMARY KEY (parent, kind, ordinal)
) WITHOUT ROWID;
CREATE INDEX relations_child ON relations (child);

-- Content-addressed blobs: SHA-256 of the raw bytes, codec and version, verified on read (D-19).
CREATE TABLE resources (
    hash TEXT NOT NULL UNIQUE,
    media_type TEXT NOT NULL,
    codec TEXT NOT NULL,
    codec_version INTEGER NOT NULL,
    raw_length INTEGER NOT NULL,
    data BLOB NOT NULL
);

-- One row per tile plane: a uniform plane stores its one sample, a dense plane names a resource.
CREATE TABLE tile_maps (
    raster_id TEXT NOT NULL,
    version INTEGER NOT NULL,
    tx INTEGER NOT NULL,
    ty INTEGER NOT NULL,
    plane INTEGER NOT NULL,
    uniform BLOB,
    resource_hash TEXT,
    PRIMARY KEY (raster_id, version, tx, ty, plane),
    CHECK ((uniform IS NULL) <> (resource_hash IS NULL))
) WITHOUT ROWID;
CREATE INDEX tile_maps_resource ON tile_maps (resource_hash);

-- Saved revisions with their transaction label and command provenance (spec §9.2).
CREATE TABLE revisions (
    id TEXT PRIMARY KEY NOT NULL,
    sequence INTEGER NOT NULL,
    parent TEXT,
    label TEXT NOT NULL,
    transaction_id TEXT,
    root TEXT NOT NULL,
    provenance TEXT NOT NULL
) WITHOUT ROWID;

-- Embedded and linked sources (M2); empty in Stage 1.
CREATE TABLE assets (
    id TEXT PRIMARY KEY NOT NULL,
    kind TEXT NOT NULL,
    payload TEXT NOT NULL
) WITHOUT ROWID;

-- Composite previews tagged with revision and quality; readers too old for the file can still show one.
-- `profile` names the ICC resource the samples are encoded in.
CREATE TABLE previews (
    kind TEXT NOT NULL,
    revision TEXT NOT NULL,
    width INTEGER NOT NULL,
    height INTEGER NOT NULL,
    media_type TEXT NOT NULL,
    resource_hash TEXT NOT NULL,
    profile TEXT NOT NULL,
    quality TEXT NOT NULL,
    PRIMARY KEY (kind, revision)
) WITHOUT ROWID;
