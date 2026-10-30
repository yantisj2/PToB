# StickS3 · mesh health and relay diagnostics

PToB firmware for the **M5Stack StickS3 (K150)**: survey nearby clocks, inspect
mesh health and reported GNSS quality, or follow time as a BLE receiver with
an experimental estimated-TX relay. The default **Time Diagnostics (TD)** mode
surveys the network without admitting a timing source.

[Build and set up](../../docs/ss3/build.md) ·
[Group setup](../../docs/GETTING_STARTED.md) ·
[Development history](../../CHANGELOG.md)

<a href="../../docs/images/ss3-health-preview.png"><img src="../../docs/images/ss3-health-preview.png" width="135" alt="StickS3 illustrative network-health screen with delivery, signal and source status"></a>
<a href="../../docs/images/ss3-epoch-preview.png"><img src="../../docs/images/ss3-epoch-preview.png" width="135" alt="StickS3 Epoch preview with signed mesh birth UTC, estimated elapsed time and separate local uptime"></a>
<a href="../../docs/images/ss3-reference-preview.png"><img src="../../docs/images/ss3-reference-preview.png" width="135" alt="StickS3 Reference preview with reported fix, PPS, satellites, HDOP and election quality"></a>

*Production-renderer previews with illustrative inputs; the health image uses
an earlier layout. These are not hardware captures or accuracy measurements.
[Image provenance](../../docs/images/README.md) ·
[Epoch/Reference preview identities](../../docs/images/ss3-diagnostics-provenance.json).*

## Capabilities and specifications

| Area | Current configuration |
| --- | --- |
| Hardware | M5Stack StickS3 **K150**, ESP32-S3; **8 MiB flash** build configuration and PSRAM-backed LCD storage. |
| Display | **135 × 240** portrait color LCD with main/side-button navigation. |
| Profiles | **RC at BLE100** and passive TD startup. Optional receiver/relay operation uses estimated transmit timing with an unknown bound. No GM or local election. |
| Diagnostics | Eight TD pages: receiver/relay health, stream quality, RF, sample detail, source health, epoch and reference. |
| Management wire | Receive-only **GM7 implementation 3** plus legacy GS/GD1/GD2. The selected codec is fixed by [provenance](contracts/gm7_diagnostics_provenance.json). |
| Reference inputs | Remote GM reports only. No local GNSS, external PPS, LoRa, Wi-Fi or PTS service. |
| USB | Diagnostic CDC console; not the T096 UTS1 timing interface required by the macOS client. |

**Check wire compatibility before choosing a source.** StickS3 retains its
selected pre-TZ receiver/diagnostic profile; it does not implement the coarse TZ
profile used by current T096, W12, N30 and CoreS3-SE. Sharing a G1/RC7 family name
does not make those images compatible. Use the
[build guide](../../docs/ss3/build.md) to identify the selected source and contracts.

## Get started

1. **Confirm the hardware.** This target is the StickS3 K150, not a generic
   ESP32-S3 board. Follow the [build guide](../../docs/ss3/build.md) for pinned
   public dependencies, offline checks and preview generation.
2. **Prepare trust and names.** Supply private enrollment, a verified factory
   binding and the [shared registry](../../registry/README.md). Credentials and
   device identity are separate from aliases and local source discovery.
   Keep enrolled images private.
3. **Arrange a compatible mesh.** Match the selected wire profile before
   installation. Use the exact-device recovery, bounded drain, app-only write
   and readback checks in the build guide; the builder itself opens no hardware.
4. **Explore TD first.** Boot into passive diagnostics and use the side button
   to browse the pages. Inspect report freshness and scope. To receive timing,
   switch from Health to Relay Client with the main-button hold described below;
   timing still requires authenticated admission and fresh physical corrections.

## Screens and controls

A side-button press or main-button tap advances the page. TD provides:

| Page | What it shows |
| --- | --- |
| Health | Delivery, signal and source-permission summary; a held LCD view never extends permission. |
| Stream Quality | Timing-stream diagnostics and selectable chart views. |
| Relay Test | Four strongest observed relays by fresh smoothed RSSI; expired entries leave the list. |
| RF Health | Radio reception and signal diagnostics. |
| Sample Detail | Received sample detail for diagnosis. |
| Source Health | Reported source and observed GM/HOT state. |
| Epoch | Signed mesh birth UTC date/time, elapsed estimate, original report freshness and separate local uptime. |
| Reference | Reported GM fix, PPS, qualification, satellites, HDOP and election quality. |

Hold the main button for **1.5 s** on Health to leave TD for Relay Client.
On the receiver's front page, subsequent holds cycle **Relay Client → Relay Only
→ Time Diagnostics**. In TD, the same hold on Stream Quality changes chart view;
on Relay Test it toggles the test request when a fresh relay is available.
Other TD pages have no hold action. Receiver mode has clock/system pages.

A startup-held button or overlapping main/side press is suppressed until release.
The hardware power-button configuration is preserved; these navigation gestures
do not request a firmware sleep operation.

## Understand freshness and timing

Epoch birth belongs to the mesh, independently of the current leader or SS3's
boot. Passive TD has no admitted UTC clock: **ELAPSED EST** projects a signed
announcement's UTC using local elapsed time. **LIVE/LAST** describes the original
report deadline; the estimate never trains a clock or grants timing permission.
Missing signed birth or UTC stays unknown.

Reference values describe the GM, not StickS3 output. Expired or mismatched
reference data becomes unknown; reports without HDOP cannot supply it. Observed
HOT dwell and scan gaps are descriptive evidence, not proof of continuous
standby readiness. A **150 ms** display hold affects presentation only.

In receiver mode, source expiry, physical correction matching and radio ownership
remain independent of display activity. Estimated relay TX does not claim actual
RF capture. Fresh authentication is required after a source-context change.

## Development and limits

The [build guide](../../docs/ss3/build.md) owns dependencies, profile selection,
offline tests and installation guards. Read the
[cutover record](../../docs/ss3/cutover.md), [import provenance](imports/2026-10-16.json),
[notices](NOTICE.md) and [changelog](../../CHANGELOG.md) for source history.
The [family timing schematic](https://ptob.dev/hardware/esp32-s3/timing/) illustrates
W12 paths; its optional GNSS/PPS inputs do not imply StickS3 support.

Independent UTC/output precision, calibrated relay RF timing, optical scanout
and continuous reliability remain unqualified. Builds, synthetic previews and
observations on other targets cannot qualify this device. A generic public
enrolled binary and installation/recovery tool are not supplied.

Copyright (c) 2026 Jonathan Yantis. Original documentation: [MIT](../../LICENSE).
Preserved dependency notices apply.
