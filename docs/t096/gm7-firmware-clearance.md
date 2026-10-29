# GM7 offline firmware implementation clearance

Copyright (c) 2026 Jonathan Yantis. MIT.

**Decision: A CLEAR for functional OFFLINE IMPLEMENTATION AND BUILDS, subject to
the engineering exit checks below. B NOT READY TO FLASH. C NOT ACCEPTED.**

This is an engineering authorization boundary, not approval of a firmware image.
The authorized objective is functional connectionless GM7, including receiver
bootstrap, existing supported relay paths, diagnostics and UI. Merely disabling
all clocks, retaining a hidden JOIN/GATT dependency, or producing codec-only
scaffolding does not satisfy it. No firmware/hardware operation was performed
by this audit. Attachment of four T096s identifies a future opportunity, not
permission to skip exact-device checks or treat all attached devices as targets.

The read-only source checkpoint was public HEAD
`2eb37802cc1bb15ceadd315e5116888896c633cf`. Unrelated working changes are not part
of this approval; implement in the managed isolated checkout and coordinate any
shared W12/N30 changes against their actual current source/pins. This report
adds no private enrollment, identity list or raw laboratory record.

## Reviewed identities and concrete findings

The frozen [GM7-draft1](../../protocol/gm7/README.md) remains unchanged, manifest
`d200141fc9f4cd2ea91b80843d0bd563d7f349c1ff6c2e40fd4f8a7415006ef4`.
Its 29 offline tests/11 manifested artifacts remain the prior reviewed baseline.
The new [GM7 implementation2 contract](../../protocol/gm7_v2/README.md) explicitly
supersedes its UTC-only freshness bootstrap and one-control-job schedule;
exact offsets, new domains and synthetic vectors are in that separate collection.
Do not call the new implementation draft1-wire-compatible. Retain both manifests.

The audit found resolvable architectural gaps, not reasons to require field
measurements before writing code:

- `protocol/network.c:506` constructs the current coarse source from actual
  PPS/NMEA evidence, preserves original GPS/PPS expiry and translated fractional
  UTC, and deliberately leaves absolute/source association qualification false.
  That flag is not the independent 60s GNSS admission state. It must not be
  relabeled as calibrated UTC or treated as proof that functional UTC is absent.
- `protocol/group_join.{h,c}` and `src/group_admission.c` currently use fresh
  nonces and causal exchanges. Replacing them with HMAC+sequence on cold boot
  would lose replay protection. Implementation2 supplies a bounded connectionless
  nonce exchange instead; it must be integrated, not omitted behind a permissive
  “trusted packet” flag.
- `protocol/group_precision.c` already authenticates complete G1 bodies with
  group context and full root ID at byte126. Its four-entry ID array is a local
  decode dictionary, not an intrinsic recipient roster. Populate it from accepted
  identities/path, preserving actual body/proof validation and full identity.
- Current `src/radio_service.c:770` updates GS/GD metadata at one-second cadence,
  alternates records because116+90 exceeds191 bytes, and does not provide a
  250ms phase-reserved management scheduler. HCI submission is not proof of
  on-air time. A new bounded controller-owned management scheduler is real
  implementation work, including source invalidation at dispatch.
- Current `prj.conf` has two advertising sets and one periodic sync; the RC
  overlay already permits three advertising sets. Resource configuration and
  controller assertions must match the actual GM7 backend. No fixed extra sync
  is intrinsically required by GM7: each standby follows one ACTIVE timing
  train. Extra management periodic contexts, if chosen to realize bounded
  control jobs, must be justified and sized, not silently added as doctrine.
- Current HOT display logic in `src/ui_render.c` and `src/ble_service.c` depends
  on received map validity. Directly reusing it would erase HOT duration during
  the very blackout that GM7 intends to survive. Owner-derived armed state and
  distinct timing-map validity are required.

## A: approved functional implementation scope

