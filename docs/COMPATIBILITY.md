# Compatibility

## Tested beta target

- Board: Seeed Studio XIAO ESP32S3
- ADC: HX711
- Preferred HX711 mode: 80 SPS
- BLE name: `WeighMyBru+`

## BLE services and characteristics

Primary WeighMyBru service:

```text
6E400001-B5A3-F393-E0A9-E50E24DCCA9E
```

Characteristics:

```text
6E400002-B5A3-F393-E0A9-E50E24DCCA9E  20-byte WeighMyBru/GaggiMate-compatible weight
6E400003-B5A3-F393-E0A9-E50E24DCCA9E  command
6E400004-B5A3-F393-E0A9-E50E24DCCA9E  4-byte Float32 weight
6E400005-B5A3-F393-E0A9-E50E24DCCA9E  WMB+ capabilities
```

Standard BLE Battery Service:

```text
180F / 2A19
```

## Compatibility expectations

### Bean Conqueror style Float32 clients

Expected:

- Reads live weight from `6E400004`.
- Receives a pure 4-byte little-endian Float32 only.
- Sees a paced 20 Hz compatibility stream.
- Does not need to understand WMB+ metadata.

### WeighMyBru/GaggiMate style 20-byte clients

Expected:

- Reads weight from `6E400002`.
- Existing clients should keep reading weight.
- WMB+ metadata should not be required for basic compatibility.

### WMB+ aware clients

WMB+ aware clients can read:

- capabilities from `6E400005`
- standard BLE battery from `180F / 2A19`
- extended telemetry from the 20-byte packet
- USB serial telemetry when wired

## Commands

Known command IDs:

```text
0x01  tare
0x02  timer start
0x03  timer stop
0x04  timer reset
0x07  atomic tare + start
```

Atomic tare/start is optional and should be used only when capabilities confirm support.
