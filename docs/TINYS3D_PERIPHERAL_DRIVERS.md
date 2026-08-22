# TinyS3[D] peripheral driver staging

The builder wiring reference identifies these devices for the WMB+ TinyS3[D]
hardware:

| Device | Interface | Driver/model status |
| --- | --- | --- |
| LIS2DW12 accelerometer | I2C, address `0x19` | Standalone firmware driver and Wokwi model exist |
| MAX17048 fuel gauge | Shared I2C, address `0x36` | Dedicated firmware driver, dashboard diagnostics, and alert-aware Wokwi model exist |
| Waveshare 1.47-inch Touch LCD | JD9853 SPI display | Standalone RGB565 firmware driver and pixel-rendering Wokwi model exist |
| Waveshare touch controller | AXS5106L I2C, address `0x63` | Standalone two-point firmware driver and Wokwi model exist |

`TinyS3DPeripherals` now instantiates the production LIS2DW12, JD9853 display,
AXS5106L touch, motion-analysis, and color-UI paths only for `BOARD_TINYS3D`.
The separate `WMBP_WOKWI_RUNTIME_HARNESS` coordinator exercises the same
drivers in the TinyS3[D] simulator proxies using explicitly simulated pins.
They are compiled only when `BOARD_TINYS3D` is selected; XIAO and SuperMini
builds do not include the LIS2DW12, motion analyzer, MAX17048, Waveshare color
display/touch, QR dependency, or packed color-logo code.

## Firmware staging boundary

- `LIS2DW12Driver` validates `WHO_AM_I=0x44`, performs a bounded reset,
  configures high-performance sampling, supports all four full-scale ranges,
  reads XYZ/temperature, and can route data-ready to `INT1`.
- `MotionAnalyzer` accepts timestamped XYZ samples without owning a bus or GPIO.
  Its diagnostics snapshot reports gravity/orientation, linear acceleration,
  vibration energy/RMS, impact peaks, quiet confidence, and bounded tap and
  double-tap candidates. It is not connected to weight filtering or tare.
- `JD9853DisplayDriver` owns only the low-level 172x320 RGB565 SPI protocol,
  rotation, address windows, reset, display enable, inversion, and backlight.
- `AXS5106TouchDriver` uses the shared `TwoWire` bus, validates address `0x63`,
  reads up to two points, and applies the orientation mapping from Waveshare's
  ESP32 reference demo.
- `Waveshare147UI` is a pin-independent, PSRAM-backed color canvas. It stages a
  live weight screen, an autoscaling 24-second flow curve, a web-app QR screen, and a
  system/battery status screen. Its Pour Over view consumes the same
  firmware-authoritative recipe, current stage, stage timer, total timer,
  target weight, and flow state published to the PWA. `begin()` immediately presents the official
  WeighMyBru+ artwork as a 16-color packed splash screen, optionally labelled
  with the firmware version.
  Production rendering is capped at 4 Hz; flow history is sampled at that same
  display cadence so the 96-point graph spans about 24 seconds without coupling
  full-frame SPI traffic to HX711 acquisition.
- `MAX17048Driver` reads VCELL, SOC, VERSION, CRATE, STATUS, CONFIG, and VALRT.
  It exposes latched alerts and communication counters and provides explicit,
  opt-in QuickStart, alert-clear, SOC-threshold, and voltage-threshold methods.
  Normal battery polling never invokes QuickStart or changes gauge settings.
- Drivers receive bus objects and GPIOs through configuration. They do not call
  `Wire.begin()` or use speculative `BoardConfig.h` assignments.

## Color display staging

The staged portrait UI uses the 172x320 panel as an operational display rather
than a stretched version of the 128x32 OLED:

- primary weight with flow and timer at a glance;
- a 96-sample signed flow history with automatic vertical scale;
- a version-4 QR code for the local HTTP web app plus a readable address;
- scale/Wi-Fi/BLE/battery state and MAX17048 charge-rate/alert information.
- synchronized Pour Over stage name, stage/total timers, progress, weight
  target, and flow, ready for runtime binding once LCD pins are verified.

The framebuffer consumes about 110 KiB and prefers PSRAM, with heap fallback
only for test environments. The UI is created by `main.cpp` through
`TinyS3DPeripherals` on production Tiny builds and through the separate Wokwi
coordinator in simulation. XIAO and SuperMini never instantiate this path.

The custom Wokwi JD9853 model now maintains RGB565 GRAM and presents it through
Wokwi's 172x320 RGBA framebuffer. It handles CASET, RASET, RAMWR, COLMOD,
MADCTL rotation, reset, display enable, and backlight visibility using buffered
SPI reception. LCD readback and panel electrical behavior remain out of scope.

## Fuel-gauge telemetry

TinyS3[D] `/api/battery` output now includes the gauge version, signed CRATE in
percent/hour, STATUS and CONFIG words, asserted-alert state, SOC and voltage
alert thresholds, communication-error count, and diagnostic timestamp. The
dashboard includes the signed gauge rate and alert state for lightweight UI
use. These fields are additive and do not change XIAO/SuperMini ADC behavior.

The signed CRATE also supplies provisional current-load runtime and charge-time
estimates before the longer observation-window learner has enough SOC movement.
The battery page exposes separate WiFi-off and WiFi-on runtime and charge
projections. Those scenario values use the configured battery capacity and the
explicit `tinys3d-v1-linear` assumptions (39 mA active with WiFi off, 81 mA
with WiFi on, 300 mA charger, and 85% charge efficiency); they are projections,
not MAX17048 current measurements. CRATE-based estimates expire if gauge
diagnostics become stale. The linear 100% projection is likely optimistic near
the charger taper and needs physical calibration.

