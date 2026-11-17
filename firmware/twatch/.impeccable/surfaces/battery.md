---
version: 1
slug: battery
primary_target: firmware/twatch/main/watch.cpp
related_targets: [firmware/twatch/main/watch.hpp, firmware/twatch/main/display.cpp]
---

# Battery app

Mode: Operate. Native embedded 410 × 502 AMOLED; target-scoped extension.

## Direction contract
THESIS: Read battery level and screen use together across the same period, without searching through diagnostics.
OWN-WORLD: User-pinned Apple Watch inspiration: black ground, quiet rules, bright green battery columns and blue display-on bars. Inherit the bundled Rajdhani atlas, utility back header and safe aperture. No logos or raster artwork.
STORY: Open Battery from Apps, read the current gauge, compare percentage history against display-on segments, switch 1h/6h/12h/24h, and see compact uptime. User confirmed awake means display-on time over the selected period and requested clean bars.
FIRST VIEWPORT: Common back/title header; a horizontal four-option scale; current percentage and power status; two aligned thirty-column charts with explicit 0–100% and minutes-per-segment scales; period awake total; compact uptime at the bottom. Empty, unavailable and preboot intervals remain empty, never fabricated discharge.
FORM: Precise user-pinned local extension, code-led native production renderer. No concept tournament or image comp needed; three watch faces and existing settings retain their identity.
FINISH: unreviewed and undocumented is unfinished; this build ends with the finish review, the verdict, DESIGN.md, and every shipping raster carrying its provenance

## Completed review — 2026-11-16

Finish disposition: **ship**. The finish reviewer matched every direction criterion and required no material fixes. This is a precisely specified extension of the existing utility screens; `DESIGN.md` and `.impeccable/design.json` were compared with the result and preserved.

The native renderer retains the common amber back control, 36px title, x=42 content inset, quiet rules, and bundled Rajdhani 36/26/20/14px hierarchy. Battery adds surface-local green level columns (source RGB 119/224/103) and blue display-on columns (104/189/249), both quantized through the existing RGB565 renderer. The selected scale has a tonal field and underline as well as different text color. These local chart colors do not redefine the three faces or the shared palette.

`main/watch.cpp` and `main/watch.hpp` establish the default 1h scale, 1/6/12/24h controls, aligned thirty-column plots, explicit 0–100% scale and 2/12/24/48-minute display-on segment scales, selected-period display-on total, compact uptime, and Apps navigation. `main/battery_history.hpp` retains a rolling 24 hours within the current boot session, leaving preboot and missing gauge intervals empty. `main/display.cpp` samples the gauge while the panel sleeps and accounts for panel-on time; the upper-left PMU short press toggles explicit screen sleep, including on USB power. No runtime estimate or persistence across reboot is implied.

Documentation inspection used `review/battery/battery.png`, `battery-24h.png`, `battery-empty.png`, `battery-unavailable.png`, and `launcher.png`. These are native 410 × 502 synthetic renderer fixtures under a conservative rounded aperture. The empty fixture says “Collecting battery history”; allocation unavailability says “History unavailable” and displays `--` for the total. They establish the reviewed source layout and states, not physical appearance, measured battery life, PMU behavior on an installed watch, or timing qualification. No HTML/CSS detector applies to this embedded C++ renderer.

Pre-existing system drift was reported without repair: the sidecar retains superseded sleep choices/default, a less-than-24-hour local-hold description, and obsolete unavailable/lower-bound pointer rules that differ from current `DESIGN.md` and code. These discrepancies were not adopted as Battery rules.
