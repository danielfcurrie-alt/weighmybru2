# WMB+ USB serial protocol

WMB+ exposes an opt-in text stream over USB CDC serial.

Serial settings:

```text
115200 baud
UTF-8 / ASCII line-oriented text
```

## Commands

Send commands followed by newline.

```text
w
```

Toggle continuous USB weight streaming.

```text
W
```

Print one header and one sample.

```text
b
```

Toggle battery benchmark logging. Send this once before capture if you want only weight rows.

```text
B
```

Print one battery benchmark line.

```text
d
```

Reset the battery drain/charge benchmark baseline to the current battery reading and print a fresh benchmark line.

```text
z
```

Print diagnostics.

## Sample format

Header:

```text
WMBP_WEIGHT_V1_HEADER,ms,seq,weight_g,flow_gps,status,quality,battery_pct,hx711_hz,dropped
WMBP_SOURCE_V1_HEADER,ms,raw_seq,raw_g,qualified_seq,qualified_g,public_seq,public_raw_seq,public_g,raw_to_public_gap,rejected,status
WMBP_FLOAT32_V1_HEADER,ms,notify_count,weight_g,source_seq,source_age_ms,source_count,window_range_g,suspect_count,status
```

Sample:

```text
WMBP_WEIGHT_V1,123456,9821,18.423,1.731,0x0041,98,75,79.82,0
WMBP_SOURCE_V1,123456,12450,18.512,12450,18.512,9821,12450,18.423,0,3,0x0007
WMBP_FLOAT32_V1,123456,240,18.512,12450,37,15,0.183,0,0x0011
```

Fields:

```text
type,ms,seq,weight_g,flow_gps,status,quality,battery_pct,hx711_hz,dropped
type,ms,raw_seq,raw_g,qualified_seq,qualified_g,public_seq,public_raw_seq,public_g,raw_to_public_gap,rejected,status
type,ms,notify_count,weight_g,source_seq,source_age_ms,source_count,window_range_g,suspect_count,status
```

## Field definitions

| Field | Type | Meaning |
| --- | --- | --- |
| `type` | string | Always `WMBP_WEIGHT_V1` for sample rows |
| `ms` | unsigned integer | firmware `millis()` timestamp |
| `seq` | unsigned integer | scale sample sequence |
| `weight_g` | float | current public weight in grams |
| `flow_gps` | float | firmware flow estimate in grams/sec |
| `status` | hex UInt16 | status bitmask |
| `quality` | UInt8 | firmware scale quality, `0...100` |
| `battery_pct` | integer | visible battery percent, valid only when battery-valid bit is set |
| `hx711_hz` | float | detected HX711 cadence |
| `dropped` | unsigned integer | cumulative skipped `WMBP_WEIGHT_V1` frames; supplemental diagnostic row drops are tracked separately in config diagnostics |

`WMBP_SOURCE_V1` rows are supplemental diagnostics for source-stage validation.
They let capture tools distinguish calibrated raw input, plausibility-qualified
input, and public output without changing the stable `WMBP_WEIGHT_V1` row.
They are opt-in on live hardware: send serial command `s` after enabling the
USB weight stream with `w`. This keeps the normal 80 Hz weight stream below the
115200 baud budget unless source diagnostics are being captured intentionally.

| Field | Type | Meaning |
| --- | --- | --- |
| `type` | string | Always `WMBP_SOURCE_V1` for source diagnostic rows |
| `ms` | unsigned integer | timestamp of the latest raw input sample |
| `raw_seq` | unsigned integer | successful raw-read/source sequence for the latest calibrated raw input |
| `raw_g` | float | latest calibrated raw grams before plausibility rejection |
| `qualified_seq` | unsigned integer | source sequence of the latest plausibility-qualified input |
| `qualified_g` | float | latest qualified grams before public smart filtering/zero qualification |
| `public_seq` | unsigned integer | public scale sample sequence |
| `public_raw_seq` | unsigned integer | newest raw/source sequence consumed by the public output stage |
| `public_g` | float | current public weight in grams |
| `raw_to_public_gap` | unsigned integer | instantaneous source-stage lag: latest raw source sequence minus latest public source sequence |
| `rejected` | unsigned integer | plausibility-rejected source sample count in the current input epoch |
| `status` | hex UInt16 | source diagnostic status bitmask |

