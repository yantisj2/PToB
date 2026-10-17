# W12 changelog

Copyright (c) 2026 Jonathan Yantis. MIT.

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
