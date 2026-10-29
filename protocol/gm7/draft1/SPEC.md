# GM7-draft1: connectionless GM coordination

**REVIEW CANDIDATE / NOT FIRMWARE CLEARED — 24 October 2026.**
Copyright (c) 2026 Jonathan Yantis. MIT.

This is a standalone proposed revision, incompatible with G5 STATUS6. It freezes
control-message examples and initial reference policies for overseer review.
No production owner, radio transport, servo, receiver bootstrap, takeover state
machine or hardware measurement is supplied. Normative MUST statements describe
this proposed revision; they do not assert current firmware compliance.

## Purpose and state boundaries

Initial election ranks independently qualified GM clocks by quality. Every
non-ACTIVE GM receives ACTIVE timing and warms a separate shadow clock while
maintaining its own independent GNSS qualification. BLE following MUST NOT count
as independent GNSS, restart its dwell, or renew local physical source permits.
A local GNSS failure withdraws eligibility even when the shadow looks excellent.

The independent qualification dwell is 60,000,000 continuous microseconds. A
local source, capture or local GNSS map generation change, expired evidence,
profile change, GNSS failure or loss of independence resets it. The reference
Admission input tuple is (local source generation, capture generation, local GNSS
association incarnation), all nonzero u32. This last value changes on a physical
association discontinuity, never on ordinary PPS evidence refresh, affine-fit
coefficient updates or publication. Those routine updates preserve the dwell.
It does not require a transmit train. Each
update's validity horizon must come from physical source evidence, not STATUS.
Monotonic regression latches failure until a fresh admission instance is created;
that instance must complete its own dwell.

A standby emits ordinary connectionless STATUS every 2,000,000 us, with a bounded
per-interval jitter of [-100,000,+100,000] us. ACTIVE emits ANNOUNCE at most every
250,000 us under the proposed scheduler contract. It directly names the best
eligible prepared HOT using full identity and boot. That candidate recognizes
its own tuple and sets HOT in its next ordinary STATUS after verifying the
nomination and shadow/source readiness. No coordinator, extra acknowledgement,
lease, grant, GATT or ACL handshake is introduced. An initial candidate needs
independent qualification but does not need a pre-existing ACTIVE or shadow.

A nominated HOT must preserve its armed/shadow state through the intended short
blackout while independently maintaining current local source health. Nominal
silence threshold is 2,750,000 us since conservatively mapped original direct
ACTIVE send/contact evidence, never simply the latest receipt;
a scheduler allowance of at most 250,000 us targets takeover start within
3,000,000 us. Unauthenticated/replayed traffic cannot reset silence. A still-recent packet
that arrives 2,000,000 us late leaves at most 750,000 us of the nominal silence
budget; it cannot start a fresh 2,750,000 us timeout at receipt. Integrations MUST
conservatively map original authenticated send/contact evidence into local
monotonic time, account for UTC/association uncertainty, and never extend the
3,000,000 us target by transport delay. `Recent.until_mono` subtracts the
conservative packet age, but is only receive expiry, not a full HOT timer.
Unknown mapping bounds cannot establish the target; this remains a scheduler
and evidence-association integration gate. Expiration
of a packet's finite receive authority MUST NOT alone destroy the locally armed
record needed for this blackout; the record is scoped to the exact nomination,
lineage, term, ACTIVE identity/boot and HOT identity/boot. Changes/withdrawal or
local health loss invalidate it. Its bounded retention and reacquisition policy
remain an implementation gate. Other clocks may restart ordinary election after
5,000,000 us without a qualifying ACTIVE/HOT appearance. Arithmetic is not a
hardware deadline guarantee or a bound on downstream receiver reacquisition.

