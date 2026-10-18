# StickS3 native RC example

<a href="../../docs/images/ss3-health-preview.png"><img src="../../docs/images/ss3-health-preview.png" width="135" alt="StickS3 network-health preview with synthetic peers and source permission"></a> <a href="../../docs/images/ss3-clock-preview.png"><img src="../../docs/images/ss3-clock-preview.png" width="135" alt="Earlier StickS3 relay-clock preview with digital time and an analog dial"></a>

*Network-health view (V17 renderer) and an earlier relay-clock view, both with
illustrative inputs. These are not current-build hardware captures or measured
accuracy. [Image provenance](../../docs/images/README.md).*

Copyright (c) 2026 Jonathan Yantis. MIT; inherited source notices apply.

**PToB owns SS3 native G1/RC7 development.** Source firmware version **0.1.2**
builds on imported source `56740553885420a7f6f301c12de10696a9c8d432`, the M5Stack StickS3
K150 V17 application with TD diagnostics and a 150 ms health-display hold.
The Relay Test screen keeps the four strongest nearby relays visible, ordered
by fresh smoothed RSSI with stable identity ties. It shows the displayed/nearby
count; missing signal sorts last and expired observations leave the list.
See the [standalone build guide](../../docs/ss3/build.md),
[cutover record](../../docs/ss3/cutover.md),
[explicit import provenance](imports/2026-10-16.json) and [notices](NOTICE.md).

The supported profile is **RC at BLE100**, including an experimental estimated-TX
relay. No GM, local election, GNSS, external PPS, LoRa, WiFi or PTS service is
provided. The relay advertises an unknown TX timing bound and does not claim
actual TX capture. Source expiry, physical receive/correction matching,
controller ownership and bounded update drain remain independent of display
health. A 150 ms LCD hold never extends timing permission.

Direct BLE100 authentication renews after 225–240 seconds, with a fixed
six-minute session. Fresh matched GR3 physical corrections maintain the coarse
replay anchor inside that session; signed source/map expiry, uncertainty and
ownership limits remain independent. RC7 admission remains five minutes with a
six-minute lease. This successor is prepared and checked offline; C-4 W12
observations do not qualify SS3 hardware.

The target consumes the [shared display registry](../../registry/README.md).
Factory binding, group enrollment and enrolled images are ignored local inputs.
No public enrolled binary or generic installation/recovery tool is supplied.
The USB interface is a diagnostic CDC console, not the T096 UTS1 timing service
used by the macOS client. Build/tests do not qualify physical RF timing,
independent output accuracy, optical scanout or continuous reliability.

For a complete group, follow the [two-GM setup guide](../../docs/GETTING_STARTED.md).

## Timing schematic

[<img src="../../docs/images/esp32-timing-schematic.jpg" width="800" alt="ESP32-S3 / W12 timing architecture schematic">](https://ptob.dev/hardware/esp32-s3/timing/)

Shared-family reference illustrated for W12; its optional GNSS/PPS and other
W12-specific paths do not imply StickS3 support. See the supported profile above.
Select the preview to open the [full interactive schematic](https://ptob.dev/hardware/esp32-s3/timing/).
[Capture provenance](../../docs/images/README.md).
