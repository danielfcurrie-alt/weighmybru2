#!/usr/bin/env python3
"""Stress the current beta9/beta10 observed-sample selector.

This mirrors the firmware selector in BluetoothScale.cpp:

* configurable output ticks, defaulting to the Float32 20 Hz lane.
* 300 ms trailing qualified-input window by default.
* median target over source samples in the window.
* emit the newest observed sample when the window is flat-coherent or fits a
  coherent ramp.
* otherwise emit the observed sample nearest the median target, preferring newer
  samples on ties.

The capture path uses exported WMB samples as a proxy for qualified firmware
input. Those samples are already public filtered/zero-qualified output, so they
are smoother than the on-device Float32 selector input. These replays compare
against the original captured Float32 lane and stress selector mechanics without
claiming final field latency.
"""

from __future__ import annotations

import argparse
import gzip
import json
import math
import random
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


WMB_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
FLOAT32_UUID = "6E400004-B5A3-F393-E0A9-E50E24DCCA9E"

DEFAULT_WINDOW_MS = 300
DEFAULT_INTERVAL_MS = 50
WMB_OUTPUT_RATES_HZ = (10, 20, 40, 80)
COHERENT_WINDOW_GRAMS = 2.0
NEWEST_MEDIAN_BAND_GRAMS = 0.5
COHERENT_RAMP_RESIDUAL_GRAMS = 0.45
COHERENT_RAMP_MAX_RATE_GPS = 12.0
COHERENT_RAMP_MIN_SPAN_MS = 100
SUSPECT_SELECTION_GRAMS = 2.0
SUPPRESS_SELECTION_GRAMS = 2.25
MAX_CONSECUTIVE_SUPPRESSED_SELECTIONS = 3
LOAD_STEP_MIN_GRAMS = 5.0
LOAD_STEP_CLUSTER_GRAMS = 0.75
LOAD_STEP_RECENT_WINDOW_MS = 100
LOAD_STEP_MIN_CLUSTER_SAMPLES = 3


@dataclass
class SourceSample:
    millis: int
    monotonic: float
    weight: float
    sequence: int


@dataclass
class OutputSample:
    millis: int
    monotonic: float
    weight: float
    source_count: int = 0
    source_age_ms: int | None = None
    source_sequence: int | None = None
    window_range_g: float = 0.0
    suspect: bool = False
    confirmed_load_step: bool = False
    reason: str = ""


def load_json(path: Path) -> dict[str, Any]:
    opener = gzip.open if path.suffix == ".gz" else open
    with opener(path, "rt", encoding="utf-8") as handle:
        return json.load(handle)


def percentile(values: Iterable[float], p: float) -> float | None:
    ordered = sorted(v for v in values if math.isfinite(v))
    if not ordered:
        return None
    if len(ordered) == 1:
        return ordered[0]
    h = (len(ordered) - 1) * p
    lo = math.floor(h)
    hi = min(lo + 1, len(ordered) - 1)
    return ordered[lo] + (h - lo) * (ordered[hi] - ordered[lo])


def avg(values: Iterable[float]) -> float | None:
    finite = [v for v in values if math.isfinite(v)]
    return sum(finite) / len(finite) if finite else None


def median(values: list[float]) -> float:
    ordered = sorted(values)
    mid = len(ordered) // 2
    if len(ordered) % 2:
        return ordered[mid]
    return (ordered[mid - 1] + ordered[mid]) * 0.5


