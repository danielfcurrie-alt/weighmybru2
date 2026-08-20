# WMB firmware beta9 Float32 clean-output review

This is a firmware design review note for the WMB+ beta9 Float32 compatibility
output. ScaleBench replay data is included only as validation evidence. The
algorithm belongs in WMB firmware, not in ScaleBench.

No firmware has been flashed with this algorithm yet.

## Current implementation review status

The beta9 firmware branch now implements the Float32 observed-sample selector
against zero-qualified observed input. That keeps the Float32 lane in the same
auto-zero/zero-clamp domain as the WMB `6E400002` lane while still emitting only
finite source-window observations, not extrapolated values.

The coherent fast path now has two routes:

- Flat coherent window: newest sample is close to the window median.
- Ramp coherent window: samples fit a bounded line up to `12 g/s` with max
  residual at or below `0.45 g`.

The synthetic flow-rate sweep in
`analysis/review-artifacts/float32-selector-stress-window-300-flow-sweep.json`
validates only that gate arithmetic. It is noiseless and should not be read as a
field-performance claim.

Replay against captured ScaleBench data remains useful but provisional. Those
replays use exported public WMB samples as a proxy input, and public WMB samples
are already filtered/zero-qualified. The real on-device Float32 selector input
is zero-qualified observed input before the public smart median/average filter,
so hardware noise can increase selected source age and reduce newest-sample
selection compared with replay.

The remaining close-out test is an on-device capture during a real pour with
USB source diagnostics enabled and a single `/api/dashboard` snapshot/poll. Run:

```bash
python3 tools/runtime-cadence-smoke.py \
  --port auto \
  --base-url http://192.168.4.1 \
  --profile dashboard-safe \
  --duration 30 \
  --source-stream \
  --json-output analysis/float32-stress/live-float32-pour.json \
  --no-fail
```

The key acceptance fields are `float32.source_age_p50_ms`,
`float32.source_age_p95_ms`, `float32.source_age_max_ms`,
`float32.source_age_over_tick_pct`, `float32.source_stale_pct`,
`float32.window_range_p95_g`, `float32.selection_suspect_pct`, and the source
section's raw/qualified/public sequence deltas.

## External review update

External reviews challenged the observed-sample medoid selector, trend veto,
validation method, and control-client framing. This changes the beta9
recommendation.

### Review blockers

Before choosing a filter, beta9 must resolve these design questions:

1. Define the estimator input stage.
   "Source sample" is currently ambiguous. It could mean calibrated raw HX711
   samples, plausibility-qualified samples, or the current public weight after
   existing dynamic filtering and zero qualification. Those are not equivalent.
   Replay should compare all relevant stages. Current Float32 firmware behavior
   reads the already-filtered/current public weight, so replaying exported public
   WMB+ samples may not measure a proposed calibrated-raw estimator.
2. Establish an independent tracking reference.
   A delayed acausal reference derived from the same gated public stream can
   hide source censorship and latency. Acceptance should prefer ungated
   calibrated-raw telemetry, controlled known-load traces, or both. Error and
   delay should be reported separately as a Pareto pair.
3. Use clean algorithm baselines.
   The previous original-score table compared high-rate WMB+ stream scores with
   Float32 replays. That is context, not a valid algorithm comparison.
4. Keep control clients on the right contract.
   Float32 `6E400004` is the Bean Conqueror compatibility characteristic.
   GaggiMate uses the WMB characteristic `6E400002`. GaggiMate/Gaggiuino control
   latency should not set the Float32 window directly. Float32 should remain a
   presentation/compatibility lane unless a specific client is confirmed to use
   it for stop/control behavior.

Key reviewer findings to carry forward:

- On the machine capture, a majority of `200 ms` source windows are disturbed.
  More precisely, a majority of public-stream windows exceeded the chosen
  instability threshold. That suggests a >50% breakdown problem for
  median/medoid/order-statistic
  approaches. In that regime the correct firmware state may be `unreliable`, not
  "find a smarter single best sample."
