# ScaleBench signal architecture proposal for beta9

Date: 2026-08-17

This note summarizes the ScaleBench evidence from beta8 testing and proposes a beta9 firmware plan. It intentionally separates two BLE contracts that should not share one generic smoothing path:

1. Clean 20 Hz Float32 compatibility output.
2. WMB+ extended stream policy: diagnostic high-rate vs selectable clean 20 Hz.

Hardware testing validates both firmware tracks, but it is not itself the firmware architecture.

## Summary

The WMB+ beta8 BLE transport is healthy in the tested captures: missing sequence count, long gap count, disconnect count, parse failures, and out-of-order frames were all zero. The main problem is not BLE delivery. The problem is measurement instability coupled into the reported weight stream.

The current evidence supports this wording: machine-coupled vibration or electrical interference is present. The WMB+ unit showed a pre-pour range of about `0.14 g` on the counter versus about `5.22 g` on the machine, derived from 10 Hz median-normalized pre-pour windows so the figures remain reproducible. The Solo Barista capture is useful as a black-box presentation-quality benchmark on the same machine, but it does not prove that the physical input to the Solo load cell was quiet. The WMB+ build being very lightweight remains a leading mechanical hypothesis, alongside position-dependent cable strain, load-path effects, wiring, grounding, load-cell mounting, and HX711 noise.

Firmware still needs to behave better when this happens. Beta9 should first deliver a robust clean 20 Hz compatibility output and honest high-rate diagnostics, then use the same clean estimator as an optional WMB+ extended profile.

## Evidence

### Captures reviewed

| Capture | Scenario | Notes |
| --- | --- | --- |
| `ScaleBench-WeighMyBru+-2026-08-17T09-01-45Z.json.gz` | WMB+ counter/faucet pour | Best WMB+ run in this set |
| `ScaleBench-WeighMyBru+-2026-08-17T09-02-53Z.json.gz` | WMB+ machine pour | Worse WMB+ machine-vibration run |
| `ScaleBench-LSJ-001-2026-08-17T09-09-30Z.json.gz` | Solo Barista machine pour | Commercial comparison scale |
| `ScaleBench-WeighMyBru+-2026-08-17T07-30-33Z.json.gz` | Earlier WMB+ machine pour | Initial noisy shot that started this review |

### WMB+ counter vs WMB+ machine

| Metric | WMB+ counter/faucet | WMB+ machine |
| --- | ---: | ---: |
| Overall score | 80 | 59 |
| Implausible frames | 26 | 316 |
| Usable frames | 2017 | 1144 |
| Negative sample steps | 26.8% | 33.5% |
| Drops <= -1 g | 160 | 268 |
| Drops <= -2 g | 70 | 167 |
| Drops <= -5 g | 30 | 47 |
| Worst single-sample delta | 16.02 g | 18.52 g |
| Worst 1-second weight range | 16.02 g | 24.56 g |
| Missing sequence count | 0 | 0 |
| Long gap count | 0 | 0 |
| Disconnect count | 0 | 0 |

The machine pour is materially worse. The counter/faucet pour is better, but it is not a clean hardware-health test because water impact and slosh can create real dynamic force. It still had multi-gram jumps and many bump-window frames.

### WMB+ machine vs Solo Barista machine

| Metric | WMB+ machine | Solo Barista machine |
| --- | ---: | ---: |
| Overall score | 59 | 44 |
| Implausible frames | 316 | 0 |
| Negative sample steps | 33.5% | 0.7% |
| Drops <= -1 g | 268 | 0 |
| Drops <= -2 g | 167 | 0 |
| Drops <= -5 g | 47 | 0 |
| Worst single-sample delta | 18.52 g | 0.40 g |
| Worst 1-second weight range | 24.56 g | 2.10 g |
| Effective sample rate | 53.5 Hz | 9.6 Hz |

The Solo Barista score is lower because the protocol is simpler and slower: no device clock, no sequence, no checksum, and about 10 Hz. Its presented weight signal is still much cleaner. Treat this as a compatibility and presentation-quality comparison, not a causal hardware argument.

### Earlier noisy WMB+ capture

