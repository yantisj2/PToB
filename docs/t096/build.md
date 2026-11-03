# Build T096 RC7

Copyright (c) 2026 Jonathan Yantis. MIT.

Native builds [clean compiler output and retain the latest two image bundles](../firmware-storage.md)
per device/profile, with installed/recovery pins preserved.

This guide builds and inspects current T096 source without opening hardware.
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

These inputs provision trust; they are separate from runtime mesh selection.
Production mesh/source selection must use local authenticated discovery, not a
fixed mesh-ID pin. A context change requires fresh authenticated admission.

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
must use matching coarse revisions; [W12/N30 ports](../esp32-time-zones.md)
support the same metadata. Check other targets' own guides. Manual CDT
uses ID 2 and offset −300 minutes. Existing authenticated parent advertisements
renew TZ for three advertising periods, independently of precision timing.
Page 1 and the analog screensaver learn the selected authenticated zone and
retain it in RAM for the current boot. Live metadata leases still
expire independently; the cache grants no timing or export authority. Total time
loss hides local conversion without erasing the learned pair; fresh valid
metadata replaces it, and physical boot clears it. Automatic GNSS zone/DST selection and ordered dynamic
updates use these same coarse fields. All participating T096 s need .209 or
newer to receive automatic changes after synchronization. See the
[current automatic-TZ results and remaining gates](auto-time-zone-plan.md#local-209-results--29-october-2026).

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

Select `--lora-passive-slow` plus one exact `--lora-transmitter-factory` for the
[current passive wire3 design](lora-protocol.md). Use the same designated binding
on all devices. It identifies privately verified hardware, never selects a
runtime mesh. Do not pass a nonzero `--lora-backup-transmitter-factory`: the RAM
cohort rejects automatic backup TX. Local authenticated discovery and current
source admission remain required. DC/PC reject passive variants.

For the experimental matched SF11/BW500 DTS and precision PHY, add
`--lora-passive-wide` on both ends. It requires slow mode and uses authenticated
schedule4, profile `RAM_30_120_300_BW500`. Narrow schedule3 images reject it.
The option leaves payload sizes, cadence, power and original deadlines
unchanged; shorter DTS packets provide more launch room in the original source
grant. DTS loses about2.5dB typical demodulation SNR threshold versus SF12;
timing, range and sensitivity require measurement. Omitting
the option retains the narrow timing PHY. Receipts and watchdog configuration
bind the selection.

The slow image requests LoRa ON at boot. Listeners receive in GM and RC profiles;
GM participation does not suppress passive RX. The designated device transmits
only in GM profile with an eligible source: qualified own-GNSS time or fresh
authenticated locked BLE time from another GM. It need not be the serving GM.
Entering GM restores its TX request; manual OFF lasts until the next RC→GM
transition or reboot. Other devices cannot become LoRa emitters by changing mode.

Slow uses30s precision,120s discovery, unchanged300s META, a4s bootstrap guard
and108s/h (3%) per-boot engineering accounting. Schedule3 requires matched
receivers; earlier schedule2 images reject its packets. The7s SYNC offset
and700-unit delay validator remain. The [soak guide](lora-soak.md)
separates ordinary reception qualification from [manual UTC jobs](lora-utc-diagnostic.md).

All mutable operational state is RAM-only. Physical boot starts zero airtime,
a fresh random seed and clean cursor/serial/pending/fault state. Service/profile/
source changes within the boot retain accounting and cooldowns. No ledger
provisioning, silent-hour proof, historical-state restore or journal erase
belongs in installation. Preserve historical tail bytes and existing protected
app-tail, credential and recovery bounds; the executable ends before 0xd7000.
No cross-boot airtime enforcement or FCC compliance is claimed. Resolve actual
RF mode, antenna and authorization before any separately scoped transmission;
see [range/compliance considerations](lora-protocol.md#range-and-fcc-compliance-considerations).

`lora` queries detailed role/source/radio state; `lora-diag` is compact.
`lora 00/01` or the menu changes the configured role's volatile request. An ON
request proves neither RF activity nor valid time. A 2 s hold opens the menu;
taps move and another hold selects. [Button controls](../../firmware/t096/README.md#button-controls).

For finite fast lab work, choose `--lora-passive-fast` instead of slow with the
same exact transmitter binding. `RAM_LAB_4_12` boots OFF; `lora 01` selects at
most 15 min of 4 s precision/12 s META under a 1080 s/h engineering cap; `lora 00`
closes it. Fast work cannot qualify slow operation. Both passive schedules
reject RTT commands and grant no normal RC7/RC9, GM or USB timing authority.

When neither passive option is selected, the historical
[RC8 wire5 implementation](../../protocol/rc8/README.md) remains a distinct
build configuration. Its default zero transmitter binding leaves LoRa hardware
off. Its source-opened RTT and `lora-rtt` lab controls do not apply to wire3.
Do not mix these protocols or infer compatibility from shared target hardware.
Native inspection retains existing owner/controller/USB/stack and linked-resource
gates; software checks do not qualify range or absolute UTC accuracy.

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

Native inspection preserves the absolute 1,024-byte unallocated RAM floor,
existing stacks, buffers and owner/controller/USB reserves. It checks the
selected LoRa, menu, time-zone and display additions against their linked
baselines. Per-candidate inspector output records actual resources;
[dated allocations](../../firmware/t096/CHANGELOG.md) are historical checks,
not runtime stack, endurance or hardware acceptance.

### Finite non-timing TX diagnostic

For separately authorized PHY/scheduler fault isolation, add
`--lora-tx-diagnostic` to an own-device `--profile GM --lora-passive-fast` build.
The exact `--lora-transmitter-factory` must equal the device configuration's
verified factory ID. This option is excluded from production slow, RC, DC and PC
builds and is bound into the receipt, compiled configuration and watchdog hash.
Its secondary class is `RAM_DIAG_4_12`. Keep the enrolled image local and use the
normal artifact retention and exact-device installation/readback procedure.

The diagnostic boots with LoRa OFF. After verified installation/readback and the
short 4 s bootstrap guard, `lora 01` permits one
36 s arm per boot. Repeating ON cannot renew it; OFF ends it and prevents another
arm until a separately authorized reset. It has a 72 s rolling-hour total cap,
starts with zero current-boot airtime and reserves less than 12 s of new airtime. The owner
stops admitting reservations that cannot finish their original complete frame
before expiry. A host disconnect cannot leave a 15-minute laboratory run active.
Channel cooldowns begin after actual or conservatively possible current-boot RF.

The test uses local monotonic phase, physical capture and the existing production
PHY, lengths, hopping and scheduler. It requires no GNSS and exports no UTC. All
frames begin with invalid `DX` bytes and contain no production context or timing
authentication tag; normal decoders reject them. LoRa reports `time_valid:false`,
`choice: 0` and `tx_diagnostic:true`. BLE GM operation retains its normal source
requirements. This mode measures TX mechanics, not client timing or UTC accuracy.
