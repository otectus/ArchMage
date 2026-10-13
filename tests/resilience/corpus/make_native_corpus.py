#!/usr/bin/env python3
"""Generates the malformed native-format corpus (plan S1-5.7) from a valid fixture.

Usage: python3 -I make_native_corpus.py <fixture.archmage> <output-directory>

Writes one damaged copy per case and corpus.json, which lists each file with the outcome a reader must
produce: an error category (and code) or a read-only opening. The corpus also seeds the native reader fuzzer
(docs/testing/fuzz.md).
"""

import json
import pathlib
import shutil
import sqlite3
import sys

TILE_PLANE = "application/vnd.archmage.tile-plane"


def connect(path: pathlib.Path) -> sqlite3.Connection:
    return sqlite3.connect(str(path), isolation_level=None)


def first_tile_blob(db: sqlite3.Connection) -> int:
    row = db.execute("SELECT rowid FROM resources WHERE media_type = ? ORDER BY hash LIMIT 1", (TILE_PLANE,)).fetchone()
    if row is None:
        raise SystemExit("the fixture has no tile-plane resource")
    return row[0]


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    fixture = pathlib.Path(sys.argv[1])
    out = pathlib.Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    data = fixture.read_bytes()
    cases = []

    def case(name: str, expect: dict) -> pathlib.Path:
        path = out / f"{name}.archmage"
        cases.append({"file": path.name, "expect": expect})
        return path

    case("truncated", {"category": "damaged_input"}).write_bytes(data[: len(data) // 2])
    case("empty", {"category": "unsupported_input", "code": "file.not_archmage"}).write_bytes(b"")
    magic = bytearray(data)
    magic[0:16] = b"SQLite format 9\x00"
    case("wrong-magic", {"category": "damaged_input"}).write_bytes(bytes(magic))

    def edited(name: str, expect: dict, *statements, params=()) -> None:
        path = case(name, expect)
        shutil.copyfile(fixture, path)
        db = connect(path)
        for statement in statements:
            db.execute(statement, params) if params else db.execute(statement)
        db.close()

    edited("wrong-application-id", {"category": "unsupported_input", "code": "file.not_archmage"},
           "PRAGMA application_id = 1")
    edited("schema-major-99", {"readOnly": "schema.newer_major"},
           "UPDATE manifest SET value = '99' WHERE key = 'schema_major'")
    edited("dimension-300001", {"category": "damaged_input", "code": "file.invalid_object"},
           "UPDATE objects SET payload = json_set(payload, '$.width', 300001) WHERE type = 'document'")

    path = case("missing-resource", {"category": "damaged_input", "code": "file.missing_resource"})
    shutil.copyfile(fixture, path)
    db = connect(path)
    db.execute("DELETE FROM resources WHERE rowid = ?", (first_tile_blob(db),))
    db.close()

    path = case("hash-mismatch", {"category": "damaged_input", "code": "blob.damaged"})
    shutil.copyfile(fixture, path)
    db = connect(path)
    row = first_tile_blob(db)
    blob = bytearray(db.execute("SELECT data FROM resources WHERE rowid = ?", (row,)).fetchone()[0])
    blob[len(blob) // 2] ^= 0x01
    db.execute("UPDATE resources SET data = ? WHERE rowid = ?", (bytes(blob), row))
    db.close()

    path = case("huge-blob-length", {"category": "damaged_input"})
    shutil.copyfile(fixture, path)
    db = connect(path)
    db.execute("UPDATE resources SET raw_length = ? WHERE rowid = ?", (1 << 30, first_tile_blob(db)))
    db.close()

    path = case("cyclic-relation", {"category": "damaged_input", "code": "file.cyclic_relation"})
    shutil.copyfile(fixture, path)
    db = connect(path)
    layer, raster = db.execute(
        "SELECT parent, child FROM relations WHERE kind = 'content' ORDER BY parent LIMIT 1").fetchone()
    db.execute("INSERT INTO relations (parent, kind, ordinal, child) VALUES (?, 'mask', 0, ?)", (raster, layer))
    db.close()

    (out / "corpus.json").write_text(json.dumps({"fixture": fixture.name, "cases": cases}, indent=2) + "\n")
    print(f"make_native_corpus.py: {len(cases)} files in {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
