#!/usr/bin/env python3
"""Offline Wokwi diagram checks that never contact Wokwi."""

from __future__ import annotations

import argparse
import json
import sys
from collections import defaultdict
from pathlib import Path


POWER_PINS = {
    "3V3",
    "3V3.1",
    "3V3.2",
    "5V",
    "GND",
    "GND.1",
    "GND.2",
    "GND.3",
    "GND.4",
    "VCC",
    "VDD",
    "VSS",
    "VBUS",
}


def signal_class(part_type: str, pin: str) -> str:
    pin = pin.upper()
    if "st7789" in part_type:
        if pin in {"SCL", "SCK", "CLK"}:
            return "spi-clock"
        if pin in {"SDA", "MOSI", "DIN"}:
            return "spi-mosi"
    if pin == "SDA":
        return "i2c-sda"
    if pin == "SCL":
        return "i2c-scl"
    return pin.lower()


def endpoint(value: str) -> tuple[str, str]:
    if ":" not in value:
        raise ValueError(f"invalid endpoint {value!r}")
    return tuple(value.split(":", 1))  # type: ignore[return-value]


def is_board(part_type: str) -> bool:
    return part_type.startswith("board-") or part_type == "wokwi-custom-board"


def check(path: Path) -> list[str]:
    errors: list[str] = []
    try:
        diagram = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return [f"cannot read diagram: {exc}"]

    parts = diagram.get("parts", [])
    by_id: dict[str, dict] = {}
    for part in parts:
        part_id = part.get("id")
        if not part_id:
            errors.append("part without id")
            continue
        if part_id in by_id:
            errors.append(f"duplicate part id: {part_id}")
        by_id[part_id] = part

    repo = path.parent
    custom_pins: dict[str, set[str]] = {}
    board_pins: dict[str, set[str]] = {}
    for part in parts:
        part_type = str(part.get("type", ""))
        part_id = str(part.get("id", ""))
        if part_type == "wokwi-custom-board":
            descriptor = repo / "wokwi" / "boards" / part_id / "board.json"
            if not descriptor.is_file():
                errors.append(f"{part_id}: missing custom board descriptor {descriptor.relative_to(repo)}")
                continue
            try:
                metadata = json.loads(descriptor.read_text(encoding="utf-8"))
                board_pins[part_id] = set(metadata.get("pins", {}))
            except (OSError, json.JSONDecodeError) as exc:
                errors.append(f"{part_id}: invalid custom board descriptor: {exc}")
            continue
        if not part_type.startswith("chip-"):
            continue
        name = part_type.removeprefix("chip-")
        source = repo / "wokwi" / f"{name}.chip.c"
        descriptor = repo / "wokwi" / f"{name}.chip.json"
        if not source.is_file():
            errors.append(f"{part_type}: missing {source.relative_to(repo)}")
        if not descriptor.is_file():
            errors.append(f"{part_type}: missing {descriptor.relative_to(repo)}")
            continue
        try:
            metadata = json.loads(descriptor.read_text(encoding="utf-8"))
            custom_pins[part_type] = set(metadata.get("pins", []))
        except (OSError, json.JSONDecodeError) as exc:
            errors.append(f"{part_type}: invalid descriptor: {exc}")

    board_uses: dict[str, list[tuple[str, str, str]]] = defaultdict(list)
    for index, connection in enumerate(diagram.get("connections", []), start=1):
        if not isinstance(connection, list) or len(connection) < 2:
            errors.append(f"connection {index}: expected two endpoints")
            continue
        try:
            left_id, left_pin = endpoint(connection[0])
            right_id, right_pin = endpoint(connection[1])
        except ValueError as exc:
            errors.append(f"connection {index}: {exc}")
            continue
        if left_id not in by_id or right_id not in by_id:
            errors.append(f"connection {index}: unknown part in {connection[0]} -> {connection[1]}")
            continue

        for part_id, pin in ((left_id, left_pin), (right_id, right_pin)):
            part_type = str(by_id[part_id].get("type", ""))
            allowed = custom_pins.get(part_type)
            if allowed is not None and pin not in allowed:
                errors.append(f"connection {index}: {part_id}:{pin} is not a declared {part_type} pin")
            allowed = board_pins.get(part_id)
            if allowed is not None and pin not in allowed:
                errors.append(f"connection {index}: {part_id}:{pin} is not a declared custom-board pin")

        left_type = str(by_id[left_id].get("type", ""))
        right_type = str(by_id[right_id].get("type", ""))
        if is_board(left_type) and is_board(right_type):
            continue
        if is_board(left_type):
            board_id, board_pin = left_id, left_pin
            peer_id, peer_pin, peer_type = right_id, right_pin, right_type
        elif is_board(right_type):
            board_id, board_pin = right_id, right_pin
            peer_id, peer_pin, peer_type = left_id, left_pin, left_type
        else:
            continue
        if peer_type == "wokwi-logic-analyzer":
            continue
        if board_pin.upper() not in POWER_PINS:
            board_uses[f"{board_id}:{board_pin}"].append(
                (peer_id, peer_pin, signal_class(peer_type, peer_pin))
            )

    for board_pin, uses in sorted(board_uses.items()):
        if len(uses) < 2:
            continue
        classes = {item[2] for item in uses}
        if classes in ({"i2c-sda"}, {"i2c-scl"}):
            continue
        details = ", ".join(f"{part}:{pin} ({kind})" for part, pin, kind in uses)
        errors.append(f"conflicting use of {board_pin}: {details}")

    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("diagrams", type=Path, nargs="+")
    args = parser.parse_args()
    failed = False
    for path in args.diagrams:
        errors = check(path.resolve())
        if errors:
            failed = True
            print(f"FAIL {path}", file=sys.stderr)
            for error in errors:
                print(f"  - {error}", file=sys.stderr)
        else:
            print(f"PASS {path} (offline)")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
