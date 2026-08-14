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

Current XIAO/SuperMini development-build example payload:

```text
03 0C 01 10 01 00 FF FF 17 00 07 00 01 14 00 1A
```

Current TinyS3[D] development-build example payload:

```text
03 0C 01 10 01 00 FF FF FF 00 07 00 01 14 00 F2
```

These advertise the supported feature bits, preferred atomic command `0x07`, extension packet version `1`, and extension packet length `20`.

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
bit 17  battery charge/runtime estimate, including learned voltage-based rate profile when available
bit 18  legacy Float32 20 Hz pacing
bit 19  fuel-gauge battery backend, currently MAX17048 on TinyS3[D]
bit 20  diagnostic event log for exception/error events only; endpoint reports whether it is PSRAM-backed
bit 21  USB power sense
bit 22  software RF antenna switch
bit 23  RGB status LED
```

The diagnostic event log is intentionally not a sample buffer. It records exceptional firmware events such as accepted bump, rejected glitch, low/critical battery, invalid battery, USB power transition, sleep/wake, missing HX711, missing display, missing fuel gauge, and BLE/WiFi faults. XIAO and TinyS3[D] builds target a 512-event PSRAM-backed log when PSRAM is available, then fall back to a small heap log if PSRAM cannot be allocated. `/api/diagnostics/events` reports the actual backend, capacity, allocated bytes, PSRAM size, and free PSRAM.

## 20-byte WMB+ extension packet v1

WMB+ uses the existing 20-byte WeighMyBru/GaggiMate weight characteristic:

```text
6E400002-B5A3-F393-E0A9-E50E24DCCA9E
```

The legacy weight contract is preserved:

- byte `0` remains product number
- byte `1` remains message type
- byte `6` remains weight sign
- bytes `7...9` remain absolute weight in centigrams
- byte `19` remains checksum

Apps should only parse the extended fields after confirming the capabilities characteristic advertises:

- feature bit `8` / `extended WMB packet`
- extension packet version `1`
- extension packet length `20`

Packet byte layout:

| Byte(s) | Name | Type / encoding | Meaning |
| --- | --- | --- | --- |
| `0` | product | UInt8 | `0x03` for WeighMyBru |
| `1` | message type | UInt8 | `0x0B` weight message |
| `2...4` | device timestamp | UInt24 big-endian | firmware sample timestamp in milliseconds, low 24 bits |
| `5` | extension version | UInt8 | `0x01` for this packet layout |
| `6` | weight sign | UInt8 | ASCII `+` (`0x2B`) or `-` (`0x2D`) |
| `7...9` | weight magnitude | UInt24 big-endian | absolute weight in centigrams (`g * 100`) |
| `10` | flow sign | UInt8 | ASCII `+` (`0x2B`) or `-` (`0x2D`) |
| `11...12` | flow magnitude | UInt16 big-endian | absolute flow in centigrams/sec (`g/s * 100`) |
| `13` | battery percent | UInt8 | `0...100`, or `0xFF` when unavailable |
| `14` | sequence | UInt8 | increments once per extended packet; wraps at `255 -> 0` |
| `15` | status flags | UInt8 bitmask | timer, HX711, tare, battery, display state |
| `16` | scale quality | UInt8 | firmware scale-quality score `0...100`, or `0xFF` when unavailable |
| `17` | detected sample rate | UInt8 | rounded detected HX711 sample rate in Hz, `0` when unknown |
| `18` | diagnostic flags | UInt8 bitmask | bump, gap, cadence, rate mode, quality, flow, extension state |
| `19` | checksum | UInt8 | XOR of bytes `0...18` |

Timestamp behavior:

- The timestamp is the firmware-side sample time, not the phone/app receive time.
- It is truncated to 24 bits and rolls over every `16,777,216 ms` / about `4.66 hours`.
- Apps should preserve both device timestamp and host arrival time if they care about BLE transport jitter.

Weight and flow units:

- Weight is signed centigrams, reconstructed from bytes `6...9`.
- Flow is signed centigrams/sec, reconstructed from bytes `10...12`.
- The firmware clamps flow magnitude to UInt16 range.

Checksum:

```text
checksum = byte0 XOR byte1 XOR ... XOR byte18
```

Reject the packet if byte `19` does not match the calculated XOR.

### Status flags, byte 15

```text
bit 0  timer running
bit 1  HX711 connected
bit 2  tare pending
bit 3  atomic tare/start pending
bit 4  battery low
bit 5  battery critical
bit 6  battery present / valid
bit 7  display present
```

Tare-pending and atomic-tare/start-pending bits are intentionally visible so an app can avoid treating transitional samples as shot data.

### Diagnostic flags, byte 18

```text
bit 0  recent bump
bit 1  long gap seen
bit 2  cadence valid
bit 3  80 SPS detected
bit 4  10 SPS detected
bit 5  quality valid
bit 6  flow present
bit 7  extension present
```

Diagnostic bits are sample annotations. They should not automatically cause an app to reject the weight unless the app's own scoring/control policy says so.

## Commands

Commands are written to:

```text
6E400003-B5A3-F393-E0A9-E50E24DCCA9E
```

Current command payloads:

| Command | Bytes | Meaning |
| --- | --- | --- |
| Tare | `03 0A 01 01 00 09` | route app tare through the physical-parity tare path |
| Timer start | `03 0A 02 01 00 0A` | start timer |
| Timer stop | `03 0A 03 01 00 0B` | stop timer |
| Timer reset | `03 0A 04 01 00 0C` | reset timer |
| Atomic tare/start | `03 0A 07 00 00 0E` | tare and start timer as one operation |

The last byte is the XOR checksum of the preceding bytes.

Apps should discover atomic tare/start support through the capabilities characteristic rather than assuming command `0x07` is present.

Command notifications, when subscribed, are acknowledgements/diagnostics only. Apps should not require an acknowledgement for basic compatibility unless they have explicitly discovered and chosen to depend on the WMB+ command notify/ack feature bit.

## Compatibility rule

The Float32 lane remains exactly four bytes:

```text
little-endian Float32 weight_g
```

Do not add metadata to the Float32 characteristic. Apps that want metadata should use the WMB+ capabilities and extended packet or USB serial stream.

## Diagnostic web endpoints

When the web UI is enabled, WMB+ exposes diagnostic endpoints for bench testing and firmware validation:

```text
GET  /api/diagnostics/self-test
GET  /api/diagnostics/events?limit=64
POST /api/diagnostics/events/clear
GET  /api/board/hardware
POST /api/board/status-led   enabled=true|false
POST /api/board/antenna      external=true|false
```

Board hardware controls are no-ops or return an error on boards that do not expose the relevant hardware. TinyS3[D] supports the RF antenna switch and optional RGB status LED; the LED defaults off for power testing.
