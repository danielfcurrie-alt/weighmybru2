# Flashing WMB+ beta

These instructions are for the WMB+ `0.2.0-beta.3` XIAO ESP32S3 build.

## Release assets

The release provides four primary XIAO firmware artifacts:

- `wmb-plus-0.2.0-beta.3-xiao-app.bin`
  - App image only.
  - Flash at `0x10000`.
  - Use when upgrading an existing compatible install.

- `wmb-plus-0.2.0-beta.3-xiao-factory-full.bin`
  - Recommended merged factory image.
  - Flash at `0x0`.
  - Includes bootloader, partition table, app firmware, and LittleFS web UI filesystem assets.
  - Use for first-time beta installs and partition-table migration.

- `wmb-plus-0.2.0-beta.3-xiao-factory-minimal.bin`
  - Advanced/recovery merged factory image.
  - Flash at `0x0`.
  - Includes bootloader, partition table, and app firmware.
  - Intentionally does not include web UI filesystem assets.

- `wmb-plus-0.2.0-beta.3-xiao-littlefs.bin`
  - Web UI filesystem image.
  - Flash at `0x610000` for XIAO `0.2.0-beta.3` dual-OTA.
  - Optional unless you want to update the web UI assets.

Scale, BLE, USB serial, display, battery, and sleep features are firmware features and do not require a web UI filesystem update. The web dashboard, OTA pages, and StopMyBru browser UI do require LittleFS assets.

## App-only upgrade

Use this only if the device already has a compatible ESP32-S3 bootloader and partition table.
For web app OTA, the device must have the WMB+ dual-OTA partition table. If the device was flashed with the originally published `0.2.0-beta.1` fallback assets or another legacy/single-app layout, use the `0.2.0-beta.3` factory-full image once first.

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX --baud 460800 write_flash 0x10000 wmb-plus-0.2.0-beta.3-xiao-app.bin
```

This does not erase NVS or calibration.

## Fresh install with recommended full merged image

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX --baud 460800 write_flash 0x0 wmb-plus-0.2.0-beta.3-xiao-factory-full.bin
```

Use this for a clean firmware install on a XIAO ESP32S3. It installs the dual-OTA partition table required for future web app OTA and includes the LittleFS web UI.

## Advanced minimal merged image

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX --baud 460800 write_flash 0x0 wmb-plus-0.2.0-beta.3-xiao-factory-minimal.bin
```

Use this only when you intentionally want to leave the LittleFS web UI filesystem untouched or unavailable.

## Optional LittleFS web UI update

Use this only when you want to update the web UI filesystem assets:

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX --baud 460800 write_flash 0x610000 wmb-plus-0.2.0-beta.3-xiao-littlefs.bin
```

The XIAO `0.2.0-beta.3` dual-OTA partition table places LittleFS at `0x610000`. SuperMini dual-OTA places LittleFS at `0x310000`. Do not use the SuperMini address on a XIAO build.

This does not erase NVS or calibration.

## Verify after flashing

Open serial at `115200` and send:

```text
z
```

Expected indicators:

- Banner includes `WMB+ v0.2.0-beta.3`.
- Board is `XIAO ESP32S3`.
- BLE name is `WeighMyBru+`.
- `legacyFloat32Cadence=20Hz`.
- USB weight stream commands include `w` and `W`.
- HX711 reports the expected cadence, usually about `80 Hz` for 80 SPS builds.
- Updates page reports firmware OTA as ready after the dual-OTA partition table is installed.

## Do not erase unless you mean it

Avoid `erase_flash` for normal beta updates. Erasing flash removes NVS state such as WiFi settings, calibration, and stored diagnostics.
