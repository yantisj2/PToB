# Build T096 RC7

Copyright (c) 2026 Jonathan Yantis. MIT.

This guide builds and inspects T096 source **0.15.208** without opening hardware.
For profile choices and controls, see the [T096 README](../../firmware/t096/README.md).
Release-by-release implementation and validation notes are in the
[T096 changelog](../../firmware/t096/CHANGELOG.md).

The common mesh image has two profiles: **GM** and **RC**. USB timing is compiled
by default; the RC backend publishes its accepted BLE source, while the normal
GM backend does not publish a USB source map. The separate flash-only
[DC image](direct-connect.md) serves GNSS over USB with Bluetooth disabled and no
group enrollment. Use the [GM7 implementation guide](gm7-implementation.md) for current
GM7 scope and compatibility and the [macOS guide](../../clients/macos/README.md) for USB consumers.

The separate [PC awake-only experiment](power-control.md) uses `--profile PC`,
group enrollment and `--pc-parent` with a direct-GM identity or `auto` for
authenticated direct-GM discovery. It has no runtime profile switch.

## Prerequisites and dependencies

The first validated build host is Apple Silicon macOS. Install Git, CMake,
Python 3.12+ and LLVM/Clang for host sanitizer tests. Linux/Windows SDK locks and
clean-host validation remain pending. The dependency acquisition command uses
public URLs and verifies SDK archive hashes. Fresh dependency acquisition and
GM/RC builds passed from a clean checkout on Apple Silicon macOS during the
[public-source review](../PUBLIC_RELEASE_REVIEW.md). This was an existing
development host, not a newly installed OS.

```sh
python3 firmware/t096/tools/dependencies.py --dest artifacts/deps
```

A previously acquired cache is accepted with `--deps PATH` only after repository
revision/cleanliness and SDK archive checks. It is not necessary to have a private
sibling checkout. Builds put all generated and secret-bearing output under
ignored `artifacts/` with owner-only permissions.

The builder uses the shared `tools/firmware_artifacts.py` lifecycle: compiler
output is disposable, the latest two successful bundles per device/profile/
configuration are retained, and installed/recovery pins remain protected.
Do not archive whole compiler trees or bypass this lifecycle.

Normal GM/RC builds also select the hash-pinned non-chain periodic AUX context
correction in `cmake/scan-aux-context.patch`. The dependency checkout stays clean;
CMake compiles exactly one generated `ull_scan_aux.c` with its input, output and
patch hashes recorded and inspected. This shared controller correction preserves
normal RC periodic reception and extended scanning; it changes no GATT exchange
or precision policy. Its production-body regression can run independently:

```sh
artifacts/deps/venv/bin/python firmware/t096/tools/scan_aux_context_tests.py \
  --deps artifacts/deps --out artifacts/scan-aux-context-test-01
```

## Local inputs

Create an owner-only `artifacts/local/enrollment.json` using the schema below.
Replace the descriptions with your private group's values; these are not literal
keys to use. Group ID is 16 lowercase hexadecimal digits; PSK is 64 hexadecimal
digits from OS randomness; key epoch is a nonzero unsigned 32-bit integer.

```json
{"group_id":"<16 hex digits>","key_epoch":1,"psk":"<64 random hex digits>"}
```

Create `artifacts/local/device.json` with your actual hardware ID. This example
uses a synthetic ID and must not be used to identify real hardware:

```json
{"board":"heltec_t096_ht_n5262g","device_id":"0123456789abcdef","label":"PToB0","battery":-1}
```

The label is exactly five ASCII letters/digits/underscore/hyphen for the existing
USB identity format. Battery is `-1` unknown, `0` absent or `1` present. The image
refuses any other device ID. Never infer battery or board compatibility from a
label. Local aliases and the device-ID rules are documented [separately](../security/identity-and-names.md).

```sh
artifacts/deps/venv/bin/python firmware/t096/tools/build.py \
  --deps artifacts/deps --out artifacts/build-rc-01 \
  --profile RC --enrollment artifacts/local/enrollment.json \
  --device-config artifacts/local/device.json
```

