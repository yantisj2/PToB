# RC8 LoRa contract, wire version 3

Copyright (c) 2026 Jonathan Yantis. MIT.

T0960.15.126 uses80-byte timing packets and128-byte descriptive companion
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

## Source and authority

Only the exact privately configured transmitter factory can transmit, with the
ordinary GM profile, TX requested, service allowed and no radio fault. Actual
serving-GM election is not required. A locked authenticated selected-BLE follower
needs no local PPS. An actual serving GM retains its own qualified PPS/source
gate. Neither a local GNSS fix nor an alias may replace the selected source.
Every other enrolled device defaults to passive receive; the configured
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
functional selection. ON reacquires. Page7 release between1.5 and4s toggles;
4s poweroff, wake-only and navigation behavior remain. Stop retains its original
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
