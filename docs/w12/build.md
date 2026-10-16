# Build W12 RC

Copyright (c) 2026 Jonathan Yantis. MIT.

PToB owns the W12 native G1/RC7 application. The public target supports **RC**:
a BLE50 or BLE100 receiver, optionally an experimental BLE100 relay. It does
not provide a GM profile. LoRa, WiFi, PTS and GNSS input are disabled. This
selection supports the Meshnology W12 ESP32-S3 display/pin profile, not arbitrary
ESP32-S3 boards or GPS-equipped variants.

The first build host is Apple Silicon macOS with Git, CMake, Make, Python and
ESP-IDF's Xtensa tools. Acquire the pinned public SDK and its toolchain:

```sh
python3 firmware/w12/tools/dependencies.py --dest artifacts/w12-deps/esp-idf
artifacts/w12-deps/esp-idf/install.sh esp32s3
. artifacts/w12-deps/esp-idf/export.sh
```

The pin is ESP-IDF v5.5.5, commit
`b774170ff46c393eeb5e495ea37936038d3f4f4f`. The builder checks the SDK revision,
cleanliness, submodule revisions, controller and GPTimer private ABI hashes.
The SDK's pinned `tools/tools.json` supplies public tool URLs and hashes.
The build uses Xtensa GCC 14.2.0 / `esp-14.2.0_20260121`. The local cache has an
uninitialized nested OpenThread mbedTLS subtree; it is explicitly recorded and
unused by this BLE-only target. No target dependency is excused by that rule. The SDK may build unused WiFi
components because its hidden WiFi Kconfig symbol defaults on. The final ELF
must contain no WiFi initialization/start or NVS write entry points.
Clean-host network acquisition and other host platforms remain unvalidated.

Create an ignored owner-only `artifacts/local/enrollment.json`, using the same
group enrollment as your other targets. Group ID is a nonzero 16-digit lowercase
hex value, epoch a nonzero uint32, and PSK 32 random bytes, not all zero expressed as
64 hex digits. Do not copy these placeholder strings literally:

```json
{"group_id":"<16 hex digits>","key_epoch":1,"psk":"<64 random hex digits>"}
```

Create `artifacts/local/w12.json` with your actual factory MAC. The following
identity is synthetic. The 64-bit wire ID currently remains `0000` followed by
the full factory MAC; aliases and UUIDs do not replace that rule.

```json
{"board":"meshnology_w12_esp32s3","factory_mac":"02:00:00:00:00:05","device_id":"0000020000000005","label":"PToB-W12","legacy_wire_id":2}
```

`legacy_wire_id` is a local compatibility field for inactive legacy diagnostics,
not an authenticated wire identity or membership grant. GNSS capability is not
inferred from a factory label. Supply the shared [display registry](../../registry/README.md)
through the same ignored map used by T096:

```sh
python firmware/w12/tools/build.py --idf "$IDF_PATH" \
  --out artifacts/w12-rc-01 --profile RC --listening-percent 100 \
  --enrollment artifacts/local/enrollment.json \
  --device-config artifacts/local/w12.json --registry artifacts/local/registry.json
```

`--estimated-relay` enables the existing BLE100 laboratory RC publisher. It
uses programmed TX estimates, advertises an unknown timing bound, and does not
claim actual TX capture or qualified relay precision. BLE50 is client-only.
The +3056 ns donor correction experiment is excluded; output correction is zero.

A candidate requires a clean local commit. `--development` explicitly marks an
uncommitted build as unflashable. Build receipts bind source, SDK, effective
configuration, generated local inputs, registry, compiler and application bytes.
All output is ignored and owner-only. Headers and binaries contain your PSK;
never publish them. Storage provisioning remains build-time; the protected
bootloader/partition/NVS prefix is preserved during a guarded update.

Run checks with a recent LLVM Clang and the activated SDK environment:

```sh
CC=clang CXX=clang++ python firmware/w12/tools/g1_host_tests.py --out artifacts/w12-host-01
python firmware/w12/tools/rc7_permission_e2e.py --sdk "$IDF_PATH" --out artifacts/w12-permission-01
python3 registry/test_registry.py
```

These checks use production codecs/models and synthetic controller boundaries.
They do not establish physical RF duty, absolute UTC accuracy, independent
output precision, or reliability. Some retained historical fixture sources are
not part of the selected runner and do not inherit a passing result.

## Hardware boundary

The public builder never opens hardware. A generic public flasher is not yet
shipped. Installation requires separately authorized exact silicon/current-image
binding, exclusive ownership, bounded application drain, identified rollback,
actual application preservation/readback and exact protected-prefix comparison.
A failed or unknown gate stops the update. Do not replace those gates with a
mass erase, partition change or guessed reset. **Do not run `idf.py flash`,
use generated `flash_args`, or copy the SDK build log’s generic flash command:**
those include bootloader and partition writes outside this app-only procedure. The migration's local C-2-only
validation uses the existing reviewed laboratory guard with outputs retained
under ignored PToB artifacts; it is not general public installation acceptance.
