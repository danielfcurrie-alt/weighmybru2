# Codex prompt: install WMB+ from GitHub Release assets

Use this prompt when you want Codex to install a published WMB+ release onto a supported ESP32-S3 scale from GitHub Release files.

This prompt is intentionally release-asset based. It does not sync or build source code. That makes the install reproducible for testers.

```text
This is a WMB+ firmware install task.

Goal: install the published WMB+ beta full factory image onto a supported ESP32-S3 scale using GitHub Release artifacts.

Default target:
- Board: Seeed Studio XIAO ESP32S3
- Expected flash size: 8MB
- Release asset: wmb-plus-0.2.0-beta.2-xiao-factory-full.bin
- Expected flash offset: 0x0
- Expected XIAO LittleFS offset inside the full image: 0x610000

Safety rules:
- Do not use SuperMini assets for a XIAO board.
- Do not run erase_flash.
- Do not restore stock firmware.
- Do not build from local source unless the release asset is missing or invalid and I explicitly approve a source build.
- Do not sync source from another machine.
- Do not write files into unrelated app project directories.
- Do not assume project-specific Codex skills are installed. Use ordinary shell commands and explain any local fallback you use.
- Stop before flashing and ask for exact confirmation.

Release repo:
  https://github.com/danielfcurrie-alt/weighmybru2

Release/tag:
  v0.2.0-beta.2

Required assets:
  wmb-plus-0.2.0-beta.2-xiao-factory-full.bin
  wmb-plus-0.2.0-beta.2-sha256.txt, if present

Why this image:
- It includes bootloader, the WMB+ dual-OTA partition table, app firmware, and LittleFS web UI.
- It migrates a XIAO ESP32S3 device to the WMB+ dual-OTA layout in one USB flash.
- After this one-time factory install, future app firmware and web UI updates can use the web OTA page.
- This may reset settings/calibration/NVS because the partition table/layout is being migrated, so recalibration may be needed.

Steps:

1. Create a clean release download folder outside any app project:

   mkdir -p ~/FrankenBru-workspaces/releases/wmb-plus-0.2.0-beta.2
   cd ~/FrankenBru-workspaces/releases/wmb-plus-0.2.0-beta.2

2. Download release assets.

   Prefer GitHub CLI if available:

   gh release download v0.2.0-beta.2 \
     --repo danielfcurrie-alt/weighmybru2 \
     --pattern 'wmb-plus-0.2.0-beta.2-xiao-factory-full.bin' \
     --pattern 'wmb-plus-0.2.0-beta.2-sha256.txt' \
     --clobber

   If GitHub CLI is unavailable, use the GitHub release page or curl. Do not proceed until the full factory image is present locally.

3. Verify files:

   ls -lh

   Required:
   - wmb-plus-0.2.0-beta.2-xiao-factory-full.bin

4. Verify full factory image size:

   python3 - <<'PY'
   from pathlib import Path
   p = Path("wmb-plus-0.2.0-beta.2-xiao-factory-full.bin")
   print(p, p.stat().st_size)
   if p.stat().st_size != 8388608:
       raise SystemExit("ERROR: XIAO full factory image is not 8MB / 8388608 bytes")
   PY

5. Verify SHA-256:

   shasum -a 256 wmb-plus-0.2.0-beta.2-xiao-factory-full.bin

   If wmb-plus-0.2.0-beta.2-sha256.txt exists, compare the hash against the entry for:

   wmb-plus-0.2.0-beta.2-xiao-factory-full.bin

   Stop if the SHA does not match.

6. Resolve an esptool command.

   Do not assume `python3 -m esptool` is the only valid path. If a dedicated esptool virtualenv is already present and working, use it instead of installing another copy.

   Define this helper in the current shell:

   run_esptool() {
     if python3 -m esptool version >/dev/null 2>&1; then
       python3 -m esptool "$@"
       return
     fi

     if command -v esptool.py >/dev/null 2>&1; then
       esptool.py "$@"
       return
     fi

     if [ -f "$HOME/.platformio/packages/tool-esptoolpy/esptool.py" ]; then
       python3 "$HOME/.platformio/packages/tool-esptoolpy/esptool.py" "$@"
       return
     fi

     while IFS= read -r py; do
       if [ -x "$py" ] && "$py" -m esptool version >/dev/null 2>&1; then
         "$py" -m esptool "$@"
         return
       fi
     done < <(find "$HOME/.venvs" "$HOME/venvs" "$HOME/FrankenBru-workspaces" -maxdepth 5 -path '*/bin/python' -type f 2>/dev/null)

     echo "No existing esptool found. Installing esptool into the user Python environment..."
     python3 -m pip install --user esptool
     python3 -m esptool "$@"
   }

   Verify it:

   run_esptool version

   Report which path was used if it was not system `python3 -m esptool`, for example:

   - dedicated esptool venv
   - PlatformIO bundled esptool
   - `esptool.py` from PATH
   - newly installed user Python esptool

7. Identify the serial port:

   python3 -m serial.tools.list_ports -v

   Choose the ESP32-S3 serial port. It often looks like:

   /dev/cu.usbmodem1101

8. Verify chip and flash before writing:

   run_esptool --chip esp32s3 --port <actual-port> flash_id

   Confirm:
   - Chip is ESP32-S3
   - Flash is 8MB for XIAO
   - This is the intended scale

9. Pre-flash confirmation:

   Print exactly what will be flashed:

   Image:
   wmb-plus-0.2.0-beta.2-xiao-factory-full.bin

   Offset:
   0x0

   Port:
   <actual-port>

   Warning:
   This writes bootloader, partition table, app firmware, and LittleFS. It does not run erase_flash, but settings/calibration may need to be restored or redone because the partition layout is being migrated.

   Stop and ask me to type exactly:

   FLASH_FULL_FACTORY

   Do not continue unless I type exactly FLASH_FULL_FACTORY.

10. Flash the full factory image:

   run_esptool --chip esp32s3 \
     --port <actual-port> \
     --baud 460800 \
     write_flash 0x0 wmb-plus-0.2.0-beta.2-xiao-factory-full.bin

   Do not run erase_flash.
   Do not flash any SuperMini file.
   Do not flash a separate LittleFS image after this full factory image unless there is a specific reason.

11. Post-flash serial check:

   Open serial monitor at 115200:

   pio device monitor --port <actual-port> --baud 115200

   Send:

   z

   Capture/report:
   - firmware banner/version
   - Board, expected XIAO ESP32S3
   - BLE advertising name, expected WeighMyBru+
   - WiFi state
   - LittleFS/web server status
   - OTA/update status if shown
   - decoded/observed partition layout if available
   - HX711 cadence
   - battery
   - whether calibration/settings appear reset

12. Optional functional checks:

   - Confirm ScaleBench can read USB serial.
   - Confirm BLE advertises as WeighMyBru+.
   - Confirm Bean Conqueror still reads weight.
   - Confirm web UI loads if WiFi/AP is enabled.
   - Confirm Updates page exists and app OTA reports ready.

13. Final report:

   Include:
   - downloaded asset path
   - which esptool path was used
   - SHA-256
   - file size
   - detected serial port
   - chip/flash_id summary
   - flash command result
   - serial/config output summary
   - whether the installed release appears dual-OTA
   - whether recalibration appears needed
   - confirm no erase_flash, no restore, no source sync, and no source build were run
```
