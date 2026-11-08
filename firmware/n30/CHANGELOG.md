# N30 changelog

## 0.2.64 — 2026-11-08

Use a current authenticated direct-GM follow-up to correct an already captured
original when the follow-up has no usable RF stamp. Require the exact live PA
owner, capture epochs, preceding publication, event and channel. The follow-up
cannot supply a physical sample of its own. When both corrections in a report
match pending captures, skip an older pair that has exhausted its signed
permission and consider the next pair in the same report. Keep the full
causal guard and the earlier of the original and correction deadlines.

Native-owner regressions use authenticated GR3 packets. They cover missing
follow-up stamps, overlapping grant expiry, stale owners, missing originals,
replay and bad authentication. The remaining field gaps need an installed
same-GM comparison to establish their cause and frequency.

## 0.2.63 — 2026-11-06

Retain the reviewed LoRa receive implementation. Raw packet-status/span probes
from the withdrawn .55/.56 prototypes are excluded. Correct the current build
receipt to declare standard counters/first-fault diagnostics instead of those
unavailable probe fields. Historical receipts and failed observations remain
unchanged.

Pause same-scope qualification across permission-only gaps up to the existing
1.5s rebootstrap bound. The independent observer still expires after250ms;
missing time earns no credit and current output permission never extends.
The expanded388-schedule model reproduces20 failures against the earlier
250ms-pause implementation, including recurring300–374ms gaps.

Preserve a valid direct-GR3 physical pair when its later authenticated correction
carries a newer immutable source envelope. Previously a later envelope expiry
caused a blanket refusal, even when both signed packets and the old RF original
were still valid. Intersect both source deadlines and keep the full age, replay,
coarse guard and scope checks. A new header alone cannot renew an old RF model.

The mandatory coupled gate now authenticates real GR3 packets and checks100
phase/delay/expiry schedules for each target profile, shortened/expired grants,
bad MACs and source/boot/train/capture changes. These are offline regression
results; individual field-gap attribution and endurance remain separate.

## 0.2.62 — 2026-11-06

Catch recurring permission-gap starvation offline with source/UI sequences and
a coupled test of the actual native publisher, expiry reader, eligibility, UI
adapter and selector. The30s return dwell pauses across permission-only gaps
below250ms; denied time earns no credit and BLE output still stops immediately
at expiry. Same-scope current quality must return; longer gaps and hard fences
restart qualification.

Keep a directly authenticated, physically arriving GM train through a source
permission drought up to the existing15s physical-stall supervisor. Fresh
admission is still requested after1.5s, all original grants stay finite, and
replay expiry or absence of fresh associated PA still retires acquisition.
Retained parent reasons distinguish expiry, replay, packet scope and successor
withdrawal; CRC schema2 exposes bounded qualification pauses.

Commissioning requires a passing source-bound coupled receiver gate, including
W12 production holdover/recovery. Synthetic results remain separate from
hardware acceptance; prior installation failures are retained.

## 0.2.61 — 2026-11-06

Observe the BLE return dwell at main-owner cadence, independently of display
publication. Source/UI event schedules reproduced indefinite LoRa selection when
publication gaps repeatedly reset the timer despite qualified source input.
Fresh current permissions and settled display quality remain mandatory.

Preserve acquisition on a physically arriving authenticated direct GM through
the existing bounded1.5s rebootstrap permission gap. Output withdraws immediately;
this scheduling reservation grants no time and cannot keep an expired parent
forever. Native-owner regression covers the gap and exact grace expiry.

Retain source-reset reasons, the first interruption while waiting to return to
BLE, and source/parent journals behind nonconsuming nonce/CRC reads. History
wrap and missing parent events remain explicit. The host suite includes coupled
source/publication/permission/mode/scope sequences and actual diagnostic payload
validation. These regressions address gaps exposed after .60's finite checks;
long-duration hardware reliability remains unqualified.

## 0.2.60 — 2026-11-06

