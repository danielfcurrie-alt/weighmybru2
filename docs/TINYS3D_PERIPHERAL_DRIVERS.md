# TinyS3[D] peripheral driver staging

The builder wiring reference identifies these devices for the WMB+ TinyS3[D]
hardware:

| Device | Interface | Driver/model status |
| --- | --- | --- |
| LIS2DW12 accelerometer | I2C, address `0x19` | Standalone firmware driver and Wokwi model exist |
| MAX17048 fuel gauge | Shared I2C, address `0x36` | Existing firmware backend and Wokwi model |
| Waveshare 1.47-inch Touch LCD | JD9853 SPI display | Standalone RGB565 firmware driver and protocol-level Wokwi model exist |
| Waveshare touch controller | AXS5106L I2C, address `0x63` | Standalone two-point firmware driver and Wokwi model exist |

The current SSD1306 `Display` implementation remains the production backend.
The new classes are compiled but not instantiated, so they do not initialize a
bus, claim a GPIO, or change current firmware behavior.

## Firmware staging boundary

- `LIS2DW12Driver` validates `WHO_AM_I=0x44`, performs a bounded reset,
  configures high-performance sampling, supports all four full-scale ranges,
  reads XYZ/temperature, and can route data-ready to `INT1`.
- `JD9853DisplayDriver` owns only the low-level 172x320 RGB565 SPI protocol,
  rotation, address windows, reset, display enable, inversion, and backlight.
- `AXS5106TouchDriver` uses the shared `TwoWire` bus, validates address `0x63`,
  reads up to two points, and applies the orientation mapping from Waveshare's
  ESP32 reference demo.
- Drivers receive bus objects and GPIOs through configuration. They do not call
  `Wire.begin()` or use speculative `BoardConfig.h` assignments.

## Pinout gate

Do not enable these drivers in `main.cpp` until the builder supplies a text pin
table for all of the following signals:

```text
LIS2DW12: SDA SCL INT1
LCD:      MISO MOSI SCLK LCD_CS LCD_DC LCD_RST LCD_BL
Touch:    TP_SDA TP_SCL TP_INT TP_RST
```

The table must state the TinyS3[D] GPIO number, module pin label, shared-bus
relationship, active level, and whether each connection is verified from the
PCB/netlist. The image remains useful corroborating evidence but is not precise
enough to assign crossing wires safely.

## Next integration steps

1. Bind verified pins in `BoardConfig.h` behind TinyS3[D]-only feature flags.
2. Add bounded USB/dashboard diagnostics for LIS2DW12 identity, XYZ, interrupt,
   read errors, and last-sample age before using motion to change scale output.
3. Introduce a display interface and port screens from the SSD1306 backend to a
   color layout; keep the low-level JD9853 transport independent of that UI.
4. Add short Wokwi scenarios for sensor discovery, data-ready, touch points,
   display initialization, and continued HX711 cadence. Do not run the full
   matrix while developing one peripheral.
5. Validate orientation, touch mapping, SPI frequency, backlight polarity, and
   motion thresholds on real hardware.

Protocol sources: ST LIS2DW12 datasheet/AN5038 and Waveshare's official
1.47-inch Touch LCD ESP32 demo (JD9853 + AXS5106L).
