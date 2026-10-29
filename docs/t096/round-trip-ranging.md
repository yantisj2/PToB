# Proposed RC round-trip ranging

Copyright (c) 2026 Jonathan Yantis. MIT.

Research proposal, 29 October 2026. Not an implemented wire profile, installed
feature, calibrated distance result or hardware-operation instruction. Source
review: `b041dbfdfcc745a407305404230b147ab28df60e`. Existing unrelated local
documentation changes were not inputs to the timing analysis.

## Recommendation and scope

Start with **T096 GM ↔ directly following T096 RC**, using a granted,
low-priority, double-sided two-way exchange. Collect results without steering
the clock first. Request a new measurement approximately every 300 seconds
after stable lock; the GM serves one RC at a time and can defer indefinitely
when its timing, management or resource obligations leave no safe opportunity.
Five minutes is a refresh objective, never a service deadline.

The first useful output is an **estimated propagation-related link delay** with
age and calibration status. Metres are a derived estimate only after calibrating
radio endpoint delays. A few metres to tens of metres is a useful experimental
question; the existing evidence cannot promise any particular distance accuracy.
Do not turn an uncalibrated RTT residual into an automatic phase correction.

Extend to other RC targets only when both TX and RX events are qualified for
this observable. W12 currently has an experimental fine RX marker but estimated
TX; a 12.5 ns counter alone does not make its bidirectional ranging qualified.
See [W12 timing](../../firmware/w12/README.md) and
[TX reference](../w12/tx-reference.md). No GM role is added to W12 and no DC
runtime mode changes are needed.

## What the repository establishes

| Evidence or source | Finding | Consequence |
| --- | --- | --- |
| [October 12 Nordic experiment](../../evidence/v14/2026-10-12/README.md) | 436 eligible of 602 nominal PPS opportunities; 62.504 ns uncentered RMS, 191 ns maximum; 65 invalid-reference, 3 expired and 98 unobserved | Historical relative phase, not a measured range or current-build acceptance |
| [October 24 W12 baseline](../../evidence/s3/2026-10-24-w12-utc-basis/README.md) | W12 522/600 eligible, 60.328 ns RMS; downstream 451/600, 61.922 ns RMS, 211 ns maximum; selected TX 58/120 eligible | Keep availability and tails; neither cohort estimates bidirectional radio noise |
| [T096 capture](../../firmware/t096/src/capture.c) | Continuous 32-bit TIMER3 at 16 MHz, hardware capture and software extension | 62.5 ns per tick; extend each event promptly, never subtract raw ticks across a five-minute cycle |
| [Radio capture contract](../../firmware/t096/include/radio_capture.h) and [implementation](../../firmware/t096/src/radio_capture.c) | Raw RADIO ADDRESS; TX/RX equivalence explicitly uncalibrated; current parser accepts selected AUX_SYNC_IND timing records only | Existing captures are a foundation, not an ACL ranging implementation |
| [Admission policy](../../firmware/t096/include/rc_admission_policy.h) | Renewal at 240 s, 360 s maximum binding, bounded exchange and retry/start limits | Renewal, recovery and original expiry precede ranging |
| [Transport](../../firmware/t096/protocol/ble_transport.c) and [configuration](../../firmware/t096/prj.conf) | Four connection resources; three persistent control links can leave the fourth needed for advertising/join | No assumption that a spare ranging connection exists |
| [IPS draft](../../papers/indoor-ips/README.md) | Range is confounded with clock phase and persistent radio bias; averaging is conditional | Preserve native timestamps outside the timing servo; do not recycle fitted phase as an independent range measurement |

The public V14 and W12 verification commands were rerun successfully during this
review. Their hashes, denominators and numerical results reproduce; RF extraction,
hardware behavior and calibration were not rerun. No existing evidence was altered.

## Measurement and estimator

Use A = RC, B = GM. After the GM grants a finite session:

