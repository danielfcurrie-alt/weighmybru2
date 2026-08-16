# WMB+ beta tester checklist

Record the board, load cell, HX711 rate mode, battery size, and app used for each test.

## 1. Boot and identity

- Serial banner shows `WMB+ v0.2.0-beta.5`.
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

## 5a. Runtime cadence smoke test

Run this on a flashed XIAO reference unit when validating dashboard/API changes. It measures the real USB `WMBP_WEIGHT_V1` stream while also polling web endpoints, so it catches runtime interference that simulation builds and source-level checks cannot prove.

```bash
python3 tools/runtime-cadence-smoke.py \
  --port /dev/cu.usbmodem1101 \
  --base-url http://192.168.4.1 \
  --profile baseline \
  --profile dashboard-safe
```

Expected:

- `baseline` passes at roughly 79-80 Hz on 80 SPS hardware.
- `dashboard-safe` also passes at roughly 79-80 Hz while the web dashboard endpoints are being polled.
- `device_gaps_over_100ms` is zero.
- `missingSeq` is zero.
- `droppedDelta` is zero.
- Web polling reports zero endpoint errors.

To intentionally reproduce old dashboard pressure without making the shell command fail:

```bash
python3 tools/runtime-cadence-smoke.py \
  --port /dev/cu.usbmodem1101 \
  --base-url http://192.168.4.1 \
  --profile aggressive-repro \
  --no-fail
```

`static-repro` isolates the old short static-info polling cadence:

```bash
python3 tools/runtime-cadence-smoke.py \
  --port /dev/cu.usbmodem1101 \
  --base-url http://192.168.4.1 \
  --profile static-repro \
  --no-fail
```

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
- Battery benchmark session output from serial `BATTERY_BENCH` or `/api/battery/benchmark`.

Battery percent is voltage-estimated. If possible, compare against a multimeter or USB power meter.

Drain/charge benchmark test:

1. Start from a reasonably stable battery level, ideally below 95% for charge tests and above 30% for drain tests.
2. Reset the benchmark baseline:
   - serial: send `d`
   - web: `POST /api/battery/benchmark/reset` with optional `label`
3. Let the scale run for at least 30 minutes per mode. 90+ minutes is better.
4. Save the starting and ending `/api/battery/benchmark` JSON or serial `BATTERY_BENCH` rows.
5. Compare `voltage_mv_per_hour`, `raw_percent_per_hour`, `trend`, and `confidence`.

Recommended mode labels:

- `xiao-wifi-off`
- `xiao-wifi-ap`
- `xiao-ble-connected`
- `xiao-usb-stream`
- `xiao-oled-on`
- `supermini-wifi-off`
- `supermini-wifi-ap`
- `tiny-fuelgauge-wifi-off`
- `tiny-fuelgauge-charging`

Minimum comparison matrix:

| Board | Mode | Minimum time | What it tells us |
| --- | --- | ---: | --- |
| XIAO ESP32S3 | WiFi off, BLE advertising, OLED on | 30 min | baseline ADC-board drain |
| XIAO ESP32S3 | WiFi AP on | 30 min | WiFi/AP penalty |
| XIAO ESP32S3 | BLE connected + active capture | 30 min | app-connected penalty |
| XIAO ESP32S3 | USB connected / charging | 30 min | charge trend and charge-rate estimate |
| SuperMini | same as XIAO baseline | 30 min | board-to-board current difference |
| TinyS3[D] | WiFi off + fuel gauge | 30 min | fuel-gauge accuracy and USB-power detection |

Learning test:

- Run on battery for at least 20 minutes with WiFi in the normal test state.
- Confirm learned discharge observations increase after a material percent drop.
- Plug into USB and charge for at least 15 minutes.
- Confirm learned charge observations increase after a material percent rise.
- Reboot and confirm learned charge/discharge rates survive.

## 9. StopMyBru HTTP webhook relay

Smoke-test the HTTP webhook path before every public beta. A real relay is useful, but not required for the basic ON/OFF HTTP test.

Without a real relay:

- Start a simple HTTP receiver on a computer on the same LAN as the scale, for example `python3 -m http.server 8080`.
- Enable WiFi and connect the scale to the same LAN as the receiver.
- Open the StopMyBru web page.
- Enable HTTP webhook relay.
- Use custom `http://` URLs for the receiver, for example `http://<computer-ip>:8080/wmbplus/on` and `http://<computer-ip>:8080/wmbplus/off`.
- Save the webhook settings.
- Press Test ON and Test OFF from the page.
- Confirm both requests appear in the receiver logs and the StopMyBru page reports HTTP `2xx` or `3xx`.

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
- A fake local HTTP receiver sees both the ON and OFF requests during the smoke test.
- Manual OFF and target-weight cutoff both send the OFF webhook.
- Manual OFF does not train the learned offset.
- Automatic target cutoff waits for settled final weight, then adjusts the learned offset toward the observed overshoot/undershoot.
- If WiFi is disabled or disconnected, the page reports that the webhook cannot run.

## 10. Release-blocking OTA smoke test

Run this before every public beta tag. Do not mark a release ready until this passes on the primary XIAO reference unit.

Start from the previous published beta installed with the dual-OTA factory layout, then update to the release candidate using only the browser Updates page.

Required:

- Download the candidate `xiao-app.bin`.
- Upload `xiao-app.bin` through **App Firmware OTA**.
- Confirm the upload reaches 100%, the scale restarts, and the version/boot log reports the candidate version.
- Confirm `/api/ota/status` or the Updates page shows the active partition changed and the next OTA partition is available.
- Download the candidate `xiao-littlefs.bin`.
- Upload `xiao-littlefs.bin` through **Web UI / LittleFS OTA**.
- Confirm the upload reaches 100% and the web UI remains available after restart/reload.
- Confirm the Updates page shows the candidate UI text/assets.
- Confirm NVS settings, calibration factor, WiFi settings, and learned battery/StopMyBru state survive.

This test must use the actual browser multipart upload path. Building LittleFS, checking release asset size, verifying partition offsets, or flashing `factory-full.bin` over USB is not a substitute.

Failure examples this test is meant to catch:

- app OTA writes to the wrong partition
- LittleFS OTA rejects a valid filesystem image
- multipart upload size is confused with file size
- OTA succeeds but calibration/NVS is lost
- web UI assets are stale after an app update

## 11. Web OTA

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

## 12. Report useful failures

Include:

- firmware version
- board
- app used
- BLE or USB
- screenshot/log/export
- exact steps
- whether rollback fixed it
