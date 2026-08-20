# WMB+ 0.2.0-beta.7 internal notes

Beta 7 was an internal hardware/acquisition and web-cadence hardening series.
It was not published as a public GitHub Release.

The beta7 work closed the gap between the earlier beta6 safety hardening and
the later beta8/beta9 field builds. Several XIAO-only test images were produced
for MacBook flashing, including DOUT acquisition, web-pressure cache, AsyncTCP
churn, BLE sequence, and runtime tare builds.

## Main changes

- Added baseline HX711 acquisition diagnostics for raw reads, cadence, long
  gaps, accepted/rejected samples, and public-stream lag.
- Added the first Wokwi DOUT acquisition harness for HX711 timing experiments.
- Hardened high-rate tare publishing so public samples and tare state remain
  coherent during runtime tare operations.
- Cached dynamic and static web status endpoints to reduce dashboard/API
  pressure on the 80 SPS acquisition path.
- Added bounded HTTP connection-churn smoke coverage.
- Hardened the AsyncTCP web listener under repeated browser polling and
  reconnect churn.
- Updated ESPAsyncWebServer/AsyncTCP and migrated the BLE scale service to
  NimBLE-Arduino 2.5.1.
- Kept BLE weight packet sequence contiguous when notifications are dropped.
- Used a recent public sample for runtime tare so app/web tare behavior better
  matches the live public stream.

## Web and PWA work

- Added the WMB+ installable app shell and PWA shortcuts.
- Streamlined the WMB+ web app views.
- Polished WMB+ web UI branding and app attention cues.
- Added `wmb.local` as the primary mDNS short URL with `wmbplus.local` retained
  as a legacy alias.
- Added dashboard system details and firmware-copy affordances for tester
  reporting.
- Made dashboard refresh less aggressive so the web UI does not steal timing
  budget from the scale loop.
- Added OTA progress near upload buttons.

## Release posture

Beta 7 was a test train, not a public tester release. Its job was to validate
the acquisition, web, BLE, and dependency changes before producing a cleaner
field build.

Known beta7 test artifact families included:

- `beta7-dout-*`
- `beta7-dout.1-*`
- `beta7-dout-interrupt-*`
- `wmb-plus-beta7-webpressure-cache-*`
- `wmb-plus-beta7-live-endpoint-cache-*`
- `wmb-plus-beta7-asynctcp35-connection-churn-*`
- `wmb-plus-beta7-ble-sequence-*`
- `wmb-plus-beta7-runtime-tare-*`

## Validation focus

- Confirm the reference XIAO build still reports near-80 Hz HX711 cadence under
  dashboard/API activity.
- Confirm web polling does not create acquisition stalls.
- Compare DOUT-interrupt and non-DOUT builds before making DOUT interrupt the
  default path.
- Confirm Bean Conqueror Float32 and WMB/GaggiMate-compatible BLE paths still
  advertise and notify after the NimBLE migration.
- Confirm tare, runtime tare, and app-triggered tare remain physical-path
  compatible.
