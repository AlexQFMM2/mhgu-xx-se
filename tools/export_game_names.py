#!/usr/bin/env python3
"""Export authoritative MHXX save-ID name arrays from an extracted RomFS table.

The input directory is kept outside the repository. The deterministic JSON
contains only canonical Japanese labels, reviewed table counts, and hashes of
the small source tables used to derive them.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path


FORMAT = "mhxx-game-name-export-v1"
GMD_MAGIC = b"GMD\0"

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


def grouped_names(values: list[str], block: int, name_index: int, label: str) -> list[str]:
    if len(values) % block:
        raise ValueError(f"{label}: {len(values)} messages is not divisible by {block}")
    return [values[offset + name_index] for offset in range(0, len(values), block)]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("table_dir", type=Path, help="extracted MHXX RomFS/table directory")
    parser.add_argument("output", type=Path, help="deterministic JSON output outside the repository")
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
    armor_count = read_counted_binary(armor_binary)
    if len(armor_messages) != armor_count * 10:
        raise ValueError("armor message count is not ten messages per save ID")
    armor_keys = ("armor_head", "armor_chest", "armor_arms", "armor_waist", "armor_legs")
    armor = {
        key: grouped_names(armor_messages, 10, part, armor_gmd.name)
        for part, key in enumerate(armor_keys)
    }
    used.extend((armor_gmd, armor_binary))

    weapons: dict[str, list[str]] = {}
    for key, code in WEAPONS.items():
        gmd = root / f"weapon{code:02d}MsgData_jpn.gmd"
        message_map = root / f"weapon{code:02d}MsgData.w{code:02d}m"
        names = grouped_names(read_gmd(gmd), 6, 0, gmd.name)
        if read_weapon_map(message_map) != len(names):
            raise ValueError(f"{key}: message-map count differs from GMD")
        weapons[key] = names
        used.extend((gmd, message_map))

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
        f"{sum(len(rows) for rows in weapons.values())} weapon IDs"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
