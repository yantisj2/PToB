# Build and onboard N30 RC offline

Copyright (c) 2026 Jonathan Yantis. MIT.

Read [the target README](../../firmware/n30/README.md) for capabilities and
unverified hardware assumptions. These commands build or inspect files; they do
not connect, flash, start a serial reader or operate a device.

## Public dependencies

Use the same pinned ESP-IDF v5.5.5 and Xtensa GCC14.2.0 release as
[W12](../w12/build.md). The N30 builder verifies the public SDK commit, controller,
GPTimer ABI and compiler banner. No private sibling checkout is needed.

```sh
python3 firmware/n30/tools/dependencies.py --dest artifacts/n30-deps/esp-idf
export IDF_TOOLS_PATH="$PWD/artifacts/esp-tools"
artifacts/n30-deps/esp-idf/install.sh esp32s3
. artifacts/n30-deps/esp-idf/export.sh
```

A verified existing public SDK cache can be supplied with `--idf`. Component
manager downloads are disabled. ESP-IDF and linked component notices retain
upstream terms. macOS is the current build host; clean-OS/other-host validation
is pending.

## Offline previews and tests

These require a C++20 host compiler and Python, without enrollment or ESP-IDF:

```sh
python3 firmware/n30/tools/preview.py --out artifacts/n30-preview --alias N3-1
python3 firmware/n30/tools/tests.py --out artifacts/n30-tests
python3 registry/test_registry.py
```

Use fresh output directories. `--alias N3-2` generates the other reserved name.
Previews include all four pages, separate dual RF clocks and LoRa OFF/ON/fallback/fault states, BLE/RC1/RC2 savers at all three moving positions,
minimal header-only PPS-on/off frames at all three positions, and
relay-only/pending main-page examples. They are native64×128 PNGs from the actual firmware renderer. Preview receipts
record source/fixture hashes and synthetic scope. The tests cover both8192-pixel
rotations, bounds, unavailable/fault/expired states, input/wake/idle behavior,
activity scope reset, no-GPS inputs and shared-map collision rejection.

## Onboard the arriving clocks

Do this only after the hardware arrival process supplies each verified factory
MAC. Do not substitute advertised BLE addresses or select a unit by alias.
Keep the existing shared map in an ignored owner-only local path. If creating a
new group, first follow [group setup](../GETTING_STARTED.md); do not replace an
existing deployment registry with the synthetic example.

```sh
python3 firmware/n30/tools/onboard.py --alias N3-1 \
  --factory-mac "$N31_FACTORY_MAC" --rotation cw \
  --registry artifacts/local/registry.json --out artifacts/local/n3-1-onboarding
python3 firmware/n30/tools/onboard.py --alias N3-2 \
  --factory-mac "$N32_FACTORY_MAC" --rotation cw \
  --registry artifacts/local/registry.json --out artifacts/local/n3-2-onboarding
```

The tool performs no device read: these environment variables must come from
independently verified factory identity. Actual MACs belong only in ignored local device/registry records.
Each transaction validates the whole map, preserves existing rows/UUIDs, refuses
reassignment, writes `device.json` and a receipt, and atomically replaces the map.
`ccw` selects the opposite mounted orientation. Confirmation of orientation is a
hardware gate, not inferred from the product photograph. Share the same final
map with T096, W12, SS3 and macOS builds. Existing images are not changed by a map
edit. The registry grants no membership, timing authority or installation consent.

## Native build

Supply the existing group's ignored enrollment; see [PSK setup](../security/psk.md).
There is no N30 GNSS input/profile option. There are no arbitrary pin overrides.

```sh
python firmware/n30/tools/build.py --idf "$IDF_PATH" \
  --out artifacts/n30-n3-1-offline --profile RC --listening-percent 100 \
  --enrollment artifacts/local/enrollment.json \
  --device-config artifacts/local/n3-1-onboarding/device.json \
  --registry artifacts/local/registry.json --development
python firmware/n30/tools/inspect_build.py artifacts/n30-n3-1-offline
```

Repeat with N3-2's device file and fresh output directory. BLE50 accepts
`--listening-percent 50` and is client-only; `--estimated-relay` requires100.
Development receipts identify dirty sources. Default N30 builds are
`OFFLINE_NOT_FLASHABLE`, even from a clean commit. `--commissioning` produces a
`COMMISSIONING_CANDIDATE_PHYSICAL_UNKNOWN` only from clean committed sources; it
never authorizes hardware itself. A separate exact-device review must bind the
operator scope, physical identity/security/flash, selected factory app slot,
ROM recovery, protected-prefix comparison, app-only bounds and actual readback. Outputs contain enrollment
keys and remain private/ignored. Inspection verifies identity/configuration;
it cannot promote an offline build to deployment approval.

The application is bounded to the existing1MiB slot starting at0x10000 with a
64KiB protected prefix, on an8MiB flash configuration. Those are the proposed
application layout, not a statement about incoming factory partitions. No first
installation is permitted on the assumption that Meshtastic has that layout.

## Arrival gates (retain separate outcomes)

1. Identify each physical board, factory MAC, revision, flash, USB bridge/console,
   bootloader and partition layout; bind N3-1/N3-2 to exact identity.
2. Confirm the proposed V3 OLED pins17/18, reset21, Vext36 LOW, I2C address0x3c,
   controller/init sequence, mounted rotation, GPIO0 button and safe rail behavior.
   Confirm no GNSS. These are fixed candidate routes, not wiring evidence.
3. Establish reviewed obtainable recovery and first-install compatibility. Later
   app-only updates reuse verified recovery, retain bounded application drain,
   exact write bounds, actual application readback and protected-prefix comparison.
   A full-flash backup is a separately requested operation.
4. Test the console/framed responses, all four pages/buttons/blank/wake, then direct
   T096 reception and existing RC paths, loss/reacquisition and several admission/
   authentication renewal cycles. Record failed/missing opportunities.
