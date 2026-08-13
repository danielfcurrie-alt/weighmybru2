# Flashing WMB+ beta

These instructions are for the WMB+ beta XIAO ESP32S3 build.

## Release assets

The release provides three firmware artifacts:

- `wmb-plus-0.2.0-beta.1-xiao-app.bin`
  - App image only.
  - Flash at `0x10000`.
  - Use when upgrading an existing compatible install.

- `wmb-plus-0.2.0-beta.1-xiao-factory-minimal.bin`
  - Merged factory image.
  - Flash at `0x0`.
  - Includes bootloader, partition table, OTA data, and app firmware.
  - Does not include web UI filesystem assets.

- `wmb-plus-0.2.0-beta.1-xiao-littlefs.bin`
  - Web UI filesystem image.
  - Flash at `0x310000`.
  - Optional unless you want to update the web UI assets.

Scale, BLE, USB serial, display, battery, and sleep features are firmware features and do not require a web UI filesystem update.

## App-only upgrade

Use this only if the device already has a compatible ESP32-S3 bootloader and partition table.

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX --baud 460800 write_flash 0x10000 wmb-plus-0.2.0-beta.1-xiao-app.bin
```

This does not erase NVS or calibration.

## Fresh install with minimal merged image

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX --baud 460800 write_flash 0x0 wmb-plus-0.2.0-beta.1-xiao-factory-minimal.bin
```

Use this for a clean firmware install on a XIAO ESP32S3. The web UI may be unavailable until the separate LittleFS filesystem image is flashed.

## Optional LittleFS web UI update

Use this only when you want to update the web UI filesystem assets:

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX --baud 460800 write_flash 0x310000 wmb-plus-0.2.0-beta.1-xiao-littlefs.bin
```

This does not erase NVS or calibration.

## Verify after flashing

Open serial at `115200` and send:

```text
z
```

Expected indicators:

- Banner includes `WMB+ v0.2.0-beta.1`.
- Board is `XIAO ESP32S3`.
- BLE name is `WeighMyBru+`.
- `legacyFloat32Cadence=20Hz`.
- USB weight stream commands include `w` and `W`.
- HX711 reports the expected cadence, usually about `80 Hz` for 80 SPS builds.

## Do not erase unless you mean it

Avoid `erase_flash` for normal beta updates. Erasing flash removes NVS state such as WiFi settings, calibration, and stored diagnostics.
