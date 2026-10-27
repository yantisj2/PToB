# Build the StickS3 RC example

Copyright (c) 2026 Jonathan Yantis. MIT.

PToB owns normal SS3 native G1/RC7 development. The selected M5Stack StickS3
K150 ESP32-S3 target is **RC, BLE100**, with an experimental estimated-TX relay.
It has no GM, local election or GNSS input. LoRa, WiFi, PTS and external PPS are
disabled. Programmed TX estimates carry an unknown bound; actual TX capture,
physical RF timing and output precision remain unqualified.

The selected source is `56740553885420a7f6f301c12de10696a9c8d432`, V17 with a
150 ms display-health hold. Initial public successor version 0.1.1 replaced private
factory/name tables with explicit local binding and the shared display registry.
These changes create new application bytes; they do not inherit the historical
installed image's hardware observations.

Source version 0.1.2 prepares the direct BLE100 authentication-session repair:
renewal is randomized over 225–240 seconds and authentication expires after six
minutes. A newly matched GR3 physical correction can maintain the coarse anchor
only inside the prior live causal interval and exact admitted scope. The
original JOIN uncertainty plus physical enclosure, signed source/map expiry,
250 ms uncertainty ceiling and original owner/drain bounds remain. Receipt,
polls, held predictions and duplicate tuples cannot extend authentication.
RC7 remains five-minute renewal/six-minute lease. Offline checks include the
actual matcher and native receive path; SS3 hardware installation/qualification
requires its own authorized device scope.

Version 0.1.3 updates GM diagnostics during existing passive survey windows.
The worker checks at most one cached signed report per second, retaining MAC,
locator, selected-GM, boot, source-epoch, freshness and replay guards. New HOT
identity/boot observations need ten seconds of dwell; observed withdrawal and
major scope/membership changes bypass the two-minute routine refresh. This is
an observed diagnostic dwell across scans, not proof of continuous HOT service.
No scan windows, radio/task priorities or precision-packet paths change.
`SS3_GM_STATUS` exposes event counts and the unchanged routine deadline.

The PToB splash paints before USB PHY restoration and BLE initialization. The
LCD task retains it for at least one second without blocking initialization;
`SS3_STARTUP` records splash and first-dashboard times for verification.

Version 0.1.4 repairs a reproduced survey-cleanup race: an asynchronous create
failure during the eight-millisecond cancel grace could leave a cleanup request
with no active owner and no queued worker. Cleanup now retires that request;
each survey phase closes once and faults if its original two-second drain fails.
No closure is fabricated and no source, authentication or timing lease is extended.

LCD transactions use the SDK's queued DMA path with finite 10 ms admission and
20 ms completion waits. The persistent descriptor, command bytes and pixel
buffer remain owned until the actual completion returns, including after a
timeout. A later completion permits a full repaint; unknown ownership fails
closed for existing watchdog recovery. LCD progress now has its own watchdog
bit, using the existing two-second stall detection and 120-second fault interval.
Intentional screen sleep stays healthy. The existing LCD/host priorities remain.
`SS3_RC7_ID` includes compact CRC-protected telemetry: `lcd` lists successful
frames, frame errors, worker ms, paint ms, I/O timeouts, I/O errors and pending;
`tds` lists survey generation, closing ms, sync handle, phase and owner flags.
The millisecond counters wrap after about 49.7 days; compare ages modulo 32 bits.

## Current diagnostics and epoch display (0.1.5)

The target now receives the same public GS v1 and GD1/GD2 diagnostic records as
CoreSE. The [GD2 contract](../../protocol/gm_diagnostics_v2/README.md) and
[SS3 source provenance](../../firmware/ss3/contracts/diagnostics_provenance.json)
identify the selected current codec separately from the original imported
baseline. A bounded one-second worker uses existing passive survey scans, with
no new scan schedule or radio owner. Canonical address, fresh observed GM,
authenticated root timing incarnation, mesh epoch, original TTL and replay
checks remain mandatory. Source/physical observations take precedence when
known; an authenticated serving-root GS can bootstrap the diagnostic scope
before periodic sampling. This is an observed survey scope, not JOIN admission.

The side button cycles all eight TD pages. Page6 (Epoch) shows the full mesh epoch,
local first-seen duration, estimated GM uptime from the signed remote monotonic
sample, and GS LIVE/LAST. The epoch's start is not transmitted. Like CoreSE,
local seen age survives a primary replacement with the same epoch label and
resets on a different label. Page7 (Reference) shows reported GNSS fix/PPS/
qualification, SV, HDOP and the PTP-shaped election tuple. Unknown accuracy and
variance stay explicit; GD1 never supplies HDOP. Reference values expire at their
original receipt deadline and disappear on observed scope mismatch.

`v12 status NONCE` includes compact GS/GD fields in the existing CRC-protected
`SS3_RC7_ID` row: `mesh_root/boot/epoch`, `gs_rx/seq/at/until/created`, `epoch_seen`,
`gd_root/boot/epoch`, `gd_rx/seq/at/until/sample`, `gd_wire/fields/values/sv/hdop`,
remote sample ages, PTP tuple and rejection counters. Ages are microseconds unless
suffixed `_ms`; `gd_hdop` is dimensionless centi-HDOP or65535 unknown. These are
receiver counters, distinct from producer advertisement counts. `gs_live` and
`gd_live` additionally require a fresh presentation snapshot and matching
observed GM scope. `ui_page` records the presentation; `pages=8`. Existing ticketed
`td ui-next` follows the same physical-button route and changes no timing policy.

The actual native diagnostic regression covers signed GS/GD2 and legacy GD1,
packet ordering, canonical-address binding, MAC corruption, duplicate/replay,
future/stale receipts, original deadlines, scope loss, root replacement and local
epoch age. Renderer checks include all eight screens, empty/expired/maximal
states and exact strip/full-frame equivalence. These are offline checks, not
physical RF, optical or timing qualification.

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
CC=clang CXX=clang++ python3 firmware/ss3/tools/td_role_tests.py --sdk "$IDF_PATH" \
  --out artifacts/ss3-role-01
CC=clang CXX=clang++ python3 firmware/ss3/tools/durability_tests.py --sdk "$IDF_PATH" \
  --out artifacts/ss3-durability-01
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