5. Compare BLE health with display activity, test both units together and record
   finite endurance. Optional estimated relay needs separate observations. Lock,
   RMS and checksums do not establish independent UTC/output accuracy.

The public builder and inspector open no hardware. Installation uses a separate
exact-device controller/review and operator authorization.

The [configuration/tooling provenance](configuration-provenance.json) records
selected W12 build adapters in addition to the [entry-point, console and font
origins](../../firmware/n30/imports/2026-10-20.json). The generated
`legacy_wire_id`3/4 is an inactive compatibility field; the authenticated device
identity remains the full factory-MAC-derived64-bit value.

Factory commissioning preserves a compatible existing bootloader/OTA partition
table and selects the actual app0 partition at0x10000 only after validating the
factory OTA selection. The compiled1MiB ceiling remains enforced inside the
larger factory slot. No old application or whole-flash backup is required for
these expressly authorized factory units. Retain the reviewed new image and
actual readback as recovery for subsequent routine updates.

The [N30 framed console parser](../../firmware/n30/tools/framed_console.py)
validates the additional `N30_UI` row as part of the original nonce/CRC/boot/
source/build/deadline frame. Bound `ui page 0` / `ui page 1` / `ui page 2` / `ui page 3` commands affect
presentation only and do not change the radio or clock authority.

Version0.1.2 keeps the shared8704-byte whole-frame and1.5s response limits,
admits exactly ten N30 rows, and uses a compact GPS-absent row to fund the
display status. Tests exercise the actual firmware submission/output worker and
render both receiver/experimental-relay formats at maximum numeric widths.
The initial0.1.1 installation booted and received the mesh, but rejected its
ten-row status at the historical nine-row submission guard. Keep that failed
console observation distinct from the corrected-image checks.

Version0.1.3 routes the ticket-bound display-only page commands before the BLE
ownership fence, after identity and maintenance checks. The physical buttons
remain on their existing display-worker path. A compiled test of the actual
dispatcher verifies page control with the receiver active, unchanged radio
state, and rejection of wrong identity/boot, replayed or expired tickets and
commands during maintenance. This changes no W12 timing or radio source.

Version0.1.4 adds actual BLE/RC1/RC2 source labels to the T096-style moving
screensaver. A main-page1.5s press posts one expiring action to the main owner,
which calls W12's unchanged upstream automatic/relay-only API and source/drain
fences. This selects reception policy; estimated relay transmission remains OFF
in the receiver images. RF help/blank and consumed wake behavior remain available.
Offline tests cover debounce/threshold/release/extended holds, no repeated or
stale queued toggles, maintenance refusal, truthful source expiry and all saver
positions. Physical gesture, mode transition and sustained reception outcomes
must be recorded separately on the arriving hardware.

Version0.1.5 adds a nonblocking500ms display-only grace on the first clock page
(internal page0). Only a successfully transferred live dial can seed the cache.
On reception/quality loss its hands freeze; at500ms the current syncing/error
panel replaces it. The10ms display worker observes loss/expiry independently
of the4Hz frame schedule and requests a frame at grace transitions. Actual
transfer/worker latency remains separate from the500ms software deadline.
No timing model, live source/lock status, radio or W12/T096 code is retained or
modified by this presentation cache. Host tests compare the frozen dial pixels
and cover499999/500000us boundaries, recovery, repeated loss, stale input,
monotonic reversal, hidden surfaces and transfer failure. Physical field
behavior remains a separate observation.

Version0.1.6 extends that same500ms grace to the complete clock presentation.
Main-page UTC digits and analog hands retain the same last successfully
transferred frame; only RMS digits and the lock remain live. A brief quality dip
retains the full screensaver frame at its actual moving position, preventing
the previous immediate saver exit/normal-page/reentry jump. The original idle
and30s movement clocks continue; wake/navigation overrides grace immediately.
All six underlying-page/position combinations, exact deadline, movement-boundary
recovery, stale input, transfer resets and pixel retention are tested offline.
Console/live timing validity and W12/T096 code are unaffected by the pixel cache.

Version0.1.7 keeps4Hz clock/saver and8Hz RF rates while anchoring their wakes to
the current modeled UTC phase. Host tests cover exact second/pulse boundaries,
arbitrary boot phase, model corrections, missed slots and changed-span ordering
in both rotations. The main dashboard and saver share one BLE-derived PPS gate;
a cached500ms clock never invents fresh pulses. `N30_UI phase_us` records the
maximum routine UTC-slot render-start lateness, excluding forced navigation;
I2C transfer and panel scanout still add latency. No physical PPS output or
independently measured display-edge/UTC accuracy is established.

## Version 0.2.3 passive LoRa receiver

Add `--lora-transmitter-factory "$VERIFIED_T096_LORA_FACTORY"` to either build
command only with the exact existing T096 transmitter identity in the shared
registry. The default all-zero value disables LoRa hardware. This identity is a
private build input, not a label inferred from a board alias. The generated
`n30_lora_config.h` and its hash bind the sole transmitter and shared enrollment
into the runtime build ID. It adds no NVS enrollment or mode writes.

The fixed candidate Heltec V3 route uses SPI2 SCK9/MOSI10/MISO11/NSS8,
reset12, BUSY13, DIO1 14, DIO2 RF switch and DIO3 1.8V TCXO. Modem settings match
the T096 parent:915MHz, SF10, BW500kHz, CR4/6, preamble12, explicit header,
CRC, private sync0x1424. See the [selected public-source and board references](../../firmware/n30/imports/2026-10-20-lora.json).
These published routes are candidate wiring, not evidence of actual reception.
No user pin or transmit override exists.

A separate low-priority core1 radio worker owns SPI and the actual MCPWM DIO1
capture. An asynchronous software-capture bracket relates its80MHz counter to
the existing20MHz timing counter, then converts to the RC8 policy's16MHz scale.
The ISR only freezes/wakes; ISR arrival time is never substituted for the edge.
This shares a pure CounterMap helper, with no GNSS driver or input. BLE remains
on core0 and the display keeps its existing cadence. Captured enclosure is a
software mapping diagnostic; RF/board latency and physical bias remain unknown.

