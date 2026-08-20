#!/usr/bin/env python3
"""Replay ScaleBench WMB+ captures through candidate Float32 presentation logic.

The output recording keeps the ScaleBench shared JSON shape so it can be
imported into the app, but it is explicitly marked as a synthetic replay in the
title/notes. The source samples are the exported public WMB+ stream; this does
not recreate unexported raw HX711 samples or real BLE behavior.
"""

from __future__ import annotations

import argparse
import gzip
import importlib.util
import json
import math
import statistics
import sys
import uuid
from copy import deepcopy
from pathlib import Path
from typing import Any


DEFAULT_SCORER = Path("analysis/scalebench-replays/macbook-scalebench_scoring.py")


def load_json(path: Path) -> dict[str, Any]:
    opener = gzip.open if path.suffix == ".gz" else open
    with opener(path, "rt", encoding="utf-8") as handle:
        return json.load(handle)


def write_json_gz(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with gzip.open(path, "wt", encoding="utf-8") as handle:
        json.dump(payload, handle, indent=2, sort_keys=True)
        handle.write("\n")


def finite_weight_samples(recording: dict[str, Any]) -> list[dict[str, Any]]:
    samples = []
    for sample in recording.get("samples", []):
        weight = sample.get("weightGrams")
        device_ms = sample.get("deviceTimestampMilliseconds")
        monotonic = sample.get("monotonicSeconds")
        if not isinstance(weight, (int, float)) or not math.isfinite(weight):
            continue
        if not isinstance(device_ms, int):
            continue
        if not isinstance(monotonic, (int, float)) or not math.isfinite(monotonic):
            continue
        samples.append(sample)
    samples.sort(key=lambda s: (s["deviceTimestampMilliseconds"], s["monotonicSeconds"]))
    return samples


def selected_window_weight(
    window: list[tuple[int, float]],
    *,
    tick_ms: int,
    output_history: list[tuple[int, float]],
    newest_policy: str,
    stream_badness: float,
) -> tuple[float | None, int, float, str]:
    if not window:
        return None, 0, 0.0, "empty"
    values = [weight for _, weight in window]
    ordered = sorted(values)
    midpoint = len(ordered) // 2
    target = (
        ordered[midpoint]
        if len(ordered) % 2 == 1
        else (ordered[midpoint - 1] + ordered[midpoint]) * 0.5
    )
    best_time, best_weight = min(
        window,
        key=lambda item: (abs(item[1] - target), -item[0]),
    )
    window_range = max(values) - min(values)

    newest_time, newest_weight = max(window, key=lambda item: item[0])
    flat_coherent = window_range <= 2.0 and abs(newest_weight - target) <= 0.5
    ramp_coherent = False
    oldest_time, oldest_weight = min(window, key=lambda item: item[0])
    span_ms = max(0, newest_time - oldest_time)
    if not flat_coherent and len(window) >= 5 and span_ms >= 100:
        slope_g_per_ms = (newest_weight - oldest_weight) / span_ms
        slope_g_per_s = slope_g_per_ms * 1000.0
        max_residual = 0.0
        for sample_time, sample_weight in window:
            sample_offset = max(0, sample_time - oldest_time)
            expected = oldest_weight + slope_g_per_ms * sample_offset
            max_residual = max(max_residual, abs(sample_weight - expected))
        ramp_coherent = abs(slope_g_per_s) <= 12.0 and max_residual <= 0.45
    del best_time
    if newest_policy in {"plausible", "trend", "adaptive"} and (flat_coherent or ramp_coherent):
        return newest_weight, len(values), window_range, "newest"

    if newest_policy in {"trend", "adaptive"}:
        trend_prediction = predicted_trend_weight(output_history, tick_ms)
        if trend_prediction is not None:
            prediction, confidence = trend_prediction
            last_output = output_history[-1][1] if output_history else None
            expected_direction = 0
            if len(output_history) >= 2:
                last_delta = output_history[-1][1] - output_history[-2][1]
                if abs(last_delta) >= 0.15:
                    expected_direction = 1 if last_delta > 0 else -1

            medoid_error = abs(best_weight - prediction)
            medoid_delta = (best_weight - last_output) if last_output is not None else 0.0
            medoid_direction_conflict = (
                expected_direction != 0
                and medoid_delta * expected_direction < -0.10
            )

            best_trend_time = 0
            best_trend_weight = best_weight
            best_prediction_error = math.inf
            for sample_time, sample_weight in window:
                prediction_error = abs(sample_weight - prediction)
                if last_output is not None and expected_direction != 0:
                    candidate_delta = sample_weight - last_output
                    if candidate_delta * expected_direction < -0.10:
                        continue

                if prediction_error < best_prediction_error or (
                    prediction_error == best_prediction_error and sample_time > best_trend_time
                ):
                    best_prediction_error = prediction_error
                    best_trend_time = sample_time
                    best_trend_weight = sample_weight

            trend_is_materially_better = (
                (newest_policy == "trend" or stream_badness >= 0.58)
                and window_range >= (6.0 if newest_policy == "trend" else 4.5)
                and confidence >= 0.60
                and best_prediction_error <= 0.80
                and medoid_error - best_prediction_error >= (0.55 if confidence >= 0.75 else 0.85)
            )

            if trend_is_materially_better and medoid_direction_conflict:
                return best_trend_weight, len(values), window_range, "trend"

    return best_weight, len(values), window_range, "medoid"


def clamp01(value: float) -> float:
    return max(0.0, min(1.0, value))


def source_window_badness(window: list[tuple[int, float]]) -> float:
    if len(window) < 3:
        return 0.0

    by_time = sorted(window)
    values = [weight for _, weight in by_time]
    window_range = max(values) - min(values)
    median = statistics.median(values)
    outlier_fraction = sum(1 for weight in values if abs(weight - median) > 2.0) / len(values)

    deltas = [by_time[i][1] - by_time[i - 1][1] for i in range(1, len(by_time))]
    strong_positive = any(delta >= 1.0 for delta in deltas)
    strong_negative = any(delta <= -1.0 for delta in deltas)
    huge_jump = any(abs(delta) >= 4.0 for delta in deltas)
    reversal_score = 1.0 if strong_positive and strong_negative else 0.0
    if huge_jump:
        reversal_score = max(reversal_score, 0.75)

    range_score = clamp01((window_range - 2.0) / 8.0)
    outlier_score = clamp01(outlier_fraction * 2.0)
    return clamp01(range_score * 0.45 + reversal_score * 0.40 + outlier_score * 0.25)


def update_stream_badness(current: float, instant: float) -> float:
    if instant > current:
        return current * 0.65 + instant * 0.35
    return current * 0.90 + instant * 0.10


def predicted_trend_weight(output_history: list[tuple[int, float]], tick_ms: int) -> tuple[float, float] | None:
    if len(output_history) < 2:
        return None

    recent = output_history[-4:]
    intervals = []
    deltas = []
    for i in range(1, len(recent)):
        dt = recent[i][0] - recent[i - 1][0]
        if dt <= 0:
            continue
        intervals.append(dt)
        deltas.append(recent[i][1] - recent[i - 1][1])
    if not deltas:
        return None

    latest_delta = deltas[-1]
    if abs(latest_delta) > 3.0:
        return None

    confidence = 0.60
    if len(deltas) >= 2:
        same_direction = all(delta == 0 or latest_delta == 0 or delta * latest_delta >= 0 for delta in deltas[-3:])
        max_delta_spread = max(deltas[-3:]) - min(deltas[-3:])
        if same_direction and max_delta_spread <= 0.85:
            confidence = 0.90
        elif same_direction and max_delta_spread <= 1.50:
            confidence = 0.75
        elif max_delta_spread <= 0.65:
            confidence = 0.70
        else:
            confidence = 0.45

    if confidence < 0.55:
        return None

    last_time, last_weight = output_history[-1]
    dt = max(0, tick_ms - last_time)
    nominal_interval = intervals[-1] if intervals else 50
    projected_delta = latest_delta * min(dt / nominal_interval, 1.25)
    projected_delta = max(-3.0, min(3.0, projected_delta))
    return last_weight + projected_delta, confidence


def replay_float32(
    source: list[dict[str, Any]],
    *,
    phase_ms: int,
    window_ms: int,
    interval_ms: int,
    suspect_step_g: float,
    suppress_step_g: float,
    confirmed_range_g: float,
    clamp: bool,
    newest_policy: str,
) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    if not source:
        return [], {"phaseMs": phase_ms, "suspectSelectionCount": 0, "clampCount": 0, "confirmedStepCount": 0}

    first_device_ms = int(source[0]["deviceTimestampMilliseconds"])
    last_device_ms = int(source[-1]["deviceTimestampMilliseconds"])
    first_monotonic = float(source[0]["monotonicSeconds"])
    first_arrival_ms = int(source[0]["arrivalTimeMillis"])
    tick = first_device_ms + phase_ms
    while tick < first_device_ms:
        tick += interval_ms

    index = 0
    ring: list[tuple[int, float]] = []
    latest: dict[str, Any] | None = None
    last_output: float | None = None
    output: list[dict[str, Any]] = []
    clamp_count = 0
    confirmed_step_count = 0
    suspect_selection_count = 0
    suppressed_suspect_count = 0
    consecutive_suppressed_count = 0
    stream_badness = 0.0
    max_stream_badness = 0.0
    badness_samples: list[float] = []

    while tick <= last_device_ms:
        while index < len(source) and int(source[index]["deviceTimestampMilliseconds"]) <= tick:
            latest = source[index]
            ring.append((int(latest["deviceTimestampMilliseconds"]), float(latest["weightGrams"])))
            index += 1

        if latest is not None:
            ring = [(sample_ms, weight) for sample_ms, weight in ring if tick - sample_ms <= window_ms]
            stream_badness = update_stream_badness(stream_badness, source_window_badness(ring))
            max_stream_badness = max(max_stream_badness, stream_badness)
            badness_samples.append(stream_badness)
            selected, count, window_range, selection_reason = selected_window_weight(
                ring,
                tick_ms=tick,
                output_history=[(int(sample["deviceTimestampMilliseconds"]), float(sample["weightGrams"])) for sample in output[-4:]],
                newest_policy=newest_policy,
                stream_badness=stream_badness,
            )
            if selected is None:
                selected = last_output if last_output is not None else float(latest["weightGrams"])

            if last_output is None:
                public_weight = selected
            else:
                delta = selected - last_output
                if not clamp:
                    public_weight = selected
                    if abs(delta) > suspect_step_g and window_range > suspect_step_g:
                        suspect_selection_count += 1
                    if (
                        abs(delta) > suppress_step_g
                        and window_range > suppress_step_g
                        and consecutive_suppressed_count < 3
                    ):
                        suppressed_suspect_count += 1
                        consecutive_suppressed_count += 1
                        tick += interval_ms
                        continue
                else:
                    if abs(delta) <= suspect_step_g:
                        public_weight = selected
                    elif count >= 3 and window_range <= confirmed_range_g:
                        public_weight = selected
                        confirmed_step_count += 1
                    else:
                        public_weight = last_output + (suspect_step_g if delta > 0 else -suspect_step_g)
                        clamp_count += 1

            sample = deepcopy(latest)
            sample["arrivalTimeMillis"] = first_arrival_ms + (tick - first_device_ms)
            sample["monotonicSeconds"] = first_monotonic + (tick - first_device_ms) / 1000.0
            sample["deviceTimestampMilliseconds"] = tick
            sample["sequence"] = len(output) % 256
            sample["weightGrams"] = round(public_weight, 3)
            sample["flowGramsPerSecond"] = None
            sample["detectedSampleRateHz"] = round(1000.0 / interval_ms)
            if "diagnosticFlags" in sample and isinstance(sample["diagnosticFlags"], dict):
                sample["diagnosticFlags"] = dict(sample["diagnosticFlags"])
                sample["diagnosticFlags"]["flowPresent"] = False
            output.append(sample)
            last_output = public_weight
            consecutive_suppressed_count = 0

        tick += interval_ms

    return output, {
        "phaseMs": phase_ms,
        "windowMs": window_ms,
        "intervalMs": interval_ms,
        "suspectStepGrams": suspect_step_g,
        "suppressStepGrams": suppress_step_g,
        "confirmedRangeGrams": confirmed_range_g,
        "clampEnabled": clamp,
        "newestPolicy": newest_policy,
        "suspectSelectionCount": suspect_selection_count,
        "suppressedSuspectCount": suppressed_suspect_count,
        "maxConsecutiveSuppressedSelections": 3,
        "clampCount": clamp_count,
        "confirmedStepCount": confirmed_step_count,
        "streamBadnessAverage": average(badness_samples),
        "streamBadnessMax": max_stream_badness,
    }


def percentile(values: list[float], p: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    h = (len(ordered) - 1) * p
    lo = math.floor(h)
    hi = min(lo + 1, len(ordered) - 1)
    return ordered[lo] + (h - lo) * (ordered[hi] - ordered[lo])


def step_metrics(samples: list[dict[str, Any]]) -> dict[str, Any]:
    steps = [
        float(samples[i]["weightGrams"]) - float(samples[i - 1]["weightGrams"])
        for i in range(1, len(samples))
    ]
    abs_steps = [abs(step) for step in steps]
    intervals = [
        (float(samples[i]["monotonicSeconds"]) - float(samples[i - 1]["monotonicSeconds"])) * 1000.0
        for i in range(1, len(samples))
    ]
    return {
        "sampleCount": len(samples),
        "negativeStepLeMinus1g": sum(1 for step in steps if step <= -1.0),
        "absStepGt2g": sum(1 for step in steps if abs(step) > 2.0),
        "stepP50Grams": percentile(abs_steps, 0.50),
        "stepP95Grams": percentile(abs_steps, 0.95),
        "stepMaxGrams": max(abs_steps) if abs_steps else None,
        "mostNegativeStepGrams": min(steps) if steps else None,
        "mostPositiveStepGrams": max(steps) if steps else None,
        "intervalP50Ms": percentile(intervals, 0.50),
        "intervalP95Ms": percentile(intervals, 0.95),
        "intervalMaxMs": max(intervals) if intervals else None,
    }


def load_scorer(path: Path):
    spec = importlib.util.spec_from_file_location("scalebench_scoring", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load scorer at {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def score_payload(recording: dict[str, Any], scorer_path: Path) -> dict[str, Any]:
    scorer = load_scorer(scorer_path)
    scoring_recording = {
        "mode": recording.get("mode"),
        "deviceKind": (recording.get("device") or {}).get("kind", "unknown"),
        "protocolCapabilities": recording.get("protocolCapabilities"),
        "recordingStartMonotonicSeconds": recording.get("recordingStartMonotonicSeconds"),
        "recordingEndMonotonicSeconds": recording.get("recordingEndMonotonicSeconds"),
        "events": recording.get("events", []),
        "frames": recording.get("samples", []),
    }
    return scorer.analyze(scoring_recording)


def average(values: list[int | float]) -> float | None:
    return sum(values) / len(values) if values else None


def count_missing_sequences(samples: list[dict[str, Any]]) -> int:
    missing = 0
    last = None
    for sample in samples:
        sequence = sample.get("sequence")
        if not isinstance(sequence, int):
            continue
        if last is not None:
            delta = (sequence - last) % 256
            if 0 < delta <= 128:
                missing += max(0, delta - 1)
        last = sequence
    return missing


def app_export_metrics(
    original_metrics: dict[str, Any],
    reference_metrics: dict[str, Any],
    samples: list[dict[str, Any]],
) -> dict[str, Any]:
    """Return ScaleBench app-export-shaped metrics.

    ScaleBench recalculates metrics when importing, but the payload still needs
    to decode into the app's ScaleQualityMetrics struct. Start with the original
    exported shape, then update fields the reference scorer and replay know.
    """
    metrics = deepcopy(original_metrics) if isinstance(original_metrics, dict) else {}
    diagnostics = reference_metrics.get("diagnostics", {})
    delivery = reference_metrics.get("delivery", {})
    frame_classification = reference_metrics.get("frameClassification", {})
    protocol_verification = reference_metrics.get("protocolVerification", {})
    validity = reference_metrics.get("validity", {})
    step = step_metrics(samples)
    qualities = [
        sample["firmwareQualityScore"]
        for sample in samples
        if isinstance(sample.get("firmwareQualityScore"), int)
        and 0 <= sample["firmwareQualityScore"] <= 100
    ]
    battery_values = [
        sample["batteryPercent"]
        for sample in samples
        if isinstance(sample.get("batteryPercent"), int)
        and 0 <= sample["batteryPercent"] <= 100
    ]

    metrics.update({
        "overallScore": delivery.get("deliveryScore") if delivery.get("applicable") else None,
        "transportScore": delivery.get("deliveryScore") if delivery.get("applicable") else None,
        "stabilityScore": None,
        "metadataScore": protocol_verification.get("verificationCoveragePercent"),
        "effectiveSampleRateHz": diagnostics.get("usableRateHz"),
        "packetIntervalP50Milliseconds": step.get("intervalP50Ms"),
        "packetIntervalP95Milliseconds": step.get("intervalP95Ms"),
        "packetIntervalMaxMilliseconds": step.get("intervalMaxMs"),
        "longGapCount": 0,
        "missingSequenceCount": count_missing_sequences(samples),
        "duplicateOrOutOfOrderTimestampCount": frame_classification.get("stale", 0),
        "rejectedPacketCount": frame_classification.get("parseFailure", 0),
        "idleNoisePeakToPeakGrams": None,
        "idleNoiseStandardDeviationGrams": None,
        "driftGramsPerMinute": None,
        "batteryMinPercent": min(battery_values) if battery_values else None,
        "batteryMaxPercent": max(battery_values) if battery_values else None,
        "firmwareQualityAverage": average(qualities),
        "firmwareBumpCount": 0,
        "scoringModelVersion": reference_metrics.get("scoringModelVersion"),
        "scoringProfileName": reference_metrics.get("scoringProfileName"),
        "validity": validity,
        "delivery": delivery,
        "frameClassification": frame_classification,
        "protocolVerification": protocol_verification,
        "signalUnreconstructable": reference_metrics.get("signalUnreconstructable"),
        "relevantWeightFrameCount": diagnostics.get("relevantWeightFrames"),
        "excludedFrameCount": diagnostics.get("excludedFrames"),
        "usableSampleCount": diagnostics.get("usableSampleCount"),
        "recordingSpanSeconds": diagnostics.get("spanSeconds"),
        "recordingBoundaryInferred": diagnostics.get("recordingBoundaryInferred"),
        "frameRateHz": diagnostics.get("frameRateHz"),
        "usableRateHz": diagnostics.get("usableRateHz"),
        "estimatedResolutionGrams": diagnostics.get("estimatedResolutionGrams"),
        "slotCount": diagnostics.get("slotCount"),
        "servedSlots": diagnostics.get("servedSlots"),
        "longestUnservedRunMilliseconds": diagnostics.get("longestUnservedRunMs"),
        "robustCoefficientOfVariation": diagnostics.get("robustCoefficientOfVariation"),
        "disconnectCount": diagnostics.get("disconnectCount"),
        "idleNoiseScore": None,
        "idleDriftScore": None,
        "idleAnalysedSampleCount": None,
        "idleResolutionGrams": None,
        "stepResponse": None,
    })
    return metrics


def make_replay_recording(
    original: dict[str, Any],
    samples: list[dict[str, Any]],
    replay_meta: dict[str, Any],
    original_path: Path,
    scorer_path: Path,
) -> dict[str, Any]:
    replay = deepcopy(original)
    replay["id"] = str(uuid.uuid4()).upper()
    replay["title"] = f"{original.get('title') or original_path.stem} - beta9 Float32 replay"
    replay["notes"] = (
        "Synthetic replay generated from an existing ScaleBench WMB+ export. "
        "The samples are a 20 Hz Float32 presentation simulation using the "
        f"{replay_meta['windowMs']} ms best-observed-sample window. "
        "Every emitted weight is an actual observed source sample from that window. "
        f"Newest-sample policy: {replay_meta['newestPolicy']}. "
        "This is not a live BLE recording and cannot prove RF delivery or raw HX711 behavior."
    )
    replay["device"] = dict(replay.get("device") or {})
    replay["device"]["name"] = "WeighMyBru+ beta9 Float32 replay"
    replay["samples"] = samples
    replay["rawPackets"] = []
    replay["batteryEvents"] = []
    if samples:
        replay["startedAtMillis"] = int(samples[0]["arrivalTimeMillis"])
        replay["endedAtMillis"] = int(samples[-1]["arrivalTimeMillis"])
        replay["recordingStartMonotonicSeconds"] = float(samples[0]["monotonicSeconds"])
        replay["recordingEndMonotonicSeconds"] = float(samples[-1]["monotonicSeconds"]) + 0.001
    reference_metrics = score_payload(replay, scorer_path)
    replay["metrics"] = app_export_metrics(original.get("metrics", {}), reference_metrics, samples)
    replay["replay"] = {
        "algorithm": "beta9-float32-observed-selector-v2",
        "sourcePath": str(original_path),
        **replay_meta,
    }
    return replay


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("captures", nargs="+", type=Path)
    parser.add_argument("--out-dir", type=Path, default=Path("analysis/scalebench-replays"))
    parser.add_argument("--window-ms", type=int, default=200)
    parser.add_argument("--interval-ms", type=int, default=50)
    parser.add_argument("--phase-ms", type=int, default=0)
    parser.add_argument("--suspect-step-g", type=float, default=2.0)
    parser.add_argument("--suppress-step-g", type=float, default=2.25)
    parser.add_argument("--confirmed-range-g", type=float, default=1.0)
    parser.add_argument("--clamp", action="store_true", help="Enable old synthetic bounded-ramp comparison mode.")
    parser.add_argument(
        "--newest-policy",
        choices=["never", "plausible", "trend", "adaptive"],
        default="plausible",
        help="Prefer the newest observed sample when the window is coherent/plausible.",
    )
    parser.add_argument("--scorer", type=Path, default=DEFAULT_SCORER)
    args = parser.parse_args()

    summary = []
    for capture in args.captures:
        original = load_json(capture)
        source_samples = finite_weight_samples(original)
        replay_samples, replay_meta = replay_float32(
            source_samples,
            phase_ms=args.phase_ms,
            window_ms=args.window_ms,
            interval_ms=args.interval_ms,
            suspect_step_g=args.suspect_step_g,
            suppress_step_g=args.suppress_step_g,
            confirmed_range_g=args.confirmed_range_g,
            clamp=args.clamp,
            newest_policy=args.newest_policy,
        )
        replay = make_replay_recording(original, replay_samples, replay_meta, capture, args.scorer)
        base = capture.name
        if base.endswith(".json.gz"):
            base = base[:-8]
        elif base.endswith(".json"):
            base = base[:-5]
        mode_suffix = "clamped" if args.clamp else f"selector-{args.newest_policy}"
        output_path = args.out_dir / f"{base}-beta9-float32-{mode_suffix}-w{args.window_ms}-p{args.phase_ms}.json.gz"
        analysis_path = args.out_dir / f"{base}-beta9-float32-{mode_suffix}-w{args.window_ms}-p{args.phase_ms}.analysis.json"
        write_json_gz(output_path, replay)

        analysis = {
            "sourcePath": str(capture),
            "outputPath": str(output_path),
            "replay": replay["replay"],
            "scaleBenchMetrics": replay["metrics"],
            "referenceScorerMetrics": score_payload(replay, args.scorer),
            "presentationStepMetrics": step_metrics(replay_samples),
        }
        analysis_path.write_text(json.dumps(analysis, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        summary.append(analysis)

    print(json.dumps({
        "generated": [
            {
                "outputPath": item["outputPath"],
                "analysisPath": str(Path(item["outputPath"].removesuffix(".json.gz") + ".analysis.json")),
                "scaleBenchOverallScore": item["scaleBenchMetrics"].get("overallScore"),
                "presentationStepMetrics": item["presentationStepMetrics"],
            }
            for item in summary
        ]
    }, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
