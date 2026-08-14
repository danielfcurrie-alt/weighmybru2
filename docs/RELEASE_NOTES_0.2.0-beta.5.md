# WMB+ 0.2.0-beta.5 release notes

Beta 5 is a hotfix for testers using the web Updates page.

## What changed from beta 4

- Fixed LittleFS / Web UI OTA uploads failing with `Update begin failed: Bad Size Given`.
- Clarified OTA wording in the README, flashing guide, beta overview, and Updates page source.
- Added a direct WMB+ GitHub Releases link to the Updates page with a board-specific asset hint.
- Added a browser-side "Check GitHub releases" helper that finds the latest WMB+ beta and shows matching app/LittleFS asset links for the detected board.

## Root cause

The web OTA handler was passing the multipart HTTP request body size to `Update.begin()`.
For LittleFS uploads, that body size is slightly larger than the `.bin` file because it includes form-data overhead.
The XIAO and TinyS3[D] LittleFS image can exactly fill the filesystem partition, so the ESP32 Update library correctly rejected the oversized request length.

Beta 5 lets the ESP32 Update library use the actual target partition size instead.

## OTA status

Beta 5 supports manual browser-upload OTA after the dual-OTA partition table is installed.

- First install or partition migration still uses `factory-full.bin` over USB at `0x0`.
- That factory-full image creates the dual-OTA layout.
- Later firmware updates can use the local Updates page with the matching `-app.bin` file.
- Later web UI updates can use the local Updates page with the matching `-littlefs.bin` file.
- GitHub self-update from the scale is not implemented in beta 5. The Updates page can check GitHub from the browser and provide manual download links.

## Firmware behavior

No BLE protocol, sample cadence, tare, battery, USB serial, StopMyBru, or scale-quality runtime behavior is intentionally changed from beta 4.

## Recommended upgrade path from beta 4

1. Use the beta 4 Updates page to upload `wmb-plus-0.2.0-beta.5-xiao-app.bin`.
2. Let the scale reboot.
3. Confirm the version reports `0.2.0-beta.5`.
4. Then upload `wmb-plus-0.2.0-beta.5-xiao-littlefs.bin` from the Updates page.

Do not use `factory-full.bin` in the App Firmware OTA field. Factory images are for USB flashing at `0x0`.