Implement GM7 as the normal selected GM7 GM/RC path with **no GATT/ACL dependency**
for discovery, election, timing admission, HOT, relay admission or renewal.
An explicit legacy build/path may remain for regression/compatibility, but must
be labeled legacy and must not silently run as a fallback inside GM7. DC remains
separate, flash-only and Bluetooth disabled. Preserve USB, RC output permission,
LoRa functional fallback and current supported relay behavior; preserve unknown
precision bounds. Do not add W12/SS3/N30 GM capability or unsupported BLE50 relay.

The Sol6.1 Extra High engineer is cleared to implement, run offline host tests,
render previews and perform native builds. Source/native resource measurement
and regression closure are **engineering exit criteria**. No hardware, daemon,
flashing, release, push or private-lab mutation is included in A.

### 1. Quality, UTC birth and physical source policy

Independent source admission remains60,000,000 continuous us with original
GPS freshness<2,500,000us, PPS freshness<1,500,000us, GM capability, valid date/time
and source availability. Real source/capture/physical UTC association changes or
expired evidence reset admission. Routine PPS, fit, publication or radio-train
updates do not reset physical dwell. Keep source eligibility, functional UTC,
phase association and calibrated UTC error as separate typed states.

Use the existing functional UTC map, including its normalized fractional RMC
and current source-generation translation, projected to the actual first ACTIVE
transition to obtain birth_us. Do not replace it with last PPS alone, random hash
or uptime. This is the source's UTC estimate; it is not a metrological accuracy
claim. Preserve original evidence/expiry and checked integer projection. Require
valid Gregorian UTC in[2000,2100), current independent eligibility and a monotonic
map; reject source timebase regression and unexplained UTC discontinuity.

Leap handling is explicit and operational: parse/reject second60 or ambiguous
UTC labels for creating a new epoch/timed yield; source discontinuity fences
maps and requalification. No smear is accepted as UTC. If a positive/negative
leap indication is available, arm the corresponding ambiguity guard. Without
predictive leap metadata, conservatively inhibit **new epoch creation and timed
quality yield** during the last minute of every UTC month and the first minute
of the next month. Existing lineage persists; a detected ambiguous source stops
output/withdraws and returns through source qualification. Monotonic control
freshness and WITHDRAW do not need UTC. Add month-end/leap/calendar regressions
and expose why new election/yield is inhibited. This conservative policy avoids
requiring an unavailable leap API for ordinary operation while preserving the
ambiguity boundary; it is not full leap-second precision qualification.

Rank lexicographically `(priority1, class, accuracy, variance, priority2, ID,boot)`.
Use configured priorities, default128. Implementation2 class is an explicit
**GM7 ordinal**:1 independently commissioned PPS/UTC association;2 functional
independently eligible GNSS/PPS;255 unqualified. Default accuracy255 and
variance65535 mean UNKNOWN; populate lower accuracy/variance only from an actual
reviewed measurement/configured quality source, never HDOP, satellite count,
BLE shadow RMS or a fabricated variance. Unknowns sort after known. Current
ordinary qualified clocks may all have class2/unknown accuracy/variance and tie
by full ID; that is honest quality-based ranking. A better quality tuple excludes
ID/boot tie-breakers when deciding voluntary yield. Legacy GD2 class248/accuracy254
remain their original version-aware labels, never silently interpreted as GM7.

### 2. Connectionless causal bootstrap and receiver integration

Implement implementation2's96-byte REQUEST/186-byte ANCHOR PDUs, each as a distinct
ordinary advertisement, target-bound fresh128-bit nonce, no connection or final
ACK. This extra request is minimal bootstrap-only machinery: ordinary STATUS
alone cannot echo a target/receiver nonce while proving a fresh serving descriptor.
It introduces no HOT appointment exchange, coordinator or lease/grant service.
The control PDU is174 bytes (178-byte AD); REQUEST100-byte AD, ANCHOR190-byte AD,
role diagnostics172-byte AD, GNSS diagnostics160-byte AD. All individually fit191.

Cold boot must use cryptographic entropy plus a monotonic nonce allocator, never
a deterministic identity/boot hash. One outstanding request per target, at most
four total. Bind nonce to current group/key epoch, local/target full identities
and boots, original actual TX time and original250ms deadline. Consume once;
ignore duplicate/late/mismatched response. A nonce allocation or timebase failure
fails admission closed. Do not renew the deadline by queueing/repeating requests.

