#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
baseline_ref="${WMBP_BETA6_REF:-c043e1a76aca}"
env_name="${WMBP_WOKWI_ENV:-esp32s3-xiao-sim-80sps}"
diagram_file="${WMBP_WOKWI_DIAGRAM:-${repo_root}/diagram.json}"
scenario_file="${WMBP_WOKWI_SCENARIO:-${repo_root}/wokwi/usb-weight-stream.scenario.yaml}"
timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
out_root="${repo_root}/.pio/wokwi-compare/${timestamp}"
baseline_tree="${out_root}/beta6-worktree"
beta6_out="${out_root}/beta6"
beta7_out="${out_root}/beta7"
baseline_cache="${WMBP_WOKWI_BASELINE_JSON:-${repo_root}/test/fixtures/wokwi/beta6-${baseline_ref}-${env_name}.analysis.json}"
refresh_baseline="${WMBP_WOKWI_REFRESH_BASELINE:-0}"

if ! command -v wokwi-cli >/dev/null 2>&1; then
  cat >&2 <<'EOF'
wokwi-cli is not installed.

Install:
  curl -L https://wokwi.com/ci/install.sh | sh

Then set your Wokwi token:
  export WOKWI_CLI_TOKEN=...

EOF
  exit 127
fi

if [[ -z "${WOKWI_CLI_TOKEN:-}" ]]; then
  cat >&2 <<'EOF'
WOKWI_CLI_TOKEN is not set.

Create a Wokwi CI token, then run:
  export WOKWI_CLI_TOKEN=...

EOF
  exit 2
fi

# ---------------------------------------------------------------------------
# Scope limit: this comparison can ONLY run on an env that BOTH refs can build.
#
# beta6 (c043e1a) has no wokwi/ directory, no diagrams, and no
# *-wokwi-hx711-dout-interrupt envs - only the *-sim-* envs. A sim build never
# reads DT/SCK, so this comparison is structurally incapable of exercising the
# DOUT acquisition path, and its results must not be read as DOUT evidence.
# It is a non-acquisition regression check.
# ---------------------------------------------------------------------------
case "${env_name}" in
  *-sim-*) ;;
  *)
    cat >&2 <<EOF
REFUSING TO RUN: '${env_name}' is not a simulation env.

The beta6 baseline (${baseline_ref}) predates the Wokwi HX711 harness and cannot
build this environment. Only *-sim-* envs exist in both refs.

For acquisition/DOUT coverage use the single-ref smoke instead:
  tools/run-wokwi-runtime-smoke.sh esp32s3-xiao-wokwi-hx711-dout-interrupt
EOF
    exit 2
    ;;
esac

mkdir -p "${out_root}"

