# T-Watch Shadow PPS device check

17 November 2026 · One LILYGO T-Watch Ultra V1 / SX1262 test unit.

Shadow observes the BLE software clock at independently captured GNSS PPS
edges. The receiver achieved a coherent GNSS fix and recurring physical PPS.
After phase-blind ten-second settling, the largest completed population in a
three-minute run contained **14 samples**:

| Baseline metric | Observed value |
| --- | ---: |
| Signed MEAN | −2.9104 µs |
| P99 absolute error | ~3.071 µs |
| RMS | 2.911 µs |

The tested unit's local fine-input profile applies a provisional **+2.900 µs**
fixed adjustment, rounded to its 50 ns coordinate step. The tested 0.1.9 image
supplied the baseline; its 0.1.10 image uses the adjusted local profile. The
correction is not a public default or a constant for every 0.1.10 build. Both
application-only installs
passed complete application readback and protected-prefix/final-sector checks.
This is a fixed empirical adjustment; GNSS remains a read-only witness.

No valid GNSS/PPS comparison population was available during the original
three-minute post-flash check. Arithmetic cancellation of that training mean
was not a measured residual. The later checks below supply fresh observations.

The run included changing BLE availability and restarted populations. The
14-sample cohort was selected by sample count, without phase trimming; other
windows and failures remain in the retained local diagnostics. `~` denotes the
coarse histogram tail. Firmware kept the selected cohort in one context, but
exact per-edge GM identities were not exported. The derivation's saved source
checkpoint is a later RC1 observation, distinct from the direct-BLE cohort.
This short result does not qualify P99 tails, separate
receiver/GNSS/path bias, or establish independent output or UTC accuracy.

## Parallel models and one shared clock

A later fixed **20-minute** window on 0.1.14 retained all **1,200 physical PPS**
records, with no missing sequence. LoRa supplied **1,157 eligible comparisons
from 41 timing pairs** in one source/reference context: MEAN **−3.599390 ms**,
P99 ABS **3.605778 ms**, RMS **3.599391 ms**. Its local delivered-model correction
is **+3.599390 ms**, applied once beyond the existing 99.328 ms engineering
symbol-delay convention. This is an aggregate endpoint adjustment, not a
receiver-only latency measurement or a public default.

BLE supplied **794/1,200** eligible comparisons with MEAN **−87 ns**. Coverage
was below the frozen 1,080-sample training gate, so its existing **+2.900 µs**
adjustment was retained. Unmapped pulses unnecessarily restarted settling;
0.1.15 rejects those pulses while allowing a fresh eligible pulse from the same
authenticated context to resume. Reference/source/capture changes still reset it.
Earlier counter-bridge refusals and all rejected observations remain retained.

A separate, fixed **120-second engineering check** on 0.1.15 observed:

| Corrected model | Eligible PPS / observed | MEAN | P99 ABS | RMS |
| --- | ---: | ---: | ---: | ---: |
| BLE | 115 / 120 | −0.051 µs | 0.112 µs | 0.059 µs |
| LoRa, five timing pairs | 115 / 120 | −0.342 µs | 4.375 µs | 1.567 µs |

The 110 jointly eligible full-UTC projections differed by at most **4.588 µs**.
Counter mapping contributes uncertainty to the LoRa result. This short check
verified correction sign and relative epoch alignment; it did not complete the
planned 20-minute held-out precision validation.

The local 0.1.16 profile enabled one continuously running clock for a fixed
**seven-minute functional test**. All 420 stage PPS retained a valid GNSS
reference. One clock instance and counter survived every transition, with zero
backward UTC steps, counter reversals or false HOLD authority claims:

| Stage | PPS modes retained | Largest absolute clock phase |
| --- | --- | ---: |
| Both on, 60 s | 57 BLE fine; 3 LoRa coarse | 0.231 µs |
| BLE off, 120 s | 118 LoRa coarse; 2 HOLD | 62.328 µs |
| BLE return, 90 s | 43 LoRa coarse; 47 BLE fine, including slewing | 80.522 µs |
| Both off, 60 s | 60 HOLD | 23.450 µs |
| Both return, 90 s | 34 HOLD; 56 BLE fine | 34.720 µs |

The oscillator rate learned from BLE remained unchanged during LoRa-only and
HOLD operation. Holdover phase was **−0.249 µs at 1.282 s**, **+1.082 µs at
10.283 s**, and **+24.103 µs at 60.284 s**. The last point fell between fixed
stages and remains separate from their statistics. Both radios were restored ON.
The RF trigger remains hardware-captured HeaderValid; GNSS never trains the clock.
A final face-control check verified GNSS rail/UART/capture retirement and clock
continuity while leaving Shadow, then restored the face and reception.

Follow-up review found a controller regression: during the 118 jointly eligible
LoRa-only PPS, the radio model measured **12.606 µs RMS**, while the shared clock
measured **32.197 µs RMS**. Their full-UTC difference was **21.170 µs RMS**.
The controller froze its BLE-learned rate, filtered model phase again and could
lose pending steering across brief mapping refusals. It also omitted the
predictor's damped acceleration terms from its frequency input. The radio
receive and estimator sources were unchanged; a thermal or load explanation
for the input-model drift remains an inference.

The 0.1.17 correction passed a separate **185-second** short check: 20 seconds
with both radios, 90 seconds with BLE physically off and 75 seconds of BLE
return. All five accuracy checkpoints passed. In the final 30-PPS coarse window,
the clock-to-LoRa difference was **1.940 µs RMS**; clock PPS RMS was **11.980 µs**
and LoRa model RMS **11.710 µs**. The return checkpoint measured **0.053 µs**
clock PPS RMS over 27 eligible pulses. One clock and counter continued without
backward UTC steps. This verifies improved controller tracking in that short
run; it does not remove LoRa input-model drift or qualify long holdover.

Its subsequent fixed seven-minute test completed all stages, including
recoverable gaps. Across 120 jointly eligible LoRa-only PPS, clock-to-model RMS
was **3.339 µs**, versus **21.170 µs** in the earlier 118-PPS population. Clock
PPS RMS was **14.761 µs**, with **13.857 µs** from the LoRa model itself. The
final BLE-return stage failed its fine-mode coverage gate: a brief mapping
refusal restarted qualification despite fresh BLE progress. This is retained as
a failed coverage result. The 0.1.18 source corrects that reset within the
existing 500 ms progress guard; offline controller regressions cover short gaps,
real absence, context changes and expiry. Its fresh hardware switching test is
pending. CPU readings stayed at 240 MHz throughout the 0.1.17 test because
Shadow kept the display awake; the normal screen-off energy policy is unchanged.

A later GM failover exposed a separate availability bug: BLE followed the new
authenticated root, but the common clock rejected its unmatched calibration and
stayed in HOLD. The 0.1.19 correction separates source discovery from calibration
applicability. It verified new-root BLE delivery and a physical BLE OFF/ON return
in **10.02 seconds**, including receiver authentication and fit qualification,
on one boot with increasing counter and UTC. The new path remains uncalibrated;
this check changed no GNSS controls and measured no PPS precision. Roaming covers
roots in the existing trusted group; another group/key needs trusted enrollment.

These finite checks establish observed operation on one unit. The coarse
controller response and oscillator changes across radio power transitions still
need refinement; the longer precision campaign, independent output and absolute
UTC accuracy remain unqualified.

[Original short baseline aggregate](shadow-pps-2026-11-17.json) ·
[Parallel/shared-clock aggregate](shadow-dual-pps-2026-11-17.json) ·
[Controls and limits](../../firmware/twatch/README.md#shadow-pps)

Copyright (c) 2026 Jonathan Yantis. Documentation: MIT; selected measurement
record: CC BY 4.0.