The earlier WMB+ capture had only `firmwareBumpCount = 2`, but it still had severe oscillation:

| Metric | Earlier WMB+ machine pour |
| --- | ---: |
| Overall score | 46 |
| Implausible frames | 1806 |
| Usable frames | 2647 |
| Negative sample steps | 39.4% |
| Drops <= -1 g | 895 |
| Drops <= -2 g | 528 |
| Drops <= -5 g | 7 |
| Worst single-sample delta | 8.13 g |

`firmwareBumpCount` in ScaleBench counts sticky bump episodes, not individual firmware bump events. It can therefore understate a noisy run. The more useful signature is frequent negative motion during an increasing pour plus large alternating positive/negative steps.

### Caveat on firmware quality average

Do not use `firmwareQualityAverage` as a primary comparison metric for the WMB+ beta8 recordings. The WMB+ captures were collected during one continuous device boot and inherited earlier diagnostic state. It is useful as context, but the beta9 decision should rely more on packet health, implausible-frame count, delta distribution, and active-pour reversals.

### Packet delivery vs source-to-public yield

Do not call source-to-public loss "delivery"; that collides with BLE transport delivery.

The reviewed captures support these separate statements:

- BLE delivery of emitted packets was apparently complete: no missing BLE packet sequence, long-gap, parse-failure, out-of-order, or disconnect evidence appeared in the exports.
- Inferred nominal-slot-to-public-packet yield was about `79.1%` for the WMB+ machine capture and `96.0%` for the WMB+ counter capture.
- The exact reason for every omitted nominal slot is not directly observable in these ScaleBench exports.

The firmware already creates a source sequence for each successful raw read in `Hx711Acquisition`. The current 20-byte BLE byte 14 deliberately counts transmitted BLE packets. Beta9 should expose both concepts rather than replacing one with the other:

- `packetSequence`: transport sequence for emitted BLE packets.
- `rawReadSequence`: successful raw-read/source sequence for acquisition-to-public accounting.
- Explicit counters for accepted samples, plausibility rejections, queue drops, read errors, and timeouts.

Changing byte 14 to mean source sequence would require a versioned packet/profile capability. Otherwise one ambiguous sequence would conflate transport loss and acquisition/public-sample loss.

### Derived vs operator-confirmed evidence

Some capture labels and intermediate states are not encoded in the ScaleBench JSON:

- Scenario labels such as counter, machine, pump state, and "nothing on the platform" are operator-confirmed unless an external log records them.
- Missing nominal slots are inferred from timestamp multiples; the exports do not directly encode the reason each slot was omitted.
- Public-stream occupancy is measured from an already gated public stream, not raw acquisition noise.
- The counter capture proves the unit is capable of quiet operation in at least one placement. It does not eliminate position-dependent cable strain, mounting effects, or load-path effects.

### HX711 rate and oscillator note

