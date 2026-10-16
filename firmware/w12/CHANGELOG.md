# W12 changelog

Copyright (c) 2026 Jonathan Yantis. MIT.

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