AUTO waits180s without live BLE relative lock before RX, then requires180s
continuous BLE lock before stopping. Main-page upstream reception mode and RF
help/blank gestures remain unchanged. Third-page1.5s hold selects the opposite
AUTO radio state. FromKEEP_ON a hold selectsKEEP_OFF; fromKEEP_OFF it restoresAUTO. Overrides are volatile. BLE regains display preference only while its settled relative lock is valid.
A marginal or pending BLE model cannot replace available LoRa time. LoRa is display fallback
only, with no BLE clock writes, precision training, downstream authority or TX.

Ticket/nonce/boot/factory-bound `lora on`, `lora off`, `lora auto` control this
RX-only mode. `lora diag` returns an alternate ten-row CRC snapshot containing
full original identities/context/event/envelope, diagnostic companion quality,
logical RX/replay/fit counters, physical IRQ/CRC/SPI/capture counters, generation,
stop acknowledgement and policy state. Select `n30_ui_revision=3` and
`n30_frame_kind="LORA"` in the parser manifest for that response. Normal status
keeps its original ten rows/8704bytes/1.5s response bound. Maintenance consumes
queued gestures and inhibits mode changes; an OFF acknowledgement requires SPI
retirement, capture release and reset assertion. A stalled DMA retains its
buffers and refuses both acknowledgement and reuse. No durable fault journal
is claimed; current software fault/context information is volatile.

In addition to the ordinary host suite, test the actual T096 portable codec,
HMAC interoperability, replay/context/expiry rules and fit oracle with:

```sh
python3 firmware/n30/tools/lora_tests.py --idf "$IDF_PATH" \
  --out artifacts/n30-lora-tests
```

This uses the public pinned SDK's TinyCrypt sources, synthetic enrollment and
no device. The ordinary suite includes production SX1262-owner peripheral
fakes, exact180s boundaries, cancellation during startup, partial capture setup,
DMA retention, worker stalls, three-page gestures and strict diagnostic framing.
Finite hardware tests must separately demonstrate the actual modem/capture,
BLE coexistence, LoRa original/followup reception, AUTO timing and physical
buttons. Checksums, link RMS and relative dual-clock values do not establish
UTC accuracy or end-to-end delay bounds. W12/T096 sources remain unchanged.

Version0.2.1 coalesces a forced page/wake redraw with a modeled UTC edge when
starting the50ms whole-frame work budget would cross it. Navigation can wait
less than50ms; routine4/8Hz cadence is unchanged. Transfers that occupy a later
slot drop that missed slot instead of issuing a catch-up burst. This corrects
the first0.2.0 canary's27ms render-start process failure on the third page;
actual display-edge and independent UTC accuracy remain unqualified.

Version0.2.2 uses the pinned SDK's finite queued SPI transfer/result APIs. The
first0.2.1 LoRa-start canary rejected an invalid finite polling-start timeout;
its SDK early log damaged that CRC response, which the parser refused. The
failed observation is retained separately from subsequent receiver tests. DMA
buffers/descriptors still remain owned until actual completion; timeout never
permits reuse or a drained acknowledgement.

Version0.2.3 keeps the analog dashboard/saver and `LOR` source while RC8 time is
available and BLE lacks a live settled relative lock. Even a valid BLE following
clock or pending upstream change cannot hide that LOR clock or its modeled PPS
lamp. Actual BLE lock restores BLE preference. This is presentation selection;
radio180s dwell, source expiry, independent raw clocks/comparison, timing models
and W12/T096 behavior remain unchanged.

Version0.2.4 preserves normal4Hz saver refresh, sets both saver modes to half
normal OLED contrast, and enters a header-only saver after900seconds of actual
saver residence. Its two routine frame edges are0/250ms within each modeled
second; the other750ms have no routine rendering or I2C. GPIO edges wake the
blocked display worker; model notifications still rephase wakes and source
expiry/grace deadlines remain bounded. Unused20ms legacy indicator calculations
pause in minimal mode. The live20MHz XTAL GPTimer's SDK lock prevents chip light
sleep, preserving the BLE/LoRa timebase; task blocking is not chip light sleep.
Host tests cover exact residence/PPS boundaries, late initial lock, wake/reset,
all positions, cached-clock loss and unchanged normal cadence. Physical power,
long-duration operation and display-edge accuracy require separate observations.


Version0.2.5 gives both saver layouts one latched session, independent of the
successfully transferred pixel cache. BLE/LOR source switches and a precision
lock dip with valid following time keep the original15minute residence and30s
movement clocks. Only500ms of continuous total timing loss (neither live following
BLE nor available LoRa), or user wake/navigation, exits that session. Raw local
HOLD and stale snapshots do not renew it. Resetting the cache after an I2C failure
does not reset residence. The ordinary page0 precision-quality grace and the PPS
lock gate remain unchanged. During a brief total outage, cached saver pixels
stay bounded by500ms; without a cache, its own layout shows WAIT/NO TIME. Minimal
mode retains its0/250ms cadence and also arms the shared loss deadline.
Production-pipeline regressions cover both layouts, all pages/positions, repeated
source changes, partial lock loss, cache resets, exact499999/500000us loss and
recovery boundaries, and a15minute transition during an outage. This does not
extend radio source validity or independently qualify physical display timing.


## Version 0.2.6 field parent recovery

N30 enables a60s failed-parent admission retry gap in the shared W12 owner.
After60s without two continuously observed seconds of raw relative lock and the
existing one-second hold budget, automatic selection seeks alternatives. The
failed parent cannot start another admission for60s, then gets at most one retry
per60s until sustained lock or an actual new parent binding. Ordinary packet
arrival, JOIN renewal, source-epoch changes and short lock flickers do not reset
that failed-parent window. The old stream may continue only within its original
source/model deadlines; this policy grants no time or admission authority.
Named-parent and relay-only operator constraints retain their policy. The shared
W12 default retry gap stays zero; T096 sources are unchanged.

