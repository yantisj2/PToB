# W12 changelog

Copyright (c) 2026 Jonathan Yantis. MIT.

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
- Add a passive 152-byte, eight-entry receive history frozen when the corrected
  model actually expires. Bounded capture diagnostics retain authenticated,
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
