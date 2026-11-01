# Passive LoRa v2 timing variants

Copyright (c) 2026 Jonathan Yantis. MIT.

T096 has a separate, explicit `--lora-passive-fast` native variant. Ordinary
images continue to use RC8 wire5. The lab variant boots with LoRa off; `lora 01`
selects a15-minute session for this boot. Diagnostics identify `LAB_FAST_4_12`
and its30% engineering cap. GM/RC changes do not automatically enable it.
The separate `--lora-passive-slow` variant implements the production timing
candidate below. Both require measured hardware acceptance; neither a build nor
the prior lab variant certifies slow operation. Range testing is a later field
test. The v2 name is independent of RC8 wire2.

## Production timing candidate

| Setting | Fast lab, schedule1 | Slow, schedule2 |
| --- | ---: | ---: |
| Precision/control period | 4s | 60s |
| Full META period | 12s | 300s |
| SYNC offset | 2s | 3s |
| Physical expiry | 6s | 62s |
| Original control-start context expiry | 18s | 362s |
| Conservative rolling-hour admission | 1080s | 72s |
| GM/RC boot request | OFF, 15-minute selection | RX ON, no lab timeout |

Only the selected schedule is admitted by an image. DC and PC reject both
variants. BLE remains preferred for normal clock authority; passive RX is not
suppressed by GM participation. An exact configured primary may transmit only
while actually serving its qualified own GNSS source. An optional exact backup
may transmit only after an authenticated primary WITHDRAW or physically timed
YIELD and its own successful GM7 takeover commit. A blackout or cold election
does not authorize backup LoRa TX. On a new primary serving incarnation, fresh
authenticated non-serving backup evidence is required before primary LoRa TX
is admitted. If that peer is absent, TX stays inhibited; RX continues. These
bindings authorize physical emitters, never select a mesh or replace enrollment.

Slow service retains one armed precision/META obligation across proven
pre-SYNC cancellations or BLE/CPU deferral. It retries with a fresh META
rendezvous and actual new SYNC timestamp, subject to unchanged source, storage,
channel and airtime admission. DISC has no retry obligation. META completion
cannot clear an owed precision pair. Each retry is fully reserved without a
refund. After recovery the next regular opportunity is at least60s later;
elapsed ticks do not create a catch-up queue. Thus 300s is the nominal META
period, not a delivery guarantee under sustained contention or missing source.
Eventual delivery requires those admission conditions to become available.

SYNC/FOLLOW software refusals may retry inside their original windows only.
FOLLOW retains its actual250–300ms gap; a possible incomplete physical pair
still latches FHSS disabled. No ACKs, client requests, discovery repeats, late
FOLLOW repair or automatic fault clearing are introduced. Every packet freezes
the original GM and source-map endpoints and their epoch/capture identity.

Historical debt up to1080s remains valid accounting even when slow admission
is72s. A reboot carries all debt for a full new hour. Migration accepts old
zero/fast schedule fields, clears only obsolete scheduler obligation bits and
retains seed, cursor, serial, debt and faults. Unknown schedule/owed bits reject
the record. A slow-to-fast migration uses the same rule. The536-byte checkpoint
layout and dedicated flash allocation are unchanged. Missing or faulted TX
state inhibits TX without disabling receive-only operation. TX/RX transitions
require confirmed hardware closure and completion flush; per-emitter replay
floors and same-boot accounting survive transitions. Slow expiry withdraws the
estimate but retains same-context fit points within the300s fit window; capture
or emitter changes require a new fit.

The slow UI shows `60s`, fresh physical signal and trained relative RMS. It does
not create TZ authority from an unknown-age META. Read-only `lora-diag` provides
a bounded polling record; `lora` retains the detailed fault/launch record. Slow
stores the latest physical audit plus per-kind counters to fit the existing
mutable-state budget. See the [autonomous soak guide](../../docs/t096/lora-soak.md).

## Wire candidate

Integers are little endian. Each packet starts with `4c 02`, followed by a
kind/flags byte and schedule ID. Kind is the low four bits; `0x80` means HAS_PAIR
and `0x40` means TIME_VALID on control packets only. Bits4..5 are reserved zero.
Schedule1 is `LAB_FAST_4_12`; schedule2 is `PROD_60_300`. Unsupported schedules
and a schedule other than the image selection fail closed.

| Kind | Bytes including MAC16 | Body after four-byte header | MAC domain |
| --- | ---: | --- | --- |
| DISC=1 | 28 | cursor:u32, delay:u16, reserved:u16=0 | `PTL2DIS1` |
| META=2 | 112 | Layout below | `PTL2MET1` |
| SYNC=3 | 24 | cursor:u32 | `PTL2SYN1` |
| FOLLOW=4 | 32 | original SYNC cursor:u32, original SYNC TX UTC ns:u64 | `PTL2FOL1` |

