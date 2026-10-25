# GM7 ordinary control, S2

Copyright (c) 2026 Jonathan Yantis. MIT.

S2 is the T096 GM implementation contract for source 0.15.185, diagnostic
identity `GM7v3+S2+C1`. It preserves initial election and
[C1 mesh birth](../gm7_birth_c1/README.md). It replaces the dedicated
REQUEST/ANCHOR startup and refresh exchange, the session clock-offset map,
descriptor cache, nonce queue, and response scheduler. Those mechanisms are
absent from the v3 codec and current owner. The frozen v2/A1/B1/S1 material
remains historical context.

This document specifies functional software behavior. A model, native build,
MAC, checksum, fitted relative clock, or successful HCI request does not establish
physical RF delivery, CPU stability, calibrated UTC error, or installed-image
acceptance. Unknown absolute UTC bounds remain unknown.

## Ordinary messages

One GM serves. Its ordinary ANN names one preferred successor. Each other
GM-profile node follows G1, maintains its own delivered shadow clock, qualifies
its own GNSS source, and sends ordinary STATUS about every two seconds. A
qualified appointed successor confirms HOT in its next ordinary STATUS. No
appointment, grant, ACK, challenge, response, or GM-to-GM GATT exchange exists.
ROLE and GNSS messages are optional diagnostic observations; neither can appoint
HOT, confirm readiness, renew control/source permission, or initiate timing.

ANN, STATUS, and WITHDRAW use wire version 3 and MAC domain
`PToB/GM7/implementation3` including its terminating NUL. The 24-byte header
starts `G7V3`, revision byte 3. Control retains the v2 fields and appends a
big-endian 32-bit `timing_epoch` at byte 158, immediately before the 16-byte tag.
Control wire size is 178 bytes; its manufacturer AD is 182 bytes. Types 4 and 5
are unsupported. Diagnostic type numbers 6 and 7 remain distinct. Ordinary
client GATT/RC9/RC7, relaying, USB, registry/enrollment, and LoRa are unchanged.
The public test key in [vectors.json](vectors.json) is a test fixture, never device
enrollment. The vectors record their selected historical input and independent
Python HMAC transformation.

ANN carries the complete serving descriptor: full root identity/boot,
birth/founder/term, timing epoch, sender source generation, capture generation,
and G1 train. STATUS carries its selected root timing epoch/train and its own
independent source/capture generations. A SHADOW/HOT report identifies that
selected scope and delivered shadow generation. Sender-local qualification
fields are not substituted for the root's physical capture domain.

## Receipt policy and replay limit

Authentication does not prove the original age of a previously unseen recorded
packet or advancing recorded stream. Without a causal exchange or independently
bounded UTC, S2 cannot prove that age. This is an explicit consequence of the
ordinary-only design. Receipt-based deadlines below are local policy horizons,
not remote-send-age or UTC-error bounds.

For authenticated control sequence `q`, native completed receive time `r`, sender
monotonic sample `m`, maximum control age `a`, and ANN source validity `v`:

- A previously unseen acceptable sequence establishes `control_until = r + a`.
  Processing delay consumes that fixed deadline; it is never `now + a`.
- ANN proposes `source_until = r + v` and sender expiry `E = m + v`, with checked
  arithmetic. The retained source fact is the largest accepted sender expiry
  and its corresponding local deadline.
- If `E` is unchanged or regresses, the local source deadline cannot exceed its
  prior value. The fact survives local expiry, STATUS, descriptor changes and
  source/capture generation changes while that incarnation remains cached.
  Only an independently advanced authenticated sender expiry may renew it.
- Duplicate/reordered control, discovery copies and repeated event copies do
  not renew either deadline. Per identity/boot sequence and monotonic-sample
  floors, group/key binding, and independent term/terminal floors remain.
- A fresh STATUS has its own receipt-bounded eligibility lifetime. It does not
  require an old serving ANN source grant and cannot renew that grant.
- Scope, term and terminal-negative rejection precedes inventory commit and
  reservation mutation. A wrong-scope negative cannot destroy accepted state.

For example, `r=10s, m=100s, a=.75s, v=1s` gives control 10.75s and source 11s.
Processing at 10.2s consumes .2s. The same sequence at 10.5s changes nothing.
A new sequence with `m=100.25s, v=.75s` still has `E=101s`; it cannot extend the
source deadline beyond 11s. Repeating that expiry after 11s cannot resurrect
permission, even with a changed source generation. An advanced `E=101.25s`
may establish a new receipt-bounded horizon.

The peer inventory holds eight incarnations. On capacity pressure only, it may
recycle the oldest expired unselected, non-nominee, non-reservation, unreferenced
entry. All live/protected entries fail closed instead. `peer_evictions` makes
recycling observable. An evicted boot, if heard again, has the first-observation
replay limitation above; no boot-age ordering is invented. The separate
term/terminal ledger has eight shared records for lineage max-term floors and
exact terminal entries; it is not eight independent lineage slots. Advancing a
committed same-lineage max-term floor reclaims redundant lower-term terminal
entries while current-term exact active/boot protection remains. The ledger is
not silently evicted. A
never-selected inferior mesh does not spend a new term-floor entry. Exhausting
necessary lineage/terminal protection remains a resource failure.

