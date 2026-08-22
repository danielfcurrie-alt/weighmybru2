#!/usr/bin/env python3
"""Analyze a captured WMB+ WMBP serial log.

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
SOURCE_PREFIX = "WMBP_SOURCE_V1,"
FLOAT32_PREFIX = "WMBP_FLOAT32_V1,"


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


def extract_source_payload(line: str) -> str | None:
    index = line.find(SOURCE_PREFIX)
    if index < 0:
        return None
    return line[index:].strip()


def extract_float32_payload(line: str) -> str | None:
    index = line.find(FLOAT32_PREFIX)
    if index < 0:
        return None
    return line[index:].strip()


def payload_host_time(payload: str) -> float:
    # Wokwi serial logs do not provide host receive timestamps. Use device time
    # as a neutral stand-in so host-rate fields remain deterministic but are not
    # mistaken for real USB timing.
    parts = payload.split(",", 3)
    if len(parts) > 1:
        try:
            return int(parts[1]) / 1000.0
        except ValueError:
            pass
    return 0.0


def parse_log(path: Path):
    runtime = load_runtime_smoke_module()
    log_text = path.read_text(encoding="utf-8", errors="replace")
    lines = log_text.splitlines()
    ignored_trailing_fragment = bool(log_text and not log_text.endswith(("\n", "\r")))
    if ignored_trailing_fragment:
        lines = lines[:-1]
    samples = []
    source_samples = []
    float32_samples = []
    malformed = 0
    malformed_source = 0
    malformed_float32 = 0
    total_weight_lines = 0
    total_source_lines = 0
    total_float32_lines = 0

    for line in lines:
        payload = extract_weight_payload(line)
        if payload is not None:
            total_weight_lines += 1
            sample = runtime.parse_weight_line(payload, payload_host_time(payload))
            if sample is None:
                malformed += 1
                continue
            samples.append(sample)
            continue

        payload = extract_source_payload(line)
        if payload is None:
            payload = extract_float32_payload(line)
            if payload is None:
                continue
            total_float32_lines += 1
            float32_sample = runtime.parse_float32_line(payload, payload_host_time(payload))
            if float32_sample is None:
                malformed_float32 += 1
                continue
            float32_samples.append(float32_sample)
            continue
        total_source_lines += 1
        source_sample = runtime.parse_source_line(payload, payload_host_time(payload))
        if source_sample is None:
            malformed_source += 1
            continue
        source_samples.append(source_sample)

    result = runtime.analyze(samples)
    result["source"] = runtime.analyze_source(source_samples)
    result["float32"] = runtime.analyze_float32(float32_samples)
    result["log_path"] = str(path)
    result["total_weight_lines"] = total_weight_lines
    result["total_source_lines"] = total_source_lines
    result["total_float32_lines"] = total_float32_lines
    result["malformed_weight_lines"] = malformed
    result["malformed_source_lines"] = malformed_source
    result["malformed_float32_lines"] = malformed_float32
    result["ignored_trailing_fragment"] = ignored_trailing_fragment
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
    source = result.get("source")
    if isinstance(source, dict) and source.get("valid"):
        print(
            "sourceLines={total_source_lines} malformedSource={malformed_source_lines} "
            "rawRate={raw_rate_hz:.2f}Hz rawDelta={raw_sequence_delta} "
            "qualifiedDelta={qualified_sequence_delta} publicRawDelta={public_raw_sequence_delta} "
            "lossDelta={source_to_public_loss_delta} rejectedDelta={rejected_delta}".format(
                total_source_lines=result["total_source_lines"],
                malformed_source_lines=result["malformed_source_lines"],
                raw_rate_hz=source["raw_rate_hz"],
                raw_sequence_delta=source["raw_sequence_delta"],
                qualified_sequence_delta=source["qualified_sequence_delta"],
                public_raw_sequence_delta=source["public_raw_sequence_delta"],
                source_to_public_loss_delta=source["source_to_public_loss_delta"],
                rejected_delta=source["rejected_delta"],
            )
        )
        print(
            "source gaps p50/p95/max={raw_to_public_gap_p50:.1f}/{raw_to_public_gap_p95:.1f}/{raw_to_public_gap_max} "
            "publicCurrent={public_current_pct:.1f}%".format(**source)
        )
    elif int(result.get("total_source_lines", 0)) > 0:
        print(
            "sourceLines={total_source_lines} malformedSource={malformed_source_lines}".format(**result)
        )
    float32 = result.get("float32")
    if isinstance(float32, dict) and float32.get("valid"):
        print(
            "float32Lines={total_float32_lines} malformedFloat32={malformed_float32_lines} "
            "rowRate={rate_hz:.2f}Hz notifyRate={notify_rate_hz:.2f}Hz "
            "sourceCount p50/min/max={source_count_p50:.1f}/{source_count_min}/{source_count_max} "
            "sourceAge p50/p95/max={source_age_p50_ms:.1f}/{source_age_p95_ms:.1f}/{source_age_max_ms}ms".format(
                total_float32_lines=result["total_float32_lines"],
                malformed_float32_lines=result["malformed_float32_lines"],
                rate_hz=float32["rate_hz"],
                notify_rate_hz=float32["notify_rate_hz"],
                source_count_p50=float32["source_count_p50"],
                source_count_min=float32["source_count_min"],
                source_count_max=float32["source_count_max"],
                source_age_p50_ms=float32["source_age_p50_ms"],
                source_age_p95_ms=float32["source_age_p95_ms"],
                source_age_max_ms=float32["source_age_max_ms"],
            )
        )
        print(
            "float32 source valid={source_valid_pct:.1f}% limited={source_limited_pct:.1f}% "
            "ageOverTick={source_age_over_tick_pct:.1f}% stale={source_stale_pct:.1f}% "
            "suspect={selection_suspect_pct:.1f}% suspectDelta={suspect_delta}".format(
                **float32
            )
        )
    elif int(result.get("total_float32_lines", 0)) > 0:
        print(
            "float32Lines={total_float32_lines} malformedFloat32={malformed_float32_lines}".format(**result)
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
    if int(result.get("malformed_source_lines", 0)) > 0:
        failures.append(f"malformed WMBP_SOURCE_V1 lines {result['malformed_source_lines']}")
    if int(result.get("malformed_float32_lines", 0)) > 0:
        failures.append(f"malformed WMBP_FLOAT32_V1 lines {result['malformed_float32_lines']}")
    return failures


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="Serial log containing WMBP_WEIGHT_V1 and optional source/Float32 diagnostic lines")
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