Passive locator/STATUS is discovery-only until fresh causal admission. A qualified
non-serving candidate can return an ANCHOR with no serving lineage and no timing
permission; its freshly anchored control floor permits initial quality ranking.
Thus stale replayed high-quality STATUS cannot indefinitely suppress an election.
Pending raw locators neither reset initial-election quiet time nor become eligible
inventory. Request the preferred candidates fairly; ignored/unanswered locators
have a2s discovery cooldown, not a permanent quality veto.

ANCHOR sampled_mono is captured after current request receipt and before actual
response TX. The client brackets remote monotonic time with the complete measured
request-TX-to-response-RX RTT<=250ms. Include response queue/processing delay,
not just airtime. Verify MAC and configured group/key; exact pending recipient,
nonce and target boot; descriptor, original source horizon and control floor.
The current reference decoder checks configured group/key explicitly. A successful
parse/MAC is not admission. Build a typed session keyed by
`(group,key_epoch,peerID,peerBoot,localBoot)`; never compare unrelated groups or
local monotonic epochs. A local monotonic regression destroys sessions, owner
permission and pending events, then requires fresh nonce admission.

Causal offset remains an interval. Expand by the declared combined200ppm drift
envelope, cap width500ms and lifetime6min; renew at225–240s. Observed envelope
violation invalidates the anchor rather than widening it silently. This is a
supported operating envelope to test, not calibrated UTC accuracy. It provides
freshness, not narrow control-window alignment. Original send time and sequence
floor reject replay; immutable duplicates cannot renew anything. Every current
ANNOUNCE's explicit source_valid_us<=1.5s is clipped from physical source expiry
and converted using earliest mapped original send, independent of max_age_us.
This renews normal output permission continuously at250ms without making2s
bootstrap cadence a source lease. Intersect it with original packet/map expiry,
source health, negative fences and matched physical evidence. No UNKNOWN bound
becomes zero or an inferred calibrated error.

A successful ANCHOR is not a captured PA sample. Reuse actual G1/GRv3 physical
capture association, event/channel/publication/generation matching and causal
precision fitter; do not fabricate capture or mark LOCKED from nonce reception.
Populate bounded ID dictionaries from authenticated full identities and actual
path, never a private fixed four-GM roster or JOIN slot. Audit root-slot,
recipient, boot/train/source/capture and callback ownership assumptions in both
encoders/decoders. Full-root G1 MAC and GRv3 root-origin/transmitter proofs remain
mandatory. The existing32-bit source_epoch becomes an explicit timing-scope token,
nonzero/no wrap, scoped by root+boot; it is not UTC birth. Increment/fence it on
real timing-source/capture discontinuity; births persist independently. A fresh
ANCHOR binds token and train to current full lineage, then actual packet matching
must agree. Metadata alone cannot bind or revive a map.

Keep broad passive discovery/bootstrap RX available while no trustworthy phase
map exists. Tentative phase inferred from a locator may schedule a nonce request
but grants no authority; a failed guess loses a bounded attempt. Establish narrow
RX/TX alignment only from accepted physically associated timing frames and
bounded map/drift. Until then, do not call a standby prepared HOT. Broad discovery
must have bounded owner scheduling so it does not starve primary timing or drain.

### 3. RC1/RC2, USB, LoRa and actual clients

Relay ANCHOR uses explicit RELAY flag and depth1/2, clears independent-eligible,
and identifies responder/boot/capture/train as the immediate transmitter while
ACTIVE/root boot/timing_epoch identify the original root. source_gen is zero
rather than claiming independent GNSS. It is allowed only with a currently live
authenticated upstream source/map. Clip valid_us to the minimum remaining
original root envelope, parent permission, signed GRv3 hold horizon/budget and
local map lifetime, then subtract complete downstream RTT. Never start a fresh
root lifetime at relay receive/response time.

