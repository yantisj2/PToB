# Clock displays and timing schematics

The five display assets illustrate the project interfaces. They were imported
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

The README's `chronycontrol-tracking-20261016.png` was supplied by Jonathan Yantis
on 16 October 2026. It shows the PToB development feed in
[ChronyControl, by Bryan Christianson](https://www.whatroute.net/chronycontrol.html).
Spikes remain under investigation. This is a tracking-log view, with no
independent UTC-accuracy or continuous-delivery qualification. The selected
device/source and complete raw run are not established by this screenshot.
It was provided after the structured stability collection's observation cutoff
and is not added to that collection's numerical populations.

At the user's request, only the top 39-pixel title bar containing the local file
path was cropped. The plotted pixels are unchanged. The manifest records source
and public hashes, exact crop and a pixel-equality check. The original unredacted
screenshot is excluded. Original screenshot/data contributions by Jonathan Yantis
are CC BY 4.0; rights in the third-party ChronyControl interface remain unchanged.
ChronyControl itself is not bundled.
