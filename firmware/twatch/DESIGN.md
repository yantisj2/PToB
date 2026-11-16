---
name: T-Watch Ultra instruments
description: Target-scoped Digital, Instrument and Solar native watch faces
colors:
  black: "#040809"
  face: "#111A1B"
  ink: "#DEEDD7"
  muted: "#98B1A2"
  amber: "#F1B859"
  green: "#86D9AA"
  rule: "#31443E"
  dim: "#182622"
  instrument-case: "#1A261D"
  instrument-bevel: "#495844"
  instrument-dial: "#1B271D"
  instrument-lume: "#CBD3AE"
  instrument-etch: "#94A384"
  instrument-recess: "#0C1711"
  instrument-red: "#C34C37"
  metal-shadow: "#0A110C"
  metal-highlight: "#798A6B"
  well-shadow: "#080E0A"
  well-top: "#0D1710"
  well-bottom: "#253222"
  well-groove: "#1E2B1D"
  dial-highlight: "#31412B"
  hand-shadow: "#060D08"
  solar-ink: "#D3EBFA"
  solar-muted: "#8BB5D2"
  solar-night: "#050F1E"
  solar-track: "#20394C"
  solar-blue: "#71BFEB"
  solar-case-top: "#0C1B2B"
  solar-moon-dark: "#283C53"
  solar-moon-lit: "#9BB5CA"
  solar-zenith-night: "#091731"
  solar-zenith-day: "#2373B0"
  solar-horizon-night: "#223A57"
  solar-horizon-day: "#74AAC0"
  solar-horizon-warm: "#FFB16F"
  solar-civil-top: "#275374"
  solar-civil-bottom: "#1E3E5F"
  solar-nautical-top: "#1B3656"
  solar-nautical-bottom: "#142642"
  solar-astronomical-top: "#12223C"
  solar-astronomical-bottom: "#0A172A"
  solar-deep-night: "#050E1B"
  solar-hour-dark: "#04111E"
  solar-hour-light: "#ECF7FF"
  solar-medallion: "#071422"
  solar-tide: "#85CECD"
  solar-sun-limb: "#FFCF8B"
  solar-sun-disc: "#FFE5B3"
  solar-sun-core: "#FFF6DD"
typography:
  title:
    fontFamily: Rajdhani
    fontSize: 36px
    fontWeight: 600
  body:
    fontFamily: Rajdhani
    fontSize: 26px
    fontWeight: 600
  label:
    fontFamily: Rajdhani
    fontSize: 20px
    fontWeight: 600
  dial-label:
    fontFamily: Rajdhani
    fontSize: 14px
    fontWeight: 600
rounded:
  square: 0px
components:
  face-field:
    backgroundColor: "{colors.face}"
    textColor: "{colors.ink}"
    rounded: "{rounded.square}"
    width: 346px
    height: 200px
  navigation-row:
    backgroundColor: "{colors.black}"
    textColor: "{colors.ink}"
    typography: "{typography.title}"
    width: 326px
  action-label:
    textColor: "{colors.amber}"
    typography: "{typography.label}"
  locked-status:
    textColor: "{colors.green}"
    typography: "{typography.body}"
  instrument-case:
    backgroundColor: "{colors.instrument-case}"
    textColor: "{colors.instrument-lume}"
    width: 366px
    height: 451px
  instrument-dial:
    backgroundColor: "{colors.instrument-dial}"
    textColor: "{colors.instrument-lume}"
    width: 338px
    height: 338px
  dial-caption:
    textColor: "{colors.instrument-etch}"
    typography: "{typography.dial-label}"

---

# Design System: T-Watch Ultra

## Overview

**Creative North Star: "The Wrist Instrument"**

Digital retains the Casio-inspired dark segmented display, pale phosphor time and amber controls within a chamfered bezel and inset LCD frame. Instrument translates the approved olive perpetual-calendar reference into original PToB mechanical indicators inside a squarish instrument case, with pale lume and restrained red needles. Directional bevels, grooved recessed wells and broad outlined pilot hands give Instrument physical depth. Neither face copies a brand mark. Solar adds the approved daily-orbit reference as a third visual world: marine blue sky, calculated twilight, a warm sun and pale blue corner scales. Digital and Instrument retain their established visual systems.

