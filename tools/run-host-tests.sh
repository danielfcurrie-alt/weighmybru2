#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${repo_root}/.pio/host-tests"

mkdir -p "${build_dir}"

c++ \
  -std=c++17 \
  -Wall \
  -Wextra \
  -Werror \
  -I"${repo_root}/include" \
  "${repo_root}/test/host/test_battery_drain_session.cpp" \
  -o "${build_dir}/test_battery_drain_session"

"${build_dir}/test_battery_drain_session"
