#!/usr/bin/env python3
"""Build the deterministic MHGU/MHXX save-editor CSV dataset."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import shutil
import tempfile
from pathlib import Path
from typing import Iterable


GENERATOR_VERSION = "1.2.0"
DEX_SOURCE = "mhxx-dex-1.0"
DEX_FALLBACK_SOURCE = "mhxx-dex-1.0-en-fallback"
SAVE_SOURCE = "mhxx-save-format-community"
SAVE_FALLBACK_SOURCE = "mhxx-save-format-community-en-fallback"
MARKER = "<!-- generated-by: tools/build_data.py -->"

BASE_COLUMNS = ("id", "name", "english", "source")
EQUIPMENT_COLUMNS = BASE_COLUMNS + ("rarity",)

ARMOR = (
    ("armor_head.csv", 1),
    ("armor_chest.csv", 2),
    ("armor_arms.csv", 3),
    ("armor_waist.csv", 4),
    ("armor_legs.csv", 5),
)

EQUIPMENT_TYPES = (
    (0, "无", "None"),
    (1, "头甲", "Head"),
    (2, "胴甲", "Chest"),
    (3, "腕甲", "Arms"),
    (4, "腰甲", "Waist"),
    (5, "腿甲", "Legs"),
    (6, "护石", "Talisman"),
    (7, "大剑", "Great Sword"),
    (8, "片手剑", "Sword and Shield"),
    (9, "锤", "Hammer"),
    (10, "长枪", "Lance"),
    (11, "轻弩", "Light Bowgun"),
    (12, "重弩", "Heavy Bowgun"),
    (13, "太刀", "Long Sword"),
    (14, "斩击斧", "Switch Axe"),
    (15, "铳枪", "Gunlance"),
    (16, "弓", "Bow"),
    (17, "双剑", "Dual Blades"),
    (18, "狩猎笛", "Hunting Horn"),
    (19, "操虫棍", "Insect Glaive"),
    (20, "盾斧", "Charge Blade"),
)

WEAPON_SAVE_TYPES = {
    "weapon_great_sword": 7,
    "weapon_sword_and_shield": 8,
    "weapon_hammer": 9,
    "weapon_lance": 10,
    "weapon_light_bowgun": 11,
    "weapon_heavy_bowgun": 12,
    "weapon_long_sword": 13,
    "weapon_switch_axe": 14,
    "weapon_gunlance": 15,
    "weapon_bow": 16,
    "weapon_dual_blades": 17,
    "weapon_hunting_horn": 18,
    "weapon_insect_glaive": 19,
    "weapon_charge_blade": 20,
}

LOOKUPS = (
    ("ID_Wpn_AxePhial.csv", "Wpn_AxePhial_ID", "Wpn_AxePhial_", "weapon_special", "14,20", "phial"),
    ("ID_Wpn_BowShot.csv", "Wpn_BowShot_ID", "Wpn_BowShot_", "weapon_special", "16", "arc_type"),
    ("ID_Wpn_GunRecoil.csv", "Wpn_GunRecoil_ID", "Wpn_GunRecoil_", "weapon_special", "11,12", "recoil"),
    ("ID_Wpn_GunReloadSpd.csv", "Wpn_GunReloadSpd_ID", "Wpn_GunReloadSpd_", "weapon_special", "11,12", "reload_speed"),
    ("ID_Wpn_GunSteadiness.csv", "Wpn_GunSteadiness_ID", "Wpn_GunSteadiness_", "weapon_special", "11,12", "deviation"),
    ("ID_Wpn_HHSongCategory.csv", "Wpn_HHSongCategory_ID", "Wpn_HHSongCategory_", "weapon_special", "18", "song_category"),
    ("ID_Wpn_ISKinsectType.csv", "Wpn_ISKinsectType_ID", "Wpn_ISKinsectType_", "kinsect", "19", "damage_type"),
    ("ID_Wpn_RapidFireGap.csv", "Wpn_RapidFireGap_ID", "Wpn_RapidFireGap_", "weapon_special", "11", "rapid_fire_gap"),
    ("ID_Wpn_RecitalEffect.csv", "Wpn_RecitalEffect_ID", "Wpn_RecitalEffect_", "weapon_special", "18", "recital_effect"),
    ("ID_Wpn_ShotType.csv", "Wpn_ShotType_ID", "Wpn_ShotType_", "weapon_special", "15,16", "shot_type"),
    ("ID_Wpn_SpAtk.csv", "Wpn_SpAtk_ID", "Wpn_SpAtk_", "attribute_type", "7-20", "dex"),
    ("ID_PeliWpn_Type.csv", "PeliWpn_Type_ID", "PeliWpn_Type_", "palico_weapon", "1", "balance"),
)

PALICO_CN = {
    "fortes": ("领导", "战斗", "防御", "协助", "回复", "爆弹", "采集", "野兽"),
    "targets": ("-----", "只攻击小型", "小型优先", "均衡", "大型优先", "只攻击大型"),
}

TALISMAN_CN = (
    "无", "士兵护石", "斗士护石", "骑士护石", "城塞护石", "女王护石",
    "国王护石", "龙之护石", "英雄护石", "传说护石", "天之护石",
)


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def index_palico_translations(translation: dict, crosswalk: dict) -> dict[str, dict[int, dict]]:
    if translation.get("format") != "mhxx-palico-cn-translation-v1":
        raise ValueError("unsupported Palico Chinese translation crosswalk")
    result: dict[str, dict[int, dict]] = {}
    for key in ("support_moves", "skills"):
        entries = translation.get(key)
        if not isinstance(entries, list):
            raise ValueError(f"Palico Chinese translation is missing {key}")
        indexed = {int(entry["id"]): entry for entry in entries}
        if len(indexed) != len(entries):
            raise ValueError(f"duplicate ID in Palico Chinese translation {key}")
        expected = crosswalk["palico"][key]
        if set(indexed) != {int(entry["id"]) for entry in expected}:
            raise ValueError(f"Palico Chinese translation ID set differs for {key}")
        for entry in expected:
            identifier = int(entry["id"])
            translated = indexed[identifier]
            if translated.get("english") != entry["english"]:
                raise ValueError(f"Palico English crosswalk mismatch for {key} ID {identifier}")
            if not str(translated.get("chinese", "")).strip():
                raise ValueError(f"empty Palico Chinese name for {key} ID {identifier}")
            if not str(translated.get("japanese", "")).strip():
                raise ValueError(f"empty Palico Japanese name for {key} ID {identifier}")
            if not str(translated.get("source", "")).strip():
                raise ValueError(f"empty Palico translation source for {key} ID {identifier}")
        result[key] = indexed
    return result


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


def write_csv(path: Path, columns: Iterable[str], rows: Iterable[dict[str, object]]) -> int:
    materialized = list(rows)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(columns), lineterminator="\n")
        writer.writeheader()
        writer.writerows(materialized)
    return len(materialized)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def localized(row: dict[str, str], prefix: str, language: str) -> tuple[str, str, str]:
    english = row[f"{prefix}0"].strip()
    if language == "en":
        return english, english, DEX_SOURCE
    name = row.get(f"{prefix}1", "").strip()
    if not name or name == "---":
        return english, english, DEX_FALLBACK_SOURCE
    return name, english, DEX_SOURCE


def strip_level(name: str) -> str:
    return re.sub(r"\s+-\s+Lv\.\d+$", "", name).strip()


def none_row(language: str, equipment: bool = False) -> dict[str, object]:
    row: dict[str, object] = {
        "id": 0,
        "name": "无" if language == "cn" else "None",
        "english": "None",
        "source": SAVE_SOURCE,
    }
    if equipment:
        row["rarity"] = 0
    return row


def build_language(
    sql_dir: Path,
    crosswalk: dict,
    palico_translations: dict[str, dict[int, dict]],
    language: str,
    output: Path,
) -> dict[str, int]:
    counts: dict[str, int] = {}
    item_data = {int(row["Itm_ID"]): row for row in read_csv(sql_dir / "DB_Itm.csv")}
    item_names = {int(row["Itm_ID"]): row for row in read_csv(sql_dir / "ID_Itm_Name.csv")}
    items: list[dict[str, object]] = []
    item_by_save_id: dict[int, dict[str, object]] = {}
    for dex_id, row in item_data.items():
        save_id = int(row["Hex"], 16)
        if save_id <= 0:
            continue
        name, english, source = localized(item_names[dex_id], "Itm_Name_", language)
        built = {"id": save_id, "name": name, "english": english, "source": source}
        items.append(built)
        item_by_save_id[save_id] = built
    items.sort(key=lambda row: int(row["id"]))
    counts["items.csv"] = write_csv(output / "items.csv", BASE_COLUMNS, items)

    skill_rows = [none_row(language)]
    for row in read_csv(sql_dir / "ID_SklTree_Name.csv"):
        identifier = int(row["SklTree_ID"])
        if identifier <= 0:
            continue
        name, english, source = localized(row, "SklTree_Name_", language)
        skill_rows.append({"id": identifier, "name": name, "english": english, "source": source})
    counts["skills.csv"] = write_csv(output / "skills.csv", BASE_COLUMNS, skill_rows)

    decorations = [none_row(language)]
    for entry in crosswalk["decorations"][1:]:
        identifier = int(entry["id"])
        row = item_by_save_id.get(identifier)
        if row is None:
            english = str(entry["english"])
            decorations.append(
                {
                    "id": identifier,
                    "name": english,
                    "english": english,
                    "source": SAVE_FALLBACK_SOURCE if language == "cn" else SAVE_SOURCE,
                }
            )
            continue
        decorations.append({**row, "source": row["source"] + "+" + SAVE_SOURCE})
    counts["decorations.csv"] = write_csv(output / "decorations.csv", BASE_COLUMNS, decorations)

    armor_data = {int(row["Amr_ID"]): row for row in read_csv(sql_dir / "DB_Amr.csv")}
    armor_names = {int(row["Amr_ID"]): row for row in read_csv(sql_dir / "ID_Amr_Name.csv")}
    for file_name, _part in ARMOR:
        rows = []
        for entry in crosswalk["armor"][Path(file_name).stem]:
            save_id = int(entry["save_id"])
            dex_id = int(entry["dex_id"])
            if save_id == 0:
                rows.append(none_row(language, equipment=True))
            elif dex_id < 0:
                english = str(entry["english"])
                rows.append({
                    "id": save_id,
                    "name": english,
                    "english": english,
                    "source": SAVE_FALLBACK_SOURCE if language == "cn" else SAVE_SOURCE,
                    "rarity": 0,
                })
            else:
                name, english, source = localized(armor_names[dex_id], "Amr_Name_", language)
                rows.append({
                    "id": save_id,
                    "name": name,
                    "english": english,
                    "source": source + "+" + SAVE_SOURCE,
                    "rarity": int(armor_data[dex_id]["Rare"]),
                })
        rows.sort(key=lambda row: int(row["id"]))
        counts[file_name] = write_csv(output / file_name, EQUIPMENT_COLUMNS, rows)

    weapon_data = {int(row["Wpn_ID"]): row for row in read_csv(sql_dir / "DB_Wpn.csv")}
    weapon_names = {int(row["Wpn_ID"]): row for row in read_csv(sql_dir / "ID_Wpn_Name.csv")}
    weapon_columns = EQUIPMENT_COLUMNS + ("max_level",)
    for base_name, mapping in crosswalk["weapons"].items():
        rows = [{**none_row(language, equipment=True), "max_level": 0}]
        for entry in mapping[1:]:
            dex_id = int(entry["dex_id"])
            if dex_id < 0:
                english = str(entry["english"])
                rows.append(
                    {
                        "id": entry["save_id"],
                        "name": english,
                        "english": english,
                        "source": SAVE_FALLBACK_SOURCE if language == "cn" else SAVE_SOURCE,
                        "rarity": 0,
                        "max_level": entry["max_level"],
                    }
                )
                continue
            name, english, source = localized(weapon_names[dex_id], "Wpn_Name_", language)
            rows.append(
                {
                    "id": entry["save_id"],
                    "name": strip_level(name),
                    "english": strip_level(english),
                    "source": source + "+" + SAVE_SOURCE,
                    "rarity": int(weapon_data[dex_id]["Rare"]),
                    "max_level": entry["max_level"],
                }
            )
        file_name = base_name + ".csv"
        counts[file_name] = write_csv(output / file_name, weapon_columns, rows)

    talismans = []
    for entry in crosswalk["talismans"]:
        identifier = int(entry["id"])
        english = str(entry["english"])
        name = TALISMAN_CN[identifier] if language == "cn" else english
        source = SAVE_SOURCE
        talismans.append({"id": identifier, "name": name, "english": english, "source": source, "rarity": 0})
    counts["talismans.csv"] = write_csv(output / "talismans.csv", EQUIPMENT_COLUMNS, talismans)

    equipment_types = [
        {"id": identifier, "name": chinese if language == "cn" else english, "english": english, "source": SAVE_SOURCE}
        for identifier, chinese, english in EQUIPMENT_TYPES
    ]
    counts["equipment_types.csv"] = write_csv(output / "equipment_types.csv", BASE_COLUMNS, equipment_types)

    lookup_rows: list[dict[str, object]] = []
    for file_name, id_col, prefix, domain, equipment_type, variant in LOOKUPS:
        for row in read_csv(sql_dir / file_name):
            identifier = int(row[id_col])
            if identifier < 0:
                continue
            name, english, source = localized(row, prefix, language)
            lookup_rows.append(
                {
                    "domain": domain,
                    "equipment_type": equipment_type,
                    "variant": variant,
                    "value": identifier,
                    "name": name,
                    "english": english,
                    "source": source,
                }
            )
    lookup_columns = ("domain", "equipment_type", "variant", "value", "name", "english", "source")
    counts["equipment_lookups.csv"] = write_csv(output / "equipment_lookups.csv", lookup_columns, lookup_rows)

    peli_weapon_data = {int(row["PeliWpn_ID"]): row for row in read_csv(sql_dir / "DB_PeliWpn.csv")}
    peli_weapon_names = {int(row["PeliWpn_ID"]): row for row in read_csv(sql_dir / "ID_PeliWpn_Name.csv")}
    palico_weapons = []
    for entry in crosswalk["palico_equipment"]["palico_weapons"]:
        save_id, dex_id = int(entry["save_id"]), int(entry["dex_id"])
        if save_id == 0:
            palico_weapons.append(none_row(language, equipment=True))
        elif dex_id < 0:
            english = str(entry["english"])
            palico_weapons.append({"id": save_id, "name": english, "english": english,
                "source": SAVE_FALLBACK_SOURCE if language == "cn" else SAVE_SOURCE, "rarity": 0})
        else:
            name, english, source = localized(peli_weapon_names[dex_id], "PeliWpn_Name_", language)
            palico_weapons.append({"id": save_id, "name": name, "english": english,
                "source": source + "+" + SAVE_SOURCE, "rarity": int(peli_weapon_data[dex_id]["Rare"])})
    palico_weapons.sort(key=lambda row: int(row["id"]))
    counts["palico_weapons.csv"] = write_csv(output / "palico_weapons.csv", EQUIPMENT_COLUMNS, palico_weapons)

    peli_armor_data = {int(row["PeliAmr_ID"]): row for row in read_csv(sql_dir / "DB_PeliAmr.csv")}
    peli_armor_names = {int(row["PeliAmr_ID"]): row for row in read_csv(sql_dir / "ID_PeliAmr_Name.csv")}
    for file_name, part in (("palico_head.csv", 11), ("palico_armor.csv", 12)):
        rows = []
        for entry in crosswalk["palico_equipment"][Path(file_name).stem]:
            save_id, dex_id = int(entry["save_id"]), int(entry["dex_id"])
            if save_id == 0:
                rows.append(none_row(language, equipment=True))
            elif dex_id < 0:
                english = str(entry["english"])
                rows.append({"id": save_id, "name": english, "english": english,
                    "source": SAVE_FALLBACK_SOURCE if language == "cn" else SAVE_SOURCE, "rarity": 0})
            else:
                name, english, source = localized(peli_armor_names[dex_id], "PeliAmr_Name_", language)
                rows.append({"id": save_id, "name": name, "english": english,
                    "source": source + "+" + SAVE_SOURCE, "rarity": int(peli_armor_data[dex_id]["Rare"])})
        rows.sort(key=lambda row: int(row["id"]))
        counts[file_name] = write_csv(output / file_name, EQUIPMENT_COLUMNS, rows)

    palico = crosswalk["palico"]
    for file_name, key in (("palico_fortes.csv", "fortes"), ("palico_targets.csv", "targets")):
        rows = []
        for entry in palico[key]:
            english = str(entry["english"])
            translated = PALICO_CN[key][int(entry["id"])] if language == "cn" else english
            rows.append(
                {
                    "id": entry["id"],
                    "name": translated,
                    "english": english,
                    "source": SAVE_SOURCE,
                }
            )
        counts[file_name] = write_csv(output / file_name, BASE_COLUMNS, rows)

    action_columns = BASE_COLUMNS + ("generation_tier",)
    for file_name, key in (("palico_support_moves.csv", "support_moves"), ("palico_skills.csv", "skills")):
        rows = []
        for entry in palico[key]:
            english = str(entry["english"])
            translation = palico_translations[key][int(entry["id"])]
            rows.append(
                {
                    "id": entry["id"],
                    "name": translation["chinese"] if language == "cn" else english,
                    "english": english,
                    "source": translation["source"] if language == "cn" else SAVE_SOURCE,
                    "generation_tier": entry["generation_tier"],
                }
            )
        counts[file_name] = write_csv(output / file_name, action_columns, rows)

    grants = []
    for forte_id, entry_ids in enumerate(palico["forte_owned_moves"]):
        grants.extend(
            {"forte_id": forte_id, "kind": "move", "entry_id": entry_id, "source": SAVE_SOURCE}
            for entry_id in entry_ids
        )
    for forte_id, entry_ids in enumerate(palico["forte_owned_skills"]):
        grants.extend(
            {"forte_id": forte_id, "kind": "skill", "entry_id": entry_id, "source": SAVE_SOURCE}
            for entry_id in entry_ids
        )
    counts["palico_forte_grants.csv"] = write_csv(
        output / "palico_forte_grants.csv", ("forte_id", "kind", "entry_id", "source"), grants
    )

    patterns = []
    for kind, key in (("move", "move_patterns"), ("skill", "skill_patterns")):
        patterns.extend(
            {"kind": kind, "pattern_id": pattern_id, "sequence": "".join(map(str, sequence)), "source": SAVE_SOURCE}
            for pattern_id, sequence in enumerate(palico[key])
        )
    counts["palico_generation_patterns.csv"] = write_csv(
        output / "palico_generation_patterns.csv", ("kind", "pattern_id", "sequence", "source"), patterns
    )

    limits = [
        {"key": key, "value": value, "source": SAVE_SOURCE}
        for key, value in sorted(palico["limits"].items())
    ]
    limits.extend(
        {"key": f"basic_move_{index}", "value": value, "source": SAVE_SOURCE}
        for index, value in enumerate(palico["basic_moves"])
    )
    counts["palico_limits.csv"] = write_csv(output / "palico_limits.csv", ("key", "value", "source"), limits)
    return counts


def readme_text() -> str:
    return rf"""# MHGU / MHXX save-editor data

