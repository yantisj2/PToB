---
name: PToB Pi gateway
description: Readable observations of the complete timing path
colors:
  indigo: "#243854"
  ink: "#172638"
  muted: "#4c6175"
  ground: "#eef3f7"
  paper: "#ffffff"
  line: "#cbd7e2"
  good: "#12644e"
  good-bg: "#e6f3ed"
  wait: "#805400"
  wait-bg: "#fff3d5"
  bad: "#9b332c"
  focus: "#1e68b3"
typography:
  headline:
    fontFamily: '-apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif'
    fontSize: "30px"
    fontWeight: 650
    lineHeight: 1.2
    letterSpacing: "-0.025em"
  title:
    fontSize: "18px"
    fontWeight: 650
    lineHeight: 1.35
  body:
    fontFamily: '-apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif'
    fontSize: "15px"
    lineHeight: 1.5
  data:
    fontSize: "13px"
    fontFeature: "tabular-nums"
rounded:
  panel: "8px"
  control: "6px"
spacing:
  small: "8px"
  medium: "16px"
  section: "24px"
  wide: "32px"
components:
  button:
    backgroundColor: "{colors.paper}"
    textColor: "{colors.indigo}"
    rounded: "{rounded.control}"
    padding: "9px 14px"
  panel:
    backgroundColor: "{colors.paper}"
    rounded: "{rounded.panel}"
    padding: "20px 24px"
---

# PToB dashboard design system

Copyright (c) 2026 Jonathan Yantis. MIT.

## Overview

The observation log informs a working dashboard: compact measurements, explicit
ages and an ordered timing path. Standard controls and tables lead; there is no
instrument costume. The user approved the initial appearance. Live operational
state and historical evidence remain distinct.

## Colors

Indigo owns the header and controls. Pale blue surrounds white work surfaces.
Green indicates a positively observed state; ochre indicates unavailable or
pending evidence. Red is reserved for connection failures. State always has a
text label; color never carries meaning alone.

## Typography

Use the system UI stack for this Operate surface. Headlines are 30px on desktop
and 27px on phones. Section titles are 18px; body is 15px; dense data is 13px,
reduced to 12px in narrow tables. Measurements use tabular numerals. Monospace
is limited to raw diagnostic output.

## Layout

The main container is 1328px including 24px side padding. The timing path uses
four columns, two below 900px and one below 540px. Primary work areas use a
1.3:1 two-column grid, collapsing below 900px. Phones use 16px page padding.
Tables scroll within their own container. Sections have 24px separation.

## Elevation & Depth

No shadows. Tone and a single 1px boundary distinguish work surfaces.

## Shapes

Panels use 8px corners and controls use 6px corners. The path is one shared
container with internal separators. No nested decorative cards.

## Components

The timing path is the signature: each stage independently carries validity and
freshness. It updates in place. Historical checks carry a date. Expandable
diagnostics use standard details/summary controls, while exports and refresh
use familiar links and buttons. Focus is a 3px blue outline with 4px offset.
Hover adds a pale blue control fill. Refresh is disabled while its request runs.

## Do's and Don'ts

Preserve unknown and stale states. Label fit residuals as internal. Never turn
source selection, a small residual or a running service into an accuracy claim.
Do not introduce a large status metric, ornamental waveform or animated numbers.
Keep source changes and loss visible without moving the page around.

## Timing charts

Use the existing indigo, green and ochre for named measurement series; the
legend and exact-value readout carry meaning as well as color. Two chart
columns become one on phones. Native window selection and CSV export sit in
the section header. Charts have labeled axes and keyboard sample inspection;
no decorative motion or smoothed interpolation. Statistics remain tabular and
scroll within their container.