This visual authority applies only to firmware/twatch. The native C++ framebuffer is authoritative; sidecar swatches illustrate components. Host captures and offline finish reviews establish source/render quality only. The operator has confirmed physical rendering of the earlier Digital face; Instrument appearance on hardware, optical timing, precision and power remain unqualified.

**Key Characteristics:**
- Time-first segmented Digital, mechanical Instrument or daily-orbit Solar display.
- Chamfered case geometry and inset readable content.
- Face-specific palette with shared utility screens and controls.
- Explicit unavailable evidence and display-only local hold.

## Colors

### Primary

Amber marks Digital actions, printed accents, seconds and attention states. Shared utility screens retain amber navigation; Instrument does not use it. The text alongside it identifies the meaning.

### Secondary

Green identifies Digital locked PLL. Face selection uses explicit SELECTED copy. It always accompanies explicit text.

### Neutral

Black surrounds the face and supports utility screens. Face is the deep green display field. Ink is pale phosphor for time and primary reading; muted supports labels and explanations. Rule separates rows and groups; dim preserves inactive segment geometry.

Instrument uses olive case and dial, bevel/etch rings, pale lume hands and lettering, dark recesses and red seconds or unavailable needles. Red parking below the subdial range distinguishes missing evidence; color alone never indicates a numeric value.

Solar uses its own marine-blue sky and twilight ramps, warm solar marker, pale blue text and scales, and muted teal tide indication. The frontmatter records every Solar source RGB endpoint; blends and RGB565 quantization are computed by the renderer.

The frontmatter records the source RGB values. The renderer packs them to RGB565 and alpha-blends the atlas, so sampled screenshot colors can differ through quantization.

## Typography

Rajdhani SemiBold is rasterized into the bundled ASCII atlas at the four frontmatter sizes. It is licensed under SIL OFL 1.1; see NOTICE.md and fonts/OFL.txt. There is no runtime font fallback. Unsupported characters become a question mark; use the implemented ASCII unit spelling (`us`).

Titles and navigation rows use the title role; date, diagnostic values and secondary headings use body; supporting copy and status captions use label; small Instrument scale bounds and captions use the 14px dial-label role. Coordinates are top-oriented atlas positions, not CSS baseline metrics. No browser line height or tracking is prescribed.

Digital uses original seven-segment geometry, not font glyphs. Main digits are 55 × 108 pixels; seconds are 16 × 34. Inactive segments remain visible in dim; missing time renders segment dashes. Instrument uses broad outlined pilot hands with luminous inserts and offset shadows, printed cardinal numerals with crossing ticks omitted, and 14px subdial labels. Its scales are drawn geometry, not numeric digital readouts.

## Layout

The only viewport is 410 × 502 pixels; there are no responsive breakpoints. Digital's outer bezel starts at (18, 20), measures 374 × 462 and has 42px chamfers. Its LCD field is (32, 104), 346 × 200. Diagnostics and P99 have separate compartments. Instrument's 366 × 451 case starts at (22, 27), with 48px chamfers; the main dial centers on (205, 247).

Inset utility text starts at x=42, with 326px rules, header text at y=42 and a separator at y=96. Bottom copy is inset further. Review all screens under the conservative synthetic 100px rounded-corner aperture; this is a design check, not measured panel geometry. Keep vital labels and controls away from corner cutouts.

All three faces retain the y>=430 MODE touch zone. The common header touch region is y<95. Face-selection rows begin at y=111, 216 and 321; their hit regions are 105<=y<210 (Digital), 210<=y<315 (Instrument) and 315<=y<425 (Solar). Keep these regions in sync with View::touch.

## Elevation & Depth

Digital retains flat tonal fields, narrow chamfer frames and LCD compartments without gradients or drop shadows. Instrument deliberately adds depth: annular metal shading lights raised rims from the upper left and reverses that direction on recessed rims. Its olive field blends the dial-highlight and instrument-dial tokens with radial/directional weighting; wells shade from well-top to well-bottom, with concentric well-groove lines and a (1, 2)px well-shadow offset. Broad outlined pilot hands carry luminous inserts above a (2, 3)px hand-shadow offset. These are deterministic RGB565 geometry and tonal blends, not photographic textures or a global shadow treatment. Preserve this distinction between the faces.

