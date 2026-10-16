# StickS3 native RC example

Copyright (c) 2026 Jonathan Yantis. MIT; inherited source notices apply.

**PToB owns SS3 native G1/RC7 development.** Public successor version **0.1.0**
selects source `56740553885420a7f6f301c12de10696a9c8d432`, the M5Stack StickS3
K150 V17 application with TD diagnostics and a 150 ms health-display hold.
See the [standalone build guide](../../docs/ss3/build.md),
[cutover record](../../docs/ss3/cutover.md),
[explicit import provenance](imports/2026-10-16.json) and [notices](NOTICE.md).

The supported profile is **RC at BLE100**, including an experimental estimated-TX
relay. No GM, local election, GNSS, external PPS, LoRa, WiFi or PTS service is
provided. The relay advertises an unknown TX timing bound and does not claim
actual TX capture. Source expiry, physical receive/correction matching,
controller ownership and bounded update drain remain independent of display
health. A 150 ms LCD hold never extends timing permission.

The target consumes the [shared display registry](../../registry/README.md).
Factory binding, group enrollment and enrolled images are ignored local inputs.
No public enrolled binary or generic installation/recovery tool is supplied.
The USB interface is a diagnostic CDC console, not the T096 UTS1 timing service
used by the macOS client. Build/tests do not qualify physical RF timing,
independent output accuracy, optical scanout or continuous reliability.

For a complete group, follow the [two-GM setup guide](../../docs/GETTING_STARTED.md).
