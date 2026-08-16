#!/usr/bin/env python3
"""Stamp every PlatformIO firmware build with reproducible build metadata.

Release packaging already stamped official builds, but normal local `pio run`
builds could still fall back to `+build.0.unknown`. That made two different
beta images look identical at runtime. This pre-build hook keeps the release
script behavior and also makes ad-hoc local builds identify their source commit.
"""

from __future__ import annotations

from datetime import datetime, timezone
import os
import subprocess
from typing import Optional

Import("env")  # type: ignore[name-defined]


def git_output(project_dir: str, *args: str) -> Optional[str]:
    try:
        return subprocess.check_output(
            ["git", *args],
            cwd=project_dir,
            stderr=subprocess.DEVNULL,
            text=True,
        ).strip()
    except Exception:
        return None


project_dir = env.subst("$PROJECT_DIR")  # type: ignore[name-defined]
now = datetime.now(timezone.utc)

build_number = os.environ.get("WMBP_BUILD_NUMBER", "0")
commit_hash = os.environ.get("WMBP_COMMIT_HASH") or git_output(
    project_dir, "rev-parse", "--short=12", "HEAD"
) or "unknown"

dirty_status = git_output(
    project_dir, "status", "--porcelain", "--untracked-files=no"
)
if dirty_status and not commit_hash.endswith("-dirty"):
    commit_hash = f"{commit_hash}-dirty"

build_date = os.environ.get("WMBP_BUILD_DATE", now.strftime("%Y-%m-%d"))
build_time = os.environ.get("WMBP_BUILD_TIME", now.strftime("%H:%M:%S"))

env.Append(  # type: ignore[name-defined]
    CPPDEFINES=[
        ("WEIGHMYBRU_BUILD_NUMBER", build_number),
        ("WEIGHMYBRU_COMMIT_HASH", f'\\"{commit_hash}\\"'),
        ("WEIGHMYBRU_BUILD_DATE", f'\\"{build_date}\\"'),
        ("WEIGHMYBRU_BUILD_TIME", f'\\"{build_time}\\"'),
    ]
)

print(
    "[wmb-build-meta] "
    f"build={build_number} commit={commit_hash} utc={build_date}T{build_time}Z"
)