For persistently better eligible quality, ACTIVE can nominate the candidate,
observe its ordinary matching HOT STATUS, then repeat a bounded timed-yield
ANNOUNCE. The named, prepared, independently eligible HOT may begin at yield_us;
ACTIVE must stop serving by that boundary even if packets are lost. Yield_us is
strictly after send time and at most 1,000,000 us ahead. It remains fixed across
repeats, never extended. Old/mismatched terms, nominees or boots cannot yield to
an unrelated candidate. Quality persistence interval, readiness tolerances,
missed-yield fallback and transition scheduling are deliberately not implemented
here and must be reviewed before firmware clearance. No extra handshake is needed.

Yield is allowed for any reason while source-eligible, using timed YIELD in
ANNOUNCE. When source becomes invalid or serving must end immediately, emit a
one-way WITHDRAW event in the same envelope. Stop output immediately; never wait
for handoff. WITHDRAW has flags=0, yield_us=0, max_age_us in [1,1,000,000], exact
last serving lineage/term/ACTIVE identity and boot, retained nonzero train, and
optional last named HOT. Source/capture generations may remain descriptive or
be zero. It never creates a nomination, positive liveness, readiness or timing
permission, and never wins merges, suppresses election or initializes lineage.
An already armed HOT may use the negative event only when its retained current
ACTIVE scope and previously named nominee match; it does not arm an unprepared
clock. A missing HOT field does not designate a new successor.

Acting on WITHDRAW requires matching already-known current scope, not merely
successful decode. Ignore duplicates, replay, mismatched boot/term/lineage/train
and changed nominees. Withdrawal is terminal for the serving scope
(group,key_epoch,birth,founder,term,ACTIVE,ACTIVE_boot): later positive claims in
that scope remain fenced even with higher sequence or a new train/source
generation. Retain this negative floor with live-lineage/retired-term state;
ordinary packets cannot clear it. A legitimately established new term is a
separate scope, never authorized by the fence helper. `WithdrawalFence.consume`
checks a recent WITHDRAW against the caller's retained authoritative ACTIVE
snapshot (which may have expired receive authority); it only records negative
state. Apply `blocks` before positive serving-owner/policy use. It fences ANNOUNCE
serving claims, not independent standby STATUS candidacy still referring to
that lineage; diagnostics likewise do not restore or confer serving permission. This helper is not a
HOT state machine and does not establish that its supplied current snapshot is
still the selected ACTIVE. Its correctness requires that owner integration.

A trustworthy current UTC anchor is required to transmit and accept timestamped
control. If UTC is also lost, do not fabricate sent_us or timed yield; stop
output and use safe silence/fallback. The successor still requires independent
source health and retained valid preparation.

## UTC mesh birth and lineage

Mesh epoch means the UTC timestamp at which the first elected GM initiates the
mesh. `birth_us` is an unsigned 64-bit count of Unix UTC microseconds since
1970-01-01 00:00:00, in the explicit supported range [2000-01-01,2100-01-01).
Zero means absent only in an unbound STATUS. It is not uptime, a random hash,
a physical source generation, boot time, or the UTC of the last PPS alone.

The first ACTIVE transition MUST project the independently eligible current GNSS
UTC map to the actual transition instant and create `(birth_us, founder, term=1)`
once. The reference projection is `anchor_utc + delta + floor(delta*rate_ppb/1e9)`;
all units are microseconds except signed rate in ppb, restricted to +/-100 ppm.
The projection is valid only in the map's half-open monotonic interval and for
its exact current source/capture/local-map tuple. Integer floor is explicit;
overflow/out-of-range is rejected, never silently wrapped, clamped or truncated.
This projection describes the estimator's timestamp, not calibrated accuracy.

Unix representation has no unique leap-second label. GM7-draft1 disallows new
birth, fresh UTC-based admission, timed yield and epoch comparison through an
announced/ambiguous leap transition or unknown UTC conversion. The integration
must provide an independently verified unambiguous UTC conversion/anchor before
resuming these operations. No leap smear is accepted as UTC. Existing lineage
is retained; physical source operation through that interval requires separate
review. This conservative rule is a limitation, not a claimed complete timescale
solution. The reference map requires `leap_clear=True`; the verifier's UTC interval
requires the same assurance from its caller.

