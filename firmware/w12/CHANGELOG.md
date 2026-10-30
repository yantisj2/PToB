# W12 changelog

## 0.2.21 — four-second shutdown away from Clock

- Hold GPIO0 for four seconds on Precision, LoRa or another non-clock page to
  blank the display, complete the original bounded application drain and enter
  deep sleep after a stable release. A fresh button press wakes by rebooting.
- Preserve the Clock-page 1.5-second relay toggle. LoRa cycles at 1.5 seconds
  and a continued four-second hold shuts down. Consume the shutdown release;
  reject boot-held, bouncing, changed-page and discontinuous-clock gestures.
- Hold the reviewed OLED/I2C, optional GNSS and LoRa off levels through sleep;
  restore those pads before normal initialization. Failures refuse sleep.
- Exercise actual button, drain/release gate and power API paths under host
  sanitizers across GNSS/LoRa selections. Native preparation remains separate
  from physical sleep current, wake and runtime acceptance.

## 0.2.20 — fit the complete GNSS/TZ application

- Compile the W12 application component with effective `-Os`, preserving SDK
  controller flags, assertions and the original 1 MiB application ceiling.
- Preserve explicit console call boundaries for retained GCC stack accounting.
  Retain GNSS, TZ, fine capture, LoRa, RC9 and per-device calibration. Build
  receipts disclose the policy; native inspection checks every main-component
  compile command and the SDK assertion/optimization configuration.
- Preserve the failed GNSS-enabled 0.2.19 size check; native size and inspection
  results are offline preparation, not hardware acceptance.

## 0.2.19 — ordered dynamic TZ updates

- Keep the independent RC9 control service at212 bytes in both client and
  server callbacks while TZ-enabled RC7 uses216 bytes; test both live owners.

- Follow the T096 0.15.206 amendment on the unchanged coarse wire. Accept
  changed pairs and explicit unknown from the fresh authenticated current
  parent; fence metadata on parent/path replacement.
- Order direct ANN by GM sequence and relay capability by its existing
  admission challenge. Refuse older or same-token conflicting snapshots and
  prevent a delayed JOIN from replacing a later ANN.
- Consume current-parent capability metadata during protected following.
  Increment the publisher's existing challenge when its advertised pair changes.
- Preserve the three-period lease, UTC timing, physical fitting, controller
  drains and calibration. Add actual decoder/owner update and withdrawal tests.

## 0.2.18 — coarse TZ and local clock display

- Consume GM7 v4, direct JOIN v5 and RC capability/control v8 using the selected
  T096 TZ codecs. Historical codecs remain explicit offline entry points.
- Learn the authenticated selected parent's fixed mesh pair; renew its 750 ms
  direct / 300 ms relay presence lease independently of precision fitting.
- Forward the current pair in RC coarse advertisements/admission. Keep timing
  payloads, clock fitting, source permission and per-device calibration unchanged.
- Show local digital/analog time and the current zone label; expire to UTC.
  Preserve UTC on LoRa/RC9 and diagnostics. N30's pixel cache respects TZ expiry.
- Add production-codec/vector, native owner and actual-renderer regressions.
  Offline checks do not establish hardware or UTC/output qualification.

## 0.2.17 — announced GM successor recovery

- Share N30's authenticated GM7 revision-3 successor consumer and bounded
  recovery preference, preserving exact root/boot/epoch/train and nominee boot.
- Withdraw old timing on matching WITHDRAW, the immutable YIELD boundary or a
  valid next-term successor announcement. Sustained source loss retains the
  existing expiry policy and tries the announced successor first.
- Preserve fresh admission, physical timing acquisition, actual PA/ACL drain,
  explicit pins, relay-only policy, GNSS profiles and per-device offsets.
- Add bounded passive control reception between actual timing events. Native
  regressions and builds remain separate from physical failover qualification.

## 0.2.16 — exact PA terminate acknowledgement

- Retire only the exact active PA owner after the pinned SDK returns successful
  HCI termination or confirms the handle is absent. Its queued EDONE remains
  a notification; a stale callback cannot close a replacement capture epoch.
- Preserve actual pending-create cancellation, original drain and BLE50 quiet
  deadlines. SDK errors and late returns remain faults.
- Distinguish terminate SDK error, late return and unclosed owner expiry in the
  existing compact fault-site byte; retain bounded call-start/return traces.
- Native regressions cover delayed/inline notifications, reused handles, SDK
  pool pressure, actual RC9-to-direct recovery and subsequent PA admission.
  No hardware acceptance is implied.

## 0.2.15 — replacement RC7 lineage cache fence

- Retire old advisory trials, locators and retry scheduling when an authenticated
  RC7 response replaces the root, boot, source epoch or full publisher path.