Allow authenticated direct JOIN to establish coarse replay context with less
than200ms uncertainty, while retaining the250ms exchange TTL and full causal
RTT/source-bound/drift subtraction from source permission. Valid delayed
exchanges could exceed the previous100ms coarse budget and be rejected for
repeated startup/recovery attempts. The actual uncertainty remains visible to
all replay checks and shortens the coarse anchor's lifetime. Precision lock
still requires fresh physical corrections; no fitting or accuracy gate changes.
The legacy shared API and W12 admission retain their100ms ceiling. Regressions
cover delayed authenticated startup, source-grant exhaustion, expired exchanges,
bad authentication, full uncertainty and original source expiry.

## 0.2.59 — 2026-11-06

Share the existing five-second unusable-admission cooldown with the parent
chooser. Previously a stronger peer could win every selection during its
transport cooldown, leaving a weaker fresh candidate unattempted. This kept
startup/recovery stalled despite discovery and authenticated exchanges.
The production regression retains the stronger unusable peer, authenticates
its NO_SOURCE reply, then verifies selection of the weaker fresh alternative
after the ordinary failed-exchange backoff. Authentication, source permission,
old-clock expiry and W12 behavior are unchanged.

## 0.2.58 — 2026-11-06

Begin the N30 GM fitting trial after a usable authenticated binding, rather
than the first connection attempt. A failed/no-time initial exchange could
reserve its candidate for60s and suppress fresh alternatives even while that
candidate was cooling down. This affected first boot and reacquisition.
Preserve the ordinary fitting trial after successful binding and all source,
parent, profile and authentication requirements. W12 selection is unchanged.
The production-owner regression authenticates a first NO_SOURCE exchange,
then verifies immediate fresh-alternative admission with no inherited time.

## 0.2.57 — 2026-11-06

Supervise BLE acquisition separately from the independent LoRa clock. Recent
eligible parent observations with15s of no physically matched corrected-pair
progress request the existing BLE-only bounded rebind and fresh admission.
Retire advisory retry history after actual owner closure; retain operator pins,
admission rate gates, terminal authenticated fences, original drain deadlines
and fault evidence. Startup uses the same supervision. Actual faults retain
their existing watchdog; continuing physical progress never resets on RMS alone.
Bounded capture records expose `ACQUISITION_REBIND` and `ble_rebind` counts.

An expired or unqualified LoRa human HOLD no longer imposes the30s return dwell
on already authorized settled BLE. Fresh qualified LoRa retains that dwell.
Host regressions cover stalled first acquisition, a live PA without map progress,
fresh-pair progress, OFF/fault/absence, pins, relay-only policy and failed drains.
These are software recovery changes; field endurance remains a separate check.

## 0.2.56 — 2026-11-04 (withdrawn local prototype)

Move raw receive probes into a separate bounded diagnostic publication. The
ordinary timing/UI view regains its original layout and status call-frame
footprint. Keep the original runtime stack requirement: .55's actual readback
passed, but its startup watermark failed that gate. The receiver model, capture
guards, receive policy and diagnostic fields are unchanged. Precision remains
unqualified; no failed interval observations are removed.

## 0.2.55 — 2026-11-04 (withdrawn local prototype)

Retain raw SX1262 packet-status bytes and separate HeaderValid/RxDone capture
spans for24,32 and112-byte packets. Status, packet RSSI, signed SNR and signal
RSSI remain distinct. A combined interrupt or dropped edge cannot invent a
second timestamp. These packet-length records require an authenticated original
capture join before timing comparison; they grant no authority and never steer
the estimator. The independent counter, frozen .53 model, RF profile, receive
gain and clock UI remain unchanged. Precision remains unqualified.

## 0.2.54 — 2026-11-04

Preserve the original descriptor timestamp in the bounded timing diagnostic and
hand off the accepted production fine timestamp separately, using exact
capture epoch, sequence, event and channel. Corrected-pair diagnostics now join
the coordinate actually consumed by the clock. No capture acceptance, clock
model, source permission or LoRa parameter changes. Both coordinates remain
visible; descriptor-bin uncertainty is not applied to a different fine endpoint.
The16-row diagnostic stays within its original4KiB limit.

The .53 prospective run retained44 physical pairs and six actual60s trials with
no counter/radio fault or reset. Ordinary next-pair RMS was613ns with three
excursions above1us (maximum1300ns); actual60s RMS was973ns with one2177ns
excursion. Those interval targets remain open. This diagnostic revision is not
a precision qualification or a changed LoRa estimator.

