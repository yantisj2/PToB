# CoreS3-SE notices

Original CoreS3-SE code, tools, UI screenshots and ordinary documentation:
Copyright (c)2026 Jonathan Yantis, MIT; see [LICENSE](../../LICENSE).

The renderer includes the existing selected bitmap font in
`firmware/n30/main/font.inc`, originally selected from the W12 display. Existing
source notices and [N30 notices](../n30/NOTICE.md) remain authoritative. No N30
calibration, factory binding, key, measured dataset or installed image is imported.

Separately acquired pinned dependencies retain their terms:

- M5GFX and M5Unified: M5Stack, MIT; retain their complete LICENSE files and the
  notices for bundled fonts, decoders and other selected components.
- ESP-IDF: Espressif and its component authors; Apache-2.0 and the individual
  component licenses. A future binary distribution needs its own linked-source
  and dependency-notice review.

HP5061A and Apollo are design references only. No HP/Keysight/NASA manual image,
trademark logo or source asset is copied into the firmware.