def finite_wmb_samples(recording: dict[str, Any]) -> list[SourceSample]:
    samples: list[SourceSample] = []
    for index, sample in enumerate(recording.get("samples", []), start=1):
        weight = sample.get("weightGrams")
        device_ms = sample.get("deviceTimestampMilliseconds")
        mono = sample.get("monotonicSeconds")
        if not isinstance(weight, (int, float)) or not math.isfinite(weight):
            continue
        if not isinstance(device_ms, int):
            continue
        if not isinstance(mono, (int, float)) or not math.isfinite(mono):
            continue
        samples.append(SourceSample(
            millis=int(device_ms),
            monotonic=float(mono),
            weight=float(weight),
            # WMB exports carry the BLE transport sequence, not firmware raw
            # source sequence. For replay identity, use capture order so
            # duplicate-selected-source suppression does not confuse transport
            # wrap/loss with raw-source reuse.
            sequence=index,
        ))
    samples.sort(key=lambda s: (s.millis, s.monotonic))
    return samples


def parse_float32_raw_packets(recording: dict[str, Any]) -> list[OutputSample]:
    samples: list[OutputSample] = []
    for packet in recording.get("rawPackets", []):
        if packet.get("characteristicUUID") != FLOAT32_UUID:
            continue
        mono = packet.get("monotonicSeconds")
        arrival = packet.get("arrivalTimeMillis")
        if not isinstance(mono, (int, float)) or not math.isfinite(mono):
            continue
        if not isinstance(arrival, int):
            continue
        weight = None
        hex_bytes = packet.get("bytesHex")
        if isinstance(hex_bytes, str):
            try:
                raw = bytes(int(part, 16) for part in hex_bytes.split())
                if len(raw) >= 4:
                    weight = struct.unpack("<f", raw[:4])[0]
            except (ValueError, struct.error):
                weight = None
        if weight is None:
            for field in packet.get("fields", []):
                if field.get("semantic") == "weight":
                    text = str(field.get("decodedValue", "")).split()[0]
                    try:
                        weight = float(text)
                    except ValueError:
                        weight = None
        if weight is None or not math.isfinite(weight):
            continue
        samples.append(OutputSample(
            millis=int(round(float(mono) * 1000.0)),
            monotonic=float(mono),
            weight=float(weight),
        ))
    samples.sort(key=lambda s: (s.monotonic, s.millis))
    return samples


def select_window_weight(window: list[SourceSample]) -> tuple[SourceSample | None, float, str]:
    if not window:
        return None, 0.0, "empty"

    values = [sample.weight for sample in window]
    target = median(values)
    window_range = max(values) - min(values)

    newest = max(window, key=lambda sample: sample.millis)
    best = newest
    best_error = math.inf
    best_millis = -1
    for sample in window:
        error = abs(sample.weight - target)
        if error < best_error or (error == best_error and sample.millis >= best_millis):
            best = sample
            best_error = error
            best_millis = sample.millis

    flat_coherent = window_range <= COHERENT_WINDOW_GRAMS and abs(newest.weight - target) <= NEWEST_MEDIAN_BAND_GRAMS
    ramp_coherent = False
    oldest = min(window, key=lambda sample: sample.millis)
    span_ms = max(0, newest.millis - oldest.millis)
    if not flat_coherent and len(window) >= 5 and span_ms >= COHERENT_RAMP_MIN_SPAN_MS:
        slope_g_per_ms = (newest.weight - oldest.weight) / span_ms
        slope_g_per_s = slope_g_per_ms * 1000.0
        max_residual = 0.0
        for sample in window:
            sample_offset = max(0, sample.millis - oldest.millis)
            expected = oldest.weight + slope_g_per_ms * sample_offset
            max_residual = max(max_residual, abs(sample.weight - expected))
        ramp_coherent = (
            abs(slope_g_per_s) <= COHERENT_RAMP_MAX_RATE_GPS
            and max_residual <= COHERENT_RAMP_RESIDUAL_GRAMS
        )

    if flat_coherent or ramp_coherent:
        return newest, window_range, "newest"
    return best, window_range, "medoid"


