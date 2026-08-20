# WMB+ beta firmware

WMB+ is an alternate beta firmware for WeighMyBru-compatible ESP32-S3 espresso scale builds.

It keeps the stock-compatible WeighMyBru Bluetooth paths available, then layers optional WMB+ capabilities on top. Existing apps can keep using the conservative compatibility streams; WMB+-aware tools can use richer telemetry.

## Why should I use this?

Use WMB+ if you want a WeighMyBru-compatible scale that actually uses 80 SPS HX711 hardware, adds richer scale diagnostics, and keeps existing app compatibility.

Main reasons to try it:

- **Real 80 SPS support:** this is the main reason to use WMB+. On the reference 80 SPS build, stock-compatible behavior was effectively around 8-9 SPS; WMB+ now delivers about 79-80 SPS through WMB+-aware paths instead of handicapping the hardware.
- **Grind/brew by weight with no machine mods:** the scale can directly call local Tasmota or Shelly HTTP webhooks from the StopMyBru screen, so a smart plug/relay can stop a grinder or brewer at target weight without modifying the machine. WMB+ can also learn the cutoff offset for that setup, so it compensates for beans/liquid that arrive after power is cut.
- **Better feature telemetry:** WMB+ adds an extended stream for timestamp, sequence, flow, battery, status flags, firmware scale quality, cadence, and diagnostics.
- **Bump/glitch diagnostics:** the firmware can identify likely bumps, one-frame ADC/load-cell glitches, cadence problems, and scale-quality changes instead of leaving every app to guess.
- **Compatibility stays on:** Bean Conqueror has been hardware-tested through the Float32 Bluetooth path. WeighMyBru/GaggiMate/Gaggiuino-style clients should continue to work through their existing 20-byte Bluetooth path.
- **Standard BLE battery:** exposes battery through the standard `180F / 2A19` Battery Service instead of hiding it in a web-only path.
- **Better battery backends:** ADC-backed boards expose voltage-estimated battery in stable 5% visible steps; TinyS3[D] development builds use the onboard MAX17048 fuel gauge for finer state-of-charge/voltage plus USB-power detection. Runtime and charge-current values are learned estimates unless future hardware exposes true current sensing.
- **Battery drain/charge diagnostics:** serial and web benchmark outputs track voltage slope, raw-percent slope, charge/drain trend, confidence, and active feature state so testers can compare WiFi/display/BLE settings across XIAO, SuperMini, and TinyS3[D].
- **TinyS3[D] hardware support:** development builds can use the board’s MAX17048 fuel gauge, USB power sense, software RF antenna switch, and optional configurable RGB status LED. The LED defaults off for battery testing.
- **Diagnostics without raw buffering:** the firmware keeps a compact ring of errors/events such as bump, glitch, low battery, sleep/wake, USB-power changes, and hardware faults. It uses PSRAM when available and does not store raw sample history in PSRAM.
- **Web OTA updates:** after one USB install with the dual-OTA partition table, future app firmware can be staged from GitHub or uploaded manually from the scale’s Updates page; web UI/LittleFS images can be uploaded manually.
- **Cleaner app tare behavior:** app-triggered tare goes through the same delayed/settled path as the physical tare button.
- **Atomic tare/start:** WMB+-aware apps can issue one command to tare and start a shot timer together.
- **Clean compatibility pacing:** legacy Float32 clients get a clean 20 Hz compatibility stream derived from the high-rate acquisition path.
- **USB-C serial capture:** ScaleBench or a terminal can record high-rate wired samples directly over USB CDC serial.
- **Firmware-side quality monitoring:** the scale tracks sample cadence, long gaps, bump/glitch diagnostics, current quality, and lifetime quality.
- **Webhook stop targets:** StopMyBru can drive local HTTP relays such as Tasmota and Shelly in addition to the ESP-NOW relay module.
- **Power controls:** WiFi can stay disabled for battery saving, critical battery can force deep sleep with recovery hysteresis, USB-present boards stay awake while plugged in, USB-only bench power is handled without false low-battery alarms, and sleep sends HX711 power-down plus display/board sleep prep.
- **Rollback-friendly beta:** the release provides app-only and factory images so testers can choose the least invasive flash path for their device.

