# GM7 simplification amendment S1

Copyright (c) 2026 Jonathan Yantis. MIT.

**Offline development contract for T096 0.15.184; hardware acceptance remains
open.** The diagnostic identity is `GM7v2+S1+C1`. S1 preserves implementation2's
exact wire lengths, revision and MAC domain and C1's full UTC birth. It supersedes
implementation2's descriptor-gated GM control admission and A1's discovery and
retry policy. Frozen predecessors remain historical inputs with unchanged hashes.
Normal RC GATT/RC9, client admission, relay, USB, registry and LoRa are retained.

## One owner and ordinary messages

The serving ACTIVE names one preferred HOT in its ordinary ANNOUNCE. Other GM
nodes follow timing, independently qualify their own sources and send ordinary
STATUS about every two seconds. A qualified, physically prepared nominee arms
locally and reports HOT in its next ordinary STATUS. There is no appointment,
grant, ACK, coordinator or GM-to-GM GATT exchange. Preparing/warm UI is derived
from readiness and nomination; it is not another network negotiation.

Independent qualification remains 60 seconds; initial quiet/election fallback
remains five seconds. The appointed HOT retains its actual shadow preparation
through a bounded blackout, with 2.75-second silence and a three-second first
timing-event target. These are unchanged requirements, not measured RF results.

The older full UTC birth wins; lower full founder resolves equal births. Within
the same lineage, higher term wins, then lower leader identity. Ambiguous boots
for one physical identity are quarantined. A losing ACTIVE stops positive output
on accepted fresh older authority before acquiring its physical timing descriptor.
Surviving succession inherits birth/founder; boot, term, source and train remain
separate. Terminal negative and retired-term floors remain bounded and retained.

## Contact freshness and physical timing

T096 has no validated small independent UTC accuracy enclosure. A first MAC-valid
ANNOUNCE/STATUS is therefore an authenticated **heard locator**, not fresh control
authority. The existing two-message unpredictable nonce REQUEST/ANCHOR proves
first-contact causal liveness for a full identity and boot. There is one total
pending nonce, a per-peer two-second cooldown, consume-once handling and the
original 250 ms actual-TX deadline. A new attempt uses a new nonce.

The resulting causal monotonic interval is a contact fact, independent of role,
lineage, source/capture generations and timing train. Keep its existing six-minute
maximum lifetime, 225–240-second renewal, 200 ppm combined drift assumption and
500 ms maximum interval width. These are explicit functional operating assumptions,
not oscillator or UTC qualification. Original-send freshness uses the earliest
causal send bound. Copies and receipts never renew it.

A fresh ANNOUNCE or STATUS passes MAC/group/key/identity/boot, sequence/floor,
original-send and finite-age checks without requiring a serving/candidate
descriptor match. ANNOUNCE source permission is still clipped to its original
independent source expiry. Read-only admission precedes owner terminal/term/scope
checks; rejected negatives and retired terms cannot replace current control or
cancel pending local work. A sampled ANCHOR arriving after a newer ANN preserves
the maximum replay floor and that ANN's original control/source horizons.

Physical following remains separately fenced by a matching immutable ANCHOR
descriptor: full birth/founder/term, identity/boot, source/capture/train and timing
token, followed by actual G1 capture/correction/servo association. A descriptor
refresh never erases current contact control or delays merge stop-self. A nonce,
heard packet or matching descriptor alone cannot create physical readiness, HOT,
calibrated accuracy or a renewed source horizon. Unknown UTC bounds stay unknown.

The unchanged direct ANCHOR requires a currently eligible independent source.
Acquiring/ineligible GM STATUS remains visible as authenticated heard, with
freshness unproven and eligibility explicitly a sender claim. It cannot enter
HOT/election authority until causal admission succeeds. Heard visibility has a
4.5-second observer receipt horizon, explicitly distinct from fresh authority;
even a replay may be heard. No new contact-only response semantics are invented.

## Discovery and one secondary owner