The prior one-minute selector alone was insufficient: direct-GM retries could
keep sliding the common admission rate history forward, preventing a relay's
20s/60s rate gate from opening. A synthetic production-native regression records
seven more GM attempts with no relay before the change, and a relay opportunity
within the next60s under the same history after it. This is an opportunity bound
under those synthetic conditions, not a promise that an absent, unauthenticated
or unreachable relay will synchronize.

Run the retry integration check after building the shared host codec objects:

```sh
python3 firmware/w12/tools/g1_host_tests.py --out artifacts/n30-shared-g1-tests
python3 firmware/w12/tools/precision_priority_tests.py --n30 \
  --objects artifacts/n30-shared-g1-tests --out artifacts/n30-parent-recovery \
  --cases n30-relay-retry-budget gm-dwell gm-lock-fallback gm-lock-search \
    gm-locked-retain relay-failed-gm
```

The ordinary N30 host suite also covers exact retry boundaries, source renewal,
unavailable/rebind, operator choices, brief/sustained lock recovery and preserved
W12 defaults. The LoRa suite now links the actual SX1262 owner, RC8 codec/group
HMAC, N30 bridge and display selector against synthetic peripherals. It starts
AUTO RX after a late BLE loss, runs six simulated minutes through capture-counter
wraps, drops a CRC packet, expires timing during total RF loss, and reacquires
from fresh physical original/followup pairs. Available LoRa retains display
preference over unlocked BLE throughout. These tests do not establish physical
RF reception; RX ON alone supplies no time. No captured field trace explains
the reported LoRa non-acquisition. Keep that uncertainty separate from the
reproduced and fixed BLE admission starvation.


## Version 0.2.7 returning source and no-time watchdog

When the old source/replay scope expires, the BLE owner correctly withdraws its
binding. The selector previously kept the failed precision trial but lost the
incumbent locator used for bounded retries. Its trial exclusion could then
reject the only returning GM forever, even after advisory history expired.
N30 now allows a freshly observed locator for that unbound trial as a last
candidate, after its60s retry gap. Eligible alternatives still win. The retry
performs a new MAC/challenge/causal-time exchange; no old clock, source permission,
PA handle or locator is revived. Same-boot GM recovery works with either a changed
source epoch/publication or the same source scope. W12's default policy is unchanged.

The existing100ms supervisor also owns a separate300000000us continuous no-time
timer on N30. It runs while reception is requested and update maintenance is not
latched. Fresh authenticated BLE FOLLOW UTC or available LoRa time cancels the
interval; the next loss starts a new full five minutes. Relative precision lock
is not required. The inputs retain their original source/anchor deadlines and
250ms snapshot freshness. Local HOLD, membership, discovery, packet counters and
repeated polls cannot count as sync. LoRa RX ON without an accepted timing pair
cannot count either. Explicit OFF and maintenance cancel the timer immediately.
No chip sleep/timebase behavior or display/saver cadence changes.

At the full300s boundary the existing supervisor records factory/build-bound RTC
restart evidence and calls the supported MCU restart. Its no-time mask is524288
(`0x80000`); existing faults/stalled workers still use their separate120s timer.
No new task, NVS write, or blocking logger is added. The restart restores only the
existing explicit-parent intent, never old timing or automatic-parent caches.
Volatile LoRa overrides still return to AUTO at boot. Without any source the
unit may therefore reboot once per five-minute acquisition interval.

The ordinary N30 host suite compiles the actual supervisor at synthetic FreeRTOS,
clock and restart boundaries. It covers exact299999999/300000000us, recovery just
before/at expiry, a fresh interval after recovery or OFF/maintenance, RTC reason,
monotonic regression, the platform clock's upper range, and preserved W12/120s
fault behavior. Its adapter test covers BLE/LoRa switching, expiry, stale/held
inputs and no polling renewal. The production-native integration cases
`n30-source-return` and `n30-source-return-same-epoch` fail on the prior selector
and pass after the fix, performing a new signed exchange against the same GM
boot. Run all N30 scheduling cases with:

```sh
python3 firmware/w12/tools/precision_priority_tests.py --n30 \
  --objects artifacts/n30-shared-g1-tests --out artifacts/n30-source-return
```

Use the shared-codec preparation command above first. These are offline software
regressions. Physical five-minute watchdog timing, GNSS loss/return in the field,
and independently measured timing accuracy remain separate hardware observations.


## Version 0.2.8 intermittent GM recovery and acquisition

The old one-minute trial could be renewed by two seconds of raw lock, and any
brief displayed lock restarted LoRa's three-minute acquisition timer. Repeating
short good intervals could therefore suppress both relay discovery and LoRa
AUTO for ten minutes. N30 now accumulates60s of observed lock loss, paused by
brief good samples and cleared only by60s of continuous observed lock. A failed
trial leaves discovery open even if raw lock briefly returns. Existing60s failed
parent retry bounds, fresh locators, authenticated JOIN and operator choices
remain. The shared W12 default selection policy stays unchanged.

LoRa AUTO likewise accumulates180s of loss and requires180s of continuous lock
to clear it and stop reception. The main radio/supervisor path supplies actual
current BLE quality directly; OLED initialization, blanking, minimal mode and
displayed lock settlement do not control backup acquisition. Explicit KEEP OFF,
receiver OFF, faults and update maintenance retain their fences. Once an
original/follow-up pair provides available authenticated LoRa UTC, it replaces
HOLD, acquisition or marginal BLE on the main clock. Being in range or RX ON
alone cannot manufacture that time. LoRa never disciplines the BLE model.

The acquisition fix retains N30 corrections whose original capture preceded
the most recent model publication. Each direct original freezes its delivered
prediction before training, so its later correction is scored against that
capture-time map rather than a newer fitted map. Missing predictions stay
explicitly missing. The same eight pending slots add128B; the clock remains
within2KiB. Source/path/capture/replay checks, increasing originals and remote
time, original expiry and discontinuity fences still apply.

