#!/usr/bin/env python3
"""Validate generated MHGU/MHXX save-editor data."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path


EXPECTED_PALICO_COUNTS = {
    "palico_weapons.csv": 509,
    "palico_head.csv": 502,
    "palico_armor.csv": 524,
    "palico_support_moves.csv": 58,
    "palico_skills.csv": 97,
    "palico_fortes.csv": 8,
    "palico_targets.csv": 6,
    "palico_generation_patterns.csv": 15,
}

EXPECTED_GAME_COUNTS = {
    "items": 2991,
    "armor_head": 1287,
    "armor_chest": 1287,
    "armor_arms": 1287,
    "armor_waist": 1287,
    "armor_legs": 1287,
    "weapon_great_sword": 137,
    "weapon_sword_and_shield": 129,
    "weapon_hammer": 132,
    "weapon_lance": 127,
    "weapon_light_bowgun": 110,
    "weapon_heavy_bowgun": 109,
    "weapon_long_sword": 127,
    "weapon_switch_axe": 110,
    "weapon_gunlance": 115,
    "weapon_bow": 108,
    "weapon_dual_blades": 126,
    "weapon_hunting_horn": 112,
    "weapon_insect_glaive": 83,
    "weapon_charge_blade": 81,
    "palico_weapons": 509,
    "palico_head": 527,
    "palico_armor": 527,
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def id_index(path: Path, rows: list[dict[str, str]]) -> dict[int, dict[str, str]]:
    result: dict[int, dict[str, str]] = {}
    for row in rows:
        require(row.get("id", "").strip() != "", f"{path}: blank id")
        identifier = int(row["id"])
        require(identifier >= 0, f"{path}: negative id {identifier}")
        require(identifier not in result, f"{path}: duplicate id {identifier}")
        require(row.get("name", "").strip() != "", f"{path}: id {identifier} has blank name")
        require(row.get("english", "").strip() != "", f"{path}: id {identifier} has blank english")
        require(row.get("source", "").strip() != "", f"{path}: id {identifier} has blank source")
        result[identifier] = row
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("data", type=Path)
    parser.add_argument("--game-names", type=Path)
    args = parser.parse_args()
    root = args.data

    manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
    require(manifest.get("format") == "mhxx-save-editor-data-v1", "unsupported manifest format")
    require(manifest.get("generator_version") == "2.0.0", "unsupported generator version")
    require(
        bool(manifest.get("sources", {}).get("palico_cn_translation_sha256")),
        "manifest is missing the Palico Chinese translation hash",
    )
    game_resource = manifest.get("game_resource", {})
    require(game_resource.get("format") == "mhxx-game-name-export-v1", "missing game-resource export metadata")
    require(game_resource.get("language") == "jp", "game-resource language must be jp")
    require(game_resource.get("tables") == EXPECTED_GAME_COUNTS, "game-resource table counts differ")
    require(len(game_resource.get("sources", [])) == 36, "expected 36 hashed game-resource source files")
    for entry in game_resource.get("sources", []):
        digest = entry.get("sha256", "")
        require(
            len(digest) == 64 and all(character in "0123456789abcdef" for character in digest),
            f"invalid game-resource SHA-256 for {entry.get('file')}",
        )
    manifest_files = {entry["path"]: entry for entry in manifest["files"]}
    disk_files = {
        path.relative_to(root).as_posix()
        for path in root.rglob("*.csv")
    }
    require(set(manifest_files) == disk_files, "manifest CSV set does not match data directory")
    require(len(disk_files) == 70, f"expected 70 CSV files, found {len(disk_files)}")

    tables: dict[str, list[dict[str, str]]] = {}
    indexes: dict[str, dict[int, dict[str, str]]] = {}
    for relative, entry in manifest_files.items():
        path = root / relative
        rows = read_csv(path)
        require(len(rows) == int(entry["rows"]), f"{relative}: manifest row count differs")
        require(sha256(path) == entry["sha256"], f"{relative}: SHA-256 differs from manifest")
        tables[relative] = rows
        if rows and "id" in rows[0]:
            indexes[relative] = id_index(path, rows)

    cn_names = {path.split("/", 1)[1] for path in disk_files if path.startswith("cn/")}
    en_names = {path.split("/", 1)[1] for path in disk_files if path.startswith("en/")}
    require(cn_names == en_names, "cn/en file sets differ")
    for name in sorted(cn_names):
        cn_key, en_key = f"cn/{name}", f"en/{name}"
        if cn_key not in indexes:
            cn_rows, en_rows = tables[cn_key], tables[en_key]
            if cn_rows and "english" in cn_rows[0]:
                require(len(cn_rows) == len(en_rows), f"{name}: cn/en row counts differ")
                for cn_row, en_row in zip(cn_rows, en_rows):
                    require(cn_row["english"] == en_row["english"], f"{name}: English values differ")
                    require(en_row["name"] == en_row["english"], f"{name}: English name values differ")
                    stable = set(cn_row) - {"name", "source"}
                    require(
                        all(cn_row[key] == en_row[key] for key in stable),
                        f"{name}: language-independent values differ",
                    )
            else:
                require(cn_rows == en_rows, f"{name}: language-neutral tables differ")
            continue
        cn_index, en_index = indexes[cn_key], indexes[en_key]
        require(set(cn_index) == set(en_index), f"{name}: cn/en ID sets differ")
        for identifier in cn_index:
            require(
                cn_index[identifier]["english"] == en_index[identifier]["english"],
                f"{name}: English differs at id {identifier}",
            )
            require(
                en_index[identifier]["name"] == en_index[identifier]["english"],
                f"{name}: English name column differs at id {identifier}",
            )

    for language in ("cn", "en"):
        item_ids = sorted(indexes[f"{language}/items.csv"])
        require(item_ids == list(range(EXPECTED_GAME_COUNTS["items"])), f"{language}/items.csv: IDs are not 0..2990")
        for key, count in EXPECTED_GAME_COUNTS.items():
            if not key.startswith("weapon_"):
                continue
            ids = sorted(indexes[f"{language}/{key}.csv"])
            require(ids == list(range(count)), f"{language}/{key}.csv: IDs do not cover the game array")
        for name, expected in EXPECTED_PALICO_COUNTS.items():
            actual = len(tables[f"{language}/{name}"])
            require(actual == expected, f"{language}/{name}: expected {expected} rows, found {actual}")

        moves = indexes[f"{language}/palico_support_moves.csv"]
        skills = indexes[f"{language}/palico_skills.csv"]
        fortes = indexes[f"{language}/palico_fortes.csv"]
        for label, entries in (("move", moves), ("skill", skills)):
            for identifier, row in entries.items():
                tier = int(row["generation_tier"])
                require(0 <= tier <= 3, f"{language}: {label} {identifier} has invalid tier {tier}")

        grants = tables[f"{language}/palico_forte_grants.csv"]
        for row in grants:
            forte_id, entry_id = int(row["forte_id"]), int(row["entry_id"])
            require(forte_id in fortes, f"{language}: grant references unknown forte {forte_id}")
            target = moves if row["kind"] == "move" else skills if row["kind"] == "skill" else None
            require(target is not None, f"{language}: invalid grant kind {row['kind']}")
            require(entry_id in target, f"{language}: grant references unknown {row['kind']} {entry_id}")

        patterns = tables[f"{language}/palico_generation_patterns.csv"]
        require(sum(row["kind"] == "move" for row in patterns) == 8, f"{language}: move pattern count differs")
        require(sum(row["kind"] == "skill" for row in patterns) == 7, f"{language}: skill pattern count differs")
        for row in patterns:
            require(row["sequence"] and set(row["sequence"]) <= {"1", "2", "3"}, f"{language}: invalid pattern")

        limits = {row["key"]: int(row["value"]) for row in tables[f"{language}/palico_limits.csv"]}
        require(limits.get("max_learned_moves") == 10, f"{language}: learned move limit differs")
        require(limits.get("max_learned_skills") == 8, f"{language}: learned skill limit differs")

        for gear in ("palico_head.csv", "palico_armor.csv"):
            ids = sorted(indexes[f"{language}/{gear}"])
            require(ids != list(range(ids[-1] + 1)), f"{language}/{gear}: expected non-contiguous save IDs")

        for path, rows in tables.items():
            if path.startswith(language + "/") and rows:
                require("is_relic" not in rows[0], f"{path}: relic field must not exist in GU data")

    for name in ("palico_support_moves.csv", "palico_skills.csv"):
        for identifier, row in indexes[f"cn/{name}"].items():
            if row["english"].startswith("NULL") or row["english"] == "-----":
                continue
            require(row["name"] != row["english"], f"cn/{name}: id {identifier} still uses English fallback")
            require(
                not row["source"].endswith("-en-fallback"),
                f"cn/{name}: id {identifier} is still marked as English fallback",
            )

    if args.game_names is not None:
        game_export = json.loads(args.game_names.read_text(encoding="utf-8"))
        require(game_export.get("format") == "mhxx-game-name-export-v1", "unsupported game name export")
        require(game_export.get("language") == "jp", "game name export language must be jp")
        require(sha256(args.game_names) == game_resource.get("export_sha256"), "game name export hash differs from manifest")
        tables_from_game = game_export.get("tables", {})
        require(
            {key: len(value) for key, value in tables_from_game.items()} == EXPECTED_GAME_COUNTS,
            "supplied game name export counts differ",
        )
        for language in ("cn", "en"):
            require(
                set(indexes[f"{language}/items.csv"]) == set(range(len(tables_from_game["items"]))),
                f"{language}/items.csv differs from game ID set",
            )
            for key in ("armor_head", "armor_chest", "armor_arms", "armor_waist", "armor_legs"):
                expected = {
                    identifier for identifier, japanese in enumerate(tables_from_game[key])
                    if identifier == 0 or japanese not in {"装備なし", "装備無し"}
                }
                require(set(indexes[f"{language}/{key}.csv"]) == expected, f"{language}/{key}.csv differs from game ID set")
            for key in (name for name in EXPECTED_GAME_COUNTS if name.startswith("weapon_")):
                require(
                    set(indexes[f"{language}/{key}.csv"]) == set(range(len(tables_from_game[key]))),
                    f"{language}/{key}.csv differs from game ID set",
                )
            require(
                set(indexes[f"{language}/palico_weapons.csv"]) == set(range(len(tables_from_game["palico_weapons"]))),
                f"{language}/palico_weapons.csv differs from game ID set",
            )
            for key in ("palico_head", "palico_armor"):
                expected = {
                    identifier for identifier, japanese in enumerate(tables_from_game[key]) if japanese != "―"
                }
                require(set(indexes[f"{language}/{key}.csv"]) == expected, f"{language}/{key}.csv differs from game ID set")

    print(f"validated {len(disk_files)} CSV files ({sum(len(rows) for rows in tables.values())} rows)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
