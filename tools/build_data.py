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


GENERATOR_VERSION = "2.3.0"
GAME_EXPORT_FORMAT = "mhxx-game-resource-export-v3"
GAME_SOURCE = "mhxx-romfs-game-array"
GAME_RULE_SOURCE = "mhxx-romfs-native-table"
GAME_ONLY_TRANSLATION_SOURCE = "mhxx-reviewed-game-only-translation"
DEX_SOURCE = "mhxx-dex-1.0"
DEX_FALLBACK_SOURCE = "mhxx-dex-1.0-en-fallback"
SAVE_SOURCE = "mhxx-save-format-community"
SAVE_FALLBACK_SOURCE = "mhxx-save-format-community-en-fallback"
TALISMAN_RULE_SOURCE = "kiranico-mhgu-charm-table-crosschecked-by-mhxx-save-editors"
MARKER = "<!-- generated-by: tools/build_data.py -->"

BASE_COLUMNS = ("id", "name", "english", "source")
EQUIPMENT_COLUMNS = BASE_COLUMNS + ("rarity",)
ARMOR_COLUMNS = EQUIPMENT_COLUMNS + ("slots",)
DECORATION_COLUMNS = BASE_COLUMNS + ("slot_cost",)

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
    (11, "重弩", "Heavy Bowgun"),
    # 12 is an unused/ignored equipment code in the save format.
    (13, "轻弩", "Light Bowgun"),
    (14, "太刀", "Long Sword"),
    (15, "斩击斧", "Switch Axe"),
    (16, "铳枪", "Gunlance"),
    (17, "弓", "Bow"),
    (18, "双剑", "Dual Blades"),
    (19, "狩猎笛", "Hunting Horn"),
    (20, "操虫棍", "Insect Glaive"),
    (21, "盾斧", "Charge Blade"),
)

WEAPON_SAVE_TYPES = {
    "weapon_great_sword": 7,
    "weapon_sword_and_shield": 8,
    "weapon_hammer": 9,
    "weapon_lance": 10,
    "weapon_heavy_bowgun": 11,
    "weapon_light_bowgun": 13,
    "weapon_long_sword": 14,
    "weapon_switch_axe": 15,
    "weapon_gunlance": 16,
    "weapon_bow": 17,
    "weapon_dual_blades": 18,
    "weapon_hunting_horn": 19,
    "weapon_insect_glaive": 20,
    "weapon_charge_blade": 21,
}