- Widening the window can make the majority-contamination problem worse on the
  machine capture because the disturbance is ambient, not sparse.
- A plain trailing median at the same window should be the baseline. Medoid,
  observed-sample selection, trend veto, and adaptive badness must each beat that
  baseline before earning firmware complexity.
- The trend veto is risky because a trusted rising trend can fight a real large
  reversal such as cup removal or sudden load change.
- Shape-only output criteria can be gamed by filters that stop tracking reality.
  Validation needs a tracking-error term against a delayed acausal reference, not
  only p95 step, max step, and negative-step counts.
- The previous score table compared original high-rate WMB+ stream scores with
  Float32 replays. That is useful context but not a clean algorithm comparison.
  Float32-to-Float32 comparisons and median baselines are the right decision
  evidence.
- Plain median is not yet a complete solution under current machine noise.
  Review replay still showed machine p95/max above the earlier shape targets at
  `200 ms`, `400 ms`, and `600 ms`; the machine case reportedly needs roughly
  `1.4 s` to pass those shape-only limits, which is not an acceptable production
  recommendation.
- Window length is not equal to effective latency. A causal median's step/ramp
  delay is closer to a fraction of the window. One review measured effective lag
  against an acausal reference at about `138 ms`, `166 ms`, and `224 ms` for
  `200 ms`, `300 ms`, and `400 ms` selector windows on the machine capture.
- Bounded consistent lag is much safer than unbounded variable lag. The beta8
  Float32 freeze/release artifact reportedly held about `7.8 s`, which is far
  worse for threshold/stop consumers than adding tens of milliseconds of
  bounded estimator delay.
- A threshold consumer gives a principled reason to prefer observed samples:
  if the output crosses a threshold, that crossing corresponds to a real
  measurement. A true median can cross a threshold between observed samples. A
  different review argues this is acceptable because the BLE contract is a
  weight estimate, not an ADC-code stream. This remains an explicit firmware
  decision.
- Decoupling windows is a negative result. Using a long robust window to choose
  a target but forcing emission from only the most recent `100 ms` performed
  worse because the recent window can be disturbed end-to-end.

Revised recommendation: do not implement the medoid/trend selector as beta9
firmware yet. First define the input stage, establish median baselines and
tracking-error validation, then re-test observed-sample variants against those
baselines.

## Firmware goal

The Float32 characteristic is the legacy compatibility lane:

```text
6E400004-B5A3-F393-E0A9-E50E24DCCA9E
payload: 4-byte little-endian float weight_g
cadence: 20 Hz target
```

Beta9 should make this lane a clean 20 Hz presentation signal while preserving
the existing contract:

- Emit exactly one Float32 weight per tick.
- Keep cadence near 20 Hz without catch-up bursts.
- Emit a finite bounded weight estimate. Whether that estimate must be an actual
  observed source weight is now an open firmware decision.
- Do not extrapolate beyond the source window.
- Reset selector state immediately on tare.
- Keep this path separate from StopMyBru/control/flow decisions until latency
  and behavior are validated.

## Firmware problem being fixed

The current Float32 path snapshots the latest weight at the 20 Hz tick. During
machine vibration, the latest sample can be a bad transient even when better
samples exist in the same short interval.

The old pre-shot bump/glitch behavior also caused visible freeze/release
artifacts: the stream could hold a previous value and later jump forward. That
is bad for compatibility clients because it is both visually wrong and
dangerous for software that treats weight movement as meaningful.

Beta9 should replace "latest sample plus sticky hold" with a bounded causal
selector.

The maximum hold/lag failure must become an acceptance criterion. A bounded
`200 ms` to `400 ms` estimator may be acceptable for presentation and threshold
consumers; an unbounded multi-second hold is not.

## Original candidate selector

This section documents the original observed-sample selector candidate. It is no
longer the recommended implementation until it beats a median baseline and
passes cup/removal discontinuity tests.