The small packet MAC input is the cached full128-bit context followed by its
body including header. META authenticates its complete96-byte body. The MAC is
the existing group HMAC-SHA256 truncated to128 bits. Group trust remains
symmetric membership, not individual hardware attestation.

META offsets are: emitter4:u64, boot12:u64, root20:u64, root boot28:u64,
parent36:u64, parent boot44:u64, source epoch52:u32, stream56:u32, public hop
seed60:u32, cursor64:u32, serial68:u32, original source snapshot UTC ns72:u64,
TZ80:u16, signed offset82:i16, delay84:u16, reserved86..95 zero, tag96..111.
META snapshots the source coordinate before encoding; the actual SYNC BUSY
start is authenticated later by FOLLOW. TZ may be canonical unknown
(`id=0, offset=-32768`). Revision one admits own-reference sources only: root and emitter identities
must match, both boot/incarnation fields are nonzero, both parent fields are
zero, and epoch/stream/seed are nonzero. The emitter boot is physical; root boot
is the actual qualified GM runtime incarnation and may differ. A root change
requires a higher local stream and fresh authenticated META.
Finite upstream-grant forwarding is unsupported.

Context is the group MAC in domain `PTL2CTX1` over META bytes0..63, with the
kind/flags byte normalized to2. It binds full scope, schedule and seed.
Delay is an unsigned10ms listening hint relative to captured control RxDone;
HAS_PAIR=0 requires zero delay. Hints do not renew clock or metadata permission.

