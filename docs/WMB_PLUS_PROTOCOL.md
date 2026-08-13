# WMB+ protocol summary

WMB+ keeps stock-compatible WeighMyBru BLE behavior and adds explicit capability discovery for apps that want richer telemetry.

## Service

```text
6E400001-B5A3-F393-E0A9-E50E24DCCA9E
```

## Characteristics

```text
6E400002-B5A3-F393-E0A9-E50E24DCCA9E  20-byte weight / extended WMB+ packet
6E400003-B5A3-F393-E0A9-E50E24DCCA9E  command
6E400004-B5A3-F393-E0A9-E50E24DCCA9E  4-byte Float32 compatibility weight
6E400005-B5A3-F393-E0A9-E50E24DCCA9E  WMB+ capabilities
```

Standard battery:

```text
180F / 2A19
```

## Capability payload

The capabilities characteristic is a 16-byte payload:

```text
byte 0   product number
byte 1   capabilities message type
byte 2   capabilities payload version
byte 3   payload length
byte 4   protocol major
byte 5   protocol minor
byte 6   feature mask byte 0
byte 7   feature mask byte 1
byte 8   feature mask byte 2
byte 9   feature mask byte 3
byte 10  preferred atomic command
byte 11  preferred atomic command data1
byte 12  extension packet version
byte 13  extension packet length
byte 14  reserved
byte 15  checksum
```

## Feature mask

```text
bit 0   standard BLE Battery Service
bit 1   physical-parity tare
bit 2   atomic tare/start
bit 3   fresh-sample notify
bit 4   command notify/ack path
bit 5   WMB 20-byte weight
bit 6   Float32 weight
bit 7   HX711 cadence diagnostics
bit 8   extended WMB packet
bit 9   packet device timestamp
bit 10  packet flow
bit 11  packet battery
bit 12  packet sequence
bit 13  scale quality diagnostics
bit 14  lifetime quality diagnostics
bit 15  zero stability control
bit 16  glitch rejection
bit 17  battery charge estimate
bit 18  legacy Float32 20 Hz pacing
```

## Compatibility rule

The Float32 lane remains exactly four bytes:

```text
little-endian Float32 weight_g
```

Do not add metadata to the Float32 characteristic. Apps that want metadata should use the WMB+ capabilities and extended packet or USB serial stream.
