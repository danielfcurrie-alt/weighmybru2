#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

mode="${1:-quick}"

echo "== WMB+ beta6 regression =="
echo "mode: ${mode}"

echo
echo "== Host tests =="
tools/run-host-tests.sh

echo
echo "== Static hardening checks =="

if rg -n 'scale\.tare\(|scale->tare\(|scale\.getRawValue\(' \
  src/WebServer.cpp src/BluetoothScale.cpp src/main.cpp src/TouchSensor.cpp src/ScaleCommandQueue.cpp; then
  echo "ERROR: direct async/web/BLE scale tare or raw HX711 access found above."
  exit 1
fi
echo "OK: no direct async/web/BLE scale tare/raw HX711 access in checked paths."

if rg -n '"password"\s*:' src/WebServer.cpp data/settings.html; then
  echo "ERROR: API/UI appears to emit a password JSON field."
  exit 1
fi
echo "OK: no password JSON response field in checked paths."

if git ls-files --error-unmatch tailwindcss.exe >/dev/null 2>&1; then
  echo "ERROR: tailwindcss.exe is still tracked."
  exit 1
fi
echo "OK: tailwindcss.exe is not tracked."

if ! rg -n '^tailwindcss\.exe$' .gitignore >/dev/null; then
  echo "ERROR: tailwindcss.exe is not ignored."
  exit 1
fi
echo "OK: tailwindcss.exe is ignored."

echo
echo "== Diff hygiene =="
git diff --check
git diff --cached --check

echo
echo "== PlatformIO builds =="
if [[ "${mode}" == "full" ]]; then
  pio run \
    -e esp32s3-xiao \
    -e esp32s3-supermini \
    -e esp32s3-tinys3d \
    -e esp32s3-xiao-sim-80sps
else
  pio run \
    -e esp32s3-xiao \
    -e esp32s3-xiao-sim-80sps
fi

echo
echo "WMB+ beta6 regression passed."