## 0.2.53 — 2026-11-04

Restore the Clock page's segmented digital layout and confine acquisition copy
to the analog area. Expose estimated digital UTC from the first authenticated
pair and actual causal prediction error from pair two. Retain authenticated
boot META/TZ descriptions and a separate unqualified same-context observer
through expired live META, missed pairs and long RF absence. Keep live grants,
precision lock, PPS, admission and qualified reacquisition gates separate.
Long human HOLD freezes rate at300s; its source/error ages are never renewed.
Healthy initial LoRa acquisition and ordinary RF absence no longer reboot away
learned boot information; the armed no-time interval is12minutes while actual
fault watchdogs retain their original120s deadline.

Freeze a LoRa-only two-mode30s/120s development estimator against the complete
native .52 population. Retrospective RMS improved, but interval excursions above
1us remain. Prospective hardware accuracy and convergence are unqualified.
Retain explicit causal prediction mode/age, complete errors and immutable finite
HOLD diagnostics. Separate historical operating faults from physical cleanup
readiness during maintenance, with all original owner/drain/expiry checks.

## 0.2.52 — 2026-11-04

Pair each native LoRa CURRENT count with its coarse health witness inside the
existing bounded capture lock. Keep the sample time through clock publication;
actual availability still checks source freshness and original expiry. A delayed
caller cannot substitute a later timestamp for the hardware sample. Stop, rate,
epoch, wrap, capture, training and original source/HOLD limits remain unchanged.
The CRC diagnostics retain the first helper return or counter failure reason,
raw/coarse pair, preceding pair, epoch and actual availability until reboot.

The first .51 hardware trial received META and three authenticated pairs before
LoRa faulted with generic `-EIO`; no prediction or HOLD population was acquired.
The exact cause of that historical failure is unresolved. Delayed-caller host
reproduction identifies a software race, not proof of its hardware attribution.
Prospective fault, convergence and sub-microsecond testing remain required.

## 0.2.51 — 2026-11-04

Prepare an independent native MCPWM group0 clock for LoRa HeaderValid timing.
The same physical event and engineering endpoint-delay input remain. A pinned,
bounded CURRENT-only channel extends the80MHz hardware counter through wraps;
the existing normalized16MHz estimator coordinate has62.5ns quantization.
The original GPTimer mapping still gates physical admission, including preceding
anchors,20us brackets, two-second expiry and capture-generation checks, but its
phase/frequency no longer steers the delivered LoRa model. BLE remains separate.
Counter stop, rate, wrap ambiguity and lifecycle failures withdraw availability;
the new channel participates in the original owner drain and PM clock lifetime.

Precision labels identify the80MHz/12.5ns physical capture counter. This is not
a delivered-resolution, RF-calibration or UTC-accuracy claim. Estimator training,
one-second phase slew, source grants and finite60s HOLD deadlines are unchanged.
The existing string/character/integer console formats use the pinned Newlib
integer-only scanner, as on W12, to retain the original image ceiling.
Offline checks and prospective exact-device testing remain required; no
sub-microsecond qualification is inferred from the counter change.

## 0.2.50 — 2026-11-04

A guarded `lora hold60` diagnostic requests60s without steering the delivered
LoRa model. RX continues solely to preserve physical validation pairs. The
worker admits only a trained current model whose original physical/META grant
outlasts the experiment and whose reference is at most10s old. The immutable
T0 model and grant remain fixed; new RF pairs cannot extend that held output.
Context, source, capture, OFF and expiry fences withdraw it normally. Results
include request/run state, explicit abort classification, original deadline and
separate actual60s prediction counts/tails. Normal reception never arms a hold.

A bounded0–300s decay kernel preserves the fixed120s estimator model while
avoiding a general-domain exponential dependency in the original1MiB slot.
Analytical remainder and independent libm comparisons bound its numerical
error; this is a computation check, not physical precision qualification.

## 0.2.49 — 2026-11-04

