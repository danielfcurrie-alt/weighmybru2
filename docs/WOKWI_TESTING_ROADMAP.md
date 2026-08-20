# WMB+ Wokwi Testing Roadmap

This document tracks how WMB+ should use Wokwi as a firmware regression lab. Wokwi is not a replacement for the real XIAO reference scale, ScaleBench captures, or a physical logic analyzer. Its job is to catch deterministic firmware regressions before we burn human time flashing and retesting hardware.

## Current purpose

Use Wokwi for:

- ESP32-S3 boot/runtime smoke testing;
- custom HX711 behavior modeling;
- deterministic 10 SPS / 80 SPS acquisition scenarios;
- USB serial weight stream validation;
- scenario-driven tare/load/disconnect/glitch tests;
- early TinyS3[D] firmware-feature proxy testing with simulated MAX17048/LIS2DW12 pieces and
  a separately isolated JD9853/AXS5106L display/touch model.

The current TinyS3[D] firmware-feature proxy runs on Wokwi's XIAO ESP32-S3
board model. It is not a TinyS3[D] hardware or board-variant model. It has two
distinct coverage levels:

- MAX17048: current firmware probes and reads VCELL/SOC, so the baseline proves
  the fuel-gauge backend is selected and basic register decoding works.
- LIS2DW12: a standalone firmware driver and I2C model exist, but production
  firmware does not instantiate the driver. It is test-bed infrastructure, not
  passing accelerometer product coverage.

A second `tinys3d-devkitc-feature-proxy` runtime target uses Wokwi's DevKitC
Arduino variant with TinyS3[D] feature macros. This makes GPIO17/18 and the
GPIO35 USB-sense substitute observable while retaining working USB serial. It
still does not model the real TinyS3 variant, physical GPIO33, or GPIO38.

An initial real TinyS3[D] custom-board definition now lives under
`wokwi/boards/tinys3d/`. It uses the `um_tinys3` FQBN, the physical header
layout, 8 MB QIO flash, 8 MB QSPI PSRAM, native USB Serial/JTAG, and internal
virtual signals for GPIO33 VBUS sense, GPIO10 fuel-gauge interrupt, GPIO17/18
RGB control, and GPIO38 antenna switching. Its JSON and original SVG validate
offline. The current Wokwi CLI does not load unpublished custom boards, so this
target must first be checked with Wokwi's interactive custom-board loader and
then registered upstream before it can replace the proxies in the guarded CLI
matrix.

The builder's module is a JD9853 display with AXS5106L touch, not ST7789. Its
13-pin protocol model remains outside the active TinyS3[D] profile until the
real GPIO assignment is known. This keeps speculative wiring out while driver,
fuel-gauge, and accelerometer work proceeds.

## TinyS3[D] sensor-to-product roadmap

The TinyS3[D] accelerometer and fuel-gauge work should lead to useful scale
behavior, not exist only as extra telemetry. Product ideas worth evaluating
include deliberate double-tap tare, motion wake, machine-vibration detection,
multi-sensor shot timing, and better disturbance-aware clean output. Oscalla's
scale/display workflow is a useful product reference, while WMB+ should retain
its local-first, interoperable design: <https://oscalla.com/>.

### Phase 1: sensor foundation

- Integrate the staged LIS2DW12 discovery, identity validation, configuration,
  XYZ reading, and data-ready support; then add configurable motion/tap logic.
- Expose bounded accelerometer diagnostics through USB and the dashboard before
  allowing it to change scale behavior.
- Add MAX17048 register-level checks for VCELL, SOC, CRATE, QuickStart, alert
  thresholds, STATUS clearing, and charging/discharging direction.
- Keep raw weight, clean weight, and motion evidence separately observable so
  sensor fusion does not hide acquisition problems.

### Phase 2: deterministic Wokwi sensor scenarios

- Extend `chip-lis2dw12` with scenario controls for single tap, double tap,
  sustained vibration, cup placement, quiet periods, and `INT1` assertion.
- Add focused scenarios that verify initialization, interrupt clearing,
  threshold behavior, and continued HX711 cadence during motion events.
- Add MAX17048 scenarios for SOC/voltage changes, discharge and charge slopes,
  low-battery alerts, QuickStart, and recovery from a missing fuel gauge.
- Keep each scenario short and independently budgeted; do not run a full matrix
  while developing one sensor behavior.