At each Float32 tick, maintain a trailing source-sample window. The previously
tested candidate window was `200 ms`.

The selector chooses one source sample from that window.

### 1. Coherent-window fast path

If the source window is coherent:

```text
window range <= 2.0 g
newest sample within 0.5 g of window median
```

Then emit the newest observed source sample.

Rationale: when the stream is sane, the newest sample gives the lowest latency.

### 2. Incoherent-window fallback

If the window is not coherent:

1. Compute the median of source weights in the window.
2. Choose the observed source sample closest to that median.
3. Tie-break toward the newest sample.

This is a medoid-like choice, not a median output. If the median lies between
two samples, firmware still emits one real observed sample.

Rationale: when the stream is ugly, a single newest sample is too fragile.

### 3. Direction/trend veto under review

The medoid can be wrong when the bad samples form the majority cluster.

Example:

```text
Previous outputs: 51, 52
Current window:   46, 88, 53, 47, 49
```

The medoid is `49`, but that contradicts a growing stream. The better observed
sample is likely `53`.

The proposed firmware rule is a narrow veto:

1. Estimate a short causal trend from the last 2 to 4 emitted Float32 outputs.
2. Trust the trend only when recent deltas are small and directionally
   consistent.
3. If the medoid reverses that trusted direction, mark the medoid suspect.
4. Search the current source window for an observed sample closer to the trend.
5. Override only if that candidate is materially closer to the trend prediction.

This must remain a veto, not a smoothing engine. The firmware should not chase
its own prediction when real samples disagree.

Reviewer concern: this veto can be unsafe during real reversals. A cup removal
or sudden load change can look like a large reversal against a trusted rising
trend. The current recommendation is to keep this out of firmware until
load-discontinuity handling is explicit and tested.

### 4. Adaptive bad-stream state under review

The selector should not be equally skeptical all the time. A counter/faucet pour
and a machine-vibration pour are different operating conditions.

Maintain a rolling stream-badness score from recent source-window shape:

- Large window spread.
- Outlier fraction relative to the window median.
- Opposing positive and negative jumps inside the same window.
- Very large adjacent source jumps.

Badness rises faster than it decays. When badness is low, the selector mostly
uses the coherent-window fast path and medoid fallback. When badness is high,
the direction/trend veto is allowed to intervene in incoherent windows.

Current replay constants, for review only:

```text
selectionWindowMs = 200
coherentWindowRange = 2.0 g
newestMedianBand = 0.5 g
adaptiveTrendWindowRange = 4.5 g
trendCandidateMaxPredictionError = 0.80 g
```

These constants are not final. They are starting points for firmware simulation
and bench validation.

Reviewer concern: if the majority of source samples are disturbed, changing the
selector may not be enough. Firmware should report or expose an unreliable
state. The extended WMB+ stream can carry quality/status metadata; the legacy
Float32 characteristic cannot.

## Revised firmware candidates

Beta9 should now evaluate these candidates in order:

1. Define and replay input stages: calibrated raw, plausibility-qualified, and
   current public weight.
2. Plain trailing median baseline.
3. Observed median/nearest-median variant if "threshold crossings must
   correspond to observed measurements" remains a hard Float32 requirement.
4. Adaptive unreliable-state detector.
5. Medoid/observed-sample selector only if it beats median baselines.
6. Trend/direction veto only after cup placement/removal and large real
   discontinuities are handled.

The window should be selected from validation rather than assumed. `400 ms` may
be more realistic than `200 ms` for robustness, but one review argues a plain
median still does not solve the machine capture under earlier shape criteria.
This needs direct testing against tracking error, effective delay, cup-step
latency, and max-lag variance.

Open firmware decision: decide whether Float32 must remain observed-sample-only.
A true median may output a value between two samples. That can improve tail
behavior but weakens the "threshold crossing came from a real measurement"
property. If that property remains firm, compare against an observed
median/nearest-median variant rather than a mathematical median.

## Firmware state and reset behavior

Selector state should include:

