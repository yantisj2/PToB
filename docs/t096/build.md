# Build T096 RC7

Copyright (c) 2026 Jonathan Yantis. MIT.

There are two profiles: **GM** and **RC**. USB source streaming is always enabled.
The public builder never opens hardware. It uses only this checkout, pinned
public dependencies and your ignored local enrollment/device inputs.

The first validated build host is Apple Silicon macOS. Install Git, CMake,
Python 3.12+ and LLVM/Clang for host sanitizer tests. Linux/Windows SDK locks and
clean-host validation remain pending. The dependency acquisition command uses
public URLs and verifies SDK archive hashes; network acquisition itself must be
verified on a clean host before claiming fully reproduced setup.

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

Use `--profile GM` for a GM cold boot. The same RC7 owners and USB code are
compiled for both roles. `--development` permits an uncommitted diagnostic build;
its receipt is explicitly not flashable. A deployment candidate requires a clean
local commit and separate review. Do not upload output directories: the headers,
ELF, BIN and UF2 contain your group key. Checksums establish identity, not secrecy
or authenticity. Runtime P1 provisioning is not yet implemented.

## Checks

```sh
python3 -m unittest discover -s firmware/t096/provisioning -p 'test_*.py' -v
CC=clang python3 firmware/t096/tools/usb_sof_tests.py --out artifacts/check-usb
CC=clang python3 firmware/t096/tools/watchdog_tests.py --deps artifacts/deps --out artifacts/check-watchdog
CC=clang python3 firmware/t096/tools/runtime_profile_tests.py --deps artifacts/deps --out artifacts/check-profile
python3 firmware/t096/tools/test.py --deps artifacts/deps
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
