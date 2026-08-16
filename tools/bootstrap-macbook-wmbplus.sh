#!/usr/bin/env bash
set -euo pipefail

WMB_DIR="${WMB_DIR:-/Users/dan/FrankenBru-workspaces/firmware/weighmybru2-pr1-existing-device-quality}"
REMOTE_URL="${WMB_REMOTE_URL:-https://github.com/danielfcurrie-alt/weighmybru2.git}"
BRANCH="${WMB_BRANCH:-wmb-plus/beta-0.2.0}"
TOOLS_ROOT="${WMB_TOOLS_ROOT:-/Users/dan/FrankenBru-workspaces/tools}"
VENV_DIR="${WMB_VENV_DIR:-$TOOLS_ROOT/wmbplus-venv}"
SCALE_URL_IP="${WMB_SCALE_URL_IP:-http://192.168.86.23}"
SCALE_URL_MDNS="${WMB_SCALE_URL_MDNS:-http://wmb.local}"

say() {
  printf '\n=== %s ===\n' "$1"
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1
}

timestamp() {
  date +%Y%m%d-%H%M%S
}

say "WMB+ MacBook bootstrap"
echo "Workspace: $WMB_DIR"
echo "Branch:    $BRANCH"
echo "Remote:    $REMOTE_URL"
echo "Tools:     $TOOLS_ROOT"

mkdir -p "$(dirname "$WMB_DIR")" "$TOOLS_ROOT"

if [ -e "$WMB_DIR" ] && [ ! -d "$WMB_DIR/.git" ]; then
  backup="${WMB_DIR}.nongit-backup-$(timestamp)"
  say "Preserving existing non-git workspace"
  echo "Moving:"
  echo "  $WMB_DIR"
  echo "to:"
  echo "  $backup"
  mv "$WMB_DIR" "$backup"
fi

if [ ! -e "$WMB_DIR" ]; then
  say "Cloning WMB+ firmware repo"
  git clone --branch "$BRANCH" "$REMOTE_URL" "$WMB_DIR"
fi

cd "$WMB_DIR"

say "Git workspace"
git rev-parse --show-toplevel
git remote -v

if ! git diff --quiet || ! git diff --cached --quiet; then
  echo "ERROR: WMB+ workspace has uncommitted tracked changes. Refusing to pull over them."
  git status --short
  exit 2
fi

if [ -n "$(git ls-files --others --exclude-standard)" ]; then
  echo "Note: untracked files are present; leaving them alone:"
  git ls-files --others --exclude-standard | sed -n '1,80p'
fi

git fetch origin "$BRANCH"
git checkout "$BRANCH"
git pull --ff-only origin "$BRANCH"

say "Python tool venv"
if [ ! -x "$VENV_DIR/bin/python" ]; then
  python3 -m venv "$VENV_DIR"
fi
"$VENV_DIR/bin/python" -m pip install --upgrade pip
"$VENV_DIR/bin/python" -m pip install --upgrade platformio pyserial esptool

export PATH="$VENV_DIR/bin:$HOME/bin:$HOME/.wokwi/bin:$PATH"

say "Wokwi CLI"
if ! need_cmd wokwi-cli; then
  echo "wokwi-cli not found; installing with official Wokwi CI installer."
  curl -L https://wokwi.com/ci/install.sh | sh
  export PATH="$HOME/bin:$HOME/.wokwi/bin:$PATH"
fi

if need_cmd wokwi-cli; then
  wokwi-cli --version || true
else
  echo "WARNING: wokwi-cli still not on PATH after install attempt."
fi

say "Tool versions"
python3 --version || true
"$VENV_DIR/bin/python" --version || true
pio --version || true
"$VENV_DIR/bin/python" -m esptool version || true
echo "WOKWI_CLI_TOKEN present: ${WOKWI_CLI_TOKEN:+yes}"

say "Required Wokwi files"
missing=0
for f in \
  platformio.ini \
  wokwi.toml \
  diagram.json \
  diagram.xiao-94hz-loaded.json \
  diagram.xiao-94hz-missed-ready.json \
  docs/WOKWI_TESTING_ROADMAP.md \
  tools/run-wokwi-runtime-smoke.sh \
  tools/runtime-cadence-smoke.py \
  tools/analyze-wmbp-serial-log.py \
  wokwi/hx711-80.chip.c \
  wokwi/hx711-80.chip.json \
  wokwi/usb-weight-stream-midstream-tare.scenario.yaml
do
  if [ -e "$f" ]; then
    echo "OK   $f"
  else
    echo "MISS $f"
    missing=$((missing + 1))
  fi
done

say "USB serial ports"
"$VENV_DIR/bin/python" -m serial.tools.list_ports -v || true

say "Scale HTTP checks"
echo "--- mDNS: $SCALE_URL_MDNS/api/device/info"
curl -m 3 -sS "$SCALE_URL_MDNS/api/device/info" | head -c 700 || true
echo
echo "--- IP: $SCALE_URL_IP/api/device/info"
curl -m 3 -sS "$SCALE_URL_IP/api/device/info" | head -c 700 || true
echo
echo "--- Dashboard: $SCALE_URL_IP/api/dashboard"
curl -m 3 -sS "$SCALE_URL_IP/api/dashboard" | head -c 700 || true
echo

say "Generated artifact inventory"
du -sh . .pio build-output releases 2>/dev/null || true
find . -maxdepth 3 \( -name '*.bin' -o -name '*.elf' -o -name '*.map' -o -name '*.vcd' \) -print 2>/dev/null | sed -n '1,160p'

say "Disk"
df -h "$WMB_DIR"

if [ "$missing" -ne 0 ]; then
  echo "Bootstrap finished, but $missing required file(s) are missing."
  exit 3
fi

say "Done"
echo "WMB+ MacBook workspace is git-backed and ready."
echo "Use this venv for local hardware tests:"
echo "  export PATH=\"$VENV_DIR/bin:\\$HOME/bin:\\$HOME/.wokwi/bin:\\$PATH\""
