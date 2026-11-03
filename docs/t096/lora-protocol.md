# Passive LoRa timing: goals and wire3 design

Copyright (c) 2026 Jonathan Yantis. MIT.

This is the current T096 passive LoRa design and wire contract. It replaces the
wire2 persistence proposal and earlier wire3 design pages; Git preserves those
editions. [RC8](../../protocol/rc8/README.md) is a separate historical protocol,
not a compatible decoder. Implementation, installed configuration and measured
acceptance are separate. Use the [build guide](build.md), [soak
procedure](lora-soak.md) and [changelog](../../firmware/t096/CHANGELOG.md) for
those scopes.

## Goals and authority

LoRa extends an authenticated BLE timing deployment with a sparse, longer-range
receive path and a usable local watch when BLE is unavailable. Slow-profile
reception and relative link consistency precede field range evaluation. The soak
guide’s 10 µs RMS example is an operator criterion, not an established accuracy
specification. Neither a coverage distance nor absolute UTC accuracy has been
established. Narrow timing bandwidth trades acquisition and delivery latency for
prospective link margin; a link budget, antenna pattern and actual RF
measurements must precede any range claim.

One exact privately configured factory identity designates the physical LoRa
emitter. It is independent of BLE GM election: another device becoming ACTIVE
does not become a second LoRa transmitter. The designated GM-profile device may
export its qualified own-GNSS source or fresh authenticated locked BLE time from
another GM. Other GMs remain BLE sources and LoRa receivers. There is no
automatic backup-emitter election. These bindings identify hardware, never a
runtime mesh; local discovery and fresh authenticated admission select context.

GNSS/PPS and BLE timing take priority over every LoRa phase. BLE remains the
preferred watch input. LoRa observations have unknown independent source age and
total uncertainty; they grant no GM service/election, HOT, BLE relay, RC7/RC9 or
USB timing authority. Passive clients transmit no ACK, query or ranging packet.
There is no RTT/ToF correction in this protocol.

## Frame and schedule

A DTS control rendezvous announces a future hopped SYNC/FOLLOW pair. FOLLOW
carries the captured physical SYNC TX event mapped through its original source
clock; receivers associate it with the original HeaderValid capture. META
teaches authenticated context and coarse TZ. Its UTC value is frozen during
preparation for context/replay ordering, not an RF-start measurement. DISC
carries a scheduling hint within an already learned context.

| Setting | RAM_30_120_300, slow candidate | RAM_LAB_4_12, finite lab |
| --- | ---: | ---: |
| Precision period | 30 s | 4 s |
| DISC period | 120 s | Control slots without META |
| Nominal META period | 300 s | 12 s |
| SYNC from captured control start | 7 s | 2 s |
| Control/SYNC preparation lead | 2 s / 1.5 s | At launch |
| Control/SYNC call lateness / finish pad | 1 s / 1.25 s | 45 ms / 50 ms |
| FOLLOW after physical SYNC completion | Target 2.5 s, latest 4.5 s | 260–300 ms |
| Whole reservation | 15 s | 3 s |
| RX retune lead / tune late gate | 4 s / 1 s | 150 ms / 200 ms |
| Fresh hint HeaderValid radius / retention | ±1.5 s / 3 s | ±250 ms / 250 ms |
| Predicted HeaderValid radius / retention | ±3 s / 4 s | ±250 ms / 250 ms |
| Control prediction retirement | 4 s | 1.3 s |
| FOLLOW pending deadline | 7 s | 1 s |
| META/hint processing age | 1 s | 200 ms |
| SYNC processing age | 1 s | 500 ms |
| Physical/context expiry | 120 s / 420 s | 6 s / 18 s |
| Strict normal physical-event cadence bound | 32.51 s | Separate lab checks |
| Per-boot rolling-hour engineering cap | 108 s | 1080 s |
| Boot request | ON | OFF; explicit 15-minute session |