### Phase 3: scale behaviors

- Use accelerometer energy as an explicit disturbance input to the Float32
  estimator instead of inferring all physical disturbance from weight alone.
- Implement double-tap tare only after false-positive testing against pump
  vibration, countertop knocks, cup placement, and active pouring.
- Prefer LIS2DW12 low-power interrupt wake over keeping the complete weighing
  pipeline active merely to detect a tap.
- Evaluate automatic shot start using agreement between vibration, positive
  weight slope, and sustained flow. Do not trigger from vibration alone.
- Keep saved equipment/container signatures deliberately constrained and
  user-confirmed if automatic container tare is explored.

### Phase 4: PWA and display workflows

- Add custom profiles for espresso, manual lever, pour-over, dosing, and other
  repeated workflows, including timer triggers and target ratios.
- Offer minimal weight/timer, detailed live-flow, and pour-stage views rather
  than forcing one dashboard density on every workflow.
- Add target-ratio and predicted-stop cues with configurable machine/human
  reaction compensation; do not present scale precision as stop precision.
- Store optional local brew history and complete flow traces in IndexedDB, with
  previous-shot overlays and local export/import.
- Evaluate X2 mode for split shots.
- Allow a phone, tablet, or inexpensive external display to act as a local Core
  Display equivalent without making the scale dependent on a cloud service.
- Consider optional pressure-transducer overlays only after the weight/timing
  workflow and protocol boundaries are stable.

### Guardrails

- Do not treat a vendor's aggregate internal sensor sampling claim as an HX711
  conversion-rate target.
- Predicted stop accuracy depends on machine shutoff latency, liquid already in
  flight, and human reaction time as well as scale quality.
- Wake by weight may need additional low-power analog hardware; wake by
  accelerometer interrupt is the realistic first TinyS3[D] path.
- Wokwi can validate state machines, register handling, and deterministic
  scenarios, but physical vibration coupling, false taps, RF coexistence,
  battery drain, and wake current still require real TinyS3[D] hardware.

## Simulation budget guard

`tools/run-wokwi-matrix.sh` is list-only with no arguments. Every real run must
name profiles (or explicitly pass `--all`) and provide `--budget-seconds`. The
runner sums the selected timeout caps and refuses plans larger than the budget.
Use `--dry-run` to inspect a selection without building. Rows marked
`lintOnly:true` can be built and linted with `--budget-seconds 0`; they never
start the simulator. Results are isolated under `.pio/wokwi/runs/<profile>/`.

Do not use Wokwi as final evidence for:

- real GPIO timing;
- real HX711 SCK pulse width;
- RF/BLE coexistence;
- battery charging behavior;
- actual load-cell mechanics;
- final 80 SPS performance claims.

## Already present

- Custom HX711 chip model under `wokwi/`.
- `actualSps` control so 80 SPS mode can simulate the real reference unit’s roughly 90–95 Hz behavior.
- Delayed load support so boot tare can start at 0 g and later place a simulated object.
- Jitter, glitch, and missed-ready controls.
- Wokwi scenarios for clean stream, midstream tare, and missed-ready recovery.
- TinyS3[D] feature-proxy pieces for fuel gauge and accelerometer integration;
  JD9853/AXS5106L wiring is kept out of the active profile pending verified pins.
- Runtime serial analyzer output under `.pio/wokwi/runs/<profile>/analysis.json`.

## Current important red test

The midstream-tare Wokwi scenario currently reproduces the public-stream tare gap class seen on the real scale.

Example:

```bash
cd /Users/dan/FrankenBru-workspaces/firmware/weighmybru2-pr1-existing-device-quality
tools/run-wokwi-matrix.sh --budget-seconds 30 midstream-tare-loaded
```

Expected current behavior until fixed:

- simulated load appears around 5 s;
- serial tare is queued midstream;
- weight returns to zero;
- test fails if tare creates a large public-stream gap or sequence discontinuity.

Once the firmware makes tare non-blocking at the public stream layer, this should become green.

## TBD: logic analyzer and VCD export

Add a Wokwi logic analyzer to focused diagrams.

Channels:

- HX711 `DOUT`;
- HX711 `PD_SCK`;
- optional firmware `sample_strobe` GPIO when a raw sample is read;
- optional firmware `publish_strobe` GPIO when a public sample is emitted.

Use VCD export for acquisition validation and failure postmortems:

