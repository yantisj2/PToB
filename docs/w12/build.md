# Build W12 RC

Copyright (c) 2026 Jonathan Yantis. MIT.

Native builds [clean compiler output and retain the latest two image bundles](../firmware-storage.md)
per device/profile, with installed/recovery pins preserved.

Version 0.2.21 adds GPIO0 button shutdown on every page except Clock. A hold
of four seconds blanks the OLED and starts the same original two-second drain
used for maintenance. Deep sleep requires all owners to acknowledge OFF and
GPIO0 to remain released for 40 ms. Drain timeout/fault or power API failure
remains a fault; neither extends the deadline nor claims completed shutdown.
The next GPIO0 press wakes through ESP-IDF EXT0 and reboots the application.
A button already held at boot cannot toggle a mode or request another shutdown.

The Clock page keeps its single 1.5-second relay-mode toggle even during an
extended hold. LoRa keeps its 1.5-second mode cycle; continuing that press to
four seconds shuts down. A short tap still cycles pages. Press meaning is fixed
at the initial page; a page change during the hold cancels that gesture.
The off path retains the reviewed W12 OLED/reset/I2C levels, GNSS supply OFF
only for the explicit L76K profile, and LoRa reset/enable OFF only when selected.
These are offline image changes, not measured power consumption or hardware
sleep/wake acceptance. Run the actual native button and SDK-faked power paths:

```sh
python3 firmware/w12/tools/power_tests.py --out artifacts/w12-power-tests-01
```

Version 0.2.20 uses `-Os` for the W12 main component, including its selected
RadioLib sources. SDK/controller options and enabled assertions retain their
original configuration. The builder and inspector check the effective final
optimization flag for every main-component source; the 1 MiB application
ceiling and hardware installation gates are unchanged. Run
`python3 firmware/w12/tools/size_policy_tests.py` for effective-flag and
assertion-preservation checks.

PToB owns the W12 native G1/RC7 application. The public target supports **RC**:
a BLE50 or BLE100 receiver, optionally an experimental BLE100 relay. It does
not provide a GM profile. LoRa defaults OFF; the optional LR2021 receiver is described below. Version 0.2.2 separates LoRa timestamp reads and counter extension from BLE association/fence processing while retaining GNSS capture resources. WiFi and PTS are disabled. GNSS input is disabled
by default; the explicit indicator profile below enables the reviewed Meshnology
W12 L76K route. This selection does not support arbitrary ESP32-S3 boards or
GPS-equipped variants.

The first build host is Apple Silicon macOS with Git, CMake, Make, Python and
ESP-IDF's Xtensa tools. Acquire the pinned public SDK and its toolchain:

```sh
python3 firmware/w12/tools/dependencies.py --dest artifacts/w12-deps/esp-idf
export IDF_TOOLS_PATH="$PWD/artifacts/esp-tools"
artifacts/w12-deps/esp-idf/install.sh esp32s3
. artifacts/w12-deps/esp-idf/export.sh
```

The pin is ESP-IDF v5.5.5, commit
`b774170ff46c393eeb5e495ea37936038d3f4f4f`. The builder checks the SDK revision,
cleanliness, submodule revisions, controller and GPTimer private ABI hashes.
The SDK's pinned `tools/tools.json` supplies public tool URLs and hashes.
The build uses Xtensa GCC 14.2.0 / `esp-14.2.0_20260121`. The local cache has an
uninitialized nested OpenThread mbedTLS subtree; it is explicitly recorded and
unused by this BLE-only target. No target dependency is excused by that rule. The SDK may build unused WiFi
components because its hidden WiFi Kconfig symbol defaults on. The final ELF
must contain no WiFi initialization/start or NVS write entry points.
Fresh public acquisition and a clean-checkout build are recorded in the
[public-source review](../PUBLIC_RELEASE_REVIEW.md). A freshly installed OS and
other host platforms remain unvalidated.

The builder disables ESP-IDF's component manager: this selected target has no
managed components. Builds use the pinned SDK and local source, without implicit
component downloads or the manager's host process-enumeration requirement.

Version 0.1.23 checks source cleanliness over the complete W12 tree and shared
registry generator. It separately records unrelated repository changes in
`repository_dirty`; every W12 source file still enters the immutable snapshot
and runtime identity. A changed W12 source requires `--development` and produces
`DEVELOPMENT_NOT_FLASHABLE` until committed. This does not authorize deployment.

Create an ignored owner-only `artifacts/local/enrollment.json`, using the same
group enrollment as your other targets. Group ID is a nonzero 16-digit lowercase
hex value, epoch a nonzero uint32, and PSK 32 random bytes, not all zero expressed as
64 hex digits. Do not copy these placeholder strings literally:

```json
{"group_id":"<16 hex digits>","key_epoch":1,"psk":"<64 random hex digits>"}
```

Create `artifacts/local/w12.json` with your actual factory MAC. The following
identity is synthetic. The 64-bit wire ID currently remains `0000` followed by
the full factory MAC; aliases and UUIDs do not replace that rule.

```json
{"board":"meshnology_w12_esp32s3","factory_mac":"02:00:00:00:00:05","device_id":"0000020000000005","label":"PToB-W12","legacy_wire_id":2}
```

`legacy_wire_id` is a local compatibility field for inactive legacy diagnostics,
not an authenticated wire identity or membership grant. GNSS capability is not
inferred from a factory label. For a Meshnology W12 with the L76K module and
verified RX39/PPS41 wiring, add this explicit field to the ignored device input:

```json
"gnss_profile": "meshnology_w12_l76k_rx39_pps41"
```

This selects board profile 12 and the existing 9600-baud UART RX39 / hardware
PPS41 input path. GPIO48 LOW enables the module supply; UART TX38, WAKE40 and
RESET42 remain inputs. It sends no GNSS commands, time assistance or NVM writes.
The generated factory capability and compiler flags must agree, and the build
inspector checks the actual configuration. Verify this physical route before
enabling it on a device; an enclosure, alias or initialized UART is not wiring
or reception evidence. Unrecognized profile strings and pin overrides fail.

On this profile, the top-right lamps show, from top to bottom: physical PPS,
checksum-valid GNSS packets, and a fresh valid RMC fix. PPS/packet pulses last
100 ms; the fix indication expires with the receiver's RMC freshness gate.
Missing or stale GNSS leaves these lamps dark even if BLE is active. The BLE
lock header retains its separate relative-clock meaning. Without this explicit
profile, the existing BLE PPS/RX/TX activity display remains available. GNSS
inputs do not grant source authority or qualify UTC/output precision.

Version 0.1.21 adds `W12_G1_PPS_SHADOW` records during bounded capture on this
profile. They compare each physical PPS with a production G1 model that was
available before the edge, retaining exact source scope and original deadlines.
`relative_phase_ns` and `output_phase_ns` separate the actuator phase from its
existing output correction. Invalid or reference-missing records remain
explicit; the older `W12_R2_PPS_SHADOW` uses a separate strict R2 model. Neither
metric calibrates the GNSS receiver, GPIO path or estimated relay RF timestamp.

