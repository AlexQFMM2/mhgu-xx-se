#!/usr/bin/env python3
"""Export the compact MHXX Palico rule crosswalk from native RomFS tables."""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path


def payload(path: Path, record_size: int) -> list[bytes]:
    data = path.read_bytes()
    if len(data) < 8 or struct.unpack_from("<f", data)[0] != 1.0:
        raise ValueError(f"{path}: unsupported native-table header")
    count = struct.unpack_from("<I", data, 4)[0]
    if len(data) != 8 + count * record_size:
        raise ValueError(f"{path}: native-table size/count mismatch")
    return [data[8 + index * record_size:8 + (index + 1) * record_size] for index in range(count)]


def groups(root: Path, prefix: str) -> dict[str, list[list[object]]]:
    result: dict[str, list[list[object]]] = {}
    for points, group in ((3, "A"), (2, "B"), (1, "C")):
        rows = payload(root / f"ot{prefix}Ini{points}pt.otil", 6)
        result[group] = [[struct.unpack_from("<H", row)[0], list(row[2:])] for row in rows]
    return result


def patterns(root: Path, name: str) -> list[list[object]]:
    rows = payload(root / name, 10)
    letters = {3: "A", 2: "B", 1: "C"}
    return [[index, row[0], "".join(letters[value] for value in row[1:] if value)]
            for index, row in enumerate(rows)]


def own_support(root: Path) -> tuple[list[int], list[list[int]]]:
    rows = payload(root / "otLotOwnSupport.olos", 8)
    primary: list[int] = []
    secondary: list[list[int]] = []
    for forte, row in enumerate(rows):
        values = list(struct.unpack("<4H", row))
        if values[0] != forte:
            raise ValueError("otLotOwnSupport forte order differs")
        primary.append(values[1])
        secondary.append(list(dict.fromkeys(value for value in values[2:] if value)))
    return primary, secondary


def own_skills(root: Path) -> list[list[int]]:
    rows = payload(root / "otLotOwnSkill.olsk", 6)
    result: list[list[int]] = []
    for forte, row in enumerate(rows):
        values = list(struct.unpack("<3H", row))
        if values[0] != forte:
            raise ValueError("otLotOwnSkill forte order differs")
        result.append(values[1:])
    return result


def export(root: Path) -> dict[str, object]:
    primary, secondary = own_support(root)
    return {
        "format": "mhxx-palico-native-rules-v1",
        "source": "mhxx-romfs-native-table",
        "support_groups": groups(root, "Support"),
        "skill_groups": groups(root, "Skill"),
        "patterns": {
            "normal_move": patterns(root, "otSupportPoint.otpt"),
            "charisma_move": patterns(root, "otSupportPointSp.otpt"),
            "skill": patterns(root, "otSkillPoint.otpt"),
        },
        "fixed": {
            "common_support": [9, 1],
            "primary_support": primary,
            "secondary_support": secondary,
            "fixed_skills": own_skills(root),
        },
        "valid_length": {
            "normal_move_fixed": 4, "normal_move_transfer": 2,
            "charisma_move_fixed": 3, "charisma_move_transfer": 3,
            "skill_fixed": 2, "skill_transfer": 2,
        },
        "sentinels": {"support": 57, "skill": 96},
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("table_dir", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--verify", type=Path)
    args = parser.parse_args()
    result = export(args.table_dir)
    if args.verify:
        expected = json.loads(args.verify.read_text(encoding="utf-8"))
        if result != expected:
            raise ValueError("native Palico tables differ from the committed rule crosswalk")
    if args.output:
        args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if not args.output and not args.verify:
        print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
