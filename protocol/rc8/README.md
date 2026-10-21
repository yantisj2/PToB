# RC8 LoRa contract, wire version 3

Copyright (c) 2026 Jonathan Yantis. MIT.

T0960.15.131 uses80-byte timing packets and128-byte descriptive companion
packets. Both are one-way group-authenticated broadcasts. There is no ACK,
authentication handshake, listener transmission or ranging exchange. Wire3 is
incompatible with [historical wire2](wire2-historical.md); its original
[contract](contract.json) and [vectors](vectors.json) remain unchanged. The
[current machine contract](contract-wire3.json), [synthetic vectors](vectors-wire3.json)
and [production codec](../../firmware/t096/protocol/lora_rc8.h) identify this
layout explicitly. Native, authorized hardware and independently measured output
qualification are separate from host tests. Output uncertainty remains UNKNOWN. Version125 fixes a service preparation
ordering error in124: mandatory context is now populated before validating the
prior correction. Wire3 layout/HMAC domains/vectors are unchanged; the124
physical capture population and its failed acquisition remain separate evidence.

## GM receiver policy in T0960.15.148

Version148 permits passive LoRa reception on a solo GM, including a solo
serving GNSS GM. Recent authenticated BLE participation suppresses GM reception
independently of brief PT/map loss: original10s remote membership, direct
STATUS contact (original120s), current proved/accepted JOIN, or original client
and demand leases. Local attempts and reported root/selected IDs are not contact.
Expiry is evaluated by the progressing serialized owner; no new lease or timer
is created. Silent passive listeners beyond their contact lease are unobservable.
RC reception and the sole configured GM transmitter are unaffected. Manual
`requested` remains separate from `rx_suppressed`; old LoRa UTC/coordination is
fenced immediately, while physical closure still requires every acknowledgement
within the original200ms drain. A stalled owner retains conservative suppression
until the existing watchdog recovery. This is an offline candidate, not durable
HOT or hardware acceptance.

## Source and authority

Only the exact privately configured transmitter factory can transmit, with the
ordinary GM profile, TX requested, service allowed and no radio fault. Actual
serving-GM election is not required. A locked authenticated selected-BLE follower
needs no local PPS. An actual serving GM retains its own qualified PPS/source
gate. Neither a local GNSS fix nor an alias may replace the selected source.
Other enrolled devices request passive receive by default, subject to the GM
contact suppression above; the configured
transmitter cannot receive its own stream. Receiver policy rejects a root equal
to its own factory and rejects an own-GM claim whose root differs from the
configured transmitter. Group HMAC proves symmetric membership, not individual
hardware attestation: another PSK holder can forge an emitter claim.

Usable BLE is always preferred. RC8 can provide a separate unqualified functional
estimate when BLE is unusable, without RC7 publication, GM promotion, RC9 or USB
authority. A cold replay can appear functional and is labelled freshness UNKNOWN.
An optional independent anchor can improve replay assurance but is never renewed
by RC8. Installing an anchor requires a complete validated companion scope;
a timing-only partial scope cannot gain anchored assurance. Original source-envelope authority and functional availability remain
separate: an old event can support a functional estimate after its live source
permission expired. The original expiry stays visible and is never extended.

Version131 uses a separate negative election constraint while an accepted
foreign-root physical pair remains within its original12s functional hold.
It does not assert that the original RC7 source permit remains current.
Timing-only root/epoch is sufficient to inhibit local GM/HOT operation; unknown
boots grant no eligibility. A matching companion resolves the immutable boot
but cannot create or renew that hold. TIME_INVALID withdrawal, OFF, fault and
source/context replacement withdraw it; a new context needs its own matched
physical pair. Cold replay can still inhibit, within the stated UNKNOWN passive
freshness model and symmetric group-key trust.