Both variants use normal GM/RC images; DC/PC reject them. The short 4 s
bootstrap guard is independent of cadence. A fresh boot chooses its first grid
opportunity 200 ms after a source second, at least 2 s ahead for slow or 300 ms
for fast; subsequent opportunities retain the selected period. There is no
hour-long migration or 60 s reboot silence policy. Actual source qualification
can delay acquisition.

The slow bounds are engineering allowances, not measured latency maxima. A
complete frame fits because 1.25+7+1.25+0.232+4.5+0.269 < 15 s. FOLLOW plus
processing fits the original pending window: 0.232+4.5+0.269+1 < 7 s from SYNC
HeaderValid. RX lead 4 s covers a 2.5 s relative control/SYNC start shift plus 1
s retune. Prediction-only hints tolerate one lost control while the narrower
fresh-control bounds remain independent. The next cursor cannot renew a saved
prediction's original control+4 s horizon. The 7 s SYNC offset leaves retune
margin after that retirement horizon.

The strict cadence gate is 30 s + two 1.25 s pads + 10 ms rounding = 32.51 s. A
120 s physical hold accommodates recovery but cannot turn a missed pair into a
passing cadence. META 420 s is an observation lease, not a delivery promise. All
packet-level source, RF cutoff, capture and watchdog bounds remain mandatory.

### P30/D120/M300 airtime and phase

Each30s slot contains one SYNC/FOLLOW pair. Full META remains112B every300s.
DISC is28B every120s, offset by one30s slot: META at0/300/600s, DISC at30/150/
270/390s. These phases do not collide. Other slots contain no DTS control;
a successful authenticated FOLLOW predicts the next pair. Receivers listen on
DTS before every possible control slot, preserving late-control/retune fences.
A lost pair can break that prediction chain until the next DISC or META;
recovery is not a passing normal-cadence observation. Source-turnover and bounded
recovery META remain exceptional charged work, never a catch-up burst.

Nominal hourly reservations are120 pairs x500765us,30 DISC x454416us and12 META
x1142544us: **87.434808s/hour (2.428745%)**. Every packet includes the original
10ms engineering allowance. The old60/300 schedule reserved65.568396s/hour.
The108s rolling-hour cap (3%) covers all ordinary and recovery reservations;
conservative minute-bucket admission can reach90.534063s even on the nominal
grid. This is accounting arithmetic, not measured RF occupancy or authorization.

The shared bounded HOLD rate consensus uses the existing16-point LoRa history,
with sparse180/300s pair-slope medians, at least60s pair separation,85% span,
at least8 valid slopes,20/10ppb internal guards and10ppb window agreement.
It preserves the BLE selector's bounded admission and150ppb adjustment ceiling,
with explicit conversion between local-frequency and LoRa correction signs.
No BLE/GNSS sample trains this LoRa model. Unlike dense BLE fits, the sparse
fits cannot center repeated channels within their short history; channel delay
and propagation remain uncalibrated.
Native inspection separately bounds the added sparse-fit/model scalars and their
published copy to128B above the inspected .272 baseline, retaining the existing
histories, stacks, buffers and absolute1024B static RAM floor.

Phase is the median historical pair projection at the selected causal rate,
not the latest raw packet timestamp. The owner commits at an actual current
capture after FOLLOW. A one-second phase slew preserves the previously delivered
clock; discrepancies above1ms reacquire explicitly. Physical/context expiry and
stale display age still originate at the original SYNC event. The original
180s training requirement stays: ideal30s spacing needs seven observations.

`lora-diag.hold_rates` is [baseline correction, selected correction, short local
frequency, short guard, long local frequency, long guard], all inppb.
`hold_model` is [actual commit tick, UTC ns, correction ppb, remaining phase ns].
The phase correction completes over one second from the commit tick.
`hold_selected` reports the guarded rate selection; phase fitting applies to
all trained maps. `discovery_us` explicitly reports120000000.