## Physical following and succession

The GM7 managed PT adapter explicitly accepts receipt-origin ANN provenance.
Its UTC uncertainty remains unknown. Exact root/boot/timing epoch/train/capture,
original ANN source deadline, G1 group/MAC, signed created/expires arithmetic,
original/correction association, hardware capture age, and sequence checks remain.
G1 cannot extend the ANN grant. Its coarse 250ms timestamp sanity tolerance is
policy only; it is not calibrated accuracy or a proof of remote original age.
Normal RC/JOIN bounded-anchor validation follows its existing path.

Precision, retained phase, LOCK, and HOT readiness come from actual causal G1
matching and the delivered-map fitter. A received control, diagnosis, nonce,
source claim, timer, or successful HCI operation cannot seed that readiness.

The primary remains at 38ms of a 250ms frame. One immutable ordinary copy is
selected at 45/50/55ms by full identity/boot/frame diversity. Enabled GM management
scan remains on primary channel 37 within its original epoch; ordinary RC scan
behavior is unchanged. There are three bounded controller contexts. With no
response exchange, the existing secondary serves discovery copies or optional
diagnostics. Native start/late/AUX/end/cancel and source guards remain required.

YIELD and WITHDRAW each retain three immutable primary copies. WITHDRAW stops
positive output immediately. Fixed YIELD remains sample UTC plus 750ms; only the
actual retained G1 map translates its physical not-before boundary. Duplicate
copies cannot move it. The appointed HOT uses the existing handoff closure early
for accepted selected YIELD or WITHDRAW, or within that lead of the original
selected ANN source grant closing so the real map survives drain. This does
not extend positive permission or advance the original takeover boundary. The
same retained health, scope,
nominee and source fences. Blackout cause remains 2.75s and the original first
completed G1 bound remains 3s. The existing 100ms acquisition budget is a margin
before that bound, not a separately invented deadline after YIELD.

A successful PT promotion consumes its handoff capability. While the exact
original announcement reservation remains live, existing promoted/reserved
state may retain the prior bounded preparation and promoted scheduling phase.
It cannot create readiness, repeat promotion, extend a deadline, or survive
source/health/clock/scope/nomination invalidation. Losing/newer meshes stop
positive output before physical following. Successors inherit birth/founder and
advance term; boot, source, timing epoch and train remain separate.

## Observations

Existing HOT/ACTIVE/PPS timers and all eight peer pages remain. `known` is a timer
availability bitmask, not a peer count. Sender durations remain sender samples.
`gm00` adds current `preferred_hot`/`preferred_hot_boot`, a separate fresh ordinary
HOT confirmation and its bounded kernel deadline. These are coherent worker
observations, not proof that an ANN was received over RF. Confirmation is false
outside fresh serving/PT/source scope and during stop, resume, failure or an
expired/stalled publication. ROLE/timer inference cannot confirm HOT.

`gm09` is the query-only `GM7_FOLLOWER_ESTIMATOR` snapshot, `authority:false`.
It reports exact scope/capture coordinates and existing servo count/span,
observations, resets, saturation, arithmetic faults, unavailable predictions,
fit rate, quality bins and state flags. `scope_current` means the scope snapshot
is currently granted; it does not mean a valid delivered map or LOCK. State bits
are fit=1, output-valid=2, fault=4, PLL state at bits 8..15 and reason at bits
16..23. Follower facts share the existing 40-byte GM first-TX-only storage, cleared
on transitions. Follower diagnostic bits cannot be exported as first-TX proof.
No estimator algorithm, extra counter ring, or high-rate diagnostic stream is
introduced. The existing finite journal and explicit overwrite/missing counts
remain necessary when assessing real reception and event copies.

S2 starts blackout takeover no earlier than 2.5s after the last accepted ordinary
ANN native complete-receive endpoint, with the first completed native G1 still
required within 3s of that endpoint. The previous 2.75s threshold could miss
the 250ms primary phase by a receive-airtime interval, leaving no 100ms G1
startup margin. The existing 350ms physical closure lead is explicit: blackout
preparation starts at 2.15s, or earlier when the selected ANN source grant
closes within 350ms. This preserves the actual retained map without extending
the old source grant; promotion still requires independent qualified GNSS.

Timing-journal stage 7 (`0x7700`, kind 0) is emitted exactly once when an actual
observed first G1 completion meets the original takeover bound. `a` is the low
32 bits of the raw 16MHz ADDRESS capture (`first_tx_hi+623`); `b` is the low
32 bits of its derived complete-event END upper bound. END is derived from the
actual capture and encoded PDU length, not separately captured. Bind it to the
same physical boot/counter epoch and accepted ANN RX records. Missing or
overwritten records are incomplete evidence.

In the production-code cohort fixture, `first_new_g1_end` is the padded
1800us radio-slot ownership endpoint, not an exact packet END capture. With
the fixture's ADDRESS at start+40us and PDU length202, the production formula
gives START upper=start+1.0625us and derived packet END upper=start+1698.0625us
(1699us rounded outward). Both endpoints are reported separately; the shorter
derived packet bound does not enclose the extra padded ownership interval.
These synthetic inputs establish a software composition result, not RF accuracy.