An N30-only phase/rate/damped-acceleration model follows XTAL warmup from
authenticated physical LoRa pairs. The shared raw linear fit remains unchanged
and visible. Original180s training, current META, capture incarnation and120s
physical expiry are additional eligibility fences; gaps and context changes
clear the new model. Delivered phase keeps the existing bounded one-second
slew, with gross discontinuities reacquiring. No BLE/PPS training, fixed bias
adjustment, persistent model or new timing authority is introduced.

Diagnostics distinguish the actual delivered model's next-pair prediction
residual, its last-eight RMS and complete current-context counts/tails from raw
linear fit RMS. A separate60s statistic projects an older delivered model frozen
analytically; intermediate reception continues, so this is not an actual HOLD
or independently measured output/UTC result. Clock and Precision label the
delivered statistic PRED RMS and retain the raw fit in the Precision detail.
Physical precision qualification remains open pending prospective trials.

## 0.2.48 — 2026-11-04

An N30 linker fragment places the unchanged pinned SDK MCPWM software-capture
command in RAM beside the mapping helper. Native build and inspection verify
both ELF sections. The physical0.2.47 trial preserved valid mappings but still
observed7.65–14.4us software brackets while that callee remained in FLASH.
No SDK source, GPIO event, clock coordinate, timing acceptance threshold or
source permission changes; task-context placement is not ISR/cache-safe
qualification of SDK error paths. Physical performance remains a separate check.

## 0.2.47 — 2026-11-04

Place the single-lock LoRa mapping helper in IRAM, with an ELF-section check in
both native build and inspection. The previous FLASH implementation produced
21–22.5us brackets in the physical trial, so the unchanged20us acceptance guard
correctly refused many mappings. Preserve that failed trial separately; software
checks alone did not establish performance. No acceptance threshold, authority,
calibration prior or source expiry is relaxed.

## 0.2.46 — 2026-11-04

LoRa mapping brackets use direct latches of the original20MHz XTAL GPTimer
under its existing driver lock around the same public MCPWM software catch.
The pinned private ABI, running-state/clock-source/resolution checks, original
400-tick acceptance limit and causal map/expiry fences remain. This removes
repeated driver/HAL call overhead from those software brackets; it does not
change the captured radio event, engineering symbol-delay prior or BLE backend.
LoRa diagnostics expose the actual software bracket width separately from the
conservative total capture enclosure. Host counter/peripheral checks remain
separate from physical precision and RF/UTC qualification.

## 0.2.45 — 2026-11-04

Independent LoRa receive intent survives a BLE-owner fault. Explicit all-OFF,
maintenance and power-off still drain it; actual LoRa faults and the original
120-second fault watchdog retain their guards. The radio page identifies BLE
faults separately, shows LoRa training progress, and labels continuous AUTO RX
without a countdown. A paused RC7 capability update may retry controller memory
capacity error0x207 twice at100ms spacing; only an actual successful command
publishes the capability. Persistent or other errors retain the BLE fault path.
This N30-only retry does not change W12/SS3 behavior or source authority.
Eight ordinary observer-line slots replace sixteen, reclaiming12,416 bytes for
controller allocations. Priority CRC frame buffers, record sizes, deadlines
and visible overflow/expiry accounting remain unchanged.

## 0.2.44 — 2026-11-04

Clock and saver displays learn signed TZ metadata from authenticated BLE or
LoRa META and retain the latest known offset in boot-session RAM. Source loss,
lease expiry, BLE parking and input selection do not clear this display
preference. Reboot starts unknown; new authenticated metadata can replace it.
Timing leases, authentication, clock models and UTC diagnostics remain separate.
LoRa dials show the30s pair cadence. The receiver page distinguishes waiting
for META, waiting for a pair and training, with the actual fit span toward180s.
Pair ages are shown only for the current known context. A screensaver wake press is consumed from
its raw edge, so debounce cannot turn its release into page navigation.

The independent LoRa APB80/XTAL20 capture mapping intersects causal preceding
software brackets rather than using one midpoint. Original availability,
two-second expiry, modulo-age and hard continuity guards remain. Host tests
exercise asymmetric brackets and counter wraps; physical sub-microsecond
clock discipline and RF/UTC accuracy still require separate measurement.

## 0.2.43 — 2026-11-04