A winning accepted ACTIVE transfers the exact birth and founder. Succession
increments the u64 leader term without wrap and preserves birth/founder while
the live lineage survives. A cached empty STATUS, unrelated peer, expired claim
or losing ACTIVE cannot initialize lineage. The reference inheritance operation
accepts only the selected current ANNOUNCE. Total mesh loss has no automatic
flash-restoration contract. A reboot alone never authorizes service or reuse of
an old term; a returning former ACTIVE must reacquire current lineage and yield
to its successor. The arithmetic successor helper does not authorize takeover.

On meeting another eligible, authenticated, bounded-recent ACTIVE, compare:

1. Lower birth_us wins, regardless of quality or leader term.
2. For equal birth_us but different founders, lower full founder ID wins.
3. Only within the same (birth_us, founder), higher leader term wins.
4. At exact lineage/term tie, lower full ACTIVE (identity, boot) wins.

Two current boots claiming one physical identity are ambiguous and quarantined,
so the boot tie-break is not a way to resolve duplicate physical identities.
The losing serving ACTIVE MUST stop service before attempting to follow. No
quality score overrides mesh age. Equal timestamps are deterministic ties, not
proof of which physical mesh was earlier than estimator resolution/uncertainty.
Only comparable, qualified/coarse-anchored UTC claims participate. The term-floor
input retains the greatest accepted term per lineage; lower terms are excluded.
It must not be discarded while that live lineage is retained. A founder from a
different lineage does not borrow another lineage's term floor. The comparison
is deterministic convergence, not global mutual exclusion: partitions,
asymmetric loss and obsolete nominations can temporarily produce dual ACTIVE.

Mesh birth, leader term, sender boot, local physical source/capture generation,
ACTIVE transmit train and local shadow map generation are distinct scopes.
Source/capture/train changes invalidate affected timing maps even when lineage
survives unchanged. This control PDU is not a timing map and cannot renew one.

## Exact wire framing and authentication

Every integer is unsigned, big endian, fixed width, with no alignment padding.
All three kinds have the same exact **162-byte PDU**, including the 16-byte tag. There
are no extensions, trailing bytes or optional fields in draft1. A laboratory
synthetic Manufacturer Specific AD wrapper is `a5 ff ff ff <162 PDU bytes>`:
length 0xa5 includes one AD type byte, two little-endian test company bytes
0xffff, and the PDU; total wrapper length is 166 bytes. 0xffff is a synthetic
example placeholder, not an allocated production company ID. A production AD
assignment and controller payload/advertising scheduling check remain gates.
This fits the AD length byte and numerically fits the current reported 191-byte
controller ceiling; it is not evidence the current radio supports this format.

HMAC-SHA256 input is the exact ASCII/UTF-8 domain bytes
`PToB/GM7/draft1/control` followed by byte 0x00, then PDU bytes [0,146).
The tag is the first 16 bytes of the digest, placed at [146,162). The full header,
kind, identity, context and body are authenticated. Verification uses a constant
time tag comparison. The group key is 32 bytes and MUST NOT be all zero. Encode, decode and verifier
construction reject the all-zero key, including otherwise matching MACs. Key epoch and group must equal
configured values; no fallback or downgrade is tried. Separate version/domain
prevents interpreting G5 or another revision as GM7.