LOOKUPS = (
    ("ID_Wpn_AxePhial.csv", "Wpn_AxePhial_ID", "Wpn_AxePhial_", "weapon_special", "15,21", "phial"),
    ("ID_Wpn_BowShot.csv", "Wpn_BowShot_ID", "Wpn_BowShot_", "weapon_special", "17", "arc_type"),
    ("ID_Wpn_GunRecoil.csv", "Wpn_GunRecoil_ID", "Wpn_GunRecoil_", "weapon_special", "11,13", "recoil"),
    ("ID_Wpn_GunReloadSpd.csv", "Wpn_GunReloadSpd_ID", "Wpn_GunReloadSpd_", "weapon_special", "11,13", "reload_speed"),
    ("ID_Wpn_GunSteadiness.csv", "Wpn_GunSteadiness_ID", "Wpn_GunSteadiness_", "weapon_special", "11,13", "deviation"),
    ("ID_Wpn_HHSongCategory.csv", "Wpn_HHSongCategory_ID", "Wpn_HHSongCategory_", "weapon_special", "19", "song_category"),
    ("ID_Wpn_ISKinsectType.csv", "Wpn_ISKinsectType_ID", "Wpn_ISKinsectType_", "kinsect", "20", "damage_type"),
    ("ID_Wpn_RapidFireGap.csv", "Wpn_RapidFireGap_ID", "Wpn_RapidFireGap_", "weapon_special", "13", "rapid_fire_gap"),
    ("ID_Wpn_RecitalEffect.csv", "Wpn_RecitalEffect_ID", "Wpn_RecitalEffect_", "weapon_special", "19", "recital_effect"),
    ("ID_Wpn_ShotType.csv", "Wpn_ShotType_ID", "Wpn_ShotType_", "weapon_special", "16,17", "shot_type"),
    ("ID_Wpn_SpAtk.csv", "Wpn_SpAtk_ID", "Wpn_SpAtk_", "attribute_type", "7-21", "dex"),
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


def index_talisman_limits(reference: dict) -> tuple[list[int], dict[int, dict[str, object]]]:
    if reference.get("format") != "mhxx-talisman-skill-limits-v1":
        raise ValueError("unsupported talisman skill-limit reference")
    columns = reference.get("columns")
    if not isinstance(columns, list) or len(columns) != 20:
        raise ValueError("talisman skill-limit columns differ")
    rows = reference.get("skills")
    if not isinstance(rows, list):
        raise ValueError("talisman skill-limit rows are missing")
    if reference.get("source") != TALISMAN_RULE_SOURCE:
        raise ValueError("talisman skill-limit source differs")
    if any(not isinstance(row, list) or len(row) != len(columns) for row in rows):
        raise ValueError("talisman skill-limit row width differs")
    indexed = {int(row[0]): dict(zip(columns, row)) for row in rows}
    if len(indexed) != len(rows) or set(indexed) != set(range(206)):
        raise ValueError("talisman skill-limit IDs must be exactly 0..205")
    rarity = [int(value) for value in reference.get("talisman_rarity", [])]
    if len(rarity) != 11 or rarity[0] != 0 or set(rarity[1:]) != {97, 98, 99, 100}:
        raise ValueError("talisman rarity mapping differs")
    return rarity, indexed


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


def require_game_match(label: str, game_name: str, dex_name: str) -> None:
    if game_name.strip() != strip_level(dex_name):
        raise ValueError(
            f"{label}: game Japanese name {game_name!r} differs from Dex {dex_name!r}"
        )


MISSING_ITEM_NAMES = {
    0: ("无", "None"),
    93: ("未选择道具", "No Item Selected"),
    132: ("LV1 通常弹", "Normal S Lv1"),
    133: ("LV2 通常弹", "Normal S Lv2"),
    134: ("LV3 通常弹", "Normal S Lv3"),
    135: ("LV1 贯通弹", "Pierce S Lv1"),
    136: ("LV2 贯通弹", "Pierce S Lv2"),
    137: ("LV3 贯通弹", "Pierce S Lv3"),
    138: ("LV1 散弹", "Pellet S Lv1"),
    139: ("LV2 散弹", "Pellet S Lv2"),
    140: ("LV3 散弹", "Pellet S Lv3"),
    141: ("LV1 彻甲榴弹", "Crag S Lv1"),
    142: ("LV2 彻甲榴弹", "Crag S Lv2"),
    143: ("LV3 彻甲榴弹", "Crag S Lv3"),
    144: ("LV1 扩散弹", "Clust S Lv1"),
    145: ("LV2 扩散弹", "Clust S Lv2"),
    146: ("LV3 扩散弹", "Clust S Lv3"),
    147: ("LV1 火炎弹", "Flaming S Lv1"),
    148: ("LV1 水冷弹", "Water S Lv1"),
    149: ("LV1 电击弹", "Thunder S Lv1"),
    150: ("LV1 冰结弹", "Freeze S Lv1"),
    151: ("LV1 灭龙弹", "Dragon S Lv1"),
    152: ("LV1 毒弹", "Poison S Lv1"),
    153: ("LV2 毒弹", "Poison S Lv2"),
    154: ("LV1 麻痹弹", "Para S Lv1"),
    155: ("LV2 麻痹弹", "Para S Lv2"),
    156: ("LV1 睡眠弹", "Sleep S Lv1"),
    157: ("LV2 睡眠弹", "Sleep S Lv2"),
    158: ("LV1 减气弹", "Exhaust S Lv1"),
    159: ("LV2 减气弹", "Exhaust S Lv2"),
    160: ("LV1 回复弹", "Recover S Lv1"),
    161: ("LV2 回复弹", "Recover S Lv2"),
    162: ("染色弹", "Paint S"),
    163: ("捕获用麻醉弹", "Tranq S"),
    205: ("RAPID", "RAPID"),
    206: ("无瓶", "No Coating"),
}

GAME_ONLY_WEAPON_NAMES = {
    ("weapon_charge_blade", 59): ("スタールークアクス", "星光之斧", "Starlight Axe"),
    ("weapon_dual_blades", 99): ("双星の紅蓮刃", "双星红莲刃", "Twin Star Blades"),
    ("weapon_hunting_horn", 88): ("ぐでたまフライパン", "懒蛋蛋平底锅", "Gudetama Frying Pan"),
    ("weapon_long_sword", 100): ("スターライトゲート", "星光之门", "Starlight Gate"),
    ("weapon_long_sword", 126): ("気炎の太刀", "气焰太刀", "Flame Katana"),
    ("weapon_switch_axe", 109): ("レッドトマホーク", "红色战斧", "Red Tomahawk"),
    ("weapon_sword_and_shield", 100): ("スターナイトソード", "星光骑士剑", "Star Knight Sword"),
}


def missing_item_name(identifier: int, japanese: str) -> tuple[str, str]:
    if identifier in MISSING_ITEM_NAMES:
        return MISSING_ITEM_NAMES[identifier]
    match = re.fullmatch(r"ダミー珠(\d+)", japanese)
    if match:
        number = int(match.group(1))
        return f"DUMMY珠{number}", f"DUMMY Jewel {number}"
    match = re.fullmatch(r"ダミー(\d+)", japanese)
    if match:
        number = int(match.group(1))
        return f"DUMMY {number}", f"DUMMY {number}"
    raise ValueError(f"game item ID {identifier} {japanese!r} has no Dex or explicit placeholder mapping")


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
    game_tables: dict[str, list[str]],
    game_rules: dict[str, object],
    palico_translations: dict[str, dict[int, dict]],
    talisman_rarity: list[int],
    talisman_limits: dict[int, dict[str, object]],
    language: str,
    output: Path,
) -> dict[str, int]:
    counts: dict[str, int] = {}
    item_data = {int(row["Itm_ID"]): row for row in read_csv(sql_dir / "DB_Itm.csv")}
    item_names = {int(row["Itm_ID"]): row for row in read_csv(sql_dir / "ID_Itm_Name.csv")}
    items: list[dict[str, object]] = []
    item_by_save_id: dict[int, dict[str, object]] = {}
    item_dex_by_save_id = {int(row["Hex"], 16): (dex_id, row) for dex_id, row in item_data.items()}
    for save_id, japanese in enumerate(game_tables["items"]):
        dex = item_dex_by_save_id.get(save_id)
        if dex is None:
            chinese, english = missing_item_name(save_id, japanese)
            built = {
                "id": save_id,
                "name": chinese if language == "cn" else english,
                "english": english,
                "source": GAME_SOURCE,
            }
        else:
            dex_id, _row = dex
            require_game_match(f"item ID {save_id}", japanese, item_names[dex_id]["Itm_Name_3"])
            name, english, source = localized(item_names[dex_id], "Itm_Name_", language)
            built = {"id": save_id, "name": name, "english": english, "source": GAME_SOURCE + "+" + source}
        items.append(built)
        item_by_save_id[save_id] = built
    counts["items.csv"] = write_csv(output / "items.csv", BASE_COLUMNS, items)

    skill_rows = [none_row(language)]
    for row in read_csv(sql_dir / "ID_SklTree_Name.csv"):
        identifier = int(row["SklTree_ID"])
        if identifier <= 0:
            continue
        name, english, source = localized(row, "SklTree_Name_", language)
        skill_rows.append({"id": identifier, "name": name, "english": english, "source": source})
    counts["skills.csv"] = write_csv(output / "skills.csv", BASE_COLUMNS, skill_rows)

    skill_by_id = {int(row["id"]): row for row in skill_rows}
    if set(skill_by_id) != set(talisman_limits):
        raise ValueError("talisman skill-limit IDs differ from generated skills")
    for identifier, limit in talisman_limits.items():
        if skill_by_id[identifier]["english"] != limit["english"]:
            raise ValueError(f"talisman skill-limit English mismatch at ID {identifier}")

    decoration_costs = {
        int(row["item_id"]): int(row["slots"])
        for row in game_rules["decoration_slot_costs"]
    }
    decorations = [{**none_row(language), "slot_cost": 0}]
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
                    "slot_cost": decoration_costs.get(identifier, -1),
                }
            )
            continue
        decorations.append({
            **row,
            "source": row["source"] + "+" + SAVE_SOURCE + "+" + GAME_RULE_SOURCE,
            "slot_cost": decoration_costs.get(identifier, -1),
        })
    counts["decorations.csv"] = write_csv(
        output / "decorations.csv", DECORATION_COLUMNS, decorations
    )

    armor_data = {int(row["Amr_ID"]): row for row in read_csv(sql_dir / "DB_Amr.csv")}
    armor_names = {int(row["Amr_ID"]): row for row in read_csv(sql_dir / "ID_Amr_Name.csv")}
    for file_name, _part in ARMOR:
        table_key = Path(file_name).stem
        game_names = game_tables[table_key]
        native_slots = {
            int(row["armor_id"]): int(row["slots"])
            for row in game_rules["armor_slots"][table_key]
        }
        if set(native_slots) != set(range(len(game_names))):
            raise ValueError(f"{table_key}: native slot table does not cover every save ID")
        entries = {int(entry["save_id"]): entry for entry in crosswalk["armor"][table_key]}
        valid_ids = {
            identifier for identifier, japanese in enumerate(game_names)
            if identifier == 0 or japanese not in {"装備なし", "装備無し"}
        }
        if set(entries) != valid_ids:
            raise ValueError(
                f"{table_key}: crosswalk IDs differ from non-empty game-resource IDs; "
                f"missing={sorted(valid_ids - set(entries))[:10]}, extra={sorted(set(entries) - valid_ids)[:10]}"
            )
        rows = []
        for save_id, japanese in enumerate(game_names):
            if save_id not in valid_ids:
                continue
            entry = entries[save_id]
            dex_id = int(entry["dex_id"])
            if save_id == 0:
                rows.append({
                    **none_row(language, equipment=True),
                    "source": GAME_SOURCE + "+" + GAME_RULE_SOURCE,
                    "slots": native_slots[save_id],
                })
            elif dex_id < 0:
                english = str(entry["english"])
                rows.append({
                    "id": save_id,
                    "name": english,
                    "english": english,
                    "source": GAME_SOURCE + "+" + SAVE_SOURCE + "+" + GAME_RULE_SOURCE,
                    "rarity": 0,
                    "slots": native_slots[save_id],
                })
            else:
                require_game_match(
                    f"{table_key} ID {save_id}", japanese, armor_names[dex_id]["Amr_Name_3"]
                )
                if int(armor_data[dex_id]["Slot"]) != native_slots[save_id]:
                    raise ValueError(
                        f"{table_key} ID {save_id}: native slot count differs from Dex crosswalk"
                    )
                name, english, source = localized(armor_names[dex_id], "Amr_Name_", language)
                rows.append({
                    "id": save_id,
                    "name": name,
                    "english": english,
                    "source": GAME_SOURCE + "+" + source + "+" + SAVE_SOURCE + "+" + GAME_RULE_SOURCE,
                    "rarity": int(armor_data[dex_id]["Rare"]),
                    "slots": native_slots[save_id],
                })
        counts[file_name] = write_csv(output / file_name, ARMOR_COLUMNS, rows)

    weapon_data = {int(row["Wpn_ID"]): row for row in read_csv(sql_dir / "DB_Wpn.csv")}
    weapon_names = {int(row["Wpn_ID"]): row for row in read_csv(sql_dir / "ID_Wpn_Name.csv")}
    weapon_columns = EQUIPMENT_COLUMNS + ("max_level",)
    for base_name, mapping in crosswalk["weapons"].items():
        game_names = game_tables[base_name]
        entries = {int(entry["save_id"]): entry for entry in mapping}
        if set(entries) != set(range(len(game_names))):
            raise ValueError(f"{base_name}: crosswalk does not cover every game-resource ID")
        rows = [{**none_row(language, equipment=True), "source": GAME_SOURCE, "max_level": 0}]
        for save_id, japanese in enumerate(game_names[1:], 1):
            entry = entries[save_id]
            dex_id = int(entry["dex_id"])
            if dex_id < 0:
                translation = GAME_ONLY_WEAPON_NAMES.get((base_name, save_id))
                if translation is None or translation[0] != japanese.strip():
                    raise ValueError(
                        f"{base_name} ID {save_id}: missing reviewed translation for {japanese!r}"
                    )
                _reviewed_japanese, chinese, english = translation
                rows.append(
                    {
                        "id": entry["save_id"],
                        "name": chinese if language == "cn" else english,
                        "english": english,
                        "source": GAME_SOURCE + "+" + GAME_ONLY_TRANSLATION_SOURCE,
                        "rarity": 0,
                        "max_level": entry["max_level"],
                    }
                )
                continue
            require_game_match(
                f"{base_name} ID {save_id}", japanese, weapon_names[dex_id]["Wpn_Name_3"]
            )
            name, english, source = localized(weapon_names[dex_id], "Wpn_Name_", language)
            rows.append(
                {
                    "id": entry["save_id"],
                    "name": strip_level(name),
                    "english": strip_level(english),
                    "source": GAME_SOURCE + "+" + source + "+" + SAVE_SOURCE,
                    "rarity": int(weapon_data[dex_id]["Rare"]),
                    "max_level": entry["max_level"],
                }
            )
        file_name = base_name + ".csv"
        counts[file_name] = write_csv(output / file_name, weapon_columns, rows)

    weapon_level_rows = []
    weapon_level_rules = game_rules["weapon_level_slots"]
    for table_name, equipment_type in sorted(WEAPON_SAVE_TYPES.items(), key=lambda row: row[1]):
        for row in weapon_level_rules[table_name]:
            display_level = int(row["display_level"])
            weapon_level_rows.append({
                "equipment_type": equipment_type,
                "weapon_id": int(row["weapon_id"]),
                "save_level": display_level - 1,
                "display_level": display_level,
                "slots": int(row["slots"]),
                "source": GAME_RULE_SOURCE,
            })
    counts["weapon_level_slots.csv"] = write_csv(
        output / "weapon_level_slots.csv",
        ("equipment_type", "weapon_id", "save_level", "display_level", "slots", "source"),
        weapon_level_rows,
    )

    talismans = []
    for entry in crosswalk["talismans"]:
        identifier = int(entry["id"])
        english = str(entry["english"])
        name = TALISMAN_CN[identifier] if language == "cn" else english
        source = SAVE_SOURCE
        talismans.append({"id": identifier, "name": name, "english": english, "source": source, "rarity": 0})
    counts["talismans.csv"] = write_csv(output / "talismans.csv", EQUIPMENT_COLUMNS, talismans)

    rarity_prefix = {97: "mystery", 98: "shining", 99: "timeworn", 100: "enduring"}
    talisman_limit_rows = []
    for talisman_id in range(1, len(talisman_rarity)):
        prefix = rarity_prefix[talisman_rarity[talisman_id]]
        for skill_id in sorted(talisman_limits):
            limit = talisman_limits[skill_id]
            values = [int(limit[f"{prefix}_{field}"]) for field in (
                "s1_min", "s1_max", "s2_min", "s2_max"
            )]
            if values[0] > values[1] or values[2] > values[3]:
                raise ValueError(f"invalid talisman skill range for {talisman_id}/{skill_id}")
            talisman_limit_rows.append({
                "talisman_id": talisman_id,
                "skill_id": skill_id,
                "skill1_min": values[0],
                "skill1_max": values[1],
                "skill2_min": values[2],
                "skill2_max": values[3],
                "source": TALISMAN_RULE_SOURCE,
            })
    counts["talisman_skill_limits.csv"] = write_csv(
        output / "talisman_skill_limits.csv",
        ("talisman_id", "skill_id", "skill1_min", "skill1_max",
         "skill2_min", "skill2_max", "source"),
        talisman_limit_rows,
    )

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
    palico_weapon_entries = {
        int(entry["save_id"]): entry for entry in crosswalk["palico_equipment"]["palico_weapons"]
    }
    if set(palico_weapon_entries) != set(range(len(game_tables["palico_weapons"]))):
        raise ValueError("Palico weapon crosswalk does not cover every game-resource ID")
    for save_id, japanese in enumerate(game_tables["palico_weapons"]):
        entry = palico_weapon_entries[save_id]
        dex_id = int(entry["dex_id"])
        if save_id == 0:
            palico_weapons.append({**none_row(language, equipment=True), "source": GAME_SOURCE})
        elif dex_id < 0:
            english = str(entry["english"])
            palico_weapons.append({"id": save_id, "name": english, "english": english,
                "source": GAME_SOURCE + "+" + SAVE_SOURCE, "rarity": 0})
        else:
            require_game_match(
                f"palico_weapons ID {save_id}", japanese,
                peli_weapon_names[dex_id]["PeliWpn_Name_3"],
            )
            name, english, source = localized(peli_weapon_names[dex_id], "PeliWpn_Name_", language)
            palico_weapons.append({"id": save_id, "name": name, "english": english,
                "source": GAME_SOURCE + "+" + source + "+" + SAVE_SOURCE,
                "rarity": int(peli_weapon_data[dex_id]["Rare"])})
    counts["palico_weapons.csv"] = write_csv(output / "palico_weapons.csv", EQUIPMENT_COLUMNS, palico_weapons)

    peli_armor_data = {int(row["PeliAmr_ID"]): row for row in read_csv(sql_dir / "DB_PeliAmr.csv")}
    peli_armor_names = {int(row["PeliAmr_ID"]): row for row in read_csv(sql_dir / "ID_PeliAmr_Name.csv")}
    for file_name, part in (("palico_head.csv", 11), ("palico_armor.csv", 12)):
        table_key = Path(file_name).stem
        game_names = game_tables[table_key]
        entries = {
            int(entry["save_id"]): entry for entry in crosswalk["palico_equipment"][table_key]
        }
        valid_ids = {
            identifier for identifier, japanese in enumerate(game_names)
            if japanese != "―"
        }
        if set(entries) != valid_ids:
            raise ValueError(f"{table_key}: crosswalk IDs differ from non-empty game-resource IDs")
        rows = []
        for save_id, japanese in enumerate(game_names):
            if save_id not in valid_ids:
                continue
            entry = entries[save_id]
            dex_id = int(entry["dex_id"])
            if save_id == 0:
                rows.append({**none_row(language, equipment=True), "source": GAME_SOURCE})
            elif dex_id < 0:
                english = str(entry["english"])
                rows.append({"id": save_id, "name": english, "english": english,
                    "source": GAME_SOURCE + "+" + SAVE_SOURCE, "rarity": 0})
            else:
                require_game_match(
                    f"{table_key} ID {save_id}", japanese,
                    peli_armor_names[dex_id]["PeliAmr_Name_3"],
                )
                name, english, source = localized(peli_armor_names[dex_id], "PeliAmr_Name_", language)
                rows.append({"id": save_id, "name": name, "english": english,
                    "source": GAME_SOURCE + "+" + source + "+" + SAVE_SOURCE,
                    "rarity": int(peli_armor_data[dex_id]["Rare"])})
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