def is_confirmed_load_step(
    window: list[SourceSample],
    selected: SourceSample,
    last_weight: float | None,
) -> bool:
    if last_weight is None or len(window) < LOAD_STEP_MIN_CLUSTER_SAMPLES:
        return False

    step = selected.weight - last_weight
    if abs(step) < LOAD_STEP_MIN_GRAMS:
        return False

    newest_ms = max(sample.millis for sample in window)
    direction = 1.0 if step >= 0.0 else -1.0
    cluster_count = 0
    for sample in window:
        if newest_ms >= sample.millis and newest_ms - sample.millis > LOAD_STEP_RECENT_WINDOW_MS:
            continue
        if abs(sample.weight - selected.weight) > LOAD_STEP_CLUSTER_GRAMS:
            continue
        if (sample.weight - last_weight) * direction < LOAD_STEP_MIN_GRAMS * 0.60:
            continue
        cluster_count += 1

    return cluster_count >= LOAD_STEP_MIN_CLUSTER_SAMPLES


def replay_selector(
    source: list[SourceSample],
    *,
    window_ms: int = DEFAULT_WINDOW_MS,
    interval_ms: int = DEFAULT_INTERVAL_MS,
    phase_ms: int = 0,
    stale_ms: int = 250,
    suppress_duplicate_source_sequence: bool = False,
) -> list[OutputSample]:
    if not source:
        return []

    first_ms = source[0].millis
    last_ms = source[-1].millis
    first_mono = source[0].monotonic
    tick = first_ms + phase_ms

    index = 0
    ring: list[SourceSample] = []
    outputs: list[OutputSample] = []
    last_weight: float | None = None
    consecutive_suppressed = 0
    last_emitted_source_sequence: int | None = None

    while tick <= last_ms:
        while index < len(source) and source[index].millis <= tick:
            ring.append(source[index])
            index += 1
        ring = [sample for sample in ring if tick - sample.millis <= window_ms]

        selected, window_range, reason = select_window_weight(ring)
        if selected is None:
            tick += interval_ms
            continue
        else:
            source_age_ms = max(0, tick - selected.millis)
            if source_age_ms > stale_ms:
                tick += interval_ms
                continue
            if suppress_duplicate_source_sequence and last_emitted_source_sequence is not None:
                if selected.sequence <= last_emitted_source_sequence:
                    tick += interval_ms
                    continue
            confirmed_load_step = is_confirmed_load_step(ring, selected, last_weight)
            suspect = (
                not confirmed_load_step
                and last_weight is not None
                and len(ring) > 0
                and window_range > SUSPECT_SELECTION_GRAMS
                and abs(selected.weight - last_weight) > SUSPECT_SELECTION_GRAMS
            )
            suppress = (
                not confirmed_load_step
                and last_weight is not None
                and len(ring) > 0
                and window_range > SUPPRESS_SELECTION_GRAMS
                and abs(selected.weight - last_weight) > SUPPRESS_SELECTION_GRAMS
            )
            if suppress and consecutive_suppressed < MAX_CONSECUTIVE_SUPPRESSED_SELECTIONS:
                consecutive_suppressed += 1
                tick += interval_ms
                continue
            consecutive_suppressed = 0
            outputs.append(OutputSample(
                millis=tick,
                monotonic=first_mono + (tick - first_ms) / 1000.0,
                weight=selected.weight,
                source_count=len(ring),
                source_age_ms=source_age_ms,
                source_sequence=selected.sequence,
                window_range_g=window_range,
                suspect=suspect,
                confirmed_load_step=confirmed_load_step,
                reason=reason,
            ))
            last_emitted_source_sequence = selected.sequence
            last_weight = selected.weight
        tick += interval_ms
    return outputs


def three_point_implausibles(samples: list[OutputSample]) -> list[float]:
    errors: list[float] = []
    for i in range(1, len(samples) - 1):
        med = median([samples[i - 1].weight, samples[i].weight, samples[i + 1].weight])
        error = abs(samples[i].weight - med)
        if error > 0.5:
            errors.append(error)
    return errors


