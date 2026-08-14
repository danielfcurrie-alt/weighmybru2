# WMB+ 0.2.0-beta.3 release notes

Third public WMB+ beta firmware for WeighMyBru-compatible ESP32-S3 scale builds.

This release is intended to make GitHub Releases the canonical place to get OTA and first-install assets. After a tester has installed the matching `factory-full` image once, later updates can use the `app.bin` and `littlefs.bin` assets from the GitHub release through the scale's local Updates page.

## Highlights

- Publishes app-only, LittleFS, factory-minimal, and factory-full assets through GitHub Releases.
- Keeps the corrected dual-OTA partition layout from beta.2.
- Adds TinyS3[D] development release assets.
- Hardens critical-battery sleep behavior and HX711 power-down preparation.
- Keeps production sleep-touch serial logging quiet by default.
- ADC-backed boards now expose visible battery percentage in stable 5% steps while diagnostics retain raw voltage/raw percent.
- Fuel-gauge boards keep finer battery state-of-charge reporting.

## Release assets

XIAO ESP32S3:

- `wmb-plus-0.2.0-beta.3-xiao-app.bin`
- `wmb-plus-0.2.0-beta.3-xiao-factory-full.bin`
- `wmb-plus-0.2.0-beta.3-xiao-factory-minimal.bin`
- `wmb-plus-0.2.0-beta.3-xiao-littlefs.bin`

ESP32-S3 SuperMini:

- `wmb-plus-0.2.0-beta.3-supermini-app.bin`
- `wmb-plus-0.2.0-beta.3-supermini-factory-full.bin`
- `wmb-plus-0.2.0-beta.3-supermini-factory-minimal.bin`
- `wmb-plus-0.2.0-beta.3-supermini-littlefs.bin`

Unexpected Maker TinyS3[D]:

- `wmb-plus-0.2.0-beta.3-tinys3d-app.bin`
- `wmb-plus-0.2.0-beta.3-tinys3d-factory-full.bin`
- `wmb-plus-0.2.0-beta.3-tinys3d-factory-minimal.bin`
- `wmb-plus-0.2.0-beta.3-tinys3d-littlefs.bin`

Shared:

- `manifest-xiao.json`
- `manifest-supermini.json`
- `manifest-tinys3d.json`
- `build-info-xiao.json`
- `build-info-supermini.json`
- `build-info-tinys3d.json`
- `wmb-plus-0.2.0-beta.3-sha256.txt`

## OTA test path

1. Install the matching `factory-full.bin` image once over USB.
2. Enable WiFi/AP and open the scale web UI.
3. Open the Updates page.
4. Confirm firmware OTA says Ready.
5. Upload the matching `app.bin` from this GitHub release.
6. Wait for reboot.
7. Confirm the version changed and the running OTA partition flipped.
8. Upload the matching `littlefs.bin` only when web UI assets need updating.

## Notes

- Do not cross-flash board assets.
- Do not run `erase_flash` for normal beta updates.
- Calibration should be preserved because normal release flashes do not overwrite NVS, but builders should still verify with a known weight after install.
- Web OTA is local-network only; keep the scale powered during upload.