The canonical root is the original GM factory/boot/source epoch, distinct from
TC01's immediate selected BLE parent. While constrained, election and discovery
seek that root; better local quality or another root cannot override it.
Ordinary local source readmission and HOT negotiation require current direct
authenticated contact with that exact root: a direct JOIN peer/boot and current
captured direct BLE map, or a live admitted direct control peer with a causal
map. A current authenticated explicit source withdrawal retains contact but
grants no source permission; it may complete ordinary coordinated replacement
after the old service permit and existing takeover/output fences.
Relays, RSSI, stale advertisements and cached membership cannot restore it.
All existing source, management, timing, HOT and output fences remain required.
Direct loss withdraws eligibility while the LoRa constraint remains live.
ACTIVE replacement under a live foreign constraint requires its current direct
root's explicit source withdrawal and ordinary admission/output guards; source
map expiry or lost contact alone cannot promote. Self-root descendants remain
rejected for timing. A separately proved currently serving local GM may retire
an older FULL foreign context only when a same-emitter-boot, strictly newer
companion names its exact current profile incarnation/source epoch. That
retirement cannot create time, admission or a GM role. Cold, unresolved,
inactive-origin, old-boot/epoch and replayed self contexts cannot erase the
foreign constraint or extend its lifetime. This permits a completed origin
handover to survive subsequent old-root contact loss without following itself.
Expiry releases inhibition through ordinary source readmission/fences.

This one-way rule cannot prove one GM across complete RF partitions while
permitting isolated standalone GM startup. Hardware/field convergence and HOT
transitions remain separate qualification populations.

A fresh live GM-profile coordination follower is an intentional operating mode,
so version131 does not treat it as20 minutes of unsynchronized-GM failure.
The watchdog distinguishes coordination activity from qualified timing; the
qualified-observation counter and precision refusal flags remain unchanged.
Original lease expiry restores ordinary NO_SYNC recovery. Main/owner stalls,
transport/DMA faults and the independent LoRa watchdog retain their deadlines.

## Timing packet:80 bytes

All integers are little endian; signed rate is two's complement. The header is
ASCII `R8`, version3, then `2 | (scale << 2) | (kind << 3)`. Scale is0
root-relative nanoseconds or1 UTC POSIX nanoseconds. Kind is1 own-GM or2 selected
authenticated BLE. Other header bits and values are rejected.

| Offset | Bytes | Field |
| --- | ---: | --- |
| 0 | 4 | header with type, scale and source kind |
| 4 | 16 | complete immutable context HMAC |
| 20 | 8 | exact original root factory identity |
| 28 | 4 | original source epoch |
| 32 | 4 | current timing sequence |
| 36 | 4 | corrected prior timing sequence, or0 |
| 40 | 8 | exact original physical TxDone projected into source ns |
| 48 | 4 | original event's map generation |
| 52 | 4 | floor((event−original creation)/1000), microseconds |
| 56 | 4 | floor((original expiry−event)/1000), microseconds |
| 60 | 4 | original map age at TxDone, microseconds |
| 64 | 16 | timing HMAC |

The receiver reconstructs creation as `event−age_us*1000` and expiry as
`event+remaining_us*1000`. Both round inward by less than1us; neither extends the
original map's permission. TxDone retains full nanosecond resolution. Zero
remaining lifetime, offset overflow, arithmetic overflow and malformed envelopes
are rejected. An unrepresentable prior correction is dropped/counts missing-map;
the service can still emit an uncorrected timing packet. With no correction,
bytes36..63 are zero. Corrected sequence must be nonzero and less than current
sequence. Sequence never wraps. The timing HMAC domain is `PTOBR8T3` over
bytes0..63, prefixed with the group context described below.

Timing alone identifies the full root and source epoch. A cold receiver can
retain one packet's physical RxDone and match the next packet's prior TxDone
without a companion. Emitter identity is the configured transmitter; emitter
boot, root boot, parent boot and stream remain zero/UNKNOWN until a matching
companion arrives. The opaque context cannot manufacture those fields. A later
matching companion fills unknown metadata without resetting sequence, history,
sample, hold or assurance.

## Companion packet:128 bytes

The companion reports one latest source snapshot; it never supplies a timing
correction or clock observation. Header bytes0..3 are `R8`,3,1.