Enable SX1262 boosted receive gain (`RxGain` register `0x08ac = 0x96`) after
each radio reset, and require matching readback before starting reception.
Previously the driver left the chip at its power-saving gain default. A failed
write/readback retains the existing reset, fault and owner-drain behavior.
The radio remains receive-only on the existing schedule8 PHY. Host peripheral
and authenticated interoperability checks cover boost, reset/re-enable,
TCXO/RF-switch commands and refusal on mismatched readback. Physical sensitivity,
antenna performance and indoor range still require measurement.

## 0.2.42 — 2026-11-03

Precision follows the selected clock's own diagnostics. LoRa now shows its
emitter/root, independent XTAL capture coordinate and mapping enclosure,
internal fit RMS/population, committed rate/phase correction, physical pair age,
committed clock-reference age and training/availability state. META age remains
separate. Old RMS values are hidden when the LoRa model is untrained, stale or
unavailable; no BLE lock or quality is substituted. UTC/RF bounds remain UNKNOWN.
The renderer uses size optimization within the existing1MiB slot. Timing
algorithms, source selection, radio scheduling and calibration are unchanged.

## 0.2.41 — 2026-11-03

Precision shows the actual selected clock source separately from its independent
BLE path/root, RMS and lock duration. Page navigation remains presentation only;
clock selection, the30s recovery dwell, timing models and radio policy are
unchanged.

A too-wide software mapping bracket may now retain the last qualified capture
map only when monotonicity, original counter-rate/gap guards and original
freshness agree. The rejected anchor is never installed; the old map still
expires at its original two-second limit. Hard/ambiguous failures clear it.
Diagnostics distinguish soft bracket refusals from hard map resets. This is an
offline-tested recovery of valid captures, not a new precision qualification.

## 0.2.40 — 2026-11-03

Keep the Precision page root label tied to its independent BLE model when
LoRa supplies the displayed clock. The selected clock header continues to name
the LoRa emitter. Timing models, wire format, calibration and permissions are
unchanged.

## 0.2.39 — 2026-11-03

- Receive the T096 passive wire3/schedule8 fixed922.4MHz SF11/BW500/CR4/8
  profile, with fresh authenticated META context, real SYNC/FOLLOW pairs and
  preserved replay/boot/stream/counter/expiry fences; reject older namespaces.
- Capture hardware HeaderValid separately from RxDone. The independent20MHz
  XTAL map explicitly supplies the shared16MHz coordinate. Use a declared
  unverified99.328ms engineering symbol model; board/RF bias and UTC bounds
  remain unknown. No TC02 calibration is transferred.
- Keep passive AUTO/KEEP ON RX and its independent fit warm during BLE operation.
  After LoRa selection, require30continuous seconds of fresh qualified BLE in
  unchanged scope before replacing the actual displayed clock. Manual OFF,
  independent BLE fitting and receiver-only/no-export authority are preserved.
- Offline host/native/portrait preparation and separately scoped hardware
  observations must be recorded independently; this revision has no automatic
  precision or hardware qualification.

## 0.2.38 — single-button watch menu

- Open the menu from any page at exactly 2000 ms; every earlier release is short,
  including release debounce crossing the threshold. Taps cycle enabled rows;
  a separate 2000 ms hold selects once. Consume the opening hold and return to
  the invoking page on Exit or 5000 ms idle, with no timeout while held.
- Preserve RC-only operation and upstream AUTO/relay-only selection. Add bounded
  Power Off, BLE RX OFF/ON, enrolled LoRa RX OFF/ON and Exit. No GM or LoRa TX
  mode is introduced. Requests stay in the menu with owner acceptance/refusal
  feedback and separate pending/activity/fault states.
- BLE-only OFF parks the BLE owner and retains the fine-clock owner. AUTO
  acquires LoRa immediately without replacing explicit KEEP ON/KEEP OFF intent.
  BLE/RC9 clocks and cached BLE pixels are ineligible while parked; the no-time
  restart timer is disabled. Actual fault/maintenance fences remain. ON requires
  fresh admission and ordinary 180 s AUTO recovery. LoRa overrides stay volatile.
  Timing models, source expiry, authenticated admission and original shutdown
  drain/ownership/wake guards are unchanged.
