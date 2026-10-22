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
Previews include all three pages, separate dual RF clocks and LoRa OFF/ON/fallback/fault states, BLE/RC1/RC2 savers at all three moving positions,
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
4. Test the console/framed responses, all three pages/buttons/blank/wake, then direct
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
source/build/deadline frame. Bound `ui page 0` / `ui page 1` / `ui page 2` commands affect
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
