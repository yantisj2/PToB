---
version: 1
slug: "firmware-twatch-main-watch-cpp"
primary_target: "firmware/twatch/main/watch.cpp"
related_targets: ["firmware/twatch/main/watch.hpp","firmware/twatch/main/display.cpp"]
---

# T-Watch Ultra digital face

Mode: Operate. Native embedded 410 × 502 watch; this surface is independent of the other target UIs.

## Direction contract
THESIS: A Casio-inspired digital watch that makes time legible first and timing evidence inspectable second. User confirmed dark digital direction and delegated details.
OWN-WORLD: Black AMOLED surround, deep green display field, pale phosphor numerals, amber seconds track and controls. Antialiased Rajdhani labels surround original geometric segmented digits.
STORY: Read local time, inspect source/RMS/PLL, check sampled P99 hold duration, open apps and adjust display behavior. Unknown evidence stays unknown.
FIRST VIEWPORT: A 370-pixel face field contains date above 119-pixel hour/minute numerals, small amber seconds at right, a sixty-mark seconds track, aligned source/RMS/PLL columns and a separate P99 row. Apps sits in the bottom touch zone. The signature move is the amber track advancing once a second without a continuous animation loop.
FORM: Brief-pinned digital watch; explicit user choice overrides the concept assignment. Seed 5f9d34e2. Code-led; no approved image comp. Scope excludes all other repository visual systems.
FINISH: unreviewed and undocumented is unfinished; this build ends with the finish review, the verdict, DESIGN.md, and every shipping raster carrying its provenance

## Approved extension — current Instrument refinement
User confirmed physical rendering but requested safe corners and Casio chrome; Digital uses chamfered bezel/LCD frames, inset labels and MODE. Conservative100px rounded-aperture previews are design checks, not measured panel geometry.

Instrument follows the user-provided olive IWC pilot/perpetual-calendar reference inside a squarish instrument case. Original PToB artwork; no copied brand mark. Directional metal bevels, recessed concentric wells and an olive dial field create depth. Broad outlined pilot hands have pale luminous inserts and offset shadows. Centered labels use a small dial atlas.

The upper calendar bridge replaces the rejected source ring. A source drum aperture and a matching LoRa RX jewel aperture flank one concentric PLL phase well. The phase center is (205,154), radius24; phase fill and outline share that center with radius17. Apertures are41x25 at(136,142) and(234,142), centered(156,154)/(254,154), with mirrored four-column empty gaps to the phase rim. Source/RX glyph bounds and jewel center share y154. RADIO/LORA engravings share one lower band; PLL sits above its well. No selector triangle or source-ring pointer remains.

The drum explicitly distinguishes BLE/RC1/RC2, LOR, RC9, HOLD and unavailable. Full phase means current lock, half hold, dark acquisition/unavailable; NO TIME is independent. The RX jewel means LoRa receiver powered, not packet freshness. RMS remains linear0ns–10us with OVER; P99hold1–60s uses unavailable parking for lower-bound-only evidence, without a MIN annotation; battery is a reserve sector. The source watch's calendar apertures inspire the mechanical bridge. The native preview was reviewed before installation; hardware aesthetic feedback remains separate.

Either horizontal swipe switches the two faces on release and persists selection. Tap handling waits for release so a swipe over MODE cannot navigate accidentally. Wake consumes the whole initial gesture. Digital and Instrument retain MODE/tap launcher navigation, 90/120/180s sleep and separate display-only clock continuity with withdrawn precision during local HOLD.
