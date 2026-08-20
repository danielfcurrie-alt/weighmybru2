# TinyS3[D] Wokwi custom-board prototype

This directory contains an unofficial WMB+ test definition for the Unexpected
Maker TinyS3[D]. It uses Wokwi's ESP32-S3 simulation engine while replacing the
DevKitC/XIAO board shell with the TinyS3[D] header and internal GPIO mapping.

## Sources of truth

- Installed PlatformIO board metadata:
  `~/.platformio/platforms/espressif32/boards/um_tinys3.json`
- Installed Arduino variant:
  `~/.platformio/packages/framework-arduinoespressif32/variants/um_tinys3/pins_arduino.h`
- Unexpected Maker TinyS3[D] pin card:
  <https://unexpectedmaker.com/images/pinout_cards/tinys3d_pinout.jpg>
- Wokwi custom-board format:
  <https://github.com/wokwi/wokwi-boards>

The artwork in `board.svg` is an original functional test drawing. It does not
copy Unexpected Maker's product artwork.

## Modeled board properties

- ESP32-S3 MCU
- Arduino FQBN `esp32:esp32:um_tinys3`
- 8 MB QIO flash and 8 MB QSPI PSRAM
- native USB Serial/JTAG monitor
- physical TinyS3[D] header pin mapping
- internal VBUS sense on GPIO33
- internal MAX17048 interrupt on GPIO10
- RGB power/data on GPIO17/GPIO18
- antenna switch on GPIO38

Internal signals use `$`-prefixed virtual pins. They do not appear as physical
header pins, but a test diagram can connect them to power or instrumentation.

## Interactive loading

Wokwi's documented custom-board workflow currently uses the web editor:

1. Open a Wokwi project.
2. Run `Load custom board file...` from the command palette.
3. Select this directory, which contains `board.json` and `board.svg`.
4. Use `wokwi-custom-board` as the board part in the project's `diagram.json`.

`diagram.example.json` contains the minimal diagram from this workflow.
The repository-root `diagram.tinys3d-custom-board.json` contains the complete
WMB+ HX711/OLED/MAX17048/LIS2DW12/RGB test circuit. It also uses a blue LED as
an instrumentation-only indication of GPIO38 antenna-switch state.

Validate that diagram and all custom-board pin references without starting the
simulator:

```bash
python3 tools/check-wokwi-diagram.py diagram.tinys3d-custom-board.json
```

Example board part:

```json
{
  "type": "wokwi-custom-board",
  "id": "tinys3d",
  "top": 0,
  "left": 0,
  "attrs": {}
}
```

## Current boundary

The installed `wokwi-cli` 0.26.1 exposes no argument for loading an unpublished
local `board.json`/`board.svg`. Both online and `--offline` lint of
`diagram.example.json` currently report `unknown-part-type` for
`wokwi-custom-board`.

A bounded browserless experiment on 2026-08-20 patched a temporary copy of the
CLI to upload `board.json` and `board.svg` before simulation. The Simulation API
accepted the files and found the `wokwi-custom-board` part, but did not initialize
it (`Part found, but it has not been initialized`). Inspection of the public web
editor confirms why: `Load custom board file...` calls the browser-only
`loadUserBoard(boardJson, boardSvg)` registry method. Uploaded project files are
not an equivalent CLI mechanism.

Do not add this board to the paid runtime matrix until one of these paths is
proven:

1. Wokwi documents a local-board CLI configuration mechanism.
2. The definition is accepted into the `wokwi/wokwi-boards` registry.
3. Wokwi adds a portable project-local board mechanism to the CLI or VS Code
   extension.

The practical next step is contributing this definition to
`wokwi/wokwi-boards`. Once accepted, use its assigned `board-<id>` part type in
the root diagram and CLI matrix.

This board improves digital pin and variant fidelity. It still cannot validate
RF performance, electrical power behavior, physical vibration/noise, sensor
accuracy, or real deep-sleep current.
