# Build the StickS3 RC example

Copyright (c) 2026 Jonathan Yantis. MIT.

Native builds [clean compiler output and retain the latest two image bundles](../firmware-storage.md)
per device/profile, with installed/recovery pins preserved.

PToB owns normal SS3 native G1/RC7 development. The selected M5Stack StickS3
K150 ESP32-S3 target is **RC, BLE100**, with an experimental estimated-TX relay.
It has no GM, local election or GNSS input. LoRa, WiFi, PTS and external PPS are
disabled. Programmed TX estimates carry an unknown bound; actual TX capture,
physical RF timing and output precision remain unqualified.

The current selected source is the local public SS3 tree; each build records its
exact Git revision, frozen source hashes and runtime identity. Historical imports
remain documented by the linked manifests and Git. Source version 0.1.7 selects
current coarse TZ codecs (GM7 revision4, JOIN revision5, RC control/capability
revision8), while precision payloads remain unchanged.

The software servo matches W12's bounded 64-point repeated-channel fit, 2 s
acquisition minimum, 62.5 ms physical point spacing, causal pre-update scoring,
continuous phase steering and bounded fast acquisition. It uses the existing
SS3 descriptor/GPTimer capture path. No W12 pin routing, measured offsets,
thermal learner or sampled holdover-duration certification is imported. Fine
model/source expiry, authentication and relay permission remain separate.

Direct BLE100 authentication renewal remains randomized over 225–240 s with a
six-minute session. RC7 retains five-minute renewal/six-minute lease. Fresh
matched physical pairs can maintain the coarse anchor only inside the original
causal interval and exact admitted scope; poll/duplicate activity cannot renew it.

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

## Current GM7 diagnostics, time zone and mesh epoch

SS3 uses the full receive-only current GM7 revision4 diagnostic codec, with [selected-file provenance](../../firmware/ss3/contracts/gm7_diagnostics_provenance.json).
The two-file codec is vendored in the target and normal builds/tests need no
other checkout, private source or credentials. SID14 ANN, STATUS, ROLE, WITHDRAW
and GNSS are distinct from SID12 precision traffic and legacy GS/GD1/GD2.
Authentication, canonical advertiser, separate 64-bit replay spaces, lineage/
term ordering, timing scope and original receipt expiry are checked. An admitted
timing selection takes precedence; passive TD may observe an authenticated
leader with no timing admission. No transmitter or election state is imported.

Page6 (Epoch) shows the signed **mesh birth UTC date/time**, with elapsed time
since that birth. Birth/founder identify the epoch independently of the active
GM, its boot, term and timing generation. A new leader inherits the date/timer;
SS3 receipt time and local uptime cannot replace them. The date conversion and
birth subtraction match current CoreSE. A legacy GS-only observation has no
birth and shows unknown; the incorrect 0.1.5 first-seen timer is removed.

SS3's default passive TD survey has no admitted UTC clock. Its display-only
elapsed estimate uses the signed ANN UTC timestamp plus elapsed local monotonic
time. It is labeled ELAPSED EST, with independent LIVE/LAST under that ANN's
original 2.75s receipt horizon. LAST may continue as an explicit estimate between
scan windows; no report, timing or source lease is renewed. A scope change,
withdrawal, missing/future/invalid UTC or overflow withdraws the applicable
projection. Local uptime is separate. This is second-scale descriptive epoch
tracking, not qualified UTC, RF-delay compensation or output time.

Page7 (Reference) shows reported GM7 GNSS or legacy GD data with original expiry.
Unknown accuracy/variance stay explicit; GD1 never supplies HDOP. A report's
qualification flag belongs to the reporting GM. The nine-page side-button and
ticketed `td ui-next` routes are unchanged. Existing passive radio survey timing,
controller owners, correction matching and precision permissions are unchanged.

The existing CRC-protected SS3_RC7_ID row carries GS/GD values plus `gm7_wire`,
`gm7_birth_utc_us`, `gm7_founder`, `gm7_term`, ANN/STATUS/ROLE/WITHDRAW receive
counts, `gm7_utc/utc_at`, `epoch_utc_us`, `epoch_elapsed_us`, `epoch_basis`
(0unknown/1matched UTC/2reported UTC estimate), and `epoch_current`. These are
actual receive counts and original UTC coordinates; legacy `epoch_seen` remains
local first observation for debugging only. GS/GD sequences now retain all64bits
when the current source is GM7. `gd_wire=4` means current GM7 GNSS; 1/2 are legacy GD1/GD2.
HDOP remains dimensionless centi-units or65535 unknown. `ui_page` and `pages=9`
record presentation metadata, not optical readback.

Page 8 (Time Zone) shows the signed GM ID/offset pair and its original 750 ms
lease. Missing metadata, explicit unknown, mismatched scope and expiry show UTC
fallback. Passive TD does not admit a clock. RC metadata is independently scoped
to the authenticated parent/path: ordered ANN sequence or relay challenge,
bootstrap precedence, explicit-unknown withdrawal and three-period receipt expiry
match W12. The front clock uses the current pair; diagnostic/epoch UTC stays UTC.
`SS3_RC7_ID` includes `tz_id`, `tz_offset_min`, `tz_until`, `tz_rx`, `tz_live` and
separate `rc_tz_id`, `rc_tz_offset_min`, `rc_tz_until` presentation metadata.

Run the signed monitor/epoch checks in addition to the standard offline suite:

```sh
CC=clang CXX=clang++ python3 firmware/ss3/tools/gm7_tests.py --sdk "$IDF_PATH" \
  --out artifacts/ss3-gm7-tests
```

Tests cover actual codec/HMAC, ANN/STATUS/ROLE/GNSS, 64-bit replay, corrupt MAC,
canonical advertiser, original expiry, independent HOT evidence, YIELD/WITHDRAW,
signed birth across leader changes, real UTC date/leap boundaries and elapsed
projection/freshness. Native tests exercise the real SID14 discovery dispatch;
all nine renderer pages retain empty/expired/maximal states and exact strip/
full-frame equivalence. Finite tests do not establish optical, continuous RF,
endurance, admission-renewal or independent UTC/PPS/output qualification.

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
compiler into a private receipt and runtime identity. A clean SS3/registry-generator source scope is required (unrelated working changes
are separately recorded in `repository_dirty`). A clean commit is required
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