## Shapes

The cases are squarish with clipped shoulders. Digital compartments use small chamfers; Instrument combines a chamfered case with circular dials and reserve sector. Segments keep geometric ends. Utility rows retain straight rules and small drawn chevrons.

## Components

### Digital face

A pale segmented clock dominates the face. Amber seconds and a sixty-mark track progress in discrete seconds. Five-second marks are taller. Supporting diagnostics keep their labels and units; missing or expired values use dashes or WAIT. Source availability is explicit. Sync RMS is internal prediction quality and P99 is a sampled statistic; neither is calibrated UTC accuracy.

### Navigation rows

Large text, a supporting line, a right chevron and a thin bottom separator identify tappable rows. The amber back glyph occupies the common header. The printed MODE key or case engraving opens the launcher; the physical button returns from utility screens to the face. There are no native hover, keyboard-focus or press animations.

### Settings

Brightness cycles from 10 through 100 percent in ten-point increments; display sleep cycles through 15, 30, 60 and 90 seconds (30 is the default). Both rows state that they can be tapped. Save failure replaces the persistence footer with an amber retry instruction. GNSS OFF is a status, not a control. First touch or button after sleep only wakes the display. Timing stays active during display sleep; this is not a measured battery-life claim.

### Instrument face

Main hands carry time; unknown time parks them at twelve and exposes a framed NO TIME aperture. The upper calendar bridge places a source drum aperture and independent RX aperture on either side of a concentric PLL phase well. The source drum shows BLE for BLE/RC1/RC2, LOR, distinct RC9, HOLD during valid hold, and `---` when time is unavailable. No source triangle or selector ring shares the lettering. The RX jewel reports the powered LoRa receiver, not selected-source authority or fresh packets. The moon-like PLL disc is full for current lock, half for valid hold and dark for acquisition/unavailable; it is not a lunar-calendar reading.

Both source/RX apertures are 41 × 25px at (136, 142) and (234, 142), centered on (156, 154) and (254, 154). They flank the phase well at (205, 154), radius 24; its phase fill/outline share that center and radius 17. Four empty pixel columns separate each aperture edge from the phase rim. Source and RX text use atlas y=145, with visible letter pixels at y=150–158; the unavailable dash stroke occupies y=154–155. RX lettering centers on x=247 and the jewel on (265, 154). PLL is engraved above at atlas (205, 113); RADIO and LORA are advance-centered at x=156/254, atlas y=174, and share visible y=179–187. Keep these independent text/phase regions intact. Moving hands may cross them as on a mechanical watch, while fixed labels and dynamic contents retain distinct regions.

The calendar bridge is part of the Instrument face. Native previews remain synthetic; hardware appearance, continuous reliability and timing qualification are separate checks.

The Sync RMS dial is linear from `0ns` to `10us`, with 2.5us quarter divisions. Values above 10000ns park at the maximum scale and show `OVER`; exactly 10000ns has no overflow flag. Missing RMS holds its last visual position for 500 ms, then travels slowly toward the maximum over roughly 30 seconds; no red bar or parked red needle appears. Sampled P99 hold spans 1–60s with `<=1` and `>=60` bounds. Lower-bound evidence moves the needle toward its sampled duration and replaces the small `s` unit with `>=s`; an exact bounded result keeps `s`. LoRa/RC9 never gain an unsupported sampled-hold value. Battery reserve is a smoothly slewed E–F sector with unavailable input moving toward E, not a runtime estimate. These are internal/sampled metrics, not calibrated accuracy.

### Solar face

