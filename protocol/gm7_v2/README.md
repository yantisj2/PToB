# GM7 implementation-review revision 2

Copyright (c) 2026 Jonathan Yantis. MIT.

**OFFLINE ENGINEERING CONTRACT — pending implementation exit review; not flash
approval or hardware acceptance.** This explicitly supersedes selected design
rules of GM7-draft1 for functional connectionless bootstrap. The earlier
`protocol/gm7/` freeze remains byte-for-byte unchanged, manifest SHA-256
`d200141fc9f4cd2ea91b80843d0bd563d7f349c1ff6c2e40fd4f8a7415006ef4`.
The [clearance/handoff](../../docs/t096/gm7-firmware-clearance.md) defines the
normative owner, source, scheduler and consumer integration requirements.

The older draft's UTC-anchored replay-only admission cannot securely bootstrap
an unanchored receiver. This revision adds bounded, nonce-echo admission carried
by ordinary connectionless advertisements. It is a receiver authentication
exchange, explicitly not HOT appointment/acknowledgement, a connection, a GATT
service, a grant or a coordinator. No final response/confirmation message exists.
Control carries sender-monotonic time, so freshness no longer depends on claiming
calibrated UTC or a small absolute UTC error. UTC birth remains the projected
GNSS UTC estimate at first ACTIVE, not a monotonic timestamp or hash.

## Wire and exact layouts

All integers below are unsigned big endian without padding. Header is identical
for all kinds; PDU lengths include a 16-byte HMAC-SHA256 prefix tag. MAC input is
ASCII `PToB/GM7/implementation2`, byte00, then all PDU bytes before the tag.
Group/key epoch must match configuration; nonzero group and key epoch, a 32-byte
nonzero PSK, exact lengths/version/kind/reserved bits are required. Unknown kinds
are rejected. Public fixture key00..1f is not deployment enrollment.

Manufacturer AD wrapper is `[PDU_length+3]:u8 ff ff ff <PDU>` with synthetic
companyffff only. Production identifier assignment remains a publication gate.
The entire AD must fit191 bytes. Kinds1/2/3 are control,4 request,5 anchor,
6 role diagnostics,7 GNSS diagnostics. New diagnostic records are not GS1/GD2
with reinterpreted fields; their distinct `G7V2` marker/version/domain is mandatory.
Legacy strict decoding and fixtures remain intact for explicit legacy paths.

| Offset | Bytes | Header field |
| --- | --- | --- |
| 0 | 4 | ASCII G7V2 |
| 4 | 1 | revision=2 |
| 5 | 1 | kind |
| 6 | 2 | exact total PDU length |
| 8 | 2 | flags, kind-specific |
| 10 | 2 | reserved=0 |
| 12 | 8 | group |
| 20 | 4 | key_epoch |

### Control (kinds1 ANNOUNCE,2 STATUS,3 WITHDRAW)

PDU **174 bytes**, AD **178 bytes**.

| Offset | Bytes | Field |
| --- | --- | --- |
| 24 | 8 | sender |
| 32 | 8 | boot |
| 40 | 8 | sequence |
| 48 | 8 | sent_utc_us |
| 56 | 4 | max_age_us |
| 60 | 4 | source_valid_us |
| 64 | 4 | source_gen |
| 68 | 4 | capture_gen |
| 72 | 4 | train_gen |
| 76 | 1 | priority1 |
| 77 | 1 | clock_class |
| 78 | 1 | accuracy |
| 79 | 1 | priority2 |
| 80 | 2 | variance |
| 82 | 8 | birth_us |
| 90 | 8 | founder |
| 98 | 8 | term |
| 106 | 8 | active |
| 114 | 8 | active_boot |
| 122 | 8 | hot |
| 130 | 8 | hot_boot |
| 138 | 8 | yield_utc_us |
| 146 | 4 | shadow_gen |
| 150 | 8 | sent_mono_us |
| 158 | 16 | MAC tag |

### BOOTSTRAP_REQUEST (kind4)

PDU **96 bytes**, AD **100 bytes**.

| Offset | Bytes | Field |
| --- | --- | --- |
| 24 | 8 | requester |
| 32 | 8 | requester_boot |
| 40 | 8 | target |
| 48 | 8 | target_boot |
| 56 | 8 | nonce_hi |
| 64 | 8 | nonce_lo |
| 72 | 8 | request_sequence |
| 80 | 16 | MAC tag |

### ANCHOR (kind5)

PDU **186 bytes**, AD **190 bytes**.