- All item, hunter equipment and Palico equipment IDs come from zero-based
  arrays in the game's own `RomFS/table` resources. Dex supplies translations
  and attributes only; its Japanese names must exactly match the game slot.
- `cn/` uses Simplified Chinese names from the pinned MHXX Dex. Game-only
  ammunition and placeholder slots use explicit translations or DUMMY labels.
- `en/` uses English names in both `name` and `english`.
- Equipment crosswalks no longer assign IDs: they attach Dex metadata to the
  game array index and fail generation on any Japanese-name mismatch.
- Each armor CSV records native slot counts from bytes 108 through 112 of the
  127-byte `armorSeriesData.asd` record (head/chest/arms/waist/legs).
- `weapon_level_slots.csv` comes directly from all fourteen native
  `weaponXXLevelData` tables. It records both the zero-based save level and the
  one-based displayed level. `decorations.csv` records native jewel slot cost;
  `-1` marks extra DUMMY IDs absent from `decoData`.
- `talisman_skill_limits.csv` records the legal first/second skill-point ranges
  for all ten talisman grades. The ranges are keyed by explicit save skill ID;
  unavailable skill/position combinations remain `0..0` and are reported. The
  pinned facts are attributed to the
  [Kiranico MHGU charm table](https://mhgu.kiranico.com/charms) and
  cross-checked against two editors.
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
- CCI/RomFS resources, raw Dex runtime dumps, player saves, and reference
  editor source trees are research inputs and are intentionally not committed.

Rebuild and validate:

```bash
python3 tools/export_game_names.py /path/to/extracted/romfs/table /tmp/mhxx-game-resources.json
python3 tools/build_data.py --input /path/to/mhxx-dex-raw --game-names /tmp/mhxx-game-resources.json --output data
python3 tools/validate_data.py data --game-names /tmp/mhxx-game-resources.json
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
    parser.add_argument("--game-names", type=Path, required=True)
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
    parser.add_argument(
        "--talisman-limits",
        type=Path,
        default=Path(__file__).resolve().parent / "reference" / "talisman_skill_limits.json",
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
    talisman_rarity, talisman_limits = index_talisman_limits(read_json(args.talisman_limits))
    game_export = read_json(args.game_names)
    if game_export.get("format") != GAME_EXPORT_FORMAT or game_export.get("language") != "jp":
        raise ValueError("unsupported or invalid MHXX game name export")
    game_tables = game_export.get("tables")
    game_rules = game_export.get("rules")
    required_game_tables = {
        "items", *(Path(file_name).stem for file_name, _part in ARMOR),
        *crosswalk["weapons"].keys(), "palico_weapons", "palico_head", "palico_armor",
    }
    if not isinstance(game_tables, dict) or set(game_tables) != required_game_tables:
        raise ValueError("game name export table set differs from the required save-ID tables")
    if not isinstance(game_rules, dict) or set(game_rules) != {
        "armor_slots", "weapon_level_slots", "decoration_slot_costs"
    }:
        raise ValueError("game resource export rule set differs from the required native rules")
    if set(game_rules["armor_slots"]) != {
        Path(file_name).stem for file_name, _part in ARMOR
    }:
        raise ValueError("armor slot rule tables differ from the armor tables")
    if set(game_rules["weapon_level_slots"]) != set(crosswalk["weapons"]):
        raise ValueError("weapon level rule tables differ from the weapon crosswalk")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    temp_root = Path(tempfile.mkdtemp(prefix="mhxx-data-", dir=str(args.output.parent)))
    try:
        counts = {
            language: build_language(
                args.input / "direct_sql", crosswalk, game_tables, game_rules,
                palico_translations, talisman_rarity, talisman_limits,
                language, temp_root / language
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
                "talisman_skill_limits_sha256": sha256(args.talisman_limits),
            },
            "game_resource": {
                "format": game_export["format"],
                "language": game_export["language"],
                "export_sha256": sha256(args.game_names),
                "sources": game_export.get("sources", []),
                "tables": {key: len(game_tables[key]) for key in sorted(game_tables)},
                "rules": {
                    "armor_slots": sum(
                        len(rows) for rows in game_rules["armor_slots"].values()
                    ),
                    "weapon_level_slots": sum(
                        len(rows) for rows in game_rules["weapon_level_slots"].values()
                    ),
                    "decoration_slot_costs": len(game_rules["decoration_slot_costs"]),
                },
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
