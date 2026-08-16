#!/usr/bin/env python3
"""Analyze a captured WMB+ WMBP_WEIGHT_V1 serial log.

This parser is intentionally usable for both Wokwi serial logs and captured
real-device serial logs. It analyzes device timestamps from the firmware stream;
host timing is only meaningful for live serial tests handled by
runtime-cadence-smoke.py.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import sys
from pathlib import Path
from typing import Iterable


REPO_ROOT = Path(__file__).resolve().parents[1]
RUNTIME_SMOKE_PATH = REPO_ROOT / "tools" / "runtime-cadence-smoke.py"
WEIGHT_PREFIX = "WMBP_WEIGHT_V1,"


def load_runtime_smoke_module():
    spec = importlib.util.spec_from_file_location("wmbp_runtime_cadence_smoke", RUNTIME_SMOKE_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"unable to load {RUNTIME_SMOKE_PATH}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def extract_weight_payload(line: str) -> str | None:
    index = line.find(WEIGHT_PREFIX)
    if index < 0:
        return None
    return line[index:].strip()


def parse_log(path: Path):
    runtime = load_runtime_smoke_module()
    samples = []
    malformed = 0
    total_weight_lines = 0

    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        payload = extract_weight_payload(line)
        if payload is None:
            continue
        total_weight_lines += 1
        # Wokwi serial logs do not provide host receive timestamps. Use device
        # time as a neutral stand-in so host-rate fields remain deterministic
        # but are not mistaken for real USB timing.
        parts = payload.split(",", 3)
        host_time = 0.0
        if len(parts) > 1:
            try:
                host_time = int(parts[1]) / 1000.0
            except ValueError:
                pass
        sample = runtime.parse_weight_line(payload, host_time)
        if sample is None:
            malformed += 1
            continue
        samples.append(sample)

    result = runtime.analyze(samples)
    result["log_path"] = str(path)
    result["total_weight_lines"] = total_weight_lines
    result["malformed_weight_lines"] = malformed
    result["host_timing_note"] = "host timing is synthesized from device_ms for offline log analysis"
    return result


def print_summary(label: str, result: dict[str, object]) -> None:
    print(f"\n== {label} ==")
    if not result.get("valid"):
        print(f"samples={result.get('samples', 0)} totalWeightLines={result.get('total_weight_lines', 0)}")
        return
    print(
        "samples={samples} deviceRate={device_rate_hz:.2f}Hz "
        "device p50/p95/max={device_p50_ms:.1f}/{device_p95_ms:.1f}/{device_max_ms:.1f}ms".format(**result)
    )
    print(
        "gaps>100ms={device_gaps_over_100ms} gaps>250ms={device_gaps_over_250ms} "
        "missingSeq={missing_sequence} droppedDelta={dropped_delta} "
        "reportedHx711={reported_hx711_hz_avg:.2f}Hz qualityAvg={quality_avg:.1f} minQuality={quality_min}".format(**result)
    )
    print(
        "totalWeightLines={total_weight_lines} malformed={malformed_weight_lines}".format(**result)
    )


def failures_for(result: dict[str, object], args: argparse.Namespace) -> list[str]:
    if not result.get("valid"):
        return ["not enough WMBP_WEIGHT_V1 samples"]
    failures: list[str] = []
    if float(result["device_rate_hz"]) < args.min_device_rate_hz:
        failures.append(
            f"device rate {float(result['device_rate_hz']):.2f}Hz < {args.min_device_rate_hz:.2f}Hz"
        )
    if float(result["device_max_ms"]) > args.max_device_gap_ms:
        failures.append(
            f"max device gap {float(result['device_max_ms']):.1f}ms > {args.max_device_gap_ms:.1f}ms"
        )
    if int(result["device_gaps_over_100ms"]) > args.max_gaps_over_100ms:
        failures.append(f"gaps >100ms {result['device_gaps_over_100ms']} > {args.max_gaps_over_100ms}")
    if int(result["missing_sequence"]) > 0:
        failures.append(f"missing/non-contiguous sequence count {result['missing_sequence']}")
    if int(result["dropped_delta"]) > 0:
        failures.append(f"USB dropped frames increased by {result['dropped_delta']}")
    if int(result.get("malformed_weight_lines", 0)) > 0:
        failures.append(f"malformed WMBP_WEIGHT_V1 lines {result['malformed_weight_lines']}")
    return failures


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="Serial log containing WMBP_WEIGHT_V1 lines")
    parser.add_argument("--label", default="serial-log")
    parser.add_argument("--min-device-rate-hz", type=float, default=78.0)
    parser.add_argument("--max-device-gap-ms", type=float, default=150.0)
    parser.add_argument("--max-gaps-over-100ms", type=int, default=0)
    parser.add_argument("--json-output", type=Path)
    parser.add_argument("--no-fail", action="store_true")
    return parser


def main(argv: Iterable[str] | None = None) -> int:
    args = build_arg_parser().parse_args(argv)
    result = parse_log(args.log)
    print_summary(args.label, result)
    failures = failures_for(result, args)
    if args.json_output:
        args.json_output.write_text(
            json.dumps({"result": result, "failures": failures}, indent=2),
            encoding="utf-8",
        )
    if failures:
        print("\nFAIL")
        for failure in failures:
            print(f"- {failure}")
        return 0 if args.no_fail else 1
    print("\nPASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

