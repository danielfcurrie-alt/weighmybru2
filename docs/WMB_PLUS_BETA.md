# WMB+ beta firmware

WMB+ is an alternate beta firmware for WeighMyBru-compatible ESP32-S3 coffee scale builds.

The first public beta is intentionally a cumulative tester firmware. The goal is to validate the full capability set on real hardware before splitting the stable pieces into smaller upstream pull requests.

## Beta identity

- Firmware name: `WMB+`
- BLE name: `WeighMyBru+`
- Version: `0.2.0-beta.1`
- Primary tested board: Seeed Studio XIAO ESP32S3
- Primary tested HX711 mode: 80 SPS hardware configuration

## What WMB+ adds

- Stock-compatible WeighMyBru BLE service.
- 20-byte WeighMyBru/GaggiMate-compatible characteristic.
- 4-byte Float32 Bean Conqueror-compatible characteristic.
- Standard BLE Battery Service `180F / 2A19`.
- Optional WMB+ capabilities characteristic.
- Higher-rate extended telemetry when the app supports it.
- 80 SPS HX711 cadence diagnostics.
- Clean 20 Hz legacy Float32 compatibility stream.
- Physical-parity BLE tare path.
- Atomic tare/start command.
- Firmware-side scale quality diagnostics.
- Bump/glitch diagnostics.
- One-frame glitch rejection.
- Near-zero stability cleanup.
- USB-C serial weight capture.
- WiFi disabled-by-default workflow.
- Deeper pre-sleep peripheral shutdown.

## Compatibility policy

The compatibility lanes stay conservative:

- `6E400004` remains a pure 4-byte little-endian Float32 weight.
- Legacy clients do not need to understand WMB+ metadata.
- WMB+ metadata is exposed through the WMB+ extended packet, capabilities characteristic, USB serial stream, and diagnostics.

## Known limitations

- Battery percentage is voltage-estimated. It is useful but not a fuel-gauge-grade measurement.
- Charging/time-remaining estimates are experimental.
- XIAO ESP32S3 is the only board treated as beta-supported in this release.
- The web UI requires LittleFS to be flashed separately. The beta release includes a separate LittleFS image for testers who want to update web UI assets.
- Calibration remains per-device and must be checked by the builder.
- 80 SPS requires the HX711 hardware rate pin/jumper to be configured correctly.

## Release posture

Use this firmware for beta testing and data collection. Do not treat it as an official upstream WeighMyBru release.

Once the behavior is validated, stable pieces can be split into smaller upstream pull requests.