| Offset | Bytes | Field |
| --- | ---: | --- |
| 0 | 4 | companion header |
| 4,12,20 | 8 each | exact emitter factory, physical emitter boot, stream |
| 28,36 | 8 each | exact root factory and logical root boot |
| 44,52 | 8 each | exact selected parent factory and boot, or both0 UNKNOWN |
| 60 | 4 | original source epoch |
| 64,65 | 1 each | scale and source kind |
| 66 | 4 | diagnostic sequence, nonzero and increasing within context |
| 70 | 4 | source snapshot age at preparation, milliseconds |
| 74 | 4 | latest submitted timing sequence (diagnostic ordering floor) |
| 78 | 2 | coalesced event bitmap |
| 80,82 | 2 each | quality known mask and boolean values |
| 84,85 | 1 each | actual source role and hop |
| 86,87 | 1 each | advertised parent class and accuracy code |
| 88 | 4 | advertised parent uncertainty bound ns, if actually available |
| 92 | 4 | emitter's measured BLE correction age ms |
| 96 | 4 | emitter's original current map remaining life ms |
| 100 | 4 | emitter's measured rate ppb |
| 104 | 4 | emitter's measured residual/RMS ns |
| 108,109 | 1 each | source loss and recovery counters, saturating at255 |
| 110 | 1 | reserved zero |
| 111 | 1 | TIME_VALID boolean; false includes source-loss diagnostics |
| 112 | 16 | companion HMAC |

The immutable context is `HMAC-SHA256(...)[0:16]` in domain `PTOBR8C3` over
companion bytes0..65. It binds all full identities, boots, stream, parent,
source epoch, kind and scale. A companion-only NO_SOURCE scope has exact emitter/boot/stream,
root/root-boot/epoch/parent all0, kind0 and scale0. It must be TIME_INVALID;
normal timing/export validation rejects this scope. A cold transmitter reports
BOOT without inventing a root, using stream1; its first real source uses stream2.
The companion's independent `PTOBR8D3` HMAC covers
bytes0..111; mutable quality never changes the immutable context. All HMACs use
the32-byte group PSK and prefix the body with their8-byte domain followed by
`u8(1)||u64(group_id)||u32(key_epoch)`. The first16 digest bytes are retained.

The quality mask bits0..12 identify lock, holdover, parent PPS, parent GNSS,
parent class, parent accuracy, parent uncertainty, measured correction age,
measured map life, measured rate, measured residual, actual role and hop.
Boolean values use bits0..3 only, and a value without its known bit is invalid.
An absent known bit always means UNKNOWN, even if the stored scalar is zero.
Role0 is unavailable,1 actual serving GM,2 selected-BLE follower; direct follower
hop is1 and actual GM hop0. Parent class/accuracy come only from a connected, authenticated, causally bound
peer matching exact parent factory and boot. Their conservative age uses the
original accepted HELLO map observation, ages out after10s and cannot be renewed
by polling. Companion snapshot age is the oldest included local or parent datum;
it can overstate the age of a newer noncausal HELLO dataset. Unknown or stale
parent data clears its known bits. Parent GNSS/PPS and
uncertainty remain UNKNOWN when that evidence is absent. The emitter's local
GNSS never masquerades as parent GNSS. Fit residual and upstream quality do not
bound delivered RC8 output.

Events bits0..8 are boot, parent, root, source epoch/stream, loss, recovery,
holdover, quality and periodic. The service coalesces events into one bitmap and
one latest snapshot. Companions occur roughly every120s and on significant
changes, at most one per2s. A context change also arms one repeat after16s,
then the ordinary120s period; a newer context replaces that pending repeat.
A semantic event revision lets the submitted bitmap retire even when local
quality polls preempt its encoding. There is no heap queue, backlog replay or catch-up
burst. A current-context TIME_INVALID companion with an increasing diagnostic
sequence and floor at least the current highest timing sequence clears the old
sample/history immediately. A stale floor cannot withdraw newer timing; a
retired context or reordered diagnostic cannot replace it. A recovery companion
alone never creates or refreshes a sample.

## Physical events, scheduling and bounded state

Timing cadence is4s; matching uses the prior packet's original physical RxDone,
within8s including worker delay. Functional hold is less than12s from that
original event. One ingress and two timing-history entries drop newest/count on
overflow. Companion reception consumes no timing-history slot. Source/parent/
boot transitions clear unreported old-context correction and history. Known
same-boot scopes require increasing streams; the immediately retired context
and boot are remembered. Reboot replacement after a12s gap remains UNKNOWN
freshness, not an ordering claim about random boot values. Companion descriptive
metadata ages out after240s without stopping timing acquisition.