From version 0.1.22, `--gnss-fast-map-diagnostic` optionally uses the validated
GPTimer driver latch to bracket the supported MCPWM software capture. It requires
the explicit verified GNSS profile and pinned core/driver configuration, and
defaults off. The build ID and inspector bind its actual compiler selection.
Map logs label it `FAST_DRIVER_LATCH_DIAGNOSTIC`; keep its populations separate
from `GUARDED` and `BASELINE`. Physical raw80 capture, midpoint conversion,
read/rounding enclosure and original anchor availability/age rules remain in
force. Measure the resulting enclosure and scatter before drawing conclusions;
the option applies no bias correction and changes no BLE clock or permission.

Fine-clock builds use `FINE_COUNTER_BRACKET` from version 0.2.6. They enclose
the existing GNSS group0 CH1 software trigger between two group1 CURRENT
latches, then extend and convert those captures using the production counter
origin. Counter/association checks stay outside the aperture; the GNSS queue
critical section defers its map callback until both latches finish. The pinned
MCPWM driver ABI, enabled 80 MHz timer and expected channel/group are checked.
Mapping gates and original availability rules are unchanged, and failures do
not substitute another clock. Keep this population separate from `GUARDED`,
`BASELINE` and the GPTimer fast-map diagnostic. Measured enclosure and causal
PPS prediction remain distinct from calibrated UTC or relay TX performance.

From version 0.2.7, GNSS-enabled BLE50 and BLE100 builds use a causal quadratic shadow predictor after
32 consecutive physical PPS observations. It tracks at most 20 ppb/s frequency
acceleration; missing/discontinuous PPS, unavailable prior models and forecasts
beyond three seconds remain invalid. `raw_predictor` and `mapped_predictor`
distinguish `QUADRATIC32` from `LINEAR_WARMUP`; non-GNSS profiles keep shadow PPS disabled.
No shadow observation trains BLE or grants source authority. Check the analytic
positive/negative drift and refusal cases without opening hardware:

```sh
python3 firmware/w12/tools/gps_shadow_tests.py --out artifacts/w12-gps-shadow-01
```

Supply the shared [display registry](../../registry/README.md)
through the same ignored map used by T096:

```sh
python firmware/w12/tools/build.py --idf "$IDF_PATH" \
  --out artifacts/w12-rc-01 --profile RC --listening-percent 100 \
  --enrollment artifacts/local/enrollment.json \
  --device-config artifacts/local/w12.json --registry artifacts/local/registry.json
```

`--estimated-relay` enables the existing BLE100 laboratory RC publisher. It
uses programmed TX estimates, advertises an unknown timing bound, and does not
claim actual TX capture or qualified relay precision. BLE50 is client-only.
The +3056 ns donor display correction experiment is excluded. Raw clock fitting
and local display correction remain unchanged; the optional provisional relay
output profile below is separate and defaults to absent. The shared BLE1M relay
path converts its programmed packet start to the nominal ADDRESS endpoint,
800 local 20 MHz ticks (40 microseconds) later, using the frozen clock model.
This is packet-format normalization, with unknown controller/RF delay; it does
not qualify physical precision. Legacy schema-1 profiles retain their **total**
adjustment from the raw programmed start. See the
[reference semantics and regression](tx-reference.md).
Version 0.1.1 requests 100 ms for both RC discovery advertisers, matching T096;
the separate precision train stays at 62.5 ms. Downstream advertising and
publication initially wait for current parent clock lock. Version 0.1.19 keeps
an established train and management discovery through a timing/quality gap for
up to 30 seconds. Corrections and READY withdraw immediately; canonical count0
packets retain synchronization without refreshing timing authority. Same-scope
fresh corrections can resume timing on the original train and child leases.
Two seconds of observed lock recovery, with no observation gap above 250 ms,
reset the loss deadline; flicker, polling and fresh packets without lock do not.
Child demand expiry, explicit stop, fault, changed source/path, controller
ownership and the original two-second drains retain their own gates. Upstream
reception remains available to acquire the parent.
See the [changelog](../../firmware/w12/CHANGELOG.md) for the lock prerequisites
and the distinction between relative quality and qualified precision.
Version 0.1.8 started continuous BLE100's direct G1 proactive replay refresh at
150 ms uncertainty, with urgency at 175 ms. This supersedes 0.1.2's 200/225 ms
thresholds, which left about 25 seconds before replay expiry despite the
20–40 second warm failure backoff. The new threshold reserves about 50 seconds
for a failed two-second exchange, maximum backoff and another two-second
exchange. The rolling two-starts-per-60-seconds cap remains; two failures can
still exhaust that opportunity. The original 250 ms hard replay limit and
signed source/model expiry stay intact. BLE50 retains its 100/125 ms thresholds.
Version 0.1.10 moves continuous BLE100 to 100/125 ms refresh/urgency, leaving
about 75 seconds at 2000 ppm. A failed transport followed by an authenticated
unusable time reply can use the old reserve through both backoff and the rolling
start restriction. The combined-policy regression retains those waits and tests
a third attempt before the original replay deadline; the 150 ms policy fails
that same bounded scenario. This does not guarantee recovery from arbitrary
failure sequences. When a failed attempt belongs to a still-reserved current
direct stream, its warm jitter is capped by the remaining replay time minus
three seconds for an attempt and scheduling slack, with the original 20-second
minimum and 40-second maximum. The rolling start gate still applies. Other
backoffs, BLE50 thresholds and all hard expiry gates remain unchanged.
Version 0.1.18 separates direct BLE100 authentication from coarse replay-clock
maintenance. A successful direct-root challenge starts one six-minute session,
with renewal randomized between 225 and 240 seconds. Each newly matched GR3
physical correction can update the coarse UTC anchor only while its preceding
causal interval, exact root/boot/source/train, signed map and original session
remain live. The original JOIN uncertainty plus physical capture enclosure is
retained; fit RMS is not an uncertainty substitute. Duplicate/old tuples,
authenticated receipt without correction, held estimates and polls cannot
refresh the anchor or move authentication expiry. A source gap still withdraws
FOLLOW and downstream output; missing corrected progress uses the existing
bootstrap policy. Stop, fault, parent/profile and source-scope changes clear the
session. BLE50 and RC7 retain their existing behavior. Thirty-minute sessions
are not enabled by this version.

Normal RC7 admission renewal is five minutes, with a six-minute lease. Direct
G1 JOIN replay refresh, pending admission and recovery after missing corrected
clock progress are separate traffic classes; inspect their counters separately.

Version 0.1.9 gives a current BLE100 precision receiver scheduling protection
independently of the relative-lock forecast. The reservation requires the same
authenticated current parent/path, live source/model/replay, working physical
clock and matched authenticated reception within 250 ms. A quality dip can
retain that receiver and renew its current parent without broad discovery;
it cannot authorize downstream advertising or relax the lock estimate. Expiry,
missing reception, explicit rebind and faults release the reservation.

Version 0.1.13 bounds automatic parent lock trials. A reachable GM gets at least
60 seconds from its first binding; same-parent renewals, boot/source changes,
FOLLOW and healthy packet delivery do not restart that minute. Sustained success
requires two observed seconds of the existing raw relative lock and one-second
hold forecast, with no observation gap above 250 ms. Short lock flickers cannot
postpone fallback forever. A full minute without sustained lock releases only
the scheduling protection against discovery and permits a fresh authenticated
relay trial, subject to existing RF ownership, admission budgets and backoff.
The current timing map keeps its original validity while seeking alternatives.