| Offset | Bytes | Field |
| --- | --- | --- |
| 24 | 8 | requester |
| 32 | 8 | requester_boot |
| 40 | 8 | nonce_hi |
| 48 | 8 | nonce_lo |
| 56 | 8 | responder |
| 64 | 8 | responder_boot |
| 72 | 8 | response_sequence |
| 80 | 8 | sampled_mono_us |
| 88 | 8 | utc_us |
| 96 | 8 | birth_us |
| 104 | 8 | founder |
| 112 | 8 | term |
| 120 | 8 | active |
| 128 | 8 | active_boot |
| 136 | 4 | timing_epoch |
| 140 | 4 | source_gen |
| 144 | 4 | capture_gen |
| 148 | 4 | train_gen |
| 152 | 8 | control_sequence |
| 160 | 4 | valid_us |
| 164 | 4 | utc_error_us |
| 168 | 1 | depth |
| 169 | 1 | reserved |
| 170 | 16 | MAC tag |

### ROLE_DIAGNOSTIC (kind6)

PDU **168 bytes**, AD **172 bytes**.

| Offset | Bytes | Field |
| --- | --- | --- |
| 24 | 8 | sender |
| 32 | 8 | boot |
| 40 | 8 | sequence |
| 48 | 8 | sampled_mono_us |
| 56 | 8 | birth_us |
| 64 | 8 | founder |
| 72 | 8 | term |
| 80 | 8 | active |
| 88 | 8 | active_boot |
| 96 | 4 | timing_epoch |
| 100 | 4 | valid_us |
| 104 | 8 | hot |
| 112 | 8 | hot_boot |
| 120 | 4 | source_gen |
| 124 | 4 | capture_gen |
| 128 | 4 | train_gen |
| 132 | 4 | shadow_gen |
| 136 | 1 | role |
| 137 | 1 | known |
| 138 | 2 | blockers |
| 140 | 4 | hot_duration_ms |
| 144 | 4 | active_duration_ms |
| 148 | 4 | pps_duration_ms |
| 152 | 16 | MAC tag |

### GNSS_DIAGNOSTIC (kind7)

PDU **156 bytes**, AD **160 bytes**.

| Offset | Bytes | Field |
| --- | --- | --- |
| 24 | 8 | sender |
| 32 | 8 | boot |
| 40 | 8 | sequence |
| 48 | 8 | sampled_mono_us |
| 56 | 8 | birth_us |
| 64 | 8 | founder |
| 72 | 8 | term |
| 80 | 8 | active |
| 88 | 8 | active_boot |
| 96 | 4 | timing_epoch |
| 100 | 4 | valid_us |
| 104 | 4 | source_gen |
| 108 | 4 | capture_gen |
| 112 | 4 | train_gen |
| 116 | 2 | known |
| 118 | 2 | values |
| 120 | 1 | sats |
| 121 | 1 | clock_class |
| 122 | 1 | accuracy |
| 123 | 1 | priority1 |
| 124 | 1 | priority2 |
| 125 | 1 | reserved |
| 126 | 2 | variance |
| 128 | 2 | hdop_centi |
| 130 | 2 | rmc_age_ms |
| 132 | 2 | pps_age_ms |
| 134 | 2 | gga_age_ms |
| 136 | 4 | utc_error_us |
| 140 | 16 | MAC tag |

## Normative field semantics and admission

Control keeps draft1 roles, lineage comparisons, source/nominee separation,
no-wrap integers and half-open finite ages. Flags bits0..3 remain eligible,
shadow,HOT,YIELD. STATUS-only bit4 means GM-capable; a receiver STATUS without
it cannot assert eligibility or HOT. ANNOUNCE and WITHDRAW are inherently GM
records and must not set bit4. Unbound STATUS may have sent_utc_us=0; a WITHDRAW
may also use zero when UTC was lost, since authenticated monotonic freshness
is now independent of UTC. ANNOUNCE requires valid UTC and eligibility.
`sent_mono_us` is the original sender monotonic capture of record construction;
it is immutable across repetitions. No epoch, boot or nonce is derived from it.
YIELD fixes yield_utc_us=sent_utc_us+750000; the action deadline is the corresponding
original sent_mono_us+750000 mapped through the accepted physical timing phase.
A UTC step invalidates a pending yield, stops source as required and emits a
monotonic WITHDRAW. No drift/UTC correction slides that fixed boundary.

Source_gen/capture_gen identify independent source scope, train_gen the serving
or followed timing train, and shadow_gen the local shadow association. Birth is
u64 Unix UTC microseconds, range[2000,2100), retained across live succession;
founder and term rules are unchanged. Ordinary source/fit refresh never changes
birth. G1/RC7's existing u32 source_epoch is a **separate timing_epoch token**,
never a narrowed birth. Current binding is established by fresh ANCHOR plus
matching current ANNOUNCE and actual timing packet scope. Diagnostics do not
establish this association. ANNOUNCE source_gen/capture/train plus ANCHOR
timing_epoch must all match the actual owner snapshot; any mismatch invalidates
the timing context. A new request obtains a new descriptor after changes.

