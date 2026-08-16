#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  tools/build-release-assets.sh [version] [environment...]

Examples:
  tools/build-release-assets.sh
  tools/build-release-assets.sh 0.2.0-beta.6 esp32s3-xiao
  tools/build-release-assets.sh 0.2.0-beta.8 esp32s3-xiao esp32s3-supermini esp32s3-tinys3d

Builds app firmware, LittleFS, factory-minimal, factory-full, ESP32 Web Tools
manifests, build-info JSON, and SHA-256 checksums into:

  build-output/<version>/

This helper does not tag, push, or create a GitHub Release.
USAGE
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

repo_root="$(git rev-parse --show-toplevel)"
cd "${repo_root}"

version="${1:-}"
if [[ -n "${version}" ]]; then
  shift
else
  major="$(awk '$1 == "#define" && $2 == "WEIGHMYBRU_VERSION_MAJOR" {print $3; exit}' include/Version.h)"
  minor="$(awk '$1 == "#define" && $2 == "WEIGHMYBRU_VERSION_MINOR" {print $3; exit}' include/Version.h)"
  patch="$(awk '$1 == "#define" && $2 == "WEIGHMYBRU_VERSION_PATCH" {print $3; exit}' include/Version.h)"
  prerelease="$(awk '$1 == "#define" && $2 == "WEIGHMYBRU_VERSION_PRERELEASE" {print $3; exit}' include/Version.h | tr -d '"')"
  version="${major}.${minor}.${patch}"
  if [[ -n "${prerelease}" ]]; then
    version="${version}-${prerelease}"
  fi
fi

if [[ "$#" -gt 0 ]]; then
  envs=("$@")
else
  envs=(esp32s3-xiao esp32s3-supermini esp32s3-tinys3d)
fi

if ! command -v pio >/dev/null 2>&1; then
  echo "PlatformIO 'pio' was not found in PATH." >&2
  exit 1
fi

if ! python3 -m esptool version >/dev/null 2>&1; then
  echo "python3 -m esptool is not available. Install esptool or use a Python environment that has it." >&2
  exit 1
fi

out_dir="build-output/${version}"
mkdir -p "${out_dir}"

build_number="${WMBP_BUILD_NUMBER:-0}"
commit_hash="$(git rev-parse --short HEAD)"
build_date="$(date -u +"%Y-%m-%d")"
build_time="$(date -u +"%H:%M:%S")"

export WMBP_BUILD_NUMBER="${build_number}"
export WMBP_COMMIT_HASH="${commit_hash}"
export WMBP_BUILD_DATE="${build_date}"
export WMBP_BUILD_TIME="${build_time}"

board_suffix_for_env() {
  case "$1" in
    esp32s3-xiao) echo "xiao" ;;
    esp32s3-supermini) echo "supermini" ;;
    esp32s3-tinys3d) echo "tinys3d" ;;
    *) echo "Unsupported release environment: $1" >&2; return 1 ;;
  esac
}

board_name_for_env() {
  case "$1" in
    esp32s3-xiao) echo "WMB+ - XIAO ESP32S3" ;;
    esp32s3-supermini) echo "WMB+ - ESP32-S3 Supermini" ;;
    esp32s3-tinys3d) echo "WMB+ - TinyS3[D]" ;;
    *) return 1 ;;
  esac
}

littlefs_offset_for_env() {
  case "$1" in
    esp32s3-xiao|esp32s3-tinys3d) echo "0x610000" ;;
    esp32s3-supermini) echo "0x310000" ;;
    *) return 1 ;;
  esac
}

factory_size_for_env() {
  case "$1" in
    esp32s3-xiao|esp32s3-tinys3d) echo "8388608" ;;
    esp32s3-supermini) echo "4194304" ;;
    *) return 1 ;;
  esac
}

for env_name in "${envs[@]}"; do
  board_suffix="$(board_suffix_for_env "${env_name}")"
  board_name="$(board_name_for_env "${env_name}")"
  littlefs_offset="$(littlefs_offset_for_env "${env_name}")"
  expected_factory_size="$(factory_size_for_env "${env_name}")"
  base_name="wmb-plus-${version}-${board_suffix}"
  build_dir=".pio/build/${env_name}"

  echo "== Building ${env_name} (${base_name}) =="
  pio run -e "${env_name}"
  pio run -e "${env_name}" -t buildfs

  test -f "${build_dir}/firmware.bin"
  test -f "${build_dir}/littlefs.bin"
  test -f "${build_dir}/bootloader.bin"
  test -f "${build_dir}/partitions.bin"

  cp "${build_dir}/firmware.bin" "${out_dir}/${base_name}-app.bin"
  cp "${build_dir}/littlefs.bin" "${out_dir}/${base_name}-littlefs.bin"
  cp "${build_dir}/bootloader.bin" "${out_dir}/${base_name}-bootloader.bin"
  cp "${build_dir}/partitions.bin" "${out_dir}/${base_name}-partitions.bin"

  python3 -m esptool --chip esp32s3 merge_bin \
    -o "${out_dir}/${base_name}-factory-minimal.bin" \
    0x0 "${build_dir}/bootloader.bin" \
    0x8000 "${build_dir}/partitions.bin" \
    0x10000 "${build_dir}/firmware.bin"

  python3 -m esptool --chip esp32s3 merge_bin \
    -o "${out_dir}/${base_name}-factory-full.bin" \
    0x0 "${build_dir}/bootloader.bin" \
    0x8000 "${build_dir}/partitions.bin" \
    0x10000 "${build_dir}/firmware.bin" \
    "${littlefs_offset}" "${build_dir}/littlefs.bin"

  actual_factory_size="$(stat -f%z "${out_dir}/${base_name}-factory-full.bin")"
  if [[ "${actual_factory_size}" != "${expected_factory_size}" ]]; then
    echo "Factory-full size mismatch for ${env_name}: expected ${expected_factory_size}, got ${actual_factory_size}" >&2
    exit 1
  fi

  cat > "${out_dir}/manifest-${board_suffix}.json" <<EOF
{
  "name": "${board_name}",
  "version": "${version}",
  "home_assistant_domain": "weighmybru",
  "new_install_prompt_erase": false,
  "funding_url": "https://github.com/danielfcurrie-alt/weighmybru2",
  "builds": [
    {
      "chipFamily": "ESP32-S3",
      "parts": [
        { "path": "${base_name}-bootloader.bin", "offset": 0 },
        { "path": "${base_name}-partitions.bin", "offset": 32768 },
        { "path": "${base_name}-app.bin", "offset": 65536 },
        { "path": "${base_name}-littlefs.bin", "offset": $((littlefs_offset)) }
      ]
    }
  ]
}
EOF

  cat > "${out_dir}/build-info-${board_suffix}.json" <<EOF
{
  "version": "${version}",
  "board": "${env_name}",
  "build_number": ${build_number},
  "commit_hash": "${commit_hash}",
  "build_date": "${build_date}",
  "build_time": "${build_time}",
  "is_release": false,
  "factory_full_size": ${actual_factory_size},
  "littlefs_offset": "${littlefs_offset}"
}
EOF
done

(
  cd "${out_dir}"
  rm -f "wmb-plus-${version}-sha256.txt"
  find . -maxdepth 1 -type f ! -name "wmb-plus-${version}-sha256.txt" -print \
    | sed 's#^\./##' \
    | sort \
    | while IFS= read -r file; do
        shasum -a 256 "$file"
      done > "wmb-plus-${version}-sha256.txt"
)

echo "Built local release assets in ${out_dir}"
ls -lh "${out_dir}"
