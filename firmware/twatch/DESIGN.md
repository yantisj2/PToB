---
name: T-Watch Ultra instruments
description: Target-scoped Digital and mechanical Instrument native watch faces
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

Digital retains the Casio-inspired dark segmented display, pale phosphor time and amber controls within a chamfered bezel and inset LCD frame. Instrument translates the approved olive perpetual-calendar reference into original PToB mechanical indicators inside a squarish instrument case, with pale lume and restrained red needles. Directional bevels, grooved recessed wells and broad outlined pilot hands give Instrument physical depth. Neither face copies a brand mark.

This visual authority applies only to firmware/twatch. The native C++ framebuffer is authoritative; sidecar swatches illustrate components. Host captures and offline finish reviews establish source/render quality only. The operator has confirmed physical rendering of the earlier Digital face; Instrument appearance on hardware, optical timing, precision and power remain unqualified.

**Key Characteristics:**
- Time-first segmented Digital or mechanical Instrument display.
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

The frontmatter records the source RGB values. The renderer packs them to RGB565 and alpha-blends the atlas, so sampled screenshot colors can differ through quantization.

## Typography

Rajdhani SemiBold is rasterized into the bundled ASCII atlas at the four frontmatter sizes. It is licensed under SIL OFL 1.1; see NOTICE.md and fonts/OFL.txt. There is no runtime font fallback. Unsupported characters become a question mark; use the implemented ASCII unit spelling (`us`).

Titles and navigation rows use the title role; date, diagnostic values and secondary headings use body; supporting copy and status captions use label; small Instrument scale bounds and captions use the 14px dial-label role. Coordinates are top-oriented atlas positions, not CSS baseline metrics. No browser line height or tracking is prescribed.

Digital uses original seven-segment geometry, not font glyphs. Main digits are 55 × 108 pixels; seconds are 16 × 34. Inactive segments remain visible in dim; missing time renders segment dashes. Instrument uses broad outlined pilot hands with luminous inserts and offset shadows, printed cardinal numerals with crossing ticks omitted, and 14px subdial labels. Its scales are drawn geometry, not numeric digital readouts.

## Layout

The only viewport is 410 × 502 pixels; there are no responsive breakpoints. Digital's outer bezel starts at (18, 20), measures 374 × 462 and has 42px chamfers. Its LCD field is (32, 104), 346 × 200. Diagnostics and P99 have separate compartments. Instrument's 366 × 451 case starts at (22, 27), with 48px chamfers; the main dial centers on (205, 247).

Inset utility text starts at x=42, with 326px rules, header text at y=42 and a separator at y=96. Bottom copy is inset further. Review all screens under the conservative synthetic 100px rounded-corner aperture; this is a design check, not measured panel geometry. Keep vital labels and controls away from corner cutouts.

Both faces retain the y>=430 MODE touch zone. The common header touch region is y<95. Face-selection rows begin at y=116 and y=266; their hit regions divide at y=250 and end at y=390. Keep these regions in sync with View::touch.

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

Brightness cycles from 10 through 100 percent in ten-point increments; display sleep cycles through 90, 120 and 180 seconds (90 is the default and minimum). Both rows state that they can be tapped. Save failure replaces the persistence footer with an amber retry instruction. GNSS OFF is a status, not a control. First touch or button after sleep only wakes the display. Timing stays active during display sleep; this is not a measured battery-life claim.

### Instrument face

Main hands carry time; unknown time parks them at twelve and exposes a framed NO TIME aperture. The upper calendar bridge places a source drum aperture and independent RX aperture on either side of a concentric PLL phase well. The source drum shows BLE for BLE/RC1/RC2, LOR, distinct RC9, HOLD during valid hold, and `---` when time is unavailable. No source triangle or selector ring shares the lettering. The RX jewel reports the powered LoRa receiver, not selected-source authority or fresh packets. The moon-like PLL disc is full for current lock, half for valid hold and dark for acquisition/unavailable; it is not a lunar-calendar reading.