Continue verifying actual GRv3 origin and route proofs, depth, full path IDs,
physical corrections and unknown per-hop bounds. Carry the existing original
root timestamp/expiry causal enclosure through the path; a fresh parent nonce
only proves fresh parent participation and cannot launder an expired root.
Renew relay timing permission only from newly accepted live root-origin/current
parent data. No metadata/diagnostic polling or repeated origin refreshes it.
A source change through an unchanged relay must fence old root scope and pending
callbacks exactly as current recovery rules require. Add direct→RC1→RC2 and
return-to-direct tests with original-root loss, source/boot change, stale origin,
nonce renewal, phase association and finite held horizon. Structural ANCHOR tests
are not sufficient for this exit criterion.

Implement the no-GATT receiver/relay adapter in actual T096 RC, W12, N30 and SS3
supported paths, and the pinned CoreS3-SE receiver overlay. Preserve each target's
existing support limits: BLE50 client-only, optional BLE100 estimated relay
unqualified, no invented GM/GNSS, no CoreSE relay. CoreSE pinned receiver revision
and patch/source closure must explicitly include the chosen shared changes;
changing the main W12 tree alone does not update it. Preserve RC9 fallback,
source-loss recovery, original cleanup/drain ownership and unknown TX bounds.

T096 USB timing consumes the resulting accepted map exactly as before. macOS
UTS1 readers keep finite source expiry and unknown bounds; it has no direct
GM7/GD wire consumer and requires provenance/regression audit, not a speculative
new radio decoder. Do not launch chronyd. SS3 CDC remains diagnostic only.
LoRa remains a separately versioned functional fallback; no covert GM7 takeover
or precision authority via LoRa, no new RF transmitter enabled by this work.
Preserve DC GNSS/USB image and its Bluetooth exclusion.

### 4. Election, nomination, retained state and takeover

Use a bounded inventory of eight current peers and at most four pending requests.
Current authenticated same-boot higher-sequence snapshots replace old ones before
ranking. Simultaneous fresh competing boots for one physical identity quarantine
that identity; never resolve by arrival order. Invalid/expired/raw-discovery rows
cannot change ranking by input order. Overflow is reported and refuses new
admission; it does not evict an active scope, floor or negative fence to make room.

Retain per-lineage maximum observed term and terminal withdrawn serving scopes
in RAM while that lineage is live. Budget eight lineage/negative entries initially;
if exhausted, stop admitting further scope changes until safe retirement or an
explicit cold restart/fresh discovery. Do not wrap term/sequence/generations.
Fresh boot needs fresh discovery/nonce and independently qualified source; it
cannot resurrect saved ACTIVE/HOT. No automatic flash epoch restore exists.
Old boots/terms cannot overwrite a selected current ACTIVE. Identical birth ties
use founder; term applies only inside one birth/founder; exact term ties use full
ACTIVE identity. Quality never defeats an older live epoch. Stop losing output
before follow/owner transfer. A complete partition may produce dual ACTIVE;
deterministic merge is convergence, not global exclusion.

After independent60s dwell and no eligible ACTIVE, wait a fixed5s discovery quiet
interval plus stable boot-derived backoff in[0,500ms]. Do not restart it on stale
locators. Rank only causally fresh candidate STATUS; local candidate is current
owner truth. A known higher-term floor excludes an old returning ACTIVE from
both merge and initial-election suppression. A complete-loss restart creates
new birth at its actual first serving transition, not at the quiet timer start.

A standby warms its own shadow while independent GNSS continues separately.
Prepared readiness requires current accepted source/physical map, lock/fit valid,
correction age<=250ms, same ACTIVE boot/term/train/capture scope, at least5s
continuous valid preparation and the measured/derived phase enclosure within
2ms. Keep ordinary fitter acceptance thresholds; do not tighten precision RMS
into an invented accuracy test. Nomination prefers the best eligible prepared
STATUS; changing an existing healthy nominee requires10s persistent strictly
better quality (ID-only tie is not “better”). The first valid nominee need not
wait10s. A nominee recognizes its full ID/boot and current scope, arms locally
when ready, and echoes HOT in its next ordinary STATUS. No other acknowledgement.

