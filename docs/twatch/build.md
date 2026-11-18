# Build T-Watch Ultra

Copyright (c) 2026 Jonathan Yantis. MIT.

Read the [target guide](../../firmware/twatch/README.md) first. This is an offline
native port, using the same pinned ESP-IDF v5.5.5 and Xtensa toolchain as
[W12](../w12/build.md). The builder opens no hardware and marks output
`OFFLINE_NOT_FLASHABLE`. It uses the shared firmware retention lifecycle.

## Board boundary

The Ultra uses I2C SDA3/SCL2; AXP2101 at 0x34, XL9555 at 0x20 and CST9217 at
0x1a. Display QSPI uses CLK40, CS41, D0=38, D1=39, D2=42, D3=45 and RESET37.
The panel's visible columns start at 22. Radio SPI uses CLK35/MOSI34/MISO33,
CS36, RESET47, BUSY48 and DIO1=14. Radio SPI2 and display SPI3 are independent.
The expander uses pin 7 for display enable, pin 8 (P1.0) for touch reset and
pin 11 (P1.3) for the built-in radio antenna. This follows the vendor schematic
and board definition; the descriptive hardware table incorrectly lists touch
reset as pin 10, which is SD detection. GNSS uses receive-only UART1 at 38400 baud on MCU RX44; TX43 is not driven.
BHI260AP uses I2C 0x28, HIRQ8 and ALDO4 at 1.8 V; its Bosch RAM firmware
and selected driver are included with provenance. See [wrist wake](wrist-wake.md).
Native USB Serial/JTAG owns diagnostics. Shadow configures GPIO13 as input-only
PPS on MCPWM1 CH1, independently of RX17/CH0 and CURRENT/CH2. Its BLDO1 main
rail is enabled only for the awake visible face, with voltage/enable readback;
backup power and other rails are preserved. The PMU stays with the UI owner.
The main owner retires UART/capture; maintenance requires both retirement and
verified rail-off within the existing drain. This follows the
[LILYGO Ultra implementation](https://github.com/Xinyuan-LilyGO/LilyGoLib/blob/master/src/LilyGoWatchUltra.cpp);
physical routing, reception and power still require device checks.

W12 G1/RC7 and the N30 thermal estimator compile directly. Watch-owned source
selection and the selected LoRa observer adapter allow coarse time acquisition
without weakening backend authentication or precision gates. Selected main/radio/log adapters are recorded in
[imports.json](../../firmware/twatch/imports.json); W12/N30 firmware is unchanged.
The default clock uses 20 MHz units, but the descriptor-derived BLE receive
measurement still has 500 ns bins. Software fine-servo selection alone does not
improve that capture resolution. N30 GPIO4/5 fine capture and W12 calibration
must not be transplanted onto the watch’s NFC pins. Independent output bounds
remain unknown.

## Shared clock and PPS diagnostics

The [dual-radio guide](dual-radio-test.md) describes the parallel PPS export,
controller and exact calibration profile. `--dual-clock-profile` accepts only an
ignored profile bound to this device and fine-input profile. Omitting it keeps
shared-clock delivery disabled. Independent held-out BLE and LoRa offset
validation and candidate review are required before enabling delivery; GNSS
remains a witness. The builder records capability contracts and activation state
in its receipt. This does not alter installation authorization or USB guards.
Calibration bindings do not restrict authenticated root discovery. On an
unmatched source path the shared clock remains available, omits the extra path
correction and reports unknown calibration. Root roaming within the enrolled
trusted group keeps the same clock; another group/key requires trusted
enrollment. See the dual-radio guide for the distinction between live status
and frozen PPS observations.
Check the new owner paths against retained compiler stack receipts with:

```sh
python3 firmware/twatch/tools/dual_clock_memory_check.py \
  --build artifacts/watch-build-01 --out artifacts/watch-dual-memory-01
```

The separate [GNSS-free snapshot contract](gnss-free-snapshot.md) is available
through `clock read`. Its optional reviewed study profile can carry fixed
engineering priors onto unmatched paths while retaining unknown calibration.
Ordinary profiles default to matching-only extra corrections.

## Inputs and build

Use ignored owner-only device, enrollment and shared registry files. Device schema:

```json
{"board":"lilygo_twatch_ultra_v1","radio":"sx1262","factory_mac":"02:00:00:00:00:01","device_id":"0000020000000001","label":"Watch-test","legacy_wire_id":12}
```

This identity is synthetic, for offline compilation only. Actual onboarding
requires factory-MAC verification and a `twatch` row in the shared registry.
Do not infer radio type from the USB serial. An SX1280 cannot receive the current
922.4 MHz fallback. Enrollment uses the existing group/epoch/PSK schema; mesh
selection currently uses the shared receiver’s enrolled group/epoch filter.
Cross-group runtime selection is not implemented in this watch build; discovery
within the admitted trust context and fresh authentication are still required.

```sh
python firmware/twatch/tools/build.py --idf "$IDF_PATH" \
  --out artifacts/watch-build-01 \
  --enrollment artifacts/local/enrollment.json \
  --device-config artifacts/local/watch.json \
  --registry artifacts/local/registry.json
```

LoRa RX starts enabled and discovers compatible authenticated transmitters in the
enrolled trust group. No transmitter factory pin is required. The deprecated
`--lora-transmitter-factory` option is a compatibility hint only and does not
restrict reception; a zero value no longer disables the receiver. Four
non-evicting emitter replay floors survive radio OFF/ON until physical reboot.
An emitter change requires fresh metadata and timing pairs; old fit and quality
do not transfer. Run only one transmitter on the channel, as required by T096.
The candidate uses a 16 MiB flash configuration and 1,536,000-byte application
ceiling. This is a compiler layout, not evidence of the incoming factory layout.

Before installation: verify exact USB/factory identity, exclusive ownership,
security state, actual app partition and active OTA selection, compatible recovery,
original bounded drain where supported, and selected source/build review. Use
app-only write bounds, actual new-application readback and protected-prefix/tail
comparison. No whole-flash backup is part of ordinary updates. Do not erase
factory partitions to make this candidate fit. Native success is not permission
for hardware deployment or a precision qualification.

Host rendering needs a C++17 compiler. Font regeneration additionally requires
Pillow and the OFL Rajdhani SemiBold font; the generated atlas and font license
are included. Preview values are synthetic and must not be shown as observations.

Run all offline watch regressions, including UI, radio owners, power, Shadow PPS,
console capacity and capture parsers, with the pinned public SDK cache:

```sh
python3 firmware/twatch/tools/check.py --idf "$IDF_PATH" --out artifacts/watch-check-01
```

Each check writes its own log; `receipt.json` reports the complete result. Use
`--list` to inspect the checks without running them. Native builds and retained
compiler stack audits remain separate. For UI checks and previews alone:

```sh
python3 firmware/twatch/tools/test.py --out artifacts/watch-host-01
```

This produces 99 actual-renderer PPM previews and a host-check receipt.
The tests cover navigation, settings bounds, first-touch wake consumption,
expired timing, framebuffer guards and all screen destinations. Solar checks
cover fixed Central time independent of the mesh display zone, all eight visible
sunrise/sunset and twilight crossings, opposite-sun clock geometry, bounded
12px/s acquisition motion, position retention when astronomy becomes unknown,
and cached/full-frame damage equivalence for the moving clock. Geometry checks
cover all eight event/chord alignments and band ordering across 365 days,
solstices/equinoxes, per-event DST offsets and a wrong-offset reuse guard, fixed
civil cardinal positions, antialias-aware case/main-circle clearances and the
conservative 100px rounded aperture. These remain offline software checks.

Battery checks cover panel-state integration, missing/invalid gauge reads,
24-hour retention and ring wrap, boot reset, scale navigation, USB manual sleep
and PMU-key acknowledgment/failure handling. Nine Battery fixtures cover all
four scales, empty/unavailable history, low battery, USB power and gauge gaps.

The focused timing checks also run offline, without enrollment or a native
builder:

```sh
python3 firmware/twatch/tools/servo_tests.py --out artifacts/watch-servo-01
python3 firmware/twatch/tools/holdover_tests.py --out artifacts/watch-holdover-01
```

The first checks the shared N30 dense software servo and a legacy negative
control. The second checks future-pair sampled P99, warmup, missing witnesses,
source/RX-epoch resets, queue overflow and propagation to the watch model.
The bounded pair queue keeps floating-point observer work out of the BLE
callback. `dual read 0 0` exposes `p99_*` counters, scope, queue loss, maximum
main-task work and the current waiting reason in `TWATCH_SOURCE`. These checks
establish software behavior; synthetic nanosecond values are not hardware
accuracy evidence.

## Shadow receiver and statistics

The fourth face opens the u-blox MIA-M10Q receive stream without sending receiver
configuration or time aid. The bounded NMEA parser checks checksums, freshness,
coherent valid RMC and GGA fix state. GSV multipart reception commits only when
complete; missing or malformed elevation/azimuth retains signal strength but
never becomes a plotted position. The visible bars show up to twelve strongest
received signals; the retained sky population is bounded to sixteen.

A bounded physical PPS queue remains separate from radio packet association,
receiver training and clock authority. The fine profile projects the capture
into its existing 20 MHz coordinate; the default GPTimer profile uses a sampled
cross-counter enclosure and reports it. Neither establishes calibrated accuracy.
BLE witness maps preserve publication availability, scope and original expiry;
racing, missing, expired or unreferenced edges do not enter statistics. Collection
also requires the actual selected clock to be fresh BLE/RC1/RC2. LoRa, RC9,
local HOLD or selection expiry hide the results immediately.

Each visit has a fresh phase-blind ten-second settling interval. A reference,
source/scope or capture-incarnation change, sequence loss or PPS gap over 1.5 s
restarts it. The startup boundary is excluded; subsequent usable edges supply
signed MEAN, RMS and interpolated P99 magnitude. Histogram bins are exact through
1023 ns; wider power-of-two tails carry `~`, matching the T096 UI convention.
Short permission gaps on the same selected path pause its population, with no
permission renewal. Ineligible PPS edges increment skipped coverage and cannot
enter the sums. A newly eligible physical edge is required before the retained
same-context population becomes current again; LoRa/RC9/None selection, actual
clock-context changes, GNSS reference loss and PPS discontinuities clear it.
No phase-dependent rejection or runtime GNSS correction is applied. Missing/skipped
counts remain in diagnostics. The histogram/parser/witness workspace is allocated
only in PSRAM, without an internal-RAM fallback or flash writes.

The existing `TWATCH_UI` row appends compact `shadow_version=1` power/owner,
stage, fix, pulse and validity fields. Authenticated `shadow diag` returns a
separate five-row response with `TWATCH_SHADOW` reception, settling, population,
missing/skipped and fault details. Full normal status retains its fixed frame
budget; GNSS detail cannot displace it. CRC framing stays fixed.
Authenticated `ui face 0` through `ui face 3` select a face through the UI owner.
`ui sleep` requests manual panel sleep; `ui page 0` wakes the face. These commands
queue work to the display owner and are refused during maintenance.
Fine diagnostics expose CH1's GNSS auxiliary contract; leave Shadow before a
finite fine-capture command. Partial starts and cleanup failures remain owned
until their stop path proves retirement. `prepare_update` cannot acknowledge a
live GNSS rail or capture owner.

```sh
python3 firmware/twatch/tools/test.py --out artifacts/watch-shadow-host-01
python3 firmware/twatch/tools/fine_tests.py --out artifacts/watch-shadow-owner-01
```

These exercise synthetic reception, metrics, source withdrawal, the actual
capture owner and all four faces. Native compilation and preview review are
offline preparation; they do not establish installed reception, bias or energy.

The actual-controller regression additionally exercises PMU acknowledgment
preemption, partial UART/PPS starts, retained first cleanup failures, original
owner retirement and a new ten-second population after source/face changes:

```sh
python3 firmware/twatch/tools/shadow_tests.py --out artifacts/watch-shadow-controller-01
python3 firmware/twatch/tools/energy_tests.py --out artifacts/watch-control-01
```

The second command compiles the real output worker, extracts the production
common-row printf templates, expands every numeric field to its full type width
and bounds strings conservatively at 64 bytes. It checks total frame capacity,
fragmented strict parser replay, CRC rejection and the compact maintenance
handshake. Synthetic SDK/USB tests do not establish physical USB or GNSS behavior.

## USB ROM entry

The watch keeps the ESP32-S3 native USB Serial/JTAG console active, including
while its screen sleeps. Use the Console V3 CRC parser in
`firmware/twatch/tools/framed_console.py` with the exact installed build identity.
Builds declaring `BOUNDED_CONTROL_V1` provide read-only `v12 control NONCE`, with
a fixed control field set in the same ten-row V3 envelope. All mutation
acknowledgments and refusals use that bounded form. Detailed parent, fit,
satellite and battery-history populations cannot grow this path. The normal
verbose status remains available for inspection; the recovery helper selects
compact control from the exact build receipt. A fresh complete control response
supplies a single-use ticket bound to factory MAC and boot session. Both CRCs,
original response deadline and current-request coverage are still required. Send `prepare_update` using that ticket, then poll fresh status
until `application_drained=1` (`update_stage=2`). This stops BLE, LoRa, capture
and the display/settings worker within the original two-second deadline.
Images declaring `CONTROLLER_OFF_V1` additionally require host exit and actual
controller deinitialization: `ble_controller=0`, `ble_power_stage=6`,
`ble_power_off_ack=1` and `host_ready=0`. Older activity-only images retain their
original initialized-host checks. The helper can resume an already successful
READY drain, using its original completion/deadline and a fresh command ticket;
it never retries or renews a faulted drain.

For `BOUNDED_CONTROL_V1`, the host additionally requires direct display and fine
retirement, no fine cleanup fault, and actual GNSS rail/UART/capture retirement
with no pending power transition or cleanup fault. A displayed OFF snapshot does
not substitute for these physical owner proofs.

`FAULT_CLEANUP_OFF_V2` additionally supplies current-generation physical BLE and
LoRa retirement proofs. It permits retained operating-error history only when
controller/host/NPL/capture retirement and the original cleanup deadline are
proven; any cleanup error, missing proof or stale generation still refuses the
update. A BLE Init/Sync failure receives one bounded cleanup attempt. Once
physically OFF, the 90-second supervisor may restart the radio with a fresh
admission ID. Stop/deinit failures never receive a renewed cleanup lease.
The host-tail callback closes producer admission; the worker then waits for
in-flight posters and readers to retire before SDK teardown. This separation
allows the higher-priority host callback to preempt its own posting worker
without mistaking that post for an unsafe external owner. The original deadline
still applies. The running timing counter and learned rate are retained through
radio recovery.
The first-fault flash record remains available after successful recovery.

With a new ticket, send `bootrom`. The firmware waits for the complete response
and USB transmit drain, sets the S3 force-download flag and restarts into ROM.
The acknowledgment has a separate 1.5-second deadline; expiry cancels the reset
and cannot extend the maintenance drain. Failed drains and stale/wrong-session
commands cannot enter ROM. No flash or eFuse is written by this command.

Watch host tools prefix each status or mutation with NUL followed by LF. NUL
invalidates an unfinished input line; LF returns the parser to a clean boundary.
This recovers from binary ROM probes, oversized input and interrupted serial
commands without executing a pending partial mutation. A bare newline is not a
safe substitute. Fresh nonce, session, ticket and original deadline checks still
apply; this does not authorize a radio restart or bypass a failed drain.

This path requires a responsive application and USB connection. For a crashed
application, retain the physical recovery path: hold the lower-left BOOT button,
press/release the recessed right-side RESET button between USB-C and the SD
slot, then release BOOT. The upper-left button is power, not reset. See
[LILYGO's download-mode instructions](https://github.com/Xinyuan-LilyGO/LilyGoLib/blob/master/docs/lilygo-t-watch-ultra.md#entering-download-mode-on-the-t-watch-ultra).

With `pyserial` and `esptool==4.12.0` installed, the bounded host tool performs
this exchange and verifies the ROM factory identity:

```sh
python firmware/twatch/tools/usb_bootrom.py enter \
  --factory VERIFIED_FACTORY_MAC --receipt artifacts/INSTALLED_BUILD/receipt.json \
  --out artifacts/watch-rom-entry-01
python firmware/twatch/tools/usb_bootrom.py run \
  --factory VERIFIED_FACTORY_MAC --receipt artifacts/INSTALLED_BUILD/receipt.json \
  --out artifacts/watch-rom-return-01
```

`run` clears the volatile force-download bit and uses Espressif's S3 watchdog
reset to exit ROM; verify fresh application status afterward. Neither command
writes flash. The tool requires exclusive native USB ownership and fresh ignored
output directories. Its diagnostic port suppresses DTR/RTS transitions so simply
opening the console does not reset the watch. Physical recovery remains available.

## Mobile reception and gesture checks

Run the watch-specific actual-codec regression harness offline:

```sh
python3 firmware/twatch/tools/radio_tests.py --idf artifacts/public-review/esp-idf \
  --out artifacts/twatch-radio-check
```

For an explicitly authorized, connected watch, `mobile_capture.py` records finite
CRC-framed status, both radio paths, display timing and IMU events. It requires
pyserial, an unlocked macOS desktop, exclusive USB ownership, exact factory MAC
and the installed build receipt. Logs remain owner-only in ignored artifacts.

```sh
python3 firmware/twatch/tools/mobile_capture.py --factory "$WATCH_FACTORY" \
  --receipt "$WATCH_BUILD/receipt.json" --out artifacts/twatch-mobile-check \
  --seconds 180
```

The default capture is read-only. `--relay "$RELAY_DEVICE_ID"` explicitly selects
that authenticated relay on the watch after 30 seconds, then requests AUTO for
the final 60 seconds. It requires an initially unpinned AUTO watch and attempts
restoration on failure. It does not configure the relay itself. Inspect both the
command acknowledgement and following/root/path diagnostics; an accepted parent
request alone does not prove relay reception. A missing relay remains a failed
availability observation. The tool never flashes or resets a device.

For an explicitly authorized RC1 → direct BLE → RC1 comparison, combine
`--relay VERIFIED_RELAY_ID --bidirectional --radio-only --fine-monitor --seconds 360`.
The watch must initially follow a direct GM in AUTO. The harness derives that
GM's identity from the fresh authenticated snapshot, requires the relay's fresh
admission to preserve the same root/boot/source, and allows at most45s per
admission followed by60s of observation. It restores AUTO and verifies completed
owner rebind, including on failure. A source that is not currently advertising
cannot complete its leg. The summary retains sampled source transitions,
admission latency, held/time-missing observations, backward steps, servo and P99
diagnostics, with unknown RMS retained explicitly. These are firmware observations,
not independent UTC/output accuracy. Run the policy/report guard offline with
`python3 firmware/twatch/tests/roaming_capture_test.py`.

For cold LoRa admission, allow at least one six-minute metadata cycle plus a
30-second timing beat; a short capture can see RF packets without usable time.
Check source continuity, actual LoRa packets and fitted/predicted error separately.
The harness reports monotonic displayed UTC and render/transfer times; it cannot
measure independent UTC accuracy or diagnose visible panel tearing without a
physical observation. Use [wrist-wake checks](wrist-wake.md#validation-limits) for
motion recognition and its display duty cycle.

Native USB Serial/JTAG boot-ROM reset is also a recovery path when an application
fault prevents the healthy software-entry check from passing. Retain the failure
and completed original-drain evidence; verify the exact ROM factory identity
before any app-only write. A reset is not evidence that a failed receiver became
healthy, and it must not silently restart or extend a maintenance drain.

For an explicitly authorized exact-watch field-control check, add `--radio-cycle` to `mobile_capture.py` with `--seconds 120` or longer. It exercises the same Settings action queue over fresh Console V3 tickets, verifies each owner OFF and ON, and restores both ON. It cannot be combined with the relay selection test. This checks receiver lifecycle, not supply current or RF silence.

The finite capture also writes `receiver.json`: full BLE owner/admission/train
snapshot transitions, RX association counter changes, selected-source changes and
P99 readiness/reason transitions when the installed build supplies them.
Compact diagnostic rows are excluded from BLE lifecycle comparisons. Counter
resets are reported rather than subtracted, and mixed boot sessions are refused.
This distinguishes an owner stuck OFF from repeated parent/train changes without
printing in radio callbacks. Intermediate events may be missed; a snapshot does
not establish an RF cause. The strict discipline refusal counter is not the
corrected G1 model's acceptance count.

For an existing private capture, generate the same report offline:

```sh
python3 firmware/twatch/tools/receiver_report.py \
  --frames artifacts/watch-mobile-check/frames.jsonl \
  --out artifacts/watch-mobile-check/receiver-review.json
```

After a BLE-toggle instability report, first capture the unchanged state, then
use a separately identified finite `--radio-cycle` run to compare lifecycle,
parent identity, RX epoch, matched/missed/ambiguous deltas, source continuity and
holdover readiness. Keep failed windows and operator observations. A successful
restart and a working display do not establish timing-train stability.

Use `--radio-only` when IMU testing is intentionally deferred. This requests no motion snapshots and skips IMU acceptance checks; the summary explicitly records `imu_evaluated: false`. Receiver, display, identity, CRC, ownership and response-deadline guards remain enforced. It does not restart the IMU or change either radio.

For a BLE-specific recovery comparison, `--ble-cycle --radio-only --seconds 180` verifies both radios initially ON, disables BLE at 30 seconds, restores BLE at 40 seconds, and keeps LoRa ON throughout. It uses the existing Settings action queue and owner retirement checks. It cannot be combined with relay selection or the full radio cycle. The remaining window records reacquisition and source/holdover transitions; a passing owner lifecycle alone is not servo qualification.

For an isolated display-load comparison, `--wake-face --radio-only --seconds 120` wakes the current face once after ten seconds using the console page action. It leaves radio selection and settings unchanged, records the actual awake/asleep snapshots, and cannot be combined with radio cycles, relay selection or IMU diagnostics. The saved normal sleep timeout still applies; use the recorded state rather than assuming a ninety-second timeout.


For descriptor-resolution inspection, `--timing --radio-only --seconds 60`
starts a finite, every-fourth-event observer and retains original stamp, bridge
and causal-pair fields. It stops the observer explicitly. A 20 MHz timestamp
unit does not establish 50 ns physical capture resolution.

The optional `--fine-rx-shadow-profile` build input reserves the unused microphone
CLK GPIO17 for RX-only RW238 capture through MCPWM1. It requires an ignored,
exact-factory profile and a separately reviewed electrical-route record. The
profile binds the board, SX1262 variant, schematic hash and review hash. TX is
not routed; GPIO18 remains the microphone DATA output, and LoRa keeps MCPWM0.
The default build drives no fine-capture pad. This shadow profile cannot steer
the clock or import N30’s phase translation.

The separate `--fine-rx-profile` input selects a continuous RX17 relative G1
backend, mutually exclusive with `--fine-rx-shadow-profile`. Its exact-factory schema uses mode `TWATCH_ULTRA_RX17_RELATIVE_FINE`,
the same electrical-review fields, and explicit `phase80`,
`phase_evidence_sha256` and
`endpoint_class: {"phy": 0, "pdu_length": 191, "ext_header": 0, "extra_us": 4}`.
The phase must come from a retained watch training/held-out association analysis,
not the N30 calibration. Only matched captures with that endpoint class and a
valid, applied radio reference enter the fine fit. Other packets supply no fine
sample; authenticated coarse permission and separate LoRa tracking keep their
existing rules. The hardware falling capture alone supplies phase; the
descriptor locates its association interval. Delivery rounds to 50 ns units.
Neither the raw timer resolution nor this fixed coordinate convention proves an
RF endpoint, UTC bound, or output accuracy.

The continuous backend starts before BLE, excludes finite raw/window commands,
and releases its RX route on containment or maintenance. `--fine-monitor --timing --radio-only` reads its counters and original-versus-delivered timing
records without starting or stopping that owner. A production trial must verify
actual fine substitution, causal RMS, P99 readiness, source continuity and radio
restart behavior; a native build or sparse association cohort does not do so.

On an explicitly authorized, verified shadow image, `--fine-shadow --radio-only`
records raw edges. `--fine-windows --radio-only` instead records a bounded
64-packet cohort, complete packet/descriptor/bridge/pair metadata and every
retained edge before acknowledging each window. Both modes accept at most
600 seconds, exclude radio toggles and IMU tests, and verify capture release
on exit. Window failures, missing pairs, overwritten history and late callbacks
remain in the evidence. Raw capture and packet association are separate from
servo performance and independently measured output accuracy.


For a separate relay-class association cohort, combine `--fine-windows` with
`--relay VERIFIED_RELAY_ID --radio-only --seconds 180`. The harness requires
initial AUTO selection and both radios ON, verifies the requested peer is
an authenticated following RC7 relay within 45 seconds, then starts the finite
window recorder. It stops and verifies recorder release before restoring AUTO,
and also attempts AUTO restoration on failure. Requested, received and missing
cohort records stay distinct; do not pool different parent/capture incarnations
when deriving a phase convention.


For an installed, separately authorized relative-fine image,
`--fine-monitor --timing --radio-only --seconds 180` reads the continuous fine
owner while recording original and delivered timing stamps. It does not start
or stop the production counter; it only starts and stops the finite timing
observer. The parser checks the reported phase against the installed receipt.
Use `--fine-monitor --ble-cycle --radio-only --seconds 180` for a separate
BLE OFF/ON comparison with LoRa left ON. Record actual RMS, holdover readiness,
association failures and source transitions; neither a build nor a successful
owner restart establishes sub-100 ns operation.

### Focused BLE roaming recovery check

```sh
python3 firmware/twatch/tools/roaming_tests.py --idf "$IDF_PATH" --out artifacts/watch-roaming-01
```

This runs the watch-specific loss timer and actual shared receiver owner against
synthetic controller APIs. It checks loss without fresh discovery, bounded retries,
OFF/busy/fault suppression, original cleanup deadlines, and a new GM entering fresh
admission after the old PA handle drains. Actual GR3 and authenticated RC7 codec
cases exercise first-pair rate reuse after more than thirty seconds, both relay
depths, incompatible GM phase changes, missing/authentication/replay guards and
untrained successor chains. Native owner cases keep the trained oscillator
through parent rebind and BLE OFF/ON; coarse admission alone remains HOLD.
These are offline software checks, not field roaming or precision acceptance.

### Radio fault recovery and retained evidence

The watch-specific supervisor never restarts the MCU for radio faults or loss of
source time. Each enabled radio receives a recovery request after 90 continuous
seconds of fault, with subsequent requests no more often than every 90 seconds.
Explicit OFF, power-off and maintenance suppress recovery. The existing radio
owner must prove its original bounded shutdown completed before a fresh radio
generation can start. A failed cleanup, exhausted generation or unresponsive
owner remains contained; a watchdog request is not proof of recovery. A successful
late callback from a stale BLE capture epoch can expose an untracked periodic
radio handle. This sets a persistent-in-boot ownership guard (recovery reason 9);
no radio restart is admitted while retirement of that handle is unproven. Its
handle, epoch and observation time are retained in the flash fault record.

BLE remains active while LoRa supplies the face. Retained BLE estimates and
fresh presentation snapshots cannot renew receive availability. A fresh usable
LoRa candidate takes over after 30 seconds without BLE receive progress, after
three observed synchronization/transport failures without recent real BLE progress
within 60 seconds, or sooner
when BLE authority expires (after the 500 ms glitch guard). BLE takes priority
back after 30 seconds of qualified healthy reception. Counter increases caused
by an expiry repaired with real receive/pair progress within 500 ms do not reset
that qualification or force a fallback. A retained estimate or refreshed display
snapshot cannot count as progress. Individual gaps shorter than 500 ms pause
the qualification clock; their total budget is 1.5 seconds per qualification
window. Longer/frequent gaps or a changed authenticated context restart it. Missing LoRa cannot
supply time; the existing display-only held clock continues independently.

`TWATCH_SOURCE` also exposes the source selector's reasoning: `ble_return_block`
has bits 0–4 for unusable candidate, missing fresh authority, no recent RF
progress, incomplete return qualification, and an unrepaired failure burst.
`ble_return_resets` counts qualification restarts; `ble_return_reason` retains
1=unrepaired failure, 2=context change, 3=gap at least 500 ms, 4=exhausted gap
budget. `ble_switch_reason` retains 1=initial selection, 2=BLE selected,
3=failure burst, 4=stalled progress, 5=lost fresh BLE, 6=prior source unavailable,
7=other source. Raw model expiry/window miss/setup counts, ignored repaired
failure counts, accumulated gap time and actual progress age distinguish RF
loss from a repaired permission interval. These fields are generated on USB
request from RAM; they add no continuous flash writes.

`dual read 0 0` exposes `ble_fault_*` and `lor_fault_*` fields in the existing
`TWATCH_SOURCE` row, with retained reason codes for each
radio: episode cause, API/return code and timestamp, callback/capture epochs, PA
handle/SID/interval/address match, owner drain flags/deadlines and parent/root.
BLE cause codes are 1=API error, 2=periodic callback epoch mismatch,
3=periodic context mismatch, 4=capture adoption failure, 5=unknown fault,
6=watch BLE power lifecycle failure. For cause 6, stage/site and return code
identify the latest operating or cleanup failure. The Sync substage/site is
meaningful only when the failed stage is Sync (8); for Init or cleanup failures,
use the failed stage and return code. Boot-first fields preserve the
first lifecycle failure. Its owner bits 0–5 mean requested, host running, worker
running, OFF acknowledged, cleanup active and operating fault; bits 8–15 carry
the controller state.
Boot-first and latest API history are separate context, never silently reused
as the cause of a later episode. A cause code identifies a firmware refusal,
not an RF explanation. `*_build` is FNV-1a of the firmware build ID; `*_boot` separates sessions.
Events are 1=fault observed, 2=recovery queued, 3=request refused, 4=fault cleared,
5=main/host heartbeat stalled, 6=BLE owner rearmed, 7=BLE worker refused recovery.
Rearmed means a fresh owner activation was admitted, not authenticated timing. Clearing a fault does not erase its displayed
reason. `ble_recovery` separately reports attempts/rearmed/refused/pending/reason.
Recovery reasons are 1=queued, 2=rearmed, 3=original drain not proven,
4=owner not quiet, 5=other radio owner busy, 6=new fault/generation,
7=admission refused, 8=cancelled, 9=unretired periodic owner. BLE `*_owner`
bits 0–6 mean deadline exceeded, OFF acknowledged, owner busy, stopping,
unretired periodic owner, explicit OFF and G1 fault latch. LoRa bits 0–4 mean OFF acknowledged,
SPI retired, capture released, reset asserted and cleanup fault. Callback
SID/address fields are observations only for the applicable callback cause;
zero/false on an API or unknown cause must not be inferred as a mismatch.

Use `dual read 1 0` through `dual read 8 0` to read each committed ring slot;
`rf_slot_valid=0` means empty/unavailable. Reads use RAM snapshots of committed
records and cause no flash access. Slot order is physical, not chronological;
compare record sequence numbers. The normal `dual read 0 0` shows latest reasons.
`dual read 9 0` and `dual read 10 0` expose the separately committed first BLE
and LoRa fault. These two records survive ring wrap, radio recovery and reboot;
they are never overwritten by later faults. A record is available only after
a fault has been committed by an image supporting these dedicated slots.

An asynchronous eight-slot NVS ring stores fault and attempt records, without
keys or packet contents, plus one immutable first-fault slot per radio. Writes
occur on events, not on every status poll. Identical watchdog refusal events
are deduplicated within an episode. The worker sleeps until an
event or its one-second heartbeat check; USB detail is generated only on request.
Maintenance stops new writes and includes an in-flight commit in the original
application drain. NVS
errors and queue drops remain visible; storage is never erased to fix an error.
A power loss before the worker commits can lose a pending event. Flash writes
and radio recovery still require field timing/continuity validation. No reboot
watchdog can recover a permanently stalled owner without violating this watch's
time-retention policy.

Offline timer, persistence and failure tests:

```sh
python3 firmware/twatch/tools/watchdog_tests.py --out artifacts/watch-watchdog-check
python3 firmware/twatch/tools/roaming_tests.py --idf "$IDF_PATH" --out artifacts/watch-ble-owner-check
python3 firmware/twatch/tools/radio_owner_tests.py --out artifacts/watch-lora-owner-check
```

These use the actual journal implementation with a simulated NVS store and test
independent 90-second timers, OFF suppression, reload, ring bounds, write failures
and absence of a no-time reboot. Native-owner fault recovery tests additionally
exercise bounded retirement and fresh admission; no host test establishes field
roaming reliability or independent timing accuracy.

### User-button power-off

The lower-left GPIO0 button accepts two short clicks (each at most 600 ms,
release-to-release at most 450 ms), with 40 ms stable-edge debounce. GPIO sampling runs every 5 ms on the existing
ESP timer service, independently of display rendering and I2C waits; its callback
only updates the gesture state and bounded mailboxes. A single
click retains wake/launcher behavior after the double-click window. Startup-held
BOOT is ignored until released. Shutdown uses the original two-second owner
drain and waits for button release; it does not bypass drain faults or reset the MCU.
After the display/I2C owner exits, the main task sets AXP2101 COMMON_CONFIG
(0x10) bit 0, following [XPowers shutdown](https://github.com/lewisxhe/XPowersLib/blob/master/src/XPowersAXP2101.hpp).
Hold the upper-left PMU POWER button for about one second, then release, to
power on, as described in the [Ultra hardware guide](https://wiki.lilygo.cc/products/t-watch-series/t-watch-ultra/).
A short tap or the lower BOOT button does not power on from PMU shutdown.
Physical power-on remains a hardware check; host tests cover gesture and drain guards.

USB `TWATCH_UI` adds `key_edges`, `key_single`, `key_double`, `key_gap_us`,
`power_stage` and `power_error`. Power stages: 0 idle, 1 double-click detected,
2 draining, 3 request refused, 4 drain fault, 5 PMU command, 6 PMU returned.
These are RAM counters with no periodic flash writes. A surviving USB session
can distinguish a missed click from guarded shutdown refusal or PMU failure.

### Upper-left display sleep key

The UI owner polls the AXP2101 short-press latch with an 80 ms minimum interval,
including while the panel sleeps. Slow frames can defer polling; the PMU retains
the event. It enables only register 0x41 bit 3 and acknowledges only
register 0x49 bit 3, following the [XPowers AXP2101 implementation](https://github.com/lewisxhe/XPowersLib/blob/master/src/XPowersAXP2101.hpp).
Startup discards the old short-press latch. Unacknowledged reads never synthesize
a press; other IRQ status and long-press/recovery settings are preserved.

A short click sleeps the panel immediately or wakes the current screen. Explicit
sleep overrides USB keep-awake. Touch and the lower-left button also wake without
navigating; wrist raise is suppressed for 1.5 seconds after manual sleep. USB
`TWATCH_UI` exposes `pmu_key` and `pmu_key_errors`; wake source 4 is this PMU key.
Physical key latency, wrist behavior and endurance still require hardware checks.

### Battery history

The display owner samples AXP2101 percentage every 30 seconds independently of
rendering or panel sleep. A PSRAM-only ring retains 1,441 minute records: the
active minute and 24 hours of completed minutes. Allocation failure leaves the
app usable with `History unavailable`; it does not consume internal radio/DMA RAM.
There are no periodic flash writes. Reboot clears history and restores the 1h
chart scale. After 24 hours, the chart shows the latest selected period.

Each minute retains its most recent gauge sample. Invalid reads remain unknown;
columns never interpolate across missing gauge intervals. Display-on microseconds
are integrated from successful panel hide/reveal transitions, including retained
panel state during a slow render. Each chart has 30 aligned segments: 2/12/24/48
minutes at the 1/6/12/24-hour scales. Battery columns show the last valid sampled
percentage in a segment, and display bars show accumulated on-time against that
segment's full duration. Before-boot intervals remain empty. A selected window
edge inside a minute prorates that minute's display time; totals have minute
resolution rather than independent subsecond measurement accuracy. USB power is
shown as power presence, without claiming battery charging current or discharge
rate. This is gauge and panel-use history, not energy or lifetime qualification.

RAM-only `TWATCH_UI` Battery fields report `app` (4 = Battery), `battery_history`
(allocation available), `battery_pct`, `battery_samples`, `battery_sample_us`,
`battery_at_us`, `battery_hours`, `battery_columns`, `battery_awake_us` and
`battery_observed_us`. History counters are refreshed at 1 Hz, including during
panel sleep. `ui page 4` opens Battery through the existing authenticated
display-only page command; scale changes use the touchscreen. These fields let
a finite capture distinguish gauge sampling from display-on accumulation.

Large presentation models, ephemerides, damage bookkeeping and the current
model live in a bounded PSRAM-only UI workspace. The Battery ring contains its
own reusable chart summary in PSRAM; rendering and diagnostics share this
single-owner scratch instead of returning a chart-sized value on the task
stack. Each of 30 columns uses bounded 32-bit microsecond counters (maximum
48 minutes per column), with 64-bit period totals. Static size assertions bound
both allocations. `ui_psram_bytes`, `battery_psram_bytes` and
`ui_stack_min_bytes` expose allocation sizes and the observed UI stack low-water
mark. Allocation failure never borrows internal DMA/radio memory. Native stack
receipts and host fixtures are preparation; installed stability remains a
separate check.

Check the retained native compiler stack receipts before a Battery hardware run:

```sh
python3 firmware/twatch/tools/ui_memory_check.py \
  --build artifacts/watch-build-01 --out artifacts/watch-ui-memory-01
```

The direct-call budget includes the worker, render path and summary, reserving
1,536 bytes of the unchanged 6,144-byte UI stack for helpers/library work. It
does not measure actual high-water or prove a particular crash cause; compare
`ui_stack_min_bytes` during the separately authorized installed run.

### Touch capture during rendering

A separate CST9217 owner samples at 10 ms while awake and 40 ms while asleep,
using the ESP-IDF serialized I2C master bus. It compresses each gesture into its
first/last coordinates and captures up to eight completed gestures while the UI
draws. Screen-context changes and stale gestures are discarded; wake consumes
the first gesture. Bus failure does not synthesize a tap. Shutdown waits for both
the UI and touch owners within the original drain deadline. USB status reports
`touch_samples`, `touch_errors`, `touch_drops`, `touch_gap_us`, `touch_stack`,
`touch_strokes` and `touch_actions`; these counters are RAM-only.

Touch wake is signalled on initial contact and notifies the UI task immediately;
it does not wait for release or the idle polling interval. The context changes
on wake, consuming the rest of that gesture. A fresh clock/data frame is still
flushed before the panel is revealed. RAM-only `wake_us`, `wake_render_us`,
`wake_flush_us`, and `wake_source` diagnostics record the last successful wake
(source 0 startup/other, 1 touch, 2 recognized button click, 3 wrist raise).
`wake_us` starts at software detection, not an independently measured physical
contact; it excludes sensor/controller detection latency.
