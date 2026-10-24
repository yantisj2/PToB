# N30 changelog

## 0.2.31 — replacement RC7 lineage cache fence

- Consume the shared W12 authenticated RC7 path fence: new root, boot, source
  epoch or publisher path clears obsolete advisory trials and retry scheduling.
- Preserve the current authenticated response/ACL, exact PA drain deadline,
  operator pins and per-device calibration. Unchanged-path renewal remains
  separate. Native regression outcomes establish software behavior only.

## 0.2.30 — fallback transition cache fencing

- Clear old normal-source selection/trial history on authenticated RC9 takeover.
- Fence the prior advisory caches even if a newly authenticated direct root
  arrives after the earlier scope was already withdrawn.
- Reset stale fallback retry scheduling; preserve explicit pins, source/lease
  expiry and real controller drain deadlines. Actual native regressions cover
  stale-history takeover and direct admission from an empty scope.

## 0.2.29 — leader recovery

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

## 0.2.28 — normal BLE recovery from RC9 fallback

Consume the shared W12 AUTO fallback recovery fix. Bounded passive discovery
and fresh normal admission replace the former six-minute selection lockout.
Failed admissions and root hints retain fallback under its original expiry.
Explicit pins, separate clock models, capture calibration, startup and LoRa
policies remain unchanged. Earlier releases are recorded in Git and the target
README. Original documentation: MIT; copyright 2026 Jonathan Yantis.
