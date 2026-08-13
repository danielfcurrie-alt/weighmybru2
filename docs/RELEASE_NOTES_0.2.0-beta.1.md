# WMB+ 0.2.0-beta.1

First public WMB+ beta firmware for XIAO ESP32S3 WeighMyBru-compatible scale builds.

The main point of this beta is real 80 SPS operation. On the reference 80 SPS HX711 build, stock-compatible behavior was effectively around 8-9 SPS; WMB+ exposes about 79-80 SPS through WMB+-aware BLE and USB paths.

## Highlights

- BLE advertises as `WeighMyBru+`.
- Preserves stock-compatible WeighMyBru BLE service.
- Adds WMB+ capability discovery.
- Adds real 80 SPS WMB+ BLE/USB telemetry on 80 SPS HX711 hardware.
- Adds standard BLE Battery Service.
- Adds 80 SPS HX711 cadence support and diagnostics.
- Adds WMB+ extended telemetry: timestamp, sequence, flow, battery, status, quality, cadence, and diagnostics.
- Keeps a pure 4-byte Float32 compatibility stream.
- Paces legacy Float32 at a clean 20 Hz derived from the high-rate acquisition path.
- Adds physical-parity BLE tare behavior.
- Adds atomic tare/start command.
- Adds firmware-side scale quality diagnostics.
- Adds bump/glitch diagnostics and one-frame glitch rejection.
- Adds near-zero stability cleanup.
- Adds USB-C serial weight capture.
- Adds StopMyBru HTTP webhook relay support for local Tasmota/Shelly-style devices.
- Adds StopMyBru target stop learning for grinder/brewer overshoot compensation.
- Adds web update UI support in firmware.
- Note: the originally published XIAO fallback factory-minimal asset appears to use a legacy/single-app partition layout, not the later dual-OTA layout now present in the source tree.
- Defaults WiFi-off workflow for lower battery draw.
- Powers down HX711/display before ESP32 deep sleep where supported.

## Primary supported beta board

- Seeed Studio XIAO ESP32S3

## Release assets

- `wmb-plus-0.2.0-beta.1-xiao-app.bin`
- `wmb-plus-0.2.0-beta.1-xiao-factory-minimal.bin`
- `wmb-plus-0.2.0-beta.1-xiao-littlefs.bin`
- `wmb-plus-0.2.0-beta.1-sha256.txt`

Some rebuilt release sets may also include `wmb-plus-0.2.0-beta.1-xiao-factory-full.bin`. If not, `factory-minimal` plus `littlefs` can be flashed together to install the release app and web UI. For the originally published XIAO fallback asset, LittleFS is at `0x310000`.

## Known limitations

- Battery percentage is voltage-estimated.
- Charging and runtime estimates are experimental.
- StopMyBru target stop learning is experimental and should be validated with a safe smart plug/relay setup before unattended use.
- XIAO ESP32S3 is the only beta-supported prebuilt target in this release.
- The originally published XIAO fallback `factory-minimal` image intentionally excludes LittleFS and appears to use a legacy/single-app layout.
- XIAO LittleFS for the originally published fallback asset is at `0x310000`.
- XIAO LittleFS for later rebuilt dual-OTA assets is at `0x610000`. SuperMini dual-OTA LittleFS is at `0x310000`.
- Existing devices on a legacy/single-app partition table need a rebuilt dual-OTA factory flash before app OTA is available.
- Calibration is per-device and must be verified by the builder.
- 80 SPS requires HX711 hardware support/configuration.

## Suggested beta tests

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