Source owners publish two bounded original formula intervals. The new map's
start is the physical capture inside publication; the old map closes there.
Repeated owner snapshots can only shorten the original deadline. A delayed
TxDone is mapped by its original tick, capture epoch and pending stream; a later
fit cannot map an earlier event. Missing or evicted maps are counted and dropped.
Paired capture/monotonic conversion subtracts ceil(elapsed ticks/16), retaining
the original event instant. Same-epoch capture delays up to8s can be resolved;
unavailable/older captures are dropped and RX rearmed without invented time.

The priority11 worker yields to BLE. Timing and its correction take priority over
diagnostics. A diagnostic fits only if its full FEM budget plus1ms launch margin
finishes before the existing next timing deadline. The driver repeats the check
immediately before FEM enable, after SPI preparation and possible BLE preemption.
Healthy EAGAIN deferral consumes no timing sequence or correction and cannot move
the original timing deadline. Timing keeps340ms chip/360ms FEM limits;
companions use420ms/450ms. At SF10/BW500/CR4/6/preamble12, calculated airtime is
258560us timing and369152us companion:6.7716267% occupation at4s/120s before events.
These calculations do not qualify radio delivery, regional use or coexistence.

The measured host ABI uses a712-byte exclusive RX/TX union and304-byte latest
snapshot; target ABI sizes are checked independently. The worker retains2048 bytes
plus64-byte guard. Complete native ELF
comparison must prove the4096-byte added-static ceiling. Existing BLE/controller,
USB and stack reserves cannot shrink. Compiler frame sums are partial estimates;
actual device stack high-water remains required. No heap is used.

TX/RX OFF suppresses every RF packet of the configured role and withdraws the
functional selection. ON reacquires. Page7 fires once at1.5s and consumes the press, so extending it cannot poweroff.
Intentional4s poweroff remains on other pages; wake-only and navigation behavior remain. Stop retains its original
200ms deadline and requires actual DMA, radio quiescence, FEM-off and capture
release acknowledgments. True SPI/ownership/cutoff faults stay latched; stale
capture data is dropped/counts rather than promoted into time.

An original cutoff during an unfinished software SetTx command resets that
attempt. EINTR means RF delivery is ambiguous: consume the proposed packet's
sequence, clear the pending/prior physical correction, retain diagnostic event
bits and complete bounded closure before restart. Timing retries wait at least4s;
diagnostic retries wait at least2s and preserve a future timing deadline. Such an
abort is counted separately from a retired-command timeout, which stays fatal.
A brief final IRQ fence classifies abort versus retired command; no SPI or wait
is inside it. Neither this recovery nor EAGAIN lengthens a hardware RF window.
Version126 additionally recognizes an actual BUSY-high30ms deadline inside
`wait_busy` while opcode0x83 is in phase4, the owner is running, DMA has retired
and no fault exists. It resets the attempt and takes the same ambiguous EINTR
path. A generic ETIMEDOUT is never enough for recovery. Other opcodes/phases,
SPI/callback faults, DMA nonretirement and phase3 timeouts retain their fault
semantics. A competing cutoff/cancellation cannot count the same attempt twice.

Version127 adds a durable fault observer below the existing radio policy. A
first-fault snapshot is captured before inhibition. Real transport faults and
unretired DMA are never cleared by a local restart; the existing30s hardware
watchdog resets the MCU after a journal attempt completes or its2s observer
bound expires. The priority4 observer also detects30s without LoRa worker
progress; absent source or RF packets alone never triggers it. The observer
uses the diagnostic scanner's512B scratch only after startup scan completion,
with no added static RAM or reserve reduction. Version128 advances the original
start cancellation serial before a watchdog trip, preventing a returning start
from releasing reset after the out-of-band fault fence.