- Validate raw-button boundaries, all pages, row skipping/wrap, boot/wake,
  cancellation, idle/held behavior, actual owner dispatch and both renderer
  rotations offline. Native previews are synthetic; hardware acceptance and
  independent precision qualification remain separate.

## Build validation — shared stack checker

Keep N30's release sequence independent of W12 when selecting stack-evidence
contracts. N30 0.2.37 retains the common compiler-frame checks and 4 KiB reserve;
its version no longer selects W12-only GNSS, fine-owner or LR2021 evidence.
This tooling correction does not change runtime timing or hardware profiles.

## 0.2.37 — real shutdown instead of OLED blanking

- A four-second button hold on any non-clock page requests deep sleep after
  the original two-second radio/capture drain, OLED worker acknowledgement and
  40 ms stable release. A new button press wakes by rebooting the application.
- Retain Clock's one-shot 1.5-second relay toggle and LoRa's 1.5-second cycle;
  a continued LoRa hold now shuts down at four seconds. Consume wake/release,
  cancel page changes and prevent release debounce from crossing a threshold.
- Retain reviewed OLED VEXT/reset/I2C and SX1262 reset off levels through sleep.
  The display worker stops rendering before those pads change ownership.
- Host checks cover actual input, main action dispatch, fine-owner drain and
  display-stop timeout, plus power API error paths. These are offline checks;
  sleep current, battery lifetime and physical wake remain unmeasured.

## 0.2.36 — ordered dynamic TZ updates

- Keep the independent RC9 control service at212 bytes in both client and
  server callbacks while TZ-enabled RC7 uses216 bytes; test both live owners.

- Follow the T096 0.15.206 amendment on the unchanged coarse wire. Accept
  changed pairs and explicit unknown from the fresh authenticated current
  parent; fence metadata on parent/path replacement.
- Order direct ANN by GM sequence and relay capability by its existing
  admission challenge. Refuse older or same-token conflicting snapshots and
  prevent a delayed JOIN from replacing a later ANN.
- Consume current-parent capability metadata during protected following.
  Increment the publisher's existing challenge when its advertised pair changes.
- Preserve the three-period lease, UTC timing, physical fitting, controller
  drains and calibration. Add actual decoder/owner update and withdrawal tests.

## 0.2.35 — coarse TZ and local clock display

- Consume GM7 v4, direct JOIN v5 and RC capability/control v8 using the selected
  T096 TZ codecs. Historical codecs remain explicit offline entry points.
- Learn the authenticated selected parent's fixed mesh pair; renew its 750 ms
  direct / 300 ms relay presence lease independently of precision fitting.
- Forward the current pair in RC coarse advertisements/admission. Keep timing
  payloads, clock fitting, source permission and per-device calibration unchanged.
- Show local digital/analog time and the current zone label; expire to UTC.
  Preserve UTC on LoRa/RC9 and diagnostics. N30's pixel cache respects TZ expiry.
- Add production-codec/vector, native owner and actual-renderer regressions.
  Offline checks do not establish hardware or UTC/output qualification.

## 0.2.34 — continuous clock through direct GM handoff

- Retain the running local clock during the successor's coarse JOIN. Resume
  phase steering with its learned rate only after a newly authenticated,
  matched physical correction; translate between different GM counter origins.
- Bound actuator reuse to30s after the original physical observation. Reset
  fit points, causal quality and source permissions, and retain gross-step,
  expired-carry, counter and invalid-capture fences. Cold start remains ordinary
  acquisition. W12/SS3 keep their existing behavior.
- Add actual-clock signed phase/rate/continuity and actual-owner JOIN/HOLD
  regressions. Software checks do not establish physical failover precision.

## 0.2.33 — announced GM successor recovery

- Consume authenticated GM7 revision-3 announcements bound to the selected root,
  boot, timing epoch and train. Retain the nominated successor's exact boot.
- Honor terminal withdrawal and the original timed-yield boundary; recognize
  the named successor's next-term announcement. Existing sustained source loss
  also carries the nomination into a bounded three-second recovery preference.
- Retire old source/model/queued samples through the existing owner drain.
  Require a fresh locator, G1 authentication and new physical timing. Preserve
  operator pins, relay-only policy, calibration and admission rate bounds.
- Add short passive control-listening windows between associated PA receptions.
  Offline regressions do not qualify physical handoff latency or precision.