- Preserve that current response/request, exact ACL and original PA drain so
  the replacement binds without reusing an old model. Unchanged-path renewal
  retains its current policy; explicit operator pins and calibration survive.
- Bind the freshly authenticated relay role explicitly after locator retirement,
  keeping its new lock trial and failure deferral in the relay category.
- Freeze each pending PA owner's address through cancellation. A late old
  establishment drains against that original identity; fenced reports cannot
  re-enter the receiver while cleanup is queued.
- Actual native regressions reproduce stale GM deferral and admission backoff
  before the fix, then cover replacement lineage, train changes, pins, unchanged
  renewal and normal RC7 return from an active RC9 client.

## 0.2.14 — bounded RC9 server return to normal BLE

- Qualified normal BLE takes the local display immediately, independently of
  still-live downstream RC9 child leases.
- Stop new RC9 capability admissions and refuse queued requests/renewals once
  normal BLE qualifies. Existing child TIME and LoRa reception retain their
  original lease and physical-source deadlines; the publisher returns to RC7
  after those leases expire and its actual owners drain.
- Resume fallback admission if normal BLE loses qualification. Native tests
  retain the queued-renewal race, late connection quarantine, original drain,
  expiry boundary and actual local UI selection with an active child lease.

## 0.2.13 — fallback transition cache fencing

- Clear old normal-source selection/trial history on authenticated RC9 takeover.
- Fence the prior advisory caches even if a newly authenticated direct root
  arrives after the earlier scope was already withdrawn.
- Reset stale fallback retry scheduling; preserve explicit pins, source/lease
  expiry and real controller drain deadlines. Actual native regressions cover
  stale-history takeover and direct admission from an empty scope.

## 0.2.12 — leader recovery

- Fence expired or changed source state and discard old leader selection caches,
  pending fine associations and queued packets before fresh admission.
- Preserve explicit pins, enrollment, continuous counter and per-device offsets.
- Bound recovery admission credits to one per five seconds; keep authenticated
  deadlines and actual controller drain ownership unchanged.
- Handle exact duplicate incoming ACL notifications idempotently and quarantine
  valid capacity overlaps without granting service or faulting normal reception.
- Offline regressions cover loss boundaries, new-leader admission, pins, repeated
  reset throttling and duplicate/overlapping connections. These do not qualify RF
  failover latency, UTC accuracy or precision.

## 0.2.11 — normal BLE recovery from RC9 fallback

- AUTO reuses bounded 1.5 s / 30 s passive discovery and existing admission
  budgets instead of treating a six-minute fallback lease as exclusive selection.
- Retain fallback through NO_SOURCE and indirect root hints; successful fresh
  normal admission fences RC9 and drains its old periodic owner. Fallback never
  trains the normal clock. Explicit pins and original deadlines remain intact.
- Add native regressions for valid/expired fallback, scans, hints and pins;
  preserve GNSS isolation, fine capture and provisional timing adjustments.

## 0.2.10 — four-observation programmed-clock estimate

- W12 relay estimates average all four validated projections of the same
  programmed packet. Missing observations or spread exceeding2µs refuse the
  estimate; no residual is trimmed and no measured RF bound is asserted.
  Keep the existing callback/age/channel/source/expected-bank checks.
- Retain all four inputs in version4 sampled TX path records and label the
  programmed-clock mean separately from earlier single-bracket estimates.
  Preserve the previous behavior and allocation outside W12.
- Revise the provisional relay residual to +4,026 ns from separate narrow-map
  observations. The receive term remains +7,671 ns. Applying these terms to
  the changed estimator and other units/profiles remains an unmeasured transfer
  until a fresh hardware population is collected. No GNSS feedback trains BLE.
- Exercise complete/missing/inconsistent reads, integer rounding, unsigned
  overflow boundaries and version4 diagnostic capacity under ASan/UBSan.

## 0.2.9 — narrow fine-counter Bluetooth clock aperture

- Enclose the unchanged pinned ROM Bluetooth-clock read with two raw,
  driver-owned CURRENT latches. Validate owners before sampling and perform
  counter extension and coordinate conversion after the aperture. Keep the
  aperture and its fixed reader in instruction RAM.
- Preserve the production counter origin, missing/stopped-latch refusals,
  capture ownership, packet association, PHY checks and timing authority.
  No new RF timestamp or uncertainty bound is asserted. Existing phase terms
  remain disclosed and need a new hardware comparison after this change.
- Exercise wrap crossings, unavailable readers, invalid Bluetooth coordinates,
  missing completion, stale IRQ flags and stopped owners under ASan/UBSan.


## 0.2.8 — provisional relative-PPS phase offsets

