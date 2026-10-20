# RC8 LoRa contract, wire version 2

Copyright (c) 2026 Jonathan Yantis. MIT.

**Wire2 contract, integration revision3. Runtime and output precision remain
unqualified until the separate native and exact-device gates pass.** RC8 identifies the LoRa source class independently
of RC7 BLE wire/release numbering. Version 2 replaces the first offline176-byte
wire1 draft; those layouts are incompatible. Version1 was rejected because its
mandatory12-second bootstrap expiry defeated continuous passive use and because
its492ms frame repeated too much metadata. The revised contract separates
functional availability, replay freshness, source authority and uncertainty.

The [codec](../../firmware/t096/protocol/lora_rc8.h),
[policy/export interface](../../firmware/t096/include/lora_rc8_policy.h),
[machine limits](contract.json) and [synthetic vectors](vectors.json) define this
revision. The normal T0960.15.123 integration supplies source adapters, a separate
functional estimate, page7/console controls and a low-priority radio owner.
Startup requires an explicit private exact-factory build binding; its default
zero disables LoRa hardware. RC9 remains absent. Native memory and physical
coexistence/output gates are separate from portable host checks.

## Identity and source rules

Only the privately enrolled exact TC01 factory identity can transmit, in runtime
GM profile with TX requested ON and a currently valid selected source. Actual
serving-GM election is not required. Its volatile default is TX requested ON;
RC profile inhibits TX. All other enrolled T096s default to passive RX ON in
GM/RC. No listener TX, request, response or ranging command is permitted. Labels,
aliases, BLE role and UI grant no identity permission. TC01 cannot enable RX or
consume its own export. A listener rejects its own root (a returning descendant)
and an emitter other than the configured transmitter factory.

An actual serving GM exports its valid own-GM map. A GM-profile follower exports
only locked authenticated selected BLE GM time, preserving original root/boot/epoch;
an unavailable follower source fails closed without unrelated local-GNSS
substitution. Follower TX requires the actual BLE delivered-map PLL lock, with
no local GNSS/PPS requirement; an actual serving GM retains its qualified own
PPS/source requirement. RC8/RC9 cannot feed that export. Source owners must supply complete
immutable snapshots, including full factory-root resolution. Every map's
exclusive `until_tick` is the minimum original map, source, session, permission
and publication limit. Source/map transitions atomically close the old interval;
`rc8_export_close` only shortens it. Preserve the applicable old map through
TxDone resolution rather than applying a later fit to an earlier event. No new
independent GM/HOT withdrawal policy is introduced.

Integration revision3 corrects one predeployment validator assumption: an own-GM
root factory equals its emitter factory, but its logical root boot/profile
incarnation can differ from the physical emitter boot. Both nonzero boots remain
independently authenticated, exactly scoped and visible; neither is substituted.
The68/80-byte wire layout and MAC domains are unchanged. The sealed offline
revision2 draft remains preserved separately.

Group HMAC authenticates symmetric group membership, not hardware attestation.
A member possessing the PSK can forge another emitter's claim. Exact factory
restriction is a local policy, not asymmetric proof of the physical sender.

## Compact authenticated frames

Every packet has a4-byte header: ASCII `R8`, version byte2, type byte1(metadata)
or2(timing). All integers are unsigned little endian; no packed-C layout is
assumed. Metadata authenticates a complete scope, and its full128-bit MAC is
carried as the exact context reference in every timing packet. No shortened
hash, alias, partial tag or ambiguous metadata index is used.

**Metadata68B**:

| Offset | Bytes | Field |
| --- | ---: | --- |
| 0 | 4 | header, type1 |
| 4 | 8 | exact emitter factory identity |
| 12 | 8 | emitter boot incarnation |
| 20 | 8 | stream incarnation |
| 28 | 8 | original root factory identity |
| 36 | 8 | original root boot incarnation |
| 44 | 4 | original source epoch |
| 48 | 1 | scale:0 root-relative ns;1 UTC POSIX ns |
| 49 | 1 | kind:1 own GM;2 selected authenticated BLE |
| 50 | 2 | reserved zero |
| 52 | 16 | metadata MAC and full context reference |

**Timing80B**:

| Offset | Bytes | Field |
| --- | ---: | --- |
| 0 | 4 | header, type2 |
| 4 | 16 | full metadata MAC/context reference |
| 20 | 4 | current timing packet sequence |
| 24 | 4 | corrected prior timing packet sequence, or0 |
| 28 | 4 | original prior event's map generation |
| 32 | 8 | actual prior TxDone in original source ns |
| 40 | 8 | original prior event's source creation ns |
| 48 | 8 | original prior event's exclusive source expiry ns |
| 56 | 4 | original map age at TxDone, microseconds |
| 60 | 4 | upstream bound ns, `UINT32_MAX`=UNKNOWN |
| 64 | 16 | timing MAC |