1. A transmits POLL at `a1`; B receives it at `b1`.
2. B transmits RESPONSE at `b2`; A receives it at `a2`.
3. A transmits FINAL at `a3`; B receives it at `b3`.
4. A later authenticated report exchanges the completed captures. Its transport
   delay is not a ranging observable. Actual TX captures cannot be promised in
   the same packet before transmission; use follow-ups.

All six values are immutable native hardware events, with their capture domain
and nominal scale. Subtract timestamps only within the same clock domain:

```text
Ra = a2 - a1       Da = a3 - a2
Db = b2 - b1       Rb = b3 - b2
tau_observed = (Ra*Rb - Da*Db) / (Ra + Rb + Da + Db)
tau_path = tau_observed - calibrated_exchange_bias
distance_estimate = 299792458 m/s * tau_path_seconds
```

This is the asymmetric double-sided estimator described in
[Decawave/Qorvo APS013](https://forum.qorvo.com/uploads/short-url/oW6ojIIo7adaWf6S7NnKzyN7OSi.pdf).
It reduces clock-rate mismatch sensitivity without requiring equal reply delays.
For constant clock scale factors `ka,kb` and reciprocal propagation, its residual
scale is `2*ka*kb/(ka+kb)`. Arbitrarily varying frequency during the exchange,
unmatched events and changing paths remain errors. Keep each exchange short;
skip a delayed exchange at its deadline instead of stretching it through a
source transition. Use a frozen rate estimate, if needed, consistently for the
whole exchange; never insert live phase-slewed UTC values into these intervals.

A four-timestamp single-sided check is useful diagnostically, but residual rate
error `epsilon` over turnaround `D` gives approximately `epsilon*D/2` false ToF.
At 1 ppm this is 5 ns / 1.50 m for 10 ms, and 50 ns / 14.99 m for 100 ms.
Host callbacks, queued ACK times and requested launch times are unsuitable.
The [Bluetooth Channel Sounding specification](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-61/out/en/low-energy-controller/channel-sounding.html)
also separates native-clock resolution, frequency compensation and internal
delay calibration. This proposal does not imply Channel Sounding support in the
installed controllers.

The estimator measures a reciprocal average, not the forward-path delay by
itself. With unequal directional delays, forward clock compensation retains
approximately half their difference. A constant combined TX/RX detector delay
does not average away. Calibrate each PHY, device pair/direction and relevant
channel/signal-level regime; an ACL calibration must be shown to transfer to
the periodic-advertising ADDRESS observable used for clock discipline.

## Resolution, averaging and the PPS witness

One 62.5 ns tick represents **18.737 m one way**, or **9.369 m** when a single
round-trip interval tick is halved. Neither number is the accuracy of a complete
six-timestamp estimate.

For a balanced double-sided exchange, with six independent uniform timestamp
quantization errors, linearization gives `sigma_tau = tick/4 = 15.625 ns`.
For unequal intervals compute the actual Jacobian/covariance instead. The
following is a quantization-only idealization, not a performance forecast:

| Independent complete exchanges | ToF standard deviation | Distance standard deviation |
| ---: | ---: | ---: |
| 1 | 15.625 ns | 4.684 m |
| 16 | 3.906 ns | 1.171 m |
| 32 | 2.762 ns | 0.828 m |
| 64 | 1.953 ns | 0.586 m |
| 128 | 1.381 ns | 0.414 m |

Real captures can be phase-correlated, including with a common radio/timer clock
or repeating turnaround. Repeating an identical quantized exchange 256 times
can preserve exactly the same error. Random millisecond scheduling is not proof
of fractional-tick diversity. Measure autocorrelation, block-to-block variation
and phase coverage; use effective sample count, not packet count. Vary only the
optional ranging opportunities within allowed slack, never the precision train.
Changing channels can reveal bias but does not make that bias white noise.

Start with 16 complete exchanges per granted batch. Study 32–64 only if they fit
the same resource limits, or combine separate batches when stationary and in
the same calibration/scope. Retain every attempted exchange and failure reason.
Report signed raw and calibrated means, spread, channel populations and tails.
Do not clip negative noisy ranges to zero, select the shortest observation as
truth, or discard large residuals merely because they are inconvenient.

A persistent 10 ns residual means 3.00 m error; 30 ns means 8.99 m regardless of
sample count. An uncertainty model needs separate random, calibration,
directional-asymmetry, channel/NLOS and motion terms. Unknown terms stay unknown.

Use the requested **20 ns GNSS RMS** as a planning assumption for shadow PPS.
The [UC6580 product page](https://en.unicorecomm.com/products/dual-band-gps-chip-uc6580/)
states 20 ns; [datasheet R1.7](https://en.unicore.com/uploads/file/UC6580x-00_Datasheet_EN_R1.7.pdf)
states 5 ns. The historical review already records this discrepancy; neither is
this board's calibrated uncertainty. Do not substitute the more attractive value.

Two independent 20 ns references would contribute 28.28 ns RMS to a difference
(8.48 m of one-way light travel). Adding two independent 16 MHz PPS-capture
quantization errors gives 38.09 ns under that simplified model. Shared satellite
errors, PPS filtering, cable delays and temporal correlation change the budget.
Do not divide the full GNSS specification by the square root of PPS count or
subtract it from measured RMS without evidence of independence.

GNSS phase error is **not an unavoidable 20 ns floor on local RTT**: local
intervals cancel clock origins. Shadow PPS is the separate witness for whether
the proposed correction improves delivered time. It must not train the ranging
fit or the RC BLE clock during its validation window.

## Admission, scheduling and bounded work

Use a proposed capability-negotiated request/grant/report control extension;
do not silently change frozen GR3/RC7 layouts. A request grants no radio or
timing authority. The GM owns a single session token bound to authenticated
peer, both boots, root/source epoch, parent, calibration version and expiry.

Reuse an already open authenticated control exchange only if ranging fits its
original lifetime without delaying required messages. Otherwise a new short
ACL requires demonstrated spare controller/host capacity **after** reserving
normal join/recovery capacity. If three persistent links plus the required
reservation consume all four resources, return/defer BUSY. Increasing the pool
or borrowing the reservation is a separate resource-design decision.

An idle RC can announce a bounded ranging intent in a versioned discovery
extension, observed through existing GM scan opportunities, and await a signed
grant. This mailbox is proposed, not present firmware. Do not connect repeatedly
to ask for a slot, add continuous scanning or alter timing packets to carry it.
Authentication checks and pending intents must themselves be bounded and
rate-limited. At most one pending intent per admitted identity; cap the roster,
expire requests and use fair oldest-eligible selection.

Priority order: source/PPS capture and precision RX/TX; existing management,
HOT/election/renewal/recovery, USB and other required work; optional ranging.
Both endpoints must pass admission: an RC's downstream TX obligations are also
protected. Initial proposed limits, subject to controller proof:

| Limit | Proposed behavior |
| --- | --- |
| Eligibility | Same valid parent/root and stable lock for at least 60 s; qualified serving GM; no pending transition, recovery, overdue control work or queue pressure |
| Refresh | First request after eligibility, staggered by identity/session; later nominal 300 s with bounded jitter; no synchronized five-minute burst |
| Global service | One client token including teardown; no concurrent ranging exchange |
| Batch | Target 16 complete triples; at most 32 attempted triples; stop new work at 2 s from grant, including setup/report time |
| Exchange | At most 125 ms from first TX through final RX; any missing/ambiguous event invalidates it |
| RF reservations | At most 64 ms aggregate local reservation per batch, and 4 ms in any 62.5 ms frame at either endpoint, including setup, RX waiting, retries and reporting |
| Extra connection starts | Also cap ranging globally at two starts per rolling 60 s and at least 20 s apart; preserve any stricter existing owner limit |
| Expiry proximity | Do not grant if required renewal/service is due within batch plus actual bounded teardown plus a measured guard; no hard-coded grace extension |
| Contention or preemption | Cancel the affected ranging attempt; retain the failure; return to normal duties; no catch-up burst |

The RF caps are ceilings, not proof of free airtime. Before every reservation,
fit the complete worst-case event plus prepare/retune/receive-widening/abort
guards between actual protected controller reservations. Nominal 15.625 ms mesh
slots do not prove those gaps are available. If the controller cannot enforce
this, do not enable ranging. All setup, empty ACL events and automatic LL
retransmissions must count; a host-level sleep cannot enforce this budget.

The existing [precision classifier](../../firmware/t096/tools/precision_schedule.py)
classifies periodic advertisers/synchronizers as precision, and
[capture hooks](../../firmware/t096/tools/radio_capture_hooks.py) protect them.
A new ranging PA train could therefore accidentally receive timing priority.
Prefer a separately classified low-priority ACL implementation with new capture
hooks. Do not reuse the precision classification or manipulate RADIO/TIMER/PPI
behind the controller owner. No cryptography, fitting, allocation or console
output in capture ISRs; fixed bounded queues and worker processing only.

At the 2 s batch deadline stop producing new work, then use the existing bounded
controller drain. Do not pretend a connected object disappeared at the deadline;
retain ownership/token until actual closure. Planned events must obey the RF
budget during teardown too. Preserve mandatory cleanup and watchdog behavior.

The extra-start cap permits at most ten newly connected clients per five minutes
even before other duties and failed attempts. A busy four-GM configuration may
have no capacity. Expose DEFERRED and measurement age; a strict five-minute SLA
for an arbitrary client population is incompatible with these priorities.

## Capture identity and result lifecycle

Extend the supported controller capture path to bind each TX/RX to the exact
ACL PDU, connection generation, event counter, packet ordinal, direction,
channel, PHY and ranging nonce/sequence. LL retransmissions and duplicate
reception require exact attempt association; ambiguous tuples are unusable.
CRC plus authenticated transcript/report is required. A MAC authenticates the
peer and data, not physical distance or resistance to relay attacks.

Capture ticks and their domains/epochs remain separate from derived nanoseconds.
The 32-bit timer wraps after 268.435456 s, less than five minutes. Store extended
ticks and refuse ambiguous wraps/restarts. A session token must not survive a
boot, source/parent change or capture-domain reset. Check product overflow in
the DS numerator and use signed arithmetic for noisy negative estimates.

Result records should include requested/attempted/complete/invalid/preempted
counts, six event tuples, actual intervals, raw and calibrated delay, channel
statistics, sample span/effective count, bias-profile ID, uncertainty components,
last success, expiry, deferral reason and whether a correction was applied.
No saved result extends clock permission, source validity or authentication.

## Applying the correction downstream

Separate device/reference calibration from propagation. The existing
`+10543 ns` T096 correction is an aggregate historical PPS-derived adjustment,
not a measurement of RF distance or a new ToF calibration. W12's constants have
their own scope. Neither can simply be subtracted from a new RTT estimate.

At RX, corrected source time conceptually equals physical source TX time **plus**
forward propagation and the applicable endpoint correction. The existing
[boundary discipline](../../firmware/t096/src/rc_boundary.c) already has separate
`link_ns` and `rx_tx_bias_ns` terms; the active dynamic caller currently supplies
the existing direct phase correction. Integrate a new bounded link term through
every applicable delivered map and handoff, not by rewriting raw RX/TX captures.

For an old aggregate calibration obtained at distance `d0`, preserve that
baseline and add only the independently measured change in propagation from
`d0`. If `d0` or its bias decomposition is unknown, first establish a new frozen
baseline. Adding full ToF on top of a calibration already containing it would
double count delay.

Apply an accepted change causally with a bounded phase slew and new map
generation; retain immutable historical maps and actual physical TX timestamps.
In `GM → RC1 → RC2`, RC1's corrected map already carries the GM–RC1 correction
into its TX time. RC2 adds only its own RC1–RC2 link delay. Do not add root
distance at every hop. A direct GM–RC2 geometric range does not determine the
two-hop path delay. Initial GM-only service corrects only direct-GM links;
per-hop ranging at relay parents requires a later capability/scheduling design.

Store the estimate in RAM. On parent/root/boot/epoch/profile change, withdraw its
applicability immediately through the normal map-transition rules. As an initial
stationary experiment, expire application eligibility at 600 s or earlier if
calibration/clock scope expires. Never silently keep a stale compensation:
transition back to the baseline with the same monotonicity/reacquisition rules,
and mark the interval unqualified. Motion requires earlier invalidation; an
assumed speed bound `v` adds `v*age/c` delay uncertainty. At 1 m/s, five minutes
permits 300 m / roughly 1 microsecond of change. Without a motion bound, automatic
five-minute compensation is suitable only for a declared stationary experiment.

## Validation and implementation gates

1. **Offline arithmetic and scheduler proof.** Exercise unequal delays, clock
   skew/drift, correlated quantization, counter wrap, negative estimates,
   overflow, authentication/replay, duplicates, missing reports and epoch
   changes. Fault-inject high-priority work at every ranging state, including
   setup and drain. Prove resource reservations and no precision-priority
   inheritance. No hardware acceptance follows from these checks.
2. **Timestamp/calibration study.** With separately scoped hardware authorization,
   bind exact devices/images and begin with T096 peers. Survey antenna separation
   at several LOS distances, for example 1, 10, 30, 60 and 100 m where reception
   permits. Hold PHY/antenna orientation fixed initially. Freeze a training bias
   calibration, then use held-out distances, repeats, reversed roles, channels,
   temperatures, RSSI regimes and obstructed paths. Cable/attenuator tests need
   known group delays. Distance truth is surveyed separation, not GNSS positions
   or shadow phase. First establish slope near 3.33564 ns/m and stable intercept.
3. **Observe without correction.** Alternate bounded baseline and ranging windows
   with the same load. Keep GNSS as witness only. Use the existing shadow startup
   semantics and raw signed records; keep startup/missing/invalid opportunities.
   Monitor PA gaps, map expiry, PPS residuals, HOT continuity, join/renewal latency,
   USB gaps, queue maxima and faults. The shared GM reference creates covariance;
   report paired changes and block statistics, not independent-sample fiction.
4. **Held-out correction test.** Replay a frozen delay correction offline first,
   then, under separate hardware scope, compare application OFF/ON at RC and
   downstream shadow-PPS receivers. Freeze calibration before the scored window.
   Any pilot priority inversion, controller fault, attributable precision loss
   or missed mandatory service stops the treatment; preserve the failed window.
   Demonstrate lower held-out phase bias/error without worse tails or availability,
   within a declared uncertainty budget, before enabling automatic compensation.

The distance objective and timing objective need separate acceptance results.
An apparent range can fail geometric validation yet expose a stable effective
link delay; a plausible range can still worsen clock phase through asymmetry.
Shadow PPS validates relative delivered-map behavior. Calibrated physical output
and absolute UTC accuracy require their own independent measurement.

Implementation order: portable estimator/result schema; controller capture and
bounded owner; authenticated grant protocol; observation-only pilot; calibrated
map integration; other RC target qualification. No firmware, flash, hardware
configuration or existing precision wire format changed in this research task.

## Reproduce this review

```sh
python3 tools/analyze_round_trip_ranging.py
python3 tools/verify_v14.py
python3 tools/verify_w12_utc_results.py
```

The new standard-library script checks synthetic affine-clock cases, estimator
derivatives, an ideal quantization ensemble and a repeated-phase counterexample.
It computes the tables and PPS planning budget; it contains no measured ranging
data or hardware access. The two existing verifiers retain their own evidence
scope. External sources were consulted on 29 October 2026; no third-party text
or private raw laboratory records were imported.