Ordinary primary ANNOUNCE remains at 38 ms in each local 250 ms frame. Replace
A1's once-per-second identity-bit secondary with one byte-identical ordinary copy
at 45, 50 or 55 ms, selected by SHA256 over full identity, boot and changing frame.
The copy retains the primary's original sequence, timestamps and source horizon.
The completed primary buffer is retained only until this bounded copy decision;
there is no additional queue or fourth native controller context.

The complete event is bounded by 2 ms plus at most 2 ms lateness. The latest copy
therefore ends by 59 ms, before the next 62.5 ms G1 boundary. Actual submission
still needs at least 3 ms lead. Routine ROLE diagnostics displace one frame's
copy about every two seconds. All ACTIVE nodes listen whenever the actual single
radio is free. Enabled GM scanner coverage is continuous 20 ms/20 ms on channel37 regardless of
physical phase. Removing phase-driven channel rotation/restarts also removes a
source of control receive gaps. Ordinary RC scanning stays unchanged. Continuous requested scanning
is not proof of successful controller/AUX receive coverage.

A received REQUEST uses the same single secondary context, ahead of any copy or
diagnostic not yet queued. Select the first usable 43/48/53 ms opportunity in the
receiver's current or next frame from actual receive and processing times, within
the unchanged original requester deadline. Do not assume the two meshes share
phase. Busy context or exhausted deadline drops the reply; there is no response
queue or moved deadline. Requesters retry in a newly hashed ordinary slot after
the per-peer cooldown. A1 reserved-frame refusal, retry residues, frozen retry UTC,
attempt counters and retry-offset state are removed.

Three lanes can collide among four nodes. Changing full-tuple hashes provide
repeated diversified opportunities; no guarantee is made under arbitrary RF loss
or permanently unavailable scanning. Independently phased 2/3/4-node composition,
multi-lane collision, loss, preemption and partition tests are required, with
actual completed encoded TX events and modeled listening/AUX eligibility.

## Events and bounded evidence

YIELD and WITHDRAW each retain three immutable primary copies at 250 ms spacing.
YIELD keeps its original 750 ms boundary; WITHDRAW stops positive output
immediately. Duplicate reception cannot move either deadline, rearm HOT or renew
contact/source authority. Blackout remains the fallback when every copy is lost.

An operator can request YIELD for any reason with the existing fully bound USB
command `r2 <request8> <silicon16> <source64> <boot16> yield`, without arguments.
It queues one owner-thread request for at most300ms. The worker rechecks actual
serving/source/calendar permission and the same announced nominee identity and
boot, with a fresh ordinary HOT/SHADOW STATUS, immediately before the normal
event builder. It neither appoints a nominee nor supplies readiness. A repeat
does not extend the request; role, nominee, boot, scope, stop and expiry changes
cancel it. DC and non-GM7 images reject it; runtime RC refuses it.

The command's `EINPROGRESS` means queued. GM page00 adds
`yield_request_pending` and `yield_request_result`; result0 means owner and event
construction accepted the request, before queueing or physical transmission.
Failures are negative errno; no failed request is retried later. Use event
sequence, original deadline and actual TX/RX journal endpoints to assess the
three copies. Normal profile/maintenance shutdown retains its existing WITHDRAW
path. Equal-quality T096 peers need this explicit request because automatic
quality-driven YIELD requires a strictly better successor.

The existing 29-record, 40-byte timing journal is reused. Records retain its
source/boot/cursor, overwrite and missing-record accounting. Under kind32
(`R2_TIMING_GM_CONTROL`), detail is `0x7000 | stage<<8 | wire_kind`:

| Stage | Meaning | a | b |
| --- | --- | --- | --- |
| 1 | RX owner admission result | Sequence low32 | Actual AUX-END raw16MHz capture |
| 2 | TX completion result | Primary ADDRESS raw16MHz | Final AUX-END raw16MHz |
| 3 | Full sequence association | Sequence low32 | Sequence high32 |
| 4 | Original event deadline | Sender-monotonic deadline low32 | Deadline high32 |
| 5 | Immediate merge decision | Local source-until low32 us | Processing HF low32 us |
| 6 | Source/owner cancellation | Local source-until low32 us | Processing HF low32 us |