With no correction, bytes24..63 must all be zero. Otherwise correction sequence
is nonzero and less than the current timing sequence, generation is nonzero,
and `created <= event < expires`. Scope identities, boot, stream and source
epoch are nonzero. Own-GM root/boot equal emitter/boot; selected-BLE root differs
from emitter. The stream pins all scope fields. It is a strictly increasing
64-bit counter within each unpredictable nonzero emitter boot. A scope change
requires a higher stream. Never wrap stream or timing sequence: after saturation,
rotate the stream (or boot if stream itself is saturated). Original root epoch
must not silently wrap within its root boot. Metadata has no timing sequence and
its physical event never trains a clock.

MAC input is the8-byte domain `PTOBR8M2` or `PTOBR8T2`, then existing G1 context
`u8(1)||u64(group_id)||u32(key_epoch)`, then metadata bytes0..51 or timing
bytes0..63. Use the32-byte group PSK, HMAC-SHA256 and first16 digest bytes. Wrong
size/version/type/flags/reserved fields, noncanonical data and bad MAC fail
closed with cleared decode output. Both frame types have independent Python/C
known-answer vectors and exhaustive single-bit mutation tests.

Root-relative ns has meaning only within its root/boot/source epoch. UTC POSIX
ns retains the source's original UTC association; no TAI/leap/smear encoding is
provided. A discontinuity requiring a leap/step/association change ends that
source epoch and stream; do not bridge it silently.

## Tiny cache and replacement

One authenticated metadata scope and16-byte context reference are cached. A
timing packet without an exact matching unexpired cache entry is dropped and
counted without advancing sequence or training a sample. Cache decoding lifetime
is120seconds from the metadata's original physical receive event. Proposed
refresh is one metadata packet per60seconds, yielding to timing follow-up and
BLE work. Missed opportunities are skipped; there is no catch-up burst.

An identical metadata refresh may move only the decoding-cache deadline. It
never resets sequence replay floor, timing history, original event/source
expiry, optional time-anchor origin or assurance deadline. This remains true
when metadata arrives after the previous decoding cache expired. A different
scope in the same emitter boot requires a higher stream; same/lower incarnation
is rejected, including counter wrap. Accepted source/stream changes clear old
history/sample/anchor state before any new timing can be admitted. No correction
crosses contexts.

An unfamiliar emitter boot cannot replace a still advancing current stream
until its last authenticated timing packet is at least12seconds old. The
immediately retired boot is remembered and rejected; this is bounded rollback
protection, not an unlimited persistent boot ledger. Later boot freshness cannot
be proven without independent information and is UNKNOWN. A cold reset/RX OFF
loses this volatile continuity. No boot ordering is inferred from random values.
With60second metadata refresh, an unassisted late join or new boot can wait up
to one refresh period after that gap, plus two timing opportunities. Source
changes in the same boot use the higher stream immediately.

## Original event and correction

Before submission, the transmitter freezes a packet sequence, complete source
map/envelope, preparation tick, capture epoch and unique hardware generation.
Only matching physical TxDone can resolve it; wrong generation, epoch or
callback cause cannot retire it. Source transition may shorten that pending
map's event interval. Preparation-to-TxDone is at most1second. Local transmitter
hardware tick/epoch are association proof retained locally, not repeated wire
authority. The later authenticated correction carries the exact source-time
projection, map generation, original creation/expiry, age and bound from that
frozen event. A new stream drops any unreported prior-context correction.

Packet K's exact payload and physical RxDone are frozen before IRQ clear/FIFO
reuse/rearm. One bounded ingress and two history entries retain original timing
packet sequence, capture epoch/tick, receiver generation and original monotonic
event instant. Metadata consumes no timing-history slot. Drop newest on overflow
and count it. A later J corrects only a retained K in the same full context,
within8seconds of K's original RxDone including worker delay. J's arrival,
callback/service times and current maps never stand in for K. Out-of-order
reports cannot regress the last sample. All policy calls are serialized in a
single future worker; these are not concurrent IRQ queue primitives.

The80B frame deliberately omits current preparation/map/envelope duplicates.
The receiver cannot compare the corrected map against a redundant earlier
header; instead the transmitter's production tuple enforces packet/map/event
association, and the later group-authenticated correction is the original map
claim. It is not independent evidence that the sender projected time correctly.