- Apply +7,671 ns once to delivered W12 G1 clock copies, including retained
  estimates and relay input. Keep raw captures, frequency/phase fitting,
  causal quality, source envelopes and permission deadlines unchanged.
- Add +3,030 ns to the nominal RC7 ADDRESS projection. An existing explicit
  aggregate total supersedes this default residual. This is a relative-PPS
  combined-path adjustment measured with a T096 receiver's existing correction,
  not an independently identified TX or RX calibration. Transfer to other W12
  units and profiles is provisional and has not been measured there.
- Bind offsets into build receipts and disclose delivered receive adjustment
  in CRC status, PPS logs and version3 TX path companions. Preserve raw-relative
  PPS phase, corrected phase, jitter and unknown RF/UTC bounds separately.
- Check both clock backends for single application, unchanged raw fit/finalize,
  delivery/relay consistency, explicit-total precedence and arithmetic refusal.


## 0.2.7 — causal shadow clock drift prediction

- GNSS-enabled BLE50 and BLE100 builds fit bounded curvature after 32 consecutive captured PPS points;
  warm-up retains linear prediction. Non-GNSS profiles never enable shadow PPS. Use a centered integer
  fit and invert only the short relative counter interval, keeping UTC epochs
  integer. Limit acceleration to 20 ppb/s and retain prior-model availability,
  three-second expiry, missing-PPS resets and explicit invalid predictions.
- Label raw and mapped predictor methods in capture diagnostics. Held-out
  prediction errors remain separate from fitted residuals, BLE-relative phase,
  output UTC phase and physical accuracy. GNSS never trains the BLE clock.
- Add analytic positive/negative oscillator-drift checks for both counter rates,
  legacy controls, availability/expiry and excessive-acceleration refusal.
  Synthetic checks and saved-data replay are not hardware qualification.

## 0.2.6 — fine-counter shadow PPS mapping

- Enclose the existing GNSS group0 software capture with group1 CURRENT
  latches in the production fine-counter domain. Move counter extension,
  association checks and coordinate conversion outside the sampled aperture.
  Keep the bounded capture pair in instruction RAM and validate owners before
  the first latch, avoiding repeated owner checks between physical samples.
- Validate the GNSS channel, group, timer frequency and enabled state. Missing
  latches fail the map without substituting GPTimer time or widening its gates.
  Keep prior-anchor availability, expiry, mapping rejection and physical PPS
  coverage explicit. Label this population `FINE_COUNTER_BRACKET`.
- Exercise exact helper ordering, invalid channels, stopped latches and repeated
  hardware wraps with synthetic ASan/UBSan checks. These do not certify physical
  timing. Retain GNSS input-only wiring, no receiver aid, unchanged BLE fitting,
  source permissions, LoRa ownership and app-only maintenance behavior.

## 0.2.3 — source-aware RMS and AUTO recovery

- Show current LoRa link RMS on the Clock and Precision pages when LoRa supplies
  time. Label it LINK RMS and show fit warm-up or WAIT RX explicitly. The LoRa
  page labels its own current link RMS; stale fits cannot appear as live values.
- Keep BLE steering/lock diagnostics specific to BLE. LoRa's link residual is
  neither BLE synchronization RMS nor an independently measured UTC bound.
- Separate current coarse UTC permission from the original precision-model
  forecast horizon in AUTO's BLE-lock input. Short coarse renewals no longer
  reset continuous recovery; expired coarse permission, model/replay deadlines,
  quality gates and the180s recovery requirement remain enforced.
- Add actual native UI-source selection and short-permission boundary checks.

## 0.2.2 — independent LoRa timestamp reads

- Follow N30's independent LoRa-reader fix: remove BLE association/fence work
  from the LR2021 receive timestamp path. W12 retains MCPWM1 CH1 and its verified
  IRQ route, with a separate counter epoch/extension and constant-work reader.
  GNSS continues to own group0; no N30 GPIO or calibration is imported.
- Poll the independent counter while RX is active, including packet silence and
  hardware wraps. Unavailable counters fail closed without substituting BLE time.
- Expose the actual AUTO lock input and remaining transition time in the bounded
  status response. Keep the 180 s loss/recovery policy, 300 s no-time watchdog,
  source expiry, RX-only guard and original maintenance deadlines.
- Exercise independent origins, six-minute wraps/silence, BLE-reader separation,
  counter failure and peripheral drain with synthetic ASan/UBSan checks. These
  checks do not establish physical timing accuracy or hardware acceptance.

## 0.2.1 — W12 radio wiring correction

- Use LR2021 DIO8 / ESP32 GPIO14 for RX_DONE capture. DIO11 controls the
  GC1109 shutdown input and cannot also serve as the interrupt output.
