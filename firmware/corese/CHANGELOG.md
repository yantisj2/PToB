# CoreS3-SE firmware changes

Copyright (c) 2026 Jonathan Yantis. MIT.

## 0.1.16 — relay lineage and pending-controller recovery

- Clear deferred source trials and retry state when the same relay authenticates
  a changed GM lineage, preserving the current request and explicit relay role.
- Bind late periodic-create completion to the frozen actual owner address and
  refuse reports once cleanup is queued. Preserve the original drain deadline.
- Exercise actual pinned receiver RC7 lineage/role and RC9 pending-create return
  paths alongside direct leader loss, boot and incarnation controls.

## 0.1.15 — complete fallback scope fencing

- Clear normal-source selection history and retry state at authenticated RC9
  takeover and fresh direct-root admission after an empty scope. Preserve the
  original authenticated lease and real controller drain deadlines.
- Verify fresh-boot and same-address incarnation changes with production native
  fixtures; learned parents and models are never restored from flash.
- Keep the pinned receiver baseline and target GPIO6/7 counter route.

## 0.1.14 — leader transition recovery

- Backport the tested W12 leader-loss/changed-incarnation cache fence to the
  pinned receiver. Retire old sessions, estimators, queued timing, locators,
  trial history and backoff; preserve explicit pins and exact counter routing.
- Clear current source-bound capability, residual and diagnostic scan state.
  Historical reports keep their original deadlines and remain advisory.
- Test loss boundaries, successful fresh new-leader admission, pins, repeated
  invalidation throttling and actual pinned-source overlay generation. Hardware
  failover and precision require separate exact-image/device observations.

## 0.1.13 — 2026-10-24

- Admit post-PA diagnostic windows for capture ages below 10 ms. The earlier
  4 ms gate could close before fine association and authenticated decode finished.
- Display native retained time as an amber HOLD estimate for at most 500 ms
  from the last actual physical delivery. Repeated polls cannot renew it.
  Current root/path, precision, offset and source pulse remain withheld.
- Keep source permission and all timing/admission deadlines unchanged. HOLD is
  presentation with unknown error, never current mesh time authority.

## 0.1.12 — 2026-10-24

- Protect periodic timing reception from optional diagnostic discovery: replace
  80 ms discovery windows with 12 ms windows at least 200 ms apart, started only
  after a new authenticated, physically associated PA less than 4 ms old.
- Preserve the original healthy-clock coverage requirement, passive scanning,
  callback receipt fences, source deadlines and all time-authority rules.
- Bind the new scheduling policy into inspected receipts. Diagnostic delivery
  and continuous timing availability still require separate hardware evidence.

## 0.1.11 — 2026-10-24

- Backport the shared AUTO RC9 recovery fix from `6ac2721` onto the existing
  `699f100` receiver baseline. A six-minute fallback lease no longer suppresses
  bounded passive normal BLE discovery and fresh admission.
- Preserve fallback time through root hints and failed admissions, and fence it
  only after successful normal admission. Preserve explicit parent pins and all
  original timing, authentication and expiry rules.
- Record the exact recovery patch and policy in inspected build receipts; retain
  CoreSE's existing fine route, clock coordinates and calibration policy.

Hardware acceptance remains an exact-device, separately recorded operation.