The public synthetic test key is the 32 bytes 00,01,02,...,1f. The examples'
identities/group/boots are synthetic. Group PSK possession authenticates group
membership, not a unique physical sender against another compromised group
member; deployment anti-impersonation and enrollment remain separate concerns.

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 4 | magic, exactly `47 4d 37 00` |
| 4 | 1 | revision, exactly 1 |
| 5 | 2 | total PDU length, exactly 162 |
| 7 | 2 | reserved, exactly zero |
| 9 | 1 | kind |
| 10 | 2 | flags |
| 12 | 8 | group |
| 20 | 4 | key_epoch |
| 24 | 8 | sender |
| 32 | 8 | boot |
| 40 | 8 | sequence |
| 48 | 8 | sent_us |
| 56 | 4 | max_age_us |
| 60 | 4 | source_gen |
| 64 | 4 | capture_gen |
| 68 | 4 | train_gen |
| 72 | 1 | priority1 |
| 73 | 1 | clock_class |
| 74 | 1 | accuracy |
| 75 | 1 | priority2 |
| 76 | 2 | variance |
| 78 | 8 | birth_us |
| 86 | 8 | founder |
| 94 | 8 | term |
| 102 | 8 | active |
| 110 | 8 | active_boot |
| 118 | 8 | hot |
| 126 | 8 | hot_boot |
| 134 | 8 | yield_us |
| 142 | 4 | shadow_gen |
| 146 | 16 | truncated HMAC-SHA256 |

Field rules:

- kind: 1 ANNOUNCE, 2 STATUS, 3 WITHDRAW. flags: bit0 ELIGIBLE, bit1 SHADOW, bit2 HOT,
  bit3 YIELD; all remaining bits MUST be zero. ELIGIBLE in remote STATUS is a
  sender report, not independently measured permission on the receiver.
- group u64, key_epoch u32, sender u64, boot u64, sequence u64: all nonzero.
  sender is the complete physical identity, never a display alias or truncated
  slot. boot is a fresh nonzero incarnation on every restart, never an ordering
  clock. Sequence strictly increases across both message kinds for one
  (group,key_epoch,sender,boot), including if roles change. Exhaustion requires
  withdrawal/reinitialization; no wrapping or new-boot automatic takeover.
- sent_us has the same UTC units/range as birth_us. max_age_us is nonzero and at
  most 2,750,000 for ANNOUNCE, 4,500,000 for STATUS, or 1,000,000 for WITHDRAW; it bounds receive authority,
  not a source lease. No zero/unknown/infinite age is accepted.
- source_gen and capture_gen identify the sender's independent local physical
  source and capture configuration, both nonzero when ELIGIBLE. When ineligible
  they may be zero or retained descriptive values. They are scoped by sender
  and boot and are never the mesh birth. Eligible ANNOUNCE train_gen is its current
  nonzero timing transport generation; WITHDRAW retains the last
  nonzero train as descriptive scope only. STATUS train_gen is zero without SHADOW;
  with SHADOW it identifies the observed ACTIVE timing train. No standby must
  create a transmit train to become initially eligible.
- priority1, clock_class, accuracy, variance, priority2 are unsigned quality
  ordinals compared lexicographically in that order, lower better. The maximal
  field value denotes unknown/worst. These are GM7 ranking ordinals, not an
  imported IEEE class/accuracy guarantee. Full sender identity then boot breaks
  initial ranking ties. Quality does not override eligibility or mesh birth.
  Translation from physical GNSS diagnostics to these scores is a review gate.
- birth_us, founder, term, active, active_boot are all zero together for an
  unbound STATUS or all nonzero together. Birth cannot exceed sent_us. Founder
  is the initial elected GM's full physical identity, not its boot. ANNOUNCE
  MUST be ELIGIBLE. ANNOUNCE and WITHDRAW must have complete lineage, and active/active_boot MUST equal
  sender/boot. It cannot set SHADOW or HOT; shadow_gen must be zero.
- hot and hot_boot are both zero for no nominee or both nonzero. ANNOUNCE may
  name one candidate other than ACTIVE. A STATUS sets them only when asserting
  HOT, and then they equal its own sender/boot. That assertion requires
  ELIGIBLE and SHADOW; matching the current ACTIVE nomination is an additional
  stateful integration precondition, not proven by successful decoding.
- shadow_gen is a nonzero local received-map generation iff STATUS has SHADOW.
  That STATUS must name a complete followed ACTIVE different from itself.
  It describes readiness, not authority; local affected source changes invalidate
  the map independently. An eligible initial STATUS may have no shadow/lineage.
