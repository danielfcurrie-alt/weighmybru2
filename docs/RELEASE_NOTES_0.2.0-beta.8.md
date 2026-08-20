# WMB+ 0.2.0-beta.8 internal notes

Beta 8 was an internal XIAO field-test build focused on WiFi/PWA behavior,
StopMyBru access, and resettable bump diagnostics. It was not published as a
public GitHub Release.

The main beta8 test artifact was:

- `wmb-plus-beta8-wifi-pwa-bump-reset-xiao`

## Main changes

- Carried forward beta7 acquisition, BLE, web-cadence, and runtime tare
  hardening.
- Added a field-test XIAO app and LittleFS bundle for validating WiFi, PWA, and
  StopMyBru behavior on the physical scale.
- Improved PWA entry points so the StopMyBru app could be launched from the
  StopMyBru page instead of being treated as a generic dashboard shortcut.
- Preserved the lower-churn dashboard posture from beta7 while continuing to
  expose deeper diagnostics through the full web UI.
- Added or exposed reset behavior for bump/diagnostic counters so noisy-machine
  testing could begin from a clean diagnostic baseline.
- Kept WiFi credentials and calibration in NVS across app/LittleFS flashing.

## What beta8 taught us

- WiFi/PWA behavior was good enough to move toward a polished beta9 web bundle,
  but installed PWA and service-worker behavior needed more explicit offline
  handling.
- ScaleBench captures showed the WMB+ high-rate stream was useful for
  diagnostics, but the Bean Conqueror Float32 lane still needed a better
  compatibility estimator.
- Noisy machine captures showed large implausible motion, reverse pour steps,
  and vibration/interference coupling that firmware should expose honestly
  rather than silently hiding in the high-rate diagnostic path.
- The Float32 sticky hold/release behavior was not acceptable for compatibility
  clients, especially when the timer was stopped and noisy samples appeared.

## Release posture

Beta 8 was a local/tester checkpoint used to generate the data that shaped
beta9. It should not be treated as the recommended public install target.

The next public release target became beta9, with these priorities:

- Fix Float32 as a clean 20 Hz compatibility lane.
- Add better source/Float32 diagnostics for replay and ScaleBench comparison.
- Tighten PWA install/offline behavior and reduce LittleFS payload size.
- Add GitHub app self-update from the scale while preserving manual OTA.
- Keep the high-rate WMB+ path diagnostic and evidence-preserving.
