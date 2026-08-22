# WMB+ 0.2.0-beta.10 release notes

Beta 10 replaces the withdrawn beta9 build. It keeps the beta9 OTA/PWA work,
but changes the default WMB packet lane to a cleaner 20 Hz stream so Crema,
GaggiMate, Gaggiuino-style, and WMB clients do not have to fall back to Float32
and lose supporting metadata.

## Feature set

- Created a private Wokwi lab repository for runnable simulator snapshots:
  <https://github.com/danielfcurrie-alt/wmb-plus-wokwi-lab>. The public
  firmware repo remains the beta release source of truth; the private lab is for
  custom chip models, focused scenarios, and simulator-only work before it is
  ready for release documentation.
- Added a dedicated Pour Over PWA with configurable pour, pause, agitation, and
  drawdown stages; target guidance; live weight and flow; previous-brew
  comparison; and guarded agitation controls.
- Moved the active Pour Over session clock and stage progression into firmware,
  exposing compact API/SSE state for synchronized browser and display clients.
  Saved recipe libraries and completed traces remain browser-local.
- Added a staged TinyS3[D] 172x320 color UI with the official WeighMyBru+ splash,
  weight/flow, flow-curve, web-app QR, status, and synchronized Pour Over views.
- Added standalone TinyS3[D]-only JD9853 display, AXS5106L touch, LIS2DW12, and
  MAX17048 drivers. The production TinyS3[D] runtime now instantiates them on
  the builder-confirmed pin table; XIAO and SuperMini retain their existing
  hardware paths.
- Added a pin-independent motion-analysis layer for vibration energy,
  orientation, impact peaks, quiet confidence, and tap candidates. Motion is
  diagnostic-only and does not alter weight or tare behavior.
- Expanded MAX17048 diagnostics and provisional WiFi-on/off runtime and charge
  projections, including time to 80% and 100%. These estimates still require
  physical calibration.
- Expanded Wokwi models and focused profiles for HX711 cadence, LIS2DW12
  identity/scaling/data-ready and motion patterns, MAX17048 direction/alerts/
  missing-device behavior, and pixel-rendered display/touch behavior.
  Compact daily gates combine related stimuli to reduce paid simulator time,
  and the runner can load the private local CLI token file automatically.

## WMB selectable output cadence

Beta 10 makes the WMB packet lane default to the clean `20 Hz` profile so Crema
and WMB+ clients can keep WMB metadata without consuming the unstable high-rate
diagnostic stream. The clean selector also supports explicit `10`, `20`, `40`,
and `80 Hz` targets. It is an output scheduler plus a bounded causal estimator
over real source samples; it does not invent measurements or imply that a 10 SPS
HX711 can produce true 20/40/80 Hz source data.

Rules for this work:

- Treat the selected WMB cadence as the requested BLE output cadence, not the
detected acquisition cadence.
- Keep byte 17 as detected acquisition rate unless a future versioned packet
  explicitly changes the contract.
- Expose the active WMB cadence/profile through web, USB diagnostics, and a
  readable status path. Capabilities should advertise supported profiles, not
  silently select one.
- If source rate is unknown or below the selected output rate, suppress
  duplicate selected-source samples. Once detected source rate is available,
  also cap the effective clean-output cadence to the detected source cadence.
- Preserve the high-rate diagnostic profile for captures and compatibility
  testing. Do not rename transport packet sequence into source-read sequence.
- Keep raw/qualified input, clean presentation weight, and control behavior
  separately observable.

## Flow cleanup

In the default clean WMB profile, the packet flow field derives from the same
selected samples as the packet weight. Diagnostic flag bit 6 is set only when
that flow estimate is valid for the current packet; stale, suspect, or
internally inconsistent windows clear the validity bit and publish `0.00 g/s`
for legacy byte compatibility. The diagnostic-high-rate profile preserves the
existing public flow estimator for compatibility testing. Dashboard diagnostics
expose the active WMB profile, requested/effective WMB cadence, source limiting,
source age/window metadata, and clean-flow validity.

## Board boundaries

- TinyS3[D]-specific peripheral code is compiled only for the TinyS3[D] target.
- XIAO and SuperMini do not include or initialize the accelerometer, fuel gauge,
  Waveshare display/touch, QR dependency, or packed color logo.
- The common firmware-owned Pour Over session and LittleFS PWA remain available
  across supported boards.

## Ongoing beta 10 work

- The builder-confirmed TinyS3[D] GPIO map is now active for HX711, shared I2C,
  JD9853, AXS5106L, and standalone sleep/tare inputs. The exact custom-board
  diagram mirrors it; the runnable DevKitC proxy documents its unavoidable
  GPIO34/GPIO33 carrier substitutions.

- Keep the private Wokwi lab synced with the current TinyS3[D] custom-board,
   LIS2DW12, MAX17048, display/touch, HX711, VCD, and scenario work.
- Continue validating the default clean WMB `20 Hz` profile and optional `10/40/80 Hz`
   targets against hardware BLE captures and Crema imports.
- Tune clean-flow validity and smoothing against real machine captures so WMB
   packets do not mix clean weight with misleading flow.
- Validate the builder-confirmed TinyS3[D] pin table on assembled hardware.
- Validate display orientation/touch mapping and MAX17048 behavior on hardware.
- Expose bounded motion diagnostics and tune them from real captures without
   changing the weight signal.
- Run short focused simulator scenarios, then physical HX711 cadence, RF,
   battery, and UI soak tests.
- Consider tap-to-tare, motion wake, and disturbance-aware filtering only
   after false-positive evidence is strong enough.

## Validation status

- Host tests cover the Pour Over session, LIS2DW12 driver, motion analyzer, and
  battery simulation matrix.
- XIAO firmware/LittleFS builds passed, release artifacts were generated, copied
  to the MacBook, SHA-256 verified, and installed on the reference scale through
  network OTA.
- SuperMini and TinyS3[D] firmware/LittleFS builds have passed during staging.
- The XIAO reference scale served the dashboard, app, Updates, StopMyBru, Pour
  Over, manifest, service worker, and offline page over direct IP after the
  beta10 LittleFS image was installed.
- The Updates page no longer crashes during GitHub self-update checks on the
  reference scale.
- The Pour Over PWA has received desktop/mobile browser QA and a XIAO LittleFS
  smoke test using the older firmware fallback path.
- The compact TinyS3[D] motion sequence passed in Wokwi on 2026-08-21: quiet,
  vibration, impact/knock, cup placement, and double tap were observed while
  HX711 held 91.96 Hz with no gaps over 100 ms, missing sequence, or drops.
- The compact MAX17048 discharge/charge/alert sequence and missing-at-boot gate
  passed with the same gap-free HX711 cadence.
- The DevKitC color integration runtime reached all display page checkpoints,
  touch page selection, LIS2DW12 identity/data-ready, MAX17048 discharge, and
  continuous HX711 acquisition. Wokwi's full-frame SPI emulation slows the
  debug USB packet lane enough to fail its cadence threshold, so that combined
  row remains a functional peripheral gate rather than release cadence proof.
- No physical TinyS3[D] peripheral validation has been completed yet.
- USB serial was not visible on the MacBook during the final XIAO RC check, so
  LAN OTA and BLE/web validation are the preferred beta10 update paths until
  that host/device enumeration issue is isolated.