The engineering target is below1us RMS between updates. Fit residuals alone
cannot establish it. `lora_holdover_analyze.py` compares frozen causal models
with later physical pairs, retaining errors, unknowns and censored endpoints.
Its `invalid_event_snapshots` count retains reports with expired event permission
or a cleared capture scope after META; old event counters in those reports do
not create a physical observation in the newly authenticated stream.
Independent local PPS, when available, witnesses the actually delivered affine
map and slew without training it. Raw phase bias, uncentered RMS, centered
variation and packet-age populations must be reported separately. No fixed
PPS-derived subtraction or UTC qualification is implied.

## Source admission, scheduling and recovery

Own-GNSS LoRa projection receives three seconds from the original physical PPS,
independently of BLE's shorter control lease. It still requires a live original
UTC-bound map; repeated snapshots cannot renew it. A BLE follower retains its
original upstream capture interval and expiry and must be authenticated, locked
and current. LoRa-derived, ACQ, HOLD and expired maps cannot feed the emitter.
Each frame binds emitter boot/stream, actual root/root boot, selected BLE
parent/parent boot, source epoch, source kind and capture identity.

Slow encoding and FIFO preparation precede RF windows. No-RF staging needs a
currently valid source snapshot; actual launch refreshes original permission and
checks the whole packet budget after final preemption, before SetTx. A short BLE
grant may defer launch without inventing source authority. Stop, context
replacement and reconfiguration invalidate prepared work. Source turnover drains
the old transaction and requires a fresh META/stream; it cannot rewrite an
already captured SYNC with a later map.

An insufficient current grant leaves the prepared FIFO token and reservation
intact while waiting inside the original launch window. Only actual driver
entry consumes that token; the driver and final callback repeat source and
finish checks. This avoids restarting preparation after every packet-sized
grant refusal. Such capture-only waits can increase source rejection diagnostics
without being actual launch refusals, cancellations or additional reservations.

A completed physical SYNC retains its exact event/map through bounded projection
contention retries. Hardware DONE and airtime accounting occur once. First DONE
admission retains its 2 s age cap; later polls validate the saved event/audit,
generation, live capture epoch and monotonic continuity within the original
FOLLOW deadline. They cannot renew the capture or source map.

Slow precision and META obligations survive internal contention and proven
quiescent source turnover within the boot. DISC is not retried. Ordinary
admission backs off 200–302 ms and then reserves the full 2 s preparation lead;
ordinary cancellation waits 3 s. Completed recovery resumes the regular grid
without a catch-up queue. Sustained source absence, airtime exhaustion or
hardware failure can still prevent service.

A missed FOLLOW window or known healthy scheduling abort enters bounded drain.
Only an on-time actual radio/DMA/FEM/capture quiescence ACK permits recovery.
The owner retires both reserved hops, keeps full charge and cooldowns, preserves
owed precision and retries a fresh META/pair. It never repairs an old FOLLOW
late. Recovery control RF waits at least 11 s from closure for 7 s peer pending
plus 4 s retune. Pre-SYNC cancellation also waits the captured control's 7 s
SYNC offset, 1.25 s pad, 3 s hint retention, 4 s retune and 10 ms rounding
horizon. Ordinary retirement preserves unchanged MAC context; real source
turnover restarts fitting. Unknown hardware state, failed closure and terminal
faults inhibit hopping for that boot.

LoRa work runs at priority 11 and drain 10, below GNSS main 7, BLE precision 8
and GM owner 9; deadlines cannot promote it. Slow drain has one immutable 2 s
deadline; fast/classic drains retain 200 ms. Source projection runs outside
interrupt masks with immutable snapshots and checked commit versions.
Capture/control fences must remain brief; priorities and tests do not establish
interrupt latency.

## Wire format and authentication

Integers are little endian. Header bytes are `4c 03`, kind/flags, schedule. Kind
occupies the low nibble; bits 4–5 are zero. HAS_PAIR=`0x80` and
TIME_VALID=`0x40` are control-only. Schedule 1 selects fast, schedule 3 selects P30/D120/M300; an
image rejects other schedules. HAS_PAIR requires TIME_VALID and a nonzero 10 ms
delay hint; without a pair delay is zero. Slow accepts at most 700 delay units,
fast 400. Earlier schedule-2 slow receivers reject schedule 3; deploy matched current receivers. Wire 2 is rejected and no
compatibility broadcast or negotiation exists.

