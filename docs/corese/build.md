# CoreS3-SE build and installation boundary

Copyright (c) 2026 Jonathan Yantis. MIT.

Read [the target README](../../firmware/corese/README.md). Version0.1.15 provides
the live client; the earlier UI preparation build remains an offline design tool.

## Dependencies and offline checks

Use ESP-IDFv5.5.5 at `b774170ff46c393eeb5e495ea37936038d3f4f4f`, with Xtensa
GCC14.2.0 `esp-14.2.0_20260121`, as in [N30](../n30/build.md). Public acquisition:

```sh
python3 firmware/n30/tools/dependencies.py --dest artifacts/corese-sdk/esp-idf
export IDF_TOOLS_PATH="$PWD/artifacts/corese-sdk/tools"
artifacts/corese-sdk/esp-idf/install.sh esp32s3
. artifacts/corese-sdk/esp-idf/export.sh
python firmware/corese/tools/dependencies.py --dest artifacts/corese-deps
python3 firmware/corese/tools/preview.py --out artifacts/corese-preview
python3 firmware/corese/tools/tests.py --out artifacts/corese-tests --sdk artifacts/corese-sdk/esp-idf --dependencies artifacts/corese-deps
IDF_PATH="$PWD/artifacts/corese-sdk/esp-idf" python3 firmware/w12/tools/g1_host_tests.py --out artifacts/shared-g1-tests
IDF_PATH="$PWD/artifacts/corese-sdk/esp-idf" python3 firmware/corese/tools/recovery_tests.py --out artifacts/corese-recovery-tests --objects artifacts/shared-g1-tests
python3 registry/test_registry.py
```

M5GFX0.2.28 and M5Unified0.2.11 are pinned by
[dependencies.lock.json](../../firmware/corese/dependencies.lock.json).
The build checks the clean SDK, controller/private timer ABI and raw MCPWM ABI.
It disables component-manager downloads. No sibling private checkout is needed.

## Live client build

Create ignored, owner-only deployment inputs. The shared enrollment has exactly
`group_id` (16 lowercase hex digits), `key_epoch` (nonzero uint32), and `psk`
(32 bytes as hex). A CoreSE device input has exactly `board=m5stack_cores3_se`,
`factory_mac`, `device_id` (0000 plus the factory MAC without colons), `label`
and an unused `legacy_wire_id` (1–254). The shared registry must have that exact
ID with the same alias and `target=corese`. Preserve existing rows and UUIDs.
Never select a USB destination by display alias.

```sh
python firmware/corese/tools/live_build.py \
  --idf artifacts/corese-sdk/esp-idf \
  --dependencies artifacts/corese-deps \
  --out artifacts/corese-client \
  --enrollment artifacts/local/enrollment.json \
  --device artifacts/local/corese-device.json \
  --registry artifacts/local/registry.json
```

The live build does not open USB. The shared receiver source is pinned to public
Git revision `699f100c4e01db563f8e4b0a9f8d01d15e8600a4`; concurrent work on other
targets cannot silently change this receiver baseline. The GD1/GD2 diagnostic codec
is a separately frozen current public dependency; it does not advance that
receiver baseline. Version0.1.11 applies the narrowly scoped
[`auto_rc9_recovery.patch`](../../firmware/corese/tools/auto_rc9_recovery.patch)
from `6ac2721becf37b3c5d22919eea9e25e24a4054cf` to that frozen baseline before
generating the CoreSE overlay. The receipt records the patch hash and
`AUTO_RC9_PASSIVE_NORMAL_RETRY_V1` policy. No other target's timing offsets or
coordinate changes are imported. It freezes public sources, generated target
local overlays, private compiler inputs and hashes below ignored output. Runtime
build identity binds the source closure, effective config, controller and local
headers. The app is `target/ptob_corese.bin`; the receipt's inspector checks its
identity, PSRAM/client settings, relay exclusion and absent Wi-Fi runtime owner.
Source snapshots may include uncommitted development; an exact operator-reviewed
candidate and hardware authorization are still required before installation.

Version0.1.15 applies the four-file
[`leader_recovery.patch`](../../firmware/corese/tools/leader_recovery.patch)
from `6fd28f952f2787df9491fcda65a71ed0d4f3fa35`, recorded separately as
`shared_source_leader_recovery` with policy
`FENCED_SCOPE_CACHE_CLEAR_1500MS_V2`. It clears old sessions, estimators,
locators, selection history, cooldowns and pending timing after sustained
source-permission loss, with bounded recovery admission. It also fences old
normal-source history at authenticated RC9 takeover and direct-root admission
after an empty scope. Learned parents and models remain RAM-only; a fresh boot
requires live discovery and authentication. Original controller
owners and drain deadlines remain real and must complete. Target-local
source-bound capability/residual/scan observations are withdrawn on scope loss.
Explicit historical GM reports retain their original expiry and cannot grant
authority. No other target's phase adjustment or counter coordinate transfers.

The fixed fine route drives otherwise unused GPIO6/7 with internal controller
signals. Confirm a bare CoreS3-SE with both pads free before enabling it. Never
substitute N30 GPIO4/5 or a board calibration. Graphics are in 8MiB quad PSRAM;
CPU0 retains the timing/BLE owner, CPU1 owns LCD and touch.

## Exact-device installation

The [manufacturer specifications](https://docs.m5stack.com/en/core/CoreS3-SE)
and [schematic](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/498/Sch_M5_CoreS3_SE_v1.0.pdf)
identify16MiB flash,8MiB quad PSRAM, ILI9342C LCD and FT6336U touch. Public M5Stack
factory recovery is linked from that product page. Verify the downloaded recovery
image digest and partition compatibility before relying on it. No device backup
is implied or required for an authorized factory-first installation.

Before a write: verify VID/PID/USB serial and factory ROM MAC, exclusive ownership,
security state, flash size and actual partition metadata. A factory app is halted
by ROM reset; an installed PToB app must first finish its bounded maintenance drain.
Use an app-only writer restricted to the selected slot, preserving any bytes past
the final new-app byte within its last erased sector. Verify the complete actual
new application by readback, compare the protected prefix, and compare that tail.
Never run the broad `idf.py flash` recipe for an app-only installation: it includes
bootloader, partition and OTA metadata writes.

After boot, verify nonce/boot/source/build-bound complete CRC console responses,
PSRAM/display/touch initialization, current source and fine-counter health. Capture
`mesh diag` to verify independently advancing GD2 receive sequence/counters, actual HDOP known/value and
exact selected root/timing incarnation/mesh epoch, along with original expiry.
Producer advertisement counters alone do not establish client RF reception.
Capture
the current screen from the actual completed LCD framebuffer, and all eight
presentations with `ui render 0` through `ui render 7` using the on-device live
model and production renderer, without changing the visible screen. These
offscreen renders are explicitly identified as `ONDEVICE_LIVE_MODEL_RENDER`;
`ui snap` identifies `COMPLETED_LCD_FRAME`. Neither is optical readback. The bounded
presentation commands are `ui page 0` through `ui page 7`, `ui snap`, and
`ui capture` (read frozen metadata), and `ui pixels OFFSET`; they use fresh maintenance tickets and do not change timing
source, radio permission or model lifetime. Pixel chunks carry a frozen snapshot
ID and CRC. Page1 is the normal PToB instrument; page0 is the launcher.

Keep identities, keys, raw logs, enrolled firmware images and acceptance receipts ignored. Record
which transports and states were actually observed. Software tests of RC9 gates
or synthetic HOT/path fixtures do not establish live RC9 or failover acceptance.
Absolute UTC/RF endpoint accuracy requires separate independent measurement.
