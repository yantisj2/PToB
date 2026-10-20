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
Previews include both pages, BLE/RC1/RC2 savers at all three moving positions,
and relay-only/pending main-page examples. They are native64×128 PNGs from the actual firmware renderer. Preview receipts
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
4. Test the console/framed responses, both pages/buttons/blank/wake, then direct
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
source/build/deadline frame. Bound `ui page 0` / `ui page 1` commands affect
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
