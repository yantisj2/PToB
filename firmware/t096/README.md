# T096 firmware example

<a href="../../docs/images/t096-rc-preview.png"><img src="../../docs/images/t096-rc-preview.png" width="160" alt="T096 RC display preview with digital time, analog dial and BLE source status"></a>

*Illustrative firmware-rendered RC screen; displayed values are not measured
performance. [Image provenance](../../docs/images/README.md).*

T096 firmware, developed from the R2 application, supplies the GM and RC
examples in this repository. Its direction comes from v12: periodic advertising
for the precision timing stream,
with mesh participation separated from planned client services for less capable
devices. See [the architectural background](../../docs/V12_TO_R2.md).

**PToB owns T096 RC7 development.** The two profiles are **GM** and **RC**;
USB timing support is compiled by default. [Build from source](../../docs/t096/build.md)
using pinned public dependencies and ignored local enrollment. The current
application version is 0.15.76. No publicly enrolled binary is distributed.

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