- Only ANNOUNCE may set YIELD. Its yield_us is zero iff YIELD is clear; otherwise
  it is valid UTC, sent_us < yield_us <= sent_us+1,000,000, and a HOT is named.
  Repeated messages cannot change the scheduled boundary of a pending yield.

## Validation, finite freshness and inventory

Reject wrong exact length, magic, version, reserved bits, kinds, field ranges,
partial tuples, unknown flag bits, invalid role relationships, group/key epoch
or MAC before any state/sequence updates. Unknown revisions are not extensible
here. `decode` authenticates and validates structure; it does NOT accept timing
or ACTIVE authority. `Verifier.accept` additionally checks a caller-supplied
independent UTC interval `[utc_lo,utc_hi]`, width at most 200,000 us, from
qualified GNSS or a separately trusted coarse UTC source. It cannot be derived
from the very packet or its BLE shadow. Unknown bounds, leap ambiguity and
unanchored RC bootstrap cannot use this acceptance path.

Require sent_us <= utc_lo+100,000 and utc_hi-sent_us < max_age_us. Thus a birth
later than send is invalid, and birth cannot be more than 100,000 us ahead of
the independent lower UTC bound. A recent authenticated sender can still lie
about an ancient birth; PSK authentication does not independently prove history.
Remaining receive authority is min(max_age_us, max_age_us-(utc_hi-sent_us)),
converted once into a checked local monotonic expiry. The interval is half-open;
exact expiry is stale. Accepted packets cannot refresh on replay. For a known
sender/boot, sequence must increase strictly and send time cannot regress.
Invalid MACs do not consume sequence. The reference cache never silently evicts;
bounded memory, retired-boot records and key-change handling require review.

A valid HMAC and a plausible timestamp prove neither a live first contact nor
replay-safe cold boot. A first-seen recent replay is explicitly possible even
with a coarse/qualified UTC anchor; the test suite preserves that limitation.
The reference `Recent` object represents only bounded-recent authenticated
claims. Any requirement for stronger first-contact liveness needs a separately
reviewed integration mechanism. GNSS-less RC secure cold-start/bootstrap is an
explicit unresolved gate; old sequences/timestamps cannot silently solve it.
There is no challenge/response protocol added by this draft.

Each verifier and policy inventory belongs to one (group,key_epoch). Mixed
current group/key inventories fail closed. Policies consume one current accepted
snapshot per sender, from the same local
monotonic clock and comparable UTC convention. The caller is responsible for
retaining/updating accepted snapshots and known term floors. Normal ingestion
replaces an older same-boot snapshot with its newer verified sequence before
ranking; it must never pass append-only packet history to the inventory API. Exact repeated
snapshots deduplicate. Conflicting current snapshots with the same full sender
identity quarantine that identity, even if one is ineligible or uses another
boot. Malformed, unanchored or expired rows cannot make ranking order-dependent.
Policies are pure reference functions; manually constructing `Recent` in Python
is a test seam, not an authentication bypass an integration may expose.

`initial_choice` computes roster quality ranking only, and suppresses election
when its current inventory contains an eligible ANNOUNCE at or above the
retained term floor. Both initial_choice and merge_choice take the same
known_terms input: a returning lower-term former ACTIVE cannot suppress the
election indefinitely after being excluded from merge. A WITHDRAW
is excluded from both contender selection and election suppression. It does not run the
initial discovery/quiet interval or grant permission to serve. `merge_choice`
selects the winning recent eligible ACTIVE after known-term filtering;
`inherit_selected` requires that exact selected current result. A caller must
retain the winning lineage while live and cannot replace it using a partial
cache that omits its current ACTIVE. Full historical arbitration, nominee
confirmation and takeover authority are outside these initial functions.

## Pre-approval correction and freeze

This pre-approval freeze supersedes the earlier review digests:

- `2f4f2ec00988b5d09636ac2ff0a0c018db48a75e1ab0e81583f305dadcf617e4`: original draft.
- `3e87941a9f96c6a5ee818713a73f93352ad581973582cf371bda78b69ad122c8`: interim source-withdrawn YIELD correction.
- `b3d8618f7b18e32d1b955c79b58a6a0eedf8b3c942c000fbc37f5f31ac4874c6`: WITHDRAW and UI plan, before explicit event redundancy/windows.

User/overseer review replaces that interim mechanism with explicit one-way
WITHDRAW, while ANNOUNCE/STATUS remain the recurring kinds. Wire offsets and
162-byte envelope are unchanged. Term filtering, original-evidence liveness,
nonzero PSKs and eligible unbound STATUS remain. The added frozen
[diagnostics/UI migration plan](INTEGRATION_PLAN.md) is required implementation
scope, with separate diagnostic layout review gates. No firmware approval is
implied. This is an explicit pre-approval correction, not silent alteration of
a reviewed edition.

## Provenance and review gates

All authored files are original MIT reference material credited to Jonathan
Yantis. Vectors are synthetic code fixtures, not imported evidence or measured
hardware datasets. Read-only design context was root/protocol/T096 README,
T096 build guide, `docs/t096/gm-standby.md`, and current
`firmware/t096/protocol/{gm_standby.inc,election_policy.c}`. Source165's
`14c97d9` lineage change uses a boot/identity-derived 32-bit label and identity
merge; this proposal deliberately specifies UTC birth and age-first merge.
No existing production source, contract or historical hash is changed.

`manifest.json` records SHA-256 and byte size for every draft artifact plus the
parent README. It excludes only itself to avoid self-reference. Its external
SHA-256 is reported with the review result; hashes establish identity, not
accuracy or reviewer approval. Verification compares bytes and rejects missing
or extra files (except Python cache files), then runs known answers/tests. It
never regenerates the manifest. Amendments require a new explicit draft revision
or an explicitly reviewed pre-approval re-freeze; never silently bless changed
bytes using the verifier.

Pending before firmware clearance: overseer approval of this draft and wire
assignment; physical quality mapping, comparable UTC and leap handling; secure
RC bootstrap/first contact; full snapshot/term/boot cache lifetime; bounded
nomination/readiness and short-blackout retention; timed-yield persistence and
missed-yield behavior; transport/controller capacity and scheduling; source-map
invalidation and servo integration; initial election quiet interval and split
partition behavior; and adversarial end-to-end tests. Firmware implementation
and native builds belong to a separately cleared engineer. Hardware operation,
precision metrology and sustained multi-GM failover need separate exact-device
scope and acceptance. These offline tests claim none of those outcomes.

## Bounded event repetition and aligned windows

This pre-approval correction responds to the request for redundant YIELD and
WITHDRAW delivery. Each logical event MUST have three scheduled on-air
opportunities in consecutive 250,000-us control frames, spanning 500,000 us.
All copies are byte-for-byte identical: same original sent_us, sequence, scope,
nominee, body and MAC. YIELD starts at t0 and fixes yield_us at t0+750,000 us;
copy starts are t0, t0+250,000 and t0+500,000. A valid first reception of any copy
retains the original deadline; duplicate/reordered copies are idempotent and
may be rejected by the replay cache after the first accepted copy. The event
replaces recurring ANNOUNCE at those three opportunities; no interleaved newer
positive sequence may inadvertently hide its first successful reception.