Peer is the full sender identity for RX, self for TX, winner for merge. Result0
means admitted/complete; negative errno remains a rejection (including duplicate
`EALREADY`). Merge/source results are owner reasons. Sequence→TX→deadline records
are emitted by the same worker; event RX includes full sequence/deadline records.
All terminal copies and duplicate RX are recorded. Routine ANN TX is sampled at
sequence modulo4, ordinary RX is throttled per peer to one second or admission
change. A finite ring can still overwrite; missing evidence is not successful
delivery. Raw capture modulo/epoch and source boot must be resolved by collectors;
ADDRESS is not relabeled as packet START.

Peer JSON adds `heard`, `control_fresh`, `independent_eligible_claim`,
`timing_descriptor_matches`, `kind` and `admission_rc`. A heard-only peer has
unknown role255 and unknown timers; UI shows HEARD ONLY/UNQUALIFIED or CLAIM ELIG.
Actual ROLE durations retain their original sender sample and scope. None of
these observations grants authority or proves calibrated output accuracy.

## Reproducible offline checks and limits

After acquiring the public T096 dependencies, run from repository root:

```sh
python3 firmware/t096/tools/gm7_tests.py --deps artifacts/deps --out artifacts/gm7-host
python3 firmware/t096/tools/gm7_cohort_tests.py --deps artifacts/deps \
  --out artifacts/gm7-cohort.json --nodes 4 --duration 150 \
  --phases 0,69001,127777,233123 --loss-mod 7 --preempt-mod 11
```

The cohort uses separate production service/owner/codec/scheduler instances with
distinct monotonic origins. It seeds no peer/session/selection/HOT, and delivers
only completed encoded events through explicit collision/scan/AUX/G1 gates.
Final convergence requires one serving identity, exact lineage/boot agreement,
live selected control/source horizons and sustained final agreement. Its native
controller and RF endpoints are modeled, not hardware observations. The optional
`--physical` experiment connects actual PT/mailbox/GRv3/servo bodies and synthetic
captures and matching advertised PA sync ownership. It does not seed shadow
readiness or HOT, but its synthetic HAL/RF remains a model boundary. Loss and AUX
preemption use distinct SHA256 words per completed transmission/link, with
per-link attempt, delivery and drop denominators. Permanent partition failures
are retained separately from connected-loss convergence.

| Removed state/dependency | Retained fact or owner |
| --- | --- |
| Four pending nonce slots | One causal peer/boot proof at a time, cooldown/fairness |
| Descriptor role/source/train gates on control | Exact immutable descriptor only for physical following |
| A1 identity bits, reserved frames, retry residues/attempts/offsets | Full-tuple changing-frame copy hash; existing secondary arbitration |
| Frozen tentative retry UTC | Locator is only a scheduling hint; original actual-TX deadline |
| Scan phase field and phase-driven stop/restart | Explicit enabled/epoch GM37 scan, ordinary RC scanner unchanged |
| Unread roster count and constant compact coordinator/self fields | Identical compatibility values derived on read |

The administrative request is32bytes, funded from removed state; final ARM
static/stack inspection remains mandatory. Physical source provenance, cancel
generations, replay floors, output permission and original horizons remain live
state, not diagnostic substitutes.

Retain the .181 re-election, .182 stopped-PT recovery and .183 source snapshot
ordering regressions. Separate source review, synthetic production-body tests,
native resource/stack checks, actual RF endpoints, UI and independent output
measurements. A first bounded convergence canary may leave HOT/events and CPU
stability acceptance open; it is not the completed platform result. Unexplained
CPU faults remain a separate blocker, with no writer or common cause established.