`WMBP_FLOAT32_V1` rows are supplemental diagnostics for the 4-byte Float32
compatibility estimator. They are emitted only when source diagnostics are
enabled and when the Float32 estimator tick advances. They do not change the BLE
Float32 characteristic, which remains exactly four bytes. `notify_count` advances
only for actual BLE notifications, so it may remain unchanged in USB-only or
stale-source diagnostics.

| Field | Type | Meaning |
| --- | --- | --- |
| `type` | string | Always `WMBP_FLOAT32_V1` for Float32 diagnostic rows |
| `ms` | unsigned integer | device `millis()` timestamp when the Float32 estimator tick ran |
| `notify_count` | unsigned integer | cumulative Float32 notifications emitted |
| `weight_g` | float | last Float32 weight sent over BLE |
| `source_seq` | unsigned integer | source sequence selected for the last Float32 output, or `0` if none |
| `source_age_ms` | unsigned integer | age of the selected source sample at Float32 emission, or `0` if invalid |
| `source_count` | UInt8 | qualified source samples available in the selection window |
| `window_range_g` | float | min-to-max range of qualified source samples in the selection window |
| `suspect_count` | unsigned integer | cumulative suspect Float32 selections |
| `status` | hex UInt16 | Float32 diagnostic status bitmask |

## Status bits

```text
0x0001 HX711 connected
0x0002 BLE connected
0x0004 recent bump
0x0008 recent glitch
0x0010 zero clamped
0x0020 auto-zero active
0x0040 battery valid
0x0080 charging
0x0100 WiFi radio on
```

## Source status bits

```text
0x0001 latest raw input is finite
0x0002 latest qualified input is finite
0x0004 public output stage has consumed the latest qualified input
```

## Float32 status bits

```text
0x0001 selected source sample is valid
0x0002 selection window had fewer than 3 qualified source samples
0x0004 selected source sample age was at least one 20 Hz Float32 tick
0x0008 last selection was marked suspect
0x0010 BLE client connected
0x0020 last selection was a confirmed load discontinuity
0x0040 selected source was stale or unavailable
```

## Parser rules

- Ignore unknown lines.
- Treat the header as optional.
- Parse only lines beginning exactly with `WMBP_WEIGHT_V1,`.
- Tools that support source diagnostics may also parse lines beginning exactly
  with `WMBP_SOURCE_V1,`.
- Tools that support Float32 diagnostics may also parse lines beginning exactly
  with `WMBP_FLOAT32_V1,`.
- Require exactly 10 CSV fields.
- Require exactly 12 CSV fields for `WMBP_SOURCE_V1`.
- Require exactly 10 CSV fields for `WMBP_FLOAT32_V1`.
- Preserve both device timestamp and host receive timestamp.
- Treat an increase in `dropped` as USB transport/backpressure loss.
- Treat bump/glitch bits as diagnostics attached to samples, not as automatic sample rejection.

## Battery benchmark rows

When battery benchmark logging is enabled, the firmware periodically prints lines beginning with:

```text
BATTERY_BENCH
```

These are diagnostics, not weight samples. Weight parsers should ignore them unless they explicitly support battery benchmarking.

Important fields include:

| Field | Meaning |
| --- | --- |
| `session` | current benchmark label, e.g. `boot`, `serial`, `wifi-off`, `wifi-ap`, `oled-on` |
| `sessionMin` | elapsed minutes since the current benchmark baseline |
| `sessionDeltaV` | voltage change since baseline |
| `sessionDeltaRawPercent` | raw unquantized percent change since baseline |
| `voltageMvPerHour` | voltage slope, useful for comparing board/power modes |
| `rawPercentPerHour` | raw percent slope, useful once voltage-to-percent mapping is calibrated |
| `sessionTrend` | `learning`, `charging`, `draining`, `flat`, or `unknown` |
| `sessionConfidence` | `none`, `learning`, `low`, `medium`, or `high` |
| `wifiEnabled`, `wifiMode`, `wifiSleep` | WiFi state during the sample |
| `bleConnected` | whether a BLE central is connected |
| `display` | whether the OLED/display path is available |
| `hx711Hz`, `hx711Mode` | detected load-cell ADC cadence |
| `usbWeightStream` | whether continuous USB weight streaming is enabled |

The same benchmark snapshot is available over WiFi at:

```text
GET  /api/battery/benchmark
POST /api/battery/benchmark/reset
```

`/api/battery/benchmark/reset` accepts an optional `label` parameter so a test runner can mark the mode under test.