Any oscillator/rate conclusion should remain "strongly favored pending logic-analyzer confirmation." The [HX711 datasheet](https://cdn.sparkfun.com/datasheets/Sensors/ForceFlex/hx711_english.pdf) supports the expected 10 SPS / 80 SPS modes and shows the external-clock divisors that make an 11.0592 MHz clock produce nominal 10 SPS or 80 SPS. That supports the firmware-rate hypothesis, but a logic-analyzer capture is still the proof for the specific hardware build.

## Existing Float32 problem

The Float32 compatibility lane already transmits at a 20 Hz cadence, but it currently snapshots the latest weight instead of combining the roughly 3 to 4 source samples available before each notification. In the current source this behavior is in `BluetoothScale::updateFloat32CompatibilityStream()` and `BluetoothScale::getFloat32CompatibilityWeight()`.

The pre-shot bump/glitch guard also holds the previous Float32 value while the device timer is stopped. In the WMB+ machine capture, the Float32 lane showed the resulting freeze-and-release failure:

- Approximate active pour window: about 260 Float32 notifications.
- Distinct values in that window: only a few dozen, depending on the selected active-window boundary.
- Confirmed artifact: one notification jumped from `18.24 g` to `75.39 g`.
- Jump size: `+57.15 g`.

This is not acceptable as a compatibility output. The capture proves a long hold and release, but it does not prove whether each held Float32 notification was caused by the recent-glitch guard, the recent-bump guard, or both. ScaleBench's bump flag alone is not enough to attribute the trigger.

Distinct-value ratio and total held time are supporting evidence only. They can reward noisy output and penalize legitimate steady output. Use them only inside windows where an offline reference is demonstrably moving.

Better Float32 metrics:

- Maximum hold while the offline reference advances by more than a stated threshold.
- Tracking error versus an offline reference.
- Effective delay.
- Tare and cup-step settling time.
- Reverse movement per second after standardizing output to 20 Hz.

The fix is not a downward-only guard: accepting `51.90 g -> 70.42 g` and then suppressing the correction back to `53.62 g` would leave the presented value falsely high.

Offline filter exploration must be evaluated at the actual 20 Hz Float32 output cadence, not after every high-rate input. When sampled every 50 ms across all 50 possible phases, fixed trailing medians performed as follows:

| Capture/window | p95 step | Max step | Passing phases |
| --- | ---: | ---: | ---: |
| Machine, 800 ms | 1.09 g to 1.19 g | 2.22 g to 3.45 g | 0/50 |
| Machine, 1400 ms | 0.76 g to 0.87 g | 1.43 g to 1.61 g | 50/50 |
| Counter, 600 ms | 0.46 g to 0.52 g | 1.34 g to 2.11 g | 32/50 |
| Counter, 800 ms | 0.42 g to 0.48 g | 1.19 g to 1.90 g | 50/50 |

This makes a fixed trailing median even less viable under a strict latency budget. It disproves that specific filter family under the proposed budget; it does not prove that every causal adaptive or two-path estimator must fail. A median or trimmed estimate followed by a modest low-pass stage may still be useful, but beta9 should define the latency and tracking budget before choosing filter constants.

## Shared internal weight stages

Use precise stage names so the two BLE contracts do not collapse into one ambiguous presentation path:

- `calibratedRawWeight`: HX711-derived weight after calibration and tare, before plausibility filtering.
- `qualifiedWeight`: calibrated sample accepted by the plausibility gate and sample-quality logic.
- `filteredHighRateWeight`: the existing public high-rate weight semantics used by the 20-byte WMB+ stream today: `currentWeight` after zero qualification and the smart median/average filter.
- `controlWeight`: signal used by StopMyBru, target stop, tare/start sequencing, and other control logic. It may need stricter confirmation than display output.
- `presentationWeight`: robust user/client-facing estimate for the clean 20 Hz outputs.

The clean Float32 output should use `presentationWeight`. StopMyBru and other control logic should not automatically consume the same signal until its lag and overshoot behavior are explicitly tested.

USB and dashboard diagnostics should expose enough of these stages during beta testing to explain whether instability is entering before or after firmware filtering, and whether an output is being algorithmically held.

## Firmware track 1: clean 20 Hz Float32

This is the clearest beta9 priority.

Goals:

- Keep Float32 at `19 Hz` to `21 Hz`.
- Do not emit catch-up bursts after a delayed notification.
- Replace sticky bump/glitch hold with bounded, causal aggregation.
- Combine all filtered high-rate source samples from the short trailing window before each 20 Hz notification.
- Reject disturbances symmetrically, not only downward.
- Reset immediately and explicitly on tare.
- Recognize confirmed cup placement and cup removal as real steps.
- Keep Float32 separate from StopMyBru and other control logic until validated.

Candidate filter shape:

1. Collect `filteredHighRateWeight` source samples since the previous Float32 tick or over a bounded trailing window.
2. Use a robust estimator such as median, trimmed mean, or winsorized mean to reject one-frame impulses.
3. Apply a modest low-pass or slew limit tuned to the latency budget.
4. Detect confirmed real steps when multiple samples agree, allowing cup placement/removal without excessive delay.
5. Reset estimator state only on tare, profile change, reconnect if necessary, or confirmed load discontinuity.

Do not reset the presentation estimator on timer start. Starting a timer without taring must not disturb weight. Do not reset it from diagnostic-only actions such as explicit scale-quality counter reset; resetting diagnostic counters must never create a visible weight transient.

Initial targets:

- Float32 notify cadence: `19 Hz` to `21 Hz`.
- No bump-induced freeze while the underlying trend continues.
- No active-pour negative steps of `-1 g` or worse.
- Active-pour p95 step below `1 g`.
- No isolated step above `2 g`.
- Tare visible within a tested hard limit, ideally `250 ms` and no more than `350 ms`.
- Cup placement and removal visible within the same tested hard limit, with explicit tests for both.

Open Float32 tuning question:

- Is the acceptable compatibility lag closer to `250 ms`, `325 ms`, or a stricter value? Pick this before finalizing median/window/low-pass constants.

## Firmware track 2: WMB+ extended stream policy

Do not silently smooth the existing high-rate 20-byte WMB+ stream. It currently transmits the public `currentWeight`, represented here as `filteredHighRateWeight`: zero-qualified and smart-filtered by the existing median/average filter. It also carries timestamp, sequence, flow, quality, cadence, and diagnostics. It is valuable precisely because it exposes current acquisition behavior.

Beta9 should preserve those existing high-rate weight semantics. Switching `diagnostic-high-rate` to the less-filtered `qualifiedWeight` stage would be a substantial behavioral change and should not be bundled into the clean 20 Hz work.

Define two explicit WMB+ BLE profiles:

- `diagnostic-high-rate`: the existing high-rate `filteredHighRateWeight` signal with honest instability diagnostics.
- `clean-20hz`: the robust clean estimate, packaged in the extended 20-byte format for GaggiMate/WMB clients that want stable machine-facing weight.

Profile requirements:

- The read-only capabilities characteristic advertises support for both profiles; it does not perform profile selection.
- The active profile is reported through dashboard, USB diagnostics, and a readable status value.
- Profile selection can be added through web settings, serial command, or BLE command.
- For beta9 testing, `diagnostic-high-rate` remains the WMB+ default and `clean-20hz` is opt-in.
- Float32 always uses the clean 20 Hz compatibility output.
- Raw, qualified, and filtered high-rate data remain available over USB regardless of BLE profile.
- `diagnostic-high-rate` keeps sequence, timestamp, quality, and instability evidence intact.
- `clean-20hz` uses the same robust estimator family as Float32 and preserves the existing 20-byte metadata contract.

Clean 20-byte packet semantics:

- Sequence increments once per transmitted clean 20 Hz packet.
- Timestamp represents the newest input sample used by the estimator.
- Byte 17 continues to mean acquisition rate, not BLE output rate.
- Existing status, battery, quality, diagnostic, and checksum fields remain intact.
- Flow is derived from the same clean estimator family or shared `controlWeight` used for clean weight semantics.
- Flow is marked invalid during estimator uncertainty, step confirmation, tare, and cup handling.
- Do not publish zero flow merely because the estimator is uncertain; zero falsely claims confirmed no-flow.
- Do not send clean delayed weight alongside noisy or differently delayed flow.

Flow evidence caveat:

- At-rest flow false positives are conclusive when the reference window is actually at rest.
- Apparent 2x to 3x pour over-read is suggestive, not proven, when it compares instantaneous flow to whole-pour average.
- Beta9 should repair flow estimation against the clean estimator and add validity metadata before using flow as a scoring or control signal.

This is safer than trying to make a 75 Hz to 90 Hz BLE signal appear perfectly clean immediately. Beta9 can deliver a useful clean 20 Hz path while preserving the evidence needed to improve the high-rate path later.

## Disturbance and instability diagnostics

Replace the downward-motion guard concept with symmetric disturbance rejection and explicit instability reporting.

Track short rolling windows, for example 250 ms and 1000 ms:
- Negative steps <= `-1 g`.
- Positive steps >= `+1 g`.
- Negative steps <= `-2 g`.
- Positive steps >= `+2 g`.
- Absolute deltas >= `5 g`.
- Window min/max range.
- Large alternating sign changes.

Set an `unstableMeasurement` flag when a window exceeds tuned thresholds such as:

- 250 ms: at least 3 steps with `abs(delta) >= 2 g`.
- 1000 ms: too many negative and positive reversals for a monotonic pour.
- 1000 ms: range >= `8 g` without matching net pour progress.
- Any `+10 g` followed by `-8 g` within 150 ms, or inverse.

Distinguish these concepts:

- `recentBump`: likely physical knock or abrupt load shift.
- `unstableMeasurement`: oscillatory measurement burst, vibration coupling, liquid impact, or electrical interference.
- `glitch`: implausible single-frame acquisition outlier rejected by the qualification path.

Dashboard and USB beta diagnostics should include:

- `scale_calibrated_raw_weight`
- `scale_qualified_weight`
- `scale_filtered_high_rate_weight`
- `scale_control_weight`
- `scale_presentation_weight`
- `scale_unstable_measurement`
- `scale_unstable_burst_count`
- `scale_recent_unstable_burst`
- `scale_negative_step_count`
- `scale_positive_step_count`
- `scale_large_delta_count`
- `scale_one_second_range_g`
- `scale_presentation_hold_count`
- Active BLE profile name

These stages can distinguish source instability from filter behavior. They cannot, by themselves, prove whether the source instability is mechanical vibration, liquid impact, electrical interference, or a build defect.

For BLE extended packets, add an instability/status bit only if payload space allows cleanly. Dashboard and USB are enough for the first beta9 diagnostic pass.

## Hardware validation common gate

Run a quiet static-weight test before replacing parts and before judging firmware filters:

1. Put the scale on a stable table away from the espresso machine.
2. Tare.
3. Record 10 seconds empty.
4. Gently place a known stable weight, ideally 100 g or more.
5. Record 20 seconds untouched.
6. Gently remove it.
7. Record 10 seconds empty.

Pass expectations:

- No gram-scale oscillation while untouched.
- No repeated negative steps <= `-1 g` during the stable hold.
- Worst 1-second range during stable hold preferably <= `0.2 g` to `0.5 g`.
- ScaleBench implausible frames near zero during stable hold.
- Float32 clean output settles within the chosen latency budget.
- WMB+ diagnostic-high-rate output remains honest about any raw instability.

If the static test fails, inspect:

- Scale mass and base inertia. A very light WMB+ build may mechanically amplify machine vibration compared with Solo Barista.
- Load-cell screws and mechanical preload.
- Platform or case rubbing against fixed parts.
- Cup tray flex or rocking.
- Load-cell cable strain.
- HX711 solder joints.
- Load-cell lead solder joints.
- Ground continuity.
- Wire routing near motors, pump, heater wiring, or high-current paths.
- Whether load-cell leads should be twisted/shielded or shortened.
- Whether HX711 excitation/reference wiring is mechanically stressed.

If the static test passes but machine pour fails, focus on vibration isolation, added mass/damping, and clean 20 Hz presentation filtering.

Run a controlled matrix to separate mechanical and electrical hypotheses:

| Test | Purpose |
| --- | --- |
| Counter, machine absent | Baseline quiet environment |
| On machine, machine off | Mechanical coupling from placement only |
| On machine, powered/heating, pump off | Electrical/thermal environment without pump vibration |
| On machine, pump running with no pour if possible | Pump vibration/electrical coupling |
| On machine, normal pour | Full machine and liquid-impact case |
| Added mass/damping A/B | Tests whether low scale mass and resonance dominate |

Use the same capture windows and report pre-pour noise from 10 Hz median-normalized windows so the `0.14 g` counter and `5.22 g` machine figures remain comparable.

## Beta9 scope recommendation

Preferred beta9 scope:

1. Fix Float32 first.
2. Add selectable clean 20 Hz WMB+ extended profile second.
3. Preserve high-rate diagnostics.
4. Defer a fully cleaned high-rate presentation stream until the two 20 Hz outputs are validated.

Out of scope for the first beta9 pass:

- Silently smoothing the existing diagnostic high-rate stream.
- Using the clean presentation estimate for StopMyBru target stop without separate latency/overshoot validation.
- Claiming hardware failure from pour captures alone without static-weight testing.

## Beta9 acceptance criteria

### Float32 clean 20 Hz

- Cadence remains `19 Hz` to `21 Hz`.
- No catch-up bursts.
- No sticky bump freeze while the underlying trend continues.
- Active-pour window excludes tare, cup handling, and settling.
- Within the active-pour window, no negative steps <= `-1 g`.
- Within the active-pour window, p95 step below `1 g`.
- No isolated step above `2 g`.
- Tare latency is measured from command acceptance or physical tare action to the first three outputs within the configured zero tolerance.
- Cup placement/removal latency is measured from the physical step to the first three outputs within the configured target tolerance.
- Tare, confirmed cup placement, and confirmed cup removal are visible within the selected hard limit, ideally <= `250 ms` and no more than `350 ms`.

### WMB+ diagnostic-high-rate

- Missing sequence count: 0.
- Long gap count: 0.
- Disconnect count: 0.
- Sequence, timestamp, quality, cadence, and instability diagnostics remain honest.
- Raw, qualified, and filtered high-rate data remain available over USB.

### WMB+ clean-20hz profile

- Uses the same robust estimator family as Float32.
- Capabilities advertise support for the profile without selecting it.
- Active profile is reported in dashboard/USB/readable status.
- Existing 20-byte metadata fields remain intact with the clean packet semantics defined above.
- Does not pretend to be diagnostic high-rate.

### Static-weight bench

- Stable hold has no repeated gram-scale reverse steps.
- Worst 1-second range during untouched hold is within the selected threshold.
- Presentation hold/rejection counters remain near zero after the load settles.

### Normal UX

- Tare remains responsive.
- Cup placement and removal are not hidden by filtering.
- Idle zero behavior remains clean.
- BLE clients receive a stable, plausible clean output when using clean profiles.
- Dashboard shows enough diagnostic detail to separate source instability from filter behavior. The controlled matrix separates machine-coupled vibration, electrical interference, bad hardware, and normal pour turbulence.

## Implementation order

1. Expose calibrated raw telemetry, shared stage naming, separate `rawReadSequence` and `packetSequence`, and exact counters for accepted, plausibility-rejected, queue-dropped, read-error, and timeout paths.
2. Re-capture counter and machine runs with the new telemetry so source-to-public yield is visible rather than inferred only from public packet timing.
3. Run the static matrix, beginning with machine unplugged versus powered/pump-off, then pump-running and added-mass/damping A/B.
4. Design Float32 against actual 20 Hz emissions using tracking error, effective delay, hold-while-reference-moves, tare/cup-step settling time, and 20 Hz standardized reverse movement.
5. Replace Float32 sticky bump/glitch hold with bounded causal aggregation and symmetric disturbance rejection.
6. Repair flow estimation against the clean estimator and add validity metadata.
7. Add selectable clean 20 Hz WMB+ extended output while preserving `diagnostic-high-rate` as the default honest diagnostic profile.
8. Preserve USB high-rate diagnostic output regardless of BLE profile.
9. Add host/simulation tests for freeze-release, alternating deltas, tare reset, cup placement, cup removal, flow validity, sequence semantics, and profile switching.
10. Flash beta9 and rerun static, counter/faucet, WMB+ machine, and Solo comparison captures.

## Open questions

- What exact latency budget should beta9 target for Float32: `250 ms`, `325 ms`, or another hard limit?
- Which clients should opt into `clean-20hz` while WMB+ defaults to `diagnostic-high-rate` for beta9?
- How should profile selection persist: compile-time default, web setting, serial command, or BLE command?
- Should StopMyBru use `controlWeight` with separate confirmation rather than either clean presentation or diagnostic high-rate?
- What threshold best separates confirmed cup placement/removal from vibration bursts?
- How much added mass or damping is practical for the reference WMB+ physical build?

## Recommendation

Beta9 should be structured as two firmware tracks plus one shared hardware validation gate. Clean Float32 is the first priority because it is already a 20 Hz compatibility contract and currently has a confirmed freeze-release artifact. The WMB+ extended stream should keep its high-rate diagnostic identity and add a selectable clean 20 Hz profile rather than silently smoothing everything.

In parallel, validate the physical build with a static-weight test. If the static test is clean but machine pours are noisy, prioritize scale mass, damping, and vibration isolation. If the static test is noisy, inspect the load-cell mechanics, HX711 wiring, grounding, and solder joints before relying on firmware filtering to hide the problem.