The public permutation uses SHA256 Fisher-Yates with rejection sampling over
`PTOB-HOP-MODEL\0 || seed:u32 || counter:u32`. Channel index for physical cursor
`c` is `permutation[(c%64+c/64)%64]`. The64-channel raster is
902.4MHz+0.4MHz*i; DTS rendezvous is915MHz. A seed never proves trust or selects
a mesh. The driver changes neither existing PA drive nor FEM power setup.
The [Semtech driver](https://github.com/Lora-net/sx126x_driver/tree/v2.3.2)
is the PHY reference; actual RF characteristics remain unmeasured.

## Implemented policy and limits

Schedule1 uses4s slots, META every third slot, SYNC at slot+2s and a250–300ms
actual RF gap before FOLLOW. Driver hooks select SF12/BW500/CR4/6 DTS or
SF7/BW41.67/CR4/6 timing, with preamble12, explicit header and CRC.
Reserved durations, including an unqualified10ms reserve, are DISC454416us,
META1142544us, SYNC231951us and FOLLOW268814us. Full-frame reservations must
fit1080s per rolling hour. These are engineering limits, not RF authorization.

The native owner freezes the selected source export's original capture-clock
endpoint for each transmission. Entry and actual SetTx commit require the
whole hardware frame budget and launch margin to fit; a newer source map cannot
renew a staged transaction. At boot admission after the applicable guard,
the fixed monotonic grid starts200ms after an own-source four-second boundary
so the long META fits its1.5s PPS horizon. This chooses phase from an unqualified
UTC label; it does not qualify that label. Service/context restarts retain the
admitted grid. No cadence, slot index, cursor or airtime debt is reset.

The portable ledger retains61 complete minute buckets and can conservatively
refuse traffic near hour boundaries. It reserves the whole pair before
HAS_PAIR, never refunds, preserves control phase and maintains per-channel
20s no-revisit guards. The T096 backend reserves app-owned flash0xd7000..0xdefff; the executable
ends before0xd7000. Credential, diagnostic and recovery ranges stay separate.
It appends576-byte CRC/commit records across eight4KiB pages, verifies writes
before commit and prepares a spare before a transaction crosses
pages. It stops at50000 lifetime commits (fewer than1000 full erase cycles per
page under this append pattern); this is a conservative lab limit, not measured
flash-endurance qualification. The initial factory-bound seed/checkpoint must
be explicitly provisioned by the exact-device installer after proving the area
unused. Boot never manufactures missing state. For a used native checkpoint,
rebasing sums all61 recorded buckets into the current boot's bucket and keeps
that entire debt for a full new hour. No old elapsed time is credited. A4s
control guard and20s per-channel cooldown start at the default-off boot; seed,
cursor, pending faults and distribution index remain persistent. An initial
migration with unknown prior RF still requires an actual RF-silent hour.
A fresh session must be explicitly selected after boot.

A narrowly scoped initial migration may carry an operator-proved silent hour
forward: an installer-only schema2/sequence2 record in slot1 must preserve the
byte-identical, otherwise zero, factory-bound checkpoint and public seed from
schema1/sequence1 in slot0. Qualification needs exact-device physical/boot/build
and RF-counter evidence for at least3600 actual seconds; an image checksum alone
is insufficient. Firmware never writes schema2. It consumes the initial proof
with an ordinary durable record before admission. Any used cursor, slot, debt,
pending state, altered seed or missing original record rejects this path.
Subsequent reboots use conservative debt carry-forward. This is local initial
state provisioning, not an on-air grant or a runtime debt-reset command. Untrusted time,
failed commits or absent checkpoints inhibit admission. Interrupted pairs
disable FHSS; missing checkpoints cannot be synthesized at boot.

The receiver retains original physical events and6s physical/18s context
candidate deadlines. Bounded-age admission requires an independent receiver
UTC interval and independently known source error. Unknown freshness stays
observational. Replay/context floors survive local expiry. A fit keeps at most
16 points spaced at least20s apart within300s and requires at least four points
spanning180s. Every accepted pair is counted even when omitted from fitting.
RMS is relative consistency; no absolute uncertainty, GM/election, relay,
RC7/RC9 or USB timing authority is granted.

Conditional driver hooks capture DTS RxDone and FHSS HeaderValid separately.
TX DIO TxDone also drives TIMER4 CAPTURE1 while preserving the common timer's
BUSY start capture and independent FEM cutoff. This coarse gap measurement is
not a precision UTC timestamp. Launch, RF tail, retune/capture bias, channel
effects, temperature and cutoff behavior need physical qualification.
Passive SetTx uses the chip's single-packet zero-timeout mode; the original
finite MCU compare/PPI FEM cutoff and reset bound remain mandatory. A shorter
independent chip RTC timer must not race that envelope. Terminal diagnostics
retain raw IRQ flags and chip status; a missing journal write is not success.

Storage precharge may start1.2s before a slot without moving the RF target.
The source snapshot is refreshed after precharge, before guarded submission.
SYNC's actual mapping and pair progress are retained in RAM. The whole pair
remains durably pending until the next precharge includes its completed cursor
and cooldown, or a clean stop flushes completion after hardware closure. A reset
before either commit still disables FHSS, even if FOLLOW physically completed.
Failed flushes fail closed; logical pending and checkpoint_dirty are separate.
Stop
ACKs precede fault checkpoint writes so those writes cannot extend that drain.

One priority11 worker owns capture, retuning and the radio. A bounded priority5
lift within350ms of TX transitions prevents the priority6 UI from delaying
launch; storage returns to priority11 and drain uses priority9. Its unchanged2KiB
stack,200ms drain and controller/interrupt priorities remain bounded. Source maps are frozen before
turnover, and completion must match the original capture. Source withdrawal
prevents further RF. After verified closure, a live proof that no FHSS SetTx
was submitted permits cancellation without refunds or cursor changes. Any
possible FHSS transmission or reset ambiguity still latches the incomplete-pair
fault. Existing disables are never cleared by restart or ordinary flashing.
Receivers listen for each DTS control even during successful timing. Their PPS
observer uses the existing causal history and never feeds the estimator.

The native variant has no independent source-age bound, so observations remain
`FRESHNESS_UNKNOWN`. It can display a functional estimate when BLE is unavailable;
it grants no TZ, normal clock, GM, relay or USB source authority. HeaderValid
latency and propagation are uncompensated: raw PPS phase bias and centered
jitter must be reported separately. No absolute UTC accuracy is implied.
The per-kind physical-event audit supports bounded1Hz collection; keep counters,
missing observations and failures. Real RF emission/power/bandwidth, thermal
behavior, precision, installation and slow operation remain separate gates.

## Offline checks

```sh
python3 firmware/t096/tools/lora_passive_tests.py \
  --deps artifacts/public-review/t096-deps --out artifacts/passive-host-01
python3 firmware/t096/tools/lora_passive_owner_tests.py \
  --deps artifacts/public-review/t096-deps --out artifacts/passive-owner-01
python3 firmware/t096/tools/lora_passive_native_tests.py \
  --arm-reference artifacts/build-rc-01 --out artifacts/passive-arm-01
```

Host checks use production C, real HMAC, independent Python wire/hop/occupancy
oracles and fake peripheral boundaries. The ARM runner uses an inspected normal
build's compiler flags. It reports ABI sizes and individual stack frames, not
a linked native resource budget or whole call-chain margin. Both operate no
hardware. Numeric test outputs use CC BY4.0, Jonathan Yantis; code and this
documentation use MIT.