Both source/RX apertures are 41 × 25px at (136, 142) and (234, 142), centered on (156, 154) and (254, 154). They flank the phase well at (205, 154), radius 24; its phase fill/outline share that center and radius 17. Four empty pixel columns separate each aperture edge from the phase rim. Source and RX text use atlas y=145, with visible letter pixels at y=150–158; the unavailable dash stroke occupies y=154–155. RX lettering centers on x=247 and the jewel on (265, 154). PLL is engraved above at atlas (205, 113); RADIO and LORA are advance-centered at x=156/254, atlas y=174, and share visible y=179–187. Keep these independent text/phase regions intact. Moving hands may cross them as on a mechanical watch, while fixed labels and dynamic contents retain distinct regions.

The calendar bridge is part of the Instrument face. Native previews remain synthetic; hardware appearance, continuous reliability and timing qualification are separate checks.

The Sync RMS dial is linear from `0ns` to `10us`, with 2.5us quarter divisions. Values above 10000ns park at the maximum scale and show `OVER`; exactly 10000ns has no overflow flag. Missing RMS parks red below the sweep, distinctly from overflow. Sampled P99 hold spans 1–60s with `<=1` and `>=60` bounds. Lower-bound-only hold evidence uses the unavailable parking position below the sweep; the analog dial has no MIN annotation. LoRa/RC9 never gain an unsupported sampled-hold value. Battery reserve is an E–F sector with its own unavailable parking position, not a runtime estimate. These are internal/sampled metrics, not calibrated accuracy.

### Face selection

Digital and Instrument are two selectable text rows. SELECTED copy identifies the current choice. Tapping either returns to that face and saves its selection; the last selected face opens at boot. Either horizontal swipe on a face also switches and persists the face. Resolve taps and swipes on release, so swiping across MODE cannot first activate navigation. A switch requires at least 64px horizontal travel, more than twice the vertical travel, and at most 1.5 seconds; taps require less than 20px displacement on each axis. Vertical/long gestures do not switch. Wake consumes the whole initial contact gesture, including its release. MODE taps and physical-button navigation remain available. There is no obsolete one-face preview or future-library promise.

### Display-only local hold

A fresh valid presentation snapshot anchors a local clock. Missing/expired snapshots can continue its time for less than 24 hours from that anchor, using monotonic elapsed time and retained time-zone offset. Monotonic rollback discards the anchor. This continuity never feeds a timing source, PLL, admission or precision metric. Source identity, lock, RMS and sampled hold are withdrawn: Digital shows HOLD and unavailable values; Instrument shows a half disc with unavailable precision pointers. A new valid snapshot restores current presentation. Without an anchor or after its limit, time is unavailable.

### Status labels

Green LOCK and amber WAIT/HOLD/ACQ keep text meanings independent of color. The Digital footer distinguishes LOR RX from LOR OFF. Preserve these unavailable and attention states when adding screens.

## Do's and Don'ts

- Do keep this visual authority scoped to firmware/twatch.
- Do use the bundled Rajdhani atlas and original segment geometry.
- Do preserve units, unavailable states and explicit status labels.
- Do review both faces and all utility screens at native resolution, with the rounded aperture and unavailable, local-hold, overflow, RC9 and lower-bound states, plus release-resolved swipe and wake gestures.
- Don't use synthetic preview values as hardware evidence.
- Don't convey lock, selection, saturation or failure through color alone.
- Don't treat display-only local hold as current timing authority or retain precision during it.
- Don't add web hover or responsive behavior to the native touchscreen contract.
- Don't introduce image assets for elements already drawn by the renderer.

Original documentation © 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.

The Instrument static dial is cached in a single PSRAM layer at first presentation. Subsequent redraws copy that layer and paint the moving indications; cached and uncached fixture images match pixel-for-pixel. The renderer remains single-owner.
