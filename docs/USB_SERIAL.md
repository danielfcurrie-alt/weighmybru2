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
z
```

Print diagnostics.

## Sample format

Header:

```text
WMBP_WEIGHT_V1_HEADER,ms,seq,weight_g,flow_gps,status,quality,battery_pct,hx711_hz,dropped
```

Sample:

```text
WMBP_WEIGHT_V1,123456,9821,18.423,1.731,0x0041,98,75,79.82,0
```

Fields:

```text
type,ms,seq,weight_g,flow_gps,status,quality,battery_pct,hx711_hz,dropped
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
| `dropped` | unsigned integer | cumulative skipped USB serial frames |

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

## Parser rules

- Ignore unknown lines.
- Treat the header as optional.
- Parse only lines beginning exactly with `WMBP_WEIGHT_V1,`.
- Require exactly 10 CSV fields.
- Preserve both device timestamp and host receive timestamp.
- Treat an increase in `dropped` as USB transport/backpressure loss.
- Treat bump/glitch bits as diagnostics attached to samples, not as automatic sample rejection.
