# T096 firmware example

<a href="../../docs/images/t096-rc-preview.png"><img src="../../docs/images/t096-rc-preview.png" width="160" alt="T096 RC display preview with digital time, analog dial and BLE source status"></a>

*Illustrative firmware-rendered RC screen; displayed values are not measured
performance. [Image provenance](../../docs/images/README.md).*

**PToB owns T096 RC7 development.** Current version **0.15.104** supplies the
ordinary **GM** and **RC** mesh profiles, with USB timing compiled by default.
[Build from source](../../docs/t096/build.md) with pinned public dependencies
and ignored local enrollment. No enrolled binary is distributed.

T096 participates in the shared G1/RC7 mesh with W12 and StickS3 receivers and
experimental relays. RC selection considers aged timing/delivery history as
well as signal strength, gives authenticated relays time to acquire or recover,
and retains a healthy parent. Both RC1 and RC2 streams are eligible; a receiver
following RC2 stays a leaf. Earlier authentication renewal, preserved retry
backoff and source-aware recovery retain opportunities before the original
binding expires. Explicit parent identities support bounded comparisons;
normal operation uses automatic selection.

An established downstream train can survive a timing interruption for up to
30 seconds using packets with no corrections. Timing permission withdraws
immediately; source/map expiry, child leases, authentication, startup lock and
controller drain deadlines retain their bounds. A brief analog-display grace
and longer watchdog recovery allowances do not extend timing authority.
See the [changelog](../../CHANGELOG.md) for policy details and the
[selection and diagnostics guide](../../docs/t096/diagnostics.md) for checks.

RC exports its accepted BLE clock over USB independently of downstream relay
transmission. Use a compatible RC for the current
[macOS USB timing chain](../../clients/macos/README.md). The normal GM backend
has no reachable USB source-map publisher; its USB interface alone does not
establish UTC delivery. Continuous BLE/USB service and calibrated UTC/output
precision remain unqualified. The dated
[stability collection](../../evidence/stability/2026-10-16/README.md) preserves
earlier failures and its own source identities.

**[Direct Connect (DC)](../../docs/t096/direct-connect.md)** is a separate,
flash-only GNSS-to-USB image with Bluetooth compiled out. It uses no group
enrollment and cannot be selected through the GM/RC toggle. A second USB serial
function carries original GNSS output and bounded RTCM/configuration input;
raw observations depend on receiver firmware and do not imply onboard RTK.
DC and RC share an immutable USB publisher whose original source, map and
session deadlines cannot be renewed by polling.

All targets use the [shared display registry](../../registry/README.md).
[PSKs](../../docs/security/psk.md),
[identity rules](../../docs/security/identity-and-names.md) and the
[group setup guide](../../docs/GETTING_STARTED.md) describe local deployment
inputs. Real enrollment, maps and secret-bearing images remain ignored.
Mutable [provisioning/storage](../../docs/t096/provisioning-design.md) and a
generic public installation/recovery flow remain pending. Hardware updates
require separately authorized exact-device guards.

[Source provenance](imports/README.md),
[baseline limitations](../../docs/t096/baseline.md) and the
[cutover record](../../docs/t096/cutover.md) distinguish the current application
from selected historical sources. The
[V14 papers](../../papers/README.md),
[RC1 contract](../../protocol/v14_rc1/README.md) and
[R2 evidence](../../evidence/v12/r2/README.md) retain their own editions and
qualification limits. Build or simulation success does not qualify hardware.

## Timing schematic

[<img src="../../docs/images/t096-timing-schematic.jpg" width="800" alt="T096 timing architecture schematic">](https://ptob.dev/hardware/t096/timing/)

Logical capture, clock-discipline and delivery paths.
Select the preview to open the [full interactive schematic](https://ptob.dev/hardware/t096/timing/).
[Capture provenance](../../docs/images/README.md).