def longest_frozen_ms(samples: list[OutputSample], tolerance_g: float = 0.005) -> tuple[float, float]:
    if len(samples) < 2:
        return 0.0, 0.0
    longest = 0.0
    worst_release = 0.0
    run_start: float | None = None
    for i in range(1, len(samples)):
        delta = samples[i].weight - samples[i - 1].weight
        if abs(delta) <= tolerance_g:
            if run_start is None:
                run_start = samples[i - 1].monotonic
            longest = max(longest, (samples[i].monotonic - run_start) * 1000.0)
        else:
            if run_start is not None:
                worst_release = max(worst_release, abs(delta))
            run_start = None
    return longest, worst_release


def summarize_outputs(samples: list[OutputSample], *, source_reuse_interval_ms: int = DEFAULT_INTERVAL_MS) -> dict[str, Any]:
    if not samples:
        return {"count": 0}
    steps = [samples[i].weight - samples[i - 1].weight for i in range(1, len(samples))]
    abs_steps = [abs(step) for step in steps]
    intervals = [(samples[i].monotonic - samples[i - 1].monotonic) * 1000.0 for i in range(1, len(samples))]
    span = max(0.001, samples[-1].monotonic - samples[0].monotonic)
    implausible_errors = three_point_implausibles(samples)
    longest_freeze, worst_release = longest_frozen_ms(samples)
    source_counts = [sample.source_count for sample in samples if sample.source_count > 0]
    source_ages = [sample.source_age_ms for sample in samples if sample.source_age_ms is not None]
    backward_steps = [step for step in steps if step < -0.01]
    return {
        "count": len(samples),
        "spanSeconds": span,
        "effectiveHz": len(samples) / span,
        "intervalP50Ms": percentile(intervals, 0.50),
        "intervalP95Ms": percentile(intervals, 0.95),
        "intervalMaxMs": max(intervals) if intervals else None,
        "stepAbsP50G": percentile(abs_steps, 0.50),
        "stepAbsP95G": percentile(abs_steps, 0.95),
        "stepAbsMaxG": max(abs_steps) if abs_steps else None,
        "stepNegativeLeMinus1G": sum(1 for step in steps if step <= -1.0),
        "stepAbsGt2G": sum(1 for step in steps if abs(step) > 2.0),
        "backwardStepCount": len(backward_steps),
        "backwardMotionG": -sum(backward_steps),
        "implausibleCount": len(implausible_errors),
        "implausibleErrorP95G": percentile(implausible_errors, 0.95),
        "implausibleErrorMaxG": max(implausible_errors) if implausible_errors else 0.0,
        "longestFrozenMs": longest_freeze,
        "worstFreezeReleaseG": worst_release,
        "sourceCountMin": min(source_counts) if source_counts else None,
        "sourceCountP50": percentile(source_counts, 0.50),
        "sourceCountP95": percentile(source_counts, 0.95),
        "sourceCountMax": max(source_counts) if source_counts else None,
        "sourceAgeP50Ms": percentile(source_ages, 0.50),
        "sourceAgeP95Ms": percentile(source_ages, 0.95),
        "sourceAgeMaxMs": max(source_ages) if source_ages else None,
        "sourceLimitedPercent": 100.0 * sum(1 for c in source_counts if c < 3) / len(source_counts) if source_counts else None,
        "sourceReusedPercent": 100.0 * sum(1 for age in source_ages if age >= source_reuse_interval_ms) / len(source_ages) if source_ages else None,
        "selectorSuspectCount": sum(1 for sample in samples if sample.suspect),
        "confirmedLoadStepCount": sum(1 for sample in samples if sample.confirmed_load_step),
        "newestSelectionPercent": 100.0 * sum(1 for sample in samples if sample.reason == "newest") / len(samples),
    }


