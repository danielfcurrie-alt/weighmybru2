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
  a separately isolated pixel-rendering JD9853/AXS5106L display/touch model.

The current TinyS3[D] firmware-feature proxy runs on Wokwi's XIAO ESP32-S3
board model. It is not a TinyS3[D] hardware or board-variant model. It has two
distinct coverage levels:

- MAX17048: firmware reads VCELL/SOC plus VERSION, CRATE, STATUS, CONFIG, and
  voltage-alert thresholds. The model also latches SOC/voltage alerts and
  drives ALRT, so focused scenarios can cover more than basic backend selection.
- LIS2DW12: the standalone firmware driver and I2C model are instantiated only
  by the TinyS3[D] Wokwi harness. Focused profiles exercise identity/reset,
  scaling, data-ready, and diagnostic motion analysis. Production firmware
  remains gated on verified physical pins.

A second `tinys3d-devkitc-feature-proxy` runtime target uses Wokwi's DevKitC
Arduino variant with TinyS3[D] feature macros. This makes GPIO17/18 and the
GPIO35 USB-sense substitute observable. A 2026-08-20 baseline produced native
USB Serial/JTAG output, but the expanded display/touch integration profile did
not reach application serial on 2026-08-21 and remains a red runtime gate. It
still does not model the real TinyS3 variant, physical GPIO33, or GPIO38.

A bounded 2026-08-21 boot A/B used 24 simulator seconds to narrow that red
gate. The no-color build, first with the complete custom-chip diagram and then
with the Waveshare display/touch chip removed, reached the same ROM entry point
but emitted no application serial in either 12-second run. Color UI
construction/rendering and the Waveshare custom chip are therefore not the
cause. The next useful discriminator is the newer Tiny peripheral harness (or
another source/configuration change since the known-good 2026-08-20 DevKitC
baseline); do not rerun the color/no-color or Waveshare/no-Waveshare pairs.

The follow-up `tinys3d-devkitc-boot-no-peripherals` control compiled the entire
Tiny peripheral coordinator out of the linked image while retaining the same
DevKitC runtime, HX711 path, diagram, and native USB settings. It also reached
only `entry 0x403c98ac` with zero application serial in a bounded 12-second
run. The coordinator and its static construction are therefore ruled out. The
next discriminator used a minimal application rather than another peripheral
permutation.

The six-second `tinys3d-devkitc-serial-sentinel` proved that the same virtual
DevKitC flash/QSPI-PSRAM configuration reaches Arduino `setup()` and `loop()`.
Explicitly wiring `esp:TX` to `$serialMonitor:RX` restored one-way UART0
capture without driving RX/GPIO44, which is the builder-confirmed tare input.
The bounded probe delivered 300 analyzer-compatible rows at 100 Hz with no
missing sequence or gaps over 100 ms. The full DevKitC proxy can therefore use
normal application serial output; serial commands remain intentionally
unavailable on this carrier because its RX pin belongs to tare.

The first 15-second full-peripheral run on that route also proved application
boot and peripheral discovery with the builder pin map. MAX17048, LIS2DW12,
HX711, touch, and Waveshare framebuffer checkpoints all reported success, and
the source telemetry measured 91.94 Hz with zero source-to-public acquisition
loss. The strict public-stream gate remains red because the runtime harness
simultaneously emits weight, per-acquisition source, Float32, motion, battery,
and display diagnostics over one 115200-baud UART. That is a harness bandwidth
limit, not a boot or raw-acquisition failure; keep the gate red until the
diagnostic lanes are bounded or separated.

The exact-pin DevKitC integration environment now separates those concerns by
setting `WMBP_WOKWI_SOURCE_STREAM=0`. Its normal weight lane and bounded Tiny
peripheral checkpoints remain active, while source-focused profiles retain the
high-volume diagnostic rows.

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
13-pin model renders RGB565 writes into a 172x320 Wokwi framebuffer, and the
DevKitC feature proxy now drives weight, flow-curve, QR, status, and Pour Over
screens plus touch page selection. Its GPIOs are explicitly simulator-only;
physical TinyS3[D] integration still waits for the verified pin table.

The detailed 2026-08-21 builder drawing is transcribed in
`docs/TINYS3D_PERIPHERAL_DRIVERS.md`, and its HX711, shared-I2C, JD9853,
AXS5106L, and standalone-touch pins are now active in the production
`BOARD_TINYS3D` configuration. The exact custom-board diagram uses the same
map. The runnable DevKitC proxy matches every exposed external signal, with two
explicit carrier substitutions: LCD CS uses GPIO39 because DevKitC does not
expose physical GPIO34, and USB sense uses GPIO47 because it does not expose
Tiny's internal GPIO33. The builder confirmed the left standalone touch input
as sleep on GPIO37 and the right input as tare on RX/GPIO44.
The HX711 SPDT rate switch is also confirmed: 3V3 selects 80 SPS and GND
selects 10 SPS. The builder also confirmed that JD9853 MISO is intentionally
unconnected because the display path is write-only, and LIS2DW12 interrupt pins
are intentionally unused in I2C polling mode. The HX711 custom chip now also
models the RATE input, with the SPDT defaulting to the 3V3/80 SPS side. Physical
display activation still waits on active-level verification and bounded
hardware validation.

## TinyS3[D] sensor-to-product roadmap

This is the beta 10 development roadmap. Beta 9 remains the published release;
none of the staged TinyS3[D] peripheral work should be represented as beta 9
product coverage.

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

Implemented as focused matrix rows plus compact daily gates. The 35-second
`tinys3d-motion-sequence` covers quiet, vibration, knock, cup placement, and
double tap in one boot. The 25-second `tinys3d-max17048-sequence` covers signed
discharge/charge rates and alert behavior; the 15-second
`tinys3d-max17048-missing` keeps missing-at-boot separate. Display/touch and
I2C/SPI cadence are combined in the 40-second `tinys3d-devkitc-integration`
gate. Definitions and offline lint/build checks do not consume simulator
minutes; runtime remains explicitly budgeted.

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
The runtime runner automatically loads the local credential from
`~/.wokwi/cli-token.env` when `WOKWI_CLI_TOKEN` is unset; it never prints the
credential value. A separate 45-second wall-time startup watchdog stops a run
that produces ROM output but no WMB+ application marker, limiting the cost of
boot-stalled profiles.

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
- TinyS3[D] feature-proxy pieces for fuel gauge and accelerometer integration,
  plus a pixel-rendering JD9853/AXS5106L model and staged color UI on the
  simulator-only DevKitC integration profile. Physical enablement still waits
  for verified pins.
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
