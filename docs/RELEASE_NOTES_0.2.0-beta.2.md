# WMB+ 0.2.0-beta.2

Second public WMB+ beta firmware for XIAO ESP32S3 WeighMyBru-compatible scale builds.

This release fixes the beta.1 packaging problem: XIAO factory images now use the WMB+ dual-OTA partition table and include a real full factory image with LittleFS web UI assets.

## Highlights

- Fixes XIAO release packaging so the recommended `factory-full` image includes bootloader, dual-OTA partition table, app firmware, and LittleFS.
- XIAO dual-OTA layout:
  - `app0` at `0x10000`
  - `app1` at `0x310000`
  - LittleFS at `0x610000`
- Adds release assets for app-only, factory-minimal, factory-full, LittleFS, bootloader, partitions, manifest, build info, and SHA-256 checksums.
- Keeps the WMB+ `0.2.0-beta.1` feature set:
  - real 80 SPS WMB+ BLE/USB telemetry on 80 SPS HX711 hardware
  - clean 20 Hz legacy Float32 compatibility stream
  - standard BLE Battery Service
  - WMB+ extended telemetry
  - physical-parity BLE tare behavior
  - atomic tare/start command
  - firmware-side quality diagnostics
  - bump/glitch diagnostics and one-frame glitch rejection
  - near-zero stability cleanup
  - USB-C serial weight capture
  - StopMyBru HTTP webhook relay support
  - StopMyBru target stop learning
  - web update UI support

## Release assets

Primary XIAO assets:

- `wmb-plus-0.2.0-beta.2-xiao-app.bin`
- `wmb-plus-0.2.0-beta.2-xiao-factory-full.bin`
- `wmb-plus-0.2.0-beta.2-xiao-factory-minimal.bin`
- `wmb-plus-0.2.0-beta.2-xiao-littlefs.bin`
- `wmb-plus-0.2.0-beta.2-xiao-bootloader.bin`
- `wmb-plus-0.2.0-beta.2-xiao-partitions.bin`
- `manifest-xiao.json`
- `build-info-xiao.json`
- `wmb-plus-0.2.0-beta.2-sha256.txt`

SuperMini assets may be published for build validation, but XIAO ESP32S3 remains the primary beta-supported target.

## Install recommendation

For XIAO ESP32S3 first install or migration from beta.1/single-app layouts:

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX --baud 460800 write_flash 0x0 wmb-plus-0.2.0-beta.2-xiao-factory-full.bin
```

This writes the WMB+ dual-OTA partition table and LittleFS web UI in one full factory flash. It does not require `erase_flash`, but calibration/settings should be checked afterward.

For later USB app-only updates after the dual-OTA layout is already installed:

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX --baud 460800 write_flash 0x10000 wmb-plus-0.2.0-beta.2-xiao-app.bin
```

## Known limitations

- Battery percentage is voltage-estimated.
- Charging and runtime estimates are experimental.
- StopMyBru target stop learning is experimental and should be validated with a safe smart plug/relay setup before unattended use.
- Calibration is per-device and must be verified by the builder.
- 80 SPS requires HX711 hardware support/configuration.

## Suggested beta tests

- Confirm full factory install boots with LittleFS available.
- Confirm Updates page reports app OTA ready after full factory install.
- Bean Conqueror compatibility.
- WeighMyBru/GaggiMate 20-byte compatibility.
- WMB+ aware app telemetry.
- USB serial capture.
- Physical and app-triggered tare.
- Real espresso shot cadence and quality.
- StopMyBru webhook cutoff and learned target offset.
- Web OTA app and LittleFS update flow.
- 10 minute idle drift.
- Sleep/wake.
- Battery/charging behavior.
