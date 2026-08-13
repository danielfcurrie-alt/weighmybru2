# WMB+ 0.2.0-beta.1

First public WMB+ beta firmware for XIAO ESP32S3 WeighMyBru-compatible scale builds.

## Highlights

- BLE advertises as `WeighMyBru+`.
- Preserves stock-compatible WeighMyBru BLE service.
- Adds WMB+ capability discovery.
- Adds standard BLE Battery Service.
- Adds 80 SPS HX711 cadence support and diagnostics.
- Adds WMB+ extended telemetry.
- Keeps a pure 4-byte Float32 compatibility stream.
- Paces legacy Float32 at 20 Hz.
- Adds physical-parity BLE tare behavior.
- Adds atomic tare/start command.
- Adds firmware-side scale quality diagnostics.
- Adds bump/glitch diagnostics and one-frame glitch rejection.
- Adds near-zero stability cleanup.
- Adds USB-C serial weight capture.
- Defaults WiFi-off workflow for lower battery draw.
- Powers down HX711/display before ESP32 deep sleep where supported.

## Primary supported beta board

- Seeed Studio XIAO ESP32S3

## Release assets

- `wmb-plus-0.2.0-beta.1-xiao-app.bin`
- `wmb-plus-0.2.0-beta.1-xiao-factory-minimal.bin`
- `wmb-plus-0.2.0-beta.1-xiao-littlefs.bin`
- `wmb-plus-0.2.0-beta.1-sha256.txt`

## Known limitations

- Battery percentage is voltage-estimated.
- Charging and runtime estimates are experimental.
- XIAO ESP32S3 is the only beta-supported prebuilt target in this release.
- The minimal factory image does not include LittleFS web UI assets; flash the separate LittleFS image only if web UI assets should be updated.
- Calibration is per-device and must be verified by the builder.
- 80 SPS requires HX711 hardware support/configuration.

## Suggested beta tests

- Bean Conqueror compatibility.
- WeighMyBru/GaggiMate 20-byte compatibility.
- WMB+ aware app telemetry.
- USB serial capture.
- Physical and app-triggered tare.
- Real espresso shot cadence and quality.
- 10 minute idle drift.
- Sleep/wake.
- Battery/charging behavior.