Solar follows the approved `solarpath.webp` reference with original native geometry. Its 332px sky dial centers on (205, 251). The case starts at (18, 22), measures 374 × 458px and has 55px chamfers. Four compact circular subdials have radius 34px: RMS at (77.5, 81.5), P99 at (331.5, 81.5), SRC at (77.5, 419.5) and TIDE ~ at (331.5, 419.5). Centers derive from a 32px inward-normal inset from the actual frame chamfer midpoints of (18, 22)..(391, 479). Antialias-aware chamfer clearance is 10.75px for all four. Clearance to the main circle is 9.70–11.10px because its established center remains (205, 251): variation is at most 1.4px, not exact equality. Each contains 24 ticks and a 300-degree progress ring. Labels use the 14px atlas; values use 20px, falling back to 14px when their advance exceeds 54px. These self-contained complications replace the perimeter arcs.

The clock medallion has an 83px outer radius and an 81px dark interior with a pale, nearly complete rim and a quiet upper-left break. Its center travels opposite the sun on a 35px-radius internal circle around (205, 251). Elapsed-time acquisition motion stays within that circle at no more than 12px/s; missing astronomy retains the last position. Date, Central time, seconds/status and the lunar graphic travel together inside the medallion. The 36px Rajdhani atlas is resampled at 1.5× for the time. There is no SOLAR title or lunar-percentage caption.

The horizon/twilight boundaries derive from the Santa Rosa Beach coordinates (30.35, -86.23). The fixed civil 24-hour scale has 12 at top, 18 at right, 00 at bottom and 06 at left; it never rotates to solar noon. Current Central civil time (CST/CDT) determines sun and opposing clock position independently of the mesh display zone. Seasonal changes affect the horizon behind the fixed scale, not an extra seasonal clock displacement. Sunrise/sunset use -0.833 degrees; civil, nautical and astronomical twilight use -6/-12/-18 degrees. All eight actual morning/evening crossings have ticks and dots on the 124px-radius inner solar orbit, with larger sunrise/sunset dots. The chord joining actual sunrise and sunset sets horizon height and tilt. Separate morning/evening chords define the three twilight bands. Every crossing uses `calendar::central_offset(eventUTC)` for its own civil angle, including DST-change dates; neither midnight nor the current snapshot offset substitutes for the event offset. Current calculated altitude drives the sky palette. The sun advances between minute ephemerides; the moon depicts calculated phase without a numeric illumination caption. Stars are illustrative, not a constellation map. The lunar disc is distinct from Instrument's PLL phase well.

RMS spans linear 0–10us; the numeric label retains ns/us and shows `>10 us` above range. P99 shows a clean integer with appropriate `s`/`ms`/`us` units and no `>=` prefix; its 1–60s ring remains a sampled-hold indication. Bound metadata remains diagnostic, and LoRa/RC9 do not acquire unsupported hold evidence. SRC text reports only the selected source identifier. Actual RSSI drives the ring, clamped from -110 to -40dBm, with no numeric RSSI or percentage text. Missing strength dims the ring. Held time shows HOLD and withdraws live precision/source evidence.

`TIDE ~` marks approximate progress interpolated between predicted Destin high/low events: low=0, high=1. `RISE`/`FALL` describes direction, not measured water level. Unknown, unbracketed or out-of-range tide input dims the ring and removes its active endpoint; expired coverage explicitly shows `EXP`, other unavailable states show `--`. Unknown astronomy suppresses the sun/moon, shows `SKY --`, and retains explicit unavailable time. Calculations from display-only held time never grant timing authority.

The UI worker caches ephemerides per UTC minute. A 411640-byte PSRAM Solar backdrop rebuilds when the sampled minute or known-state changes. The rebuild evaluates one column of event-chord heights at a time and uses a 256-entry daylight palette, avoiding per-pixel power/trigonometric work without retaining a large internal-RAM height array. Generation-aware 32px tile damage restores old and new dynamic regions only while face, buffer and backdrop generation match; otherwise the renderer restores the full backdrop. Allocation failure draws the same base directly. This is a single-owner rendering optimization, not a hardware performance or power qualification. Synthetic fixtures and cache-equivalence checks remain separate from installed-device acceptance.