Retain that armed record and owner HOT start through a short received-map gap
for at most5s from conservatively mapped last original ACTIVE evidence, provided
independent local GNSS remains healthy and the retained prediction's uncertainty
stays within the supported scheduler envelope. Received map expiry ends current
received timing permission, not necessarily armed state. Real nomination revocation,
source fault, withdrawal of local readiness, conflicting selected ACTIVE/term,
boot/profile change or uncertainty exhaustion ends armed state immediately.
Takeover threshold2.75s plus bounded controller opportunity must fit original
contact+3s; otherwise report MISSED_BOUND and use safe re-election/withdrawal,
never extend that advertised bound. Non-HOT candidates restart ordinary election
after5s without a live valid ACTIVE; add backoff as above. At takeover increment
term, preserve birth/founder, activate local qualified source map and reset all
old-source receive permissions. Warm shadow may seed estimator state only through
the existing bounded handoff/association checks; it is not permission to reuse
old root/capture/train tags.

For voluntary quality yield require30s continuously better quality, at least30s
current ACTIVE service and a matching current prepared HOT STATUS. Freeze the
candidate/scope and750ms deadline once. Three identical copies at0/250/500ms;
no deadline movement. A source fault or immediate stop sends WITHDRAW, stops
positive output immediately and cancels queued YIELD/positive frames. Terminal
scope fences higher-sequence positives; ordinary healthy standby candidacy is
not fenced. Accept WITHDRAW only from already-known current scope/nomination,
never as authority for an unarmed clock. Monotonic freshness lets WITHDRAW be
sent safely even after UTC loss in this explicit new revision. Missed copies
use existing blackout behavior; all-copy loss remains possible.

## 5. Radio scheduling contract

Use a 250ms control frame phase derived from the current captured timing map.
Relative to its boundary, reserve control RX [28,58]ms, primary control TX
nominal38ms, and a separate secondary TX nominal43ms. Each job has a hypothetical
2ms complete controller/PHY airtime budget and at most2ms scheduling lateness;
8ms one-sided RX guard includes clock mapping enclosure, drift, turnaround and
scheduling. Primary completes by42ms before secondary starts43ms; secondary
completes by47ms. These are engineering bounds to implement and instrument,
not observed performance. A backend unable to meet them must report alignment
unavailable, stop aligned authority-dependent work and return to broad discovery.
Do not pretend ordinary random advertising delay or an unbounded HCI queue
satisfies this contract. Native controller integration is required if necessary.

Primary is reserved for ANNOUNCE or its immutable YIELD/WITHDRAW event copy.
Secondary admits at most one ANCHOR. Required primary traffic cannot be displaced.
Keep at most four pending requests; admit only responses that can finish in the
next secondary window and within the original250ms request deadline. Drop excess
or late work, never shift its timestamp or claim a new deadline. Use round-robin
eligible pending peers and per-peer2s retry throttling; this is bounded best effort,
not guaranteed admission under interference or overload. Diagnostics use secondary
only when no required response is pending. Broad startup discovery receives both
opportunities until a physical timing-map phase is established; nonce clock
freshness alone is insufficient to establish precision RX/TX phase.

Standby STATUS/REQUEST TX opportunities lie in GM RX [80,120]ms. Eight2.5ms slots
begin at90ms. Select a fresh slot each2s cycle using SHA256 of the full identity,
boot and cycle, as specified in windows.py; ±250us jitter remains inside that
slot's receive containment. Thus identities sharing low bits do not permanently
collide. Slot changes alter intervals by at most17.5ms plus jitter, within the
±100ms cadence allowance. Collisions remain possible; loss expires original
STATUS evidence and does not extend eligibility. This scheme has no assigned
roster, coordinator or slot handshake. Listening HOT candidates retain guarded
coverage through the supported short blackout; diagnostic scanning cannot replace
control reception. Measured airtime greater than2ms or guard greater than8ms
requires a reviewed revised reservation, not silent budget expansion.

YIELD/WITHDRAW use three identical event PDUs at primary opportunities in three
successive frames, with unchanged original sequence/body/deadline. Late processing
must discard an expired copy; no defer-to-next-frame substitution. WITHDRAW cancels
all positive work for that serving scope immediately while its negative copies
may continue. Blackout first-transmission feasibility includes the next aligned
opportunity, scheduler lateness and airtime before last original contact+3s.
Both scheduled start and on-air completion must be recorded; a timeout callback
at3s alone is not compliance. No number of repeats guarantees RF reception.

