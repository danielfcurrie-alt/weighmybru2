# Flashing WMB+ beta

These instructions are for WMB+ `0.2.0-beta.9` beta release assets.

Do not guess the board. Pick the asset family that matches the physical board:

| Board | Asset family | Factory-full size | LittleFS offset |
| --- | --- | ---: | ---: |
| Seeed Studio XIAO ESP32S3 | `xiao` | 8,388,608 bytes | `0x610000` |
| ESP32-S3 SuperMini / SuperMini-style board | `supermini` | 4,194,304 bytes | `0x350000` |
| Unexpected Maker TinyS3[D] | `tinys3d` | 8,388,608 bytes | `0x610000` |

If you are unsure whether the board is XIAO, SuperMini, or TinyS3[D], stop and identify it visually before flashing. `esptool flash_id` is still useful to confirm ESP32-S3 and flash size, but it cannot always identify the board model by itself.

## Release assets

The release provides matching firmware artifacts for each supported beta board. The examples below use XIAO names; replace `xiao` with `supermini` or `tinys3d` only when that is the actual board.

- `wmb-plus-0.2.0-beta.9-xiao-app.bin`
  - App image only.
  - Flash at `0x10000`.
  - Use when upgrading an existing compatible install.

- `wmb-plus-0.2.0-beta.9-xiao-factory-full.bin`
  - Recommended merged factory image.
  - Flash at `0x0`.
  - Includes bootloader, partition table, app firmware, and LittleFS web UI filesystem assets.
  - Use for first-time beta installs and partition-table migration.

- `wmb-plus-0.2.0-beta.9-xiao-factory-minimal.bin`
  - Advanced/recovery merged factory image.
  - Flash at `0x0`.
  - Includes bootloader, partition table, and app firmware.
  - Intentionally does not include web UI filesystem assets.

- `wmb-plus-0.2.0-beta.9-xiao-littlefs.bin`
  - Web UI filesystem image.
  - Flash at `0x610000` for XIAO `0.2.0-beta.9` dual-OTA.
  - Optional unless you want to update the web UI assets.

Scale, BLE, USB serial, display, battery, and sleep features are firmware features and do not require a web UI filesystem update. The web dashboard, OTA pages, and StopMyBru browser UI do require LittleFS assets.

## OTA terminology

WMB+ `0.2.0-beta.9` supports GitHub app self-update and manual browser-upload OTA after the matching dual-OTA factory layout is installed.

GitHub app self-update:

1. Connect the scale to home WiFi.
2. Open the scale's local Updates page.
3. Check GitHub releases from the scale.
4. Download the matching board app image into the inactive OTA slot.
5. Tap Install after the page reports that the update is pending.

Manual browser-upload OTA:

1. Download the matching release asset yourself.
2. Open the scale's local Updates page.
3. Upload the matching `-app.bin` for firmware, or `-littlefs.bin` for web UI files.

LittleFS/web UI updates still use manual upload or a factory-full image. GitHub self-update currently stages app firmware, not LittleFS.

The first `factory-full.bin` USB flash is what installs the dual-OTA partition table. After that, app-only updates can use the local Updates page.

## App-only upgrade

Use this only if the device already has a compatible ESP32-S3 bootloader and partition table.
For web app OTA, the device must have the WMB+ dual-OTA partition table. If the device was flashed with the originally published `0.2.0-beta.1` fallback assets or another legacy/single-app layout, use the `0.2.0-beta.9` factory-full image once first.

Replace `xiao` with the correct board asset family only after confirming the board. For first install or unknown partition layout, prefer the matching `factory-full.bin` at `0x0` instead of app-only.

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX --baud 460800 write_flash 0x10000 wmb-plus-0.2.0-beta.9-xiao-app.bin
```

This does not erase NVS or calibration.

## Fresh install with recommended full merged image

Use the matching `factory-full.bin` for the exact board. This is the preferred first-install path because it installs the bootloader, partition table, app firmware, and LittleFS web UI together.

XIAO:

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX --baud 460800 write_flash 0x0 wmb-plus-0.2.0-beta.9-xiao-factory-full.bin
```

SuperMini:

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX --baud 460800 write_flash 0x0 wmb-plus-0.2.0-beta.9-supermini-factory-full.bin
```

TinyS3[D]:

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX --baud 460800 write_flash 0x0 wmb-plus-0.2.0-beta.9-tinys3d-factory-full.bin
```

## Advanced minimal merged image

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX --baud 460800 write_flash 0x0 wmb-plus-0.2.0-beta.9-xiao-factory-minimal.bin
```

Use this only when you intentionally want to leave the LittleFS web UI filesystem untouched or unavailable.

## Optional LittleFS web UI update

Use this only when you want to update the web UI filesystem assets:

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX --baud 460800 write_flash 0x610000 wmb-plus-0.2.0-beta.9-xiao-littlefs.bin
```

The XIAO `0.2.0-beta.9` dual-OTA partition table places LittleFS at `0x610000`. Current SuperMini dual-OTA places LittleFS at `0x350000`. Do not use the SuperMini address on a XIAO build.

This does not erase NVS or calibration.

## Verify after flashing

Open serial at `115200` and send:

```text
z
```

Expected indicators:

- Banner includes `WMB+ v0.2.0-beta.9`.
- Board is `XIAO ESP32S3`.
- BLE name is `WeighMyBru+`.
- `legacyFloat32Cadence=20Hz`.
- USB weight stream commands include `w` and `W`.
- HX711 reports the expected cadence, usually about `80 Hz` for 80 SPS builds.
- Updates page reports firmware OTA as ready after the dual-OTA partition table is installed.

## Do not erase unless you mean it

Avoid `erase_flash` for normal beta updates. Erasing flash removes NVS state such as WiFi settings, calibration, and stored diagnostics.
