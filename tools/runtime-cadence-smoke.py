#!/usr/bin/env python3
"""Runtime WMB+ cadence smoke test.

This test measures the live WMB+ USB serial stream while optionally polling the
web UI endpoints that a browser dashboard would hit. It is intended to catch the
real failure mode that source checks cannot prove: web/dashboard activity causing
80 SPS acquisition or USB emission cadence to stall.

Typical use against a flashed XIAO reference unit:

    python3 tools/runtime-cadence-smoke.py --port /dev/cu.usbmodem1101 \
      --base-url http://192.168.4.1 --profile dashboard-safe

Run a baseline first, then one manual dashboard refresh during the capture:

    python3 tools/runtime-cadence-smoke.py --port /dev/cu.usbmodem1101 \
      --base-url http://192.168.4.1 --profile baseline --profile dashboard-safe

Run the low-priority recording profile:

    python3 tools/runtime-cadence-smoke.py --port /dev/cu.usbmodem1101 \
      --base-url http://192.168.4.1 --profile recording-low-priority

Reproduce the old bad pattern without failing the shell command:

    python3 tools/runtime-cadence-smoke.py --port /dev/cu.usbmodem1101 \
      --base-url http://192.168.4.1 --profile aggressive-repro --no-fail
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import queue
import statistics
import sys
import threading
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


WEIGHT_PREFIX = "WMBP_WEIGHT_V1,"
HEADER_PREFIX = "WMBP_WEIGHT_V1_HEADER,"


@dataclass(frozen=True)
class Sample:
    host_time: float
    device_ms: int
    sequence: int
    weight_g: float
    flow_gps: float
    status_hex: str
    quality: int
    battery_percent: int
    hx711_hz: float
    dropped: int


@dataclass(frozen=True)
class PollRequest:
    endpoint: str
    interval_s: float


PROFILES: dict[str, list[PollRequest]] = {
    "baseline": [],
    # Current intended dashboard behavior: one manual refresh at profile start.
    "dashboard-safe": [
        PollRequest("/api/dashboard", 3600.0),
    ],
    # Explicit recording-mode pattern: low-priority dashboard polling only after
    # a user starts a timer/recording view.
    "recording-low-priority": [
        PollRequest("/api/dashboard", 2.0),
    ],
    # Former "safe" behavior retained as a repro/comparison profile.
    "dashboard-2hz-repro": [
        PollRequest("/api/dashboard", 0.5),
        PollRequest("/api/device/info", 60.0),
        PollRequest("/api/ota/status", 60.0),
    ],
    # Old bad pattern: fast dashboard polling, duplicate /api/weight-fast, and
    # static metadata every 5 seconds. Use this as a negative/repro profile.
    "aggressive-repro": [
        PollRequest("/api/dashboard", 0.1),
        PollRequest("/api/weight-fast", 0.5),
        PollRequest("/api/device/info", 5.0),
        PollRequest("/api/ota/status", 5.0),
    ],
    # Isolate the static metadata heartbeat that matched the observed 5s gaps.
    "static-repro": [
        PollRequest("/api/dashboard", 0.5),
        PollRequest("/api/device/info", 5.0),
        PollRequest("/api/ota/status", 5.0),
    ],
    # Short, high-connection-churn accept-path stress. This intentionally uses
    # GET-only endpoints and multiple workers to compress the old 10-minute
    # browser-refresh exposure into a bounded run.
    "connection-churn": [
        PollRequest("/api/dashboard", 0.02),
        PollRequest("/api/device/info", 0.02),
        PollRequest("/api/ota/status", 0.02),
        PollRequest("/api/settings", 0.02),
        PollRequest("/api/scale/status", 0.02),
        PollRequest("/api/battery", 0.02),
    ],
}

PROFILE_WORKERS: dict[str, int] = {
    "connection-churn": 8,
}

ACQUISITION_KEYS = [
    "acquisition_model",
    "acquisition_poll_count",
    "acquisition_ready_count",
    "acquisition_not_ready_count",
    "acquisition_accepted_count",
    "acquisition_raw_read_count",
    "acquisition_estimated_lost_cadence_slots",
    "raw_read_expected_interval_us",
    "raw_read_avg_interval_us",
    "raw_read_max_interval_us",
    "raw_read_estimated_lost_cadence_slots",
    "raw_read_max_duration_us",
    "acquisition_rejected_count",
    "acquisition_read_error_count",
    "acquisition_disconnected_count",
    "acquisition_data_ready_notifications",
    "acquisition_busy_skip_count",
    "acquisition_timeout_count",
]


def percentile(values: list[float], percentile_value: float) -> float | None:
    if not values:
        return None
    if len(values) == 1:
        return values[0]
    ordered = sorted(values)
    rank = (len(ordered) - 1) * percentile_value
    lower = math.floor(rank)
    upper = math.ceil(rank)
    if lower == upper:
        return ordered[int(rank)]
    fraction = rank - lower
    return ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction


def parse_weight_line(line: str, host_time: float) -> Sample | None:
    if not line.startswith(WEIGHT_PREFIX):
        return None
    row = next(csv.reader([line]))
    if len(row) != 10:
        return None
    try:
        return Sample(
            host_time=host_time,
            device_ms=int(row[1]),
            sequence=int(row[2]),
            weight_g=float(row[3]),
            flow_gps=float(row[4]),
            status_hex=row[5],
            quality=int(row[6]),
            battery_percent=int(row[7]),
            hx711_hz=float(row[8]),
            dropped=int(row[9]),
        )
    except (ValueError, IndexError):
        return None


def discover_port() -> str:
    try:
        import serial.tools.list_ports  # type: ignore
    except ImportError as exc:
        raise SystemExit("pyserial is required for --port auto") from exc

    candidates = [
        port.device
        for port in serial.tools.list_ports.comports()
        if "usbmodem" in port.device or "usbserial" in port.device
    ]
    if len(candidates) == 1:
        return candidates[0]
    if not candidates:
        raise SystemExit("no USB serial port found; pass --port explicitly")
    raise SystemExit(f"multiple USB serial ports found; pass --port explicitly: {candidates}")


class SerialReader:
    def __init__(self, port: str, baud: int) -> None:
        try:
            import serial  # type: ignore
        except ImportError as exc:
            raise SystemExit("pyserial is required: python3 -m pip install pyserial") from exc

        self.serial = serial.Serial(port, baudrate=baud, timeout=0.2)
        self.samples: list[Sample] = []
        self.other_lines: list[str] = []
        self._queue: queue.Queue[Sample] = queue.Queue()
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._read_loop, name="serial-reader", daemon=True)

    def start(self) -> None:
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        self._thread.join(timeout=2.0)
        self.serial.close()

    def write_line(self, line: str) -> None:
        self.serial.write((line + "\n").encode("ascii"))
        self.serial.flush()

    def drain_samples(self) -> list[Sample]:
        drained: list[Sample] = []
        while True:
            try:
                drained.append(self._queue.get_nowait())
            except queue.Empty:
                break
        return drained

    def _read_loop(self) -> None:
        while not self._stop.is_set():
            raw = self.serial.readline()
            if not raw:
                continue
            host_time = time.monotonic()
            line = raw.decode("utf-8", errors="replace").strip()
            if not line or line.startswith(HEADER_PREFIX):
                continue
            sample = parse_weight_line(line, host_time)
            if sample is not None:
                self.samples.append(sample)
                self._queue.put(sample)
            elif len(self.other_lines) < 200:
                self.other_lines.append(line)


def ensure_usb_stream(reader: SerialReader, settle_s: float = 2.0) -> bool:
    """Enable USB stream if it is not already producing samples.

    Returns True if this function toggled the stream on, so callers can restore
    the previous state by sending `w` when done.
    """
    reader.drain_samples()
    deadline = time.monotonic() + settle_s
    while time.monotonic() < deadline:
        if reader.drain_samples():
            return False
        time.sleep(0.05)

    reader.write_line("w")
    deadline = time.monotonic() + settle_s
    while time.monotonic() < deadline:
        if reader.drain_samples():
            return True
        time.sleep(0.05)

    raise SystemExit("USB weight stream did not produce WMBP_WEIGHT_V1 samples after sending 'w'")


def poll_worker(
    base_url: str,
    requests: list[PollRequest],
    stop_event: threading.Event,
    stats: dict[str, dict[str, int | float]],
    stats_lock: threading.Lock,
) -> None:
    next_due = {request.endpoint: time.monotonic() for request in requests}
    while not stop_event.is_set():
        now = time.monotonic()
        soonest = now + 1.0
        for request in requests:
            due = next_due[request.endpoint]
            if now >= due:
                url = base_url.rstrip("/") + request.endpoint
                started = time.monotonic()
                status = "ok"
                try:
                    with urllib.request.urlopen(url, timeout=1.5) as response:
                        response.read(2048)
                        if response.status >= 400:
                            status = "error"
                except (urllib.error.URLError, TimeoutError, OSError):
                    status = "error"
                elapsed_ms = (time.monotonic() - started) * 1000.0
                with stats_lock:
                    endpoint_stats = stats.setdefault(
                        request.endpoint,
                        {"ok": 0, "error": 0, "max_ms": 0.0, "total_ms": 0.0},
                    )
                    endpoint_stats[status] = int(endpoint_stats[status]) + 1
                    endpoint_stats["total_ms"] = float(endpoint_stats["total_ms"]) + elapsed_ms
                    endpoint_stats["max_ms"] = max(float(endpoint_stats["max_ms"]), elapsed_ms)
                next_due[request.endpoint] = now + request.interval_s
            soonest = min(soonest, next_due[request.endpoint])
        stop_event.wait(max(0.02, min(0.25, soonest - time.monotonic())))


def fetch_dashboard_snapshot(base_url: str) -> dict[str, object]:
    url = base_url.rstrip("/") + "/api/dashboard"
    try:
        with urllib.request.urlopen(url, timeout=1.5) as response:
            return json.loads(response.read(8192).decode("utf-8"))
    except (json.JSONDecodeError, urllib.error.URLError, TimeoutError, OSError) as exc:
        return {"error": str(exc)}


def acquisition_snapshot(dashboard: dict[str, object]) -> dict[str, object]:
    return {key: dashboard.get(key, "-") for key in ACQUISITION_KEYS}


def analyze(samples: list[Sample]) -> dict[str, object]:
    if len(samples) < 2:
        return {"samples": len(samples), "valid": False}

    host_intervals_ms = [
        (b.host_time - a.host_time) * 1000.0
        for a, b in zip(samples, samples[1:])
        if b.host_time >= a.host_time
    ]
    device_intervals_ms = [
        float(b.device_ms - a.device_ms)
        for a, b in zip(samples, samples[1:])
        if b.device_ms >= a.device_ms
    ]
    missing_sequence = 0
    for a, b in zip(samples, samples[1:]):
        expected = (a.sequence + 1) & 0xFFFFFFFF
        if b.sequence != expected:
            missing_sequence += 1

    host_span_s = samples[-1].host_time - samples[0].host_time
    device_span_s = (samples[-1].device_ms - samples[0].device_ms) / 1000.0
    dropped_delta = samples[-1].dropped - samples[0].dropped
    hx_values = [sample.hx711_hz for sample in samples if math.isfinite(sample.hx711_hz)]
    quality_values = [sample.quality for sample in samples]

    return {
        "valid": True,
        "samples": len(samples),
        "host_rate_hz": (len(samples) - 1) / host_span_s if host_span_s > 0 else 0.0,
        "device_rate_hz": (len(samples) - 1) / device_span_s if device_span_s > 0 else 0.0,
        "host_p50_ms": percentile(host_intervals_ms, 0.50),
        "host_p95_ms": percentile(host_intervals_ms, 0.95),
        "host_max_ms": max(host_intervals_ms) if host_intervals_ms else None,
        "device_p50_ms": percentile(device_intervals_ms, 0.50),
        "device_p95_ms": percentile(device_intervals_ms, 0.95),
        "device_max_ms": max(device_intervals_ms) if device_intervals_ms else None,
        "device_gaps_over_100ms": sum(1 for value in device_intervals_ms if value > 100.0),
        "device_gaps_over_250ms": sum(1 for value in device_intervals_ms if value > 250.0),
        "missing_sequence": missing_sequence,
        "dropped_delta": dropped_delta,
        "reported_hx711_hz_avg": statistics.fmean(hx_values) if hx_values else None,
        "reported_hx711_hz_min": min(hx_values) if hx_values else None,
        "reported_hx711_hz_max": max(hx_values) if hx_values else None,
        "quality_avg": statistics.fmean(quality_values) if quality_values else None,
        "quality_min": min(quality_values) if quality_values else None,
    }


def print_summary(
    profile: str,
    result: dict[str, object],
    poll_stats: dict[str, dict[str, int | float]],
    acquisition: dict[str, object] | None = None,
) -> None:
    print(f"\n== {profile} ==")
    if not result.get("valid"):
        print(f"samples={result.get('samples', 0)} (not enough data)")
        return
    print(
        "samples={samples} deviceRate={device_rate_hz:.2f}Hz hostRate={host_rate_hz:.2f}Hz "
        "device p50/p95/max={device_p50_ms:.1f}/{device_p95_ms:.1f}/{device_max_ms:.1f}ms "
        "host p50/p95/max={host_p50_ms:.1f}/{host_p95_ms:.1f}/{host_max_ms:.1f}ms".format(**result)
    )
    print(
        "gaps>100ms={device_gaps_over_100ms} gaps>250ms={device_gaps_over_250ms} "
        "missingSeq={missing_sequence} droppedDelta={dropped_delta} "
        "reportedHx711={reported_hx711_hz_avg:.2f}Hz qualityAvg={quality_avg:.1f} minQuality={quality_min}".format(**result)
    )
    if poll_stats:
        total_requests = sum(int(stats["ok"]) + int(stats["error"]) for stats in poll_stats.values())
        print(f"polling: total={total_requests}")
        print("polling:")
        for endpoint, stats in sorted(poll_stats.items()):
            total = int(stats["ok"]) + int(stats["error"])
            avg_ms = float(stats["total_ms"]) / total if total else 0.0
            print(
                f"  {endpoint}: ok={stats['ok']} err={stats['error']} "
                f"avg={avg_ms:.1f}ms max={float(stats['max_ms']):.1f}ms"
            )
    if acquisition:
        print(
            "acquisition: model={acquisition_model} polls={acquisition_poll_count} "
            "ready={acquisition_ready_count} notReady={acquisition_not_ready_count} "
            "accepted={acquisition_accepted_count} raw={acquisition_raw_read_count} "
            "lostSlots={acquisition_estimated_lost_cadence_slots} rejected={acquisition_rejected_count} "
            "rawExpectedUs={raw_read_expected_interval_us} rawAvgUs={raw_read_avg_interval_us} "
            "rawMaxUs={raw_read_max_interval_us} rawReadMaxUs={raw_read_max_duration_us} "
            "readErrors={acquisition_read_error_count} disconnected={acquisition_disconnected_count} "
            "dataReady={acquisition_data_ready_notifications} busySkips={acquisition_busy_skip_count} "
            "timeouts={acquisition_timeout_count}".format(**acquisition)
        )


def assert_pass(profile: str, result: dict[str, object], args: argparse.Namespace) -> list[str]:
    if not result.get("valid"):
        return [f"{profile}: not enough USB samples"]
    failures: list[str] = []
    if float(result["device_rate_hz"]) < args.min_device_rate_hz:
        failures.append(
            f"{profile}: device rate {float(result['device_rate_hz']):.2f}Hz < {args.min_device_rate_hz:.2f}Hz"
        )
    if float(result["device_max_ms"]) > args.max_device_gap_ms:
        failures.append(
            f"{profile}: max device gap {float(result['device_max_ms']):.1f}ms > {args.max_device_gap_ms:.1f}ms"
        )
    if int(result["device_gaps_over_100ms"]) > args.max_gaps_over_100ms:
        failures.append(
            f"{profile}: gaps >100ms {result['device_gaps_over_100ms']} > {args.max_gaps_over_100ms}"
        )
    if int(result["missing_sequence"]) > 0:
        failures.append(f"{profile}: missing/non-contiguous sequence count {result['missing_sequence']}")
    if int(result["dropped_delta"]) > 0:
        failures.append(f"{profile}: USB dropped frames increased by {result['dropped_delta']}")
    return failures


def assert_polling_pass(
    profile: str,
    poll_stats: dict[str, dict[str, int | float]],
) -> list[str]:
    failures: list[str] = []
    for endpoint, stats in sorted(poll_stats.items()):
        if int(stats["error"]) > 0:
            failures.append(f"{profile}: {endpoint} polling errors={stats['error']}")
    return failures


def run_profile(reader: SerialReader, base_url: str, profile: str, duration_s: float) -> tuple[dict[str, object], dict[str, dict[str, int | float]]]:
    requests = PROFILES[profile]
    stop_event = threading.Event()
    poll_stats: dict[str, dict[str, int | float]] = {}
    stats_lock = threading.Lock()
    poll_threads: list[threading.Thread] = []
    if requests:
        for worker_index in range(PROFILE_WORKERS.get(profile, 1)):
            poll_thread = threading.Thread(
                target=poll_worker,
                args=(base_url, requests, stop_event, poll_stats, stats_lock),
                name=f"poll-{profile}-{worker_index + 1}",
                daemon=True,
            )
            poll_thread.start()
            poll_threads.append(poll_thread)

    reader.drain_samples()
    collected: list[Sample] = []
    deadline = time.monotonic() + duration_s
    while time.monotonic() < deadline:
        collected.extend(reader.drain_samples())
        time.sleep(0.05)
    collected.extend(reader.drain_samples())

    stop_event.set()
    for poll_thread in poll_threads:
        poll_thread.join(timeout=2.0)
    return analyze(collected), poll_stats


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="auto", help="USB serial port, or auto")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--base-url", default="http://192.168.4.1")
    parser.add_argument(
        "--profile",
        action="append",
        choices=sorted(PROFILES),
        default=None,
        help="Profile to run. Can be provided multiple times.",
    )
    parser.add_argument("--duration", type=float, default=20.0, help="Seconds per profile")
    parser.add_argument("--warmup", type=float, default=2.0, help="Initial sample warmup seconds")
    parser.add_argument("--min-device-rate-hz", type=float, default=78.0)
    parser.add_argument("--max-device-gap-ms", type=float, default=150.0)
    parser.add_argument("--max-gaps-over-100ms", type=int, default=0)
    parser.add_argument("--no-fail", action="store_true", help="Print failures but exit 0")
    parser.add_argument(
        "--leave-stream-on",
        action="store_true",
        help="Do not toggle USB stream off if this script enabled it",
    )
    parser.add_argument("--json-output", type=Path, help="Optional path for machine-readable results")
    return parser


def main(argv: Iterable[str] | None = None) -> int:
    args = build_arg_parser().parse_args(argv)
    profiles = args.profile or ["baseline", "dashboard-safe"]
    port = discover_port() if args.port == "auto" else args.port

    print(f"Opening {port} at {args.baud} baud")
    reader = SerialReader(port, args.baud)
    reader.start()
    stream_was_enabled_by_test = False
    all_results: dict[str, dict[str, object]] = {}
    all_poll_stats: dict[str, dict[str, dict[str, int | float]]] = {}
    failures: list[str] = []

    try:
        stream_was_enabled_by_test = ensure_usb_stream(reader)
        print("USB WMBP_WEIGHT_V1 stream active")
        if args.warmup > 0:
            time.sleep(args.warmup)
            reader.drain_samples()

        for profile in profiles:
            result, poll_stats = run_profile(reader, args.base_url, profile, args.duration)
            all_results[profile] = result
            all_poll_stats[profile] = poll_stats
            dashboard = fetch_dashboard_snapshot(args.base_url)
            acquisition = acquisition_snapshot(dashboard)
            if acquisition:
                result["acquisition"] = acquisition
            print_summary(profile, result, poll_stats, acquisition)
            if profile not in {"aggressive-repro", "static-repro", "dashboard-2hz-repro"}:
                failures.extend(assert_pass(profile, result, args))
            if profile not in {"aggressive-repro", "static-repro", "dashboard-2hz-repro", "connection-churn"}:
                failures.extend(assert_polling_pass(profile, poll_stats))

        if args.json_output:
            args.json_output.write_text(
                json.dumps({"results": all_results, "polling": all_poll_stats, "failures": failures}, indent=2),
                encoding="utf-8",
            )
    finally:
        if stream_was_enabled_by_test and not args.leave_stream_on:
            try:
                reader.write_line("w")
            except Exception:
                pass
        reader.stop()

    if failures:
        print("\nFAIL")
        for failure in failures:
            print(f"- {failure}")
        return 0 if args.no_fail else 1

    print("\nPASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