| Kind | Bytes including MAC16 | Body after header | MAC domain |
| --- | ---: | --- | --- |
| DISC=1 | 28 | cursor:u32, delay:u16, reserved:u16=0 | PTL3DIS1 |
| META=2 | 112 | Layout below | PTL3MET1 |
| SYNC=3 | 24 | cursor:u32 | PTL3SYN1 |
| FOLLOW=4 | 32 | original SYNC cursor:u32, SYNC TX UTC ns:u64 | PTL3FOL1 |

| META offset | Field |
| ---: | --- |
| 4, 12 | Emitter and physical emitter boot, u64 each |
| 20, 28 | Upstream root and root boot, u64 each |
| 36, 44 | Selected BLE parent and parent boot, u64 each; zero for own GM |
| 52, 56, 60 | Source epoch, emitter stream, public hop seed, u32 each |
| 64, 68, 72 | Hop cursor:u32, serial:u32, preparation UTC ns:u64 |
| 80, 82 | TZ ID:u16, effective offset minutes:i16 |
| 84, 86 | Delay:u16, source kind:u8 (1 own GM, 2 selected BLE) |
| 87..95 | Reserved zero |
| 96..111 | MAC16 |

Emitter/boot/root/root boot/epoch/stream/seed must be nonzero. Own GM requires
root=emitter and zero parent fields. Selected BLE requires root≠emitter and a
nonzero parent/parent boot with parent≠emitter. META serial and UTC are nonzero.
TZ must be valid or canonical unknown `(0, INT16_MIN)`; malformed reserved or
canonical fields fail closed.

The immutable context is HMAC/group domain `PTL3CTX1` over normalized header `4c
03 02 <schedule>`, META bytes 4..63, then source kind and three zero bytes (68 B
total). META authenticates its 96 B body; small packets authenticate the 16-byte
context followed by their complete body including header. The group envelope
uses HMAC-SHA256 truncated to 128 bits. Symmetric membership does not attest
individual hardware. Implementations:
[codec](../../firmware/t096/protocol/lora_passive.c),
[schedule](../../firmware/t096/protocol/lora_passive_schedule.h) and
[owner](../../firmware/t096/src/lora_passive_service.c).

DTS rendezvous is 915 MHz, SF12/BW500/CR4/6; timing uses SF7/BW41.67/CR4/6,
preamble 12, explicit header and CRC. Reserved durations, including an
unqualified 10 ms allowance, are DISC 454416 us, META 1142544 us, SYNC 231951 us
and FOLLOW 268814 us. The timing raster has 64 channels at 902.4 MHz+0.4 MHz*i.
SHA256 Fisher-Yates with rejection sampling uses `PTOB-HOP-MODEL\0 || seed:u32
|| counter:u32`; channel for cursor c is `permutation[(c%64+c/64)%64]`. The
public seed supplies hop order, not trust or mesh selection. HeaderValid is not
first RF arrival; propagation, capture bias and absolute output remain
uncalibrated.

## RAM, replay and display lifetime

Airtime buckets, cooldowns, hop cursor, serial, pending work, faults, diagnostic
records, profile selection, replay history, fits and display maps are RAM-only.
Physical boot starts at zero airtime with a fresh nonzero random public seed.
Service/profile/source changes within a boot retain accounting. Full
reservations are charged before transmission; only synchronous single-use proof
of no SetTx/RF can refund them. Unknown/possible RF keeps its charge. Historical
ledger/journal banks are neither restored nor written, erased or provisioned;
their bytes remain untouched. Static credentials remain private build inputs.
The 108 s rolling-hour cap has no cross-boot enforcement.