- Enable the sub-GHz LNA with DIO11 high and DIO9/DIO10 low during RX, while
  keeping DIO5/DIO6 and the2.4GHz power rail low. Check each DIO command and
  allow100ms bounded front-end rail settling before configuration.
- Test authenticated wrong-emitter refusal separately from packet reception;
  transmitter enrollment remains an explicit exact-factory build input.
- Preserve fine BLE clock, GNSS group0/input wiring, RX-only TX guard, existing
  source deadlines, watchdog and maintenance drain.

## 0.2.0 — dual timing and sync-loss recovery

- Add the 300 s continuous no-synchronized-time restart watchdog; current valid
  BLE or RC8 UTC clears it, with original expiry and OFF/maintenance fences.
- Port N30's 180 s AUTO/KEEP ON/KEEP OFF policy and authenticated RC8 receiver to
  W12 LR2021. Share fine counter CH1 without taking GNSS group 0 resources.
- Add Precision and LoRa OLED pages, one-shot LoRa RX hold control and bounded
  ticket-bound commands; preserve Clock-page relay control and GNSS indicators.
- Pin public RadioLib 7.8.1, retain its source/license with build artifacts, and
  bind exact transmitter/enrollment to runtime identity. Receiver TX is blocked.
- Keep RF/UTC calibration unqualified; distinguish PDU191 current GR3/RC7 fine
  capture from legacy PDU159 descriptor phase and internal SYNC RMS.

## 0.1.23 — 23 October 2026

- Add an opt-in BLE100 fine-clock test profile using a continuous 80 MHz MCPWM1
  origin and a W12-specific RX-only GPIO16 route. Preserve group 0 GNSS capture,
  use one coordinate for all clock consumers and fail closed on counter faults.
- Port N30's dense, capture-frozen fitting and 1 ppm phase slew strictly below
  2 µs. Retain lock, causal-quality, source-expiry and downstream gates. Supported
  relay packets use the experimental RW falling phase convention; direct GR3
  remains descriptor-only. No RF/UTC calibration or N30 measured result transfers.
- Bind the fine selection, factory header, ABI and source closure into build
  receipts/runtime identity. Add bounded capture counters and actual-owner,
  counter, slew, shutdown and selected stack checks. Retain legacy defaults,
  LoRa OFF and separate hardware installation authority.

Copyright (c) 2026 Jonathan Yantis. MIT.

## 0.1.22 — 19 October 2026

Add the optional `--gnss-fast-map-diagnostic` build treatment for the verified
W12 GNSS profile. It brackets the supported MCPWM software capture with the
existing validated GPTimer latch. Physical PPS capture, midpoint conversion,
the full read enclosure and prior-anchor availability remain unchanged.
Records identify this treatment separately; a narrower bracket must be measured
on the installed image and does not establish its midpoint bias or UTC accuracy.

The option defaults off. Native identity, compiler flags, task/core settings,
private driver ABI and selected stack evidence bind the chosen treatment.
BLE reception, clock discipline, source permission and relay output retain
their existing behavior. This is a measurement change, with no offset correction
or BLE dropout repair claimed.

## 0.1.21 — 18 October 2026

Add a read-only production G1 PPS diagnostic on the explicit GNSS profile.
Capture records keep the physical raw/mapped PPS edge, prior actuator model,
root/boot/source/train scope, original permission and model availability,
empirical GNSS reference, and raw versus output-corrected modulo phase.
The existing strict R2 PPS metric remains separate. Later models, uncertain
activation, expired permission and unmapped edges remain unavailable.

Keep the existing source, replay, lock, controller and output gates, GNSS input
configuration and disabled GNSS aiding. Selected native stack evidence includes
the new snapshot and PPS callback paths with the original reserve. These records
support component measurements; they do not identify an RF timing bias, repair
BLE continuity or qualify independent UTC accuracy.

## 0.1.20 — 18 October 2026

Normalize every BLE100 estimated W12 publisher from its programmed packet-start
reference to the nominal BLE1M ADDRESS endpoint (800 local 20 MHz ticks, nominal
40 microseconds). Project both endpoints through the frozen clock model;
unsupported PHYs, future/overflowing endpoints and original source/age expiry
still refuse publication. This repairs a packet-reference mismatch, with actual
schedule-to-RF delay and absolute precision still unknown. BLE50 has no publisher.

Keep schema-1 aggregate profiles as totals relative to the original programmed
start. Decompose the nominal ADDRESS component and residual without adding the
reference change twice. Version-2 TX-path records expose raw, nominal ADDRESS
and emitted times plus their components. Existing profile provenance and scope,
GNSS inputs, receive fitting and timing/permission gates are preserved. See
[the reference explanation](../../docs/w12/tx-reference.md).