cleanup() {
  if git -C "${repo_root}" worktree list --porcelain | grep -Fq "worktree ${baseline_tree}"; then
    git -C "${repo_root}" worktree remove --force "${baseline_tree}" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

copy_harness_into_baseline() {
  # Both refs must run the SAME diagram and scenario or the comparison is
  # meaningless. Previously this hardcoded diagram.json +
  # usb-weight-stream.scenario.yaml, so setting WMBP_WOKWI_DIAGRAM or
  # WMBP_WOKWI_SCENARIO silently fed beta6 and beta7 different inputs.
  mkdir -p "${baseline_tree}/wokwi" "${baseline_tree}/tools"
  cp "${diagram_file}" "${baseline_tree}/$(basename "${diagram_file}")"
  cp "${scenario_file}" "${baseline_tree}/wokwi/$(basename "${scenario_file}")"
  # Custom chip sources: beta6 has no wokwi/ dir of its own.
  for chip_file in "${repo_root}"/wokwi/*.chip.c "${repo_root}"/wokwi/*.chip.json; do
    [[ -e "${chip_file}" ]] || continue
    cp "${chip_file}" "${baseline_tree}/wokwi/"
  done
  cp "${repo_root}/tools/analyze-wmbp-serial-log.py" "${baseline_tree}/tools/analyze-wmbp-serial-log.py"
  cp "${repo_root}/tools/run-wokwi-runtime-smoke.sh" "${baseline_tree}/tools/run-wokwi-runtime-smoke.sh"
  chmod +x "${baseline_tree}/tools/analyze-wmbp-serial-log.py" "${baseline_tree}/tools/run-wokwi-runtime-smoke.sh"
}

run_one() {
  local label="$1"
  local worktree="$2"
  local output_dir="$3"

  echo
  echo "== Running ${label} (${env_name}) =="
  (
    cd "${worktree}"
    WMBP_WOKWI_NO_FAIL=1 \
      WMBP_WOKWI_DIAGRAM="${worktree}/$(basename "${diagram_file}")" \
      WMBP_WOKWI_SCENARIO="${worktree}/wokwi/$(basename "${scenario_file}")" \
      tools/run-wokwi-runtime-smoke.sh "${env_name}"
  )

  mkdir -p "${output_dir}"
  cp "${worktree}/.pio/wokwi/${env_name}/serial.log" "${output_dir}/serial.log"
  cp "${worktree}/.pio/wokwi/${env_name}/analysis.json" "${output_dir}/analysis.json"
}

echo "Output: ${out_root}"
echo "Baseline ref: ${baseline_ref}"
echo "Environment: ${env_name}"
echo "Baseline cache: ${baseline_cache}"
echo "Diagram:  ${diagram_file}"
echo "Scenario: ${scenario_file}"
echo "SCOPE: simulation env - acquisition/DOUT path is NOT exercised."

mkdir -p "${beta6_out}"
if [[ "${refresh_baseline}" != "1" && -f "${baseline_cache}" ]]; then
  echo
  echo "== Reusing cached beta6:${baseline_ref} (${env_name}) =="
  cp "${baseline_cache}" "${beta6_out}/analysis.json"
  echo "Cached analysis JSON: ${beta6_out}/analysis.json"
else
  echo
  echo "== Refreshing beta6:${baseline_ref} (${env_name}) =="
  git -C "${repo_root}" worktree add --detach "${baseline_tree}" "${baseline_ref}"
  copy_harness_into_baseline
  run_one "beta6:${baseline_ref}" "${baseline_tree}" "${beta6_out}"
  mkdir -p "$(dirname "${baseline_cache}")"
  cp "${beta6_out}/analysis.json" "${baseline_cache}"
  echo "Saved baseline cache: ${baseline_cache}"
fi

run_one "beta7:current-worktree" "${repo_root}" "${beta7_out}"

python3 - "${beta6_out}/analysis.json" "${beta7_out}/analysis.json" <<'PY'
import json
import sys
from pathlib import Path

def load(path):
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    result = data["result"]
    failures = data.get("failures", [])
    return result, failures

beta6, beta6_failures = load(sys.argv[1])
beta7, beta7_failures = load(sys.argv[2])

fields = [
    ("samples", "samples"),
    ("device_rate_hz", "rate Hz"),
    ("device_p50_ms", "p50 ms"),
    ("device_p95_ms", "p95 ms"),
    ("device_max_ms", "max ms"),
    ("device_gaps_over_100ms", "gaps>100"),
    ("device_gaps_over_250ms", "gaps>250"),
    ("missing_sequence", "missing seq"),
    ("dropped_delta", "dropped"),
    ("reported_hx711_hz_avg", "reported HX Hz"),
    ("quality_avg", "quality avg *"),
]

print("\n== Wokwi Beta 6 vs Beta 7 ==")
print(f"{'metric':<18} {'beta6':>14} {'beta7':>14} {'delta':>14}")
for key, label in fields:
    a = beta6.get(key)
    b = beta7.get(key)
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        delta = b - a
        print(f"{label:<18} {a:>14.3f} {b:>14.3f} {delta:>14.3f}")
    else:
        print(f"{label:<18} {str(a):>14} {str(b):>14} {'-':>14}")

print("\n* quality_avg is INFORMATIONAL ONLY.")
print("  The score computation may differ between the two refs, so a delta here")
print("  is not necessarily a regression - diff the scoring code before acting.")
print("  It is deliberately excluded from the regression gate below.")

if beta6_failures or beta7_failures:
    print("\nAbsolute-profile failures:")
    print("These are expected when Wokwi cannot sustain the real 80 SPS serial stream.")
    print("(Sim builds observed ~40 Hz; the smoke script now uses a 35 Hz floor for them.)")
    print("The comparison pass/fail below is based on Beta 7 regression vs Beta 6.")
    if beta6_failures:
        print("beta6:")
        for failure in beta6_failures:
            print(f"  - {failure}")
    if beta7_failures:
        print("beta7:")
        for failure in beta7_failures:
            print(f"  - {failure}")

regressions = []
rate6 = float(beta6.get("device_rate_hz") or 0.0)
rate7 = float(beta7.get("device_rate_hz") or 0.0)
if rate6 > 0 and rate7 < rate6 * 0.90:
    regressions.append(f"Beta 7 rate dropped more than 10% vs Beta 6: {rate7:.2f}Hz < {rate6 * 0.90:.2f}Hz")

max6 = float(beta6.get("device_max_ms") or 0.0)
max7 = float(beta7.get("device_max_ms") or 0.0)
if max7 > max6 + 50.0:
    regressions.append(f"Beta 7 max interval increased by more than 50ms: {max7:.1f}ms vs {max6:.1f}ms")

gaps6 = int(beta6.get("device_gaps_over_100ms") or 0)
gaps7 = int(beta7.get("device_gaps_over_100ms") or 0)
if gaps7 > gaps6 + 10:
    regressions.append(f"Beta 7 added more than 10 long gaps: {gaps7} vs {gaps6}")

for field in ("missing_sequence", "dropped_delta"):
    value = int(beta7.get(field) or 0)
    if value > 0:
        regressions.append(f"Beta 7 reported {field}={value}")

if regressions:
    print("\nREGRESSION FAIL")
    for regression in regressions:
        print(f"  - {regression}")
    raise SystemExit(1)

print("\nREGRESSION PASS")
PY

echo
echo "Artifacts:"
echo "  ${beta6_out}/analysis.json"
echo "  ${beta7_out}/serial.log"
echo "  ${beta7_out}/analysis.json"
