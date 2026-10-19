# Build T096 RC7

Copyright (c) 2026 Jonathan Yantis. MIT.

The common mesh image has two profiles: **GM** and **RC**. The separate,
flash-only **[DC image](direct-connect.md)** serves local GNSS over USB with
Bluetooth disabled and no group enrollment. USB timing support is always compiled;
source-map publication currently uses the RC backend. The reviewed normal GM
backend does not supply a USB source map. Use a compatible RC for the macOS
chain; see the [stability results](../../evidence/stability/2026-10-16/README.md).
The public builder never opens hardware. It uses only this checkout, pinned
public dependencies and your ignored local enrollment/device inputs.

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

## Checks

Recheck an existing build with the dependency environment (which includes
pyelftools). The inspector reads files only; a passing result does not authorize
installation:

```sh
artifacts/deps/venv/bin/python firmware/t096/tools/inspect_build.py artifacts/build-rc-01
```

```sh
python3 -m unittest discover -s firmware/t096/provisioning -p 'test_*.py' -v
CC=clang python3 firmware/t096/tools/dc_tests.py --out artifacts/check-dc
CC=clang python3 firmware/t096/tools/dc_gnss_tests.py --out artifacts/check-dc-gnss
CC=clang python3 firmware/t096/tools/usb_sof_tests.py --out artifacts/check-usb
CC=clang python3 firmware/t096/tools/watchdog_tests.py --deps artifacts/deps --out artifacts/check-watchdog
CC=clang python3 firmware/t096/tools/runtime_profile_tests.py --deps artifacts/deps --out artifacts/check-profile
python3 firmware/t096/tools/test.py --deps artifacts/deps
CC=clang python3 firmware/t096/tools/rc_gm_admission_tests.py --deps artifacts/deps --out artifacts/check-admission
CC=clang python3 firmware/t096/tools/rc_delivery_tests.py --out artifacts/check-delivery
CC=clang python3 firmware/t096/tools/rc_selection_tests.py --out artifacts/check-selection
CC=clang python3 firmware/t096/tools/rc_relay_admission_tests.py --deps artifacts/deps --out artifacts/check-relay-admission
CC=clang python3 firmware/t096/tools/rc_durability_tests.py --out artifacts/check-durability
CC=clang python3 firmware/t096/tools/rc_publication_tests.py --out artifacts/check-publication
CC=clang python3 firmware/t096/tools/usb_ep0_tests.py --deps artifacts/deps --out artifacts/check-ep0
CC=clang python3 firmware/t096/tools/precision_schedule_tests.py --deps artifacts/deps --out artifacts/check-schedule
CC=clang python3 firmware/t096/tools/rc_renewal_tests.py --deps artifacts/deps --out artifacts/check-renewal
CC=clang python3 firmware/t096/tools/usb_lifecycle_tests.py --deps artifacts/deps --out artifacts/check-usb-lifecycle
python3 -m unittest discover -s firmware/t096/tests -p 'test_console*.py' -v
```

Use a recent LLVM clang (the cutover checks use the installed Homebrew LLVM);
Apple's system clang sanitizer runtime is not the validated runner. Keep each
output directory fresh. Tests operate on production C bodies with fake hardware
boundaries. They do not qualify physical timing or independent UTC accuracy.

## Hardware update boundary

No general public flasher is shipped yet. The TC-04 cutover validation uses a
local reviewed maintenance/update harness with exclusive ownership, fresh source/
boot/silicon binding, app-only writes, exact protected-region checks, rollback,
actual application readback and supported return. Do not substitute a mass erase,
raw debugger write or guessed bootloader command. Public installation/recovery
instructions require their own generic-device acceptance.

With 0.15.99, explicit maintenance still drains owners and accepts commit inside
the original READY deadline. The subsequent hardware watchdog reset can take
30 seconds. Exact-device update tools must use a finite post-commit enumeration
allowance that accommodates this latency. Do not retry prepare/commit or extend
their original deadlines while waiting for the bootloader.

Use `--registry artifacts/local/registry.json` to compile your deployment’s shared
[name map](../../registry/README.md). The default is the synthetic public example.
Keep the real map ignored, alongside local enrollment; the receipt records its
hash and preserves a private build snapshot.

Version 0.15.106 adds signed serving-GM discovery to the RC owner. Compact
locator storage funds a fixed 40-byte failover state and queue arrival timestamps;
the static RAM inspection minimum remains unchanged. Run its production selector
and ingress checks with:

```sh
CC=clang python3 firmware/t096/tools/rc_gm_failover_tests.py --deps artifacts/deps --out artifacts/check-gm-failover
```

Version 0.15.105 bounds native GM scan-stop contention to a fixed 500ms
recovery interval, with public stop retries at most every 20ms. Success must be
observed before releasing scan ownership. A duplicate connection whose close
races a natural disconnect retains its reference until its actual callback;
Command Disallowed/Not Connected responses cannot alone release it. Missing
callbacks retain the original two-second failure deadline. Terminal transport
diagnostics are saved without needing a pre-fault console poll. These checks
do not extend source, authentication, election or maintenance deadlines.
Bounded native transport indices use eight-bit storage to retain the unchanged
1,024-byte static RAM inspection minimum with the additional retry state.

```sh
CC=clang python3 firmware/t096/tools/gm_drain_tests.py --deps artifacts/deps --out artifacts/check-gm-drain
```

Version 0.15.104 narrows three bounded route/admission indices without changing
their values or reducing diagnostic, stack or transport capacity. Version
0.15.103's eight-byte scheduling hint crossed a native alignment boundary and
failed the unchanged 1,024-byte static margin gate; it was not deployable.
Version 0.15.103 allows an explicit parent identity to select a direct GM as
well as a relay, with no additional static state or change to automatic selection.
Version 0.15.102 adds a source-unavailable scheduling hint to 0.15.101's
preserved retry policy and pinned AUX observations. The hint adds eight bytes
of state; native inspection still requires at least 1,024 unallocated bytes.
Version 0.15.100 introduced transport hold and earlier RC7 renewal.
Run the combined production policy and publication regressions with:

```sh
CC=clang python3 firmware/t096/tools/rc_relay_renewal_tests.py --deps artifacts/t096-deps --out artifacts/check-relay-renewal
CC=clang python3 firmware/t096/tools/rc_source_wait_tests.py --deps artifacts/t096-deps --out artifacts/check-source-wait
CC=clang python3 firmware/t096/tools/rc_transport_hold_tests.py --deps artifacts/t096-deps --out artifacts/check-transport-hold
CC=clang python3 firmware/t096/tools/initiator_aux_tests.py --deps artifacts/t096-deps --out artifacts/check-initiator-aux
```

The full host runner includes these checks. A retained periodic train carries no timing
corrections while the existing timing gates are invalid; it cannot extend a
child lease, source, map or authentication deadline. Build/native inspection and
actual protected-region/application readback remain separate hardware gates.
