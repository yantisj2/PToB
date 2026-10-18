# T096 firmware example

<a href="../../docs/images/t096-rc-preview.png"><img src="../../docs/images/t096-rc-preview.png" width="160" alt="T096 RC display preview with digital time, analog dial and BLE source status"></a>

*Illustrative firmware-rendered RC screen; displayed values are not measured
performance. [Image provenance](../../docs/images/README.md).*

T096 firmware, developed from the R2 application, supplies the GM and RC
examples in this repository. Its direction comes from v12: periodic advertising
for the precision timing stream,
with mesh participation separated from planned client services for less capable
devices. See [the architectural background](../../docs/V12_TO_R2.md).

**PToB owns T096 RC7 development.** The ordinary mesh profiles are **GM** and **RC**;
USB timing support is compiled by default. [Build from source](../../docs/t096/build.md)
using pinned public dependencies and ignored local enrollment. The current
application version is 0.15.97. No publicly enrolled binary is distributed.

The 0.15.97 RC field candidate collects ten seconds of host-owned discovery
exposure, with a 60-second search ceiling. Host ownership is not measured RF
airtime. It can select an idle authenticated relay and ranks confidence-weighted
usable-time/delivery history ahead of small RSSI differences. Quality confidence
halves over five minutes; observations expire after 30 minutes.

Relay admission has 180 seconds. Acquisition has 300 seconds, starting again at
first PA establishment, inside one 600-second ceiling from selection. A settled
relay has 300 seconds to recover degraded timing; stronger advertisements and a
returning GM cannot evict a healthy selected parent. Exhausted relay trials use
30/60/120-second probation. Same-parent renewals, repeated PENDING and repeated
PA establishment cannot rearm these budgets. Output source/map validity remains
independent. RC7 management discovery stays available during timing loss, while
periodic timing and UCS1 discovery keep their existing precision gates. These
are field-candidate policies, with durability still to be measured.

Relay GATT admission discovers the characteristic handle before reading its
212-byte capability. A receiver using an RC2 parent stays a leaf and withdraws
mesh admission advertising. Direct-GM authentication renews after four minutes;
relay renewal remains five minutes, with neither extending an existing binding.
See the [selection policy and
offline checks](../../docs/t096/diagnostics.md#rc-parent-selection-and-relay-dwell).

**[Direct Connect (DC)](../../docs/t096/direct-connect.md)** is a distinct,
flash-only GNSS-to-USB image for a dedicated macOS source. It compiles Bluetooth
out, uses no group enrollment and keeps the clock display with USB SOF RMS.
It cannot be selected through the GM/RC toggle. Exact-device local packages are
validation candidates; hardware durability, UTC precision and a generic website
release remain separate acceptance work.

The DC development source also adds a second USB serial function carrying the
receiver's original output and bounded RTCM/configuration input. It keeps the
precision USB interface separate. Raw observations depend on receiver firmware;
this transport does not establish an onboard RTK solver or an RTK fix. See the
[DC serial contract](../../docs/t096/direct-connect.md#gnss-serial-and-correction-transport).

DC and RC share the immutable USB publisher: unchanged owner polls preserve an
in-flight request and never renew its original source/map/session deadlines.
RC exports its accepted BLE clock independently of BLE relay transmission;
its source discipline and runtime GM/RC switch remain separate from USB.
Use the RC backend for the current macOS USB source chain. The reviewed normal
GM backend has no reachable USB source-map publisher; a USB interface or healthy
RF/GPS service alone does not establish USB UTC delivery. The latest
[stability results](../../evidence/stability/2026-10-16/README.md) separate bounded
admission improvements from the unresolved TC-04 USB blackout. See the
[project changelog](../../CHANGELOG.md) for the integrated changes.

[Sync diagnostics and console logging](../../docs/t096/diagnostics.md) describe
the RAM event journal, established-link transitions, and bounded offline/live
capture commands. A build or short recovery observation does not establish
continuous timing-chain reliability.

[Source provenance](imports/README.md),
[baseline and historical limitations](../../docs/t096/baseline.md),
[PSKs](../../docs/security/psk.md), and
[name maps/device IDs](../../docs/security/identity-and-names.md) explain the
source and enrollment boundaries. [P1 provisioning/storage](../../docs/t096/provisioning-design.md)
remains a draft with an offline model; the firmware still uses build-time enrollment.

The [V14 papers](../../papers/README.md) and [RC1 contract](../../protocol/v14_rc1/README.md)
are now included. Their selected 0.14.43 research source is a historical identity,
not the migration baseline or a statement about current installed firmware.

The [R2 evidence collections](../../evidence/v12/r2/README.md) contain dated
experiment records; they are not a firmware distribution.

The target includes the application, board definition, required patches,
pinned dependencies and host tests.
A fresh checkout must build without a private sibling repository. Enrollment
is supplied or generated locally; private bench keys and enrolled images are
excluded from publication.

Hardware support and verified behaviors will be recorded against exact source
and image identities. Build or simulation success alone does not qualify hardware
precision. See the [local cutover record](../../docs/t096/cutover.md).

All target families share the [display registry](../../registry/README.md).
Supply the same local registry to each migrated target; T096 consumes it today.

For a multi-device deployment, see the [group setup guide](../../docs/GETTING_STARTED.md).

## Timing schematic

[<img src="../../docs/images/t096-timing-schematic.jpg" width="800" alt="T096 timing architecture schematic">](https://ptob.dev/hardware/t096/timing/)

Logical capture, clock-discipline and delivery paths.
Select the preview to open the [full interactive schematic](https://ptob.dev/hardware/t096/timing/).
[Capture provenance](../../docs/images/README.md).
