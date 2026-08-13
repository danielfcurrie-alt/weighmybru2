# WMB+ beta tester checklist

Record the board, load cell, HX711 rate mode, battery size, and app used for each test.

## 1. Boot and identity

- Serial banner shows `WMB+ v0.2.0-beta.1`.
- Board shows `XIAO ESP32S3`.
- BLE advertises as `WeighMyBru+`.
- WiFi follows saved state and is disabled by default if disabled in settings.
- Display updates live.

## 2. Physical controls

- Physical tare button zeroes the display.
- Sleep button enters sleep.
- Touching the sleep/wake control wakes the device.
- After wake, BLE, display, HX711, and tare still work.

## 3. BLE compatibility

Test with at least one existing app:

- The app finds the scale.
- Live weight updates.
- App tare zeroes the physical display.
- Basic shot/recording flow works.
- Battery appears if the app reads standard BLE Battery Service.

## 4. WMB+ extended telemetry

With a WMB+ aware app:

- Capabilities characteristic is present.
- Device cadence reports around 80 Hz on 80 SPS hardware.
- Timestamp and sequence are present.
- Battery is present.
- Firmware quality is present.
- Bump/glitch flags appear only around disturbances.

## 5. USB serial

Open serial at `115200`.

Commands:

```text
b  disable battery benchmark noise
W  print one sample
w  start/stop continuous stream
```

Expected:

- `WMBP_WEIGHT_V1` rows parse.
- `hx711_hz` is near expected cadence.
- `dropped` remains zero during normal capture.
- Weight matches display.

## 6. Drift and zero behavior

With an empty platform:

- Tare.
- Let the scale sit untouched for 10 minutes.
- Record min/max weight drift.
- Confirm near-zero cleanup does not erase a real added weight.

## 7. Bump/glitch behavior

Test:

- Light table tap.
- Small platform disturbance.
- Real sustained weight step.
- Real pour.

Expected:

- One-frame impossible values do not become real public weight.
- Sustained weight changes pass through.
- Bump/glitch diagnostics are visible but do not break the stream.

## 8. Battery and charging

Record:

- Starting voltage and percent.
- Ending voltage and percent.
- Whether USB is connected.
- Whether firmware reports charging.
- Approximate elapsed time.

Battery percent is voltage-estimated. If possible, compare against a multimeter or USB power meter.

## 9. Report useful failures

Include:

- firmware version
- board
- app used
- BLE or USB
- screenshot/log/export
- exact steps
- whether rollback fixed it
