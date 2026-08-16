#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${repo_root}/.pio/host-tests"

mkdir -p "${build_dir}"

tests=(
  test_battery_drain_session
  test_battery_simulation_matrix
  test_sample_cadence_tracker
  test_simulation_profiles
)

for test_name in "${tests[@]}"; do
  c++ \
    -std=c++17 \
    -Wall \
    -Wextra \
    -Werror \
    -I"${repo_root}/include" \
    "${repo_root}/test/host/${test_name}.cpp" \
    -o "${build_dir}/${test_name}"

  "${build_dir}/${test_name}"
done

PYTHONDONTWRITEBYTECODE=1 python3 -m ast "${repo_root}/tools/runtime-cadence-smoke.py" >/dev/null
PYTHONDONTWRITEBYTECODE=1 python3 -m ast "${repo_root}/tools/analyze-wmbp-serial-log.py" >/dev/null
python3 "${repo_root}/tools/simulate-battery-matrix.py" --capacity-mah 700 --format json >/dev/null
python3 "${repo_root}/tools/simulate-battery-matrix.py" --capacity-mah 1000 --format json >/dev/null
python3 "${repo_root}/tools/check-dashboard-polling.py"