def capture_summary(path: Path, window_ms: int, phase_ms: int) -> dict[str, Any]:
    recording = load_json(path)
    source = finite_wmb_samples(recording)
    replay = replay_selector(source, window_ms=window_ms, phase_ms=phase_ms)
    original_float32 = parse_float32_raw_packets(recording)
    return {
        "capture": path.name,
        "sourceProxy": {
            "kind": "exported public WMB samples, not raw qualified firmware input",
            "count": len(source),
            "effectiveHz": summarize_outputs([OutputSample(s.millis, s.monotonic, s.weight) for s in source]).get("effectiveHz"),
        },
        "originalFloat32": summarize_outputs(original_float32),
        "beta9Replay": summarize_outputs(replay),
    }


def output_rate_interval_ms(rate_hz: int) -> int:
    return max(1, int(math.ceil(1000.0 / rate_hz)))


def effective_interval_ms(source_hz: float | None, requested_rate_hz: int) -> int:
    requested_interval = output_rate_interval_ms(requested_rate_hz)
    if source_hz is None or not math.isfinite(source_hz) or source_hz <= 1.0:
        return requested_interval
    if source_hz < requested_rate_hz * 0.92:
        return max(requested_interval, int(math.ceil(1000.0 / source_hz)))
    return requested_interval


def wmb_rate_sweep_summary(path: Path, window_ms: int) -> dict[str, Any]:
    recording = load_json(path)
    source = finite_wmb_samples(recording)
    source_summary = summarize_outputs([OutputSample(s.millis, s.monotonic, s.weight) for s in source])
    source_hz = numeric_metric(source_summary, "effectiveHz")
    rows = []
    for rate_hz in WMB_OUTPUT_RATES_HZ:
        interval_ms = effective_interval_ms(source_hz, rate_hz)
        replay = replay_selector(
            source,
            window_ms=window_ms,
            interval_ms=interval_ms,
            stale_ms=window_ms + 100,
            suppress_duplicate_source_sequence=True,
        )
        rows.append({
            "requestedRateHz": rate_hz,
            "effectiveIntervalMs": interval_ms,
            "sourceLimited": bool(source_hz is not None and source_hz < rate_hz * 0.92),
            "metrics": summarize_outputs(replay, source_reuse_interval_ms=interval_ms),
        })
    return {
        "capture": path.name,
        "sourceProxyHz": source_hz,
        "windowMs": window_ms,
        "rates": rows,
    }


def numeric_metric(summary: dict[str, Any], key: str) -> float | None:
    value = summary.get(key)
    return float(value) if isinstance(value, (int, float)) and math.isfinite(value) else None


def phase_sweep_summary(path: Path, window_ms: int) -> dict[str, Any]:
    recording = load_json(path)
    source = finite_wmb_samples(recording)
    phase_rows = []
    for phase in range(DEFAULT_INTERVAL_MS):
        replay = replay_selector(source, window_ms=window_ms, phase_ms=phase)
        metrics = summarize_outputs(replay)
        phase_rows.append({
            "phaseMs": phase,
            "stepAbsP95G": metrics.get("stepAbsP95G"),
            "stepAbsMaxG": metrics.get("stepAbsMaxG"),
            "stepAbsGt2G": metrics.get("stepAbsGt2G"),
            "implausibleCount": metrics.get("implausibleCount"),
            "implausibleErrorP95G": metrics.get("implausibleErrorP95G"),
            "selectorSuspectCount": metrics.get("selectorSuspectCount"),
            "confirmedLoadStepCount": metrics.get("confirmedLoadStepCount"),
            "longestFrozenMs": metrics.get("longestFrozenMs"),
            "sourceReusedPercent": metrics.get("sourceReusedPercent"),
            "sourceLimitedPercent": metrics.get("sourceLimitedPercent"),
        })

    def spread(key: str) -> dict[str, Any]:
        values = [numeric_metric(row, key) for row in phase_rows]
        finite = [value for value in values if value is not None]
        return {
            "min": min(finite) if finite else None,
            "p50": percentile(finite, 0.50),
            "p95": percentile(finite, 0.95),
            "max": max(finite) if finite else None,
        }

    return {
        "capture": path.name,
        "windowMs": window_ms,
        "sourceProxyCount": len(source),
        "phaseCount": len(phase_rows),
        "spreads": {
            key: spread(key)
            for key in [
                "stepAbsP95G",
                "stepAbsMaxG",
                "stepAbsGt2G",
                "implausibleCount",
                "implausibleErrorP95G",
                "selectorSuspectCount",
                "confirmedLoadStepCount",
                "longestFrozenMs",
                "sourceReusedPercent",
                "sourceLimitedPercent",
            ]
        },
        "phases": phase_rows,
    }


