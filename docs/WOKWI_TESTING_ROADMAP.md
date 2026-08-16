# WMB+ Wokwi Testing Roadmap

This document tracks how WMB+ should use Wokwi as a firmware regression lab. Wokwi is not a replacement for the real XIAO reference scale, ScaleBench captures, or a physical logic analyzer. Its job is to catch deterministic firmware regressions before we burn human time flashing and retesting hardware.

## Current purpose

Use Wokwi for:

- ESP32-S3 boot/runtime smoke testing;
- custom HX711 behavior modeling;
- deterministic 10 SPS / 80 SPS acquisition scenarios;
- USB serial weight stream validation;
- scenario-driven tare/load/disconnect/glitch tests;
- early TinyS3[D] proxy testing with simulated MAX17048/LIS2DW12/ST7789 pieces.

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
- TinyS3[D] proxy pieces for fuel gauge, accelerometer, and ST7789 placeholder wiring.
- Runtime serial analyzer output under `.pio/wokwi/<env>/analysis.json`.

## Current important red test

The midstream-tare Wokwi scenario currently reproduces the public-stream tare gap class seen on the real scale.

Example:

```bash
cd /Users/admin/Developer/weighmybru2-pr1-existing-device-quality
WMBP_WOKWI_TIMEOUT_MS=45000 \
WMBP_WOKWI_DIAGRAM=diagram.xiao-94hz-loaded.json \
WMBP_WOKWI_SCENARIO=wokwi/usb-weight-stream-midstream-tare.scenario.yaml \
tools/run-wokwi-runtime-smoke.sh esp32s3-xiao-wokwi-hx711-dout-interrupt
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

For OLED/ST7789 display work, add screenshot tests only after the acquisition path is stable.

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
.pio/wokwi/<env>/serial.log
.pio/wokwi/<env>/wokwi-cli.log
.pio/wokwi/<env>/analysis.json
.pio/wokwi/<env>/hx711-80sps.vcd
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