A new authenticated emitter boot discards prior pending/context/fit/display
state and requires fresh META and precision. Within a receiver boot, an
increasing per-emitter META UTC floor and eight-entry retired-boot ring reject
old/recent retired sessions. Reboot clears history. Ring eviction and unknown
independent source age bound this replay protection; observations remain
FRESHNESS_UNKNOWN. Rejected packets cannot clear, replace or renew accepted
state.

Fitting retains at most 16 points, spaced at least 20 s apart within 300 s, and
needs four points spanning 180 s. LOR RMS is overlapping relative fit residual,
not UTC accuracy or an uncertainty bound. A cold slow watch may show amber ACQ
from its first authenticated physical pair using nominal local rate. This frozen
map's reference cannot be moved or renewed by untrained packets. A trained fit
replaces it. The last trained map can continue as advancing amber HOLD while
local capture remains continuous; its reference age keeps increasing. Same
emitter factory/physical boot source turnover can retain that frozen display
while a fresh fit trains. OFF, profile/source selection, different emitter or
boot, authenticated TIME_INVALID, capture discontinuity and terminal faults
clear the map. ACQ/HOLD grant no timing, RF or export authority and cannot pass
a fresh-reception gate. Their total uncertainty remains unknown.

Authenticated selected TZ can be learned before UTC and cached for the boot.
Known geographic IDs use pinned calendars at displayed UTC. Valid changed
metadata replaces the cache; unknown metadata and timing loss do not erase it.
Live leases still expire. Calendar failure hides conversion while retaining
identity. Physical reboot clears it. Display continuity is separate from timing
and TZ authority.

## Range and FCC compliance considerations

