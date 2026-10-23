# Clock displays and timing schematics

The initial five display assets illustrate the project interfaces. They were imported
unchanged from the project website’s hardware gallery on 16 October 2026.
[The manifest](manifest.json) records public source locations, dimensions,
SHA-256 hashes, embedded renderer/fixture metadata and the meaning of each image. No private workspace history or
raw capture logs are included.

T096 and W12 images are firmware-rendered previews with illustrative values.
The SS3 health image uses the V17 renderer and synthetic identities; its analog
clock image is an earlier relay-profile preview. Displayed RC1/RC2 labels describe
relay depth, not additional public GM/RC profiles. These are screen previews,
not photographs or validation of the current firmware.

The macOS image is a historical replay from the USB/chrony study, retaining its
loss indicator and withheld UTC. Its estimator RMS is not calibrated UTC error.
See the [USB/macOS draft](../../papers/usb-macos-time-transfer-preprint.pdf) for
measurement populations and limitations. Images do not extend the claims of
those records to a newer source build.

Two timing-schematic screenshots were captured from the public
[T096](https://ptob.dev/hardware/t096/timing/) and
[ESP32-S3 / W12](https://ptob.dev/hardware/esp32-s3/timing/) pages on 16 October
2026. The browser-rendered diagrams were cropped from full-page screenshots,
retaining their path labels and limitations. The manifest records capture modes,
crop coordinates, dimensions and hashes. README previews link to the full
interactive pages. These logical architecture diagrams are not electrical wiring
diagrams, live measurements or current firmware qualification. The ESP32-S3
diagram describes W12; its optional GNSS/PPS paths do not establish StickS3 support.

Copyright (c) 2026 Jonathan Yantis. These original interface illustrations are
ordinary repository documentation under [MIT](../../LICENSE). Paper figures
and evidence datasets retain their separate [licenses](../../LICENSING.md).

## ChronyControl development tracking

The [macOS client README's](../../clients/macos/README.md) previous
`chronycontrol-tracking-10min-20261016.png` was supplied by
Jonathan Yantis on 16 October 2026. It is a user-selected cleaner interval of
approximately ten minutes, showing the PToB development feed in
[ChronyControl, by Bryan Christianson](https://www.whatroute.net/chronycontrol.html).
The image is included unchanged. Its start/end controls describe the underlying
log selection; the zoomed graph shows a shorter interval. Further transient
reduction and GM authentication work are planned. This selected view does not
establish complete-run availability or independent UTC accuracy. Device/source
identity and complete raw-run statistics are not established by the screenshot.

The earlier `chronycontrol-tracking-20261016.png` remains as a historical view.
At the user's request, its top 39-pixel title bar containing the local file path
was cropped; plotted pixels were unchanged. The manifest retains both images'
source/public hashes and transformation records. The original unredacted earlier
screenshot is excluded.

The October 16 screenshots were provided after the structured stability collection's
observation cutoff and are not added to its numerical populations. Original
screenshot/data contributions by Jonathan Yantis are CC BY 4.0; rights in the
third-party ChronyControl interface remain unchanged. ChronyControl itself is
not bundled.

## USB Direct tracking — 17 October 2026

`chronycontrol-usb-direct-20261017.png` was supplied by Jonathan Yantis on
17 October 2026 for the [macOS client README](../../clients/macos/README.md).
The exact supplied bytes are preserved, including the existing crop. The image
shows the selected approximately 11:37–11:42 AM CDT portion of the DC v0.15.80
GNSS → USB → macOS trial with experimental quarter-second chrony polling
(`poll -2`, `filter 4`). At capture time the normal client still used one-second
polling; the later shared DC/RC client adopts quarter-second polling.

The chart uses an after-acquisition display: no-reference rows and the first
selected update are omitted by acquisition state. Later selected observations
are retained regardless of magnitude. The raw acquisition values remain in
the separately recorded numerical populations. This image is not a complete-run
availability measurement or independent UTC accuracy result. The fixed first
300-second summary of the same trial ends a few seconds before the screenshot;
its [results record](../../clients/macos/results/2026-10-17-dc.json) also retains
the separate earlier DC populations, including larger transients and withheld
output. Private raw captures are excluded.

Original screenshot/data contributions by Jonathan Yantis use CC BY 4.0.
Rights in the third-party ChronyControl interface remain unchanged. The
[manifest](manifest.json) records dimensions, hashes, unchanged-byte import,
display selection and attribution.

## Current shared-bus comparison image — 17 October 2026

`chronycontrol-usb-direct-shared-bus-20261017.png` replaces the earlier
ChronyControl image in the [macOS client README](../../clients/macos/README.md).
Jonathan Yantis supplied this DC v0.15.80 screenshot unchanged. Its controls span
11:37 AM–12:42 PM CDT and the zoomed graph shows approximately 12:36–12:42 PM.
It uses the same after-acquisition display selection described above, preserving
later selected updates without an error-size threshold. Image pixels are not a
source for the comparison's numerical statistics.

The accompanying [RC versus USB Direct record](../../clients/macos/results/2026-10-17-ble-vs-direct.json)
uses a separate concurrent 180-second interval, approximately 12:42:10–12:45:10 PM
CDT, with identical sampling and chrony settings. Current measurements use a
shared USB bus; a clean-bus repeat is pending. Neither the image nor the window
qualifies continuous availability or independent UTC accuracy. Earlier images
and their original provenance remain historical records.

Original screenshot/data contributions: Jonathan Yantis, CC BY 4.0. Third-party
ChronyControl interface rights remain unchanged. The manifest records exact bytes,
dimensions, metadata hashes and the existing display selection.

## N30 portrait previews

The three main N30 screens were regenerated on 21 October 2026 from the
version 0.2.4 production renderer, using synthetic N3-1 values:

- `n30-clock-preview.png`: clock dashboard (`clock` fixture).
- `n30-rf-preview.png`: simultaneous BLE/LoRa clocks and activity (`rf-dual`).
- `n30-lora-preview.png`: passive LoRa receiver status (`lora-on`).

All are native 64×128 monochrome images. README markup enlarges them without
altering the files. The manifest records the selected generated filename,
source commit, renderer/model/font/fixture hashes, generator hash and image
identity. The dual-clock difference, drift and link quality are illustrative,
not measured timing results.

Reproduce the selected source variants with:

```sh
python3 firmware/n30/tools/preview.py --out artifacts/n30-preview --alias N3-1
```

Preview generation opens no hardware. N3-1 is a display alias in these synthetic
fixtures; no factory identity or enrollment is included. Prior screensaver
assets and their original dated source hashes remain available as historical
previews; these main-screen replacements are preserved in Git history.
Original ordinary interface documentation, Jonathan Yantis, MIT.

## N30 precision preview

`n30-precision-preview.png` is the version 0.2.20 third-page watch face,
generated on 22 October 2026 from the actual 64×128 renderer with the
`precision-lock` N3-2 fixture. Its **49 ns internal causal RMS** reproduces the
observed value in [the selected status dataset](../../evidence/s3/2026-10-22-fine/internal-quality-sanitized.csv),
ordinal 244 (uptime 585.803371 s, `LOCK_EST`, no fault). This renderer image is
not a hardware photograph. The root, BLE/PPS status, +123 ppb relative residual
trend and lock duration are illustrative. The 49 ns value is separate from
downstream PPS RMS, calibrated UTC error and physical output accuracy.
`80MHz / 12.5ns` reports counter tick size; fine delivery still rounds to 50 ns.

The [separate provenance record](n30-precision-provenance.json) records exact
renderer/model/font/fixture/generator hashes and the selected PNG hash. Earlier
preview manifests and historical hashes remain unchanged. Reproduce with the
N30 preview command above with`--alias N3-2` and select
`n30-precision-lock-preview.png`.
Original interface artwork and documentation, Jonathan Yantis, MIT.
