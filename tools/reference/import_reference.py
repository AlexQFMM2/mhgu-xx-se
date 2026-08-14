#!/usr/bin/env python3
"""Build the reviewed save-ID/legality crosswalk used by build_data.py.

The resulting JSON contains only stable save-format facts. Local reference
editor source files are never copied into this repository.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
from pathlib import Path


WEAPONS = (
    ("weapon_great_sword", 1, "EquipGreatSwordNames"),
    ("weapon_long_sword", 2, "EquipLongswordNames"),
    ("weapon_sword_and_shield", 3, "EquipSwordnShieldNames"),
    ("weapon_dual_blades", 4, "EquipDualBladesNames"),
    ("weapon_hammer", 5, "EquipHammerNames"),
    ("weapon_hunting_horn", 6, "EquipHuntingHornNames"),
    ("weapon_lance", 7, "EquipLanceNames"),
    ("weapon_gunlance", 8, "EquipGunlanceNames"),
    ("weapon_switch_axe", 9, "EquipSwitchAxeNames"),
    ("weapon_charge_blade", 10, "EquipChargeBladeNames"),
    ("weapon_insect_glaive", 11, "EquipInsectGlaiveNames"),
    ("weapon_light_bowgun", 12, "EquipLightBowgunNames"),
    ("weapon_heavy_bowgun", 13, "EquipHeavyBowgunNames"),
    ("weapon_bow", 14, "EquipBowNames"),
)

ARMOR = (
    ("armor_head", 1, "EquipHeadNames", "EquipHeadIDs"),
    ("armor_chest", 2, "EquipChestNames", "EquipChestIDs"),
    ("armor_arms", 3, "EquipArmsNames", "EquipArmsIDs"),
    ("armor_waist", 4, "EquipWaistNames", "EquipWaistIDs"),
    ("armor_legs", 5, "EquipLegsNames", "EquipLegsIDs"),
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


def base_weapon_name(name: str) -> str:
    return re.sub(r"\s+-\s+Lv\.\d+$", "", name).strip()


def tier_map(all_names: list[str], tier_lists: list[list[str]]) -> list[dict[str, object]]:
    tiers: dict[str, int] = {}
    for tier, names in enumerate(tier_lists, 1):
        for name in names:
            if name != "-----":
                tiers[name] = tier
    return [
        {"id": identifier, "english": name, "generation_tier": tiers.get(name, 0)}
        for identifier, name in enumerate(all_names)
    ]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--game-data", type=Path, required=True)
    parser.add_argument("--dex-raw", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    game_data = json.loads(args.game_data.read_text(encoding="utf-8"))
    sql_dir = args.dex_raw / "direct_sql"
    weapon_rows = read_csv(sql_dir / "DB_Wpn.csv")
    weapon_names = {
        int(row["Wpn_ID"]): row["Wpn_Name_0"].strip()
        for row in read_csv(sql_dir / "ID_Wpn_Name.csv")
        if int(row["Wpn_ID"]) > 0
    }

    weapon_crosswalk: dict[str, list[dict[str, object]]] = {}
    for file_name, dex_type, array_name in WEAPONS:
        candidates: dict[str, list[int]] = {}
        for row in weapon_rows:
            if int(row["Wpn_Type_ID"]) != dex_type:
                continue
            dex_id = int(row["Wpn_ID"])
            candidates.setdefault(base_weapon_name(weapon_names[dex_id]), []).append(dex_id)

        names = game_data[array_name]
        max_levels = game_data["WeaponMaxLevels"][array_name]
        if len(names) != len(max_levels):
            raise ValueError(f"{array_name}: name/max-level lengths differ")

        mapped: list[dict[str, object]] = [
            {"save_id": 0, "dex_id": 0, "max_level": 0, "english": "None"}
        ]
        for save_id, (name, max_level) in enumerate(zip(names[1:], max_levels[1:]), 1):
            dex_ids = candidates.get(name, [])
            roots = [dex_id for dex_id in dex_ids if weapon_names[dex_id].endswith("Lv.1")]
            choices = roots or dex_ids
            if not choices and name == "DUMMY":
                mapped.append(
                    {
                        "save_id": save_id,
                        "dex_id": -1,
                        "max_level": int(max_level),
                        "english": name,
                    }
                )
                continue
            if len(choices) != 1:
                raise ValueError(
                    f"{array_name}[{save_id}] {name!r}: expected one Dex match, found {choices}"
                )
            mapped.append(
                {
                    "save_id": save_id,
                    "dex_id": choices[0],
                    "max_level": int(max_level),
                }
            )
        weapon_crosswalk[file_name] = mapped

    armor_rows = read_csv(sql_dir / "DB_Amr.csv")
    armor_names = {
        int(row["Amr_ID"]): row["Amr_Name_0"].strip()
        for row in read_csv(sql_dir / "ID_Amr_Name.csv")
    }
    armor_crosswalk: dict[str, list[dict[str, object]]] = {}
    for file_name, part, names_key, ids_key in ARMOR:
        dex_ids_by_name: dict[str, list[int]] = {}
        for row in armor_rows:
            if int(row["Part"]) != part:
                continue
            dex_id = int(row["Amr_ID"])
            dex_ids_by_name.setdefault(armor_names[dex_id], []).append(dex_id)
        for ids in dex_ids_by_name.values():
            ids.sort()

        names = game_data[names_key]
        save_ids = game_data[ids_key]
        if len(names) != len(save_ids):
            raise ValueError(f"{names_key}/{ids_key}: lengths differ")
        occurrence: dict[str, int] = {}
        mapped: list[dict[str, object]] = []
        for save_id, name in zip(save_ids, names):
            if int(save_id) == 0:
                mapped.append({"save_id": 0, "dex_id": 0, "english": "None"})
                continue
            candidates = dex_ids_by_name.get(name, [])
            index = occurrence.get(name, 0)
            occurrence[name] = index + 1
            if not candidates:
                mapped.append({"save_id": int(save_id), "dex_id": -1, "english": name})
                continue
            # A few guild/DLC pieces intentionally share a display name. Both
            # sources keep those duplicates in the same stable order.
            dex_id = candidates[min(index, len(candidates) - 1)]
            mapped.append({"save_id": int(save_id), "dex_id": dex_id})
        armor_crosswalk[file_name] = mapped

    palico_specs = (
        ("palico_weapons", "DB_PeliWpn.csv", "ID_PeliWpn_Name.csv", "PeliWpn_ID", "PeliWpn_Name_0", None, "PalicoWeaponNames", None),
        ("palico_head", "DB_PeliAmr.csv", "ID_PeliAmr_Name.csv", "PeliAmr_ID", "PeliAmr_Name_0", 11, "PalicoHeadNames", "PalicoHeadIDs"),
        ("palico_armor", "DB_PeliAmr.csv", "ID_PeliAmr_Name.csv", "PeliAmr_ID", "PeliAmr_Name_0", 12, "PalicoArmorNames", "PalicoArmorIDs"),
    )
    palico_equipment_crosswalk: dict[str, list[dict[str, object]]] = {}
    for file_name, db_file, names_file, id_key, name_key, part, names_key, ids_key in palico_specs:
        db_rows = read_csv(sql_dir / db_file)
        localized_names = {int(row[id_key]): row[name_key].strip() for row in read_csv(sql_dir / names_file)}
        dex_ids_by_name: dict[str, list[int]] = {}
        for row in db_rows:
            if part is not None and int(row["Part"]) != part:
                continue
            dex_id = int(row[id_key])
            dex_ids_by_name.setdefault(localized_names[dex_id], []).append(dex_id)
        for ids in dex_ids_by_name.values():
            ids.sort()
        names = game_data[names_key]
        save_ids = game_data[ids_key] if ids_key else list(range(len(names)))
        if len(names) != len(save_ids):
            raise ValueError(f"{names_key}: name/save-ID lengths differ")
        occurrence: dict[str, int] = {}
        mapped = []
        for save_id, name in zip(save_ids, names):
            if int(save_id) == 0:
                mapped.append({"save_id": 0, "dex_id": 0, "english": "None"})
                continue
            candidates = dex_ids_by_name.get(name, [])
            index = occurrence.get(name, 0)
            occurrence[name] = index + 1
            if candidates:
                mapped.append({"save_id": int(save_id), "dex_id": candidates[min(index, len(candidates) - 1)]})
            else:
                mapped.append({"save_id": int(save_id), "dex_id": -1, "english": name})
        palico_equipment_crosswalk[file_name] = mapped

    result = {
        "format": "mhxx-save-data-crosswalk-v1",
        "sources": {
            "game_data_origin": "https://github.com/Gicotto/Cotto-MHGU-Editor",
            "game_data_commit": "3e2f9ba421eb4c811003d418f3d489be7bf64bcb",
            "game_data_sha256": sha256(args.game_data),
            "dex_manifest_sha256": sha256(args.dex_raw / "manifest.json"),
        },
        "weapons": weapon_crosswalk,
        "armor": armor_crosswalk,
        "palico_equipment": palico_equipment_crosswalk,
        "decorations": [
            {"id": identifier, "english": name}
            for identifier, name in zip(game_data["JwlIDs"], game_data["JwlNames"])
        ],
        "talismans": [
            {"id": identifier, "english": name}
            for identifier, name in enumerate(game_data["EquipTalismanNames"])
        ],
        "palico": {
            "fortes": [
                {"id": identifier, "english": name}
                for identifier, name in enumerate(game_data["PalicoForte"])
            ],
            "targets": [
                {"id": identifier, "english": name}
                for identifier, name in enumerate(game_data["PalicoTarget"])
            ],
            "support_moves": tier_map(
                game_data["PalicoSupportMoves"],
                [game_data[f"PalicoSupportMoves{tier}"] for tier in range(1, 4)],
            ),
            "skills": tier_map(
                game_data["PalicoSkills"],
                [game_data[f"PalicoSkills{tier}"] for tier in range(1, 4)],
            ),
            "forte_owned_moves": game_data["PalicoForteOwnedMoves"],
            "forte_owned_skills": game_data["PalicoForteOwnedSkills"],
            "basic_moves": game_data["PalicoBasicMoves"],
            "move_patterns": game_data["PalicoMovePatterns"],
            "skill_patterns": game_data["PalicoSkillPatterns"],
            "limits": {
                "max_learned_moves": game_data["PalicoMaxLearnedMoves"],
                "max_learned_skills": game_data["PalicoMaxLearnedSkills"],
                "charisma_fixed_move_slots": 3,
                "other_forte_fixed_move_slots": 4,
            },
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
