# CoreS3-SE notices

Original CoreS3-SE code, tools, UI screenshots and ordinary documentation:
Copyright (c)2026 Jonathan Yantis, MIT; see [LICENSE](../../LICENSE).

The renderer includes the existing selected bitmap font in
`firmware/n30/main/font.inc`, originally selected from the W12 display. Existing
source notices and [N30 notices](../n30/NOTICE.md) remain authoritative. No N30
calibration, factory binding, key, measured dataset or installed image is imported.

The target-local GM7 implementation4 codec is an exact two-file selection from
PToB's identified public revision. Its paths, hashes and MIT
attribution are retained in [protocol/provenance.json](protocol/provenance.json).
No private enrollment, laboratory logs or Git history accompanies it.

The explicit W12 helper and TZ selections are recorded in
[tools/precision_sources.json](tools/precision_sources.json). The two rate-only
helper adaptations remove fields absent from the pinned CoreSE clock; the
production prediction/retained-clock ABI is preserved. Selected JOIN/RC source
keeps its Apache-2.0 notice; original helpers and CoreSE code use MIT.

Separately acquired pinned dependencies retain their terms:

- M5GFX and M5Unified: M5Stack, MIT; retain their complete LICENSE files and the
  notices for bundled fonts, decoders and other selected components.
- ESP-IDF: Espressif and its component authors; Apache-2.0 and the individual
  component licenses. A future binary distribution needs its own linked-source
  and dependency-notice review.

HP5061A and Apollo are design references only. No HP/Keysight/NASA manual image,
trademark logo or source asset is copied into the firmware.