This is beta firmware for testers. It is not an official upstream WeighMyBru release.

## Repository status

This repository is a GitHub fork of the public WeighMyBru firmware and is intended to validate changes before smaller upstream pull requests are prepared.

Current GitHub repository status:

- Public beta repository: [`danielfcurrie-alt/weighmybru2`](https://github.com/danielfcurrie-alt/weighmybru2)
- GitHub fork metadata: yes, forked from [`031devstudios/weighmybru2`](https://github.com/031devstudios/weighmybru2)
- Upstream goal: split proven pieces into focused pull requests later

## Current beta release

Latest beta:

- [WMB+ 0.2.0-beta.9 release](https://github.com/danielfcurrie-alt/weighmybru2/releases/tag/v0.2.0-beta.9)

Supported beta boards:

- **Primary tested reference:** Seeed Studio XIAO ESP32S3
- **Available beta build:** ESP32-S3 SuperMini / SuperMini-style board
- **Available development build:** Unexpected Maker TinyS3[D]. Do not flash XIAO, SuperMini, or TinyS3[D] assets onto the wrong board.

Release assets:

XIAO ESP32S3:

- `wmb-plus-0.2.0-beta.9-xiao-app.bin`
- `wmb-plus-0.2.0-beta.9-xiao-factory-full.bin`
- `wmb-plus-0.2.0-beta.9-xiao-factory-minimal.bin`
- `wmb-plus-0.2.0-beta.9-xiao-littlefs.bin`

ESP32-S3 SuperMini:

- `wmb-plus-0.2.0-beta.9-supermini-app.bin`
- `wmb-plus-0.2.0-beta.9-supermini-factory-full.bin`
- `wmb-plus-0.2.0-beta.9-supermini-factory-minimal.bin`
- `wmb-plus-0.2.0-beta.9-supermini-littlefs.bin`

Unexpected Maker TinyS3[D]:

- `wmb-plus-0.2.0-beta.9-tinys3d-app.bin`
- `wmb-plus-0.2.0-beta.9-tinys3d-factory-full.bin`
- `wmb-plus-0.2.0-beta.9-tinys3d-factory-minimal.bin`
- `wmb-plus-0.2.0-beta.9-tinys3d-littlefs.bin`

Shared:

- `wmb-plus-0.2.0-beta.9-sha256.txt`

For first-time beta installs or migration from `0.2.0-beta.1`, use the matching `factory-full.bin` for the exact board at `0x0`. It includes bootloader, dual-OTA partition table, app firmware, and LittleFS web UI.

After flashing, the dashboard is available at `http://192.168.4.1` in access-point mode. When mDNS works on the client device/network, `http://wmb.local` is the primary short URL and `http://wmbplus.local` is kept as a legacy alias.

For SuperMini installs, use the matching `supermini` asset names. Do not flash XIAO images onto a SuperMini or SuperMini images onto a XIAO. XIAO uses an 8MB factory image; SuperMini uses a 4MB factory image.

If you are not sure which board you have, stop before flashing. Identify the board visually first:

- **Seeed Studio XIAO ESP32S3:** use only `xiao` assets.
- **ESP32-S3 SuperMini / SuperMini-style board:** use only `supermini` assets.
- **Unexpected Maker TinyS3[D]:** use only `tinys3d` assets.

`esptool flash_id` can confirm that the chip is an ESP32-S3 and show flash size, but it cannot always identify the board model by itself. When in doubt, send a board photo or ask before choosing an image.

Calibration should be preserved. The matching factory-full image writes the bootloader, partition table, app firmware, and LittleFS web UI, but it does not run `erase_flash` and does not overwrite the ESP32 NVS area at `0x9000` where WeighMyBru stores calibration. After flashing, verify with a known weight, especially on a newly built or unusual partition-layout device. Calibration can be lost if you explicitly run `erase_flash`, use a reset-NVS/factory-reset endpoint, or come from a nonstandard layout.

Important: the originally published `0.2.0-beta.1` XIAO fallback assets used LittleFS at `0x310000` and appeared to use a legacy/single-app partition layout. Current beta9 assets use the corrected dual-OTA layouts.

## OTA status in this beta

WMB+ `0.2.0-beta.9` supports both manual browser-upload OTA and GitHub app self-update after the matching dual-OTA partition table is installed.

- **First install / partition migration:** flash the matching `factory-full.bin` once over USB at `0x0`. This installs the bootloader, dual-OTA partition table, app firmware, and LittleFS web UI.
- **After factory-full is installed:** the Updates page should show `Firmware OTA: Ready` and a `Next OTA partition` such as `app1`.
- **Future app updates:** use the Updates page to check GitHub while the scale is on home WiFi, stage the matching `-app.bin` into the inactive OTA slot, then tap Install when the page reports a pending update. Manual `-app.bin` upload remains available as a fallback.
- **Future web UI updates:** download the matching `-littlefs.bin` release asset yourself, then upload it from the scale's Updates page. GitHub self-update currently stages app firmware, not LittleFS.

Manual browser-upload OTA still updates over WiFi/local network instead of USB serial and remains the recovery path if GitHub self-update is unavailable.

## What WMB+ adds

- BLE advertises as `WeighMyBru+`.
- Stock-compatible WeighMyBru BLE service remains present.
- 20-byte WeighMyBru/GaggiMate-compatible weight characteristic remains present.
- 4-byte Float32 Bean Conqueror-compatible characteristic remains pure Float32 weight.
- Real 80 SPS acquisition and WMB+ telemetry on 80 SPS HX711 hardware.
- Clean 20 Hz legacy Float32 stream derived from the high-rate acquisition path.
- Standard BLE Battery Service `180F / 2A19`.
- ADC-backed battery estimate on XIAO/SuperMini builds.
- MAX17048 fuel-gauge battery backend and USB-power detection on TinyS3[D] development builds. MAX17048 provides voltage and state-of-charge, not true current measurement; WMB+ learns charge/drain estimates from observed percentage movement.
- Learned battery runtime/charge estimates persisted across reboots where the active battery backend supports enough observation data.
- USB-aware power behavior on boards with VBUS sense: critical battery sleep and auto-sleep are inhibited while plugged in, and USB-only/no-battery bench power is reported as a valid external-power state instead of a critical battery fault.
- Resettable battery benchmark session exposed through serial `BATTERY_BENCH` rows, `/api/battery/benchmark`, and `/api/battery/benchmark/reset`.
- Diagnostic event log for exceptions only: bump, glitch, low/critical battery, invalid battery, USB power changes, sleep/wake, missing HX711, missing display, missing TinyS3[D] fuel gauge, BLE/WiFi faults. XIAO and TinyS3[D] builds target a 512-event PSRAM-backed log when PSRAM is available, with a small heap fallback.
- TinyS3[D] RF antenna switch setting, defaulting to onboard/internal antenna.
- Optional TinyS3[D] RGB status LED, defaulting off to avoid corrupting battery tests, with web-configurable enable and low brightness.
- WMB+ capabilities characteristic.
- WMB+ extended telemetry packet.
- Fresh-sample notification cadence and HX711 cadence diagnostics for capable clients.
- Physical-parity BLE tare behavior.
- Atomic tare/start command.
- Firmware-side scale quality and lifetime quality diagnostics.
- Bump/glitch diagnostics.
- One-frame glitch rejection.
- Near-zero stability cleanup.
- USB-C serial weight capture.
- StopMyBru HTTP webhook relay support for local Tasmota/Shelly-style devices.
- StopMyBru target stop learning to compensate for grinder/brewer overshoot after relay cutoff.
- Web OTA upload for app firmware and LittleFS web UI images.
- WiFi-disabled workflow for lower battery draw.
- Critical-battery deep sleep guard. Defaults are enabled, `3.45V` for ADC-backed boards and `7%` state of charge for fuel-gauge boards. After a critical-battery sleep, full boot requires a recovery margin of about `+0.10V` or `+3%` SOC unless USB power is present.
- HX711 power-down before ESP32 deep sleep. This holds HX711 `PD_SCK` high for the low-power state, then turns off display/board peripherals where supported.

The Bluetooth extension packet is documented in [WMB+ BLE protocol](docs/WMB_PLUS_PROTOCOL.md). The USB-C text stream is documented in [USB serial protocol](docs/USB_SERIAL.md).

## Validated so far

Hardware-tested on the XIAO ESP32S3 reference build:

- BLE advertises as `WeighMyBru+`.
- Bean Conqueror reads live weight through the pure Float32 compatibility path.
- The Float32 compatibility path can run as a clean app-friendly stream while the WMB+ path keeps the higher-rate diagnostic stream available.
- WMB+-aware Bluetooth telemetry reports real high-rate operation on the reference 80 SPS HX711 build, around 79-80 Hz in ScaleBench captures.
- USB-C serial weight capture works at approximately 80 Hz with zero reported USB drops in initial testing.
- USB-C serial capture has also been confirmed from Android host-side tooling.
- Standard BLE Battery Service data is visible to WMB+-aware tooling.
- Battery/USB state reporting includes charging detection on the tested reference unit.
- WiFi can remain disabled for battery-focused use.
- Local web UI works in access-point mode and can also use saved local WiFi credentials.
- Manual browser-upload OTA works for app firmware.
- GitHub app self-update check/download/pending-install is implemented for WiFi-connected scales.
- Manual browser-upload OTA works for LittleFS web UI updates.
- The browser-side GitHub release checker works when the browser has internet access.
- StopMyBru HTTP webhook configuration and manual Test ON/Test OFF paths are implemented and can be smoke-tested with any local HTTP receiver.
- Physical tare and sleep controls work on the reference build.
- Firmware quality diagnostics report high quality on clean captures.

Build/release validation currently covers:

- XIAO ESP32S3 app and LittleFS builds.
- ESP32-S3 SuperMini app and LittleFS builds.
- TinyS3[D] app and LittleFS development builds.
- Release manifest checks for board-specific flash size, LittleFS offset, and non-erase install policy.
- Host-side simulation tests for battery/runtime/power-mode estimation logic.
- A release-blocking OTA smoke-test checklist for future beta releases.

## Testing and compatible apps

For firmware and transport-quality testing, use **ScaleBench**:

- ScaleBench GitHub: <https://github.com/danielfcurrie-alt/ScaleBench>

ScaleBench can record supported Bluetooth scale streams, calculate a comparable scale-quality score, inspect packet cadence/gaps/rejections, and export JSON recordings for debugging. This is the preferred tool when reporting WMB+ beta firmware behavior because it captures both compatibility-path data and WMB+ telemetry when available.

Existing compatibility apps:

- Bean Conqueror has been hardware-tested and reads the pure 4-byte Float32 stream.
- GaggiMate / Gaggiuino-style WeighMyBru clients should continue to read the 20-byte WeighMyBru-compatible stream.

If you see a regression in Bean Conqueror, GaggiMate, or Gaggiuino compared with stock WeighMyBru behavior, please report it as a compatibility bug.

## Start here

- [WMB+ beta overview](docs/WMB_PLUS_BETA.md)
- [Flashing instructions](docs/FLASHING.md)
- [Rollback instructions](docs/ROLLBACK.md)
- [Compatibility checklist](docs/COMPATIBILITY.md)
- [Tester checklist](docs/TESTING.md)
- [Release checklist](docs/RELEASE_CHECKLIST.md)
- [USB serial protocol](docs/USB_SERIAL.md)
- [WMB+ BLE protocol](docs/WMB_PLUS_PROTOCOL.md)
- [Release notes](docs/RELEASE_NOTES_0.2.0-beta.9.md)

## Quick flash commands

Download the matching release assets first, verify the SHA-256 file, and replace `/dev/cu.usbmodemXXXX` with your actual serial port. Do not run `erase_flash` for normal beta installs.

### XIAO ESP32S3

Use these for the primary 8MB XIAO build. XIAO LittleFS is at `0x610000`.

Recommended fresh install or migration from `0.2.0-beta.1`:

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX --baud 460800 write_flash 0x0 wmb-plus-0.2.0-beta.9-xiao-factory-full.bin
```

App-only upgrade after the `0.2.0-beta.9` dual-OTA layout is already installed:

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX --baud 460800 write_flash 0x10000 wmb-plus-0.2.0-beta.9-xiao-app.bin
```

Web UI / LittleFS-only update:

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX --baud 460800 write_flash 0x610000 wmb-plus-0.2.0-beta.9-xiao-littlefs.bin
```

### ESP32-S3 SuperMini

Use these for the 4MB ESP32-S3 SuperMini / SuperMini-style build. SuperMini LittleFS is at `0x350000`.

Recommended fresh install:

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX --baud 460800 write_flash 0x0 wmb-plus-0.2.0-beta.9-supermini-factory-full.bin
```

App-only upgrade after the `0.2.0-beta.9` dual-OTA layout is already installed:

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX --baud 460800 write_flash 0x10000 wmb-plus-0.2.0-beta.9-supermini-app.bin
```

Web UI / LittleFS-only update:

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX --baud 460800 write_flash 0x350000 wmb-plus-0.2.0-beta.9-supermini-littlefs.bin
```

Minimal factory images are also published for advanced cases where you intentionally want to install firmware without updating the web UI filesystem. Most testers should use `factory-full`.

XIAO `0.2.0-beta.9` uses two 3MB OTA app slots and places LittleFS at `0x610000`. SuperMini dual-OTA uses two 1.5MB OTA app slots and places LittleFS at `0x350000`. Do not cross-flash board assets. If your installed image reports a legacy/single-app layout, do not rely on app OTA until you install the matching `0.2.0-beta.9` full factory image.

### Optional Codex install prompt

If you want Codex or another coding agent to install from release assets with guardrails, use this prompt:

```text
Install WMB+ v0.2.0-beta.9 from GitHub release assets for my board.

Board: <XIAO ESP32S3, ESP32-S3 SuperMini, or TinyS3[D]>.

If the board is not explicitly identified, stop and ask me for a board photo or board name before choosing an asset.

Release repo: https://github.com/danielfcurrie-alt/weighmybru2
Release tag: v0.2.0-beta.9

Download the matching factory-full asset and wmb-plus-0.2.0-beta.9-sha256.txt. Verify SHA-256 and image size before flashing:
- XIAO factory-full must be 8,388,608 bytes.
- SuperMini factory-full must be 4,194,304 bytes.
- TinyS3[D] factory-full must be 8,388,608 bytes.

Use factory-full for first install or partition migration. This creates the dual-OTA partition layout required for later app self-update and browser-upload OTA.
Do not use the app-only asset for first install from stock/unknown partition layouts.

Detect the ESP32-S3 serial port and flash size with esptool flash_id. Use flash_id as a safety check, not as the only board identifier. If detected flash size conflicts with the selected factory-full size, stop.

Safety rules:
- Do not run erase_flash.
- Do not build from source.
- Do not use assets for the other board.
- Do not guess XIAO vs SuperMini from file names or defaults.
- Stop and ask before write_flash.

After I confirm, flash the matching factory-full image at 0x0. Then open serial at 115200 and report firmware version, BLE name, LittleFS/web status, running partition, next OTA partition, Firmware OTA readiness, HX711 rate, battery, and calibration factor.

Clarify OTA status in your report:
- This beta supports GitHub app self-update from the local Updates page when the scale is connected to home WiFi.
- Manual browser-upload OTA remains available for matching -app.bin and -littlefs.bin assets.
- GitHub self-update currently stages app firmware, not LittleFS.
```

Verify over serial at `115200` by sending:

```text
z
```

Expected indicators:

- `WMB+ v0.2.0-beta.9`
- `Board: XIAO ESP32S3`
- BLE name `WeighMyBru+`
- `legacyFloat32Cadence=20Hz`
- USB commands `w` and `W`

## USB serial capture

Open the USB serial port at `115200` baud.

Useful commands:

```text
b  toggle battery benchmark logging
B  print one battery benchmark row
d  reset battery drain/charge benchmark session
e  print diagnostic event log
E  clear diagnostic event log
W  print one weight sample
w  start/stop continuous weight stream
z  diagnostics
```

Sample row:

```text
WMBP_WEIGHT_V1,123456,9821,18.423,1.731,0x0041,98,75,79.82,0
```

See [USB serial protocol](docs/USB_SERIAL.md) for field definitions.

Battery benchmark data is also available at `/api/battery/benchmark`. Reset the comparison baseline with `POST /api/battery/benchmark/reset`; optional form/query parameter `label` can identify the active test, e.g. `wifi-off`, `wifi-ap`, `oled-on`, `ble-connected`, or `charging`.

The firmware defaults to a 700 mAh battery capacity setting. If your build uses a different pack, set the capacity on the Settings page or with:

```bash
curl -X POST http://wmb.local/api/battery/capacity \
  -H 'Content-Type: application/x-www-form-urlencoded' \
  --data 'capacityMah=1000'
```

Capacity does not change the voltage-to-percent curve. It lets WMB+ convert learned percent/hour into estimated mA and more realistic runtime/charge-time estimates.

Critical battery sleep is configurable from the Settings page or with:

```bash
curl -X POST http://wmb.local/api/battery/settings \
  -H 'Content-Type: application/x-www-form-urlencoded' \
  --data 'capacityMah=700&criticalShutdownEnabled=true&criticalShutdownVoltage=3.45&criticalShutdownPercent=7'
```

## Development checks

Host-side tests for firmware math:

```bash
tools/run-host-tests.sh
```

Runtime cadence smoke test for a flashed XIAO reference unit:

```bash
python3 tools/runtime-cadence-smoke.py \
  --port /dev/cu.usbmodem1101 \
  --base-url http://192.168.4.1 \
  --profile baseline \
  --profile dashboard-safe
```

This keeps the USB `WMBP_WEIGHT_V1` stream running while polling the same web endpoints as the dashboard. It should pass with about 79-80 Hz device cadence, no sequence loss, no USB drops, and no web polling errors. Use it before public beta releases to catch dashboard or API changes that stall 80 SPS acquisition.

Firmware simulation builds:

```bash
pio run -e esp32s3-xiao-sim-80sps
pio run -e esp32s3-xiao-sim-10sps
pio run -e esp32s3-xiao-sim-glitch
pio run -e esp32s3-xiao-sim-bump
pio run -e esp32s3-xiao-sim-battery-wifi-ap
pio run -e esp32s3-xiao-sim-battery-charging
```

Simulation builds are for parser/cadence/UI development only. They bypass HX711 hardware and battery hardware, then emit deterministic synthetic streams through the normal firmware paths. Use the 10/80 SPS profiles for cadence testing, the glitch/bump profiles for diagnostic flag testing, and the battery profiles for drain/charge trend testing. Do not publish or flash simulation builds as tester firmware unless the goal is explicitly simulation.

Estimated battery/runtime matrix:

```bash
tools/simulate-battery-matrix.py --capacity-mah 700
```

For a 1000 mAh pack:

```bash
tools/simulate-battery-matrix.py --capacity-mah 1000
```

This compares XIAO ESP32S3, TinyS3[D], ESP32-S3 SuperMini, and Waveshare ESP32-S3-Zero-style boards across WiFi off/on, 10/80 SPS, and sleep/HX711-power states. It also prints a sleep-charging estimate for XIAO vs TinyS3[D] with HX711 powered and HX711 power-off variants. It is an explicit assumption model for planning and should be replaced with measured drain-test constants as tester data arrives.

## Apple Silicon LittleFS builds

The project pins the newer ESP32-oriented PlatformIO LittleFS tool:

```ini
platform_packages =
  tasmota/tool-mklittlefs@^4.0.0
```

On Apple Silicon, PlatformIO may still install an x86_64 `mklittlefs` binary for that package. If `pio run -e esp32s3-xiao -t buildfs` fails with `Bad CPU type in executable`, run:

```bash
tools/install-macos-arm64-mklittlefs.sh
pio run -e esp32s3-xiao -t buildfs
```

The helper builds `mklittlefs` 4.0.0 locally as an arm64 binary and backs up the previous PlatformIO executable.

For TinyS3[D] development builds, use:

```bash
pio run -e esp32s3-tinys3d
pio run -e esp32s3-tinys3d -t buildfs
```

## Compatibility policy

Compatibility paths stay conservative:

- `6E400004` remains exactly a 4-byte little-endian Float32 weight.
- Existing apps should not need WMB+ metadata to read weight.
- Optional metadata is exposed through WMB+ capabilities, the extended packet, USB serial, and diagnostics.
- PSRAM is reserved for compact diagnostic/error events, not raw sample buffering. On XIAO and TinyS3[D], `/api/diagnostics/events` reports `backend`, `capacity`, `allocated_bytes`, `psram_size`, and `free_psram` so testers can verify whether the event log is actually PSRAM-backed.

## Known limitations

- Battery percentage is voltage-estimated on XIAO/SuperMini builds. TinyS3[D] development builds use the MAX17048 fuel gauge when present.
- Charging/runtime estimates are experimental; WMB+ learns charge/discharge rates over time, but this is still voltage-based intelligence, not a dedicated fuel gauge.
- XIAO ESP32S3 is the primary hardware-tested beta target. ESP32-S3 SuperMini and TinyS3[D] builds are available for beta/development testing, but XIAO remains the reference path.
- App firmware OTA requires the WMB+ dual-OTA partition table. Existing devices on a legacy/single-app layout need the current matching `factory-full.bin` flashed once over USB before relying on app OTA.
- Calibration is still per-device and must be verified by the builder.
- 80 SPS requires the HX711 hardware rate pin/jumper to be configured correctly.
- Tare and sleep inputs expect active-high digital touch sensor modules. The firmware enables `INPUT_PULLDOWN` on those pins; bare capacitive pads or open-drain sensors need appropriate external conditioning.

## Original WeighMyBru README

The original upstream README content is preserved below for hardware/project background.

<p align="center">
<img src="docs/assets/Weighmybru-logo.png" alt="WeighMyBru Dashboard" width="500" height="745"/>
</p>

<p align="center">  <b>The smart coffee scale that doesn't break the bank!<br>https://weighmybru.com</p>
<br>



[![](https://dcbadge.limes.pink/api/server/HYp4TSEjSf)](https://discord.gg/HYp4TSEjSf)
[![License](https://img.shields.io/badge/License-CC%20BY--NC--SA%204.0-lightgrey.svg?style=for-the-badge)](LICENSE)

This project is a smart coffee scale with a webserver hosted on the ESP32-S3.
This scale was designed to be used in conjunction with GaggiMate, but can also be used as a standalone scale. The project is still in a beta phase and is actively being worked on. Please head over to the [Discord](https://discord.gg/HYp4TSEjSf) server for more info. This scale was inspired by [EspressiScale](https://www.espressiscale.com) but with a non-custom PCB approach. The idea is to be low-cost, easily sourcable scale.

<br>
<br>
<p>
<img src="docs/assets/dashboard.jpg" alt="WeighMyBru Dashboard" width="250" />
<img src="docs/assets/weighmybru.jpg" alt="WeighMyBru Dashboard" width="700" />
</p>

## Documentation

The documentation, build video guide, flashing software etc. can be found on the website. 

[WeighMyBru Guides](https://weighmybru.com/guides/)

## Features

- Webserver via WiFi
- Bluetooth connectivity to GaggiMate
- Calibration via webserver
- Real-time flowrate display
- Adjustable decimal point readings
- Different modes for to cater for espresso and pour-overs


## GaggiMate

GaggiMate now fully supports WeighMyBru scale.

[GaggiMate](https://github.com/jniebuhr/gaggimate)

## Installation

For WMB+ beta installs, start with the [flashing instructions](docs/FLASHING.md) and the current [GitHub release](https://github.com/danielfcurrie-alt/weighmybru2/releases/tag/v0.2.0-beta.9). Use the XIAO `factory-full` image for first install or migration from `0.2.0-beta.1`.

```
  this project requires VSCode with PlatformIO extension installed
```

### Important: Filesystem Upload Required

After uploading the firmware, you **must also upload the filesystem** for the web interface to work:

```bash
# Upload filesystem (required for web interface)
pio run -t uploadfs

# Or use the specific environment for your board
pio run -e esp32s3-supermini -t uploadfs  # For ESP32-S3 Supermini
pio run -e esp32s3-xiao -t uploadfs       # For XIAO ESP32S3
```

**Without the filesystem upload:**
- The device will function normally for scale operations
- The web interface will be unavailable
- You'll see a clear message explaining how to fix this issue

### 🌐 Web-Based Installation (Recommended)

For beginners, we now support **ESP32 Web Tools** for easy browser-based installation:

1. **Visit [weighmybru.com](https://weighmybru.com)** 
2. **Connect your ESP32 board** via USB
3. **Click "Install Firmware"** and select your board:
   - ESP32-S3 Supermini
   - XIAO ESP32S3
4. **Follow the prompts** - no software installation required!

**Benefits:**
- ✅ No need to install VS Code or PlatformIO
- ✅ Automatic latest firmware version
- ✅ Complete installation (firmware + filesystem)
- ✅ Works on any modern browser (Chrome, Edge, Opera)
- ✅ Version checking and device information

For advanced users or development, continue with the PlatformIO instructions below.

## Bill Of Materials (BOM)

| Qty |           Item                      | Amazon Link | Aliexpress Link |
| --- | ----------------------------------- | -----------------------| --------------- |
|  1  | 500g Mini Loadcell (I-shaped)       | https://a.co/d/6kvxZ0H | https://www.aliexpress.us/item/3256810229632696.html |
|  1  | HX711                               | https://a.co/d/3KiYRkA | https://www.aliexpress.us/item/3256806665065792.html |
|  1  | ESP32-S3-Supermini Board            | https://a.co/d/6289vbS | https://www.aliexpress.us/item/3256806649605779.html |
|  2  | Capacitive Touch Pads               | https://a.co/d/014di6l | https://www.aliexpress.us/item/3256806118244119.html |
|  1  | 0.91" SSD1306 OLED Display          | https://a.co/d/9UClWku | https://www.aliexpress.us/item/3256807486098308.html |
|  1  | 800mAh Li-ion Battery               | https://a.co/d/gbr1Yft | https://www.aliexpress.us/item/3256809665395688.html |
|  1  | JST-PH 2.0 Male Connector           | https://a.co/d/3BZGuHW | https://www.aliexpress.us/item/3256808498055527.html |
|  1  | 5mm Slide Switch                    | https://a.co/d/9KRqMyF | https://www.aliexpress.us/item/3256808144255016.html |
|  1  | Hookup Wire (Various Colors)        | https://a.co/d/1Fs8os9 |  |
|  2  | M3x5x4 Heat Set Inserts             | https://a.co/d/bnQD7Iu |  |
|  16 | M1.7x4 Self Tapping Screws          | https://a.co/d/1np5Nes | https://www.aliexpress.us/item/2255800110173778.html |
|  4  | M3x12 Button Head Screws            | https://a.co/d/iqM3d6E | https://www.aliexpress.us/item/3256807424674896.html |
|  2  | 100K ohm 1% 1/4w resistors          | https://a.co/d/3R0YmGM |  |
|  4  | Self-Adhesive Rubber Feet           | https://a.co/d/0q9TRmR |  |
|  1  | Double Sided Tape (To hold Battery) | https://a.co/d/gM5SWwH |  |



## Printed Parts (Found in CAD Folder)

| Qty |           Item                    | 
| --- | ----------------------------------|  
|  1  | Bottom*                           |  
|  1  | Top                               |
|  1  | ESP32 Clamp                       |
|  1  | Screen Clamp                      |
|  4  | M2 Washers (Used for HX711)       |

\* Bottom has 2 options - The supported option has engineered supports, while the standard option requires you to slice with your own supports.