## 6. Diagnostics and UI migration

Revision2 freezes separate ROLE_DIAG and GNSS_DIAG schemas and synthetic vectors;
these replace neither historical GS116 nor GD2 90-byte decoding in place. Preserve
legacy strict decode and historical fixtures, with explicit version-aware labels.
Never reinterpret a32-bit legacy epoch as UTC birth, or legacy PTP accuracy254 as
the new quality ordinal255. New full birth/founder/term/ACTIVE/HOT boot scopes are
explicit; source/timing epochs remain separate. No diagnostic grants timing,
renews original permission or starts a radio/GATT service. Cache expiry uses
original sampled time and bounded authenticated session mapping. Unknown bounds
remain unknown, including after stale caches or missing fields.

Implement producer→decoder→scope/freshness cache→UI together. T096 owners and
`src/ble_service.c`, `include/ui.h`, `src/ui_render.c` must derive armed HOT from
the owner state, not the expiring received precision map. Start its local monotonic
timer exactly at armed transition; retain across intended short blackout, ordinary
beacons, STATUS/nominee echoes, fit refresh and page navigation. End/reset on real
revocation, source fault, boot/profile/leadership-scope change, uncertainty expiry
or takeover. Keep PPS and ACTIVE continuous-duration timers separate. Unknown or
stale owner state must not fabricate continuity. Preserve names, Bluetooth beat,
GNSS/PPS quality, normal RC and separate flash-only DC screens, and explicit shadow
page measurement/settling behavior. Rendering ordinary pages must not trigger
precision sampling/control. Distinguish shadow/relative RMS from physical output
accuracy and independent GNSS eligibility.

Update `gm_role_status.h`, GS/GD adapters and a new versioned GM JSON console
snapshot. Retire coordinator/grant/attempt fields explicitly under the new schema;
do not fabricate old values. Include source age/generations, UTC birth/founder,
term, nominee ID/boot/freshness/blockers, readiness/permission distinction, event
sequence, transition reasons/counters, duplicate/drop/late-window counters and
original deadline. Snapshot copying/formatting must be bounded. Diagnostic packets
fit individually (ROLE172-byte AD; GNSS160-byte AD); they cannot be concatenated
with178-byte control or190-byte ANCHOR under the191-byte controller limit. Budget
freshness-aware multiplexing below control/response priority. Repeated/cached
packets never renew their original expiry.

Actual consumers requiring changes are T096 RC `rc_dynamic_service.c`; CoreS3-SE
`mesh_owner.cpp`, `mesh_wire.cpp`, `mesh_diagnostics.{hpp,cpp}`, `display.cpp`,
`ui.{hpp,cpp}` and its pinned-source overlay; SS3 `native_g1.cpp`,
`td_role_update.hpp`, `ss3_td_model.hpp`, `ss3_native_display.cpp` and copied codecs.
CoreSE's MESH/PATH/QUALITY/REFERENCE/WIRE pages must retire coordinator text.
Its current UNCHANGED durations describe observer-local identity stability, not
remote continuous HOT. Its observed mesh-label age remains distinct from UTC
birth. SS3's10s stability filter and120s diagnostic poll may remain presentation
history only; they cannot delay takeover or claim instant remote HOT. Audit W12,
N30 and macOS actual build membership; no direct GS/GD consumer was established
there in this audit. Their connectionless timing adapters still require integration.
USB provenance/expiry and unknown bounds remain intact; no daemon launch is an
offline check. Preserve LoRa authority and expiry boundaries separately.

## 7. Engineer handoff and exit review

Sol may implement this complete functional scope in the isolated public checkout,
including controller integration, native receiver/relay admission, codecs/owners,
diagnostics and UI. No ordinary GM7 JOIN/GATT/ACL dependency is permitted. Retain
an explicitly separate legacy compatibility path only when labeled and tested as
such. The exact revision2 wire contract and draft1 identity are review inputs;
any necessary schema change requires a named new revision, vectors and review,
not silently changing a frozen contract. Source/build/resource work is an A exit
criterion, not a circular prerequisite to writing the candidate.

