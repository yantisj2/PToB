# RC8 LoRa timing and optional calibration

Copyright (c) 2026 Jonathan Yantis. MIT.

T096 uses **RF event wire5** for timing, context, name, companion and measurement.
This is breaking: current receivers accept wire5 only, with no negotiation or
compatibility broadcast. Other target ports are deferred.
[Machine contract](contract-wire5.json) · [Synthetic vectors](vectors-wire5.json) ·
[Historical wire4 contract](contract-wire4.json) · [Historical wire3](wire3-historical.md).

## Continuous timing and authority

The privately configured exact source emitter sends80-byte timing broadcasts
nominally every4s. Each carries the preceding packet's actual physical TX_START
projected through its original source map. Receivers pair it with that packet's
original RX_HEADER. Their passive affine fit supplies relative rate and link RMS;
RMS remains relative consistency, not an absolute error bound.

The emitter exports qualified own-GM time or its accepted locked BLE source.
BLE remains preferred. With LoRa as the only usable input, **both GM and RC
profiles may synchronize their local watch**. That grants no serving-GM,
election, HOT, BLE relay, RC7/RC9 or USB timing authority. Existing negative
foreign-root coordination constraints remain separate; metadata and probes
cannot renew them. UTC remains the timestamp coordinate.

The physical pair retains its original12s functional hold and separate20s
coarse display continuation. OFF, fault, withdrawal and source/context changes
withdraw old time. Preserve the existing source-map, replay, full immutable
identity/boot/epoch and symmetric group-trust rules described in the historical
contract. Cold passive freshness and total output uncertainty remain UNKNOWN.

## Optional source-opened RTT

One source opportunity can open at most once per60s. An80-byte authenticated
OFFER names the current context, latest timing sequence, fresh salt, offer token
and admission hash width. Only current LoRa-only receivers with a complete
companion, at least20s without usable BLE, and a current valid passive rate fit
may request. Other receivers remain passive. Hash admission spreads load;
requests start500..1000ms after the original physical OFFER RX_HEADER and must
finish inside its1.75s request window. The source admits its OFFER only when
that entire window plus250ms guard fits before the next timing deadline.

The first admissible request occupies the single global transaction. There is
no peer roster, waiting queue, mandatory ACK or promised response. Empty/CRC
contention windows adjust the admission width conservatively. Silence,
refusal, expired work and unmatched replies are normal outcomes, counted
separately from real radio/transport faults. Failed clients back off60..180s.

A newly online receiver has its first request due immediately after passive
stabilization and the existing20s BLE-loss qualification. It takes the first
admitted OFFER without an extra initial client cooldown. This does not force a
source opportunity or bypass silence/refusal; later failures and successful
refreshes retain their ordinary backoff and long intervals.

REQUEST, RESPONSE and FOLLOW are80 bytes, authenticated in domain `PTOBR8P5`.
Their common body is: header0..3 (`R8`,5,kind), context4..19, client factory20..27,
client boot28..35, nonce36..43, offer token44..47, t2 at48..55, t3 at56..63,
and16-byte tag64..79. Integers are little endian. REQUEST has zero t2/t3;
RESPONSE supplies original request RX_HEADER source-ns t2; FOLLOW repeats t2 and
supplies the response's actual TX_START source-ns t3. OFFER uses zero client/boot,
nonzero salt in nonce, current timing sequence in t2 and hash bits0..12 in t3.
Full context binds emitter/root/parent identities, boots, stream and source epoch.
Request RX_HEADER and response TX_START projections are preserved before source-map
turnover; later formulas cannot timestamp earlier events. Processing delay
consumes the original transaction lifetime. The console RTT tuple appends the
last refusal reason:1 malformed/auth,2 context,3 admission,4 window,5 missing
original RX map; it never turns these optional refusals into radio faults.

The client retains actual request TX_START t1 and response RX_HEADER t4 in the same
capture incarnation. Only a complete matched exchange computes
`effective_RTT = rate_corrected(t4 - t1) - (t3 - t2)`. A nonnegative result at
most250ms yields half that interval as an **estimated effective mean link delay**.
This includes modem/capture delays; asymmetry and independent accuracy remain
unqualified. Rate must be within100000ppb; an out-of-domain rate does not erase
otherwise valid passive residual statistics.

