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

`battery.png` shows the one-hour Battery app with synthetic percentage samples,
aligned display-on bars and uptime. Its values illustrate the UI, not observed
discharge or battery endurance. `launcher.png` includes the Battery destination.

`solar.png` previews the third face with synthetic timing inputs and calculated
Santa Rosa Beach/Central astronomy. Its stars are illustrative and tide progress
is approximate prediction interpolation, not a measurement. No reference raster
is shipped as face artwork. The Solar layout uses four compact circular corner
subdials, a pale-rimmed clock moving opposite the sun, and eight morning/evening
sunrise/sunset and twilight markers. The civil scale stays fixed at 12/18/00/06;
actual morning/evening event chords set seasonal horizon and twilight bands.
Corner subdials share equal antialias-aware case clearance; their main-circle
clearance varies by at most 1.4px. Source text is the identifier; strength is
encoded in its ring. The moon is a phase graphic without a percentage caption.

`shadow.png` previews the fourth face with synthetic satellites, a physical-PPS
annunciator and illustrative signed MEAN/P99 ABS/RMS. These are timing-error
statistics, distinct from the hold-duration P99 on the other faces. The actual
receiver never fabricates sky positions when GSV coordinates are unavailable.
Search, fix, PPS, ten-second settling and measurement are native states; preview
motion and values do not establish installed GNSS reception or timing accuracy.
