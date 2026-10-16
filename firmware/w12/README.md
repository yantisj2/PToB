# W12 native RC example

<a href="../../docs/images/w12-rc-preview.png"><img src="../../docs/images/w12-rc-preview.png" width="384" alt="W12 OLED preview showing time, relay parent and clock diagnostics"></a>

*Illustrative firmware-rendered OLED screen. RC2 indicates relay depth within the
RC role. [Image provenance](../../docs/images/README.md).*

Copyright (c) 2026 Jonathan Yantis. MIT; preserved source notices apply.

**PToB owns W12 native G1/RC7 development.** Current version **0.1.5** builds on the initial 0.1.0 successor
from the selected native application at `48b9ab7`. See the
[build guide](../../docs/w12/build.md), [cutover record](../../docs/w12/cutover.md),
[explicit import provenance](imports/2026-10-16.json) and [source notices](NOTICE.md).

The supported public profile is **RC**: BLE50 client-only or BLE100 receiver,
with an optional experimental estimated-TX relay. An explicit Meshnology W12
L76K hardware profile enables GNSS reception/PPS/fix indicators; it does not
provide GM service or GNSS discipline of the BLE clock. GNSS is disabled by
default. LoRa, WiFi, PTS and donor output correction experiments are not supplied
by this target. Original signed source expiry, physical receive/correction matching,
controller ownership, bounded update drain and precision refusal remain in force.
The estimated relay has unknown TX uncertainty and no actual TX capture claim.
Both RC discovery advertisers request 100 ms, matching T096. The independent
precision train remains 62.5 ms (16 Hz), with its existing parent-relative phase.
See the [changelog](CHANGELOG.md).

The [shared registry](../../registry/README.md) supplies display aliases. Device
factory binding, group enrollment and real maps are explicit ignored local
inputs. No enrolled binary is distributed and runtime mutable provisioning is
not implemented. Build/host-test success is separate from physical precision,
absolute UTC, RF duty and reliability qualification.

The selected source includes the application under `main/`, a closed historical
display/GNSS include boundary under `legacy/`, contracts and host checks, pinned
public SDK acquisition, and a no-hardware builder. The legacy code path supplies
compatibility types and display routines; its presence does not activate other
radios or GPS. A generic public installation/recovery tool remains pending.

For a multi-device deployment, see the [group setup guide](../../docs/GETTING_STARTED.md).

## Timing schematic

[<img src="../../docs/images/esp32-timing-schematic.jpg" width="800" alt="ESP32-S3 / W12 timing architecture schematic">](https://ptob.dev/hardware/esp32-s3/timing/)

Logical receive, clock-discipline and relay paths.
Select the preview to open the [full interactive schematic](https://ptob.dev/hardware/esp32-s3/timing/).
[Capture provenance](../../docs/images/README.md).