def make_synthetic(name: str, rate_hz: float, duration_s: float, seed: int = 42) -> list[SourceSample]:
    rng = random.Random(seed)
    samples: list[SourceSample] = []
    interval_ms = 1000.0 / rate_hz
    count = int(duration_s * rate_hz)
    for i in range(count):
        t_ms = int(round(i * interval_ms))
        t_s = t_ms / 1000.0
        if name == "cup_step":
            weight = 0.0 if t_s < 2.0 else 50.0
        else:
            weight = max(0.0, (t_s - 2.0) * 2.5)
        noise = rng.gauss(0.0, 0.03)
        if name == "noisy_80sps" and 3.0 <= t_s <= 12.0 and rng.random() < 0.18:
            noise += rng.choice([-1.0, 1.0]) * rng.uniform(1.5, 7.5)
        elif name == "vibration_idle" and 2.0 <= t_s <= 12.0:
            weight = 20.0
            noise += math.sin(t_s * 2.0 * math.pi * 18.0) * 0.75 + rng.gauss(0.0, 0.20)
        samples.append(SourceSample(
            millis=t_ms,
            monotonic=t_s,
            weight=round(weight + noise, 3),
            sequence=i + 1,
        ))
    return samples


def make_linear_ramp(rate_hz: float, flow_gps: float, duration_s: float = 8.0) -> list[SourceSample]:
    samples: list[SourceSample] = []
    interval_ms = 1000.0 / rate_hz
    count = int(duration_s * rate_hz)
    for i in range(count):
        t_ms = int(round(i * interval_ms))
        t_s = t_ms / 1000.0
        weight = max(0.0, (t_s - 1.0) * flow_gps)
        samples.append(SourceSample(
            millis=t_ms,
            monotonic=t_s,
            weight=round(weight, 3),
            sequence=i + 1,
        ))
    return samples


def flow_rate_sweep_summary(window_ms: int) -> list[dict[str, Any]]:
    rows = []
    for flow_gps in [1.0, 2.0, 3.0, 3.33, 4.0, 5.0, 6.4, 8.0, 10.0, 12.0, 14.0]:
        source = make_linear_ramp(80.0, flow_gps)
        replay = replay_selector(source, window_ms=window_ms)
        summary = summarize_outputs(replay)
        rows.append({
            "flowGps": flow_gps,
            "inputHz": 80.0,
            "sourceAgeP50Ms": summary.get("sourceAgeP50Ms"),
            "sourceAgeP95Ms": summary.get("sourceAgeP95Ms"),
            "sourceAgeMaxMs": summary.get("sourceAgeMaxMs"),
            "newestSelectionPercent": summary.get("newestSelectionPercent"),
            "stepAbsP95G": summary.get("stepAbsP95G"),
            "stepAbsMaxG": summary.get("stepAbsMaxG"),
        })
    return rows