REQUEST targets a complete identity and boot learned from passive discovery.
That discovery is a locator only, never live candidacy. requester_boot is a
fresh incarnation. nonce_hi/nonce_lo form one unpredictable128-bit nonce,
nonzero, generated from the existing cryptographic randomness source; counter
alone is insufficient. Bind it to exactly one original request TX timestamp,
target, requester, boot and key epoch. One outstanding request per target;
at most four total. Retries use a new nonce and original deadline, never restart
an old causal window. One original request expires at250000us; first successful
request transmission fixes its TX timestamp. Requests are limited to one per
identity/boot per2s in steady discovery, with bounded fairness/queues.

ANCHOR echoes that nonce and recipient tuple and authenticates the responder's
current independently eligible direct-source/candidate snapshot, or its explicitly
marked relay snapshot backed by live authenticated upstream timing. Header flags: bit0 independent-eligible
(required for direct GM/candidate, forbidden for relay), bit1 currently supplying timing, bit2 functional UTC available, bit3 known
finite UTC error, bit4 relay. Relay depth is1 or2; direct/candidate depth is0.
The separate reserved byte is always zero. A non-serving initial candidate may respond with bit0 and
optional bit2, but MUST zero birth/founder/term/ACTIVE/timing_epoch/train. Such a
reply proves a live qualified candidate, never a time-serving source. It enables
fresh initial ranking, so replayed once-good STATUS cannot hold off election.
For a serving reply the exact lineage/root identity/boot and nonzero timing/train
scope are mandatory. In relay mode responder/boot identifies the immediate
transmitter, active/active_boot the root, timing_epoch the original root token,
capture_gen/train_gen the relay transmitter scope, source_gen=0 (not independent
GNSS), and depth the authenticated route depth. Direct serving requires
responder=ACTIVE and boot=ACTIVE_boot; relay mode forbids that equality. response_sequence is a separate nonwrapping per-boot
counter; control_sequence is the current control-stream floor, not a new control
message. Response flags/eligibility/source permission are captured atomically.

The responder's sampled_mono_us MUST be captured after this fresh request has
arrived and before its response actually transmits. The whole response stays
inside the original requester250ms RTT allowance. Never refresh sampled time
on a retransmitted body. Require a still-outstanding exact nonce, current group,
recipient and target boots, verified MAC, and measured RTT in[0,250000]us.
Consume the nonce once. A response received later, after target/key/boot change,
or after consumption is rejected. Unknown/new candidate responses must not
silently replace a live winner. Server request handling is bounded, no allocation
or radio work on parser/diagnostic tasks; saturated queues simply drop requests.

Causal offset interval is `[request_tx - sampled_mono, response_rx - sampled_mono]`
in local-minus-remote microseconds. It does not assume symmetric one-way delay.
Keep the interval, not its midpoint alone. Expand both ends by at most200ppm
combined-clock drift over elapsed time, using checked arithmetic. This is an
explicit supported oscillator-envelope assumption to validate, not an accuracy
measurement. Reject/reset the session on local/source monotonic discontinuity
or observed drift exceeding the envelope. Maximum session lifetime6min, renew
at225–240s, maximum interval width500ms. Failed renewal cannot extend the
original session. No receipt/fit/diagnostic polling renews it.

Accept subsequent control only for that sender+boot, at sequence>=the anchored
control floor and strictly above the last accepted sequence, with finite age
using the *earliest* mapped original send instant. Reject future/stale claims;
never make a late packet look fresh at receipt. This causal interval is for
freshness, not precision or tight radio windows. A response never itself selects
an ACTIVE, inherits lineage, arms HOT or qualifies a precision map. Wait for a
matching fresh control snapshot and normal election/merge/negative-fence rules.
A serving response supplies only bounded coarse source permission:
`valid_us<=1500000`, clipped to original independent source expiry at snapshot;
subtract the entire measured RTT at receive, including response queueing and
processing before reception. Do not subtract only the on-air portion. A candidate response's valid_us
bounds its evidence only and never grants timing. UTCerror=0xffffffff means
UNKNOWN; bit3 must be clear. Functional UTC validity and calibrated/known UTC
error are independent. Never turn UNKNOWN into zero.

The reference admission functions expect MAC-decoded records and an owner-held
outstanding request. They show causal interval/freshness arithmetic, not a full
nonce manager, source owner, physical timing receiver or authentication review.
Firmware tests must cover actual nonce consumption and resource ownership.


