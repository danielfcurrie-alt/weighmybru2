#!/usr/bin/env python3
"""Score runtime-cadence-smoke JSON captures with the ScaleBench reference scorer.

Use this with JSON produced by:

    tools/runtime-cadence-smoke.py --save-samples --json-output <path>

The score is ScaleBench-shaped and useful for firmware iteration, but USB
runtime rows are not identical to a BLE ScaleBench recording.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
from typing import Any


DEFAULT_SCORER = Path("analysis/scalebench-replays/macbook-scalebench_scoring.py")


def load_scorer(path: Path):
    spec = importlib.util.spec_from_file_location("scalebench_scoring", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load scorer at {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def weight_frames(samples: list[dict[str, Any]]) -> list[dict[str, Any]]:
    frames = []
    for sample in samples:
        frames.append({
            "kind": "weight",
            "monotonicSeconds": sample["host_time"],
            "weightGrams": sample["weight_g"],
            "sequence": sample["sequence"] % 256,
            "deviceTimestampMs": sample["device_ms"],
        })
    return frames


def float32_frames(samples: list[dict[str, Any]]) -> list[dict[str, Any]]:
    frames = []
    for index, sample in enumerate(samples):
        frames.append({
            "kind": "weight",
            "monotonicSeconds": sample["host_time"],
            "weightGrams": sample["weight_g"],
            "sequence": index % 256,
            "deviceTimestampMs": sample["device_ms"],
        })
    return frames


def score(frames: list[dict[str, Any]], scorer) -> dict[str, Any]:
    if not frames:
        return {"validity": {"isValid": False, "reasons": ["no frames"]}}
    start = frames[0]["monotonicSeconds"]
    end = frames[-1]["monotonicSeconds"] + 0.001
    return scorer.analyze({
        "mode": "shot",
        "recordingStartMonotonicSeconds": start,
        "recordingEndMonotonicSeconds": end,
        "events": [],
        "frames": frames,
        "protocolCapabilities": {
            "sequenceModulus": 256,
            "hasDeviceClock": True,
            "deviceClockSemantics": "freeRunning",
            "deviceClockModulus": 1 << 32,
        },
    })


def compact(result: dict[str, Any]) -> dict[str, Any]:
    return {
        "score": result.get("delivery", {}).get("deliveryScore"),
        "delivery": result.get("delivery"),
        "frameClassification": result.get("frameClassification"),
        "signalUnreconstructable": result.get("signalUnreconstructable"),
        "diagnostics": {
            key: result.get("diagnostics", {}).get(key)
            for key in [
                "frameRateHz",
                "usableRateHz",
                "slotCount",
                "servedSlots",
                "longestUnservedRunMs",
                "intervalP50Ms",
                "intervalMaxMs",
                "estimatedResolutionGrams",
            ]
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--profile", default=None)
    parser.add_argument("--scorer", type=Path, default=DEFAULT_SCORER)
    args = parser.parse_args()

    payload = json.loads(args.capture.read_text(encoding="utf-8"))
    results = payload.get("results", {})
    profile_names = [args.profile] if args.profile else sorted(results)
    scorer = load_scorer(args.scorer)

    output = {}
    for profile in profile_names:
        result = results.get(profile)
        if not isinstance(result, dict):
            continue
        output[profile] = {
            "weight": compact(score(weight_frames(result.get("captured_weight_samples", [])), scorer)),
            "float32": compact(score(float32_frames(result.get("captured_float32_samples", [])), scorer)),
        }

    print(json.dumps(output, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
