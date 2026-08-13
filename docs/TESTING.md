# WMB+ beta tester checklist

Record the board, load cell, HX711 rate mode, battery size, and app used for each test.

## 1. Boot and identity

- Serial banner shows `WMB+ v0.2.0-beta.2`.
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
- Runtime estimate, charge estimate, learning confidence, and learned charge/discharge rates from the battery page or `/api/battery`.

Battery percent is voltage-estimated. If possible, compare against a multimeter or USB power meter.

Learning test:

- Run on battery for at least 20 minutes with WiFi in the normal test state.
- Confirm learned discharge observations increase after a material percent drop.
- Plug into USB and charge for at least 15 minutes.
- Confirm learned charge observations increase after a material percent rise.
- Reboot and confirm learned charge/discharge rates survive.

## 9. StopMyBru HTTP webhook relay

If you have a local Tasmota or Shelly relay:

- Enable WiFi and connect the scale to the same LAN as the relay.
- Open the StopMyBru web page.
- Enter the relay host/IP.
- Apply the Tasmota, Shelly Gen1, Shelly Plus/Pro, or custom preset.
- Save the webhook settings.
- Confirm target stop learning is enabled.
- Test ON and Test OFF from the page.
- Arm the relay/timer and confirm the OFF webhook fires when target weight is reached.
- Run several automatic target stops with the same grinder/brewer and cup/load setup.
- Confirm the learned offset and effective cutoff update in the StopMyBru page.

Expected:

- The page reports WiFi connected before tests.
- Test ON and Test OFF return HTTP `2xx` or `3xx`.
- Manual OFF and target-weight cutoff both send the OFF webhook.
- Manual OFF does not train the learned offset.
- Automatic target cutoff waits for settled final weight, then adjusts the learned offset toward the observed overshoot/undershoot.
- If WiFi is disabled or disconnected, the page reports that the webhook cannot run.

## 10. Web OTA

After installing the WMB+ dual-OTA factory image:

- Enable WiFi and open the Updates page.
- Confirm firmware OTA reports ready.
- Upload an app firmware `-app.bin`.
- Confirm the scale restarts and the version/boot log is correct.
- Upload a LittleFS `-littlefs.bin`.
- Confirm the web UI remains available after restart.

Expected:

- App OTA is blocked with a clear message on older single-app partition tables.
- Successful OTA restarts the scale automatically.
- NVS settings, calibration, and learned battery/StopMyBru state survive.

## 11. Report useful failures

Include:

- firmware version
- board
- app used
- BLE or USB
- screenshot/log/export
- exact steps
- whether rollback fixed it
