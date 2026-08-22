# WMB+ beta firmware

WMB+ is an alternate beta firmware for WeighMyBru-compatible ESP32-S3 coffee scale builds.

The first public beta is intentionally a cumulative tester firmware. The goal is to validate the full capability set on real hardware before splitting the stable pieces into smaller upstream pull requests.

The headline improvement is real 80 SPS operation. On the reference 80 SPS HX711 build, stock-compatible behavior was effectively around 8-9 SPS. WMB+ exposes about 79-80 SPS through WMB+-aware BLE and USB paths instead of leaving the hardware handicapped.

## Beta identity

- Firmware name: `WMB+`
- BLE name: `WeighMyBru+`
- Current beta version: `0.2.0-beta.10`
- Latest published release: `0.2.0-beta.10`
- Primary tested board: Seeed Studio XIAO ESP32S3
- Primary tested HX711 mode: 80 SPS hardware configuration

## What WMB+ adds

- Stock-compatible WeighMyBru BLE service.
- 20-byte WeighMyBru/GaggiMate-compatible characteristic.
- 4-byte Float32 Bean Conqueror-compatible characteristic.
- Real 80 SPS acquisition and WMB+ telemetry on 80 SPS HX711 hardware.
- Clean 20 Hz legacy Float32 compatibility stream derived from the high-rate acquisition path.
- Extended timestamp, sequence, flow, battery, status, quality, cadence, and diagnostic fields.
- Standard BLE Battery Service `180F / 2A19`.
- Learned voltage-based battery runtime and charge estimates persisted across reboots.
- MAX17048 fuel-gauge battery backend and USB-power detection on TinyS3[D] development builds.
- Diagnostic event log for exceptions only, not raw sample buffering. The log uses PSRAM when available.
- TinyS3[D] RF antenna switch support.
- Optional TinyS3[D] RGB status LED, defaulting off for battery testing.
- Optional WMB+ capabilities characteristic.
- 80 SPS HX711 cadence diagnostics.
- Physical-parity BLE tare path.
- Atomic tare/start command.
- Firmware-side scale quality diagnostics.
- Bump/glitch diagnostics.
- One-frame glitch rejection.
- Near-zero stability cleanup.
- USB-C serial weight capture.
- StopMyBru HTTP webhook relay support for local Tasmota/Shelly-style devices.
- StopMyBru target stop learning for grinder/brewer overshoot compensation.
- Dedicated Pour Over PWA section with configurable recipes, typed pour/pause/agitation/drawdown stages, target-driven guidance, live weight/flow trace, previous-brew reference, peak flow, and a press-and-hold agitation guard.
- Firmware-authoritative Pour Over stage/session clock synchronized through cached dashboard/SSE state, with browser-local fallback for older firmware and a staged TinyS3[D] color-screen view.
- Web update UI support. App self-update requires the WMB+ dual-OTA layout; manual browser upload of matching release assets remains available.
- WiFi disabled-by-default workflow.
- Deeper pre-sleep peripheral shutdown.

## Compatibility policy

The compatibility lanes stay conservative:

- `6E400004` remains a pure 4-byte little-endian Float32 weight.
- Legacy clients do not need to understand WMB+ metadata.
- WMB+ metadata is exposed through the WMB+ extended packet, capabilities characteristic, USB serial stream, and diagnostics.

## Known limitations

- Battery percentage is voltage-estimated on XIAO/SuperMini. TinyS3[D] development builds use the MAX17048 fuel gauge when present.
- Charging/time-remaining estimates are experimental. WMB+ learns observed charge/discharge rates over time; TinyS3[D] fuel-gauge data should improve this once validated on real hardware.
- XIAO ESP32S3 is the primary hardware-tested beta reference. SuperMini and TinyS3[D] assets are available for beta/development testing.
- The originally published `0.2.0-beta.1` XIAO fallback asset used LittleFS at `0x310000` and appeared to use a legacy/single-app layout.
- `0.2.0-beta.10` XIAO factory-full uses the dual-OTA layout with LittleFS at `0x610000`.
- App firmware OTA requires the WMB+ dual-OTA partition table. Devices flashed with beta.1 or another legacy/single-app layout need a `0.2.0-beta.10` full factory flash before app OTA is available.
- The Updates page can check GitHub releases from the scale when it is on WiFi, download a matching app firmware image into the inactive OTA slot, and show it as pending install until you tap Install. Manual browser upload of matching `-app.bin` or `-littlefs.bin` assets remains available as a fallback.
- HTTP webhook relay support requires WiFi to be connected to the same local network as the relay. Local `http://` URLs are supported; HTTPS is intentionally not part of the beta path.
- StopMyBru target stop learning is based on settled scale weight after automatic target cutoff. It intentionally ignores large errors and manual OFF actions.
- Calibration remains per-device and must be checked by the builder.
- 80 SPS requires the HX711 hardware rate pin/jumper to be configured correctly.
- Saved recipe libraries and completed brew traces remain browser-local. Preparing a recipe uploads the active recipe to volatile firmware RAM so the scale owns the live stage/session clock and multiple connected pages see the same run; the recipe must be prepared again after a reboot.
- The TinyS3[D] Pour Over color-screen renderer is complete, but physical display initialization remains disabled until the builder's final LCD/touch pin table is verified.

## Release posture

Use this firmware for beta testing and data collection. Do not treat it as an official upstream WeighMyBru release.

Once the behavior is validated, stable pieces can be split into smaller upstream pull requests.
