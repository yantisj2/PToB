# W12 holdover and sampled P99 duration

Copyright (c) 2026 Jonathan Yantis. MIT.

Current W12 0.2.35 uses **BLE-only, rate-only production HOLD**. Temperature
learning runs separately in shadow. The clock footer's **`P99:` is hold duration
in seconds at a fixed sampled absolute-error threshold of 1000 ns**. Error
P95/P99/MAX, thermal-shadow errors and selected-output tests are separate
populations. See the [approved paper and reproducible evidence](../../papers/mcxo-holdover-duration/README.md)
for dated results; they do not qualify every later image or operating window.

## Production clock

The active BLE servo remains the fast timing path. A separate causal HOLD rate
selector retains accepted physical pairs at least 250 ms apart over 32 seconds.
Channel-aware 16/32-second fits require 32 points, 85% span, eight repeated-channel
degrees of freedom, guards no larger than 10 ppb and agreement within 5 ppb.
The longer guard must improve on the fast fit. The median with the fast rate
must lie within 15 ppb of it; the final entry change is capped at ±5 ppb.
Insufficient history, stale input or a fit older than two seconds retains the
fast rate. Temperature never affects this choice.

HOLD freezes the trusted BLE frequency and residual phase at entry, reanchoring
the delivered clock to preserve calibration and continuity. It can continue
through permitted coarse acquisition. A trained returning BLE model starts
from the delivered time and slews the residual; a difference above one
millisecond refuses that continuous handoff. Local HOLD grants no source,
relay or authentication authority. Source and downstream expiry remain intact.

Missing temperature cannot invalidate rate-only HOLD. The separate silicon-sensor
model can learn and score thermal candidates; it never steers production or
the duration population. Build/console contracts disclose
`BLE_RATE_CONSENSUS_TEMPERATURE_SHADOW_V3` and `SHADOW_ONLY`.

## Continuous duration measurement

Forty-eight offset clocks freeze the production entry policy, phase and rate.
Later accepted BLE pairs score each frozen clock without correcting it. Seven
of every eight starts use a two-second lane; the eighth uses adaptive
12/24/48/96/120-second probes. Short completions accelerate ramp without
substituting for a long-age denominator. The target is the longest supported
sub-1 µs duration; twelve seconds is historical context.

At each age, take the maximum absolute error from entry through the first real
witness at or after that age, including witness overshoot and earlier crossings.
A recovered endpoint cannot erase a crossing. Gaps above 250 ms are counted;
their interiors are never interpolated. A run expires one second after its
probe without unsupported ages being marked successful. Those outcomes remain
unknown unless an earlier crossing already proves failure.

Each age retains its latest 128 outcomes and requires at least 100 scored runs.
Failed and unknown outcomes count against passing; allow at most `floor(n/100)`.
Select the contiguous passing prefix of ages: 250 ms steps through four seconds,
then 6/8/12/16/24/32/48/64/96/120 seconds. `>=` means the supported frontier has
not located a later adequately populated failure; `--` means insufficient or
stale evidence. Display tenths round downward. Source-scope changes reset the
population; five seconds without a fresh witness withdraws readiness.
Overlapping experiments are correlated. Sampled P99 is neither a confidence
guarantee nor a continuous or independently calibrated UTC bound.

The Hold page shows duration, run count, probe horizon and gaps. Existing status
fields `ui_hold_percentile=99`, `ui_hold_ms` and `ui_hold_samples` mirror it.
Capture-only `W12_HORIZON_START/END/RESET/STATUS` records retain this population
separately from thermal `W12_SHADOW` and production `W12_HOLDOVER` records.
Copied `last20` binds replay to the recorded status snapshot, after uncaptured
prehistory has left each relevant age ring.

## Capture and replay

Use the existing ticketed, boot-bound console and bounded capture lease.
`holdover test N` selects the delivered frozen production model for
1,000–120,000 ms while BLE remains a witness; `holdover test 0` stops it.
Admission requires a trained, sub-microsecond model and bounded residual phase.
This test is distinct from ordinary continuous shadow observation.

Retain these records together:

| Records | Purpose |
| --- | --- |
| `W12_HORIZON_*` plus accepted pairs | Independent prefix/ring duration replay |
| `W12_HOLDOVER_PAIR/RATE/ENTRY` | Causal rate inputs, cache time, selected and fast entry snapshots |
| `W12_HOLDOVER_EVENT/SENSOR/STATUS/RESUME` and samples | Selected output, zero thermal coefficient, crossings, gaps and BLE-return continuity |
| `W12_SHADOW` | Separate temperature candidate and paired baseline |

Run `tools/holdover_horizon_analyze.py` on a complete capture to replay duration;
use `tools/holdover_production_analyze.py` for selected-output decomposition,
causal rate replay, P95/P99/MAX and return steps. Missing capture history cannot
certify replay. Preserve rejected fits, missing records, unknowns, all ages and
full tails. Separate tuning from fresh validation, and keep per-device,
per-boot and physical-outage experiments distinct.

Offline checks from the repository root:

```sh
python3 firmware/w12/tools/shadow_holdover_tests.py --out artifacts/w12-shadow-tests
python3 firmware/w12/tests/holdover_production_analyze_test.py
```

These cover frozen-clock isolation, production/shadow equivalence, causal fits,
prefix crossings, expiry, quantiles, continuity and actual OLED rendering.
Exact-device work still follows the [hardware guards](build.md#hardware-boundary).
No learned-model persistence or partition change is implemented. The proposed
compact temperature data collection remains future work.

## Historical formats

The v22–v24 footer reported per-witness error within a single thermal-shadow
trial; v25 briefly enabled thermal production correction. v26 restored
temperature shadow-only, v27 added causal rate consensus, v28 changed `P99:` to
hold duration, and v29/v30 added parallel lanes and actual-witness coverage.
v31–v34 improve host allocation, exact storage and watchdog policy. Analyzers
retain historical formats, but their metrics stay separate. See the
[target changelog](../../firmware/w12/CHANGELOG.md) and dated evidence instead
of applying historical error-display instructions to current firmware.