Main alone submits the fault to existing journal bank0, with reason0x400 in
schema5. Words80..95 replace the event snapshot with the fault record; the BLE
pre/post snapshots, source/silicon/boot binding, CRC/commit,2s deadline and all
other storage/maintenance guards remain. Recoverable launches share the8/hour,
10s event budget; a first terminal fault has one immediate admission opportunity.
A busy/unavailable/failed journal is reported as such. The existing watchdog
capsule reason6 retains error, stage/op, closure, generation and journal result
in words8,9,10,11,32. Retained RAM survival is conditional; committed flash records
survive same-image resets, subject to existing journal rotation. A newer image
retains the existing startup cleanup rule. No fault record proves RF or UTC
accuracy, and faults remain counted in their original boot populations.

Page7 RX link RMS is the population residual RMS of an affine fit to original
physical RxDone ticks paired with exact corrected TxDone ns. It uses bounded
nonoverlapping blocks of4..16 accepted pairs,100us X units, and nominal-rate-
removed Yns. Integer sufficient statistics are exact within the declared120s/
16ms fit domain; the residual is evaluated in double precision and displayed
with integer-ns truncation. Numeric overflow/domain or degenerate-X conditions
start a fresh block. At16 samples, the next pair starts COLLECTING until four
new pairs exist; an old RMS is never presented as the new block's result.
Context, capture epoch, applicable loss, OFF and original-event gap>=12s reset
the block. Quality-only metadata preserves it. Expired/unavailable timing shows
NO TIMING. The independent Decimal test recomputes every centered residual;
the metric is relative link consistency and includes source/modem/capture
variation. It is neither an absolute error bound nor the companion BLE-fit RMS.
TX-only units cannot measure this one-way link metric and show source validity.

The `lora` console preserves original association/counter fields and adds:
`parent=[factory,boot]`, `companion=[sequence,snapshot_age_ms,event_mask,local_age_ms,metadata_known,time_valid]`,
and `quality=[known,values,role,hop,parent_class,parent_accuracy,parent_bound_ns,correction_age_ms,map_life_ms,rate_ppb,residual_ns,losses,recoveries]`.
Unknown scalar fields require their known bit; output bound is JSON null.
Version125 adds `link_fit=[samples,rms_ns]` (RMS usable only for samples>=4 and
available original timing). Its `hw` array appends `launch_aborts` after
`invalid_capture`: RXdone, TXdone, CRCbad, overflow, SPIok, SPIerror, cutoffs,
capture epoch, generation, error, phase, invalid capture, launch aborts.
Version126 appends `busy_aborts` at `hw[13]` and `last_busy_op` at `hw[14]`.
The BUSY counter saturates at65535; opcode is the last BUSY-high deadline's
command, not proof of whether RF began. Both fields use existing structure
padding: the hardware diagnostic remains52 bytes.
Version127 adds `watchdog=[schema,serial,coalesced,terminal,saved_serial,journal_result,reset_requested]`
and `owner=[stage_opcode,stage_at_ms,worker_beat_ms,monitored,closure]`. Closure bits
are DMA retired0, radio reset1, FEM off2, capture released3 and hardware fault4.
`fault_record` has16 uint32 words: magic0x4c525731; signed errno; fault uptime ms;
stage<<8|opcode; original stage uptime ms; original phase/ownership flags;
generation; low capture epoch; physical tick low/high; IRQ/GPIO flags; stop serial;
SPI success/error counts; cutoff count; launch/busy abort counts (low/high16).
Ownership word5 contains phase bits0..7, running8, DMA-inflight9, latched10..11,
capture-owned12, RX13, launch-abort14..15 and cutoff16. Its journal copy adds
closure bits20..24 as actually observed after closure or the original200ms drain
bound. Word10 contains chip IRQ bits0..15, BUSY-high16, DIO-high17 and GPIO-read
errors18/19. Launch count in the compact record saturates at65535; full counters
remain in `hw`. Stage values0..7 are idle/start/BUSY-before/DMA/BUSY-after/take/
rearm/stop; stage time is diagnostic observation, never a timing sample.
Version129 stage8 identifies a late unretired launch and word4 then contains
the original FEM-enable millisecond; launch-abort reason3 identifies this path.
Version132 records GPIO-owned NSS active/read-error in word10 bits20/21;
asserted reset/FEM-off closure does not claim a stuck NSS became inactive.
Stage9 identifies a precommit/original-finish-deadline abort, with the original
precommit millisecond in word4. Its `launch` array contains budget, precommit,
FEM-start and NSS-commit-upper-bound microseconds32, then committed0/1. These
boot-local observations are not RF timestamps. Successful command bytes start
the unchanged FEM window and release NSS in one bounded GPIO/timer fence;
worker wakeup never renews that window or substitutes for the command commit.
A terminal fault superseding an uncommitted recoverable record increments coalesced; the
first terminal cause stays frozen. Saved serial advances only after confirmed
journal completion. Records with a failed optional spare preparation remain
conservatively failed even if their earlier commit may be readable.