N30 also keeps every eligible16Hz fitting opportunity in its existing64-point
history, instead of decimating after the first fit into a roughly16s window.
It measures acceleration across at least one observed second: differences
between adjacent overlapping fits had amplified channel/quantization noise
into an infinite guard. The unchanged255/unknown guard still applies to large
observed acceleration. W12 retains its original fitting and scoring paths.
The16s physical-time quality buckets retain every eligible causal tail; no
outlier removal, threshold relaxation, clock step or source deadline extension
is introduced. RC7 authenticated timing and its existing mapping fences remain.

```sh
python3 firmware/w12/tools/g1_host_tests.py --out artifacts/n30-shared-g1-tests
python3 firmware/n30/tools/recovery_tests.py \
  --objects artifacts/n30-shared-g1-tests --out artifacts/n30-recovery-tests
python3 firmware/w12/tools/precision_priority_tests.py --n30 \
  --objects artifacts/n30-shared-g1-tests --out artifacts/n30-native-recovery
python3 firmware/n30/tools/lora_tests.py --idf "$IDF_PATH" \
  --out artifacts/n30-lora-recovery
```

Synthetic4s-good/16s-bad cases previously selected no relay and never enabled
LoRa during600s. The corrected selector exposes the fresh relay opportunity
by76s and enables AUTO before240s under the same observations. Actual native
owner tests cover transient raw lock after failure and relay admission.
The real SX1262-owner/RC8/HMAC test continues through authenticated LoRa pair
acquisition, CRC loss, total RF loss and reacquisition using synthetic peripherals.

A synthetic hopping-channel test with±5ppm initial frequency and±100ppb/s rate
change previously never locked during120s. The corrected N30 reaches the
unchanged relative lock gate at18s and stays locked for all1280 observations
after40s; observed causal maxima are below350ns in those two scenarios. Its
separate dense-stream check retains all150 eligible corrections, preserves nine
signed-envelope refusals, and records two unavailable startup predictions.
These are deterministic software regressions, not field RF evidence or an
independently measured UTC/PPS/output guarantee. Physical acquisition speed,
intermittent-GM roaming and five-minute watchdog behavior still need retesting.

## Version 0.2.9 reusable timing diagnostics

Timing diagnostics are a volatile runtime observer, OFF at boot. The existing
factory/boot/ticket/nonce and update-maintenance checks apply to these actions:

```text
timing start DURATION_MS EVERY
timing read AFTER_RECORD_NUMBER
timing stop
```

Duration is1000–600000ms and EVERY is1–64. A running session cannot be extended
or replaced by another start. Stop it explicitly before starting a new session.
Reboot, ordinary OFF and update preparation stop recording. No NVS is written.
The deadline starts at command admission; response reads do not renew it.
Numbers outside uint32 or malformed commands are refused before configuration.

Use `firmware/n30/tools/framed_console.py` to generate the bound command from
a fresh verified status frame. Parse its response with `n30_frame_kind="TIMING"`
in the same installation manifest. Each alternate response keeps the original
ten-row8704-byte/1.5s envelope and carries status, four sample slots and an
explicit observational contract. A fresh nonce/ticket is required for each
request. Advance the host cursor only after complete CRC validation. Reads do
not consume records, so a failed response can be retried. A new diagnostic id
starts a separate cursor population.

The fixed16-sample RAM ring records matched descriptor captures in the host
worker, then attaches only authenticated corrections accepted by the existing
clock. Raw captures without accepted corrections remain explicitly unmatched;
they are not zero-valued timing errors. Records wait for their correction or
the original250ms association horizon before export. `seen` counts stable
consume-once captures, not every RF opportunity. `pair_missing` includes
intentional decimation. Sample numbers and `sampled`/`overwritten` counters
expose records not obtained by the host. Neither a RAM read nor queue admission
proves delivery to the host.

EVERY4 with one read per second is a conservative starting point at16Hz. Faster
sampling requires enough successful batch reads and UART capacity; EVERY1
requires four batches per second before accounting for other output. Keep
overwrites, missed CRC frames and unavailable corrections in the population.
Default-OFF callbacks use a fast disabled check, with no additional ISR work or
descriptor/register writes. The observer preserves exact raw fields and frozen
causal predictions; it supplies no timestamp or source authority of its own.

Offline verification and analysis:

```sh
python3 firmware/n30/tools/timing_tests.py --out artifacts/n30-timing-tests
python3 firmware/n30/tools/timing_analyze.py \
  --raw artifacts/local/timing.raw --manifest artifacts/local/installed-manifest.json \
  --out artifacts/n30-timing-analysis
python3 firmware/n30/tools/timing_analyze.py \
  --raw artifacts/local/timing.raw --manifest artifacts/local/installed-manifest.json \
  --assume-centered-bins --accel-bound-ppb-s 100 --work-margin-us 1000 \
  --out artifacts/n30-conditional-shadow
```

The analyzer revalidates CRC/factory/source/boot binding and accounts for
incomplete frames, conflicting records, missing samples and unavailable causal
predictions. It separates complete device/diagnostic/root/boot/source/train/local
capture scopes. Channel summaries retain signed means and full RMS. Fine-field
high bits and mapping bracket widths are observations, not calibrated phases.

The optional shadow intersects affine phase/rate constraints on up to64 points.
It retains the full recorded mapping enclosure, a500ns descriptor interval,
TX tick quantization and the explicitly assumed acceleration envelope. An
original is scored before its later correction can train the hypothetical
model; the work margin is a model assumption, not measured firmware execution.
Inconsistent, stale or insufficient observations produce unavailable estimates.
Coherent phase can remain unidentified even when residual RMS is zero.
Centered bins, channel/RF delay and the acceleration envelope are not calibrated
by this tool. Its conditional interval never replaces displayed causal RMS,
lock gates, signed source expiry or an absolute UTC/output bound. No shadow
estimate is installed in the firmware clock.

