# WMB+ release checklist

Use this checklist before creating any public WMB+ beta tag.

## Scope check

- Confirm the release target version is set in `include/Version.h`.
- Confirm release notes exist for the target version.
- Confirm README, flashing guide, and beta overview point at the target version.
- Confirm no private planning notes, local machine paths, or unrelated app references are included.

## Build validation

- Run `git diff --check`.
- Run `./tools/run-host-tests.sh`.
- Build XIAO app firmware: `pio run -e esp32s3-xiao`.
- Build XIAO LittleFS: `pio run -e esp32s3-xiao -t buildfs`.
- Build SuperMini app firmware: `pio run -e esp32s3-supermini`.
- Build SuperMini LittleFS: `pio run -e esp32s3-supermini -t buildfs`.
- Build TinyS3[D] app firmware: `pio run -e esp32s3-tinys3d`.
- Build TinyS3[D] LittleFS: `pio run -e esp32s3-tinys3d -t buildfs`.
- Build local release assets without publishing: `./tools/build-release-assets.sh <version>`.
- Confirm the local asset builder verifies factory-full image sizes and LittleFS offsets before any GitHub release is created.

## Release-blocking OTA smoke test

This is mandatory for every public beta.

Use the primary XIAO reference unit and start from the previous published beta with the dual-OTA partition layout already installed.

- Upload the candidate `xiao-app.bin` through the local Updates page.
- Confirm upload completes, the scale restarts, and the version/boot log reports the candidate version.
- Confirm the active OTA app partition changed and the next OTA partition remains available.
- Upload the candidate `xiao-littlefs.bin` through the local Updates page.
- Confirm upload completes and the web UI remains available after restart/reload.
- Confirm the Updates page shows candidate UI text/assets.
- Confirm calibration, WiFi settings, NVS-backed configuration, and learned battery/StopMyBru state survive.

Do not replace this with USB flashing, release asset size checks, partition-offset checks, or `factory-full.bin` validation. This gate exists specifically to exercise the real browser multipart upload path.

## Release-blocking StopMyBru webhook smoke test

This validates the firmware HTTP client path without requiring a real relay.

- Start a simple local HTTP receiver on a computer on the same LAN as the scale, for example `python3 -m http.server 8080`.
- Connect the scale to that LAN WiFi.
- Open the StopMyBru page.
- Enable HTTP webhook relay with custom local `http://` URLs such as `http://<computer-ip>:8080/wmbplus/on` and `http://<computer-ip>:8080/wmbplus/off`.
- Save the webhook settings.
- Press Test ON and confirm the receiver logs the request and the page reports HTTP `2xx` or `3xx`.
- Press Test OFF and confirm the receiver logs the request and the page reports HTTP `2xx` or `3xx`.
- Confirm `/api/smb/status` reports the last webhook HTTP code and message.

This does not replace real Tasmota/Shelly validation for automatic target cutoff. It only proves the release still fires configured HTTP webhook URLs.

## Release asset verification

After GitHub Actions publishes the release:

- Confirm the release is a prerelease, not a draft.
- Confirm XIAO `factory-full.bin` is 8,388,608 bytes.
- Confirm TinyS3[D] `factory-full.bin` is 8,388,608 bytes.
- Confirm SuperMini `factory-full.bin` is 4,194,304 bytes.
- Confirm `manifest-xiao.json` has `new_install_prompt_erase: false` and LittleFS offset `0x610000`.
- Confirm `manifest-tinys3d.json` has `new_install_prompt_erase: false` and LittleFS offset `0x610000`.
- Confirm `manifest-supermini.json` has `new_install_prompt_erase: false` and LittleFS offset `0x310000`.
- Confirm the published SHA-256 file is present.

## Communication

- State clearly whether OTA means manual browser-upload OTA or GitHub self-update.
- State that first install / partition migration uses `factory-full.bin` over USB at `0x0`.
- State that later app updates use `-app.bin` from the local Updates page.
- State that later web UI updates use `-littlefs.bin` from the local Updates page.
- State that normal beta updates should not use `erase_flash`.
