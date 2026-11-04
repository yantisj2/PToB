# N30 changelog

## 0.2.42 — 2026-11-03

Precision follows the selected clock's own diagnostics. LoRa now shows its
emitter/root, independent XTAL capture coordinate and mapping enclosure,
internal fit RMS/population, committed rate/phase correction, physical pair age,
committed clock-reference age and training/availability state. META age remains
separate. Old RMS values are hidden when the LoRa model is untrained, stale or
unavailable; no BLE lock or quality is substituted. UTC/RF bounds remain UNKNOWN.
Timing algorithms, source selection, radio scheduling and calibration are unchanged.

## 0.2.41 — 2026-11-03

Precision shows the actual selected clock source separately from its independent
BLE path/root, RMS and lock duration. Page navigation remains presentation only;
clock selection, the30s recovery dwell, timing models and radio policy are
unchanged.

A too-wide software mapping bracket may now retain the last qualified capture
map only when monotonicity, original counter-rate/gap guards and original
freshness agree. The rejected anchor is never installed; the old map still
expires at its original two-second limit. Hard/ambiguous failures clear it.
Diagnostics distinguish soft bracket refusals from hard map resets. This is an
offline-tested recovery of valid captures, not a new precision qualification.

## 0.2.40 — 2026-11-03

Keep the Precision page root label tied to its independent BLE model when
LoRa supplies the displayed clock. The selected clock header continues to name
the LoRa emitter. Timing models, wire format, calibration and permissions are
unchanged.

## 0.2.39 — 2026-11-03

- Receive the T096 passive wire3/schedule8 fixed922.4MHz SF11/BW500/CR4/8
  profile, with fresh authenticated META context, real SYNC/FOLLOW pairs and
  preserved replay/boot/stream/counter/expiry fences; reject older namespaces.
- Capture hardware HeaderValid separately from RxDone. The independent20MHz
  XTAL map explicitly supplies the shared16MHz coordinate. Use a declared
  unverified99.328ms engineering symbol model; board/RF bias and UTC bounds
  remain unknown. No TC02 calibration is transferred.
- Keep passive AUTO/KEEP ON RX and its independent fit warm during BLE operation.
  After LoRa selection, require30continuous seconds of fresh qualified BLE in
  unchanged scope before replacing the actual displayed clock. Manual OFF,
  independent BLE fitting and receiver-only/no-export authority are preserved.
- Offline host/native/portrait preparation and separately scoped hardware
  observations must be recorded independently; this revision has no automatic
  precision or hardware qualification.

## 0.2.38 — single-button watch menu

- Open the menu from any page at exactly 2000 ms; every earlier release is short,
  including release debounce crossing the threshold. Taps cycle enabled rows;
  a separate 2000 ms hold selects once. Consume the opening hold and return to
  the invoking page on Exit or 5000 ms idle, with no timeout while held.
- Preserve RC-only operation and upstream AUTO/relay-only selection. Add bounded
  Power Off, BLE RX OFF/ON, enrolled LoRa RX OFF/ON and Exit. No GM or LoRa TX
  mode is introduced. Requests stay in the menu with owner acceptance/refusal
  feedback and separate pending/activity/fault states.
- BLE-only OFF parks the BLE owner and retains the fine-clock owner. AUTO
  acquires LoRa immediately without replacing explicit KEEP ON/KEEP OFF intent.
  BLE/RC9 clocks and cached BLE pixels are ineligible while parked; the no-time
  restart timer is disabled. Actual fault/maintenance fences remain. ON requires
  fresh admission and ordinary 180 s AUTO recovery. LoRa overrides stay volatile.
  Timing models, source expiry, authenticated admission and original shutdown
  drain/ownership/wake guards are unchanged.
- Validate raw-button boundaries, all pages, row skipping/wrap, boot/wake,
  cancellation, idle/held behavior, actual owner dispatch and both renderer
  rotations offline. Native previews are synthetic; hardware acceptance and
  independent precision qualification remain separate.

## Build validation — shared stack checker

Keep N30's release sequence independent of W12 when selecting stack-evidence
contracts. N30 0.2.37 retains the common compiler-frame checks and 4 KiB reserve;
its version no longer selects W12-only GNSS, fine-owner or LR2021 evidence.
This tooling correction does not change runtime timing or hardware profiles.

## 0.2.37 — real shutdown instead of OLED blanking

- A four-second button hold on any non-clock page requests deep sleep after
  the original two-second radio/capture drain, OLED worker acknowledgement and
  40 ms stable release. A new button press wakes by rebooting the application.
- Retain Clock's one-shot 1.5-second relay toggle and LoRa's 1.5-second cycle;
  a continued LoRa hold now shuts down at four seconds. Consume wake/release,
  cancel page changes and prevent release debounce from crossing a threshold.
- Retain reviewed OLED VEXT/reset/I2C and SX1262 reset off levels through sleep.
  The display worker stops rendering before those pads change ownership.
- Host checks cover actual input, main action dispatch, fine-owner drain and
  display-stop timeout, plus power API error paths. These are offline checks;
  sleep current, battery lifetime and physical wake remain unmeasured.

## 0.2.36 — ordered dynamic TZ updates

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
history below. Original documentation: MIT; copyright 2026 Jonathan Yantis.

## 2026-10-22 — selected fine-receiver observations

Historical results moved from the target README. The
[research draft](../../papers/ptob-esp32s3-timing-preprint.pdf) and
[selected evidence](../../evidence/s3/2026-10-22-fine/README.md) retain exact
edition, source and measurement identities. These observations do not qualify
later images or every operating window.

| Measurement | Result | Population and limits |
| --- | --- | --- |
| N3-2 internal causal SYNC RMS | **49 ns observed** | Rolling prediction quality; three 49 ns readings among 311 finite readings in 313 status snapshots. |
| N3-2 downstream relative shadow PPS | **166.590 ns RMS** | Full 660 s window: 657 eligible + 3 invalid, zero nominal missing; mean −106.811 ns, maximum absolute error 1591 ns. All tails retained. |
| N3-1 same-correction transfer | **150.954 ns RMS** | Separate short check: 48 eligible + 16 invalid, no sequence gaps; mean +115.167 ns, maximum 362 ns; one terminal unread edge. |

The full N3-2 run used a fixed 50909 ns relay correction. N3-1 used a revised
51016 ns total correction derived from that run, with its own publisher and
direct-root scope. The fresh N3-1 check retained a positive mean: the constant
did not transfer zero mean across boards. No refit was made during that check.
Display RMS, counter resolution and calibrated RF/UTC accuracy are distinct;
the sub-100 ns downstream target remained open. The precision preview uses the
recorded internal 49 ns value with otherwise illustrative inputs.