The physical extended capture tick/epoch is authoritative. Convert an event's
coarse monotonic deadline conservatively from a paired current same-epoch
capture+monotonic read, subtracting the ceiling elapsed ticks/16. IRQ observation
and worker time remain diagnostic arrival times. If the epoch/conversion cannot
be established, reject timing rather than renewing an old event's expiry.

## Four separate outputs

1. **Functional clock availability:** a valid group-authenticated, context/event-
   matched observation may be exposed for less than12seconds from its original
   RxDone. A new matched physical event can sustain the stream indefinitely;
   reception/metadata/polling alone cannot. Lost corrections eventually expire
   the estimate. This is an unqualified RC8 functional estimate, even when the
   source's original live permission has already expired.
2. **Replay/freshness assurance:** cold passive reception is UNKNOWN. A complete
   recorded stream can replay successfully after cold reset; tests demonstrate
   that limitation. It may still be exposed as FUNCTIONAL_UNVERIFIED. Do not
   pretend that MAC, increasing sequence or event matching proves absolute
   freshness. Optional independently fresh BLE/control/external admission can
   anchor a conservative time interval to receiver monotonic continuity. Initial
   width<=100ms and oscillator bound<=100000ppb are explicit unqualified inputs.
   Advance its uncertainty with elapsed receiver time; check the corrected event
   against that interval at its original RxDone, allowing at most1second older
   residence. Current anchored assurance ends after at most1hour or1second
   interval width, without disabling functional reception. Thereafter an
   uninterrupted authenticated stream retains only STREAM_CONTINUITY. New
   source/context, clock discontinuity or a12second sample gap loses that
   assurance; cold/unknown functional acquisition remains possible. Relative
   increments must be monotonic and agree within1second plus100ppm of elapsed
   local time. Continuity cannot bound absolute replay age over arbitrary time.
3. **Original source authority:** only a still-valid independent anchored
   interval can classify the old event envelope as CURRENT (upper bound before
   original expiry) or EXPIRED (lower bound at/after expiry). Otherwise authority
   is UNKNOWN. Neither functional availability nor continuity grants current
   BLE/source permission. A valid historical correction remains historical.
4. **Output uncertainty:** always UNKNOWN in this phase. One transmitter cannot
   perform four-timestamp ranging; path/modem delay and independent output
   accuracy are unmeasured. Upstream bound or fit residual is diagnostic only.

Usable ordinary BLE always wins. `rc8_select` requires explicit
`allow_unverified` to select functional UNKNOWN/CONTINUITY RC8 when BLE is
unusable. This enables the intended lower-quality passive secondary stream
without inventing a strong-freshness dependency. Integration must label this
status honestly. No LoRa estimate trains an RC7 publisher, becomes GM or grants
future RC9 permission. An independent anchor never gets renewed by metadata,
packets, corrections or polling. Its1h/1s policy is not qualified oscillator or
UTC accuracy. Source/map age and original expiry remain unchanged in every
output; historical V10's110second lifetime is not inherited.

## OFF, closure and gestures

RX OFF immediately withdraws cache, samples, history and anchor. ON advances the
volatile receiver generation and reacquires; generation saturation fails closed.
TC01 long press toggles TX requested; other devices toggle RX. The proposed
helper arms at1.5seconds and toggles once on release before4seconds. Reaching4s
cancels toggle and preserves poweroff; wake-only, page change, maintenance and
ineligible presses cancel. T096 page7 uses this release gesture; its4-second hold
still requests poweroff. The `lora` query and `lora 00`/`lora 01` use the same
volatile request handler and cannot select an arbitrary transmit role. OFF/ON
clears the receive cache/history and reacquires; requests are not persisted.

Stop first inhibits input/TX and withdraws selection, then drains within the
original200ms policy limit; repeated requests cannot renew it. Quiescence needs
actual DMA retirement, radio quiescence (including asserted reset where
applicable), FEMoff and capture-release acknowledgements. Reset is not called
standby. Missing acknowledgement/deadline expiry faults locally without releasing
DMA ownership. Tests cover all eight phases and each missing acknowledgement.
This is policy proof, not physical cutoff qualification. BLE ownership and
existing common closure deadlines remain unchanged.

## Airtime and whole integration budget