Production publisher-to-receiver/PPS regressions cover multiple identities,
RC2 forwarding, default and signed legacy profiles, slope/slew, PHY/source/age
refusals and counter rollover. The old source fails the same numerical case.
Host and native permission tests are synthetic; per-device hardware and
remaining relative offset/jitter need separate observations.

## 0.1.19 — 18 October 2026

- Hold an established downstream train and management discovery for 30 seconds
  through loss of parent lock or usable timing. Previously one quality dip
  immediately stopped PA, discarded child demand and changed the train.
- Withdraw corrections, frozen timing snapshots and READY immediately. Continue
  canonical count0 packets; only fresh accepted same-scope corrections can
  restore timing. Retain original child lease deadlines and require two seconds
  of observed recovery to reset the loss episode, so flicker cannot extend it.
- Keep startup lock, scope changes, explicit stop, faults, child expiry and
  actual controller/ACL drains separate. Add production native regressions for
  short loss, recovery, hold expiry, flapping, pending management, child expiry,
  scope/stop and unchanged two-second ACL drain deadlines. Hardware field
  behavior remains a separate check; estimated TX and UTC bounds stay unknown.

## 0.1.18 — 18 October 2026

- Separate direct BLE100 authentication from replay-clock maintenance. Renew
  after 225–240 seconds with a fixed six-minute session, reducing repeated
  uncertainty-triggered JOINs while matched GR3 timing continues.
- Maintain the coarse anchor only from a newly matched physical correction
  within the previous causal interval and the exact admitted scope. Retain
  initial JOIN uncertainty, capture enclosure, signed source/model expiry,
  the 250 ms uncertainty limit and unchanged owner/drain/retry bounds.
  Duplicate tuples, polling and held predictions cannot extend authentication.
- Add actual matcher and native receive-path regressions through renewal and
  hard expiry, legacy-cadence comparisons and stale/scope/source refusal cases.
  BLE50 and RC7 lease policies remain unchanged; hardware outcomes are separate.

## 0.1.17 — 18 October 2026

- Correct the generated profile macro consumed by the production config.
  Version 0.1.16 declared an enabled profile in its build inputs while the
  mismatched macro selected zero in the compiled config. Its strict console
  manifest guard refused that discrepancy; no applied correction is inferred.
- Execute builder-generated headers through the production config and emitted
  RC7/receiver timestamp path for default zero and signed enabled profiles.
  Compile-time assertions also bind every enabled generated field to the
  production config, and the old macro fails the regression. The offset scope,
  raw records, source/precision gates and unknown bounds remain unchanged.

## 0.1.16 — 18 October 2026

- Add an explicit per-device, build-time provisional aggregate-path offset to
  emitted RC7 estimated timestamps. It defaults to zero; enablement requires
  BLE100 estimated relay, the exact publisher and direct-root identity, boot
  and training source epoch, plus the existing valid BLE1M receive reference.
  An explicit opt-in permits unmeasured transfer across fresh epochs of that
  same root boot without transferring authority or a jitter guarantee. The
  multicast correction affects every receiver using that publisher.
- Freeze the offset with its publication and apply it once after the raw clock
  projection. Both raw and adjusted timestamps must remain inside the original
  signed source envelope. Raw captures, clock fitting, proof/hold/precision,
  source expiry, slots, parent selection and ownership deadlines are unchanged.
- Bind the profile, training receiver/source/correction and evidence hashes
  into the generated device header and build identity. Status discloses its
  configured identity prefix; capture-only companion records retain the exact
  raw/emitted tuple, effective profile, offset and capture identity without
  enlarging the fixed report payload. RF and UTC bounds remain unknown.
- Production sign/zero/scope/frozen-publication/expiry tests preserve unknown
  precision. Old unadjusted and double-application branches fail the sign
  regression. Residual jitter remains a separate observed statistic; applying
  an offset does not reduce jitter or identify RX versus TX latency.

## 0.1.15 — 18 October 2026

- Treat HCI Connection Parameters rejection (571) as a failed admission only
  for the exact normalized production request while it still owns an unadopted
  pending procedure. Retain the refusal, peer failure and existing retry/rate
  limits. Accepted ACLs, other errors, malformed parameters and other API sites
  retain their fault handling. Source/map expiry and drain deadlines do not grow.
- Add capture-only successful selected-TX observations, sampled every sixteenth
  event with a maximum of 60 per bounded capture. Records retain the frozen
  publication, source and map, estimated programmed tick, controller clock
  bracket and emitted correction. They explicitly report actual=0 and an unknown
  RF bound; no RF timing, calibration or protocol rule changes.
- Production regressions cover refusal containment, accepted-owner and other
  error controls, late callback drain at its original deadline, frozen TX tuples
  and capture budgets. Old production branches fail the corresponding negative
  controls. The underlying controller refusal and prior incoming-owner fault
  remain unresolved hardware causes; these changes do not qualify precision.