Default SX1262 drive is 18 dBm, targeting nominal 28 dBm board output
([T096 PA gain curve](https://github.com/meshtastic/firmware/blob/develop/src/configuration.h)); actual power/EIRP remain unmeasured.
Future slow-profile range tests must record hardware/firmware, antenna
gain/orientation, placement, terrain, distance, interference, RSSI/SNR,
acquisition, accepted/missed populations, failures and relative/independent
timing separately. Bench reception establishes no coverage or emissions result.

The 108 s/h engineering limiter is not an FCC duty-cycle approval. The proposed
mixed rendezvous/hopping design needs an applicable equipment-authorization and
measurement route. For 902–928 MHz hopping channels with measured 20 dB
bandwidth below 250 kHz, §15.247(a)(1) requires at least 50 frequencies,
pseudorandom/equal-average use, appropriate spacing, synchronized receivers and
at most 0.4 s occupancy per channel in 20 s. The digitally modulated route
requires measured 6 dB bandwidth ≥500 kHz; configured bandwidth alone proves
neither route. Power, spectral density and out-of-band limits require their own
assessment. A regulatory hybrid claim needs a separate §15.247(f) assessment,
including occupancy with digital modulation disabled and spectral-density tests
with hopping disabled. [47 CFR
15.247](https://www.ecfr.gov/current/title-47/section-15.247).

Reboot resets accounting and the public hop session, so firmware alone cannot
establish aggregate cross-boot occupancy. A manual UTC job may explicitly exceed
the engineering cap; it grants no regulatory exemption. Neither cap, host model,
nominal PA setting nor manufacturer marketing establishes compliance or
authority for this changed waveform. Confirm the actual grant, allowed operating
modes, antenna and configuration. Antenna design/use and modifications are
governed separately by
[§15.203](https://www.ecfr.gov/current/title-47/section-15.203) and
[§15.204](https://www.ecfr.gov/current/title-47/section-15.204). Part 15
operation must avoid harmful interference and accept interference under
[§15.5](https://www.ecfr.gov/current/title-47/section-15.5). No FCC compliance,
approved power or range claim is made here.

## Diagnostic fields

RX activity and TX permission are distinct. `lora-diag.tx_gate` reports
independent inhibition bits: 1 not TX-capable, 2 RC profile, 4 service
disallowed, 8 request off, 16 reserved historical GM-admission gate, 32 source
absent, 64 TX core not ready, 128 current-boot FH inhibit, 256 insufficient
full-META/pair airtime headroom, 512 latest reservation refused, 1024 deferred
attempt not due. A cap or source gate is not a radio fault; `fault`, `error` and
the retained first-cause record remain separate.

Detailed `lora.tx_attempt` is ordered as validity bits, cursor, serial, prepare
us, reservation/commit us, postcommit lead us, first due-check lateness us,
fresh source room us, fresh source phase us, maximum prepare us, maximum save us
and source revision. Valid bits are 1 prepared, 2 committed, 4 first due check,
8 fresh source, 16 command attempted, 32 reservation refused. Source values
clear for each new attempt. The attempt uses the existing 512 B scratch; it
grants no new launch/source/FOLLOW budget. Fault word 10 bit 22 proves the
current GetIrqStatus response completed; bit 23 identifies rejected chip status.
IRQ flags stay in bits 0–15 and raw chip status in bits 24–31. Failed transport
clears these response-valid bits before submission, so an old response cannot be
attributed to the new operation. An `-EIO` alone is not a DMA diagnosis.

Detailed `lora.tx_source_max_us` is [maximum available original source room, UTC
phase at that maximum], sampled whenever the owner obtains a valid current
source, including between launch attempts. `lora.tx_reject` retains [reason,
point, monotonic us, original source-until tick, original finish us, available
source us, required us, remaining finish us, source-rejection count,
finish-rejection count, generation, no-RF evidence, UTC phase us]. Reasons are 1
SOURCE_ROOM and 2 FINISH_WINDOW. Points are 1 pre-reservation, 2 driver entry, 3
after SPI/before SetTx, 4 callback source fence and 5 callback finish fence. The
point-1 capture-only required room includes time to the planned start when
pre-reserving, or the complete current packet when waiting with staged work.
Later cap
refusals do not erase this record. `no_rf_refunds` counts exact single-use RAM
reservation refunds after synchronous healthy pre-SetTx rejection. Callback
fences, ordinary cancellation, possible RF and terminal faults retain charge. No
field extends source/capture/finish/FEM limits or proves UTC accuracy.

`rf_window_us` orders call lateness, finish pad, FOLLOW target/latest,
HeaderValid radius, hint retention, pending deadline and processing age.
`prediction_window_us` reports predicted radius/retirement; `schedule_window_us`
reports SYNC offset, whole frame, RX lead and control retirement. `tx_due_us`
contains last then maximum call lateness for DISC, META, SYNC and FOLLOW,
including rejected calls. `sync_map` counts mapped completions, deferred
projection polls, failed original-map/fence checks and FOLLOW deadline expiry.
`scheduler_priority` reports actual/normal/drain; `source_read` counts ready,
transient, absent and version races. These distinguish deferral from loss; none
authorizes RF or proves source accuracy.

The non-timing fast diagnostic is separately built and finite; its current
profile is RAM_DIAG_4_12. See the [build
scope](build.md#finite-non-timing-tx-diagnostic). It exports no timing map.
Historical flash `store_timing`/carry-forward traces are not operational state
in the current RAM-only service.

## Verification and measured limits

Use the [build checks](build.md#checks), [autonomous soak](lora-soak.md) and
[bounded UTC diagnostic](lora-utc-diagnostic.md). Offline production-body codec,
owner, RAM, scheduling and display tests exercise software behavior without RF.
Native inspection separately checks resources and image identity. Diagnostic
requests are limited to one job, at most 31 s admission deadline; their optional
lab cap override, packets and manual aging never count as ordinary
qualification.

A reception gate requires 600 continuous real seconds, three advancing ordinary
META boundaries, trained timing, strict cadence/error checks and no host gaps. A
completed soak additionally requires its full requested duration and no
post-gate violations. Stopping early is STOPPED. Dated results, including failed
and incomplete runs, belong in the
[changelog](../../firmware/t096/CHANGELOG.md). Independent UTC/output accuracy,
RF emissions, range and extended stability remain separate measured gates.
