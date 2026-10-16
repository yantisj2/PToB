# Build the StickS3 RC example

Copyright (c) 2026 Jonathan Yantis. MIT.

PToB owns normal SS3 native G1/RC7 development. The selected M5Stack StickS3
K150 ESP32-S3 target is **RC, BLE100**, with an experimental estimated-TX relay.
It has no GM, local election or GNSS input. LoRa, WiFi, PTS and external PPS are
disabled. Programmed TX estimates carry an unknown bound; actual TX capture,
physical RF timing and output precision remain unqualified.

The selected source is `56740553885420a7f6f301c12de10696a9c8d432`, V17 with a
150 ms display-health hold. Public successor version 0.1.1 replaces private
factory/name tables with explicit local binding and the shared display registry.
These changes create new application bytes; they do not inherit the historical
installed image's hardware observations.

## Dependencies and local inputs

Use Git, CMake, Make and Python with ESP-IDF's Xtensa environment. The SDK pin is
ESP-IDF v5.5.5, commit `b774170ff46c393eeb5e495ea37936038d3f4f4f`;
Xtensa GCC 14.2.0 / `esp-14.2.0_20260121`. Controller and GPTimer private ABI
hashes are enforced. The SDK provides pinned public tool URLs/hashes. Apple
Silicon macOS is the validated build host. Fresh public acquisition and a
clean-checkout build are recorded in the [public-source review](../PUBLIC_RELEASE_REVIEW.md);
a freshly installed OS and other host platforms remain unvalidated.

```sh
python3 firmware/ss3/tools/dependencies.py --dest artifacts/ss3-deps/esp-idf
export IDF_TOOLS_PATH="$PWD/artifacts/esp-tools"
artifacts/ss3-deps/esp-idf/install.sh esp32s3
. artifacts/ss3-deps/esp-idf/export.sh
```

An existing public SDK cache can be supplied through `--idf`; its exact commit,
cleanliness, initialized submodule revisions and private ABI hashes are checked.
The optional unused OpenThread nested mbedTLS subtree has the same explicitly
recorded cache exception as W12. No active BLE dependency is waived.
No source path depends on a laboratory sibling checkout. Component-manager
network fetching is disabled; the selected application has no managed component.

Create owner-only ignored local files. Use the same group enrollment as T096/W12:

```json
{"group_id":"<nonzero 16 lowercase hex digits>","key_epoch":1,"psk":"<64 random hex digits>"}
```

Device binding is independent of aliases. The following factory MAC is synthetic:

```json
{"board":"m5stack_sticks3_esp32s3","factory_mac":"02:00:00:00:00:08","device_id":"0000020000000008","label":"PToB-SS3","legacy_wire_id":8}
```

The wire ID remains `0000` followed by the full factory MAC. Supply your actual
MAC and matching ID in `artifacts/local/ss3.json`; keep enrollment in
`artifacts/local/enrollment.json`. `legacy_wire_id` is an inactive compatibility
field, not authenticated membership. Use the shared schema-1 registry for names:

```sh
python firmware/ss3/tools/build.py --idf "$IDF_PATH" \
  --out artifacts/ss3-rc-01 --profile RC \
  --enrollment artifacts/local/enrollment.json \
  --device-config artifacts/local/ss3.json --registry artifacts/local/registry.json
```

The default registry is the synthetic public example. The selected builder always
includes BLE100 estimated relay, zero output correction and TD startup. It binds
source, SDK, effective configuration, factory binding, enrollment, registry and
compiler into a private receipt and runtime identity. A clean commit is required
for a candidate; `--development` marks a dirty build unflashable. Generated
headers and images contain the group PSK. Output is owner-only under ignored
`artifacts/`; do not distribute it. Runtime mutable provisioning is not supplied.

## Offline checks

Use recent LLVM Clang with ASan/UBSan, with the SDK environment activated:

```sh
CXX=clang++ python3 firmware/ss3/tools/test.py --out artifacts/ss3-host-01
python3 firmware/ss3/tools/rc7_permission_e2e.py --sdk "$IDF_PATH" \
  --cc clang --out artifacts/ss3-permission-01
python3 registry/test_registry.py
```

The first runner exercises production TD model, health presentation, renderer,
signal filter, publication ownership, clock expiry and lifecycle models. The
second compiles actual codecs and the common native client with synthetic radio
callbacks: 36 permission/model cases, nine native boundary cases, compiled wire
vectors and independent HMAC checks. It does not instantiate the complete SS3
PSRAM/controller/USB stack. Tests do not establish optical scanout, physical
airtime, calibrated UTC, field reliability or independent output precision.

## Installation boundary

The builder never accesses hardware. Generic public installation/recovery is
pending. Do not run `idf.py flash`, use generated `flash_args`, or copy the SDK's
generic flash command: those write bootloader/partition regions outside an
app-only update. Hardware work needs separately authorized exact silicon/image
identity, exclusive ownership, supported drain/recovery, identified rollback,
actual application readback and exact protected-prefix preservation. No hardware
access or installation occurred during this migration.