## 0.1.14 — 18 October 2026

- Install outgoing adopted-ACL termination ownership and its original deadline
  before the yielding public SDK call. A delayed CONNECT during that call may
  acknowledge the same exact terminating owner; it cannot restart GATT or extend
  the drain. Wrong handles remain faults and stale callbacks remain refused.
- Add fixed incoming relay-owner observation records for descriptor lookup,
  roles, handles, admission decisions, state/gate masks and original drain
  deadlines. They use the existing bounded diagnostic queue and main renderer;
  the ordinary status frame and source/quality/slot/ownership rules are unchanged.
- Reproduce the outgoing SDK-yield race in the production native adapter and
  retain missing-disconnect, wrong-handle and stale-callback controls. This race
  is separate from the observed incoming-owner site-2 fault; no incoming cause
  or hardware stability repair is claimed by these source changes.

## 0.1.13 — 17 October 2026

- Bound automatic GM precision trials to a full minute before seeking relays,
  including when authenticated packet delivery and FOLLOW remain healthy.
  Same-parent renewals and source changes cannot restart the trial; two observed
  seconds of the existing relative-lock/hold gate establish sustained success.
- Release discovery suppression after a failed trial without discarding a valid
  timing map. Prefer a fresh relay even when the GM has stronger RSSI. Defer a
  failed parent for five minutes so it cannot immediately reclaim a new trial;
  keep seeking if no fresh alternative exists. Explicit pins remain effective.
- Use the fresh lock-report time for subsequent advisory history checks; an
  older worker timestamp must not bypass failed-GM deferral during a relay lock.
- Preserve fast acquisition, fine hold, all authority/quality/expiry gates and
  controller/drain bounds. Add policy and production scheduling regressions for
  healthy delivery without lock, exact dwell, flicker, renewals, relay selection,
  discovery and retained lock. Synthetic tests do not qualify physical failover.

## 0.1.12 — 17 October 2026

- Clear acquisition phase continuously with a proportional slew capped at
  50 ppm. Within 1 microsecond, retain the existing 1 ppm fine slew. The
  previous fixed 1 ppm actuator could accumulate tens of microseconds while
  the frequency fit warmed up, then spend tens more seconds
  removing that backlog. No timestamp step or oscillator adjustment is added.
- Use the same phase law in delivered maps, completion rebasing, stale-fit
  continuation and relative forecasts. Start channel-aware fits after two seconds and at least eight repeated-channel
  differences. Acquire points at 62.5 ms until the first fit, then use the
  original 250 ms spacing, growing the same 64-point history toward its normal sixteen-second
  span. Retain the rate/scatter guards, all sixteen seconds of causal error history, source/replay/model deadlines,
  and lock/hold thresholds. Acquisition tails still delay lock until they age
  out; a faster actuator does not establish calibrated precision.
- Add both-sign acquisition/settled-hold regressions and exact fine-slew checks.

## 0.1.11 — 17 October 2026

- Separate direct BLE100 control from precision processing deadlines. Allow
  control replies up to one second in the queue, four seconds for initiation,
  500 ms connection supervision and GATT discovery through the original
  five-second authenticated exchange. Reject processing after that original
  exchange expires, even if the callback arrived earlier. Nonce, MAC, sequence,
  source permission and coarse-bootstrap uncertainty checks remain unchanged;
  a delayed membership reply supplies no precision measurement.
- Allow six warm direct BLE100 starts per rolling minute, with
  five-second spacing and 5–10-second failure backoff. Retain two starts for
  BLE50/RC7 and preserve history
  across parent changes. Deadline-aware direct retry jitter now reserves five
  seconds for the longer attempt and scheduling slack. This remains best effort
  within a finite replay lifetime.
- Preserve narrow warm initiation, precision-first queue drains, 50 ms precision
  worker age, 250 ms physical association, original signed/model expiry and
  relative-lock quality gates. Cancellation, stop, fault and update drain keep
  their original bounds and require actual completion.
- Add production-boundary regressions for delayed control versus stale precision,
  expired exchanges, longer control discovery, legacy/RC7 bounds and stop expiry.

## 0.1.10 — 17 October 2026

- Start direct BLE100 replay refresh at 100 ms uncertainty and urgency at
  125 ms. A failed transport followed by an authenticated unusable time reply
  can consume the prior 50-second reserve through backoff and the rolling
  start limit. Earlier refresh leaves about 75 seconds at 2000 ppm. Retain
  two warm starts per 60 seconds and every hard replay/source/model deadline.
  Repeated failures can still exhaust
  that finite context; JOIN acceptance alone does not renew timing authority.
