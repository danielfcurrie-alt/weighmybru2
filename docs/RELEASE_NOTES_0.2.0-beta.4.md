# WMB+ 0.2.0-beta.4 release notes

Beta 4 is a release-readiness update for testers moving from beta 3.

## What changed from beta 3

- Clarified the Updates page: WMB+ can remain its own access point for setup and manual OTA uploads; GitHub self-update is not implemented in this beta.
- Updated the firmware version string to `0.2.0-beta.4`.
- Updated release documentation and quick-flash examples to beta 4 asset names.
- Updated ESP32 Web Tools manifests to avoid prompting testers to erase flash by default.
- Updated GitHub release metadata so release links point at the tagged README instead of a moving default branch.

## OTA status

Beta 4 supports manual browser-upload OTA after the dual-OTA partition table is installed.

- First install or partition migration still uses `factory-full.bin` over USB at `0x0`.
- That factory-full image creates the dual-OTA layout.
- Later firmware updates can use the local Updates page with the matching `-app.bin` file.
- Later web UI updates can use the local Updates page with the matching `-littlefs.bin` file.
- GitHub self-update from the scale is not implemented in beta 4.

## Firmware behavior

No BLE protocol, sample cadence, tare, battery, USB serial, StopMyBru, or scale-quality runtime behavior is intentionally changed from beta 3.

## Primary validation target

- Seeed Studio XIAO ESP32S3
- HX711 configured for 80 SPS
- BLE advertising as `WeighMyBru+`
- Standard BLE Battery Service enabled
- WMB+ 20-byte stream enabled
- Float32 compatibility stream enabled
- USB serial stream enabled

## Compatibility status

- Bean Conqueror Float32 compatibility has been hardware-tested.
- GaggiMate / Gaggiuino-style 20-byte WeighMyBru compatibility is expected to remain stock-compatible.
- WMB+-aware tools can use the 20-byte extended stream, capabilities characteristic, and USB serial stream for diagnostics.

## Install guidance

Use the matching board asset from the GitHub release.

- First install or partition migration: use `factory-full.bin` at `0x0`.
- App-only update from an existing beta 3 dual-OTA install: use `app.bin` at `0x10000`, or upload the app image from the local Updates page.
- Web UI-only update: use `littlefs.bin` at the board-specific LittleFS offset.

Do not run `erase_flash` for normal beta updates. Erasing flash removes calibration, WiFi settings, and stored diagnostics.
