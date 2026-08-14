#!/usr/bin/env python3
"""Estimate WMB+ battery impact across board/power-mode combinations.

This is an engineering model, not a measurement. The defaults intentionally
live in one plain table so real drain-test data can replace the assumptions as
we collect it from XIAO, TinyS3[D], SuperMini, and Waveshare Zero builds.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from dataclasses import asdict, dataclass
from typing import Iterable, Optional


@dataclass(frozen=True)
class BoardModel:
    board: str
    active_base_ma: float
    wifi_ap_delta_ma: float
    sps80_delta_ma: float
    sleep_ma: float
    hx711_ma: float
    charge_current_ma: float
    notes: str


@dataclass(frozen=True)
class Variant:
    key: str
    label: str
    wifi_on: bool
    hz: int
    sleeping: bool
    hx711_powered: bool


@dataclass(frozen=True)
class Result:
    board: str
    variant: str
    hz: int
    wifi_on: bool
    sleeping: bool
    hx711_powered: bool
    estimated_current_ma: float
    estimated_runtime_hours: float
    relative_to_board_base_percent: float
    estimated_percent_per_hour: float
    notes: str


@dataclass(frozen=True)
class SleepChargeResult:
    board: str
    variant: str
    hx711_powered: bool
    sleep_load_ma: float
    assumed_charge_current_ma: float
    assumed_charge_efficiency_percent: float
    net_charge_current_ma: float
    start_percent: float
    minutes_to_80: Optional[float]
    minutes_to_100: Optional[float]
    notes: str


BOARD_MODELS = [
    BoardModel(
        board="xiao-esp32s3",
        active_base_ma=42.0,
        wifi_ap_delta_ma=45.0,
        sps80_delta_ma=3.0,
        sleep_ma=0.22,
        hx711_ma=1.5,
        charge_current_ma=250.0,
        notes="XIAO reference class; ADC battery estimate; WiFi AP is the large variable.",
    ),
    BoardModel(
        board="tinys3d",
        active_base_ma=36.0,
        wifi_ap_delta_ma=42.0,
        sps80_delta_ma=3.0,
        sleep_ma=0.10,
        hx711_ma=1.5,
        charge_current_ma=350.0,
        notes="TinyS3[D] estimate; MAX17048 gives better SoC/USB-power telemetry, not lower HX711 power.",
    ),
    BoardModel(
        board="supermini",
        active_base_ma=48.0,
        wifi_ap_delta_ma=50.0,
        sps80_delta_ma=3.5,
        sleep_ma=0.50,
        hx711_ma=1.5,
        charge_current_ma=250.0,
        notes="Cheap SuperMini-style estimate; regulator/quiescent losses may vary a lot by clone.",
    ),
    BoardModel(
        board="waveshare-zero",
        active_base_ma=44.0,
        wifi_ap_delta_ma=47.0,
        sps80_delta_ma=3.0,
        sleep_ma=0.30,
        hx711_ma=1.5,
        charge_current_ma=250.0,
        notes="Waveshare ESP32-S3-Zero-class estimate; verify battery/charger path per exact board.",
    ),
]


VARIANTS = [
    Variant("base-wifi-off-10sps", "WiFi off, 10 SPS", False, 10, False, True),
    Variant("wifi-on-10sps", "WiFi on, 10 SPS", True, 10, False, True),
    Variant("wifi-off-80sps", "WiFi off, 80 SPS", False, 80, False, True),
    Variant("wifi-on-80sps", "WiFi on, 80 SPS", True, 80, False, True),
    Variant("sleep-wifi-off-hx711-on", "Sleep, WiFi off, HX711 powered", False, 0, True, True),
    Variant("sleep-wifi-off-hx711-off", "Sleep, WiFi off, HX711 power off", False, 0, True, False),
]


def estimate_current_ma(board: BoardModel, variant: Variant) -> float:
    if variant.sleeping:
        current = board.sleep_ma
        if variant.hx711_powered:
            current += board.hx711_ma
        return current

    current = board.active_base_ma
    if variant.wifi_on:
        current += board.wifi_ap_delta_ma
    if variant.hz >= 80:
        current += board.sps80_delta_ma
    return current


def simulate(capacity_mah: float) -> list[Result]:
    results: list[Result] = []
    for board in BOARD_MODELS:
        board_base_variant = VARIANTS[0]
        board_base_current = estimate_current_ma(board, board_base_variant)
        for variant in VARIANTS:
            current_ma = estimate_current_ma(board, variant)
            runtime_hours = capacity_mah / current_ma if current_ma > 0 else float("inf")
            percent_per_hour = (current_ma / capacity_mah) * 100.0
            relative = (current_ma / board_base_current) * 100.0 if board_base_current > 0 else 0.0
            results.append(
                Result(
                    board=board.board,
                    variant=variant.label,
                    hz=variant.hz,
                    wifi_on=variant.wifi_on,
                    sleeping=variant.sleeping,
                    hx711_powered=variant.hx711_powered,
                    estimated_current_ma=round(current_ma, 3),
                    estimated_runtime_hours=round(runtime_hours, 2),
                    relative_to_board_base_percent=round(relative, 1),
                    estimated_percent_per_hour=round(percent_per_hour, 2),
                    notes=board.notes,
                )
            )
    return results


def minutes_to_target(capacity_mah: float, start_percent: float, target_percent: float, net_charge_ma: float) -> Optional[float]:
    if start_percent >= target_percent:
        return 0.0
    if net_charge_ma <= 0.0:
        return None

    needed_mah = capacity_mah * ((target_percent - start_percent) / 100.0)
    return (needed_mah / net_charge_ma) * 60.0


def simulate_sleep_charge(capacity_mah: float, start_percent: float, charge_efficiency: float) -> list[SleepChargeResult]:
    charge_boards = {board.board: board for board in BOARD_MODELS if board.board in {"xiao-esp32s3", "tinys3d"}}
    variants = [variant for variant in VARIANTS if variant.sleeping]
    results: list[SleepChargeResult] = []

    for board in charge_boards.values():
        for variant in variants:
            sleep_load_ma = estimate_current_ma(board, variant)
            effective_charge_ma = board.charge_current_ma * charge_efficiency
            net_charge_ma = effective_charge_ma - sleep_load_ma
            minutes_to_80 = minutes_to_target(capacity_mah, start_percent, 80.0, net_charge_ma)
            minutes_to_100 = minutes_to_target(capacity_mah, start_percent, 100.0, net_charge_ma)
            results.append(
                SleepChargeResult(
                    board=board.board,
                    variant=variant.label,
                    hx711_powered=variant.hx711_powered,
                    sleep_load_ma=round(sleep_load_ma, 3),
                    assumed_charge_current_ma=round(board.charge_current_ma, 2),
                    assumed_charge_efficiency_percent=round(charge_efficiency * 100.0, 1),
                    net_charge_current_ma=round(net_charge_ma, 3),
                    start_percent=round(start_percent, 1),
                    minutes_to_80=None if minutes_to_80 is None else round(minutes_to_80, 1),
                    minutes_to_100=None if minutes_to_100 is None else round(minutes_to_100, 1),
                    notes=board.notes,
                )
            )
    return results


def format_minutes(minutes: Optional[float]) -> str:
    if minutes is None:
        return "never"
    hours = minutes / 60.0
    if hours >= 10.0:
        return f"{hours:.1f} h"
    return f"{minutes:.0f} min"


def print_table(results: Iterable[Result]) -> None:
    rows = list(results)
    print("| Board | Variant | Est. mA | Runtime @ capacity | %/hour | vs board base |")
    print("| --- | --- | ---: | ---: | ---: | ---: |")
    for row in rows:
        print(
            f"| {row.board} | {row.variant} | "
            f"{row.estimated_current_ma:.2f} | "
            f"{row.estimated_runtime_hours:.2f} h | "
            f"{row.estimated_percent_per_hour:.2f}% | "
            f"{row.relative_to_board_base_percent:.1f}% |"
        )


def print_sleep_charge_table(results: Iterable[SleepChargeResult]) -> None:
    rows = list(results)
    print("| Board | Sleep variant | Sleep load | Charger | Net charge | To 80% | To 100% |")
    print("| --- | --- | ---: | ---: | ---: | ---: | ---: |")
    for row in rows:
        print(
            f"| {row.board} | {row.variant} | "
            f"{row.sleep_load_ma:.2f} mA | "
            f"{row.assumed_charge_current_ma:.0f} mA @ {row.assumed_charge_efficiency_percent:.0f}% | "
            f"{row.net_charge_current_ma:.2f} mA | "
            f"{format_minutes(row.minutes_to_80)} | "
            f"{format_minutes(row.minutes_to_100)} |"
        )


def write_csv(results: Iterable[Result], path: str) -> None:
    rows = [asdict(row) for row in results]
    with open(path, "w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capacity-mah", type=float, default=700.0)
    parser.add_argument("--start-percent", type=float, default=20.0)
    parser.add_argument("--charge-efficiency", type=float, default=0.85)
    parser.add_argument("--format", choices=("table", "json", "csv"), default="table")
    parser.add_argument("--output", help="Output path for csv/json; stdout when omitted.")
    args = parser.parse_args(argv)

    if args.capacity_mah <= 0:
        parser.error("--capacity-mah must be positive")
    if args.start_percent < 0 or args.start_percent > 100:
        parser.error("--start-percent must be 0-100")
    if args.charge_efficiency <= 0 or args.charge_efficiency > 1:
        parser.error("--charge-efficiency must be >0 and <=1")

    results = simulate(args.capacity_mah)
    sleep_charge_results = simulate_sleep_charge(args.capacity_mah, args.start_percent, args.charge_efficiency)

    if args.format == "table":
        print(f"Assumed battery capacity: {args.capacity_mah:.0f} mAh")
        print("Assumption quality: comparative estimate only; replace board constants with measured drain-test data.")
        print()
        print_table(results)
        print()
        print(f"Sleep charging estimate: from {args.start_percent:.0f}% SoC, charger efficiency {args.charge_efficiency * 100.0:.0f}%")
        print_sleep_charge_table(sleep_charge_results)
        return 0

    if args.format == "json":
        payload = {
            "capacity_mah": args.capacity_mah,
            "charge_start_percent": args.start_percent,
            "charge_efficiency": args.charge_efficiency,
            "assumption_quality": "comparative estimate only",
            "boards": [asdict(board) for board in BOARD_MODELS],
            "variants": [asdict(variant) for variant in VARIANTS],
            "results": [asdict(row) for row in results],
            "sleep_charge_results": [asdict(row) for row in sleep_charge_results],
        }
        text = json.dumps(payload, indent=2, sort_keys=True)
        if args.output:
            with open(args.output, "w", encoding="utf-8") as handle:
                handle.write(text)
                handle.write("\n")
        else:
            print(text)
        return 0

    if args.output:
        write_csv(results, args.output)
    else:
        writer = csv.DictWriter(sys.stdout, fieldnames=list(asdict(results[0]).keys()))
        writer.writeheader()
        writer.writerows(asdict(row) for row in results)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
