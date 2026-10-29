# N30 changelog

## 0.2.36 — ordered dynamic TZ updates

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

## 0.2.35 — coarse TZ and local clock display

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

## 0.2.34 — continuous clock through direct GM handoff

- Retain the running local clock during the successor's coarse JOIN. Resume
  phase steering with its learned rate only after a newly authenticated,
  matched physical correction; translate between different GM counter origins.
- Bound actuator reuse to30s after the original physical observation. Reset
  fit points, causal quality and source permissions, and retain gross-step,
  expired-carry, counter and invalid-capture fences. Cold start remains ordinary
  acquisition. W12/SS3 keep their existing behavior.
- Add actual-clock signed phase/rate/continuity and actual-owner JOIN/HOLD
  regressions. Software checks do not establish physical failover precision.

## 0.2.33 — announced GM successor recovery

- Consume authenticated GM7 revision-3 announcements bound to the selected root,
  boot, timing epoch and train. Retain the nominated successor's exact boot.
- Honor terminal withdrawal and the original timed-yield boundary; recognize
  the named successor's next-term announcement. Existing sustained source loss
  also carries the nomination into a bounded three-second recovery preference.
- Retire old source/model/queued samples through the existing owner drain.
  Require a fresh locator, G1 authentication and new physical timing. Preserve
  operator pins, relay-only policy, calibration and admission rate bounds.
- Add short passive control-listening windows between associated PA receptions.
  Offline regressions do not qualify physical handoff latency or precision.

## 0.2.32 — exact PA terminate acknowledgement

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
