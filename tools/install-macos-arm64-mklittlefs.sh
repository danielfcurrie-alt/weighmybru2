#!/usr/bin/env bash
set -euo pipefail

# PlatformIO's mklittlefs package can resolve to a macOS x86_64 executable even
# when the package metadata advertises darwin_arm64 support. This helper builds
# the ESP32-oriented mklittlefs 4.0.0 from source as a native arm64 binary and
# installs it into PlatformIO's active package path.

repo_url="https://github.com/jason2866/mklittlefs.git"
version="4.0.0"
pio_pkg_dir="${HOME}/.platformio/packages/tool-mklittlefs"
pio_bin="${pio_pkg_dir}/mklittlefs"
backup_bin="${pio_pkg_dir}/mklittlefs.x86_64.bak"
work_dir="$(mktemp -d /tmp/wmbplus-mklittlefs.XXXXXX)"

cleanup() {
  rm -rf "${work_dir}"
}
trap cleanup EXIT

if [[ "$(uname -s)" != "Darwin" || "$(uname -m)" != "arm64" ]]; then
  echo "This helper is only intended for macOS arm64."
  exit 1
fi

if [[ ! -d "${pio_pkg_dir}" ]]; then
  echo "PlatformIO mklittlefs package not found at ${pio_pkg_dir}."
  echo "Run: pio run -e esp32s3-xiao -t buildfs"
  echo "Then rerun this helper if the tool is still x86_64."
  exit 1
fi

git clone --depth 1 "${repo_url}" "${work_dir}/mklittlefs"
cd "${work_dir}/mklittlefs"
git submodule update --init

make clean
make VERSION="${version}" CXXFLAGS="-Wno-error=vla-cxx-extension"

if ! file ./mklittlefs | grep -q "arm64"; then
  echo "Built mklittlefs is not arm64:"
  file ./mklittlefs
  exit 1
fi

if [[ -f "${pio_bin}" && ! -f "${backup_bin}" ]]; then
  cp "${pio_bin}" "${backup_bin}"
fi

cp ./mklittlefs "${pio_bin}"
chmod +x "${pio_bin}"

echo "Installed native arm64 mklittlefs:"
file "${pio_bin}"
"${pio_bin}" --version