Solar also allocates one 63,600-byte immutable geometry cache explicitly in PSRAM at first use. It retains the subdial/rim coordinates, fixed subdial alpha operations, solar halo and resampled numeric glyph masks. Only its pointer and initialization guard live in internal RAM; a compile-time 64KiB cap prevents silent cache growth. The renderer preserves the original ordered RGB565 blending and exact fractional medallion position. Geometry-allocation failure uses the original calculations, and oversized future glyphs use the uncached path. No graphics cache falls back to scarce internal SPI/DMA/radio memory. The first Solar frame constructs this cache; subsequent frames reuse it. Host work counters and exact static/moving-frame comparisons verify the optimization, while actual device latency and power still require measurement. Moving soft-disc edges use hardware float square roots away from raster decision boundaries, reverting to double near integer span edges and 8-bit alpha thresholds or for squared distances above 84². The conservative guard preserves quantized pixels without adding cache memory. Its square-root work counter counts double-precision calls only; float roots are excluded. The performance check also runs an uninstrumented moving sequence to catch accidental float-to-double promotion by measurement wrappers.


### Face selection

Digital, Instrument and Solar are three selectable text rows. SELECTED copy identifies the current choice. Tapping one returns to that face and saves its selection; the last selected face opens at boot. A left swipe advances Digital → Instrument → Solar → Digital; a right swipe reverses that order. Both persist the choice using modulo-three selection. Resolve taps and swipes on release, so swiping across MODE cannot first activate navigation. A switch requires at least 64px horizontal travel, more than twice the vertical travel, and at most 1.5 seconds; taps require less than 20px displacement on each axis. Vertical/long gestures do not switch. Wake consumes the whole initial contact gesture, including its release. MODE taps and physical-button navigation remain available. There is no obsolete one-face preview or future-library promise.

### Display-only local hold

A fresh valid presentation snapshot anchors a local clock. Missing/expired snapshots continue its time until reboot from that anchor, using monotonic elapsed time and retained time-zone offset. Monotonic rollback discards the anchor. This continuity never feeds a timing source, PLL, admission or precision metric. Source identity, lock, RMS and sampled hold are withdrawn: Digital shows HOLD and unavailable values; Instrument shows a half disc with unavailable precision pointers. A new valid snapshot restores current presentation through bounded clock slewing, without backward jumps. Without an anchor, time is unavailable. All analog pointers use elapsed-time motion; sub-500 ms evidence loss holds their positions. Bosch wrist tilt wakes the last face for 5 seconds, independently of the normal interactive timeout.

### Status labels

Green LOCK and amber WAIT/HOLD/ACQ keep text meanings independent of color. The Digital footer shows LOR with three reception bars for recent authenticated pairs, dim bars when merely enabled, and LOR OFF when disabled. Preserve these unavailable and attention states when adding screens.

## Do's and Don'ts

- Do keep this visual authority scoped to firmware/twatch.
- Do use the bundled Rajdhani atlas and original segment geometry.
- Do preserve units, unavailable states and explicit status labels.
- Do review all three faces and all utility screens at native resolution, with the rounded aperture and unavailable, local-hold, overflow, RC9 and lower-bound states, plus release-resolved swipe and wake gestures.
- Don't use synthetic preview values as hardware evidence.
- Don't convey lock, selection, saturation or failure through color alone.
- Don't treat display-only local hold as current timing authority or retain precision during it.
- Don't add web hover or responsive behavior to the native touchscreen contract.
- Don't introduce image assets for elements already drawn by the renderer.

Original documentation © 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.

The Instrument static dial is cached in a single PSRAM layer at first presentation. Subsequent redraws copy that layer and paint the moving indications; cached and uncached fixture images match pixel-for-pixel. The renderer remains single-owner.

Settings uses four 80-pixel rows: brightness, normal sleep timeout, BLE and LoRa. Radio feedback is actual owner state, with ON/OFF/WAIT/FAULT text independent of color. Radio choices are temporary until boot; the footer makes that explicit. Existing display settings stay persistent.

Digital shows the authenticated local zone abbreviation (CST/CDT for Central), with integer P99 duration and s/ms/us units. Bounds remain diagnostic metadata. Valid USB power keeps the panel on; after unplugging, the saved timeout applies. Wake transfers a freshly sampled, complete frame while the panel is hidden, then reveals it.