Companion preparation age uses a fresh monotonic snapshot under the owner lock;
polling cannot make the actual parent-advertisement age younger. Page7 omits
parent class/accuracy once preparation plus receive age reaches10s.

## Reproduce

```sh
CC=clang python3 firmware/t096/tools/lora_rc8_tests.py --deps artifacts/deps --out artifacts/check-rc8
CC=clang python3 firmware/t096/tools/lora_source_owner_tests.py --deps artifacts/deps --out artifacts/check-rc8-source
CC=clang python3 firmware/t096/tools/lora_runtime_tests.py --deps artifacts/deps --out artifacts/check-rc8-runtime
CC=clang python3 firmware/t096/tools/lora_hw_tests.py --out artifacts/check-rc8-owner
CC=clang python3 firmware/t096/tools/test.py --deps artifacts/deps
```

Use the build guide's validated recent LLVM sanitizer runner. Independent
Python/C vectors, single-bit mutation, malformed frames, cold acquisition/replay,
quality refresh, diagnostic ordering/loss, original inward envelope, map ownership,
delayed capture, bounded scheduling, OFF/ON, gestures and source gates exercise
production bodies with fake physical boundaries. Native compilation and exact
hardware populations retain their own identities, failures and limitations.
Future RC9 and W12 changes are excluded.


Version129 temporarily runs only inhibited drain work at priority9, with an
explicit wakeup, before returning to ordinary priority11 after complete closure.
Priority10 USB/diagnostic work cannot consume that application's drain budget;
BLE priorities and the200ms deadline remain unchanged. Late acknowledgment
remains a fault with ETIMEDOUT cause, including eventual full closure.

An unretired SetTx returns bounded EINTR if more than20ms (timing) or30ms
(companion) has elapsed from original FEM enable without a latched DIO. Thus its
full340/420ms chip timeout still fits the unchanged360/450ms FEM window at
phase3 retirement. Counters and normal bounded close/cooldown apply; this is
ambiguous delivery, not a successful physical transmission. Stage8 in a fault
record identifies this case; word4 is its original FEM-enable millisecond and
word5 records launch-abort reason3. No actual phase3 timeout is converted to
recovery, and no unretired DMA is reused. These are software contracts, separate
from exact-device timing, recovery and coexistence evidence.


Version130 makes the LoRa-page gesture a one-shot threshold action at1.5s,
with the press claimed until release. Extending that hold cannot poweroff or
toggle again, including a delayed poll or page change. The TX/RX ON/OFF heading
shows the applied request. Other pages retain intentional4s poweroff, and a
screensaver-wake press remains wake-only. Physical button timing remains an
exact-device gate; the firmware tests use the production UI worker with fake
button samples.

Schema6 retains the primary LoRa fault through normal journal activity. Each
later ordinary event carries the same16 words at80..95 and original fault boot
at39..40; its own event boot remains at4..5. Reason0x400 identifies a newly
observed fault,0x800 a carried copy, and0x1000 a terminal primary. These are flags
combined with the ordinary event reason for carried records. Binding, CRC,
commit, BLE pre/post context, bank0 bounds, radio-aware admission, write count and
original2s deadline remain. A new committed copy exists before spare-page erase.
Startup scans/restores the newest valid copy before LoRa initialization without
another RAM reserve. A newer terminal fault supersedes the primary; recoverable
noise cannot replace an unacknowledged terminal. A legacy full journal with no
safe fault handoff slot fails ENOSPC before erasing its sole fault copy. Existing
new-source initialization remains a distinct boundary; archival fault evidence
must be retained before an app update. A reset capsule remains conditional RAM,
separate from this committed flash record.