- Bound warm failure jitter by the remaining replay time only for a reserved,
  still-live current direct precision stream. Keep the 20-second floor and
  40-second maximum, reserving three seconds for a two-second attempt and
  worker/closure slack where possible. Rolling start eligibility remains a
  separate gate; missing authority, RC7 and cold retry behavior stay unchanged.
- Exercise the combined retry policies with transport failure, unusable time
  and a third attempt. The former 150 ms policy fails the same bounded
  scenario. Keep BLE50's existing 100/125 ms thresholds and RC7 renewal intact.

## 0.1.9 — 17 October 2026

- Reserve a current BLE100 precision receiver during relative-quality dips,
  using its authenticated parent/path, live source/model/replay, working clock
  and physical delivery within 250 ms. Renew that same parent without broad
  discovery. Source/model expiry, missing reception, rebind and faults still
  release the reservation. Relative lock and downstream advertising retain
  their existing quality gates; scheduling never grants timing authority.
- Request 5 ms/20 ms initiation windows while that precision receiver is
  reserved. Cold acquisition keeps 20 ms/20 ms. Preserve the original attempt,
  exchange, supervision and actual cancellation/drain deadlines. These public
  controller requests do not establish physical RF priority or delivery.
- Refresh the receive clock reference before control work, and drain queued
  precision reports between relay, GATT and JOIN operations. All drains share
  one eight-report budget per worker iteration, replacing separate per-drain
  limits. Check report age and subsequent control decisions against fresh time after yielding SDK calls; keep the
  original 50 ms worker-age and 250 ms physical association limits.
- Add production-boundary regressions for quality dips, warm/cold initiation,
  expiry/fault withdrawal, SDK yields and total report budget. The previous
  implementation fails the corresponding ordering/reservation regressions.

## 0.1.8 — 17 October 2026

- Start continuous BLE100 direct-G1 replay refresh at 150 ms uncertainty,
  with urgency at 175 ms. The prior 200/225 ms thresholds left about 25 seconds
  before the hard replay deadline at the declared 2000 ppm drift allowance,
  less than the existing 20–40 second warm failure backoff. The new threshold
  reserves about 50 seconds for one failed two-second exchange, maximum
  backoff and a second two-second exchange. Two failed attempts or a longer
  source/RF outage can still lose lock.
- Keep the rolling limit of two warm connection starts per 60 seconds, the
  250 ms hard replay limit, signed source/model expiry, BLE50 thresholds and
  RC7 five-minute renewal/six-minute lease. Earlier control attempts grant no
  timing authority. A combined-policy regression covers accepted bootstrap
  uncertainty and rejects the old threshold with the same retry schedule.

## 0.1.7 — 16 October 2026

- Isolate GNSS-aid and command ownership status snapshots in separate non-inlined
  functions. Their full `R2Status` copies no longer remain in the main loop or
  parser frames while rendering another complete status response. The first
  attached-unit 0.1.6 candidate overflowed task main on its startup status request;
  build and native host checks had passed. The failed candidate was withdrawn.
- Keep the configured 12,288-byte main task stack. Build and standalone inspection
  now require retained GCC static frame measurements for selected main-task
  paths, with a 4,096-byte reserve for uncounted callees. Reject missing, ambiguous
  or dynamic frame evidence. A regression rejects the observed 11,216-byte
  nested status path. This check is not a complete call-graph proof or a runtime
  reliability claim; exact-unit watermark observation remains required. The
  historical `main_stack_free_words` field is byte-valued on the pinned SDK.

## 0.1.6 — 16 October 2026

- Keep an acquired direct parent and its periodic receiver through authenticated
  NO_SOURCE, unusable time, and foreign-root locator replies. Record zero-root
  NO_SOURCE separately; only a positive foreign root receives the 60-second
  follower deferral. Every old source, replay and corrected-model deadline stays
  unchanged. New scope still requires a fresh authenticated binding.
- After the first actual corrected receive, use 20–40 second jittered failed
  control retries and at most two connection starts in a rolling 60 seconds.
  The start history survives parent changes. Waiting for that budget cannot
  pause downstream capability advertising or its precision publisher. Actual
  ACL arbitration and the existing parent-lock/source gate still apply.
- Preserve the last physically corrected local model for OLED HOLD after source
  or replay expiry, with unknown error bound. This presentation estimate cannot
  authorize admission, extend a lease, train the fitter, advertise precision,
  or supply downstream timing. Explicit stop, faults and detected clock steps
  withdraw it. Current authority and relative quality expire independently.
- Add a passive 160-byte, eight-entry receive history frozen when the corrected
  model actually expires. The first frozen capsule survives recovery for the
  boot; later losses/reacquisitions are counted with detailed history unavailable.
  Bounded capture diagnostics retain authenticated,
  associated, mapped, stale and refused worker observations. Missing controller
  RF opportunities, DONE and CRC measurements remain unknown. Timer/controller
  ownership and the existing ordinary console frame size are unchanged.
