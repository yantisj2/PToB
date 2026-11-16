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

The drum explicitly distinguishes BLE/RC1/RC2, LOR, RC9, HOLD and unavailable. Full phase means current lock, half hold, dark acquisition/unavailable; NO TIME is independent. The RX jewel means LoRa receiver powered, not packet freshness. RMS remains linear0ns–10us with OVER; P99hold1–60s shows sampled lower bounds with a smoothly moving needle and a small >=s unit; battery is a reserve sector. The source watch's calendar apertures inspire the mechanical bridge. The native preview was reviewed before installation; hardware aesthetic feedback remains separate.

Horizontal swipes cycle all three faces on release and persist selection: left advances Digital → Instrument → Solar → Digital; right reverses modulo three. Face-picker hit rows are 105<=y<210, 210<=y<315 and 315<=y<425. Tap handling waits for release so a swipe over MODE cannot navigate accidentally. Wake consumes the whole initial gesture. Digital and Instrument retain MODE/tap launcher navigation, 90/120/180s sleep and separate display-only clock continuity with withdrawn precision during local HOLD.


## Solar face — approved reference extension
THESIS: A daily solar orbit gives a wrist-sized sense of place and time while four corner scales retain timing evidence.
OWN-WORLD: User-pinned solarpath.webp reference, re-composed for the 410 x 502 chamfered AMOLED: marine blue daylight, layered calculated twilight, illustrative stars, warm solar disc, correctly lit lunar phase, pale blue circular corner subdials. Digital and Instrument remain intact.
STORY: Read local time; locate the sun on today's 24-hour path; recognize lunar phase; glance RMS, sampled P99 hold, selected source/strength and predicted tide movement. Santa Rosa Beach is the fixed astronomy location, not inferred from the current display zone.
FIRST VIEWPORT: A 332px sky dial contains an 83px-radius pale-rimmed clock medallion. Its center moves opposite the sun along a 35px internal circle; elapsed-time acquisition is bounded to 12px/s and unknown astronomy retains its position. Central date/time and the moon graphic travel inside it, without SOLAR or lunar-percentage copy. The fixed civil track has 12 top, 18 right, 00 bottom and 06 left. Current Central civil time drives the sun and opposing clock. Actual sunrise/sunset and three morning/evening twilight chords set seasonal horizon/band height and tilt, with all eight crossing ticks/dots on the inner orbit; each event uses its own Central UTC offset, including DST changes. There is no seasonal clock displacement beyond current civil-hour motion. Four circular radius-34px corner subdials center at (77.5,81.5), (331.5,81.5), (77.5,419.5), (331.5,419.5), labeled RMS/P99/SRC/TIDE ~. Their centers sit 32px inward along the actual frame chamfer normals: all four antialias-aware case clearances are 10.75px; main-circle clearances are 9.70–11.10px around the retained (205,251) center, within 1.4px variation.
FORM: User-pinned reference overrides open direction selection; established code-led native renderer, seed inherited 5f9d34e2. No raster is shipped as UI artwork. SRC text shows only the source identifier; actual RSSI drives the ring clamped to [-110,-40]dBm, with no numeric RSSI text. RMS is linear 0–10us with >10 us above range; P99 is a clean integer with appropriate units and no >= prefix. Tide is explicitly ~ approximate interpolation between predicted Destin events (low=0, high=1), with RISE/FALL direction, a dim ring and no active endpoint when unavailable, EXP at coverage expiry and -- for other unavailable states.
RENDERING: Cached minute ephemerides drive a PSRAM minute backdrop with cached chord column heights and a 256-entry daylight palette; sky-band construction has no per-pixel pow/trigonometry. Generation-aware 32px tile damage restores old/new dynamic regions, with full restore on face, buffer or backdrop generation changes and direct rendering on allocation failure.
FINISH: Offline native fixtures cover all months, solstices/equinoxes, DST boundaries, day, dawn, sunset, night, new/full moon, held/unknown time, overflow and tide expiry. Cache damage equivalence and three-face persistence/navigation are tested. Independent finish review and token-bearing documentation close the work; no hardware acceptance is inferred.
