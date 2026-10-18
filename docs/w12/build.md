# Build W12 RC

Copyright (c) 2026 Jonathan Yantis. MIT.

PToB owns the W12 native G1/RC7 application. The public target supports **RC**:
a BLE50 or BLE100 receiver, optionally an experimental BLE100 relay. It does
not provide a GM profile. LoRa, WiFi and PTS are disabled. GNSS input is disabled
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
output profile below is separate and defaults to zero.
Version 0.1.1 requests 100 ms for both RC discovery advertisers, matching T096;
the separate precision train stays at 62.5 ms. Downstream advertising and
publication wait for current parent clock lock and withdraw when that lock or
source is lost. Upstream reception remains available to acquire the parent.
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
synthetic radio/TX and parent-lock inputs, including startup hold, lock loss,
reacquisition and unchanged admission drain deadlines.

## Hardware boundary

The public builder never opens hardware. A generic public flasher is not yet
shipped. Installation requires separately authorized exact silicon/current-image
binding, exclusive ownership, bounded application drain, identified rollback,
actual application preservation/readback and exact protected-prefix comparison.
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
rate value. Every line states `reference=PROGRAMMED_HALF_SLOT actual=0
bound=UNKNOWN`: a successful selection is not RF transmission confirmation.

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

Version 0.1.16 accepts an optional `provisional_path` object in the ignored,
factory-bound device input. Its absence selects zero. This is an operator
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

The worker freezes the effective offset with each publication, then adds it
once to the raw selected timestamp. Both raw and adjusted values must lie inside
the unchanged signed source envelope. It never alters raw physical captures,
the oscillator fit, source/proof/hold/precision gates, root envelope, slots or
parent rules. During bounded capture, `W12_G1_TX_PATH` accompanies the existing
sampled `W12_G1_TX_EST`, retaining publisher/root boots and epochs, train,
publication/event/channel, programmed tick, raw and emitted timestamp, effective
profile/offset and capture ID. Match the complete tuple and preserve missing
companions or queue losses; no tuple is an actual RF TX observation.

Report signed mean offset, slope and residual jitter separately. A constant
adjustment moves the mean; it does not reduce residual jitter, prove which
endpoint caused the bias or create an uncertainty bound. RF/UTC bounds and
absolute association remain unknown. GNSS reception/PPS/fix indications remain
separate observations under their existing gates; this profile sends no GNSS
commands and does not discipline the BLE clock from weak indoor GNSS.