A failed parent is deferred for five minutes in bounded within-boot advisory
history; successful packet delivery does not clear that precision failure. This
prevents a reachable failed GM from immediately pulling a new relay trial back.
A sustained recovered lock clears the deferral. With no eligible alternative,
retain the current source and keep seeking; stale locators never authorize a
connection. Explicit parent pins and relay-only constraints remain in force.
All precision, source, replay, model, downstream and drain gates are unchanged.
Warm initiation requests 5 ms/20 ms scan windows; cold acquisition retains
20 ms/20 ms. Original attempt, exchange and actual drain bounds stay intact.
The controller remains responsible for physical radio scheduling.

Precision reports are drained between relay/GATT/JOIN calls under one shared
eight-report worker budget. Receive reference refresh and fresh report/decision
times surround yielding control work. The 50 ms worker-age and 250 ms physical
association limits remain unchanged. The host runner includes synthetic SDK
yield, scheduling and budget regressions; these do not measure actual RF priority.

Version 0.1.12 uses continuous proportional acquisition phase steering capped
at 50 ppm, returning to the original 1 ppm slew within 1 microsecond. It keeps
channel-aware frequency fits with at least two seconds and eight repeated-channel
differences. Acquisition uses 62.5 ms point spacing until the first fit, then
returns to 250 ms; the same 64 points grow toward the normal sixteen-second history.
Rate/scatter guards and all sixteen seconds of causal quality history remain;
startup errors stay visible until they age out. Completion
rebasing, stale-fit continuation and forecasts use the same phase law. Fine
hold, lock thresholds, authority and precision qualification are unchanged.

Version 0.1.11 separates direct BLE100 control handling from precision timing:
control callbacks may queue for less than one second, initiation has four seconds,
and link supervision requests 500 ms. GATT discovery may use the remainder of
the original five-second authenticated exchange. Processing after that exchange
expires is rejected, and coarse time extraction retains its own stricter bounds;
control membership never becomes physical precision evidence. Warm direct
BLE100 allows six starts per rolling minute, at least five seconds apart, with
5–10-second failure backoff. Its deadline-aware jitter reserves five seconds
for an attempt and slack.
BLE50/RC7 control bounds remain unchanged. Actual cancellation, stop and update
drains retain their original deadlines. Precision report age, association,
authentication, correction matching and quality/expiry gates are unchanged.

Version 0.1.6 preserves an acquired periodic receiver through authenticated
NO_SOURCE and unusable control replies. Positive foreign roots are counted
separately. After the first physical corrected update, failed retries wait a
random 20–40 seconds and a persistent rolling budget permits at most two
connection starts per 60 seconds. These waits cannot withdraw a still-eligible
publisher; actual ACL arbitration and the original parent-lock gate remain.

The last delivered physical model can continue the OLED's local HOLD estimate
when source or replay permission expires. Its error bound is unknown; FOLLOW,
root authority, relative lock and downstream timing retain their original gates.
A local estimate never renews a source, lease, replay context or fitter. Explicit
stop, fault or detected gross clock step withdraws it. No USB timing output is
implemented by W12.

Bounded capture diagnostics add `W12_G1_ADMISSION` and `W12_G1_RX_LOSS`. The latter
freezes up to eight worker report outcomes at corrected-model expiry, using
160 bytes of RAM. Outcome bits are report=1, stale=2, authenticated=4,
associated=8, mapped=16 and refused=32. These are observed reports, not a count
of unreported RF opportunities; controller DONE/CRC remain unknown. A fresh
physical map does not overwrite frozen evidence: the first loss remains for the
boot, with explicit counters for later expiries/reacquisitions and unavailable
later detail. The ordinary V3 status frame remains the
same size and does not enable capture by itself.

Version 0.1.7 isolates large status snapshots from the main loop and command
parser. The 0.1.6 attached candidate overflowed task main on its initial status
request despite passing offline checks; that candidate is withdrawn. The main
stack remains 12,288 bytes. New builds and standalone inspection check retained
GCC `.su` frames on selected main-task paths, including the nested status path,
with 4,096 bytes reserved for uncounted callees. Missing, ambiguous or dynamic
frame evidence fails closed. This is a regression bound, not a complete
call-graph proof; observe `main_stack_free_words` on the exact installed image
through normal status and bounded capture before accepting operational behavior.
That historical console key reports **bytes** on the pinned ESP-IDF Xtensa port
(`StackType_t` is one byte); do not multiply it by four.

Version 0.1.14 installs the outgoing adopted-ACL terminating owner and original
drain deadline before the public SDK call can yield. A delayed CONNECT for that
same canonical master handle acknowledges cleanup without starting GATT. Wrong
handles remain faults, stale generations remain refused, and a missing actual
DISCONNECT still faults at the original deadline. Production native regressions
inject each of those callback sequences; they do not identify the separately
observed incoming relay-owner fault.

`W12_G1_RELAY_OWNER` records provide fixed callback observations through the
existing bounded queue, without changing the ordinary status frame. Events are
CONNECT, TERMINATE_BEGIN, TERMINATE_RETURN and DISCONNECT. CONNECT decisions are
0=failed callback, 1=admitted, 2=refused recoverable overlap and 3=hard refusal.
A role of 255 denotes no observed descriptor. `descriptor_rc` belongs to a public
lookup for CONNECT; DISCONNECT uses its callback snapshot. State bits 0–7 are
parent admission lock, outgoing pending ACL, outgoing handle present, admitted
incoming session present, closing, fault, relay stopping and BLE100.
Service bits 0–3 record client running, client stopping, pending stop and room
for an original two-second deadline at the observation time. Gate bits
0–15 are lookup success, slave role, admission lock, ACL availability, no outgoing
handle, no outgoing initiation, BLE100, not closing, not faulted, not relay
stopping, generation available, challenge available, quarantine slot available,
new quarantine owner, distinct outgoing handle and distinct session handle.
The record keeps the original quarantine, outgoing and admitted-session drain
deadlines and public peer address; API observations are not RF measurements.
Ordinary source, path, lock, slot, authority and bounded drain gates remain intact.

A candidate requires a clean local commit. `--development` explicitly marks an
uncommitted build as unflashable. Build receipts bind source, SDK, effective
configuration, generated local inputs, registry, compiler and application bytes.
All output is ignored and owner-only. Headers and binaries contain your PSK;
never publish them. Storage provisioning remains build-time; the protected
bootloader/partition/NVS prefix is preserved during a guarded update.

Run checks with a recent LLVM Clang and the activated SDK environment:

```sh
CC=clang CXX=clang++ python firmware/w12/tools/g1_host_tests.py --out artifacts/w12-host-01
python firmware/w12/tools/rc7_permission_e2e.py --sdk "$IDF_PATH" --out artifacts/w12-permission-01
python3 registry/test_registry.py
```

These checks use production codecs/models and synthetic controller boundaries.
They do not establish physical RF duty, absolute UTC accuracy, independent
output precision, or reliability. Some retained historical fixture sources are
not part of the selected runner and do not inherit a passing result.
The permission runner also exercises the native relay server owner with
synthetic radio/TX and parent-lock inputs, including startup refusal, 30-second
loss hold, timing withdrawal, same-train recovery, flapping, child expiry,
scope/stop and unchanged admission drain deadlines. Relay flag bit 8 reports
management eligibility (including a held train); bit 9 reports an active loss
hold. Neither bit grants timing authority.