## Accelerometer test boundary

Host tests now execute the real `LIS2DW12Driver` against a register-level fake
I2C device. They cover identity/reset, all XYZ ranges, status and pin-based
data-ready, communication counters, and deterministic 94.2 Hz HX711 cadence
bookkeeping while 100 Hz accelerometer reads are interleaved. Motion-analysis
tests cover quiet, vibration, knock, cup-placement ring-down, double tap, and
orientation. The Wokwi chip provides matching selectable waveform controls.

Focused Wokwi profiles now add driver discovery, data-ready, all motion
patterns, fuel-gauge direction/alert/missing-device behavior, rendered screen
checkpoints, touch page selection, and HX711 cadence under concurrent I2C/SPI
traffic. They do not measure enclosure resonances, real interrupt latency, or
electrical margins; those remain physical TinyS3[D] gates.

## Builder wiring transcription

The detailed builder image received on 2026-08-21 is preserved at
`docs/images/tinys3d-builder-wiring-20260821.png`. The following table is a
careful image transcription, not yet a PCB/netlist verification:

| Device signal | TinyS3[D] connection | Confidence / note |
| --- | --- | --- |
| HX711 `DT` | GPIO21 | Clear labeled endpoint |
| HX711 `SCK` | GPIO2 | Clear labeled endpoint |
| HX711 `VCC` / `GND` | 3V3 / GND | Clear power rails |
| HX711 `RATE` | SPDT switch: 3V3 = 80 SPS, GND = 10 SPS | Builder-confirmed polarity on 2026-08-21 |
| LIS2DW12 `SDA` / `SCL` | GPIO8 / GPIO9 | Shared I2C bus |
| LIS2DW12 `VCC` / `GND` | 3V3 / GND | Clear power rails |
| LIS2DW12 `SDO`, `CS` | No external connection in drawing | Breakout is configured for I2C; verify its onboard address/CS straps during hardware bring-up |
| LIS2DW12 `INT1`, `INT2` | Intentionally not connected | Builder confirmed I2C polling mode; no interrupt GPIO is used |
| Display `MOSI` / `SCLK` | GPIO35 / GPIO36 | Clear labeled endpoints |
| Display `MISO` | Intentionally not connected | Builder confirmed the JD9853 only receives data from the MCU; use a write-only SPI path |
| Display `LCD_CS` / `LCD_DC` | GPIO34 / GPIO1 | Clear labeled endpoints |
| Display `LCD_RST` / `LCD_BL` | GPIO4 / GPIO5 | Clear labeled endpoints |
| Display touch `TP_SDA` / `TP_SCL` | GPIO8 / GPIO9 | Shared I2C bus |
| Display touch `TP_INT` / `TP_RST` | GPIO6 / GPIO7 | Clear labeled endpoints |
| Display `VCC` / `GND` | 3V3 / GND | Clear power rails |
| Standalone sleep touch (left) | GPIO37 | Builder-confirmed role on 2026-08-21 |
| Standalone tare touch (right) | RX / GPIO44 | Builder-confirmed role on 2026-08-21 |
| Battery positive | Power switch to `BAT` | Battery negative connects to GND |

This map is now active in the `BOARD_TINYS3D` branch of `BoardConfig.h`:
TinyS3[D] HX711 is GPIO21/2 rather than GPIO5/6, GPIO4/5 are consumed by LCD
reset/backlight, and the standalone touch inputs are GPIO37/GPIO44 rather than
GPIO4/3. GPIO8/9 remain the shared I2C bus. XIAO and SuperMini retain their
existing WMB+ reference pins.

The builder has confirmed that display MISO and the LIS2DW12 interrupt pins are
intentionally unconnected. The driver configuration now uses write-only SPI and
I2C polling accordingly. Before physical display enablement, verify active
levels for LCD reset/backlight and display-touch reset/interrupt from the module
documentation or a bounded hardware test.

## Beta 10 integration sequence

1. Validate the builder-confirmed pin map and active levels on the first
   physical TinyS3[D] assembly.
2. Confirm the shared I2C bus discovers MAX17048, LIS2DW12, and AXS5106L while
   HX711 acquisition and full-frame display updates remain continuous.
3. Validate LIS2DW12 motion diagnostics while keeping them disconnected from
   weight filtering and tare behavior.
4. Validate the color UI, official splash, flow history, QR page, Pour Over
   session, orientation, and touch page selection on the physical panel.
5. Validate MAX17048 SOC, alerts, charge direction, and runtime/charge
   projections against physical discharge and charge observations.
6. Run the short, independently budgeted Wokwi scenarios for rendered screen
   checkpoints, sensor discovery, data-ready, touch points, and continued HX711
   cadence. Do not run the full matrix while developing one peripheral.
7. Validate orientation, touch mapping, SPI frequency, backlight polarity,
   motion thresholds, RF coexistence, and power draw on real hardware.
8. Only after diagnostics and false-positive testing are stable, evaluate
   double-tap tare, motion wake, and disturbance-aware clean output.

Protocol sources: ST LIS2DW12 datasheet/AN5038 and Waveshare's official
1.47-inch Touch LCD ESP32 demo (JD9853 + AXS5106L).