- Retain the legacy five-minute RC7 renewal, six-minute lease, BLE50 client-only
  restriction, optional unqualified BLE100 estimated relay, and explicit GNSS
  indicator profile. W12 provides no USB timing stream or USB timing relay.

## 0.1.5 — 16 October 2026

- Replace the inherited C-3-only OLED factory guard with the generated exact
  device binding. Board 12 requires the explicit GNSS capability; unsupported
  board routes and mismatched factories remain refused before GPIO activity.
  The old guard made a correctly bound C-4 GNSS image stop before normal service.

## 0.1.4 — 16 October 2026

- Bind the GNSS lamp mode once in the shared indicator model, so full-screen
  and fast corner refreshes use the same physical-input policy. The 0.1.3
  full-screen refresh could briefly fall back to BLE activity during GNSS loss.

## 0.1.3 — 16 October 2026

- Add an explicit local Meshnology W12 L76K RX39/PPS41 indicator profile.
  Preserve GNSS-disabled defaults; neither the alias nor factory ID selects
  wiring. Bind the selected capability into the generated factory header and
  inspect its agreement with the actual compiler flags.
- Restore physical PPS / checksum-valid GNSS packet / fresh RMC fix corner
  indicators on that profile. Missing GNSS keeps these lamps dark instead of
  falling back to BLE activity. Actual GNSS presence still requires fresh data.
- Reuse the existing board-12 power/input route, with GPIO48 LOW and UART
  TX/WAKE/RESET high impedance. Send no GNSS commands or time aid. Keep GNSS
  outside the BLE estimator and source authority. RC discovery intervals,
  parent-lock admission gate, replay limits and precision train are unchanged.

## 0.1.2 — 16 October 2026

- Reduce continuous BLE100 direct-G1 JOIN traffic by moving proactive replay
  refresh from 100 to 200 ms of accumulated uncertainty, with urgency at 225 ms.
  Keep the original 250 ms hard replay limit, signed source expiry, source-loss
  recovery and BLE50's 100/125 ms acquisition thresholds unchanged. The remaining
  50 ms replay budget gives about 25 seconds at the unchanged 2000 ppm allowance
  for the existing bounded admission attempts; it grants no extra source life.
- This addresses direct G1 replay refresh observed on C-4; RC7's separate
  five-minute renewal and six-minute lease retain their existing constants.
  Relative clock lock, RF timing, UTC accuracy and long-term reliability remain
  distinct outcomes.

## 0.1.1 — 16 October 2026

- Both RC discovery advertisers (SID 12 SyncInfo discovery and SID 13
  capability/admission) request 100 ms: 160 GAP units of 625 µs. This matches
  the T096 RC setting. The previous W12 requests were 31.25 ms each; their
  combined nominal event rate changes from 64 to 20 per second, before
  controller scheduling and advertising delay.
- Hold both discovery advertisers and downstream periodic publication until
  the current authenticated parent has the existing raw clock lock: a usable
  corrected model, the current source/path and permission, sub-1 µs relative
  quality, and at least one second of projected hold. This is a development
  quality estimate, not independent UTC or output qualification.
- On lock or source loss, stop downstream advertising/publication, expire
  child demand and drain incoming admission using the original deadline.
  Preserve upstream reception and its clock estimator. Reacquisition requires
  fresh child admission and a new downstream train.
- Preserve the separate 62.5 ms (16 Hz) precision train and its existing
  parent-relative phase request. A locked relay can advertise capability before
  child demand, so admission can start the periodic train without a cycle.
- Keep RC7's five-minute normal renewal and six-minute admission lease. Poor
  delivery statistics alone no longer trigger repeated early admission while
  the current parent clock remains locked. Missing clock progress and expired
  admission still recover through the existing bounded paths. Direct G1 JOIN
  replay refresh and failed acquisition are separate from RC7 normal renewal;
  neither is relabeled as a five-minute operation. Signed source/model expiry
  and original replay bounds remain unchanged.
- Extend the reproducible permission runner with the existing native server
  owner fixture, using synthetic factory and parent-lock inputs. Check both
  discovery intervals at the NimBLE boundary and keep the independent periodic
  interval assertion. Physical admission, RF timing and reliability remain
  separate hardware outcomes.
  Server status `rs7` flag bit 8 reports current advertisement lock eligibility;
  it does not qualify actual RF transmission or absolute timing.

## 0.1.0 — 16 October 2026

Initial public-source successor to the selected W12 native G1/RC7 application.
See [cutover and validation scope](../../docs/w12/cutover.md).
