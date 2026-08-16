#!/usr/bin/env python3
"""Guard dashboard polling so the web UI cannot silently damage 80 SPS cadence.

This is intentionally a lightweight source-level regression check. The firmware
simulation build proves the 80 SPS path still compiles; this script catches the
specific web-dashboard mistakes that caused periodic acquisition stalls:

- duplicate weight polling via /api/weight-fast
- static system/OTA refresh every few seconds
- dashboard live polling faster than the budgeted cadence
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DASHBOARD = REPO_ROOT / "data" / "index.html"

MIN_DASHBOARD_POLL_MS = 500
MIN_STATIC_INFO_POLL_MS = 60_000


def fail(message: str) -> None:
    print(f"dashboard polling check failed: {message}", file=sys.stderr)
    raise SystemExit(1)


def main() -> None:
    text = DASHBOARD.read_text(encoding="utf-8")

    if "fetch('/api/weight-fast')" in text or 'fetch("/api/weight-fast")' in text:
        fail("dashboard must not poll /api/weight-fast separately; reuse /api/dashboard")

    dashboard_intervals = [
        int(value)
        for value in re.findall(r"setInterval\(\s*updateWeight\s*,\s*(\d+)\s*\)", text)
    ]
    if not dashboard_intervals:
        fail("no updateWeight polling interval found")
    too_fast = [value for value in dashboard_intervals if value < MIN_DASHBOARD_POLL_MS]
    if too_fast:
        fail(
            "updateWeight interval below "
            f"{MIN_DASHBOARD_POLL_MS} ms: {', '.join(map(str, too_fast))}"
        )

    static_intervals = [
        int(value)
        for value in re.findall(
            r"setInterval\(\s*refreshStaticSystemInfo\s*,\s*(\d+)\s*\)",
            text,
        )
    ]
    if not static_intervals:
        fail("no refreshStaticSystemInfo polling interval found")
    too_fast_static = [value for value in static_intervals if value < MIN_STATIC_INFO_POLL_MS]
    if too_fast_static:
        fail(
            "static system/OTA refresh interval below "
            f"{MIN_STATIC_INFO_POLL_MS} ms: {', '.join(map(str, too_fast_static))}"
        )

    print(
        "Dashboard polling check passed "
        f"(updateWeight intervals={dashboard_intervals}, "
        f"static intervals={static_intervals})"
    )


if __name__ == "__main__":
    main()