- Source ring buffer: timestamp + weight.
- Last emitted Float32 weight.
- Last 2 to 4 emitted Float32 outputs for trend confidence.
- Rolling stream-badness score.
- Diagnostic counters for suspect selections and trend veto activations.
- Input epoch/tare epoch.
- Source age and last finite estimate age.

Reset selector state on:

- Tare.
- Float32 profile/selector setting change, if added.
- Reconnect only if required by the BLE lifecycle.
- Confirmed load discontinuity, if we later add cup-placement/removal logic.

Do not reset selector state merely because:

- Timer starts or stops.
- Diagnostic counters are cleared.
- A dashboard/status read occurs.

Additional semantics to define before firmware implementation:

- The estimator should run continuously, independent of BLE connection.
- Reconnect should not reset the estimator unless the BLE lifecycle absolutely
  requires it.
- Successful tare completion starts a new input epoch; pre-tare and post-tare
  samples must not mix.
- Post-tare warm-up needs a bounded defined fallback.
- Fresh-but-unreliable input should continue producing the best finite estimate
  while reporting unreliable diagnostics where possible.
- Stale/disconnected input should stop notifications rather than publish a
  frozen weight indefinitely.

## Validation evidence

ScaleBench replay was used to evaluate the firmware idea before flashing. It is
not the algorithm and it is not the architecture.

Replay limitations:

- Replays use exported WMB+ public samples, not every raw HX711 acquisition.
- The estimator input stage is not yet defined, so these replays may be testing
  the wrong stage for the final firmware design.
- ScaleBench score does not know physical truth or accuracy.
- Score is affected by 20 Hz output cadence, duplicates, and usable-slot
  accounting.

All-phase replay means all 50 possible 20 Hz tick phases were tested.

### Score evidence, not clean algorithm evidence

The original scores below are high-rate WMB+ stream scores, while strict and
adaptive are Float32-lane replays. They are retained as context only and should
not be used as the primary algorithm comparison.

| Capture | Original high-rate WMB+ score | Strict selector mean | Adaptive selector mean |
|---|---:|---:|---:|
| Bad WMB+ machine pour | 46 | 73.6 | 74.9 |
| WMB+ counter/faucet pour | 80 | 71.2 | 72.4 |
| Later WMB+ machine pour | 59 | 56.6 | 56.2 |

Limited interpretation:

- The bad machine case improves dramatically.
- The clean counter case loses score versus original high-rate WMB+ because
  Float32 is limited to 20 Hz and ScaleBench does not directly score accuracy.
- The later machine case is roughly flat.
- Median-baseline and Float32-to-Float32 comparisons are still required.

### Implausible-count evidence

All-phase mean implausible counts:

| Capture | Old Float32 | Strict selector | Adaptive selector |
|---|---:|---:|---:|
| Bad WMB+ machine pour | 118.0 | 44.7 | 42.6 |
| WMB+ counter/faucet pour | 43.0 | 12.8 | 10.2 |
| Later WMB+ machine pour | 48.0 | 20.7 | 20.1 |

Interpretation:

- Bad machine implausibles drop about 64%.
- Counter implausibles drop about 76%.
- Later machine implausibles drop about 58%.

### Implausible-severity evidence

Severity was reconstructed by measuring each implausible frame's absolute
deviation from its local 3-sample median.

| Capture | Algorithm | Implausibles | Mean error | Std dev | P95 |
|---|---:|---:|---:|---:|---:|
| Bad WMB+ machine | old Float32 | 118.0 | 0.935 g | 0.420 g | 1.745 g |
| Bad WMB+ machine | strict | 44.7 | 0.806 g | 0.298 g | 1.429 g |
| Bad WMB+ machine | adaptive | 42.6 | 0.806 g | 0.304 g | 1.443 g |
| WMB+ counter | old Float32 | 43.0 | 0.726 g | 0.225 g | 1.294 g |
| WMB+ counter | strict | 12.8 | 1.394 g | 0.971 g | 3.118 g |
| WMB+ counter | adaptive | 10.2 | 1.449 g | 0.965 g | 3.110 g |
| Later WMB+ machine | old Float32 | 48.0 | 1.141 g | 0.503 g | 2.088 g |
| Later WMB+ machine | strict | 20.7 | 0.981 g | 0.597 g | 2.066 g |
| Later WMB+ machine | adaptive | 20.1 | 1.004 g | 0.566 g | 1.923 g |

