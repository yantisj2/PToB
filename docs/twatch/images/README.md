# T-Watch renderer previews

Copyright (c) 2026 Jonathan Yantis. MIT. Rajdhani glyphs retain OFL-1.1.

These are 410 × 502 frames from the native production renderer, using explicitly
synthetic fixtures. They do not show installed hardware or timing measurements.
The P99 value is illustrative; real firmware requires fresh eligible evidence.
Rounded corners use a conservative 100-pixel preview mask, not a measured panel
outline. The source and image hashes are recorded in [manifest.json](manifest.json).

Generate PPMs with `python3 firmware/twatch/tools/test.py --out artifacts/watch-preview-01`.
Convert the selected PPMs to PNG with Pillow; retain the synthetic/provenance
metadata. No image-generation model or raster artwork was used.
