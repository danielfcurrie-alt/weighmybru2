# WMB+ beta firmware

WMB+ is an alternate beta firmware for WeighMyBru-compatible ESP32-S3 espresso scale builds.

It keeps the stock-compatible WeighMyBru Bluetooth paths available, then layers optional WMB+ capabilities on top: standard BLE battery, higher-rate telemetry, 80 SPS diagnostics, USB-C serial capture, app/physical tare parity, atomic tare/start, and firmware-side scale-quality diagnostics.

This is beta firmware for testers. It is not an official upstream WeighMyBru release.

## Repository status

This repository is a GitHub fork of the public WeighMyBru firmware and is intended to validate changes before smaller upstream pull requests are prepared.

Current GitHub repository status:

- Public beta repository: [`danielfcurrie-alt/weighmybru2`](https://github.com/danielfcurrie-alt/weighmybru2)
- GitHub fork metadata: yes, forked from [`031devstudios/weighmybru2`](https://github.com/031devstudios/weighmybru2)
- Upstream goal: split proven pieces into focused pull requests later

An earlier standalone beta repository existed at `danielfcurrie-alt/wmb-plus-firmware`. The active beta location is this fork.

## Current beta release

Latest beta:

- [WMB+ 0.2.0-beta.1 release](https://github.com/danielfcurrie-alt/weighmybru2/releases/tag/v0.2.0-beta.1)

Primary supported beta board:

- Seeed Studio XIAO ESP32S3

Release assets:

- `wmb-plus-0.2.0-beta.1-xiao-app.bin`
- `wmb-plus-0.2.0-beta.1-xiao-factory-minimal.bin`
- `wmb-plus-0.2.0-beta.1-xiao-littlefs.bin`
- `wmb-plus-0.2.0-beta.1-sha256.txt`

The LittleFS web UI image is packaged separately. The scale, BLE, USB serial, display, battery, tare, and sleep features are firmware features and do not require a web UI filesystem update.

## What WMB+ adds

- BLE advertises as `WeighMyBru+`.
- Stock-compatible WeighMyBru BLE service remains present.
- 20-byte WeighMyBru/GaggiMate-compatible weight characteristic remains present.
- 4-byte Float32 Bean Conqueror-compatible characteristic remains pure Float32 weight.
- Standard BLE Battery Service `180F / 2A19`.
- WMB+ capabilities characteristic.
- WMB+ extended telemetry packet.
- Fresh-sample notification cadence for capable clients.
- Legacy Float32 stream paced at 20 Hz.
- 80 SPS HX711 cadence diagnostics.
- Physical-parity BLE tare behavior.
- Atomic tare/start command.
- Firmware-side scale quality and lifetime quality diagnostics.
- Bump/glitch diagnostics.
- One-frame glitch rejection.
- Near-zero stability cleanup.
- USB-C serial weight capture.
- WiFi-disabled workflow for lower battery draw.
- HX711/display shutdown before ESP32 deep sleep where supported.

## Validated so far

Early hardware validation on a XIAO ESP32S3 reference build:

- BLE advertises as `WeighMyBru+`.
- Bean Conqueror reads weight through the Float32 compatibility path.
- USB serial stream works at approximately 80 Hz with zero reported USB drops in initial testing.
- Standard battery field is visible to WMB+ aware tooling.
- WiFi can remain disabled.
- Firmware quality diagnostics report high quality on clean captures.

More tester reports are needed before upstream pull requests are split out.

## Start here

- [WMB+ beta overview](docs/WMB_PLUS_BETA.md)
- [Flashing instructions](docs/FLASHING.md)
- [Rollback instructions](docs/ROLLBACK.md)
- [Compatibility checklist](docs/COMPATIBILITY.md)
- [Tester checklist](docs/TESTING.md)
- [USB serial protocol](docs/USB_SERIAL.md)
- [WMB+ BLE protocol](docs/WMB_PLUS_PROTOCOL.md)
- [Release notes](docs/RELEASE_NOTES_0.2.0-beta.1.md)

## Quick flash commands

App-only upgrade for an existing compatible XIAO ESP32S3 install:

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX --baud 460800 write_flash 0x10000 wmb-plus-0.2.0-beta.1-xiao-app.bin
```

Minimal fresh firmware install for XIAO ESP32S3:

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX --baud 460800 write_flash 0x0 wmb-plus-0.2.0-beta.1-xiao-factory-minimal.bin
```

Optional web UI filesystem image:

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX --baud 460800 write_flash 0x310000 wmb-plus-0.2.0-beta.1-xiao-littlefs.bin
```

Verify over serial at `115200` by sending:

```text
z
```

Expected indicators:

- `WMB+ v0.2.0-beta.1`
- `Board: XIAO ESP32S3`
- BLE name `WeighMyBru+`
- `legacyFloat32Cadence=20Hz`
- USB commands `w` and `W`

## USB serial capture

Open the USB serial port at `115200` baud.

Useful commands:

```text
b  toggle battery benchmark logging
W  print one weight sample
w  start/stop continuous weight stream
z  diagnostics
```

Sample row:

```text
WMBP_WEIGHT_V1,123456,9821,18.423,1.731,0x0041,98,75,79.82,0
```

See [USB serial protocol](docs/USB_SERIAL.md) for field definitions.

## Compatibility policy

Compatibility paths stay conservative:

- `6E400004` remains exactly a 4-byte little-endian Float32 weight.
- Existing apps should not need WMB+ metadata to read weight.
- Optional metadata is exposed through WMB+ capabilities, the extended packet, USB serial, and diagnostics.

## Known limitations

- Battery percentage is voltage-estimated, not fuel-gauge-grade.
- Charging and runtime estimates are experimental.
- XIAO ESP32S3 is the only prebuilt beta target in `0.2.0-beta.1`.
- The minimal factory image does not include LittleFS web UI assets; flash the separate LittleFS image only if you want the web UI assets updated.
- Calibration is still per-device and must be verified by the builder.
- 80 SPS requires the HX711 hardware rate pin/jumper to be configured correctly.

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

Installation instructions are currently under development, for now follow the [link](https://031devstudios.github.io/weighmybru-docs/#/installation/flashing) for step-by-step installation instructions. Additionally, a video is available on [YouTube](https://www.youtube.com/watch?v=O5SP40Liuq0)

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