## GM successor recovery

W12 0.2.17 and N30 0.2.33 consume GM7 revision-3 ANNOUNCE and WITHDRAW.
The bounded decoder snapshot is described in the
[codec provenance](../../firmware/w12/main/gm7/README.md).
Only the selected authenticated root, boot, timing epoch and train can nominate
a successor. Control sequence, lineage, term, canonical advertiser and original
freshness are checked. The current admitted G1 coarse replay interval bounds
ANNOUNCE age; its paired UTC/monotonic sample bounds a later UTC-less WITHDRAW.
These are receiver routing hints, with no GM election or takeover authority.

A matching WITHDRAW immediately retires the old source. YIELD schedules that
retirement at the conservative latest original 750 ms boundary; repeated copies
cannot move it. A fresh announcement from the exact named successor boot in the
next term also starts recovery. The existing sustained source-loss fence can
reuse a recent nomination. Recovery clears obsolete selection history and gives
the successor one three-second re-observation preference, then permits ordinary
selection. Missing, changed, replayed or stale nominations cannot renew it.

Every replacement needs a fresh connectable locator and authenticated G1
admission, followed by new physical timing samples. Old models, permissions and
controller handles are not reused. Original source/model expiry, five-second
recovery credit, real PA/ACL drain, explicit pins and relay-only constraints
remain. A terminal old train is refused during direct and relay admission.
Fresh admission to a new scope clears the old control binding.

While following, a current corrected model and associated PA can place an
18 ms passive scan window at UTC quarter-frame offsets 32–50 ms, with an 8 ms
start guard and an end before 56 ms after the actual PA. No fresh model/capture
means no extra scan window. BLE50 remains within its existing listening window.
Ordinary discovery also consumes complete control reports. Delivery can still
miss control events; seamless handoff and RF latency remain unqualified.

Run the policy and actual native-owner regressions without hardware:

```sh
python3 firmware/n30/tools/gm_successor_tests.py --idf "$IDF_PATH" \
  --out artifacts/n30-gm-successor-tests
python3 firmware/n30/tools/gm_successor_tests.py --w12 --idf "$IDF_PATH" \
  --out artifacts/w12-gm-successor-tests
```

These cover withdrawal, fixed yield, promotion, loss, missing-successor fallback,
pins, relay-only policy, bad MAC/address and protected listening, plus scope,
freshness, duplicate and original-expiry boundaries. Host/native success does
not establish hardware acceptance or independently measured timing accuracy.

## Hardware boundary

The public builder never opens hardware. A generic public flasher is not yet
shipped. Installation requires separately authorized exact silicon/current-image
binding, exclusive ownership, bounded application drain, identified rollback,
actual application readback and exact protected-prefix comparison. Routine
app-only updates reuse an identified, verified recovery image; do not back up
the full flash or copy the installed application before every update. Preserve
only a partial final sector when needed to keep bytes outside the application
unchanged. A full flash backup requires a separate explicit operator request.
A failed or unknown gate stops the update. Do not replace those gates with a
mass erase, partition change or guessed reset. **Do not run `idf.py flash`,
use generated `flash_args`, or copy the SDK build log’s generic flash command:**
those include bootloader and partition writes outside this app-only procedure. The migration's local C-2-only
validation uses the existing reviewed laboratory guard with outputs retained
under ignored PToB artifacts; it is not general public installation acceptance.

Version 0.1.15 contains a controller Connection Parameters rejection
(`BLE_HS_HCI_ERR(0x3b)`, decimal 571) only at the outgoing Connect return. The
request must still be pending with no adopted or terminating handle, and its
normalized parameters must exactly match the production request: interval 6/6,
latency 0, supervision 10 or 50, scan interval 32 and window 8 or 32, and CE
length 0/0. The pinned NimBLE host clears its unaccepted master procedure before
returning the command refusal. Existing admission failure accounting, peer
cooldown, rolling start rate and retry remain in force. The last API and failed
peer counters retain the observation. Other error codes, API sites, malformed
requests and accepted ownership remain subject to their existing fault rules.
This contains the consequence of a rejected request; it does not explain the
controller rejection or repair the separately observed incoming-owner fault.

During an explicitly bounded capture, `W12_G1_TX_EST` retains successful selected
estimated-TX events independently of their error or channel, once per sixteenth
event and at most 60 records per capture identity. It uses the existing worker
queue and main-task renderer. Worker observation reads the local clock/logger;
it adds no callback formatting, radio API or ordinary status-frame bytes. The fixed record contains publisher
and root identities/boots/epochs, train/publication/event/channel, programmed
`tick20`, callback time, controller clock bracket and half-slot clock, emitted
`tx_ns`, frozen map tick/epoch/phase/rate, original and queued times, inherited
source issue/expiry and correction age. `rate_known=0` means no representable
rate value. `W12_G1_TX_EST` retains `reference=PROGRAMMED_HALF_SLOT actual=0
bound=UNKNOWN`; `tick20` is the raw scheduled packet start, while `tx_ns` is
the emitted correction. Its version-2 `W12_G1_TX_PATH` companion distinguishes
the raw start, nominal ADDRESS tick/projection, actual projected reference
component, residual profile adjustment and applied total. A successful
selection is not RF transmission confirmation.

For offline correlation with a hardware receiver, match publisher and boot,
source/path epochs, train, publication, event and channel before comparing the
emitted correction with the receiver's immutable ADDRESS capture. Retain failed
associations, missing records and capture boundaries. PPS shadow values observe
the combined upstream receive map, estimated relay transmit reference and
receiver/reference path. A fixed offset or small residual does not independently
identify TX versus RX bias, establish UTC association or bound RF latency. Do
not treat a measured shadow offset as an independently identified TX/RX
calibration. A separately reviewed provisional aggregate-path profile is
available below; it does not supply missing endpoint evidence. Source, slot,
parent and precision gates are unchanged.

## Provisional aggregate-path offset

Version0.2.8 also selects two shared provisional W12 relative-PPS constants:
`receive_ns=7671` and `relay_residual_ns=3030`. The receive term appears once
in delivered clock copies, including relay inputs, while the raw fitter stays
unshifted. The relay term adds to the nominal ADDRESS projection unless an
explicit aggregate total below supersedes that residual. Neither term changes
source envelopes, deadlines, physical captures or the oscillator frequency fit.
Receipts carry `phase_offsets`; status reports `rxcal`, PPS records retain
`raw_relative_phase_ns` and `receive_correction_ns`, and version3 TX companions
retain the receive term plus nominal/residual/total projection. An inspector
and matching console manifest must disclose both constants.

The measurements come from one fine BLE100 W12 and a T096 child with its
existing receiver correction. Applying these defaults to all W12 units and
profiles is an operator-selected, unmeasured transfer. It does not identify
which RF endpoint caused the combined bias, certify legacy/BLE50 behavior,
reduce residual jitter, calibrate UTC, or establish an uncertainty bound.
GNSS-off profiles receive the same clock adjustment without enabling shadow PPS.