WITHDRAW stops positive output immediately on the fault/stop decision, then
uses the next allowed control opportunity and two subsequent ones for negative
control only. Immediate output stop is never delayed until the first copy.
Its finite receive lifetime is fixed from original sent_us, at most 1,000,000 us.
A superseding WITHDRAW cancels queued YIELD and every positive serving frame in
that scope. Terminal state prevents old queued positive frames from resurfacing.
A missing/late queue entry is dropped, never moved to a later frame; the
original yield boundary, expiry and blackout deadline cannot slide. The reference dispatch check permits at most the declared 2,000-us scheduling
latency inside that original guarded opportunity, never a new frame. A real
driver must prove that bound and actual airtime. `blackout_first_tx_bound` rounds up to the next aligned control opportunity
after the 2.75s threshold/processing time, adds the complete uncertainty guard
and airtime, and rejects when that completion would exceed original contact+3s.
Processing at exactly +3s is already too late for this bound. This is synthetic
arithmetic, not measured first-TX completion. Late processing after a fixed deadline
reports failure and takes the existing safe fallback; it cannot extend the
3-second target or reschedule the original handoff.

Repetition is redundancy, not guaranteed RF delivery. If all copies are lost,
there is no new event knowledge and the existing armed-HOT blackout fallback
applies. If alignment becomes untrustworthy, do not claim scheduled receipt or
transmit in guessed windows. Fail closed for this aligned schedule, retain
truthful source/armed state and use the separately reviewed safe listening/
blackout fallback. Initial discovery without a trustworthy ACTIVE map remains
a separate integration gate; this model does not bootstrap that map.

The following **synthetic schedule** is checked by `schedule.py` and
`test_schedule.py`. Frame phase derives from the currently accepted ACTIVE
timing-map frame, never each device's independent boot/receipt time. The
250,000-us frame contains four nominal 62,500-us timing periods numerically;
actual radio coexistence/airtime is unmeasured. Values below are offsets in us.

| Window | ACTIVE owner | Standby owner |
| --- | --- | --- |
| [0,10000), [62500,72500), [125000,135000), [187500,197500) | Reserved primary timing work, including its separate guard budget | Primary timing receive work as required |
| [30000,52000) | Control TX nominal [40000,42000), otherwise guard/turnaround | Required authority/control RX, including armed HOT |
| [80000,120000) | STATUS control RX | One scheduled STATUS TX in every eighth frame |

The synthetic uncertainty guard is mapping uncertainty 2,000 + accumulated
drift since sync 500 + scheduling latency 2,000 + radio turnaround 1,000 =
5,500 us per side. Airtime is included separately in the nominal 2,000-us
control and 2,000-us STATUS intervals. Both are hypothetical >=2,000-us airtime
budgets; actual PHY/LL/AUX overhead, fragmentation and radio-mode overhead must
be measured and may require larger windows. Require guard <=8,000 and guarded TX
interval contained in peer RX. These are explicit test assumptions, not measured
hardware budgets. Increase drift with elapsed synchronization age; armed HOT
must preserve listening coverage through the intended blackout using that
larger bound. Exhaustion disables this aligned schedule, not the uncertainty
check. Opportunistic diagnostic scanning is not control RX coverage.

STATUS nominal start is 90,000 + (full_identity mod 8)*2,500 us in every eighth
frame. Jitter is independently bounded to +/-250 us INSIDE this window, not
added across receive-window boundaries. This is a stricter subset of the earlier
2s +/-100ms interval budget: adjacent jitter extremes change the actual interval
by at most 500 us. No roster, coordinator or slot-allocation handshake is needed.
IDs can collide in a slot; this mechanism does not guarantee collision freedom,
particularly with many clocks, persistent interference or correlated loss.
Resolution/listening/fairness must be assessed before firmware clearance.

Example with frame origin 1,000,000 us: control event starts at 1,040,000,
1,290,000 and 1,540,000; fixed YIELD boundary is 1,790,000. The last copy's
nominal end is 1,542,000, leaving 248,000 us to the boundary before separately
budgeted end-to-end processing/clock error. A first successful third copy cannot
restart a 750ms yield timer. Required processing plus uncertainty must fit that
remaining margin or the timed action is unsafe. A WITHDRAW with this origin
expires at 2,040,000, never one second after a later successful copy.

The tiny EventBook helper assumes prior authenticated freshness, exact-current
scope and HOT authorization checks; it models idempotence/supersession only.
It never grants takeover, actual delivery or firmware scheduling conformance.