Calculated assumptions: SF10/BW500kHz/CR4/6,12-symbol preamble, explicit header,
CRC, low-data-rate optimization OFF. Airtime is221696us for68B metadata and
258560us for80B timing, versus209408us for historical64B and492032us for the
first176B draft. One timing packet carries a prior correction; no extra
unaccounted follow-up exists. At proposed4s timing plus60s metadata the total is
**6.833493%**. At30s timing plus60s metadata it is**1.23136%**; that slow timing
cadence cannot sustain12s functional hold and is only a transport comparison.
Startup through the first usable correction is one metadata plus two timing
packets: **738816us total air**, versus984064us for two176B packets. Metadata
loss/mismatch adds explicit reacquisition time and drops; no hidden retries.

Future provisional80B budgets are340ms chip TX timeout and360ms independent
FEM cutoff, with1s preparation-to-TxDone. Radio parameters, timing tolerances,
physical packet/cutoff behavior and regional operation remain integration gates.

The portable sizes are receiver504B, exact export144B, packet64B and offline
TX helper264B. Runtime uses a504B mutually exclusive role union: either the
receiver or two144B exact source intervals plus bounded TX state. The224B
published diagnostic record is a single latest association, with no ring.
The functional mapper projects only the original accepted event by elapsed
receiver monotonic time, with UNKNOWN one-way delay, freshness and total error.
It cannot renew its12-second event hold. Normal BLE wins while usable; an
actual own GM remains a separate preferred normal source.

The earlier4052B allocation remains a planning envelope, not an integrated ELF
measurement. Keep the4096B total added-static ceiling,1024B original minimum and
prefer4096B free; never reduce BLE/controller/USB/critical stack reserves. The
2048B worker runs at priority11 below normal owners. Native known C chains in
the integration are1416B TX and1576B RX, including worker/dispatch frames and
nested TinyCrypt key setup, before unmeasured kernel/libgcc/exception costs.
These sums are not stack high-water; `lora` exposes actual unused worker stack
(orUINT32_MAX when unavailable). No heap is permitted.

Two history entries retain formulas actually published by the source owner.
The new interval starts at the capture inside its locked publication commit;
the old interval closes there. Original affine anchors and original expiry
remain fixed. Missing/evicted maps are dropped and counted. A periodic metadata
refresh waits when the next timing follow-up is within the360ms control guard;
there are no catch-up transmissions. Physical event conversion uses a paired
current capture/monotonic sample and subtracts the rounded-up tick delta,
never IRQ observation or worker arrival time.

Operational radio faults stay latched, inhibit RF restart and leave healthy BLE
running. Timely acknowledgment of DMA retirement, reset-asserted quiescence,
FEM off and capture release permits maintenance recovery. Failed/late200ms
closure remains an error; retrying cannot extend its original deadline.

## Reproduce and integrate

```sh
CC=clang python3 firmware/t096/tools/lora_rc8_tests.py \
  --deps artifacts/deps --out artifacts/check-rc8
```

An optional `--arm-reference` points to an existing native build supplying
pinned CortexM4 compiler flags; new objects and `.su` files go only into the new
output directory. Existing build/report/log artifacts are not changed. The
runner checks independent metadata/timing vectors, all bit mutations, malformed
frames, strict identity/profile/source choice, TX generation/map capture,
metadata miss/replacement/expiry/refresh, replay/saturation, cold replay labeling,
continuous920-event reception beyond one hour, anchored-to-continuity downgrade,
original expiry, loss/overflow/OFF, closure and gestures under ASan/UBSan.

The focused runtime checks additionally run the actual PT GM/follower adapters,
source withdrawal/no-GNSS-substitution, worker cancellation/retry, cache/hold,
source changes, saturation and production UI gestures/rendering:

```sh
CC=clang python3 firmware/t096/tools/lora_source_owner_tests.py --deps artifacts/deps --out artifacts/check-rc8-source
CC=clang python3 firmware/t096/tools/lora_runtime_tests.py --deps artifacts/deps --out artifacts/check-rc8-runtime
CC=clang python3 firmware/t096/tools/lora_hw_tests.py --out artifacts/check-rc8-owner
```

All are fake-boundary checks without device opens. The integration must audit
TIMER3 CC4/PPI/GPIOTE ownership, leaving CC0 PPS, CC1 BLE ADDRESS, CC2 reads and
CC3 USB SOF intact; TIMER4 independently owns FEM cutoff. Never transplant legacy
raw-MMIO/fixed PPI/GPIOTE IRQ ownership. Physical coexistence and independently
measured output remain separate exact-device gates. Future RC9 is one LoRa-to-
BLE hop for direct clients only, without relay, GM promotion or RC7 training.
No W12/RC9 implementation is part of this contract.