Version0.2.9 fine builds use `FINE_RAW_CURRENT_BRACKET` for the Bluetooth clock
read. Two owner-validated raw latches enclose the unchanged pinned ROM reader;
counter extension and coordinate conversion occur afterwards. The aperture
and reader reside in instruction RAM. Legacy builds retain the GPTimer bracket.
Refused or stopped reads never substitute another clock. Retain measured
aperture widths and repeat relative-PPS comparisons: earlier path offsets and
precision populations do not qualify this new mapping by assumption.

Version0.2.10 requires four valid programmed-clock projections for a W12 relay
estimate and uses their rounded arithmetic mean. It refuses missing inputs or
spread over2µs without trimming a sample. The four ticks remain in version4
TX path companions, and the estimate label is `PROGRAMMED_HALF_SLOT_MEAN4`.
The provisional relay residual becomes4,026ns, while receive remains7,671ns.
This changes the estimator; repeat the paired hardware measurement rather than
carrying forward a prior precision claim. RF and UTC bounds remain unknown.

Version 0.1.16 accepts an optional `provisional_path` object in the ignored,
factory-bound device input. Its absence selects no provisional profile; the
shared nominal ADDRESS conversion still applies. This is an operator
assumption based on an observed combined path, not automatic calibration or
feedback from a running PPS witness. It requires `--estimated-relay` and
BLE100. Do not infer an offset for another W12 or silently copy a profile.

```json
"provisional_path": {
  "schema": 1,
  "scope": "AGGREGATE_PATH_RELATIVE_PPS",
  "offset_ns": 47000,
  "root": "<16 lowercase hex digits>",
  "root_boot": "<16 lowercase hex digits>",
  "source_epoch": 1,
  "allow_source_epoch_transfer": false,
  "receiver": "<training receiver identity: 16 lowercase hex digits>",
  "receiver_source_sha256": "<64 lowercase hex digits>",
  "receiver_phase_correction_ns": 0,
  "evidence_sha256": "<64 lowercase hex digits>",
  "baseline_evidence_sha256": "<64 lowercase hex digits>"
}
```

Replace every placeholder with reviewed evidence. The example offset illustrates
sign: an aggregate shadow phase of −46,940 ns would become +60 ns after adding
47,000 ns. It is not a recommended offset for an arbitrary device. Record the
training receiver's actual existing phase correction; this metadata neither
changes nor applies that receiver correction again. Preserve source, boot,
parent, reference association, valid/invalid/missing denominators and separate
measurement populations in the evidence. A baseline from another source version
or time is not a matched differential calibration.

Offsets must be nonzero signed integers within ±100,000 ns; unsupported fields,
missing provenance and malformed scope fail validation. The builder derives a
profile ID and full provenance hash from the object and exact publisher identity.
These enter the generated device header, receipt and runtime build identity.
The inspector verifies them against the retained input. A console manifest for
an enabled image must carry the receipt's exact `provisional_path_profile`;
the framed parser refuses an undisclosed configured offset. `pcfg` reports an eight-hex-digit prefix of the configured profile ID, within
the unchanged whole-frame capacity. The nonce-bound source/build and full
manifest bind its complete identity and offset; a prefix alone is insufficient.
`cal` retains its local display meaning.

The offset applies only to this publisher at RC1 while it follows the exact
configured root identity and root boot with the existing valid BLE1M receive
reference from the accepted capture incarnation. By default, its source epoch
must also equal the training `source_epoch`. A mismatched scope selects zero. Since RC7 uses a
multicast stream, the adjustment affects **all receivers consuming that
publisher**; training receiver metadata cannot restrict it to that receiver.
Changing the root boot requires a fresh profile review. Optional
`allow_source_epoch_transfer: true` explicitly permits an **unmeasured
provisional transfer** across source epochs of that same root boot; omission
means false. Training epoch/provenance stay unchanged, and each current epoch
remains in the raw TX tuple. New epochs still require all ordinary fresh
admission, authentication, path, replay and source-expiry checks. This flag
cannot borrow an old epoch's authority or transfer its observed jitter as a
guarantee. An RC2 does not apply the RC1 profile again.

The worker freezes the effective profile with each publication. Schema 1's
`offset_ns` remains the **total** adjustment from the original raw programmed
start. The shared nominal ADDRESS component is included in that total, rather
than added a second time. A +47,000 ns legacy total therefore has a residual
near +7,000 ns after the nominal 40-microsecond conversion. The worker uses the
actual difference between the two frozen clock projections, preserving the
legacy emitted value exactly even when clock rate or phase slew makes this
difference a few nanoseconds different from 40,000. No private profile rewrite
or new calibration is implied. A scope mismatch selects the standard nominal
ADDRESS conversion without the provisional profile.

Raw, nominal ADDRESS and emitted values must all lie inside
the unchanged signed source envelope. It never alters raw physical captures,
the oscillator fit, source/proof/hold/precision gates, root envelope, slots or
parent rules. During bounded capture, `W12_G1_TX_PATH` accompanies the existing
sampled `W12_G1_TX_EST`, retaining publisher/root boots and epochs, train,
publication/event/channel, programmed tick, raw and emitted timestamp, effective
profile/legacy total and capture ID. Version 2 also records `address20`,
`address_ns`, `base_ns`, `residual_ns` and `applied_ns`; `base_ns+residual_ns`
equals the applied total. Match the complete tuple and preserve missing
companions or queue losses; no tuple is an actual RF TX observation.

Report signed mean offset, slope and residual jitter separately. A constant
adjustment moves the mean; it does not reduce residual jitter, prove which
endpoint caused the bias or create an uncertainty bound. RF/UTC bounds and
absolute association remain unknown. GNSS reception/PPS/fix indications remain
separate observations under their existing gates; this profile sends no GNSS
commands and does not discipline the BLE clock from weak indoor GNSS.

Version 0.1.17 corrects 0.1.16's generated profile macro binding. The earlier
image could declare a nonzero profile while compiling the zero default; its
strict `pcfg`/manifest check refused startup acceptance. The builder-generated
header now runs through the production config and emitted-timestamp regression,
and native compile assertions compare all enabled fields with that header.
Do not reinterpret a declared 0.1.16 profile as evidence that it was applied.

## Fine-clock test profile

Version 0.1.23 accepts `--fine-clock` on BLE100 builds. Add it to the ordinary
build command using each unit's existing factory-bound device input and shared
enrollment/registry. `--estimated-relay` remains an independent explicit option.
BLE50 and `--gnss-fast-map-diagnostic` are refused with this profile. Omitting
the flag preserves the legacy GPTimer backend and its 1 µs fine-slew threshold.