## 0.2.32 — exact PA terminate acknowledgement

- Retire only the exact active PA owner after the pinned SDK returns successful
  HCI termination or confirms the handle is absent. Its queued EDONE remains
  a notification; a stale callback cannot close a replacement capture epoch.
- Preserve actual pending-create cancellation, original drain and BLE50 quiet
  deadlines. SDK errors and late returns remain faults.
- Distinguish terminate SDK error, late return and unclosed owner expiry in the
  existing compact fault-site byte; retain bounded call-start/return traces.
- Native regressions cover delayed/inline notifications, reused handles, SDK
  pool pressure, actual RC9-to-direct recovery and subsequent PA admission.
  No hardware acceptance is implied.

## 0.2.31 — replacement RC7 lineage cache fence

- Consume the shared W12 authenticated RC7 path fence: new root, boot, source
  epoch or publisher path clears obsolete advisory trials and retry scheduling.
- Preserve the current authenticated response/ACL, exact PA drain deadline,
  operator pins and per-device calibration. Unchanged-path renewal remains
  separate. Native regression outcomes establish software behavior only.

## 0.2.30 — fallback transition cache fencing

- Clear old normal-source selection/trial history on authenticated RC9 takeover.
- Fence the prior advisory caches even if a newly authenticated direct root
  arrives after the earlier scope was already withdrawn.
- Reset stale fallback retry scheduling; preserve explicit pins, source/lease
  expiry and real controller drain deadlines. Actual native regressions cover
  stale-history takeover and direct admission from an empty scope.

## 0.2.29 — leader recovery

- Fence expired or changed source state and discard old leader selection caches,
  pending fine associations and queued packets before fresh admission.
- Preserve explicit pins, enrollment, continuous counter and per-device offsets.
- Bound recovery admission credits to one per five seconds; keep authenticated
  deadlines and actual controller drain ownership unchanged.
- Handle exact duplicate incoming ACL notifications idempotently and quarantine
  valid capacity overlaps without granting service or faulting normal reception.
- Offline regressions cover loss boundaries, new-leader admission, pins, repeated
  reset throttling and duplicate/overlapping connections. These do not qualify RF
  failover latency, UTC accuracy or precision.

## 0.2.28 — normal BLE recovery from RC9 fallback

Consume the shared W12 AUTO fallback recovery fix. Bounded passive discovery
and fresh normal admission replace the former six-minute selection lockout.
Failed admissions and root hints retain fallback under its original expiry.
Explicit pins, separate clock models, capture calibration, startup and LoRa
policies remain unchanged. Earlier releases are recorded in Git and the target
history below. Original documentation: MIT; copyright 2026 Jonathan Yantis.

## 2026-10-22 — selected fine-receiver observations

Historical results moved from the target README. The
[research draft](../../papers/ptob-esp32s3-timing-preprint.pdf) and
[selected evidence](../../evidence/s3/2026-10-22-fine/README.md) retain exact
edition, source and measurement identities. These observations do not qualify
later images or every operating window.

| Measurement | Result | Population and limits |
| --- | --- | --- |
| N3-2 internal causal SYNC RMS | **49 ns observed** | Rolling prediction quality; three 49 ns readings among 311 finite readings in 313 status snapshots. |
| N3-2 downstream relative shadow PPS | **166.590 ns RMS** | Full 660 s window: 657 eligible + 3 invalid, zero nominal missing; mean −106.811 ns, maximum absolute error 1591 ns. All tails retained. |
| N3-1 same-correction transfer | **150.954 ns RMS** | Separate short check: 48 eligible + 16 invalid, no sequence gaps; mean +115.167 ns, maximum 362 ns; one terminal unread edge. |

The full N3-2 run used a fixed 50909 ns relay correction. N3-1 used a revised
51016 ns total correction derived from that run, with its own publisher and
direct-root scope. The fresh N3-1 check retained a positive mean: the constant
did not transfer zero mean across boards. No refit was made during that check.
Display RMS, counter resolution and calibrated RF/UTC accuracy are distinct;
the sub-100 ns downstream target remained open. The precision preview uses the
recorded internal 49 ns value with otherwise illustrative inputs.