Audit obtainable S3 implementation leads independently:

```sh
python3 firmware/n30/tools/s3_timestamp_audit.py --idf "$IDF_PATH" \
  --objdump /path/to/xtensa-esp-elf-objdump --out artifacts/n30-s3-audit
```

This checks the pinned archive hash, receive10-bit624/625 arithmetic, diagnostic
GPIO names and the SDK MCPWM APB capture path. It does not read live ROM or
operate GPIOs. The SDK's BLE-log timestamp-sync GPIO is toggled by software;
it is not an ADDRESS event latch. Diagnostic GPIO names alone do not establish
a usable BLE RX/TX marker or its delay. A supported finer latch/marker and exact
packet association remain prerequisites for replacing descriptor timestamps.
MCPWM capture resolution must be queried; selecting a faster timer cannot
recover phase absent from a500ns descriptor. Preserve existing LoRa capture
ownership when evaluating any additional route.

A T096 subscribed through the optional N30 estimated relay can provide its
shadow-PPS witness. Keep a direct and a relay population separate, bind complete
parent/root/boot/epoch context, preserve startup and missing/invalid records,
and restore the reviewed parent/page afterward. That comparison includes N30
receive/model/estimated-TX behavior, T096 receive/model behavior and its local
GNSS/PPS reference. It cannot uniquely calibrate N30 RX, TX or absolute UTC.


## Version 0.2.10 downstream composite offset

