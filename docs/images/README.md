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

The README's current `chronycontrol-tracking-10min-20261016.png` was supplied by
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

Both screenshots were provided after the structured stability collection's
observation cutoff and are not added to its numerical populations. Original
screenshot/data contributions by Jonathan Yantis are CC BY 4.0; rights in the
third-party ChronyControl interface remain unchanged. ChronyControl itself is
not bundled.
