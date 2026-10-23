# W12 native RC example

<a href="../../docs/images/w12-rc-preview.png"><img src="../../docs/images/w12-rc-preview.png" width="384" alt="W12 OLED preview showing time, relay parent and clock diagnostics"></a>

*Illustrative firmware-rendered OLED screen. RC2 indicates relay depth within the
RC role. [Image provenance](../../docs/images/README.md).*

Copyright (c) 2026 Jonathan Yantis. MIT; preserved source notices apply.

**PToB owns W12 native G1/RC7 development.** Current version **0.1.23** supports
**RC**: a BLE50 client or BLE100 receiver with an optional experimental
estimated-TX relay. It receives from the shared T096/W12/StickS3 mesh; it has no
GM profile or USB timing stream. Start with the
[build guide](../../docs/w12/build.md).

Channel-aware acquisition, precision-first report processing and bounded GM
lock trials support acquisition and fallback to relays. Automatic selection
retains a healthy parent and defers failed precision trials. Direct BLE100
authentication renews before its fixed six-minute expiry; newly matched
physical corrections maintain replay time within that session. An established
downstream train can persist for up to 30 seconds through timing loss, carrying
no corrections while timing permission is withdrawn. Original source/map
expiry, physical matching, startup lock, child leases and controller drain
bounds remain in force.

The estimated relay converts programmed BLE1M packet start to the nominal
ADDRESS endpoint, 40 microseconds later. This aligns packet references across
mixed targets; actual controller/RF delay remains unknown. An optional,
explicit per-device aggregate-path profile retains its total adjustment and
exact source scope. It affects every child of that publisher and does not
identify TX versus RX bias. See the
[timestamp reference](../../docs/w12/tx-reference.md).

The explicit Meshnology W12 L76K profile enables verified GNSS inputs and
PPS/fix indicators; GNSS is disabled by default and does not discipline the BLE
clock. Bounded production G1 PPS diagnostics distinguish local clock mapping,
relative phase, output correction and source permission. The optional fast
counter-map diagnostic defaults off. LoRa, WiFi, PTS and donor correction
experiments are excluded.

The optional `--fine-clock` BLE100 test profile ports N30's continuous 80 MHz
counter, capture-frozen dense fitting and 1 ppm phase slew below 2 µs. Its W12
route uses **GPIO16 for RX only**, with no TX pad; N30's GPIO4/5 route conflicts
with W12 board functions. Supported relay packets use an experimental captured
RW falling-edge phase convention; direct-GM packets retain descriptor timing
on the same counter. Counter ticks are 12.5 ns and delivered coordinates round
to 50 ns. This has no W12 hardware validation or transferred N30 calibration.
See the [fine-clock test profile](../../docs/w12/build.md#fine-clock-test-profile).

Relay-path offsets and intermittent timing loss remain under investigation.
Estimated TX has unknown RF/UTC bounds; a local OLED HOLD estimate grants no
source or downstream authority. Build/host checks and finite hardware
observations do not qualify precision, continuous BLE service or failover.
See the [target changelog](CHANGELOG.md) for development details and the
[project changelog](../../CHANGELOG.md) for the mixed-target changes. The dated
[stability collection](../../evidence/stability/2026-10-16/README.md) retains
earlier failures and separate observation populations.

The application uses pinned public dependencies and the
[shared display registry](../../registry/README.md). Factory binding, enrollment
and real maps remain ignored local inputs; no enrolled binary is distributed.
Runtime mutable provisioning and a generic public installation/recovery tool
remain pending. See the [cutover record](../../docs/w12/cutover.md),
[import provenance](imports/2026-10-16.json), [source notices](NOTICE.md) and
[group setup guide](../../docs/GETTING_STARTED.md).

## Timing schematic

[<img src="../../docs/images/esp32-timing-schematic.jpg" width="800" alt="ESP32-S3 / W12 timing architecture schematic">](https://ptob.dev/hardware/esp32-s3/timing/)

Logical receive, clock-discipline and relay paths.
Select the preview to open the [full interactive schematic](https://ptob.dev/hardware/esp32-s3/timing/).
[Capture provenance](../../docs/images/README.md).