The optional `provisional_path` object in the ignored N30 device input uses the
[existing W12 schema1 profile](../w12/build.md#provisional-aggregate-path-offset).
It requires `--estimated-relay` and BLE100. Absence selects no profile. The N30
builder reuses the same public validator/header generator; the full profile,
evidence hashes and publisher identity enter its receipt and runtime identity.
The inspector verifies the retained input and compiled header. Keep real device
identities and evidence in ignored local artifacts.

Schema1 `offset_ns` is the **total from the programmed packet endpoint**, not an
additional adjustment to the nominal ADDRESS endpoint. With a nominal40000ns
conversion, a measured composite phase of−7133ns calls for47133ns total: the
additional correction is+7133ns. This illustrates the sign and once-only
arithmetic; it is not a board-wide default or a calibration for another unit.
Use the actual recorded endpoint decomposition when selecting a profile.

The offset changes the canonical RC7 correction timestamps broadcast by this
publisher, so every downstream receiver receives the adjustment through the
ordinary authenticated codec. No receiver-specific configuration or second
application is required. It does not change the publisher's fitted RX clock.
The existing valid-reference, RC1, publisher and exact root/boot/source guards
remain; outside that scope the ordinary nominal conversion applies. Source
epoch transfer requires the profile's explicit existing opt-in and remains an
unmeasured transfer. No profile extends source expiry or supplies an RF/UTC
uncertainty bound. Keep bias, residual jitter and availability separate.

Supply the receipt's full `provisional_path_profile` in the console manifest.
`pcfg` is a compact configured-profile prefix; it alone does not prove a selected
packet used the offset. During an authorized bounded capture, the existing
`W12_G1_TX_PATH` record reports the actual selected publication/event/channel,
raw and advertised timestamps, profile id, nominal `base_ns`, residual and
`applied_ns`. Check `tx_ns = raw_tx_ns + applied_ns` and the matching nominal
ADDRESS decomposition, then verify an independent downstream T096 shadow-PPS
population with matching publisher/root/boot/epoch context. Preserve invalid
and expired maps, missing records and startup separately; a valid-only RMS is
not a continuous-service qualification. No new wire profile is introduced.

The generated-header integration check executes the actual RC7 codec, HMAC,
capture association and receiver map for two synthetic receiver identities:

```sh
python3 firmware/w12/tools/g1_host_tests.py --out artifacts/n30-shared-g1-tests
python3 firmware/n30/tests/provisional_generated_test.py \
  --objects artifacts/n30-shared-g1-tests --out artifacts/n30-offset-generated
```

The ordinary N30 suite also checks malformed profiles, zero defaults, mode
restrictions, generated capability flags and provenance changes. These software
checks establish arithmetic and scope, not RF accuracy.


## Version 0.2.11 raw native marker observer

The finite raw observer defaults OFF and allocates no peripheral or pin before
an exact factory/boot/ticket-bound `fine start <milliseconds> <every>` command.
Duration is 1000–600000ms and event decimation is1–64. `fine read <cursor>` uses
an independent non-consuming32-record history, four records per ten-row CRC
frame; `fine stop`, OFF, expiry and maintenance disconnect the route and stop
all three channels and the timer. Missed ring records remain counted. Failed
cleanup blocks the original two-second update drain. Ordinary timing, source
expiry, clock quality and estimated relay payloads retain their existing path.

By default, the generated profile is `CURRENT_ONLY`: no GPIO is driven or
connected to a radio signal. MCPWM group1 provides channels0/1 reserved for RX/TX
and channel2 for synchronous software CURRENT. LoRa retains group0. The timer
must report80000000Hz; external edges and IRQ are disabled on CURRENT. A pinned
private driver/register adapter clears onlyCH2, verifies RAW0, triggers SW,
waits for RAW1 within128 polling reads/4096 CPU cycles, then reads and clears the
hardware latch. This is not a stable public IDF synchronous API. Build hashes
bind the private driver, HAL and register definitions; allocated group/channel
IDs are checked at runtime. Failures return unavailable, with no stale fallback.

The arbitrary fixed counter origin supplies raw80 and observer tick20=floor of
the same counter divided by4. Current reads unwrap32 bits; a half-wrap service
gap, coarse reversal, stopped/stale counter, gross frequency mismatch or wrong
incarnation invalidates it. Coarse time guards lifecycle only, never phase.
Telemetry includes first/latest raw32, wraps, extended80, service gap and latch
latency. The production clock continues using its existing GPTimer domain: these
observer ticks must never be mixed into production models or mapped through the
old descriptor/BleMapping brackets and described as fine precision.

A marker build accepts only the fixed `NATIVE_GPIO4_5_SHADOW` local profile via
`--fine-shadow-profile`, with exact factory, HeltecV3/V3.1/V3.2 board family and
SHA256 identities for both schematic and actual physical wiring reviews. Keep
that profile ignored. GPIO4/5 are MCU-to-header nets in the reviewed official
schematics; exact-board external attachments must still be verified before
driving them. RX uses native signal230 (SYNCFOUND), TX235 (PKT_TX_ON). Route
setup checks API results and actual matrix/input/output readback, observing both
polarities. Marker names establish no ADDRESS endpoint, polarity, native phase,
RF delay or actual packet association. Hardware latches have no FIFO; software
callback/ring counts cannot certify absence of coalesced physical events.

The `N30_FINE_CONTRACT` retains RAW_ONLY, UNKNOWN endpoint/RF/UTC/coalescing,
NONE packet association and NONE clock update. The configured provisional aggregate correction
belongs solely to the legacy programmed endpoint and does
not calibrate these markers. A future authoritative path requires independently
validated marker semantics, unique immutable payload/event/channel association,
one common physical counter for all production times, separate RX/TX endpoint
calibration and held-out causal scoring. A raw observer flash or CURRENT wrap
check does not meet those later gates or certify UTC/output accuracy.

Run the actual-owner synthetic peripheral/counter failure checks offline:

```sh
python3 firmware/n30/tools/fine_tests.py --out artifacts/n30-fine-tests
```

The tests cover default resource inactivity, group/channel identity, stale raw
status/latch, timeout/cycle bounds, stopped/rate/wrap arithmetic, setup expiry,
old-admission/ISR interleaving, overwrite/decimation/non-consuming cursors,
actual maintenance drain refusal after failed cleanup and maximum frame widths.
These tests do not simulate RF semantics. All hardware work remains a separately
scoped exact-device experiment; keep its finite observations and unqualified
claims distinct from software test results.


### Version 0.2.12 native activity trial guards

The optional reviewed GPIO4/5 profile now arms ISR containment before enabling
native capture inputs. A callback budget or expired lease immediately masks only
group1 RX/TX interrupts, disables those hardware channels and both pad outputs;
main-task cleanup then retires the public driver owners. The driver CURRENT
channel remains separate. Partial route or cleanup failures attempt both pads
and keep maintenance unavailable until all owned resources acknowledge release.

CRC status retains actual GPIO output/input matrix registers, output-enable
bits, capture-channel and interrupt masks, total callbacks, budget/expiry
refusals and containment. A callback count describes observed callbacks; it is
not a count of every RF edge. Synthetic tests compile both CURRENT-only and
native profiles and inject pad/API/readback failures, partial channel enable,
callbacks during enable, a callback storm and expiry without a main-task poll.

The first native trial may test signal activity only. A flat signal under a
verified route and observed traffic limits this pinned configuration; it does
not rule out other documented ESP32-S3 capture facilities. Active signals still
need an immutable packet/window journal and independent RF endpoint evidence
before packet association, timing improvement or production migration.

Every native start explicitly clears only RX/TX pending RAW bits while those
hardware channels are disabled, then restores their peripheral IRQ masks before
public channel enable. The pinned driver owns CPU interrupt enable separately;
its channel-enable API does not restore a mask cleared by containment. Normal
stop quiesces those hardware channels and IRQs before disconnecting pads. Host
regressions exercise expiry containment, cleanup, restart and a new callback,
plus stale pending bits and refused clear/mask restoration.


### Version 0.2.13 synthetic capture-chain isolation

A reviewed native GPIO4/5 build also accepts `fine probe <spacing_ms> <edges>`.
Spacing is 50–1000 ms and total edges is 4–24 in multiples of four. This finite
mode drives only the fixed reviewed pins, alternately high/high/low/low through
ordinary GPIO output256, and feeds the same pad inputs into group1 channels0/1.
It verifies initial LOW before capture/IRQ arming. The lease is fixed at
`spacing_ms * edges + 1000` ms; each main-task poll can issue at most one
transition, with no catch-up burst or extension. Delays can consume the nominal
quiet tail and leave the pattern incomplete.

CRC records retain immutable `SYNTHETIC_GPIO` provenance through stop and a new
incarnation for each successful history. A refused or failed start cannot
relabel an older retained history. Startup stages its latch statistics and
counter origin, committing history/domain/provenance together; separate
`start_*` fields retain the attempted mode and latch outcomes on failure. Attempted writes, verified pad transitions
by channel/polarity, actual pad input levels, initial LOW mask, bounded level
poll count, last issue and stop times are separate from captured callbacks.
`initial_low=3` means both pads were verified LOW before arming; level polling
is limited to 128 reads. Quiet time is the actual interval from the last
verified transition to stop, not a promised one-second observation.

A complete chain check compares all unique captured edges with the verified
issued counts after an observed quiet interval, retaining missing, extra,
coalesced, overwritten or invalid records. CPU output intent is not an RF
reference or a measured RF timestamp. Synthetic pulses do not prove BLE marker
semantics, resolution, endpoint delay, packet association or UTC accuracy.
The ordinary BLE/LoRa clocks and legacy correction scope remain unchanged.
Every stop, expiry and containment path prevents future writes and disables
RX/TX capture/IRQ before releasing pad outputs. The group0 LoRa owner remains
independent. This mode requires the same exact factory and physical pin review
as native capture and defaults OFF.


### Version 0.2.14 fixed RW marker observation

The optional `NATIVE_RW_GPIO4_5_SHADOW` profile selects the pinned SDK's
RW_RX_ON238 and RW_TX_ON237 outputs on the same reviewed GPIO4/5 pair. It
retains exact factory, schematic and physical-review hashes. The builder accepts
only the named fixed pairs; no arbitrary signal index or GPIO console option
exists. The original 230/235 profile remains selectable and byte-identical in
its generated configuration. No baseband, coexistence or debug-enable register
is changed.

Native RW records use immutable `NATIVE_RW_SIGNAL` provenance and explicit
RW names in their CRC contract. This is a raw activity experiment with unknown
RF endpoint, latency, coalescence, packet association and UTC bounds. Synthetic
GPIO pulses retain their separate provenance and finite issued/captured-count
contract. All modes default OFF, preserve the production GPTimer clock and
legacy correction scope, and share the existing IRQ/expiry/cleanup guards.


From version0.2.14, the runtime build ID also binds the generated fine-capture
configuration header. Builds of the same source and device with different
fixed marker profiles therefore retain distinct runtime identities. The
inspector preserves the original identity formula when reading older builds;
no historical source or receipt is rewritten.


### Version 0.2.15 targeted TX diagnostic capture

The finite N30-only `capture tx <ms>` command accepts1000–10000ms through the
normal fresh ticket/nonce, exact factory and boot guards. It filters the complete
formatted record to `W12_G1_TX_EST ` or `W12_G1_TX_PATH ` before enqueue. The
existing `capture` and `capture rolling` ALL behavior and wire bytes remain
unchanged. A rejected start preserves the prior mode and counter. Stop and
expiry retain the selected mode for final diagnostics; the next successful ALL
capture clears it. This observer grants no RF, clock or publication authority.

TX-only LOGv2 uses this exact order, preceded by its ordinary LF delimiter:

```text
W12_V12_LOG version=2 mode=TX capture_id=<16hex> log_seq=<u64> enqueue_us=<u64> record_bytes=<u16> record_crc32=<8hex> record=<payload><LF>
```

`record_bytes` counts the normalized complete payload, including its final LF.
The CRC32/IEEE seed is0. Its protected bytes are the leading LF, the header
through the `record_bytes` value, literal ` record=`, and the complete payload.
Only the inserted ` record_crc32=<8hex>` field is excluded. `parse_tx_log` checks
this exact grammar, capture ID, length, printable single-line payload and CRC
before a caller joins the immutable TX tuples. A CRC establishes record identity;
it does not establish an RF endpoint, delay bound or timestamp accuracy.

The original250ms line expiry can abandon a partially transmitted record. The
host must retain and reject partial, expired or CRC-invalid records; it cannot
complete a prefix from a later line. Queues, response priority and deadlines are
unchanged. The original log sequence includes intentionally filtered attempts,
so sequence gaps alone are not a transport-loss count. The TX-only END field
`xf` is the saturating per-capture filtered count, separate from dropped,
expired, truncated and frame-loss counters. It remains covered by the complete
END CRC after stop or expiry. The parser's optional `n30_tx_capture_crc` flag is
a per-request expectation, not a global application capability.

The maximum normal body remains7903 bytes in its original7904-byte buffer. A
TX-only END adds at most14 bytes for ` xf=4294967295`: the conservative complete
END bound is799 bytes including its CRC and final LF, leaving its NUL within
800 bytes. Maximum whole-frame use is8702/8704 bytes. ALL/default maximum use
remains8688/8704. The actual output worker and independently checked CRCs are
covered by host tests; this is not a UART-delivery or RF qualification.

The small public logging wrapper rejects ISR calls before entering its noinline
formatter, whose automatic record/header storage is task-only. The live-snapshot
entry retains its own early refusal. ISR refusal increments its bounded counter
without reading the format or snapshot; host tests cover every public entry, and
native stack/disassembly review checks the branch before the large allocation.
This selected call-path review is not a complete vendor/libc stack bound.

## Version 0.2.16: precision watch face

The fourth portrait page adds a monochrome watch bezel, large segmented causal
`SYNC RMS`, signed active-clock frequency correction in `PPB`, raw 80 MHz
observer state and a segmented `HH:MM:SS` lock stopwatch. It uses the existing
font, monochrome frame, button gestures and 4 Hz display schedule. A short press
cycles clock, dual radio, LoRa, precision, then clock; a 1.5-second hold on
precision opens help and a four-second hold blanks the display.

The RMS comes from the active BLE dashboard's settled causal quality, matching
the clock page. The value remains conditional on current following time and
its original source/model deadlines. A downstream receiver's relative shadow-PPS
RMS, an offline fit or a calibrated advertised relay offset is a different
quantity and is never substituted. The signed ppb value rounds the active
clock model's frequency correction to the nearest ppb; nonfinite or out-of-range
values show dashes. Larger RMS values change units through ns, us, ms and s.

The stopwatch begins when the final settled BLE lock is first observed. It
continues across screen changes and ordinary clock-fit publications. Loss of
lock, expired time, a publication gap of 250 ms, a backward observation time,
or a change of clock origin, root boot, source epoch or causal-quality scope
starts a new interval. Unlocked live time shows zero; stale/unavailable values
show dashes. The display saturates at `99:59:59` and adds `+` afterward. This is
observed relative lock duration, not an independent accuracy guarantee.

The raw-counter indicator reads a cached owner snapshot only. Rendering and
publication never allocate MCPWM, start capture, latch CURRENT or drive GPIO.
`80MHz OFF` is the default. `OBS` requires an active healthy finite raw-observer
lease; `ERR` describes an active unhealthy observer and `---` a stale UI snapshot.
This indicator grants no packet association or authority to the delivered clock.
Production timestamp selection and its RF/UTC uncertainty remain unchanged.

The bound console accepts `ui page 3` without radio or timing mutations. Parser
manifests for this image use `n30_ui_revision=4`; revision 3 still accepts only
pages 0–2. The ten-row wire frame, response deadline and byte budgets are
unchanged. Host tests cover scope/expiry/loss/gap resets, actual page-command
dispatch, old-revision refusal, all RMS units, stale values, cached observer
reads without resource activity and both mounted framebuffer rotations.
Illustrative previews contain synthetic values and are not hardware acceptance.
