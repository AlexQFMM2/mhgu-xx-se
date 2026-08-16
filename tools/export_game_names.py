#!/usr/bin/env python3
"""Export authoritative MHXX save-ID arrays and native rules from RomFS/table.

The input directory is kept outside the repository. The deterministic JSON
contains only canonical Japanese labels, normalized weapon/decorations rules,
reviewed table counts, and hashes of the small source tables used to derive them.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path


FORMAT = "mhxx-game-resource-export-v4"
GMD_MAGIC = b"GMD\0"

ARMOR_RECORD_SIZE = 127
ARMOR_SLOT_OFFSETS = {
    "armor_head": 108,
    "armor_chest": 109,
    "armor_arms": 110,
    "armor_waist": 111,
    "armor_legs": 112,
}

WEAPONS = {
    "weapon_great_sword": 0,
    "weapon_sword_and_shield": 1,
    "weapon_hammer": 2,
    "weapon_lance": 3,
    "weapon_heavy_bowgun": 4,
    "weapon_light_bowgun": 6,
    "weapon_long_sword": 7,
    "weapon_switch_axe": 8,
    "weapon_gunlance": 9,
    "weapon_bow": 10,
    "weapon_dual_blades": 11,
    "weapon_hunting_horn": 12,
    "weapon_insect_glaive": 13,
    "weapon_charge_blade": 14,
}

# weaponXXLevelData has a common 8-byte header, but its record layout depends
# on the weapon class. The save stores level zero-based; this table stores the
# displayed level one-based.
WEAPON_LEVEL_LAYOUTS = {
    0: (15, 14),   # Great Sword
    1: (15, 14),   # Sword and Shield
    2: (15, 14),   # Hammer
    3: (15, 14),   # Lance
    4: (100, 10),  # Heavy Bowgun
    6: (100, 10),  # Light Bowgun
    7: (15, 14),   # Long Sword
    8: (17, 16),   # Switch Axe
    9: (16, 15),   # Gunlance
    10: (31, 12),  # Bow
    11: (17, 16),  # Dual Blades
    12: (16, 15),  # Hunting Horn
    13: (15, 14),  # Insect Glaive
    14: (17, 16),  # Charge Blade
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def source(path: Path) -> dict[str, object]:
    return {"file": path.name, "size": path.stat().st_size, "sha256": sha256(path)}


def read_gmd(path: Path) -> list[str]:
    data = path.read_bytes()
    if len(data) < 41 or data[:4] != GMD_MAGIC:
        raise ValueError(f"{path}: not a GMD message table")
    count = struct.unpack_from("<I", data, 0x18)[0]
    payload_size = struct.unpack_from("<I", data, 0x20)[0]
    label_length = struct.unpack_from("<I", data, 0x24)[0]
    start = 0x29 + label_length
    end = start + payload_size
    if end != len(data):
        raise ValueError(f"{path}: GMD payload size differs from file size")
    raw = data[start:end].split(b"\0")
    if raw[-1] != b"":
        raise ValueError(f"{path}: GMD payload has no final terminator")
    values = [value.decode("utf-8", errors="strict") for value in raw[:-1]]
    if len(values) != count:
        raise ValueError(f"{path}: expected {count} strings, decoded {len(values)}")
    if any(not value for value in values):
        raise ValueError(f"{path}: GMD contains an empty message")
    return values


def read_counted_binary(path: Path) -> int:
    data = path.read_bytes()
    if len(data) < 8:
        raise ValueError(f"{path}: truncated counted table")
    count = struct.unpack_from("<I", data, 4)[0]
    if count == 0 or (len(data) - 8) % count:
        raise ValueError(f"{path}: invalid record count/size")
    return count


def read_skill_names(gmd_path: Path, binary_path: Path) -> list[str]:
    """Read the save-ID skill-tree array used by equipment and charms."""
    messages = read_gmd(gmd_path)
    count = read_counted_binary(binary_path)
    if len(messages) != count * 2:
        raise ValueError(
            f"{gmd_path}: expected two messages for each of {count} skill trees"
        )
    return grouped_names(messages, 2, 0, gmd_path.name)


def read_talisman_skill_limits(root: Path, skill_count: int) -> list[dict[str, int]]:
    """Read the game's eight positional charm-skill range tables.

    Files 00/01 are mystery first/second skill, 02/03 shining,
    04/05 timeworn and 06/07 enduring. Missing IDs have a native 0..0
    range. The skill IDs in these records index skillTypeData directly.
    """
    result = [
        {
            "save_skill_id": skill_id,
            **{
                f"{rarity}_s{position}_{bound}": 0
                for rarity in ("mystery", "shining", "timeworn", "enduring")
                for position in (1, 2)
                for bound in ("min", "max")
            },
        }
        for skill_id in range(skill_count)
    ]
    for table_index in range(8):
        path = root / f"amuletSkillData{table_index:02d}.amskl"
        data = path.read_bytes()
        if len(data) < 8:
            raise ValueError(f"{path}: truncated talisman skill table")
        count = struct.unpack_from("<I", data, 4)[0]
        if len(data) != 8 + count * 4:
            raise ValueError(f"{path}: talisman skill table size mismatch")
        rarity = ("mystery", "shining", "timeworn", "enduring")[table_index // 2]
        position = table_index % 2 + 1
        seen: set[int] = set()
        for index in range(count):
            skill_id, minimum, maximum = struct.unpack_from("<Hbb", data, 8 + index * 4)
            if not 0 < skill_id < skill_count:
                raise ValueError(f"{path}: invalid skill ID {skill_id} at record {index}")
            if skill_id in seen:
                raise ValueError(f"{path}: duplicate skill ID {skill_id}")
            if minimum > maximum:
                raise ValueError(f"{path}: reversed range for skill ID {skill_id}")
            seen.add(skill_id)
            result[skill_id][f"{rarity}_s{position}_min"] = minimum
            result[skill_id][f"{rarity}_s{position}_max"] = maximum
    return result


def read_weapon_map(path: Path) -> int:
    data = path.read_bytes()
    if len(data) < 8:
        raise ValueError(f"{path}: truncated weapon message map")
    count = struct.unpack_from("<I", data, 4)[0]
    if len(data) != 8 + count * 4:
        raise ValueError(f"{path}: weapon message map size mismatch")
    values = list(struct.unpack_from(f"<{count}I", data, 8))
    if values != list(range(count)):
        raise ValueError(f"{path}: message-map indices are not the save-ID sequence")
    return count


def read_weapon_level_slots(path: Path, record_size: int, slot_offset: int,
                            weapon_count: int) -> list[dict[str, int]]:
    data = path.read_bytes()
    if len(data) < 8:
        raise ValueError(f"{path}: truncated weapon level table")
    count = struct.unpack_from("<I", data, 4)[0]
    if len(data) != 8 + count * record_size:
        raise ValueError(f"{path}: weapon level table size mismatch")
    rows: list[dict[str, int]] = []
    seen: set[tuple[int, int]] = set()
    levels_by_weapon: dict[int, list[int]] = {}
    for index in range(count):
        offset = 8 + index * record_size
        weapon_id = data[offset + 4]
        display_level = data[offset + 5]
        slots = data[offset + slot_offset]
        key = (weapon_id, display_level)
        if weapon_id >= weapon_count:
            raise ValueError(f"{path}: weapon ID {weapon_id} is outside the name table")
        if not 1 <= display_level <= 15:
            raise ValueError(f"{path}: invalid displayed level {display_level} at record {index}")
        if not 0 <= slots <= 3:
            raise ValueError(f"{path}: invalid slot count {slots} at record {index}")
        if key in seen:
            raise ValueError(f"{path}: duplicate weapon/level pair {key}")
        seen.add(key)
        levels_by_weapon.setdefault(weapon_id, []).append(display_level)
        rows.append({"weapon_id": weapon_id, "display_level": display_level, "slots": slots})
    if set(levels_by_weapon) != set(range(weapon_count)):
        raise ValueError(f"{path}: level table does not cover every weapon save ID")
    for weapon_id, levels in levels_by_weapon.items():
        if sorted(levels) != list(range(1, max(levels) + 1)):
            raise ValueError(f"{path}: weapon ID {weapon_id} has non-contiguous levels")
    return rows


def read_decoration_slot_costs(path: Path) -> list[dict[str, int]]:
    data = path.read_bytes()
    if len(data) < 8:
        raise ValueError(f"{path}: truncated decoration table")
    count = struct.unpack_from("<I", data, 4)[0]
    if len(data) != 8 + count * 5:
        raise ValueError(f"{path}: decoration table size mismatch")
    rows = []
    for index in range(count):
        slots = data[8 + index * 5]
        if not 1 <= slots <= 3:
            raise ValueError(f"{path}: invalid decoration slot cost {slots} at record {index}")
        rows.append({"item_id": 2638 + index, "slots": slots})
    return rows


def read_armor_slots(path: Path) -> dict[str, list[dict[str, int]]]:
    data = path.read_bytes()
    if len(data) < 8:
        raise ValueError(f"{path}: truncated armor series table")
    count = struct.unpack_from("<I", data, 4)[0]
    if len(data) != 8 + count * ARMOR_RECORD_SIZE:
        raise ValueError(f"{path}: armor series table size mismatch")
    result: dict[str, list[dict[str, int]]] = {}
    for table, slot_offset in ARMOR_SLOT_OFFSETS.items():
        rows = []
        for armor_id in range(count):
            slots = data[8 + armor_id * ARMOR_RECORD_SIZE + slot_offset]
            if not 0 <= slots <= 3:
                raise ValueError(
                    f"{path}: invalid {table} slot count {slots} at save ID {armor_id}"
                )
            rows.append({"armor_id": armor_id, "slots": slots})
        result[table] = rows
    return result


def grouped_names(values: list[str], block: int, name_index: int, label: str) -> list[str]:
    if len(values) % block:
        raise ValueError(f"{label}: {len(values)} messages is not divisible by {block}")
    return [values[offset + name_index] for offset in range(0, len(values), block)]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("table_dir", type=Path, help="extracted MHXX RomFS/table directory")
    parser.add_argument("output", type=Path, help="deterministic resource JSON outside the repository")
    args = parser.parse_args()

    root = args.table_dir.resolve()
    used: list[Path] = []

    item_gmd = root / "itemData_jpn.gmd"
    item_binary = root / "itemData.itm"
    item_names = grouped_names(read_gmd(item_gmd), 2, 0, item_gmd.name)
    if read_counted_binary(item_binary) != len(item_names):
        raise ValueError("itemData.itm count differs from itemData_jpn.gmd")
    used.extend((item_gmd, item_binary))

    armor_gmd = root / "armorSeriesData_jpn.gmd"
    armor_binary = root / "armorSeriesData.asd"
    armor_messages = read_gmd(armor_gmd)
    armor_slots = read_armor_slots(armor_binary)
    armor_count = len(armor_slots["armor_head"])
    if len(armor_messages) != armor_count * 10:
        raise ValueError("armor message count is not ten messages per save ID")
    armor_keys = ("armor_head", "armor_chest", "armor_arms", "armor_waist", "armor_legs")
    armor = {
        key: grouped_names(armor_messages, 10, part, armor_gmd.name)
        for part, key in enumerate(armor_keys)
    }
    used.extend((armor_gmd, armor_binary))

    weapons: dict[str, list[str]] = {}
    weapon_level_slots: dict[str, list[dict[str, int]]] = {}
    for key, code in WEAPONS.items():
        gmd = root / f"weapon{code:02d}MsgData_jpn.gmd"
        message_map = root / f"weapon{code:02d}MsgData.w{code:02d}m"
        names = grouped_names(read_gmd(gmd), 6, 0, gmd.name)
        if read_weapon_map(message_map) != len(names):
            raise ValueError(f"{key}: message-map count differs from GMD")
        weapons[key] = names
        used.extend((gmd, message_map))
        level_data = root / f"weapon{code:02d}LevelData.w{code:02d}d"
        record_size, slot_offset = WEAPON_LEVEL_LAYOUTS[code]
        weapon_level_slots[key] = read_weapon_level_slots(
            level_data, record_size, slot_offset, len(names)
        )
        used.append(level_data)

    decoration_binary = root / "decoData.deco"
    decoration_slot_costs = read_decoration_slot_costs(decoration_binary)
    used.append(decoration_binary)

    skill_gmd = root / "skillTypeData_jpn.gmd"
    skill_binary = root / "skillTypeData.skt"
    skills = read_skill_names(skill_gmd, skill_binary)
    if len(skills) != 206 or skills[0] != "なし":
        raise ValueError("skillTypeData is not the expected 206-entry save-ID array")
    talisman_skill_limits = read_talisman_skill_limits(root, len(skills))
    used.extend((skill_gmd, skill_binary))
    used.extend(root / f"amuletSkillData{index:02d}.amskl" for index in range(8))

    palico_weapon_gmd = root / "otWeaponData_jpn.gmd"
    palico_weapon_binary = root / "otWeaponData.owp"
    palico_weapons = grouped_names(read_gmd(palico_weapon_gmd), 2, 0, palico_weapon_gmd.name)
    if read_counted_binary(palico_weapon_binary) != len(palico_weapons):
        raise ValueError("Palico weapon binary/GMD counts differ")
    used.extend((palico_weapon_gmd, palico_weapon_binary))

    palico_armor_gmd = root / "otArmorData_jpn.gmd"
    palico_armor_binary = root / "otArmorData.oar"
    palico_armor_messages = read_gmd(palico_armor_gmd)
    palico_armor_count = read_counted_binary(palico_armor_binary)
    if len(palico_armor_messages) != palico_armor_count * 4:
        raise ValueError("Palico armor message count is not four messages per save ID")
    palico_head = grouped_names(palico_armor_messages, 4, 0, palico_armor_gmd.name)
    palico_armor = grouped_names(palico_armor_messages, 4, 1, palico_armor_gmd.name)
    used.extend((palico_armor_gmd, palico_armor_binary))

    payload = {
        "format": FORMAT,
        "language": "jp",
        "sources": [source(path) for path in sorted(used, key=lambda path: path.name)],
        "tables": {
            "items": item_names,
            **armor,
            **weapons,
            "palico_weapons": palico_weapons,
            "palico_head": palico_head,
            "palico_armor": palico_armor,
            "skills": skills,
        },
        "rules": {
            "armor_slots": armor_slots,
            "weapon_level_slots": weapon_level_slots,
            "decoration_slot_costs": decoration_slot_costs,
            "talisman_skill_limits": talisman_skill_limits,
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    print(
        f"exported {len(item_names)} items, {armor_count} armor IDs, "
        f"{sum(len(rows) for rows in weapons.values())} weapon IDs, "
        f"{sum(len(rows) for rows in weapon_level_slots.values())} weapon levels"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
