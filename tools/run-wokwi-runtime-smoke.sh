#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
env_name="${1:-esp32s3-xiao-sim-80sps}"
timeout_ms="${WMBP_WOKWI_TIMEOUT_MS:-35000}"
out_dir="${repo_root}/.pio/wokwi/${env_name}"
project_dir="${out_dir}/project"
log_file="${out_dir}/serial.log"
json_file="${out_dir}/analysis.json"
wokwi_stdout_file="${out_dir}/wokwi-cli.log"
diagram_file="${WMBP_WOKWI_DIAGRAM:-${repo_root}/diagram.json}"
scenario_file="${WMBP_WOKWI_SCENARIO:-${repo_root}/wokwi/usb-weight-stream.scenario.yaml}"

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

cd "${repo_root}"
pio run -e "${env_name}"

rm -rf "${project_dir}"
mkdir -p "${project_dir}"
mkdir -p "${project_dir}/chips"

cat > "${project_dir}/wokwi.toml" <<EOF
[wokwi]
version = 1
firmware = "../../../build/${env_name}/firmware.bin"
elf = "../../../build/${env_name}/firmware.elf"
EOF

if [[ ! -f "${diagram_file}" ]]; then
  echo "Wokwi diagram not found: ${diagram_file}" >&2
  exit 2
fi

cp "${diagram_file}" "${project_dir}/diagram.json"
mkdir -p "${project_dir}/wokwi"
if [[ ! -f "${scenario_file}" ]]; then
  echo "Wokwi scenario not found: ${scenario_file}" >&2
  exit 2
fi

cp "${scenario_file}" "${project_dir}/wokwi/runtime-smoke.scenario.yaml"

for chip_source in "${repo_root}"/wokwi/*.chip.c; do
  [[ -e "${chip_source}" ]] || continue
  chip_base="$(basename "${chip_source}" .chip.c)"
  chip_json="${repo_root}/wokwi/${chip_base}.chip.json"
  if [[ ! -f "${chip_json}" ]]; then
    echo "Wokwi custom chip JSON not found for ${chip_source}: ${chip_json}" >&2
    exit 2
  fi
  echo "Compiling Wokwi custom chip: ${chip_base}"
  wokwi-cli chip compile "${chip_source}" -o "${project_dir}/chips/${chip_base}.chip.wasm"
  cp "${chip_json}" "${project_dir}/chips/${chip_base}.chip.json"
  cat >> "${project_dir}/wokwi.toml" <<EOF

[[chip]]
name = '${chip_base}'
binary = 'chips/${chip_base}.chip.wasm'
EOF
done

echo "Running Wokwi runtime smoke for ${env_name}"
echo "Diagram: ${diagram_file}"
echo "Scenario: ${scenario_file}"
echo "Serial log: ${log_file}"
echo "Wokwi CLI log: ${wokwi_stdout_file}"

if ! wokwi-cli "${project_dir}" \
    --scenario "wokwi/runtime-smoke.scenario.yaml" \
    --serial-log-file "${log_file}" \
    --timeout "${timeout_ms}" \
    --timeout-exit-code 0 \
    >"${wokwi_stdout_file}" 2>&1; then
  tail -n 160 "${wokwi_stdout_file}" >&2 || true
  exit 1
fi

analyzer_args=(
  "${repo_root}/tools/analyze-wmbp-serial-log.py"
  "${log_file}"
  --label "wokwi:${env_name}"
  --json-output "${json_file}"
  --min-device-rate-hz "${WMBP_WOKWI_MIN_DEVICE_RATE_HZ:-78.0}"
  --max-device-gap-ms "${WMBP_WOKWI_MAX_DEVICE_GAP_MS:-150.0}"
  --max-gaps-over-100ms "${WMBP_WOKWI_MAX_GAPS_OVER_100MS:-0}"
)

if [[ "${WMBP_WOKWI_NO_FAIL:-0}" == "1" ]]; then
  analyzer_args+=(--no-fail)
fi

python3 "${analyzer_args[@]}"

echo "Analysis JSON: ${json_file}"
