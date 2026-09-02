#!/usr/bin/env python3
"""Import audited MHGU armor/skill data into the CSV source tree.

The input is the MIT-licensed MHGUDB-iOS database pinned below.  The importer
does not trust its IDs blindly: armor/decorations must cross-check against the
native slot tables already generated from MHXX RomFS, and skill-tree IDs must
match this editor's native save-skill table.  Rows that cannot be joined are
excluded from automatic search rather than guessed.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import sqlite3
from pathlib import Path

SOURCE_REPOSITORY = "https://github.com/JoeLago/MHGUDB-iOS"
SOURCE_COMMIT = "a91a02bc076883df671c2a4fff3ac53be7d710a8"
SOURCE_SHA256 = "4f1e1f64dffc66c0f088dffe4a4586559623483755c5eee8e58fce5ab9258be1"
SOURCE_LABEL = "mhgudb-ios-mit-a91a02b"
CHARM_SOURCE_REPOSITORY = "https://github.com/AthenaADP/MHGU-ASS"
CHARM_SOURCE_COMMIT = "ae7f125d1d381278d15908d7ce5b092076423918"
CHARM_SLOT_HASHES = {
    "mystery_slots.csv": "61ce1a78c3d1d9e13de91c5e24b0560e412e2558d7f76d10b106a37da3c89f52",
    "shining_slots.csv": "e1d585f8eac7eacf20e715b9a033bd5406f5a03ce71292c48092bdb8a28e28fd",
    "ancient_slots.csv": "3c465fbf35ada25fb880469dfaadaceed8fc618b3a01b3c4f82fa5b662868e6a",
    "enduring_slots.csv": "4211958fcddbd59c0bc93f098f2cdc36d8960dd777334920735023c6d3afe1a8",
}
CHARM_RARITIES = {"mystery_slots.csv": 97, "shining_slots.csv": 98,
                  "ancient_slots.csv": 99, "enduring_slots.csv": 100}

ARMOR_TYPES = {"Head": (1, "armor_head.csv"), "Body": (2, "armor_chest.csv"),
               "Arms": (3, "armor_arms.csv"), "Waist": (4, "armor_waist.csv"),
               "Legs": (5, "armor_legs.csv")}


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def read_index(path: Path) -> dict[int, dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        return {int(row["id"]): row for row in csv.DictReader(handle)}


def write_rows(path: Path, fields: list[str], rows: list[dict[str, object]]) -> None:
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def import_charm_slots(source: Path) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    identifier = 1
    for filename, expected in CHARM_SLOT_HASHES.items():
        path = source / filename
        if not path.is_file() or digest(path) != expected:
            raise ValueError(f"{filename} differs from the pinned MHGU-ASS input")
        with path.open("r", encoding="utf-8-sig", newline="") as handle:
            rows = list(csv.reader(handle))
        if len(rows) != 21 or len(rows[0]) != 4:
            raise ValueError(f"{filename} must contain one header and 20 threshold rows")
        for expected_fulfillment, row in enumerate(rows[1:], 1):
            values = [int(value) for value in row]
            if values[0] != expected_fulfillment or any(value < 0 or value > 100 for value in values[1:]):
                raise ValueError(f"invalid threshold row in {filename}: {row}")
            records.append({"id": identifier, "rarity_marker": CHARM_RARITIES[filename],
                "fulfillment": values[0], "slot1_threshold": values[1],
                "slot2_threshold": values[2], "slot3_threshold": values[3],
                "source": "mhgu-ass-mit-ae7f125"})
            identifier += 1
    return records


def import_data(source: Path, charm_slots: Path, root: Path) -> None:
    if digest(source) != SOURCE_SHA256:
        raise ValueError("MHGUDB-iOS source SHA-256 differs from the pinned audited input")
    connection = sqlite3.connect(f"file:{source}?mode=ro&immutable=1", uri=True)
    if connection.execute("PRAGMA integrity_check").fetchone()[0] != "ok":
        raise ValueError("MHGUDB-iOS source database failed integrity_check")

    native_skills = read_index(root / "en" / "skills.csv")
    source_skills = {int(row[0]): row[1] for row in connection.execute(
        "SELECT _id,name FROM skill_trees ORDER BY _id")}
    if set(source_skills) - set(native_skills):
        raise ValueError("source skill trees contain IDs absent from native save-skill data")
    # Names are a second independent guard against joining a database-internal
    # ID to the wrong save skill.  Minor known spelling differences are
    # normalized, but ordering mismatches remain fatal.
    normalize = lambda text: "".join(ch.lower() for ch in text if ch.isalnum())
    aliases = {"coldblooded": "cldblooded", "thunderres": "thdrres",
               "constitution": "constitutn", "speedsetup": "spdsetup"}
    for identifier, name in source_skills.items():
        left, right = normalize(name), normalize(native_skills[identifier]["english"])
        left, right = aliases.get(left, left), aliases.get(right, right)
        if left != right:
            raise ValueError(f"skill ID {identifier} name mismatch: {name!r} / {native_skills[identifier]['english']!r}")

    armor_stats: list[dict[str, object]] = []
    armor_points: list[dict[str, object]] = []
    stat_id = point_id = 1
    for slot, (equipment_type, filename) in ARMOR_TYPES.items():
        native = read_index(root / "en" / filename)
        rows = connection.execute("""
            SELECT _id,defense,max_defense,fire_res,water_res,thunder_res,ice_res,
                   dragon_res,gender,hunter_type,num_slots
              FROM armor WHERE slot=? ORDER BY _id
        """, (slot,)).fetchall()
        for row in rows:
            armor_id = int(row[0]) & 0xFFFF
            if armor_id not in native:
                raise ValueError(f"{slot} armor {armor_id} is absent from native ID table")
            if int(native[armor_id]["slots"]) != int(row[10]):
                raise ValueError(f"{slot} armor {armor_id} slot count differs from native RomFS")
            armor_stats.append({"id": stat_id, "equipment_type": equipment_type,
                "armor_id": armor_id, "gender": int(row[8]), "combat": int(row[9]),
                "base_defense": int(row[1]), "max_defense": int(row[2]),
                "fire_res": int(row[3]), "water_res": int(row[4]),
                "thunder_res": int(row[5]), "ice_res": int(row[6]),
                "dragon_res": int(row[7]), "source": SOURCE_LABEL})
            stat_id += 1
            for skill_id, points in connection.execute("""
                SELECT skill_tree_id,point_value FROM item_to_skill_tree
                 WHERE item_id=? ORDER BY skill_tree_id
            """, (row[0],)):
                if int(skill_id) not in native_skills:
                    raise ValueError(f"armor references unknown skill {skill_id}")
                armor_points.append({"id": point_id, "equipment_type": equipment_type,
                    "armor_id": armor_id, "skill_id": int(skill_id), "points": int(points),
                    "source": SOURCE_LABEL})
                point_id += 1

    native_decorations = read_index(root / "en" / "decorations.csv")
    decoration_points: list[dict[str, object]] = []
    point_id = 1
    for decoration_id, slots in connection.execute(
            "SELECT _id,num_slots FROM decorations ORDER BY _id"):
        if int(decoration_id) not in native_decorations or int(native_decorations[int(decoration_id)]["slot_cost"]) != int(slots):
            raise ValueError(f"decoration {decoration_id} does not match native decoData")
        for skill_id, points in connection.execute("""
            SELECT skill_tree_id,point_value FROM item_to_skill_tree
             WHERE item_id=? ORDER BY skill_tree_id
        """, (decoration_id,)):
            decoration_points.append({"id": point_id, "decoration_id": int(decoration_id),
                "skill_id": int(skill_id), "points": int(points), "source": SOURCE_LABEL})
            point_id += 1

    active_skills = [{"id": int(identifier), "skill_id": int(skill_id),
        "points": int(points), "name": name, "english": name, "source": SOURCE_LABEL}
        for identifier, skill_id, points, name in connection.execute("""
            SELECT _id,skill_tree_id,required_skill_tree_points,name
              FROM skills ORDER BY _id
        """)]
    connection.close()

    outputs = {
        "armor_stats.csv": (["id", "equipment_type", "armor_id", "gender", "combat",
            "base_defense", "max_defense", "fire_res", "water_res", "thunder_res",
            "ice_res", "dragon_res", "source"], armor_stats),
        "armor_skill_points.csv": (["id", "equipment_type", "armor_id", "skill_id", "points", "source"], armor_points),
        "decoration_skill_points.csv": (["id", "decoration_id", "skill_id", "points", "source"], decoration_points),
        "active_skills.csv": (["id", "skill_id", "points", "name", "english", "source"], active_skills),
        "talisman_slot_thresholds.csv": (["id", "rarity_marker", "fulfillment",
            "slot1_threshold", "slot2_threshold", "slot3_threshold", "source"],
            import_charm_slots(charm_slots)),
    }
    for language in ("cn", "en"):
        for filename, (fields, records) in outputs.items():
            write_rows(root / language / filename, fields, records)

    manifest_path = root / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["sources"]["mhgudb_ios"] = {"repository": SOURCE_REPOSITORY,
        "commit": SOURCE_COMMIT, "database_sha256": SOURCE_SHA256, "license": "MIT"}
    manifest["sources"]["mhgu_ass_charm_slots"] = {
        "repository": CHARM_SOURCE_REPOSITORY, "commit": CHARM_SOURCE_COMMIT,
        "license": "MIT", "files": CHARM_SLOT_HASHES,
    }
    files = []
    for path in sorted(root.glob("*/[!.]*.csv")):
        with path.open("r", encoding="utf-8", newline="") as handle:
            count = sum(1 for _ in csv.DictReader(handle))
        files.append({"path": path.relative_to(root).as_posix(), "rows": count,
                      "sha256": digest(path)})
    manifest["files"] = files
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2,
                                        sort_keys=True) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path, help="pinned MHGUDB-iOS mhgu.db")
    parser.add_argument("charm_slots", type=Path,
                        help="pinned MHGU-ASS Run/Data/Charm Generation directory")
    parser.add_argument("data", type=Path, help="editor data directory")
    args = parser.parse_args()
    import_data(args.source, args.charm_slots, args.data)


if __name__ == "__main__":
    main()