{MARKER}

This directory is generated by `tools/build_data.py`. Do not edit the CSV
files by hand.

- `cn/` uses Simplified Chinese names from the pinned MHXX Dex. Missing Dex
  translations use English and are marked with an `-en-fallback` source.
- `en/` uses English names in both `name` and `english`.
- Equipment IDs are on-disk save IDs. Weapon tree IDs are joined to Dex root
  rows through the reviewed crosswalk in `tools/reference/`.
- MHGU has no MH4G-style relic equipment, so no relic-only fields are emitted.
- Palico weapons/head/body armor come from Dex. Support-move names are reviewed
  against the linked Bahamut MHXX article and Axibug wiki; passive-skill names
  come from the Axibug wiki. The reviewed ID/Japanese/English/Chinese mapping is
  `tools/reference/palico_cn_translation.json`.
- Palico fortes, RNG generation tiers and patterns, innate grants, and limits
  are save-format facts. They remain data-driven advanced editor options even
  though Dex does not contain those tables.
- Translation references:
  [Bahamut MHXX Palico article](https://forum.gamer.com.tw/Co.php?bsn=5786&sn=829755),
  [Axibug support moves](https://mhwiki.axibug.com/mhxx-wiki/data/2832.html),
  [Axibug passive skills](https://mhwiki.axibug.com/mhxx-wiki/data/2829.html).
- Raw Dex runtime dumps, player saves, and reference editor source trees are
  research inputs and are intentionally not committed.

Rebuild and validate:

```bash
python3 tools/build_data.py --input /path/to/mhxx-dex-raw --output data
python3 tools/validate_data.py data
```

Create the raw dump on 32-bit-capable Windows first:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\run_windows.ps1 `
  -DexDir 'D:\MH\DEX\xx图鉴' `
  -OutDir D:\MH\DEX\mhxx-dex-raw
```
"""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--crosswalk",
        type=Path,
        default=Path(__file__).resolve().parent / "reference" / "mhxx_save_data_crosswalk.json",
    )
    parser.add_argument(
        "--palico-cn",
        type=Path,
        default=Path(__file__).resolve().parent / "reference" / "palico_cn_translation.json",
    )
    args = parser.parse_args()

    raw_manifest = read_json(args.input / "manifest.json")
    if raw_manifest.get("format") != "mhxx-dex-runtime-dump-v1":
        raise ValueError("input is not an MHXX Dex runtime dump")
    source_hashes = {row["name"]: row["sha256"] for row in raw_manifest.get("sourceFiles", [])}
    expected = "5e0508b80b02565c5c7217861c315911878896567b07659631c34b7f7dae46c7"
    if source_hashes.get("MHXX Dex.exe") != expected:
        raise ValueError("raw dump was not produced from the pinned MHXX Dex executable")

    crosswalk = read_json(args.crosswalk)
    if crosswalk.get("format") != "mhxx-save-data-crosswalk-v1":
        raise ValueError("unsupported save-data crosswalk")
    palico_translations = index_palico_translations(read_json(args.palico_cn), crosswalk)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    temp_root = Path(tempfile.mkdtemp(prefix="mhxx-data-", dir=str(args.output.parent)))
    try:
        counts = {
            language: build_language(
                args.input / "direct_sql", crosswalk, palico_translations, language, temp_root / language
            )
            for language in ("cn", "en")
        }
        (temp_root / "README.md").write_text(readme_text(), encoding="utf-8")
        files = []
        for path in sorted(temp_root.rglob("*.csv")):
            relative = path.relative_to(temp_root).as_posix()
            files.append({"path": relative, "rows": counts[relative.split("/", 1)[0]][path.name], "sha256": sha256(path)})
        manifest = {
            "format": "mhxx-save-editor-data-v1",
            "generator_version": GENERATOR_VERSION,
            "sources": {
                "dex_dump_manifest_sha256": sha256(args.input / "manifest.json"),
                "mhxx_dex_exe_sha256": expected,
                "save_data_crosswalk_sha256": sha256(args.crosswalk),
                "palico_cn_translation_sha256": sha256(args.palico_cn),
            },
            "files": files,
        }
        (temp_root / "manifest.json").write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )

        if args.output.exists():
            readme = args.output / "README.md"
            if not readme.is_file() or MARKER not in readme.read_text(encoding="utf-8"):
                raise ValueError(f"refusing to replace unmarked output directory: {args.output}")
            shutil.rmtree(args.output)
        temp_root.rename(args.output)
    finally:
        if temp_root.exists():
            shutil.rmtree(temp_root)
    print(f"generated {len(manifest['files'])} CSV files in {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
