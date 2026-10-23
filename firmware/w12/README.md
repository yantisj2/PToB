# W12 native RC example

<a href="../../docs/images/w12-rc-preview.png"><img src="../../docs/images/w12-rc-preview.png" width="384" alt="W12 OLED preview showing time, relay parent and clock diagnostics"></a>

*Illustrative firmware-rendered OLED screen. RC2 indicates relay depth within the
RC role. [Image provenance](../../docs/images/README.md).*

Copyright (c) 2026 Jonathan Yantis. MIT; preserved source notices apply.

**PToB owns W12 native G1/RC7 development.** Current version **0.2.2** supports
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
counter-map diagnostic defaults off. WiFi, PTS and donor correction experiments are excluded. Optional LR2021
LoRa timing is a separate authenticated passive RC8 receiver; it does not
train or publish through the BLE clock.

Version 0.2.2 follows N30's independent LoRa timestamp-reader fix. W12 keeps
its CH1 capture on the continuous MCPWM1 timer, with a separate software epoch
and counter extension that never evaluates BLE association history or fences.
It polls that counter during packet silence. GNSS retains group0 and its
verified inputs; the LoRa path never substitutes BLE clock-model coordinates.

The optional `--fine-clock` BLE100 test profile ports N30's continuous 80 MHz
counter, capture-frozen dense fitting and 1 ppm phase slew below 2 µs. Its W12
route uses **GPIO16 for RX only**, with no TX pad; N30's GPIO4/5 route conflicts
with W12 board functions. Current GR3 and RC7 PDU191 packets use an experimental captured
RW falling-edge phase convention; legacy PDU159 packets retain descriptor timing
on the same counter. Counter ticks are 12.5 ns and delivered coordinates round
to 50 ns. RF and UTC bounds remain unknown; N30 calibration is not transferred.
See the [fine-clock test profile](../../docs/w12/build.md#fine-clock-test-profile).

Version 0.2.0 adds the N30 fallback policy: AUTO starts LoRa RX after 180 s of
accumulated loss of actual BLE precision lock, then stops RX after 180 s of
continuous recovered lock. KEEP ON and KEEP OFF are volatile overrides. A fresh
RC8 UTC snapshot can supply the clock display when BLE lacks lock; BLE fitting,
source permissions and relay publication remain independent. Radio TX is blocked.

Tap cycles **Clock → Precision → LoRa → Clock**. Precision shows live causal
SYNC RMS, the relative-lock timer, clock steering in PPB and the selected counter
resolution. These are internal clock diagnostics, not independent UTC accuracy.
Hold the LoRa page for 1.5 s to change its RX mode; the Clock-page relay gesture
is preserved. The GNSS route and indicators remain available on configured units.

A separate watchdog restarts after **300 s of continuous failure to obtain valid
synchronized UTC** from either current authenticated BLE or LoRa. Fresh time clears
that timer; stale snapshots, retained HOLD and precision lock alone cannot feed it.
Explicit receiver OFF and maintenance suspend the no-time timer. Existing 120 s
continuous owner-fault supervision is retained.

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