Normal GM/RC builds select GM7 for the GM backend. Normal RC GATT, UCS1,
RC9 and relay services remain supported in the same image. `--gm7` is an
optional explicit spelling of that default. `--legacy-gm-regression` selects
the historical GM5/6 backend for regression builds only; runtime never falls
back to it. DC and PC exclude GM7 and reject either GM selector. See the
[GM7 implementation guide](gm7-implementation.md) for frozen contracts and
current validation limits.

The normal image now selects the breaking [coarse TZ wire](../../protocol/time-zone-v1/README.md):
GM revision 4, JOIN revision 5, and RC control/capability revision 8. GM and RC
must use matching new-wire images; other targets remain deferred. Manual CDT
uses ID 2 and offset −300 minutes. Existing authenticated parent advertisements
renew TZ for three advertising periods, independently of precision timing.
Page 1 and the analog screensaver display local time while that lease is valid,
then fall back to UTC. Source .206 adds automatic GNSS zone/DST selection and
dynamic selected-source metadata updates. All participating T096s need that
update for automatic DST; old .205 receivers cache their first pair. See the
[current bounded results and remaining gates](time-zone-test-plan.md#local-205-results-29-october-2026).

Normal GM/RC builds default to `--tz-mode auto`. Geographic ambiguity, stale
position or an expired calendar produce unknown/UTC. For a stationary GM,
`--tz-mode manual-zone --tz-id 2` follows Chicago's DST calendar without needing
location selection. Use `--tz-mode manual-offset --tz-id 2 --tz-offset-min -300`
only for a deliberately fixed offset. Manual values are explicit and validated;
DC, PC and historical regression images reject these options. Mode, values and
database identity are bound into each private receipt/image. See the
[automatic TZ guide](auto-time-zone-plan.md) and [ID registry](../../tools/time_zones/zones.json).

Use `--profile GM` for a GM cold boot. For a dedicated GNSS/USB device, use
`--profile DC` and omit `--enrollment`; DC rejects group credentials and has no
runtime role switch. See the DC guide for its package command and USB RMS meaning.
The common RC7 owners and USB code are compiled for both GM and RC.
`--development` permits an uncommitted diagnostic build;
its receipt is explicitly not flashable. A deployment candidate requires a clean
local commit and separate review. Do not upload GM/RC output directories: their
headers, ELF, BIN and UF2 contain your group key. DC contains no group key, but
its exact device binding and local package remain private validation material.
Checksums establish identity, not secrecy
or authenticity. Runtime P1 provisioning is not yet implemented.

Use `--registry artifacts/local/registry.json` to compile your deployment’s shared
[name map](../../registry/README.md). The default is the synthetic public example.
Keep the real map ignored, alongside local enrollment; the receipt records its
hash and preserves a private build snapshot.

## Optional LoRa configuration

Normal GM/RC builds include the [RC8 wire 3 integration](../../protocol/rc8/README.md).
The default transmitter factory is zero, so LoRa hardware does not start. The
normal-only `--lora-transmitter-factory` option takes a 16-digit exact factory
identity verified against ignored T096 registry metadata. It is included in the
private build/watchdog identity; a display alias grants no permission.

The configured transmitter requests TX ON only in GM profile, including when it
follows an authenticated locked BLE source without local PPS. An actual serving
GM retains its qualified local PPS/source requirement. Other enrolled T096s
request passive RX, subject to the [GM7 participation policy](gm7-implementation.md).
A configured transmitter in RC profile cannot consume its own stream.

The `lora` query and page 7 expose role, source and radio diagnostics; `lora 00/01`
or a 1.5-second page-7 hold changes the configured role's volatile request.
Functional LoRa fallback has UNKNOWN freshness/total uncertainty and grants no
normal RC7 source or future RC9 authority. See the
[LoRa contract](../../protocol/rc8/README.md) for wire format, metrics and source
fences. Native inspection retains the 4,096-byte added-static ceiling and all
existing owner/controller/USB/stack gates.

## Optional controller diagnostics

Add `--controller-diagnostics` to a normal GM/RC build for unsolicited timing
and controller console streams. It defaults off; DC/PC reject the option. The
receipt, generated build identity, watchdog binding and
`controller_diagnostics:true` status distinguish this image.

Ordinary images retain explicit timing queries, the bounded journal, watchdog,
and quiet failure-only snapshots. They do not emit periodic controller/timing
streams. The verbose variant preserves the seven-entry controller FIFO and
existing resource reserves; it is an observation variant, not a controller
failure workaround. Use normal, nonoptimized Python so build-generator assertions
remain enabled.

See the [controller canary](diagnostics.md#controller-prepare-canary),
[host-retirement snapshot](diagnostics.md#disconnect-host-snapshot-source153),
[controller failure snapshot](diagnostics.md#controller-failure-snapshot-source155)
and [prepare-FIFO capture](diagnostics.md#prepare-fifo-failure-only-capture-source156)
for decoding requirements and UNKNOWN handling. Compare exact ordinary and
verbose images under the same placement, roles and observation cadence before
attributing a field failure to diagnostic load.

## Checks

Recheck an existing build with the dependency environment (which includes
pyelftools). The inspector reads files only; a passing result does not authorize
installation:

```sh
artifacts/deps/venv/bin/python firmware/t096/tools/inspect_build.py artifacts/build-rc-01
```

Run the complete production-body host suite and provisioning/console checks:

```sh
python3 firmware/t096/tools/test.py --deps artifacts/deps
python3 -m unittest discover -s firmware/t096/provisioning -p 'test_*.py' -v
python3 -m unittest discover -s firmware/t096/tests -p 'test_console*.py' -v
```

Use a recent LLVM clang (the cutover checks use the installed Homebrew LLVM);
Apple's system clang sanitizer runtime is not the validated runner. The host
runner selects Homebrew LLVM when available; use `CC` to select another compiler.
Keep each output directory fresh. Focused runners and their invocation examples
are retained with the relevant [release notes](../../firmware/t096/CHANGELOG.md)
and [diagnostic schemas](diagnostics.md).

For the GM7 source snapshot ordering and original freshness gates alone:

```sh
artifacts/deps/venv/bin/python firmware/t096/tools/gm7_source_order_tests.py --deps artifacts/deps --out artifacts/source-order-01
```

Tests exercise production C bodies with explicit fake hardware boundaries.
Native inspection separately checks linked resources, stack budgets, generated
controller hashes and translation-unit membership. Neither check qualifies
physical timing, radio coexistence, sustained HOT operation or UTC accuracy.

## Hardware update boundary

No general public flasher is shipped yet. The
[cutover validation](cutover.md#attached-unit-validation) used a local reviewed
maintenance/update harness with exclusive ownership, fresh source/boot/silicon
binding, app-only writes, exact protected-region checks, rollback, actual
application readback and supported return. Do not substitute a mass erase, raw
debugger write or guessed bootloader command. Public installation/recovery
instructions require their own generic-device acceptance.

Explicit maintenance drains owners and accepts commit inside the original READY
deadline. The subsequent hardware watchdog reset can take 30 seconds. Exact-device
update tools must use a finite post-commit enumeration allowance that accommodates
this latency. Do not retry prepare/commit or extend their original deadlines
while waiting for the bootloader.

The [GM acceptance plan](gm-standby.md#development-checkpoint-and-remaining-acceptance)
keeps installation, reset latency, source qualification, HOT renewal and physical
multi-device observations as separate checks. Release records and simulations
provide context; they do not authorize hardware operations.

Source 0.15.205 inspection separately accounts for the 104-byte HOT diagnostic
allowance and 272-byte TZ allowance, alongside the existing crash diagnostics
and 4,096-byte RC8 incremental budget. The absolute 1,024-byte unallocated floor
and existing stack allocations are unchanged. The four locally installed .205
images leave 4,372 bytes of linked static RAM unallocated; this is a linker
result, not a runtime stack or endurance measurement.

Source .206 separately caps the complete automatic-TZ mutable addition at
256 bytes against that .205 baseline and generated flash data at 16 KiB.
These limits include position, discovery queues and copied snapshots; they do
not increase radio/USB or stack allocations. Actual native resource results
are recorded with each candidate's inspector output.
