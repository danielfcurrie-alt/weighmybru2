#!/usr/bin/env python3
"""Guard dashboard polling so the web UI cannot silently damage 80 SPS cadence.

This is intentionally a lightweight source-level regression check. The firmware
simulation build proves the 80 SPS path still compiles; this script catches the
specific web-dashboard mistakes that caused periodic acquisition stalls:

- duplicate weight polling via /api/weight-fast
- idle dashboard polling
- static system/OTA refresh intervals
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DASHBOARD = REPO_ROOT / "data" / "index.html"

def fail(message: str) -> None:
    print(f"dashboard polling check failed: {message}", file=sys.stderr)
    raise SystemExit(1)


def main() -> None:
    text = DASHBOARD.read_text(encoding="utf-8")

    if "fetch('/api/weight-fast')" in text or 'fetch("/api/weight-fast")' in text:
        fail("dashboard must not poll /api/weight-fast separately; reuse /api/dashboard")

    update_weight_intervals = re.findall(r"setInterval\(\s*updateWeight\b", text)
    if update_weight_intervals:
        fail("idle dashboard must not poll updateWeight; refresh is manual")

    if re.search(r"setInterval\(\s*refreshStaticSystemInfo\b", text):
        fail("idle dashboard must not poll static system info; refresh is manual")

    low_priority_intervals = [
        int(value)
        for value in re.findall(
            r"setInterval\(\s*\(\)\s*=>\s*updateWeight\(\{recording:\s*true\}\)\s*,\s*(\d+)\s*\)",
            text,
        )
    ]
    if low_priority_intervals and min(low_priority_intervals) < 2000:
        fail("recording refresh interval must stay at or above 2000 ms")

    print("Dashboard polling check passed (idle manual refresh, recording low-priority)")


if __name__ == "__main__":
    main()