Every eligible ANNOUNCE includes explicit source_valid_us in[1,1500000], clipped
to the original independent source expiry at sent_mono_us. Other kinds require
zero. It is distinct from max_age_us and cannot be minted from control TTL.
Each accepted current ANNOUNCE updates source permission to the earliest mapped
original send instant plus source_valid_us, never receipt+valid_us. Reject if
already expired. Thus 250ms ordinary ANNOUNCE can maintain continuous permission
without 2s bootstrap renewals. The Session reference exercises twelve successive
renewals and exact expiry. Actual output is still intersected with timing packet
expiry, physically matched correction/model lifetime, negative fences and source
health. A candidate-only session cannot create timing output without a fresh
serving ANCHOR/descriptor after selection.

A relay ANCHOR is admitted only while its existing original authenticated root
origin and parent timing permission are live. valid_us is the minimum of their
remaining lifetimes at sampled_mono_us, never a new root permission. Receiver
also checks GRv3 root-origin proof, root/boot/timing_epoch, responder boot/train/
capture and depth<=2 against physically associated corrections. Current GRv3
signed hold horizon/budget and unknown bound propagation remain unchanged.
Renew relay permission only from a newly verified, causally mapped live original
root envelope and current immediate-parent timing data; repeats/polls/ANCHOR
reception cannot revive an expired root. The relay's causal session establishes
parent-monotonic freshness, not root-origin freshness by proxy: retain the
original signed origin creation/expiry and its existing causal enclosure through
the path. Add actual RC1/RC2 production-body tests, not merely these structural
relay vectors. No relay may assert independent GNSS, enter GM election, nominate
HOT or change root lineage. No unsupported BLE50 relay capability is introduced.

## Diagnostic field interpretation

Kinds6/7 have flags=0, original sampled_mono_us, nonzero sender/boot/sequence and
valid_us<=4500000(role) or<=30000000(GNSS). Scope lineage is either all zero or
complete. Bind it to current source/boot/timing_epoch/train/term before rendering
current values; unmatched reports may be explicitly labeled history only.
Repeated records never extend the original cache expiry. Legacy observed-age
labels remain observer-local, not remote duration or UTC mesh age.

Role enum:0 other standby,1 ACTIVE,2 nominated/preparing,3 armed HOT,4 RC.
Known bits0/1/2 cover local HOT/ACTIVE/PPS duration fields respectively; unknown
requires0xffffffff, finite values saturate at0xfffffffe. Durations describe the
sender's owner-derived current state, not proof of continuity between reports.
Blocker bits0..12: noGNSS,noPPS,dwell,shadowUnready,wrongNominee,wrongBootTerm,
staleEvidence,withdrawn,mergeLost,utcUnavailable,alignmentUntrusted,
bootstrapPending,resourceLimit. All other bits reserved zero. Do not use these
reports to grant timing or HOT authority. Detailed event counters remain in the
bounded versioned JSON snapshot rather than overfilling the radio record.

GNSS known bits0..6: FIX,PPS,QUAL,SATS,QUALITY,HDOP,UTC_ERROR. Values bits0..2 only.
True FIX requires RMCage<2500ms; true PPS PPSage<1500ms; true QUAL requires true
FIX/PPS plus independently completed dwell at producer. Known SATS/HDOP require
GGAage<4000ms. Ages65535 unknown,65534 saturated; sats255 unknown; hdop65535 unknown.
Known QUALITY uses the explicitly documented GM7 rank tuple, not IEEE accuracy.
Unknown UTC error is0xffffffff. No diagnostic field substitutes for source
admission, independent uncertainty or precision measurement.

## Reproduction and freeze

```sh
python3 -B protocol/gm7_v2/verify.py
python3 -B protocol/gm7/draft1/verify.py
```

Ten public encoded/decoded vectors cover ANNOUNCE, eligible unbound STATUS,
request, serving/candidate/relay ANCHOR, role/GNSS diagnostics, YIELD and WITHDRAW.
Tests independently mutate every bit, check field bounds and causal
nonce/recipient/RTT/sequence/age boundaries. Native resource, owner and physical
radio tests are engineer exit criteria, not results of this reference suite.
The manifest excludes only itself. The earlier draft1 verifier stays unchanged.

The separately reserved secondary control opportunity and rotating STATUS slot
algorithm are normative in clearance section5 and `windows.py`. The declared2ms
complete-PDU airtime/2ms lateness/8ms guard are candidate backend limits, not
measured controller capability. The offline checks cover containment including
lateness, job non-overlap, response RTT, jitter extremes and same-low-bit identity
slot separation over32 cycles. They cannot establish fair RF delivery.