Required offline commands from repository root include:

```sh
python3 -B protocol/gm7/draft1/verify.py
python3 -B protocol/gm7_v2/verify.py
python3 firmware/t096/tools/test.py --deps artifacts/public-review/t096-deps
python3 -m unittest discover -s firmware/t096/provisioning -p 'test_*.py' -v
python3 -m unittest discover -s firmware/t096/tests -p 'test_console*.py' -v
```

Run GM, RC and separate DC native builds using `firmware/t096/tools/build.py`
with public dependencies, isolated output and ignored enrollment/device config as
required by `docs/t096/build.md`. Inspect artifacts and report exact source/image
identity, RAM/flash/stack/controller job ownership and bounded queue sizing. Do
not weaken size checks to obtain a build. One warm shadow timing subscription is
required; extra periodic sync contexts are backend choices requiring resource
justification, not a protocol requirement. Run actual consumer host/native tests
and preview fixtures using their current build guides: CoreSE mesh wire/diagnostic
and five-page previews, SS3 role/native/health and RC7 permission suites, W12/N30
receiver/relay permission tests and portrait previews, registry tests, and macOS
USB offline self-test only. Record unavailable toolchains precisely.

Add meaningful integration tests for initial election permutations and stale
candidate locators; nonce loss/replay/boot reset/group mismatch; candidate vs ACTIVE
vs RC1/RC2 anchors; root-origin/GR3 expiry clipping; recurring source permit renewal;
map/capture/train fences; term floors and returning former GM; nomination/revocation;
HOT timer continuity through map expiry; YIELD/WITHDRAW duplicates, reordering and
all-copy loss; obsolete nominee and asymmetric partitions; phase/guard exhaustion,
late queues and first-TX bound; all diagnostic version/cache/unknown-value cases.
Test every source producer→decoder→cache→renderer path with synthetic fixtures.
Existing frozen tests remain compatibility evidence, never proof of hardware timing.

A exits require reviewer inspection of actual functional code and tests, no
hidden GATT dependency, no default infinite permission, no cold-boot replay shortcut,
no source-generation alias for UTC birth, and no unbounded/untested callback work.
Use an implementation manifest and list the exact commands/results and limitations.
Engineering may resolve ordinary implementation details within these bounds;
security/wire/authority changes return to review.

**B: NOT READY FOR FLASH.** After A exits, establish each exact attached T096's
factory identity, ownership, chosen GM/RC/DC image/enrollment, verified recovery,
application write bounds and protected-prefix baseline. Reuse verified recovery
images; do not take unsolicited full flash backups. Review candidate image hashes,
application readback plan and bounded drain before exact-device operations. Four
attached boards are availability information, not a substitute for these checks.
No device or serial interface was opened by this audit.

**C: NOT HARDWARE ACCEPTED.** Measure actual controller airtime/queue/turnaround,
RX coverage/drift enclosure, real event TX timestamps,3s blackout bound, relay
expiry and independent physical PPS/output behavior across the four-device cases.
Include RF losses, all-repeat loss, asymmetric partitions, source faults, boot and
term changes, and stale diagnostics. Reacquisition/servo/output accuracy require
separate measurements; synthetic schedule arithmetic establishes no RF guarantee.
Fail-closed bounds permit implementing and testing a functional candidate now;
field promotion waits for measured evidence and exact-device review.

Audit validation is recorded by the revision2 read-only verifier and manifest.
Only stdlib reference tests and frozen artifact checks were run for this audit;
no native firmware builds, flashing, daemon, hardware or private-lab mutation ran.

Final offline audit result: draft1 verifier passed29 tests and11 artifact hashes;
revision2 passed11 tests covering10 exact vectors (including exhaustive individual
bit corruption), causal admission/source renewal and schedule bounds. The new
manifest binds this report and every revision2 artifact. Its verification command
never regenerates fixtures or hashes. These counts describe this audit only;
production integration and native builds are still Sol's required exit work.