def synthetic_wmb_rate_summaries(window_ms: int) -> list[dict[str, Any]]:
    scenarios = [
        ("quiet_80sps_ramp", 80.0, "quiet_80sps"),
        ("noisy_80sps_ramp", 80.0, "noisy_80sps"),
        ("hx711_10sps_ramp", 10.0, "quiet_80sps"),
        ("under_15sps_ramp", 15.0, "quiet_80sps"),
    ]
    rows = []
    for label, source_hz, kind in scenarios:
        source = make_synthetic(kind, source_hz, 16.0)
        rate_rows = []
        for rate_hz in WMB_OUTPUT_RATES_HZ:
            interval_ms = effective_interval_ms(source_hz, rate_hz)
            replay = replay_selector(
                source,
                window_ms=window_ms,
                interval_ms=interval_ms,
                stale_ms=window_ms + 100,
                suppress_duplicate_source_sequence=True,
            )
            rate_rows.append({
                "requestedRateHz": rate_hz,
                "effectiveIntervalMs": interval_ms,
                "sourceLimited": source_hz < rate_hz * 0.92,
                "metrics": summarize_outputs(replay, source_reuse_interval_ms=interval_ms),
            })
        rows.append({
            "scenario": label,
            "inputHz": source_hz,
            "rates": rate_rows,
        })
    return rows


def synthetic_summaries(window_ms: int) -> list[dict[str, Any]]:
    scenarios = [
        ("quiet_80sps_ramp", 80.0, "quiet_80sps"),
        ("noisy_80sps_ramp", 80.0, "noisy_80sps"),
        ("hx711_10sps_ramp", 10.0, "quiet_80sps"),
        ("under_15sps_ramp", 15.0, "quiet_80sps"),
        ("vibration_idle_80sps", 80.0, "vibration_idle"),
        ("cup_step_80sps", 80.0, "cup_step"),
    ]
    rows = []
    for label, rate, kind in scenarios:
        source = make_synthetic(kind, rate, 16.0)
        replay = replay_selector(source, window_ms=window_ms)
        rows.append({
            "scenario": label,
            "inputHz": rate,
            "beta9Replay": summarize_outputs(replay),
        })
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("captures", nargs="*", type=Path)
    parser.add_argument("--window-ms", type=int, default=DEFAULT_WINDOW_MS)
    parser.add_argument("--interval-ms", type=int, default=DEFAULT_INTERVAL_MS)
    parser.add_argument("--phase-ms", type=int, default=0)
    parser.add_argument("--phase-sweep", action="store_true")
    parser.add_argument("--wmb-rate-sweep", action="store_true")
    parser.add_argument("--out", type=Path, default=Path("analysis/float32-stress/current-firmware-float32-stress.json"))
    args = parser.parse_args()

    result = {
        "algorithm": {
            "name": "beta9-current-firmware-float32-observed-selector",
            "windowMs": args.window_ms,
            "intervalMs": args.interval_ms,
            "coherentWindowGrams": COHERENT_WINDOW_GRAMS,
            "newestMedianBandGrams": NEWEST_MEDIAN_BAND_GRAMS,
            "coherentRampResidualGrams": COHERENT_RAMP_RESIDUAL_GRAMS,
            "coherentRampMaxRateGps": COHERENT_RAMP_MAX_RATE_GPS,
            "coherentRampMinSpanMs": COHERENT_RAMP_MIN_SPAN_MS,
            "note": (
                "Capture replays use exported public WMB samples as a smoother proxy for "
                "the actual on-device zero-qualified observed input. Treat replay latency "
                "and newest-selection rates as optimistic until verified on hardware."
            ),
            "flowRateSweepNote": (
                "The synthetic flow-rate sweep is noiseless. It validates coherent-ramp "
                "gate arithmetic, not field performance under machine vibration."
            ),
        },
        "captures": [capture_summary(path, args.window_ms, args.phase_ms) for path in args.captures],
        "phaseSweeps": [phase_sweep_summary(path, args.window_ms) for path in args.captures] if args.phase_sweep else [],
        "wmbRateSweeps": [wmb_rate_sweep_summary(path, args.window_ms) for path in args.captures] if args.wmb_rate_sweep else [],
        "flowRateSweep": flow_rate_sweep_summary(args.window_ms),
        "syntheticWmbRateSweeps": synthetic_wmb_rate_summaries(args.window_ms) if args.wmb_rate_sweep else [],
        "synthetic": synthetic_summaries(args.window_ms),
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
