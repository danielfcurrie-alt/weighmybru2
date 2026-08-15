# WMB+ 0.2.0-beta.6 internal notes

Beta 6 is an internal hardening checkpoint. It is intentionally not a public GitHub Release; the next planned public GitHub Release target is beta 8.

## Main changes

- Hardened web/API scale mutation paths so tare, calibration, and filter updates are queued onto the firmware loop path instead of directly touching scale state from AsyncTCP callbacks.
- Removed direct web reads from HX711 where cached acquisition data is sufficient.
- Routed BLE/web tare through the same physical-parity tare path.
- Validated calibration factors before saving them to NVS.
- Prevented saved WiFi passwords from being returned by the web API.
- Fixed the post-tare flow spike by clearing flow state when flow calculation resumes.
- Increased OLED I2C speed to reduce blocking display update time.
- Removed the unused tracked Tailwind Windows executable.
- Added WMB+ web logo assets and WiFi password recovery guidance.
- Added local beta6 regression and release-asset packaging helpers.

## Release posture

- Do not create a GitHub Release for beta 6.
- Do not update the public README's "Current beta release" section to beta 6.
- Use beta 6 locally to validate hardening, OTA upload behavior, full factory image size/offset correctness, and web UI asset packaging.
- Target beta 8 as the next public GitHub Release after beta 7 feature work and a beta 8 hardening pass.

## Required validation before beta 8

- Full regression script.
- Local release-asset build for XIAO, SuperMini, and TinyS3[D].
- Browser-upload OTA smoke test on the XIAO reference unit.
- LittleFS upload smoke test on the XIAO reference unit.
- StopMyBru webhook smoke test against a local HTTP receiver.
