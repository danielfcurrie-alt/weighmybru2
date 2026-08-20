# WMB+ 0.2.0-beta.9 release notes

Beta 9 is focused on cleaner compatibility output, self-update readiness, and a smaller web UI footprint.

## What changed from beta 8

- Reworked the Bean Conqueror Float32 compatibility path into a clean 20 Hz stream derived from plausibility-qualified source samples.
- Removed the unbounded Float32 freeze/release behavior seen on noisy machine captures.
- Added Float32/source diagnostics so stale input, source age, selected sample age, window range, suspect selection, and sequence gaps are inspectable from USB/web tooling.
- Added GitHub app self-update support from the scale while it is connected to home WiFi: check latest release, download the matching board app image to the inactive OTA slot, show a pending install, then reboot only when the user taps Install.
- Kept manual browser-upload OTA for app firmware and LittleFS web UI images as the fallback path.
- Tightened the PWA/web UI payload with smaller icons/assets and a simpler Basic/Extended view model.
- Fixed installed PWA/live-dashboard stale-state handling so a sleeping or unreachable scale is marked offline/stale instead of showing a stale green Connected state.
- Reclaimed SuperMini app space by moving the SuperMini LittleFS partition to `0x350000`.
- Expanded Wokwi hardware models for TinyS3[D] validation work, including MAX17048 and LIS2DW12 behavior.

## OTA status

Beta 9 supports two OTA paths after the matching dual-OTA factory layout is installed.

- **GitHub app self-update:** from the Updates page, the scale can check WMB+ GitHub releases over home WiFi, stage the matching `-app.bin` into the inactive app slot, and report that the update is pending install.
- **Manual browser-upload OTA:** testers can still upload the matching `-app.bin` or `-littlefs.bin` from the local Updates page.
- **LittleFS/web UI updates:** still use manual upload or a factory-full image. GitHub self-update currently stages app firmware, not LittleFS.
- **First install or partition migration:** still uses the matching `factory-full.bin` over USB at `0x0`.

Do not use `erase_flash` for normal beta updates. NVS stores calibration, WiFi credentials, and learned settings.

## Final beta9 release validation

- Published release tag `v0.2.0-beta.9` points at `d1c1e13`.
- All XIAO, SuperMini, and TinyS3[D] release assets were rebuilt from a clean `d1c1e13` worktree.
- The published release is a GitHub prerelease, not a draft.
- `wmb-plus-0.2.0-beta.9-sha256.txt` validates the local and MacBook release artifact folders.
- Manifests keep `new_install_prompt_erase: false` and the expected board-specific LittleFS offsets: XIAO/TinyS3[D] at `0x610000`, SuperMini at `0x350000`.
- Factory-full image sizes were verified: XIAO and TinyS3[D] are 8,388,608 bytes; SuperMini is 4,194,304 bytes.
- XIAO reference hardware was flashed and exercised with beta9 firmware plus the final web UI bundle.
- Quiet countertop and noisy machine pours were captured with ScaleBench after beta9 Float32 changes.
- OTA refresh/download/pending-install behavior was fixed and smoke-tested on the reference unit.
- PWA sleep/offline behavior was fixed and tested on the reference unit.

## Upgrade path

For devices already on a WMB+ dual-OTA layout, use the local Updates page with the matching board `wmb-plus-0.2.0-beta.9-*-app.bin`, then update LittleFS with the matching `-littlefs.bin` if you want the beta9 web UI.

For first-time installs, unknown partition layouts, or migration from beta1-era assets, flash the matching `factory-full.bin` at `0x0` over USB.