Interpretation:

- On the worst machine capture, both count and severity improve.
- On the later machine capture, count improves substantially and p95 severity is
  flat to slightly better.
- On the counter capture, the selector removes many small implausibles, leaving
  fewer but harder edge cases.

## Review questions

Please review this as a WMB firmware algorithm:

1. What exact input stage should the Float32 estimator consume: calibrated raw,
   plausibility-qualified, or current public weight?
2. Should Float32 remain observed-sample-only for threshold-crossing semantics,
   or is a true median acceptable because Float32 is a weight estimate?
3. Is `400 ms` a better baseline window than `200 ms` once effective delay and
   tracking error are measured separately?
4. What hard max-lag and lag-variance bounds should beta9 enforce so the
   Float32 lane can never repeat the beta8 multi-second freeze/release failure?
5. Confirm the GaggiMate/Gaggiuino contract from source or project maintainers:
   do they consume WMB `6E400002`, Float32 `6E400004`, or both? If they consume
   weight for threshold/predictive stop rather than feedback control, latency
   cost is expected to be overshoot roughly equal to flow times lag, but this is
   still an inference until confirmed.
6. Should clean Float32 remain presentation-only while the high-rate WMB path
   keeps lower-latency control/diagnostic semantics plus quality flags?
7. Should the firmware use a fixed window or vary the window with stream
   badness?
8. Are the coherent-window thresholds reasonable, or do they reject legitimate
   clean ramps above roughly `5 g/s`?
9. Is the medoid fallback safe enough when bad samples form a majority cluster?
10. Is the trend veto too risky during cup placement, cup removal, or sudden real
   load changes?
11. Should confirmed load discontinuities bypass the normal selector?
12. What diagnostic counters should firmware expose over USB/dashboard?
13. Should the extended WMB+ stream report `unreliable` when instability exceeds
   a threshold?
14. Should this remain Float32-only for beta9 while the high-rate WMB+ stream
   stays diagnostic?

## Proposed beta9 implementation boundary

Do not flash the observed-sample/trend selector yet. The next beta9 step is
simulation/replay work:

- Define and replay the estimator input stage: calibrated raw,
  plausibility-qualified, and current public weight.
- Establish plain trailing median baselines at `200 ms`, `400 ms`, and possibly
  `600 ms`.
- Add tracking-error validation against an independent reference where possible;
  if using delayed acausal public-stream reference, report its limitations.
- Report error and effective delay separately as a Pareto pair, including
  worst/p95 phase results, not only all-phase means.
- Add max-lag and lag-variance bounds so a multi-second freeze/release failure
  is impossible.
- Add explicit instability-threshold/unreliable-window metrics.
- Compare true median, observed median, medoid, and adaptive variants using the
  same Float32 output cadence.
- Test cup placement, cup removal, tare, and real load discontinuities before
  any trend veto reaches firmware.
- Fit constants on separate captures from those used for acceptance.

When firmware work resumes:

- Keep WMB+ high-rate stream semantics unchanged.
- Keep StopMyBru/control logic unchanged.
- Add diagnostics for input samples, emitted samples, source age, window sample
  count/range, detrended MAD, instability/unreliable transitions,
  discontinuity bypasses, tare epoch, raw-read sequence gaps, plausibility
  rejects, queue drops, and selector decisions.
- Validate in Wokwi/runtime smoke tests where possible.
- Then flash a test build and recapture ScaleBench data.

Do not treat this as a ScaleBench feature. ScaleBench is only the measurement
tool used to evaluate the firmware selector before hardware flashing.
