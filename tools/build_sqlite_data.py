#!/usr/bin/env python3
"""Build a deterministic MHGU/MHXX SQLite database from generated CSV files.

The CSV tree remains the audited source of truth; this command only packages it
for fast read-only runtime access.  Every table keeps its CSV columns as TEXT
or INTEGER-compatible values and the manifest records the input hashes.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import sqlite3
from pathlib import Path

VERSION = "mhgu-save-editor-data-v1"


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def rows(path: Path):
    with path.open("r", encoding="utf-8-sig", newline="") as f:
        yield from csv.DictReader(f)


def sql_type(values):
    for value in values:
        if value is None or value == "":
            continue
        try:
            int(value)
        except ValueError:
            return "TEXT"
    return "INTEGER"


def build(data_root: Path, output: Path, manifest: Path) -> None:
    csv_files = sorted(data_root.glob("*/[!.]*.csv"))
    if not csv_files:
        raise SystemExit(f"no CSV files under {data_root}")
    output.parent.mkdir(parents=True, exist_ok=True)
    tmp = output.with_suffix(output.suffix + ".tmp")
    if tmp.exists():
        tmp.unlink()
    db = sqlite3.connect(tmp)
    db.execute("PRAGMA journal_mode=OFF")
    db.execute("PRAGMA synchronous=OFF")
    db.execute("PRAGMA foreign_keys=OFF")
    table_manifest = []
    for path in csv_files:
        language = path.parent.name
        table = f"{language}__{path.stem}"
        data = list(rows(path))
        columns = list(data[0].keys()) if data else []
        if not columns:
            continue
        types = {c: sql_type([r.get(c, "") for r in data]) for c in columns}
        quoted = ", ".join('"%s" %s' % (c.replace('"', '""'), types[c]) for c in columns)
        db.execute(f'DROP TABLE IF EXISTS "{table}"')
        db.execute(f'CREATE TABLE "{table}" ({quoted})')
        placeholders = ",".join("?" for _ in columns)
        values = []
        for record in data:
            row = []
            for c in columns:
                value = record.get(c, "")
                if types[c] == "INTEGER":
                    row.append(int(value) if value not in (None, "") else None)
                else:
                    row.append(value)
            values.append(tuple(row))
        db.executemany(f'INSERT INTO "{table}" VALUES ({placeholders})', values)
        if "id" in columns:
            db.execute(f'CREATE INDEX IF NOT EXISTS "idx_{table}_id" ON "{table}"("id")')
        table_manifest.append({"table": table, "rows": len(data), "columns": columns,
                               "path": str(path.relative_to(data_root)), "sha256": sha256(path)})
    db.execute("PRAGMA user_version=1")
    db.commit()
    db.close()
    tmp.replace(output)
    digest = sha256(output)
    manifest.write_text(json.dumps({"format": VERSION, "database": {"file": output.name,
        "sha256": digest, "bytes": output.stat().st_size, "user_version": 1}, "tables": table_manifest},
        ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("data", type=Path, help="generated data directory")
    parser.add_argument("--output", type=Path, default=None)
    parser.add_argument("--manifest", type=Path, default=None)
    args = parser.parse_args()
    output = args.output or args.data / "mhgu.sqlite"
    manifest = args.manifest or args.data / "mhgu-sqlite-manifest.json"
    build(args.data, output, manifest)


if __name__ == "__main__":
    main()
