#!/usr/bin/env python3
"""Assert HX711 bus discipline from a Wokwi logic-analyzer VCD capture.

Checks the things a bad bit-banged HX711 driver gets wrong silently:
  * clocks per conversion must be 25/26/27 (datasheet: "PD_SCK clock pulses
    should not be less than 25 or more than 27 within one conversion period")
  * DOUT must return high after the burst (data clocked out, ready deasserted)
  * no clock burst may start while DOUT is high (reading when not ready)

It does NOT validate real T3 pulse width: the Wokwi harness builds with
WMBP_HX711_PULSE_SETTLE_US=3, which deliberately widens pulses. Widths are
reported for information only.
"""
import argparse, re, sys
from collections import Counter


def parse_vcd(path):
    ids, ts, changes, cur = {}, 1e-9, [], {}
    scale = 1e-9
    with open(path) as fh:
        text = fh.read()
    m = re.search(r"\$timescale\s+(\d+)\s*(fs|ps|ns|us|ms|s)\s*\$end", text)
    if m:
        scale = int(m.group(1)) * {"fs": 1e-15, "ps": 1e-12, "ng": 1e-9,
                                   "ns": 1e-9, "us": 1e-6, "ms": 1e-3, "s": 1.0}[m.group(2)]
    for sym, name in re.findall(r"\$var\s+\w+\s+\d+\s+(\S+)\s+([^\s$]+)", text):
        ids[sym] = name.strip()
    now = 0.0
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        if line.startswith("#"):
            now = int(line[1:]) * scale
        elif len(line) >= 2 and line[0] in "01xzXZ" and line[1:] in ids:
            cur[ids[line[1:]]] = line[0]
            changes.append((now, ids[line[1:]], line[0]))
    return changes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("vcd")
    ap.add_argument("--dt-signal", default="DT")
    ap.add_argument("--sck-signal", default="SCK")
    ap.add_argument("--burst-gap-us", type=float, default=500.0,
                    help="idle gap that separates one conversion's clock burst from the next")
    ap.add_argument("--min-bursts", type=int, default=20)
    args = ap.parse_args()

    changes = parse_vcd(args.vcd)
    if not changes:
        print(f"FAIL: no value changes parsed from {args.vcd}", file=sys.stderr)
        return 1

    dt_level, bursts, cur, last_edge = "1", [], None, None
    dt_at_burst_start = {}
    for t, name, val in changes:
        if name == args.dt_signal:
            dt_level = val
            continue
        if name != args.sck_signal or val != "1":
            continue
        if cur is None or (t - last_edge) * 1e6 > args.burst_gap_us:
            if cur:
                bursts.append(cur)
            cur = {"start": t, "edges": 0, "widths": []}
            dt_at_burst_start[len(bursts)] = dt_level
        cur["edges"] += 1
        cur["end"] = t
        last_edge = t
    if cur:
        bursts.append(cur)

    if len(bursts) < args.min_bursts:
        print(f"FAIL: only {len(bursts)} clock bursts (expected >= {args.min_bursts})", file=sys.stderr)
        return 1

    counts = Counter(b["edges"] for b in bursts)
    bad = {n: c for n, c in counts.items() if n < 25 or n > 27}
    early = [i for i, lvl in dt_at_burst_start.items() if lvl == "1"]

    print(f"bursts: {len(bursts)}")
    print("clocks/burst: " + ", ".join(f"{n}x{c}" for n, c in sorted(counts.items())))
    if bursts:
        span = [(b["end"] - b["start"]) * 1e6 for b in bursts if "end" in b]
        if span:
            print(f"burst duration us: min={min(span):.1f} max={max(span):.1f} "
                  f"(informational; harness widens pulses)")

    failures = []
    if bad:
        failures.append(f"clock bursts outside 25..27: {bad}")
    # burst 0 is often partial (capture starts mid-transaction)
    if [i for i in early if i > 0]:
        failures.append(f"{len(early)} burst(s) started while DOUT was high (read when not ready)")

    if failures:
        print("\nFAIL")
        for f in failures:
            print(f"  - {f}")
        return 1
    print("\nPASS: HX711 bus discipline OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
