#!/usr/bin/env bash
# Guarded runner for the acquisition matrix in wokwi/matrix.json.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
matrix_file="${WMBP_WOKWI_MATRIX:-${repo_root}/wokwi/matrix.json}"
mode="list"
budget_seconds=0
run_all=0
profiles=()

usage() {
  cat <<'EOF'
Usage:
  tools/run-wokwi-matrix.sh                         # list only
  tools/run-wokwi-matrix.sh --dry-run PROFILE...
  tools/run-wokwi-matrix.sh --budget-seconds N PROFILE...
  tools/run-wokwi-matrix.sh --budget-seconds N --all

Rows marked lintOnly build and lint but never start the simulator. Runtime
rows require an explicit aggregate budget at least as large as their timeouts.
EOF
}

while [[ "$#" -gt 0 ]]; do
  case "$1" in
    --list) mode="list"; shift ;;
    --dry-run) mode="dry-run"; shift ;;
    --budget-seconds)
      [[ "$#" -ge 2 ]] || { echo "--budget-seconds requires a value" >&2; exit 2; }
      budget_seconds="$2"; mode="run"; shift 2 ;;
    --all) run_all=1; shift ;;
    -h|--help) usage; exit 0 ;;
    --*) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    *) profiles+=("$1"); shift ;;
  esac
done

case "${budget_seconds}" in
  ''|*[!0-9]*) echo "--budget-seconds must be a non-negative integer" >&2; exit 2 ;;
esac
[[ -f "${matrix_file}" ]] || { echo "Matrix not found: ${matrix_file}" >&2; exit 2; }
if [[ "${run_all}" == "1" && "${#profiles[@]}" -gt 0 ]]; then
  echo "Use either --all or named profiles, not both." >&2
  exit 2
fi
if [[ "${mode}" == "run" && "${run_all}" == "0" && "${#profiles[@]}" -eq 0 ]]; then
  echo "Refusing an implicit full-matrix run. Name profiles or pass --all." >&2
  exit 2
fi

python_args=("${matrix_file}")
if [[ "${run_all}" == "0" ]]; then
  python_args+=("${profiles[@]}")
fi

rows=()
while IFS= read -r row; do
  rows+=("${row}")
done < <(python3 - "${python_args[@]}" <<'PY'
import json, sys
from pathlib import Path

matrix = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
wanted = set(sys.argv[2:])
known = {run["name"] for run in matrix["runs"]}
unknown = sorted(wanted - known)
if unknown:
    raise SystemExit("Unknown matrix profile(s): " + ", ".join(unknown))
for run in matrix["runs"]:
    if wanted and run["name"] not in wanted:
        continue
    print("\x1f".join([
        run["name"], run["env"], run["diagram"], run["scenario"],
        str(run.get("timeoutMs", "")),
        str(run["minDeviceRateHz"]), str(run["maxDeviceGapMs"]),
        str(run["maxGapsOver100ms"]), "1" if run.get("vcd") else "0",
        "1" if run.get("lintOnly") else "0", run.get("why", ""),
    ]))
PY
)

[[ "${#rows[@]}" -gt 0 ]] || { echo "No matrix rows selected." >&2; exit 2; }

required_budget=0
for row in "${rows[@]}"; do
  IFS=$'\x1f' read -r name env diagram scenario timeout_ms min_hz max_gap max_gaps vcd lint_only why <<<"${row}"
  printf '%-28s %s\n' "${name}" "${env}"
  printf '  %s | %s\n' "${diagram}" "${scenario}"
  printf '  timeoutMs=%s minHz=%s maxGapMs=%s maxGaps>100ms=%s vcd=%s lintOnly=%s\n' \
    "${timeout_ms:-default}" "${min_hz}" "${max_gap}" "${max_gaps}" "${vcd}" "${lint_only}"
  [[ -n "${why}" ]] && printf '  %s\n' "${why}"
  if [[ "${lint_only}" != "1" ]]; then
    row_timeout="${timeout_ms:-35000}"
    required_budget="$(( required_budget + (row_timeout + 999) / 1000 ))"
  fi
  echo
done

echo "Runtime simulation budget required: ${required_budget}s"
if [[ "${mode}" == "list" || "${mode}" == "dry-run" ]]; then
  exit 0
fi
if (( budget_seconds < required_budget )); then
  echo "Refusing run: approved ${budget_seconds}s, requires ${required_budget}s." >&2
  exit 2
fi

failed=()
run_stamp="$(date -u +%Y%m%dT%H%M%SZ)"
for row in "${rows[@]}"; do
  IFS=$'\x1f' read -r name env diagram scenario timeout_ms min_hz max_gap max_gaps vcd lint_only why <<<"${row}"
  echo "== ${name} (${env})"
  if ! "${repo_root}/tools/check-wokwi-diagram.py" "${repo_root}/${diagram}"; then
    failed+=("${name}:offline-lint"); continue
  fi

  if [[ "${lint_only}" == "1" ]]; then
    if ! (cd "${repo_root}" && pio run -e "${env}"); then
      failed+=("${name}:build"); continue
    fi
    if ! wokwi-cli lint "${repo_root}/${diagram}" --warnings-as-errors; then
      failed+=("${name}:wokwi-lint"); continue
    fi
    echo "== ${name}: PASS (lint/build; simulator not started)"
    continue
  fi

  row_timeout="${timeout_ms:-35000}"
  row_budget="$(( (row_timeout + 999) / 1000 ))"
  if WMBP_WOKWI_DIAGRAM="${repo_root}/${diagram}" \
     WMBP_WOKWI_SCENARIO="${repo_root}/${scenario}" \
     WMBP_WOKWI_TIMEOUT_MS="${row_timeout}" \
     WMBP_WOKWI_VCD="${vcd}" \
     WMBP_WOKWI_MIN_DEVICE_RATE_HZ="${min_hz}" \
     WMBP_WOKWI_MAX_DEVICE_GAP_MS="${max_gap}" \
     WMBP_WOKWI_MAX_GAPS_OVER_100MS="${max_gaps}" \
     WMBP_WOKWI_RUN_NAME="matrix-${name}-${run_stamp}" \
     WMBP_WOKWI_RUN_CONFIRMED=1 \
     WMBP_WOKWI_BUDGET_SECONDS="${row_budget}" \
     "${repo_root}/tools/run-wokwi-runtime-smoke.sh" "${env}"; then
    echo "== ${name}: PASS"
  else
    failed+=("${name}")
  fi
done

if [[ "${#failed[@]}" -gt 0 ]]; then
  echo "MATRIX FAIL: ${failed[*]}" >&2
  exit 1
fi
echo "MATRIX PASS (${#rows[@]} row(s))"
