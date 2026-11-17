---
version: 1
slug: shadow
primary_target: firmware/twatch/main/shadow_face.inc
related_targets: [firmware/twatch/main/shadow_model.hpp, firmware/twatch/main/shadow_gnss.cpp, firmware/twatch/main/watch.hpp, firmware/twatch/main/watch.cpp]
---

# Shadow PPS face

Mode: Operate. Native embedded 410 × 502 AMOLED; fourth-face extension scoped to T-Watch Ultra.

## Direction contract

THESIS: An original Casio/Garmin-inspired GNSS comparison watch makes mesh time, satellite reception and independent physical PPS evidence legible at a glance. No brand marks.
OWN-WORLD: Graphite case, pale segmented time, lime activity, blue-grey labels and quiet rules. The six dedicated Shadow RGB tokens live in DESIGN.md frontmatter; shared black and amber remain surround and attention colors. Digital, Instrument, Solar and shared utility screens retain their established worlds. Use the bundled Rajdhani atlas and native RGB565 geometry, with no new runtime fonts or raster artwork.
STORY: Swipe or select Shadow, let GNSS search, observe actual satellites and physical PPS, wait ten phase-blind settling seconds, then read signed MEAN and P99 ABS with smaller RMS/N. Leaving Shadow, opening an app, sleeping or maintenance removes the GNSS main-power request while preserving backup power. GNSS never admits a mesh, disciplines the clock, trains the servo, aids/configures the receiver or changes Solar's fixed location.
FIRST VIEWPORT: Date and segmented time above the independent PPS/mesh-source strip; an actual azimuth/elevation plot beside used/view counts and twelve C/N0 bars; explicit status; SCAN / FIX / PPS / 10s / LIVE progression; signed MEAN and P99 ABS; small RMS/sample count; GNSS AUTO and safe MODE control. Unknown evidence stays `--`; coarse P99 tails carry `~`.
FORM: Brief-pinned code-led addition, using the approved native preview rather than an image comp. No concept tournament or HTML/CSS detector applies to this embedded renderer. The only viewport is 410 × 502; the conservative 100px rounded aperture is a design check, not measured panel geometry.
FINISH: Unreviewed and undocumented is unfinished. Native finish review, verdict and token-bearing documentation close source/render preparation; installation and physical qualification remain separate.

## Implemented contract

The renderer draws only satellite positions marked `position_known`; missing azimuth/elevation never becomes a fabricated sky dot. C/N0 remains independently usable. The four-second acquisition sweep appears during powered search without a fix and carries no satellite dots. The PPS pulse is tied to an actual captured edge, with a 220ms flash and a 1.5-second recency bound; a sweep or clock-second transition cannot trigger it.

Collection requires current selected fresh BLE/RC1/RC2 evidence plus an independent GNSS fix/PPS reference. LoRa, RC9, local HOLD, source expiry and stale presentation expose no retained BLE population. Source/scope or reference changes, PPS gaps and a new visit reset the phase-blind ten-second window. No phase trimming or outlier removal is used. MEAN is signed offset, P99 ABS is absolute-error percentile and RMS/N is secondary; the other faces retain their different sampled-hold P99 meaning. Receiver bias, empirical association and absolute UTC remain unqualified.

Face selection has four 80px rows: Digital 105<=y<185, Instrument 185<=y<265, Solar 265<=y<345 and Shadow 345<=y<425. Left swipes advance Digital → Instrument → Solar → Shadow → Digital; right reverses modulo four. Selection persists, swipe/tap handling resolves on release, the waking gesture is consumed and the y>=430 MODE zone remains available.

## Completed native review — 2026-11-17

Initial finish disposition: **pass** following the source-gating and invalid-sky fixes. The native preview was approved before separately authorized flash/testing. This review established source/render quality; subsequent control correction and installed limitations are recorded below.

Documentation inspection used `review/shadow-states.png` and `review/shadow-aperture.png`. Native synthetic fixtures cover search, fix/wait PPS, settling, measuring, wait mesh, lost, fault, coarse tails, LoRa, HOLD and invalid sky positions. The aperture capture preserves the clock, comparison columns and MODE clearance. These fixtures establish source layout and state presentation, not photographs, measured GNSS power, installed reliability, independent precision or absolute UTC accuracy. No shipping raster artwork was added, so no artwork provenance record is needed.

PRODUCT.md and DESIGN.md now include the fourth face; `.impeccable/design.json` adds Shadow metadata and current four-face navigation. Pre-existing unrelated sleep/local-hold/pointer drift in the sidecar and incumbent surface briefs remains unchanged and is not adopted as Shadow behavior.

Original documentation © 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.

## Control reliability correction

The acknowledged GNSS power transition fences the main owner; it cannot consume
a start generation while PMU I/O is pending. Retained startup/cleanup/health
failures inhibit a new owner and preserve first evidence. USB maintenance uses a
bounded control response independent of detailed watch statistics and reads
actual rail/UART/capture retirement. Offline controller and protocol regressions
precede any further installation; the earlier preview approval does not establish
physical USB, reception, accuracy or power behavior.

Post-correction documentation verification matched the unchanged approved renderer
and the source-bound native-05 v0.1.8 candidate. The independent finish disposition
is **ship at scored offline fixes only**. The bounded read-only `v12 control`
response and compact mutation ACK/refusal path preserve the original protocol
binding and deadlines. Maintenance checks actual GNSS rail/UART/capture, fine and
display retirement independently of delayed presentation; detailed Shadow
statistics and receive counters remain on a dedicated response. The production
formatter/strict-parser, controller, SDK and stack checks pass offline. Product,
design and sidecar contracts remain consistent without palette or layout changes;
unrelated pre-existing drift remains preserved.

Native-05 v0.1.8 is **not installed**. Installed native-03 v0.1.7 has verified
application bytes but retains its later USB status/update-entry failure; no
subsequent reset or flash is implied. GNSS lock, independent precision and power
remain unverified. Indoor control/reception and maintenance-return checks must
pass before an outdoor fix/PPS and ten-second collection trial can establish new
hardware observations.
