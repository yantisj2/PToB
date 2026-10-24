# N30 changelog

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
