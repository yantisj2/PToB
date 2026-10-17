# Build W12 RC

Copyright (c) 2026 Jonathan Yantis. MIT.

PToB owns the W12 native G1/RC7 application. The public target supports **RC**:
a BLE50 or BLE100 receiver, optionally an experimental BLE100 relay. It does
not provide a GM profile. LoRa, WiFi and PTS are disabled. GNSS input is disabled
by default; the explicit indicator profile below enables the reviewed Meshnology
W12 L76K route. This selection does not support arbitrary ESP32-S3 boards or
GPS-equipped variants.

The first build host is Apple Silicon macOS with Git, CMake, Make, Python and
ESP-IDF's Xtensa tools. Acquire the pinned public SDK and its toolchain:

```sh
python3 firmware/w12/tools/dependencies.py --dest artifacts/w12-deps/esp-idf
export IDF_TOOLS_PATH="$PWD/artifacts/esp-tools"
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
Fresh public acquisition and a clean-checkout build are recorded in the
[public-source review](../PUBLIC_RELEASE_REVIEW.md). A freshly installed OS and
other host platforms remain unvalidated.

The builder disables ESP-IDF's component manager: this selected target has no
managed components. Builds use the pinned SDK and local source, without implicit
component downloads or the manager's host process-enumeration requirement.

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
inferred from a factory label. For a Meshnology W12 with the L76K module and
verified RX39/PPS41 wiring, add this explicit field to the ignored device input:

```json
"gnss_profile": "meshnology_w12_l76k_rx39_pps41"
```

This selects board profile 12 and the existing 9600-baud UART RX39 / hardware
PPS41 input path. GPIO48 LOW enables the module supply; UART TX38, WAKE40 and
RESET42 remain inputs. It sends no GNSS commands, time assistance or NVM writes.
The generated factory capability and compiler flags must agree, and the build
inspector checks the actual configuration. Verify this physical route before
enabling it on a device; an enclosure, alias or initialized UART is not wiring
or reception evidence. Unrecognized profile strings and pin overrides fail.

On this profile, the top-right lamps show, from top to bottom: physical PPS,
checksum-valid GNSS packets, and a fresh valid RMC fix. PPS/packet pulses last
100 ms; the fix indication expires with the receiver's RMC freshness gate.
Missing or stale GNSS leaves these lamps dark even if BLE is active. The BLE
lock header retains its separate relative-clock meaning. Without this explicit
profile, the existing BLE PPS/RX/TX activity display remains available. GNSS
inputs do not grant source authority or qualify UTC/output precision.

Supply the shared [display registry](../../registry/README.md)
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
Version 0.1.1 requests 100 ms for both RC discovery advertisers, matching T096;
the separate precision train stays at 62.5 ms. Downstream advertising and
publication wait for current parent clock lock and withdraw when that lock or
source is lost. Upstream reception remains available to acquire the parent.
See the [changelog](../../firmware/w12/CHANGELOG.md) for the lock prerequisites
and the distinction between relative quality and qualified precision.
Version 0.1.8 starts continuous BLE100's direct G1 proactive replay refresh at
150 ms uncertainty, with urgency at 175 ms. This supersedes 0.1.2's 200/225 ms
thresholds, which left about 25 seconds before replay expiry despite the
20–40 second warm failure backoff. The new threshold reserves about 50 seconds
for a failed two-second exchange, maximum backoff and another two-second
exchange. The rolling two-starts-per-60-seconds cap remains; two failures can
still exhaust that opportunity. The original 250 ms hard replay limit and
signed source/model expiry stay intact. BLE50 retains its 100/125 ms thresholds.
Normal RC7 admission renewal is five minutes, with a six-minute lease. Direct
G1 JOIN replay refresh, pending admission and recovery after missing corrected
clock progress are separate traffic classes; inspect their counters separately.

Version 0.1.6 preserves an acquired periodic receiver through authenticated
NO_SOURCE and unusable control replies. Positive foreign roots are counted
separately. After the first physical corrected update, failed retries wait a
random 20–40 seconds and a persistent rolling budget permits at most two
connection starts per 60 seconds. These waits cannot withdraw a still-eligible
publisher; actual ACL arbitration and the original parent-lock gate remain.

The last delivered physical model can continue the OLED's local HOLD estimate
when source or replay permission expires. Its error bound is unknown; FOLLOW,
root authority, relative lock and downstream timing retain their original gates.
A local estimate never renews a source, lease, replay context or fitter. Explicit
stop, fault or detected gross clock step withdraws it. No USB timing output is
implemented by W12.

Bounded capture diagnostics add `W12_G1_ADMISSION` and `W12_G1_RX_LOSS`. The latter
freezes up to eight worker report outcomes at corrected-model expiry, using
160 bytes of RAM. Outcome bits are report=1, stale=2, authenticated=4,
associated=8, mapped=16 and refused=32. These are observed reports, not a count
of unreported RF opportunities; controller DONE/CRC remain unknown. A fresh
physical map does not overwrite frozen evidence: the first loss remains for the
boot, with explicit counters for later expiries/reacquisitions and unavailable
later detail. The ordinary V3 status frame remains the
same size and does not enable capture by itself.

Version 0.1.7 isolates large status snapshots from the main loop and command
parser. The 0.1.6 attached candidate overflowed task main on its initial status
request despite passing offline checks; that candidate is withdrawn. The main
stack remains 12,288 bytes. New builds and standalone inspection check retained
GCC `.su` frames on selected main-task paths, including the nested status path,
with 4,096 bytes reserved for uncounted callees. Missing, ambiguous or dynamic
frame evidence fails closed. This is a regression bound, not a complete
call-graph proof; observe `main_stack_free_words` on the exact installed image
through normal status and bounded capture before accepting operational behavior.
That historical console key reports **bytes** on the pinned ESP-IDF Xtensa port
(`StackType_t` is one byte); do not multiply it by four.

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
The permission runner also exercises the native relay server owner with
synthetic radio/TX and parent-lock inputs, including startup hold, lock loss,
reacquisition and unchanged admission drain deadlines.

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