The TX reference is **BUSY falling at preamble start**; the RX reference is
**DIO1 rising at HeaderValid**, before payload completion. Both use a dedicated
GPIOTE event and PPI to the common16MHz capture clock. The PPI fork disables its
own allocated group at the first edge: a later BUSY transition cannot overwrite
TX_START while BLE delays software. TX capture arms after SetTx DMA retirement,
immediately before its actual NSS commit, with BUSY low. Exact TxDone and the
original generation/capture/map must still validate the timestamp.

RX keeps HeaderValid asserted until the same single-RX packet completes. The
worker polls completion without clearing the early IRQ or reusing the FIFO;
only HeaderValid|RxDone with no error exports its original capture. CRC/header
errors, timeout and missing HeaderValid discard the observation. An incomplete
header expires450ms after its physical edge. The early RX event cannot inhibit
the FEM: the separate DIO1/FEM-clear route is enabled only for TX, alongside the
unchanged independent hardware transmit cutoff.

[Semtech SX1261/2 Rev2.2](https://www.semtech.com/products/wireless-rf/lora-connect/sx1262)
§8.3.1 identifies BUSY falling with TX preamble start. Its IRQ table exposes
PreambleDetected and HeaderValid for LoRa; SyncWordValid is GFSK-only.
PreambleDetected depends on acquisition; HeaderValid supplies a repeatable
explicit-header marker, still delayed by modem detection/decoding. The
[Heltec schematic](https://resource.heltec.cn/download/Mesh_Node_T096/Schematic/Mesh_Node_T096_V0.2.pdf)
connects BUSY and DIO1 to the MCU; DIO2 controls the FEM and DIO3 the TCXO.
This board exposes no documented matched LoRa chirp-arrival timestamp or
calibrated antenna epoch. No symbol duration or observed millisecond offset is
subtracted to invent one.

If `b_CS` and `b_SC` denote propagation plus each direction's RX_HEADER minus
TX_START offset, the measured quantity remains `D = (b_CS + b_SC) / 2`. It now
includes the fixed header/detection interval; applying it leaves directional
residual `(b_CS - b_SC) / 2`, plus upstream reference error. The250ms effective
RTT ceiling bounds this longer explicit reference interval. It is not a distance
measurement or a claim of BLE-equivalent RF accuracy.

T096 shadow-PPS observes the frozen LoRa watch map independently. Its schema2
records identify `input=LORA`, `applied_lora_delay_ns`, signed corrected
`phase_ns` and `uncorrected_phase_ns`. Activation, original physical hold,
capture incarnation, source scope, missing records and unavailable references
remain explicit. PPS cannot feed the LoRa fit or RTT calculation. The LoRa
console `rtt_events` tuple is `[t1_tick,t2_ns,t3_ns,t4_tick,capture_epoch,
accepted_mono_us,offer,rate_ppb]`; it is zero without a live calibration context.
`rtt_reference=TX_BUSY_START_RX_HEADER` names the measurement endpoint. These diagnostics
support a bounded PPS comparison; they do not supply an absolute UTC bound.

The original24s rate-fit deadline also bounds each frozen shadow map. At rate
expiry, precision observation becomes invalid until a fresh valid fit arrives.
The coarse watch preserves continuity by retaining only phase accumulated up to
that deadline, then extrapolates at nominal rate; a new physical pair resets that
carry. `watch_coarse_phase_ns` exposes it. This cannot extend the original12s
physical hold,20s coarse display limit or any calibration/source permission.

The client performs this calculation only after FOLLOW. It applies rate and
any live delay estimate to its independent local LoRa watch map, never to BLE
or GNSS. Successful calibration lasts at most30min; the next request becomes
due after a randomized15..30min. Calibration expiry falls back to coarse LoRa.
Context/capture replacement, OFF and fault clear calibration. A transient
same-context source withdrawal immediately stops time but retains the original
finite link-delay cache. Recovery metadata cannot create time or renew that
cache; only fresh physical timing can use it again. Local owner/context
reacquisition permits another request after at least60s, subject to source offers.
Usable BLE immediately blocks further probes and stage2 updates.

T096's local `lora-rtt 01` diagnostic command temporarily selects 5s offer/retry
floor, 5–15s failed-client backoff and 15–30s successful refresh for five minutes.
It is off at boot, expires without renewal and can be ended with `lora-rtt 00`.
Use it on both endpoints for lab work, then return both to production timers.
The override changes no wire fields, eligibility, BLE priority, physical checks,
capture lifetime or transaction deadlines; it neither creates time nor renews
the original delay cache. Mode exit schedules future requests using production
limits. TZ remains on its ordinary low-priority schedule.

## Coarse timezone companion

The128-byte wire5 companion retains its scope, ordering floor and quality fields
through byte107. The retained coarse format uses bytes108..111 with the same atomic
`uint16_le tz_id, int16_le tz_offset_min` pair used by BLE. TIME_VALID moves to
bit7 of byte65; source kind occupies bits0..1 and bits2..6 are zero. Source
loss/recovery counters remain local diagnostics rather than OTA fields.

The companion tag uses `PTOBR8D5` over bytes0..111. The immutable context still
uses `PTOBR8C5` over the canonical revision5 scope header and unflagged source
kind; TZ changes cannot rotate a timing context or reset a fit. Timing and name layouts retain their field offsets, with wire5 headers and
domains `PTOBR8T5` and `PTOBR8N5`. Group authentication proves membership, not
unique hardware authorship.

The emitter forwards the selected serving source's current metadata. A BLE
follower never substitutes its own GNSS zone. Publication must match the exact
current root/boot/epoch; a context change clears the prior source's lease. Companion updates are coalesced,
nominally every120s and on changes, with the existing minimum2s diagnostic gap.
The receiver's original240s companion lease bounds TZ; timing/name/probe traffic
cannot renew it. Unknown updates clear it, expiry displays UTC, and invalid TZ
normalizes to unknown without invalidating otherwise authenticated UTC.
Both digital and analog watches use this selected LoRa metadata.

## Scheduling and resources

One priority11 worker owns half-duplex RX/TX and yields to BLE. Physical captures
and FEM cutoffs remain hardware-bound. BLE timing and the ordinary LoRa timing
packet precede optional responses, follow-ups and metadata. Responses need no
exact turnaround: they may wait across ordinary beacon intervals, within the
original10s transaction deadline. Every optional launch rechecks its original
finish deadline immediately before FEM enable. Missed work expires without
catch-up or deadline renewal. Actual SPI/DMA/cutoff faults retain their existing
latched shutdown and watchdog behavior.

There is one bounded transaction, no heap and no new thread/ticker reservation.
The original4096B RC8 and256B display budgets remain; the complete new RTT/TZ
addition is separately capped at512B against the inspected0.15.215 affected
objects, including copied UI models. Existing2048B worker stack, radio/USB
buffers, independent cutoffs and1024B absolute static margin remain required.
Native inspection and hardware stack high-water are separate checks.

A one-exchange/minute ceiling means100 clients need at least100 minutes for one
refresh each, before collisions, refusals and deferrals. Passive timing service
continues independently. These are capacity constraints, not delivery guarantees.

## Offline verification

Use the public pinned dependencies and commands in the
[T096 build guide](../../docs/t096/build.md):

```sh
CC=clang python3 firmware/t096/tools/lora_rc8_tests.py --deps artifacts/deps --out artifacts/check-rc8
CC=clang python3 firmware/t096/tools/lora_runtime_tests.py --deps artifacts/deps --out artifacts/check-rc8-runtime
CC=clang python3 firmware/t096/tools/lora_hw_tests.py --out artifacts/check-rc8-owner
python3 firmware/t096/tools/test.py --deps artifacts/deps --out artifacts/check-t096
```

Synthetic codec, physical-event and scheduler cases establish software behavior
only. Native builds, guarded installation, finite radio coexistence and independent
output/UTC metrology remain distinct evidence populations.

A drain keeps its original200ms deadline. A fully acknowledged safe closure
observed late is counted by `drain` diagnostics and returns `ETIMEDOUT` to that
app/flash drain attempt. The array is
`[original_deadline_us,promoted_us,first_poll_us,ack_us,late_count,current_late,closure_bits,close_result]`.
Internal optional recovery may start a new incarnation
from proven closed resources; it cannot turn the old drain into timely success.
Any missing DMA/reset/FEM/capture acknowledgment or hardware fault remains fatal.