The fixed W12 route connects RW_RX_ON238 through **GPIO16** into MCPWM group 1,
channel0. Channel2 supplies bounded software CURRENT captures; channel1 stays
disabled and disconnected unless the optional LoRa receiver borrows it. There is no TX output or arbitrary pin
override. GPIO4 controls a PA rail and GPIO5 reaches the display interface, so
N30's GPIO4/5 route is unsuitable. The manufacturer's
[W12 schematic](https://wiki.meshnology.com/assets/files/W12-MB-V0.2%281%29-b62d4b0929d59c1a3c9ca5847eb65bf8.pdf)
shows IO16 between the MCU and expansion connector, separately from the OLED,
GNSS and LR2021 routes. Confirm the actual board revision and absence of external
loads on IO16 before any separately authorized installation. A schematic and
successful build do not verify an assembled unit's wiring.

The delivered BLE clock uses one fixed group 1 origin at 80 MHz, rescaled to
the existing 20 MHz coordinate with nearest-50 ns rounding (ties upward).
RX, current time, Bluetooth brackets, fitted maps, UI and estimated-TX
projections share that origin. A failed, stopped, stale, wrongly clocked or
late-event counter withdraws availability; it cannot substitute GPTimer ticks.
GNSS retains its group 0 capture owner and maps into this selected production
coordinate. MCPWM private ABI/driver/register hashes are pinned and verified.

For BLE1M, PDU191/ext0 current GR3 or RC7 packets, one unique rising/falling pair is required
inside the immutable descriptor-located search interval. Missing, ambiguous,
overwritten and unsupported observations are refused. The falling capture
minus **1568 µs** is an explicit experimental phase convention inherited from
N30's matching packet class, **not a W12 RF endpoint calibration**. The original
coarse enclosure plus actual coordinate displacement still guards source life;
it is not an RF accuracy bound. Legacy PDU159/ext0 packets retain their
descriptor phase on the same group 1 origin: no fine phase convention has been
qualified for that packet class. Other classes cannot silently inherit one.

The profile also retains all 16 Hz fit opportunities in the bounded 64-point
history, freezes predictions before corrections train them and observes rate
acceleration over at least one second. It selects the existing 1 ppm fine slew
when absolute phase backlog is **strictly below 2000 ns**; at exactly 2000 ns it
retains bounded acquisition steering. Source/model expiry, causal tails, lock
thresholds and downstream permissions are preserved. Existing aggregate-path
profiles from the descriptor backend cannot transfer to this new phase path;
fine test builds refuse those profiles pending separate W12 calibration.

During ordinary bounded capture, `W12_FINE_STATUS` reports callbacks, fine
claims, direct descriptor samples, pending searches, missing/ambiguous/unsupported
results and faults. These are software observations with unknown RF coalescing,
RF delay and UTC bounds. A callback is not proof of an RF packet. Normal framed
status retains its existing byte/row/deadline bounds. Maintenance retains the
counter until all RX hooks and clock consumers drain, then requires actual pad,
channel and timer retirement within the original two-second deadline.

```sh
python3 firmware/w12/tools/fine_tests.py --out artifacts/w12-fine-tests
```

The runner exercises the actual owner and maintenance code with synthetic
peripherals under ASan/UBSan, including wrap, late pending captures, counter
failure, route/enable failures, interrupt bursts and cleanup refusal. The
ordinary G1 host runner tests both legacy and fine clock fitting. Native builds
retain selected main/NimBLE stack checks. These checks do not establish W12
sub-2 µs accuracy, RF marker semantics, calibrated relay output or UTC accuracy.

## Optional LR2021 dual timing and screens

The receiver borrows group 1 CH1 for the LR2021 DIO8 signal on W12
GPIO14 ([board wiring](lr2021.md)), corrected in0.2.1. BLE RX remains on
CH0/GPIO16 and CURRENT on CH2. Version0.2.2 retains that physical80MHz timer
but gives LoRa its own software origin, extension and reader, separate from
BLE association/fence processing. It polls this counter during packet silence;
hardware CH1 edges remain the timestamp input. No software arrival timestamp,
BLE clock model or asynchronous counter map is substituted. GNSS keeps its group 0 timer, RX39/PPS41 and existing power/input
profile. LoRa OFF disconnects only CH1 and leaves the BLE counter alive.
Maintenance stops every consumer before retiring that counter.

Acquire the public MIT RadioLib 7.8.1 pin explicitly, then add both options to
the ordinary fine-clock build command:

```sh
python3 firmware/w12/tools/radio_dependencies.py --dest artifacts/radiolib-7.8.1
# Add to the existing build invocation, using an exact enrolled registry identity:
# --fine-clock --radiolib artifacts/radiolib-7.8.1 \
# --lora-transmitter-factory <exact-16-hex-T096-factory>
```

Zero transmitter identity is the default and compiles the radio OFF. A nonzero
identity requires the pinned dependency and fine clock. The generated header
binds that factory, receiver identity and existing group enrollment into the
runtime build ID. Never select a transmitter from an unverified alias. The
image retains the public dependency source and license in its ignored artifact;
there is no private dependency or implicit download during compilation.

The fixed RX modem is 915MHz, SF10/BW500/CR4/6, preamble 12, explicit header,
CRC 2, private sync 0x12, matching RC8 wire3. The HAL refuses SetTx, TX test and
automatic RX/TX commands. Actual RX_DONE capture precedes FIFO processing;
authentication, replay, companion/context and original source deadlines remain
those of the selected portable T096 policy. Chipset delay and total RF/UTC
uncertainty remain unknown. RC8 link RMS describes fitted relative observations.

AUTO uses N30's 180 s accumulated lock-loss and180 s continuous lock-recovery
policy. A fresh actual BLE precision lock takes display priority. KEEP ON/OFF
can be selected by holding 1.5 s on the LoRa page; settings reset on reboot.
Tap navigates Clock, Precision and LoRa. Precision's SYNC RMS is the live BLE
causal metric; TRIM is software-clock steering, not N30's display-only REL
residual trend. Version0.2.3 selects current LoRa LINK RMS on Clock and Precision
when LoRa supplies time, with explicit fit warm-up and WAIT RX states. The LoRa
page shows its own current link RMS. Expired fits remain unavailable. AUTO's
one-second precision forecast uses the original model/replay deadlines while
separately requiring coarse UTC permission to be live now; short coarse renewals
do not restart the180s recovery countdown. GNSS indicators and the Clock-page relay control are preserved.
The existing GPS diagnostic page remains accessible by bound `ui page 14`.

Fresh ticket-bound commands `lora auto`, `lora on`, `lora off`, and
`ui page 0`, `ui page 15`, `ui page 9` provide equivalent volatile controls.
Normal framed status keeps its 9 body rows,8704 byte whole limit and original
1.5 s response deadline. LoRa builds omit seven historical discovery counters
from the R2 row to accommodate current LoRa state/UTC/capture error counters.
All source, owner drain, fault and stop-generation checks remain.

A separate 300 s no-time watchdog accepts fresh authenticated current BLE or RC8
UTC until their original deadlines, regardless of UI page and precision lock.
Retained HOLD, stale snapshots and GNSS packet receipt cannot reset it.
Explicit OFF or maintenance disables only that source-loss timer. The 120 s
continuous owner-fault watchdog and RTC restart evidence are preserved.

```sh
python3 firmware/w12/tools/sync_watchdog_tests.py --out artifacts/w12-sync-tests
python3 firmware/w12/tools/lora_tests.py --idf artifacts/esp-idf --out artifacts/w12-lora-tests
python3 firmware/w12/tools/status_budget_test.py --rc7-estimate --lora
```

These host checks exercise actual policy/codec/HMAC, replay/expiry,180 s modes,
300 s supervisor/reset boundaries, capture coexistence, owner cancellation, DMA
failure retention and TX refusal. Native builds use the pinned real RadioLib;
the owner fixture uses a synthetic RadioLib call boundary. These are separate
from actual RF interoperability and independently measured clock accuracy.

### Separate RC9 service and display cadence

Add `--rc9` to an existing `--fine-clock --estimated-relay` LR2021 build to
receive RC9 and serve current RC8 through the separate RC9 BLE service. The
flag is bound into the runtime build ID. Normal RC7 advertisements are drained
before SID14 periodic/SID15 connectable RC9 advertisements start. Original
source expiry remains unchanged. See [the RC9 contract](rc9.md).

The OLED redraw deadline follows the selected UTC fractional second. A forced
page repaint retains that boundary; a render crossing it schedules an immediate
fresh paint. `rc9 diag` includes selected time, sample, completed paint and next
boundary timestamps under its original nonce/CRC deadline. These observations
measure local scheduling, not independently calibrated UTC or optical latency.

AUTO's quality forecast matches the native G1 controller: coarse and corrected
packet permission must each be live now, while the one-second forecast uses the
original replay anchor. Frequent short packet renewals do not revoke an
otherwise healthy fit. This policy grants no future clock output permission;
actual source/correction expiry still withdraws synchronization immediately.
The separate fixed ten-row RC9 diagnostic writer retains the normal nine-row
status writer, whole-frame capacity and original deadline. Test the real native
formatter, output worker and CRC parser together:

```sh
python3 firmware/w12/tools/rc9_console_tests.py --out artifacts/w12-rc9-console
```


Version 0.2.11 retains the 0.2.10 capture/offset/profile contracts and adds
[bounded normal BLE recovery from AUTO RC9 fallback](rc9.md#auto-client-return-to-normal-ble).

## Leader recovery

Version 0.2.12 discloses `leader_recovery_policy` as
`FENCED_SCOPE_CACHE_CLEAR_1500MS_V1`. Sustained loss beyond the existing
1.5-second acquisition bound discards the old session, estimator, selection
history, locators, cooldowns and queued timing records. Authenticated changed
source incarnations also require fresh acquisition. Explicit pins and hardware
profiles survive. Exact controller owners drain under their original deadlines;
cleared software state does not constitute controller closure. New authenticated
JOIN supplies a coarse bootstrap; precision requires newly matched corrections.
Repeated invalidations cannot grant a recovery admission more often than once
per five seconds. Short gaps and a live source seen alongside an advisory hint
retain their original acquisition and permission limits.

Run the offline owner, source-loss, new-leader admission and pin regressions:

```sh
python3 firmware/w12/tools/g1_host_tests.py --out artifacts/w12-leader-tests
python3 firmware/w12/tools/rc7_permission_e2e.py --out artifacts/w12-owner-tests
python3 firmware/w12/tools/rc9_native_tests.py \
  --objects artifacts/w12-leader-tests --out artifacts/w12-rc9-tests
```

These use the pinned SDK's public crypto headers through `IDF_PATH` and synthetic
radio callbacks. Hardware verification must preserve missing/expired samples,
measure reacquisition separately from precision lock, and test actual GM changes.
Neither host checks nor internal fit RMS establishes calibrated UTC accuracy.

## Shadow holdover

W12 0.2.22–0.2.24 keep the existing production servo and add an experimental
numerical shadow model. It learns a bounded linear temperature response from
BLE-derived frequency fits using an eight-second silicon-temperature proxy.
There is no local GNSS/LoRa training or production cutover. Insufficient thermal
coverage retains the fitted rate without inventing a temperature slope.
Version 0.2.24 anchors frequency to the last trusted BLE fit. It removes the
0.2.22/0.2.23 blend with a historical absolute temperature-rate intercept, which
can confound warm-up and aging with temperature. Short BLE gaps now remain
inside the frozen run and are counted explicitly, rather than restarting it.
Only temperature changes after freezing modify the anchored rate; no later BLE reference corrects it.

The previous 0.2.22 sampler tested only a single endpoint at one second. That
was a measurement ceiling, not a measured oscillator limit. An extended trial
freezes the raw clock, phase and candidate coefficients at their actual availability
time for 120 seconds. Every later authenticated matched physical BLE event
scores both candidate and held current method, without correcting either.
BLE-derived learning continues separately for future trials; it cannot change
held coefficients. A causal cumulative temperature integral survives eviction
of the eight-reading history and never uses sensor readings after physical RX.

Each trial retains the first sampled absolute error **>= 1,000 ns**, the last
preceding sampled age below that threshold and the maximum error over the
entire run. It continues after a crossing to preserve tails. A no-crossing
120-second result is censored by the test cap. Gaps above 250 ms increment
missing-coverage counts without restarting the frozen clock; their interiors remain unmeasured. The Hold page discloses these
gaps. Missing causal temperature or lost source permission aborts a run, with
no zero filling or interpolation. Source changes reset the population. Lifetime
counters retain starts, completed 120-second trials, missed coverage and aborts.

`P99:` remains measured error, not a percentile of holdover duration. It is the
nearest-rank absolute error of the latest 128 BLE observations **within the
current trial**, requiring 100 observations. These are correlated samples of
varying holdover ages, not 100 independent trials or a safe-duration guarantee.
The window resets each trial and becomes `--` after five seconds without a
score or immediately on abort. The Hold page shows elapsed age / 120s, P99,
sample count, and `1us HIT` or `SUB 1us`. Only the latter reports the latest
sampled safe age before any crossing. It cannot cover errors between samples.

During bounded capture, `W12_SHADOW` records retain exact integer reference and
candidate/baseline predictions, signed errors, trial/population, physical age,
first crossing, sampled safe age, cumulative maxima, gap count and maximum
observed gap. Use
`tools/shadow_holdover_tests.py` for frozen-clock isolation, 120-second runs,
100-second threshold crossings, coverage failures, causal thermal history,
rolling tails and actual framebuffer checks without opening hardware.
Use `tools/shadow_holdover_analyze.py` on captured console data to reproduce
P95/P99/MAX and paired comparisons, keeping trial and age-band populations
separate. It also supports historical 0.2.22 records. Partial starts, missing log
sequences and tails remain explicit. No detrending or tail removal is applied.
Radio-off behavior, continuous bounds and independently measured UTC remain
unqualified. Tuning and production promotion require repeatable matched trials
across operating temperatures and loads; no automatic cutover is implemented.

## Sampled P99 hold duration

Version 0.2.28 defines the clock footer's `P99:` as seconds of hold at a fixed
absolute error limit of 1000 ns. `P99:2.2s` is the conservative tested age whose
99th percentile of sampled prefix maxima remains below that limit; `>=12s`
means the current qualifying evidence reaches that age without locating the
percentile's limit. `--` means fewer than 100 runs or stale evidence. This is an
empirical sampled estimate, not a statistical confidence bound on a population.
The performance floor is two seconds; the display is not capped there.

A separate BLE-only shadow sampler freezes the same rate-choice policy and
phase used by production HOLD. Later accepted BLE pairs only score each frozen
clock. Thermal coefficients are always zero in this population; the existing
experimental temperature model and its error statistics continue separately.
Calibration and the active servo are unchanged. A root/boot/source/owner fence
clears the duration population. A five-second witness expiry hides the result.

At each tested age, keep the maximum absolute error from entry through the first
accepted witness at or after that age. A witness up to 250 ms after the target
conservatively includes its overshoot; an unsupported age is unknown, never a
success. An early threshold crossing stays in every subsequent prefix even if
later error improves. Each age keeps the latest 128 complete outcomes, requiring
100, and allows only `floor(n/100)` failed or unknown outcomes. Select a
contiguous passing prefix of age bins (250 ms spacing through four seconds,
then 6/8/12/16/24/32/48/64/96/120 seconds). Display tenths round downward.
Internal gaps over 250 ms remain counted without interpolating their interiors;
this statistic concerns observed samples and grants no continuous error bound.

Sixteen frozen clocks and packed outcomes use at most 2560 bytes. Start near
once per second with a 12-second probe, extending to 24/48/96/120 seconds only
when the current horizon qualifies. Longer probes reduce launch frequency to
fit the same bounded storage. Overlapping starts are correlated and are not
independent trials. Error tails continue through each probe after a crossing.

The Hold page shows the 1 µs criterion, P99 seconds, run count, probe horizon and
gaps. Existing nine-row status fields `ui_hold_percentile=99`, `ui_hold_ms` and
`ui_hold_samples` mirror the rendered value; they add no response bytes/fields.
Capture-only `W12_HORIZON_START/END/RESET/STATUS` records keep this population
separate from thermal `W12_SHADOW` and selected-output `W12_HOLDOVER` records.
`tools/holdover_horizon_analyze.py` independently replays frozen clocks from
accepted pairs, preserves unknowns/early crossings and verifies rolling duration
results after capture-start history has left the rolling window. Original
P95/P99/MAX error diagnostics, finite production tests and recovery remain.
No persistence or partition change is enabled.

## Production holdover

**Current 0.2.28 policy: temperature is shadow-only.** Production retains the
last trusted BLE frequency and phase, with an explicitly zero thermal coefficient.
Temperature learning and thermal-candidate scoring continue separately. The
optimized retained-clock selector, continuity through coarse acquisition,
step-free return, calibration and source/relay expiry remain in place.
Rate-only HOLD has no temperature dependency, so missing or invalid sensor
readings do not remove the delivered clock. They still invalidate thermal
shadow observations. The firmware/compiler receipt binds
`BLE_RATE_CONSENSUS_TEMPERATURE_SHADOW_V3` and `SHADOW_ONLY`; BEGIN records expose
the policy and actual zero coefficient. The analyzer refuses nonzero production
temperature steering under that policy. P99/P95/MAX, sample counts, crossings,
gaps and the existing finite selected-output tests are preserved.

Version 0.2.27 separates HOLD rate selection from the fast BLE servo. A bounded
history retains accepted physical pairs at least 250 ms apart over 32 seconds.
Fixed channel intercepts remove channel offsets in separate 16/32-second rate
fits. Both require 32 points, 85% span, eight repeated-channel degrees of freedom
and estimated guards no larger than 10 ppb. The longer guard must improve on the
fast fit, both rates must agree within 5 ppb, and the median of the three rates
must lie within 15 ppb of the fast rate. The final change is capped at ±5 ppb.
Fits refresh at most once per second; stale inputs or cache age over two seconds
fall back to the fast rate. These gates are engineering policy, not measured
RF or UTC uncertainty. Phase is reanchored continuously when HOLD is selected.
Temperature never influences this rate choice. The active BLE actuator is unchanged.

`W12_HOLDOVER_PAIR` records accepted BLE local/reference/availability/channel
and generation. `W12_HOLDOVER_RATE` records cached fits and both selected and
fast projector inputs. `W12_HOLDOVER_ENTRY` losslessly encodes the actual fast
fit's compact points as little-endian U32 local delta, signed I24 residual and
U8 channel, relative to full-width bases. Bounded capture is required; ordinary
status frames remain the same size. `holdover_rate_replay.py`, called by the
production analyzer, reconstructs rate fits using only inputs available before
the recorded cache time and checks entry provenance. Witness samples score
frozen projectors; they never train the running HOLD clock. Missing capture
history cannot certify replay. Keep all rejects, gaps and tail errors.

For the short HOLD target, use repeated finite 12-second selected-output trials
and report 0–2, 2–4, 4–8 and 8–12 second age bands. A practical initial cohort is
24 trials after 60 seconds of acquisition, with BLE return between trials.
Report per-trial and pooled P95/P99/MAX, sample counts, gaps, fallback/selection
counts and return steps. Samples in one boot share operating conditions; this
cohort is not independent cold-start, GM-transition or 120-second qualification.
Do not infer continuous bounds from sampled P99. Separate tuning captures from
fresh validation captures before increasing the rate-change cap or adding trend.

The following describes the original 0.2.25 thermal production experiment;
0.2.26 keeps its selector and continuous return while isolating temperature.

Version 0.2.25 selects the same frozen temperature-change model for local
production HOLD. The last measured BLE rate, phase and learned temperature
coefficient freeze at entry. Only causal silicon-temperature samples advance
the eight-second lag and integral. Missing sensor history, a sensor gap above
three seconds, invalid temperature or an excessive correction refuses delivery.
The temperature coefficient is zero until sufficient learning is available.
BLE remains the only timing reference; temperature is a proxy, not a time source.

A trained retained clock takes precedence over a new coarse JOIN until the
successor has its own trained physical model. On return, the trained BLE servo
starts from the delivered HOLD time, preserving its target and bounded slew.
Fresh observations must reestablish quality. A difference above one millisecond
refuses that continuous handoff. Local HOLD grants no new source, relay or
authentication permission. A finite test does not qualify actual GM failover.

The ticketed, boot-bound console command `holdover test N` accepts 1,000–120,000
milliseconds only when following a trained clock; the owning loop also checks
sub-microsecond quality and remaining phase before starting. `holdover test 0`
stops it. During the bounded test the **delivered local clock** uses the frozen
production model while incoming BLE continues as an observation witness.
No BLE reference updates that delivered model. Ordinary acquisition remains
ready for return. The original finite capture lease and hardware guards apply.

Capture `W12_HOLDOVER_EVENT`, `W12_HOLDOVER_SENSOR`, `W12_HOLDOVER`,
`W12_HOLDOVER_STATUS` and `W12_HOLDOVER_RESUME` records together. BEGIN retains
the exact floating-point entry parameters; sensor records permit independent
causal replay. STATUS samples the same reader used by the display and records
selected model, actual UTC output, delivered microseconds and following state.
Sample records normalize the configured receiver shift for comparison with BLE.
Use `tools/holdover_production_analyze.py` to check replay, output decomposition,
rolling percentiles, selection and zero-step return. A completed activation
check requires a complete bounded trial, at least 100 observations from its
start, selected-output records, no missing sample sequences and a verified
zero-step resume. It still reports every sampled error, crossing and coverage gap.

Run `tools/shadow_holdover_tests.py` and
`tests/holdover_production_analyze_test.py` offline before native preparation.
These include production/shadow equivalence over 120 seconds, frozen learning,
stale-sensor rejection, handoff and actual OLED rendering. `P99:` remains the
latest 100–128 sampled holdover errors; during a production test it scores the
selected output, and otherwise scores continuous shadow validation. A production
HOLD page identifies the selected path. Neither the display nor the activation
checks establish continuous sub-microsecond performance or independent UTC.