```bash
wokwi-cli . --vcd-file .pio/wokwi/hx711-80sps.vcd
```

Validate in VCD:

- DOUT ready cadence;
- 25 SCK pulses per HX711 read;
- no simulated interrupt storm;
- no extra SCK high stretch in the model;
- raw-sample strobe remains gap-free when the public stream stalls;
- publish-strobe gaps align with analyzer failures.

Caution: Wokwi VCD proves simulated behavior only. Real PD_SCK high-time and electrical timing still need a physical logic analyzer on XIAO/TinyS3[D].

## TBD: scenario-driven hardware-in-the-loop style tests

Add YAML scenarios that drive controls and serial commands:

- boot and expect HX711 ready;
- delayed load appears;
- midstream tare returns to zero without stream gap;
- missed-ready recovery does not deadlock;
- forced DOUT-high disconnect reports bounded timeout/fault;
- reconnect recovers without reboot where practical;
- glitch injection is flagged/rejected without breaking cadence;
- TinyS3[D] fuel gauge path reports fuel-gauge battery rather than ADC fallback.

Use Wokwi scenario steps such as:

- `delay`;
- `write-serial`;
- `wait-serial`;
- `set-control`;
- `take-screenshot`.

## TBD: custom HX711 controls

Expose scenario-friendly controls in the custom HX711 chip:

- `loadGrams`;
- `noiseLsb`;
- `forceDisconnect` as 0/1;
- `actualSps`;
- `modeSps` or existing `sps`;
- `missReadyEvery`;
- `oneShotMissReady`;
- `glitchEvery`;
- `glitchCounts`;
- `busyWindowMs` or equivalent synthetic stall/fault input if useful.

Prefer controls that scenarios can change without creating many nearly identical diagrams.

## TBD: serial expect / fail gates

Add cheap always-on CI-style gates:

```bash
wokwi-cli . \
  --expect-text "HX711" \
  --expect-text "80SPS" \
  --fail-text "HX711 timeout" \
  --fail-text "Guru Meditation" \
  --timeout 20000
```

Use these for fast PR checks. Use full analyzer scenarios for deeper checks.

## TBD: screenshots and display regression

For OLED/JD9853 display work, add screenshot tests only after the acquisition path is stable.

Potential checks:

- boot screen renders;
- weight screen updates after load;
- tare message appears;
- post-tare weight returns near zero;
- low battery / charging state renders.

Do not let display screenshots become a blocker for acquisition hardening.

## TBD: GDB debugging

Configure Wokwi GDB when chasing task/ISR bugs:

```toml
[wokwi.gdb]
gdbServerPort = 3333
```

Useful breakpoint areas:

- `Hx711Io::readRaw`;
- DOUT ISR doorbell;
- acquisition task loop;
- raw-to-processed fan-out;
- tare application boundary.

## TBD: WiFi/web simulation

Use Wokwi WiFi/web testing carefully:

- dashboard/API polling should not starve simulated acquisition;
- OTA/web smoke can verify route shape and boot behavior;
- Wokwi does not certify real RF coexistence.

Prefer static/manual-refresh dashboard behavior during acquisition tests so the web UI does not become the thing being measured.

## TBD: MCP / agent integration

Evaluate `wokwi-cli mcp` later if it lets Codex run simulations, read serial, poke controls, and summarize failures with less shell glue.

This is optional. The command-line runner already covers the critical path.

## TBD: VCD artifacts on failure

Update the Wokwi runner so focused tests can optionally export VCD traces only on failure or only for named acquisition tests. Avoid generating large VCD files for every routine run.

Suggested outputs:

```text
.pio/wokwi/runs/<profile>/serial.log
.pio/wokwi/runs/<profile>/wokwi-cli.log
.pio/wokwi/runs/<profile>/analysis.json
.pio/wokwi/runs/<profile>/logic.vcd
```

## Release posture

Before calling a Beta 7 acquisition build ready:

1. Native host tests pass.
2. Wokwi clean stream passes.
3. Wokwi missed-ready recovery passes or produces expected bounded fault.
4. Wokwi midstream tare gap test is green, or the remaining red behavior is explicitly accepted.
5. Real XIAO runtime cadence smoke passes.
6. Real ScaleBench capture confirms no unexplained long gaps.
7. Physical logic analyzer validates the real SCK/DOUT timing if we make hard timing claims.
